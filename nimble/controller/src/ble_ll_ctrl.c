/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "nimble/ble.h"
#include "nimble/nimble_opt.h"
#include "nimble/hci_common.h"
#include "controller/ble_ll.h"
#include "controller/ble_ll_hci.h"
#include "controller/ble_ll_ctrl.h"
#include "ble_ll_conn_priv.h"

/*
 * XXX: TODO
 *  1) Do I need to keep track of which procedures have already been done?
 *     Do I need to worry about repeating procedures?
 *  2) Should we create pool of control pdu's?. Dont need more
 *  than the # of connections and can probably deal with quite a few less
 *  if we have lots of connections.
 *  3) What about procedures that have been completed but try to restart?
 *  4) NOTE: there is a supported features procedure. However, in the case
 *  of data length extension, if the receiving device does not understand
 *  the pdu or it does not support data length extension, the LL_UNKNOWN_RSP
 *  pdu is sent. That needs to be processed...
 *  5) We are supposed to remember when we do the data length update proc if
 *  the device sent us an unknown rsp. We should not send it another len req.
 *  Implement this how? Through remote supported features?
 *  6) Remember: some procedures dont have timeout rules.
 *  7) Says that we should reset procedure timer whenever a LL control pdu
 *  is queued for transmission. I dont get it... do some procedures send
 *  multiple packets? I guess so.
 *  8) How to count control pdus sent. DO we count enqueued + sent, or only
 *  sent (actually attempted to tx). Do we count failures? How?
 *  9) NOTE: we are not supposed to send a REJECT_IND_EXT unless we know the
 *  slave supports that feature
 */

/*
 * XXX: I definitely have an issue with control procedures and connection
 * param request procedure and connection update procedure. This was
 * noted when receiving an unknown response. Right now, I am getting confused
 * with connection parameter request and updates regarding which procedures
 * are running. So I need to go look through all the code and see where I
 * used the request procedure and the update procedure and make sure I am doing
 * the correct thing.
 */

/*
 * This array contains the length of the CtrData field in LL control PDU's.
 * Note that there is a one byte opcode which precedes this field in the LL
 * control PDU, so total data channel payload length for the control pdu is
 * one greater.
 */
const uint8_t g_ble_ll_ctrl_pkt_lengths[BLE_LL_CTRL_OPCODES] =
{
    11, 7, 1, 22, 12, 0, 0, 1, 8, 8, 0, 0, 5, 1, 8, 23, 23, 2, 0, 0, 8, 8
};

static int
ble_ll_ctrl_chk_supp_bytes(uint16_t bytes)
{
    int rc;

    if ((bytes < BLE_LL_CONN_SUPP_BYTES_MIN) ||
        (bytes > BLE_LL_CONN_SUPP_BYTES_MAX)) {
        rc = 0;
    } else {
        rc = 1;
    }

    return rc;
}

static int
ble_ll_ctrl_chk_supp_time(uint16_t t)
{
    int rc;

    if ((t < BLE_LL_CONN_SUPP_TIME_MIN) || (t > BLE_LL_CONN_SUPP_TIME_MAX)) {
        rc = 0;
    } else {
        rc = 1;
    }

    return rc;
}

static int
ble_ll_ctrl_len_proc(struct ble_ll_conn_sm *connsm, uint8_t *dptr)
{
    int rc;
    struct ble_ll_len_req ctrl_req;

    /* Extract parameters and check if valid */
    ctrl_req.max_rx_bytes = le16toh(dptr);
    ctrl_req.max_rx_time = le16toh(dptr + 2);
    ctrl_req.max_tx_bytes = le16toh(dptr + 4);
    ctrl_req.max_tx_time = le16toh(dptr + 6);

    if (!ble_ll_ctrl_chk_supp_bytes(ctrl_req.max_rx_bytes) ||
        !ble_ll_ctrl_chk_supp_bytes(ctrl_req.max_tx_bytes) ||
        !ble_ll_ctrl_chk_supp_time(ctrl_req.max_tx_time) ||
        !ble_ll_ctrl_chk_supp_time(ctrl_req.max_rx_time)) {
        rc = 1;
    } else {
        /* Update the connection with the new parameters */
        ble_ll_conn_datalen_update(connsm, &ctrl_req);
        rc = 0;
    }

    return rc;
}

/**
 * Called when we receive either a connection parameter request or response.
 *
 * @param connsm
 * @param dptr
 * @param rspbuf
 * @param opcode
 *
 * @return int
 */
static int
ble_ll_ctrl_conn_param_pdu_proc(struct ble_ll_conn_sm *connsm, uint8_t *dptr,
                                uint8_t *rspbuf, uint8_t opcode)
{
    int rc;
    int indicate;
    uint8_t rsp_opcode;
    uint8_t ble_err;
    struct ble_ll_conn_params *req;
    struct hci_conn_update *hcu;

    /* Extract parameters and check if valid */
    req = &connsm->conn_cp;
    req->interval_min = le16toh(dptr);
    req->interval_max = le16toh(dptr + 2);
    req->latency = le16toh(dptr + 4);
    req->timeout = le16toh(dptr + 6);
    req->pref_periodicity = dptr[8];
    req->ref_conn_event_cnt  = le16toh(dptr + 9);
    req->offset0 = le16toh(dptr + 11);
    req->offset1 = le16toh(dptr + 13);
    req->offset2 = le16toh(dptr + 15);
    req->offset3 = le16toh(dptr + 17);
    req->offset4 = le16toh(dptr + 19);
    req->offset5 = le16toh(dptr + 21);

    /* Check if parameters are valid */
    ble_err = BLE_ERR_SUCCESS;
    rc = ble_ll_conn_hci_chk_conn_params(req->interval_min,
                                         req->interval_max,
                                         req->latency,
                                         req->timeout);
    if (rc) {
        ble_err = BLE_ERR_INV_LMP_LL_PARM;
        goto conn_param_pdu_exit;
    }

    /*
     * Check if there is a requested change to either the interval, timeout
     * or latency. If not, this may just be an anchor point change and we do
     * not have to notify the host.
     *  XXX: what if we dont like the parameters? When do we check that out?
     */
    indicate = 1;
    if (opcode == BLE_LL_CTRL_CONN_PARM_REQ) {
        if ((connsm->conn_itvl >= req->interval_min) &&
            (connsm->conn_itvl <= req->interval_max) &&
            (connsm->supervision_tmo == req->timeout) &&
            (connsm->slave_latency == req->latency)) {
            indicate = 0;
            goto conn_parm_req_do_indicate;
        }
    }

    /*
     * A change has been requested. Is it within the values specified by
     * the host? Note that for a master we will not be processing a
     * connect param request from a slave if we are currently trying to
     * update the connection parameters. This means that the previous
     * check is all we need for a master (when receiving a request).
     */
    if ((connsm->conn_role == BLE_LL_CONN_ROLE_SLAVE) ||
        (opcode == BLE_LL_CTRL_CONN_PARM_RSP)) {
        /*
         * Not sure what to do about the slave. It is possible that the
         * current connection parameters are not the same ones as the local host
         * has provided? Not sure what to do here. Do we need to remember what
         * host sent us? For now, I will assume that we need to remember what
         * the host sent us and check it out.
         */
        hcu = &connsm->conn_param_req;
        if (hcu->handle != 0) {
            if (!((req->interval_min < hcu->conn_itvl_min) ||
                  (req->interval_min > hcu->conn_itvl_max) ||
                  (req->interval_max < hcu->conn_itvl_min) ||
                  (req->interval_max > hcu->conn_itvl_max) ||
                  (req->latency != hcu->conn_latency) ||
                  (req->timeout != hcu->supervision_timeout))) {
                indicate = 0;
            }
        }
    }

conn_parm_req_do_indicate:
    /*
     * XXX: are the connection update parameters acceptable? If not, we will
     * need to know before we indicate to the host that they are acceptable.
     */
    if (indicate) {
        /*
         * Send event to host. At this point we leave and wait to get
         * an answer.
         */
        /* XXX: what about masked out event? */
        ble_ll_hci_ev_rem_conn_parm_req(connsm, req);
        connsm->host_reply_opcode = opcode;
        connsm->csmflags.cfbit.awaiting_host_reply = 1;
        rsp_opcode = 255;
    } else {
        /* Create reply to connection request */
        rsp_opcode = ble_ll_ctrl_conn_param_reply(connsm, rspbuf, req);
    }

conn_param_pdu_exit:
    if (ble_err) {
        rsp_opcode = BLE_LL_CTRL_REJECT_IND_EXT;
        rspbuf[1] = opcode;
        rspbuf[2] = ble_err;
    }
    return rsp_opcode;
}

/**
 * Called to process and UNKNOWN_RSP LL control packet.
 *
 * Context: Link Layer Task
 *
 * @param dptr
 */
static void
ble_ll_ctrl_proc_unk_rsp(struct ble_ll_conn_sm *connsm, uint8_t *dptr)
{
    uint8_t ctrl_proc;
    uint8_t opcode;

    /* Get opcode of unknown LL control frame */
    opcode = dptr[0];

    /* XXX: add others here */
    /* Convert opcode to control procedure id */
    switch (opcode) {
    case BLE_LL_CTRL_LENGTH_REQ:
        ctrl_proc = BLE_LL_CTRL_PROC_DATA_LEN_UPD;
        break;
    case BLE_LL_CTRL_CONN_UPDATE_REQ:
        ctrl_proc = BLE_LL_CTRL_PROC_CONN_UPDATE;
        break;
    case BLE_LL_CTRL_SLAVE_FEATURE_REQ:
        ctrl_proc = BLE_LL_CTRL_PROC_FEATURE_XCHG;
        break;
    case BLE_LL_CTRL_CONN_PARM_RSP:
    case BLE_LL_CTRL_CONN_PARM_REQ:
        ctrl_proc = BLE_LL_CTRL_PROC_CONN_PARAM_REQ;
        break;
    default:
        ctrl_proc = BLE_LL_CTRL_PROC_NUM;
        break;
    }

    /* XXX: are there any other events that we need to send when we get
       the unknown response? */
    /* If we are running this one currently, stop it */
    if (connsm->cur_ctrl_proc == ctrl_proc) {
        /* Stop the control procedure */
        ble_ll_ctrl_proc_stop(connsm, ctrl_proc);
        if (ctrl_proc == BLE_LL_CTRL_PROC_CONN_PARAM_REQ) {
            ble_ll_hci_ev_conn_update(connsm, BLE_ERR_UNSUPP_REM_FEATURE);
        } else if (ctrl_proc == BLE_LL_CTRL_PROC_FEATURE_XCHG) {
            /* XXX: should only get this if a slave initiated this */
            ble_ll_hci_ev_rd_rem_used_feat(connsm, BLE_ERR_UNSUPP_REM_FEATURE);
        }
    }
}

/**
 * Create a link layer length request or length response PDU.
 *
 * NOTE: this function does not set the LL data pdu header nor does it
 * set the opcode in the buffer.
 *
 * @param connsm
 * @param dptr: Pointer to where control pdu payload starts
 */
static void
ble_ll_ctrl_datalen_upd_make(struct ble_ll_conn_sm *connsm, uint8_t *dptr)
{
    htole16(dptr + 1, connsm->max_rx_octets);
    htole16(dptr + 3, connsm->max_rx_time);
    htole16(dptr + 5, connsm->max_tx_octets);
    htole16(dptr + 7, connsm->max_tx_time);
}

/**
 * Called to make a connection parameter request or response control pdu.
 *
 * @param connsm
 * @param dptr Pointer to start of data. NOTE: the opcode is not part
 *             of the data.
 */
static void
ble_ll_ctrl_conn_param_pdu_make(struct ble_ll_conn_sm *connsm, uint8_t *dptr,
                                struct ble_ll_conn_params *req)
{
    uint16_t offset;
    struct hci_conn_update *hcu;

    /* If we were passed in a request, we use the parameters from the request */
    if (req) {
        htole16(dptr, req->interval_min);
        htole16(dptr + 2, req->interval_max);
        htole16(dptr + 4, req->latency);
        htole16(dptr + 6, req->timeout);
    } else {
        hcu = &connsm->conn_param_req;
        /* The host should have provided the parameters! */
        assert(hcu->handle != 0);
        htole16(dptr, hcu->conn_itvl_min);
        htole16(dptr + 2, hcu->conn_itvl_max);
        htole16(dptr + 4, hcu->conn_latency);
        htole16(dptr + 6, hcu->supervision_timeout);
    }

    /* XXX: NOTE: if interval min and interval max are != to each
     * other this value should be set to non-zero. I think this
     * applies only when an offset field is set. See section 5.1.7.1 pg 103
     * Vol 6 Part B.
     */
    /* XXX: for now, set periodicity to 0 */
    dptr[8] = 0;

    /* XXX: deal with reference event count. what to put here? */
    htole16(dptr + 9, connsm->event_cntr);

    /* XXX: For now, dont use offsets */
    offset = 0xFFFF;
    htole16(dptr + 11, offset);
    htole16(dptr + 13, offset);
    htole16(dptr + 15, offset);
    htole16(dptr + 17, offset);
    htole16(dptr + 19, offset);
    htole16(dptr + 21, offset);
}

static void
ble_ll_ctrl_version_ind_make(struct ble_ll_conn_sm *connsm, uint8_t *pyld)
{
    /* Set flag to denote we have sent/received this */
    connsm->csmflags.cfbit.version_ind_sent = 1;

    /* Fill out response */
    pyld[0] = BLE_HCI_VER_BCS_4_2;
    htole16(pyld + 1, NIMBLE_OPT_LL_MFRG_ID);
    htole16(pyld + 3, BLE_LL_SUB_VERS_NR);
}

/**
 * Called to make a LL control channel map request PDU.
 *
 * @param connsm    Pointer to connection state machine
 * @param pyld      Pointer to payload of LL control PDU
 */
static void
ble_ll_ctrl_chanmap_req_make(struct ble_ll_conn_sm *connsm, uint8_t *pyld)
{
    /* Copy channel map that host desires into request */
    memcpy(pyld, g_ble_ll_conn_params.master_chan_map, BLE_LL_CONN_CHMAP_LEN);
    memcpy(connsm->req_chanmap, pyld, BLE_LL_CONN_CHMAP_LEN);

    /* Place instant into request */
    connsm->chanmap_instant = connsm->event_cntr + connsm->slave_latency + 6 + 1;
    htole16(pyld + BLE_LL_CONN_CHMAP_LEN, connsm->chanmap_instant);

    /* Set scheduled flag */
    connsm->csmflags.cfbit.chanmap_update_scheduled = 1;
}

/**
 * Called to make a connection update request LL control PDU
 *
 * Context: Link Layer
 *
 * @param connsm
 * @param rsp
 */
static void
ble_ll_ctrl_conn_upd_make(struct ble_ll_conn_sm *connsm, uint8_t *pyld,
                          struct ble_ll_conn_params *cp)
{
    uint16_t instant;
    uint32_t dt;
    uint32_t num_old_ce;
    uint32_t new_itvl_usecs;
    uint32_t old_itvl_usecs;
    struct hci_conn_update *hcu;
    struct ble_ll_conn_upd_req *req;

    /*
     * Set instant. We set the instant to the current event counter plus
     * the amount of slave latency as the slave may not be listening
     * at every connection interval and we are not sure when the connect
     * request will actually get sent. We add one more event plus the
     * minimum as per the spec of 6 connection events.
     */
    instant = connsm->event_cntr + connsm->slave_latency + 6 + 1;

    /*
     * XXX: This should change in the future, but for now we will just
     * start the new instant at the same anchor using win offset 0.
     */
    /* Copy parameters in connection update structure */
    hcu = &connsm->conn_param_req;
    req = &connsm->conn_update_req;
    if (cp) {
        /* XXX: so we need to make the new anchor point some time away
         * from txwinoffset by some amount of msecs. Not sure how to do
           that here. We dont need to, but we should. */
        /* Calculate offset from requested offsets (if any) */
        if (cp->offset0 != 0xFFFF) {
            new_itvl_usecs = cp->interval_max * BLE_LL_CONN_ITVL_USECS;
            old_itvl_usecs = connsm->conn_itvl * BLE_LL_CONN_ITVL_USECS;
            if ((int16_t)(cp->ref_conn_event_cnt - instant) >= 0) {
                num_old_ce = cp->ref_conn_event_cnt - instant;
                dt = old_itvl_usecs * num_old_ce;
                dt += (cp->offset0 * BLE_LL_CONN_ITVL_USECS);
                dt = dt % new_itvl_usecs;
            } else {
                num_old_ce = instant - cp->ref_conn_event_cnt;
                dt = old_itvl_usecs * num_old_ce;
                dt -= (cp->offset0 * BLE_LL_CONN_ITVL_USECS);
                dt = dt % new_itvl_usecs;
                dt = new_itvl_usecs - dt;
            }
            req->winoffset = dt / BLE_LL_CONN_TX_WIN_USECS;
        } else {
            req->winoffset = 0;
        }
        req->interval = cp->interval_max;
        req->timeout = cp->timeout;
        req->latency = cp->latency;
        req->winsize = 1;
    } else {
        req->interval = hcu->conn_itvl_max;
        req->timeout = hcu->supervision_timeout;
        req->latency = hcu->conn_latency;
        req->winoffset = 0;
        req->winsize = connsm->tx_win_size;
    }
    req->instant = instant;

    /* XXX: make sure this works for the connection parameter request proc. */
    pyld[0] = req->winsize;
    htole16(pyld + 1, req->winoffset);
    htole16(pyld + 3, req->interval);
    htole16(pyld + 5, req->latency);
    htole16(pyld + 7, req->timeout);
    htole16(pyld + 9, instant);

    /* Set flag in state machine to denote we have scheduled an update */
    connsm->csmflags.cfbit.conn_update_scheduled = 1;
}

/**
 * Called to respond to a LL control PDU connection parameter request or
 * response.
 *
 * @param connsm
 * @param rsp
 * @param req
 *
 * @return uint8_t
 */
uint8_t
ble_ll_ctrl_conn_param_reply(struct ble_ll_conn_sm *connsm, uint8_t *rsp,
                             struct ble_ll_conn_params *req)
{
    uint8_t rsp_opcode;

    if (connsm->conn_role == BLE_LL_CONN_ROLE_SLAVE) {
        /* Create a connection parameter response */
        ble_ll_ctrl_conn_param_pdu_make(connsm, rsp + 1, req);
        rsp_opcode = BLE_LL_CTRL_CONN_PARM_RSP;
    } else {
        /* Create a connection update pdu */
        ble_ll_ctrl_conn_upd_make(connsm, rsp + 1, req);
        rsp_opcode = BLE_LL_CTRL_CONN_UPDATE_REQ;
    }

    return rsp_opcode;
}

/**
 * Called when we receive a connection update event
 *
 * @param connsm
 * @param dptr
 * @param rspbuf
 *
 * @return int
 */
static int
ble_ll_ctrl_rx_conn_update(struct ble_ll_conn_sm *connsm, uint8_t *dptr,
                           uint8_t *rspbuf)
{
    uint8_t rsp_opcode;
    uint16_t conn_events;
    struct ble_ll_conn_upd_req *reqdata;

    /* Only a slave should receive this */
    if (connsm->conn_role == BLE_LL_CONN_ROLE_MASTER) {
        return BLE_ERR_MAX;
    }

    /* Retrieve parameters */
    reqdata = &connsm->conn_update_req;
    reqdata->winsize = dptr[0];
    reqdata->winoffset = le16toh(dptr + 1);
    reqdata->interval = le16toh(dptr + 3);
    reqdata->latency = le16toh(dptr + 5);
    reqdata->timeout = le16toh(dptr + 7);
    reqdata->instant = le16toh(dptr + 9);

    /* XXX: validate them at some point. If they dont check out, we
       return the unknown response */

    /* If instant is in the past, we have to end the connection */
    conn_events = (reqdata->instant - connsm->event_cntr) & 0xFFFF;
    if (conn_events >= 32767) {
        ble_ll_conn_timeout(connsm, BLE_ERR_INSTANT_PASSED);
        rsp_opcode = BLE_ERR_MAX;
    } else {
        connsm->csmflags.cfbit.conn_update_scheduled = 1;
    }

    return rsp_opcode;
}

/**
 * Called when we receive a feature request or a slave initiated feature
 * request.
 *
 *
 * @param connsm
 * @param dptr
 * @param rspbuf
 * @param opcode
 *
 * @return int
 */
static int
ble_ll_ctrl_rx_feature_req(struct ble_ll_conn_sm *connsm, uint8_t *dptr,
                           uint8_t *rspbuf, uint8_t opcode)
{
    uint8_t rsp_opcode;

    /*
     * Only accept slave feature requests if we are a master and feature
     * requests if we are a slave.
     */
    if (opcode ==  BLE_LL_CTRL_SLAVE_FEATURE_REQ) {
        if (connsm->conn_role != BLE_LL_CONN_ROLE_MASTER) {
            return BLE_LL_CTRL_UNKNOWN_RSP;
        }
    } else {
        /* XXX: not sure this is correct but do it anyway */
        if (connsm->conn_role != BLE_LL_CONN_ROLE_SLAVE) {
            return BLE_LL_CTRL_UNKNOWN_RSP;
        }
    }

    /* Set common features and reply */
    rsp_opcode = BLE_LL_CTRL_FEATURE_RSP;
    connsm->common_features = dptr[0] & ble_ll_read_supp_features();
    memset(rspbuf + 1, 0, 8);
    rspbuf[1] = connsm->common_features;

    return rsp_opcode;
}

/**
 *
 *
 * Context: Link Layer task
 *
 * @param connsm
 * @param dptr
 * @param rspbuf
 *
 * @return int
 */
static int
ble_ll_ctrl_rx_conn_param_req(struct ble_ll_conn_sm *connsm, uint8_t *dptr,
                              uint8_t *rspbuf)
{
    uint8_t rsp_opcode;

    /*
     * This is not in the specification per se but it simplifies the
     * implementation. If we get a connection parameter request and we
     * are awaiting a reply from the host, simply ignore the request. This
     * might not be a good idea if the parameters are different, but oh
     * well. This is not expected to happen anyway. A return of BLE_ERR_MAX
     * means that we will simply discard the connection parameter request
     */
    if (connsm->csmflags.cfbit.awaiting_host_reply) {
        return BLE_ERR_MAX;
    }

    /* XXX: remember to deal with this on the master: if the slave has
     * initiated a procedure we may have received its connection parameter
     * update request and have signaled the host with an event. If that
     * is the case, we will need to drop the host command when we get it
       and also clear any applicable states. */

    /* XXX: Read 5.3 again. There are multiple control procedures that might
     * be pending (a connection update) that will cause collisions and the
       behavior below. */
    /*
     * Check for procedure collision (Vol 6 PartB 5.3). If we are a slave
     * and we receive a request we "consider the slave initiated
     * procedure as complete". This means send a connection update complete
     * event (with error).
     *
     * If a master, we send reject with a
     * transaction collision error code.
     */
    if (IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CONN_PARAM_REQ)) {
        if (connsm->conn_role == BLE_LL_CONN_ROLE_SLAVE) {
            ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CONN_PARAM_REQ);
            ble_ll_hci_ev_conn_update(connsm, BLE_ERR_LMP_COLLISION);
        } else {
            /* The master sends reject ind ext w/error code 0x23 */
            rsp_opcode = BLE_LL_CTRL_REJECT_IND_EXT;
            rspbuf[1] = BLE_LL_CTRL_CONN_PARM_REQ;
            rspbuf[2] = BLE_ERR_LMP_COLLISION;
            return rsp_opcode;
        }
    }

    /*
     * If we are a master and we currently performing a channel map
     * update procedure we need to return an error
     */
    if ((connsm->conn_role == BLE_LL_CONN_ROLE_MASTER) &&
        (connsm->csmflags.cfbit.chanmap_update_scheduled)) {
        rsp_opcode = BLE_LL_CTRL_REJECT_IND_EXT;
        rspbuf[1] = BLE_LL_CTRL_CONN_PARM_REQ;
        rspbuf[2] = BLE_ERR_DIFF_TRANS_COLL;
        return rsp_opcode;
    }

    /* Process the received connection parameter request */
    rsp_opcode = ble_ll_ctrl_conn_param_pdu_proc(connsm, dptr, rspbuf,
                                                 BLE_LL_CTRL_CONN_PARM_REQ);
    return rsp_opcode;
}

static int
ble_ll_ctrl_rx_conn_param_rsp(struct ble_ll_conn_sm *connsm, uint8_t *dptr,
                              uint8_t *rspbuf)
{
    uint8_t rsp_opcode;

    /* A slave should never receive this response */
    if (connsm->conn_role == BLE_LL_CONN_ROLE_SLAVE) {
        return BLE_ERR_MAX;
    }

    /*
     * This case should never happen! It means that the slave initiated a
     * procedure and the master initiated one as well. If we do get in this
     * state just clear the awaiting reply. The slave will hopefully stop its
     * procedure when we reply.
     */
    if (connsm->csmflags.cfbit.awaiting_host_reply) {
        connsm->csmflags.cfbit.awaiting_host_reply = 0;
    }

    /* If we receive a response and no procedure is pending, just leave */
    if (!IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CONN_PARAM_REQ)) {
        return BLE_ERR_MAX;
    }

    /* Process the received connection parameter response */
    rsp_opcode = ble_ll_ctrl_conn_param_pdu_proc(connsm, dptr, rspbuf,
                                                 BLE_LL_CTRL_CONN_PARM_RSP);
    return rsp_opcode;
}

/**
 * Called to process the LL control PDU VERSION_IND
 *
 * Context: Link Layer task
 *
 * @param connsm
 * @param dptr
 * @param rspbuf
 *
 * @return int
 */
static int
ble_ll_ctrl_rx_version_ind(struct ble_ll_conn_sm *connsm, uint8_t *dptr,
                           uint8_t *rspbuf)
{
    uint8_t rsp_opcode;

    /* Process the packet */
    connsm->vers_nr = dptr[0];
    connsm->comp_id = le16toh(dptr + 1);
    connsm->sub_vers_nr = le16toh(dptr + 3);
    connsm->csmflags.cfbit.rxd_version_ind = 1;

    rsp_opcode = BLE_ERR_MAX;
    if (!connsm->csmflags.cfbit.version_ind_sent) {
        rsp_opcode = BLE_LL_CTRL_VERSION_IND;
        ble_ll_ctrl_version_ind_make(connsm, rspbuf);
    }

    /* Stop the control procedure */
    if (IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_VERSION_XCHG)) {
        ble_ll_hci_ev_rd_rem_ver(connsm, BLE_ERR_SUCCESS);
        ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_VERSION_XCHG);
    }
    return rsp_opcode;
}

/**
 * Called to process a received channel map request control pdu.
 *
 * Context: Link Layer task
 *
 * @param connsm
 * @param dptr
 */
static void
ble_ll_ctrl_rx_chanmap_req(struct ble_ll_conn_sm *connsm, uint8_t *dptr)
{
    uint16_t instant;
    uint16_t conn_events;

    /* If instant is in the past, we have to end the connection */
    if (connsm->conn_role == BLE_LL_CONN_ROLE_SLAVE) {
        instant = le16toh(dptr + BLE_LL_CONN_CHMAP_LEN);
        conn_events = (instant - connsm->event_cntr) & 0xFFFF;
        if (conn_events >= 32767) {
            ble_ll_conn_timeout(connsm, BLE_ERR_INSTANT_PASSED);
        } else {
            connsm->chanmap_instant = instant;
            memcpy(connsm->req_chanmap, dptr, BLE_LL_CONN_CHMAP_LEN);
            connsm->csmflags.cfbit.chanmap_update_scheduled = 1;
        }
    }
}

/**
 * Callback when LL control procedure times out (for a given connection). If
 * this is called, it means that we need to end the connection because it
 * has not responded to a LL control request.
 *
 * Context: Link Layer
 *
 * @param arg Pointer to connection state machine.
 */
void
ble_ll_ctrl_proc_rsp_timer_cb(void *arg)
{
    /* Control procedure has timed out. Kill the connection */
    ble_ll_conn_timeout((struct ble_ll_conn_sm *)arg, BLE_ERR_LMP_LL_RSP_TMO);
}

/**
 * Initiate LL control procedure.
 *
 * This function is called to obtain a mbuf to send a LL control PDU. The data
 * channel PDU header is not part of the mbuf data; it is part of the BLE
 * header (which is part of the mbuf).
 *
 * Context: LL task.
 *
 * @param connsm
 * @param ctrl_proc
 */
static struct os_mbuf *
ble_ll_ctrl_proc_init(struct ble_ll_conn_sm *connsm, int ctrl_proc)
{
    uint8_t len;
    uint8_t opcode;
    uint8_t *dptr;
    struct os_mbuf *om;

    /* Get an mbuf for the control pdu */
    om = os_msys_get_pkthdr(BLE_LL_CTRL_MAX_PDU_LEN, sizeof(struct ble_mbuf_hdr));

    if (om) {
        dptr = om->om_data;

        switch (ctrl_proc) {
        case BLE_LL_CTRL_PROC_CONN_UPDATE:
            opcode = BLE_LL_CTRL_CONN_UPDATE_REQ;
            ble_ll_ctrl_conn_upd_make(connsm, dptr + 1, NULL);
            break;
        case BLE_LL_CTRL_PROC_CHAN_MAP_UPD:
            opcode = BLE_LL_CTRL_CHANNEL_MAP_REQ;
            ble_ll_ctrl_chanmap_req_make(connsm, dptr + 1);
            break;
        case BLE_LL_CTRL_PROC_FEATURE_XCHG:
            if (connsm->conn_role == BLE_LL_CONN_ROLE_MASTER) {
                opcode = BLE_LL_CTRL_FEATURE_REQ;
            } else {
                opcode = BLE_LL_CTRL_SLAVE_FEATURE_REQ;
            }
            dptr[1] = ble_ll_read_supp_features();
            break;
        case BLE_LL_CTRL_PROC_VERSION_XCHG:
            opcode = BLE_LL_CTRL_VERSION_IND;
            ble_ll_ctrl_version_ind_make(connsm, dptr + 1);
            break;
        case BLE_LL_CTRL_PROC_TERMINATE:
            opcode = BLE_LL_CTRL_TERMINATE_IND;
            dptr[1] = connsm->disconnect_reason;
            break;
        case BLE_LL_CTRL_PROC_CONN_PARAM_REQ:
            opcode = BLE_LL_CTRL_CONN_PARM_REQ;
            ble_ll_ctrl_conn_param_pdu_make(connsm, dptr + 1, NULL);
            break;
        case BLE_LL_CTRL_PROC_DATA_LEN_UPD:
            opcode = BLE_LL_CTRL_LENGTH_REQ;
            ble_ll_ctrl_datalen_upd_make(connsm, dptr);
            break;
        default:
            assert(0);
            break;
        }

        /* Set llid, length and opcode */
        dptr[0] = opcode;
        len = g_ble_ll_ctrl_pkt_lengths[opcode] + 1;

        /* Add packet to transmit queue of connection */
        ble_ll_conn_enqueue_pkt(connsm, om, BLE_LL_LLID_CTRL, len);
    }

    return om;
}

/**
 * Called to determine if the pdu is a TERMINATE_IND
 *
 * @param hdr
 * @param opcode
 *
 * @return int
 */
int
ble_ll_ctrl_is_terminate_ind(uint8_t hdr, uint8_t opcode)
{
    int rc;

    rc = 0;
    if ((hdr & BLE_LL_DATA_HDR_LLID_MASK) == BLE_LL_LLID_CTRL) {
        if (opcode == BLE_LL_CTRL_TERMINATE_IND) {
            rc = 1;
        }
    }
    return rc;
}

/**
 * Called to determine if the pdu is a TERMINATE_IND
 *
 * @param hdr
 * @param opcode
 *
 * @return int
 */
int
ble_ll_ctrl_is_reject_ind_ext(uint8_t hdr, uint8_t opcode)
{
    int rc;

    rc = 0;
    if ((hdr & BLE_LL_DATA_HDR_LLID_MASK) == BLE_LL_LLID_CTRL) {
        if (opcode == BLE_LL_CTRL_REJECT_IND_EXT) {
            rc = 1;
        }
    }
    return rc;
}


/**
 * Stops the LL control procedure indicated by 'ctrl_proc'.
 *
 * Context: Link Layer task
 *
 * @param connsm
 * @param ctrl_proc
 */
void
ble_ll_ctrl_proc_stop(struct ble_ll_conn_sm *connsm, int ctrl_proc)
{
    if (connsm->cur_ctrl_proc == ctrl_proc) {
        os_callout_stop(&connsm->ctrl_proc_rsp_timer.cf_c);
        connsm->cur_ctrl_proc = BLE_LL_CTRL_PROC_IDLE;
    }
    CLR_PENDING_CTRL_PROC(connsm, ctrl_proc);

    /* If there are others, start them */
    ble_ll_ctrl_chk_proc_start(connsm);
}

/**
 * Called to start the terminate procedure.
 *
 * Context: Link Layer task.
 *
 * @param connsm
 */
void
ble_ll_ctrl_terminate_start(struct ble_ll_conn_sm *connsm)
{
    int ctrl_proc;
    uint32_t usecs;
    struct os_mbuf *om;

    assert(connsm->disconnect_reason != 0);

    ctrl_proc = BLE_LL_CTRL_PROC_TERMINATE;
    om = ble_ll_ctrl_proc_init(connsm, ctrl_proc);
    if (om) {
        connsm->pending_ctrl_procs |= (1 << ctrl_proc);

        /* Set terminate "timeout" */
        usecs = connsm->supervision_tmo * BLE_HCI_CONN_SPVN_TMO_UNITS * 1000;
        connsm->terminate_timeout = cputime_get32() +
            cputime_usecs_to_ticks(usecs);
    }
}

/**
 * Called to start a LL control procedure except for the terminate procedure. We
 * always set the control procedure pending bit even if the control procedure
 * has been initiated.
 *
 * Context: Link Layer task.
 *
 * @param connsm Pointer to connection state machine.
 */
void
ble_ll_ctrl_proc_start(struct ble_ll_conn_sm *connsm, int ctrl_proc)
{
    struct os_mbuf *om;

    assert(ctrl_proc != BLE_LL_CTRL_PROC_TERMINATE);

    om = NULL;
    if (connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_IDLE) {
        /* Initiate the control procedure. */
        om = ble_ll_ctrl_proc_init(connsm, ctrl_proc);
        if (om) {
            /* Set the current control procedure */
            connsm->cur_ctrl_proc = ctrl_proc;

            /* Initialize the procedure response timeout */
            if (ctrl_proc != BLE_LL_CTRL_PROC_CHAN_MAP_UPD) {
                os_callout_func_init(&connsm->ctrl_proc_rsp_timer,
                                     &g_ble_ll_data.ll_evq,
                                     ble_ll_ctrl_proc_rsp_timer_cb,
                                     connsm);

                /* Re-start timer. Control procedure timeout is 40 seconds */
                os_callout_reset(&connsm->ctrl_proc_rsp_timer.cf_c,
                                 OS_TICKS_PER_SEC * BLE_LL_CTRL_PROC_TIMEOUT);
            }
        }
    }

    /* Set bitmask denoting control procedure is pending */
    connsm->pending_ctrl_procs |= (1 << ctrl_proc);
}

/**
 * Called to determine if we need to start a LL control procedure for the given
 * connection.
 *
 * Context: Link Layer
 *
 * @param connsm Pointer to connection state machine.
 */
void
ble_ll_ctrl_chk_proc_start(struct ble_ll_conn_sm *connsm)
{
    int i;

    /* If we are terminating, dont start any new procedures */
    if (connsm->disconnect_reason) {
        /*
         * If the terminate procedure is not pending it means we were not
         * able to start it right away (no control pdu was available).
         * Start it now.
         */
        ble_ll_ctrl_terminate_start(connsm);
        return;
    }

    /* If there is a running procedure or no pending, do nothing */
    if ((connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_IDLE) &&
        (connsm->pending_ctrl_procs != 0)) {
        /*
         * The specification says there is no priority to control procedures
         * so just start from the first one for now.
         */
        for (i = 0; i < BLE_LL_CTRL_PROC_NUM; ++i) {
            if (IS_PENDING_CTRL_PROC(connsm, i)) {
                /*
                 * The version exchange is a special case. If we have already
                 * received the information dont start it.
                 */
                if ((i == BLE_LL_CTRL_PROC_VERSION_XCHG) &&
                    (connsm->csmflags.cfbit.rxd_version_ind)) {
                    ble_ll_hci_ev_rd_rem_ver(connsm, BLE_ERR_SUCCESS);
                    CLR_PENDING_CTRL_PROC(connsm, i);
                } else {
                    ble_ll_ctrl_proc_start(connsm, i);
                    break;
                }
            }
        }
    }
}

/**
 * Called when the Link Layer receives a LL control PDU.
 *
 * NOTE: this function uses the received PDU for the response in some cases. If
 * the received PDU is not used it needs to be freed here.
 *
 * Context: Link Layer
 *
 * @param om
 * @param connsm
 */
int
ble_ll_ctrl_rx_pdu(struct ble_ll_conn_sm *connsm, struct os_mbuf *om)
{
    uint8_t features;
    uint8_t feature;
    uint8_t len;
    uint8_t opcode;
    uint8_t rsp_opcode;
    uint8_t *dptr;
    uint8_t *rspbuf;

    /* XXX: where do we validate length received and packet header length?
     * do this in LL task when received. Someplace!!! What I mean
     * is we should validate the over the air length with the mbuf length.
       Should the PHY do that???? */

    /* Get length and opcode from PDU.*/
    dptr = om->om_data;
    rspbuf = dptr;
    len = dptr[1];
    opcode = dptr[2];

    /* Move data pointer to start of control data (2 byte PDU hdr + opcode) */
    dptr += (BLE_LL_PDU_HDR_LEN + 1);

    /*
     * Subtract the opcode from the length. Note that if the length was zero,
     * which would be an error, we will fail the check against the length
     * of the control packet.
     */
    --len;

    /* opcode must be good */
    if ((opcode >= BLE_LL_CTRL_OPCODES) ||
        (len != g_ble_ll_ctrl_pkt_lengths[opcode])) {
        goto rx_malformed_ctrl;
    }

    /* Check if the feature is supported. */
    switch (opcode) {
    case BLE_LL_CTRL_LENGTH_REQ:
        feature = BLE_LL_FEAT_DATA_LEN_EXT;
        break;
    case BLE_LL_CTRL_SLAVE_FEATURE_REQ:
        feature = BLE_LL_FEAT_SLAVE_INIT;
        break;
    case BLE_LL_CTRL_CONN_PARM_REQ:
    case BLE_LL_CTRL_CONN_PARM_RSP:
        feature = BLE_LL_FEAT_CONN_PARM_REQ;
        break;
    case BLE_LL_CTRL_ENC_REQ:
    case BLE_LL_CTRL_START_ENC_REQ:
    case BLE_LL_CTRL_PAUSE_ENC_REQ:
        feature = BLE_LL_FEAT_LE_ENCRYPTION;
        break;
    default:
        feature = 0;
        break;
    }

    if (feature) {
        features = ble_ll_read_supp_features();
        if ((features & feature) == 0) {
            /* Construct unknown rsp pdu */
            rsp_opcode = BLE_LL_CTRL_UNKNOWN_RSP;
            goto ll_ctrl_send_rsp;
        }
    }

    /* Process opcode */
    rsp_opcode = 255;
    switch (opcode) {
    case BLE_LL_CTRL_CONN_UPDATE_REQ:
        rsp_opcode = ble_ll_ctrl_rx_conn_update(connsm, dptr, rspbuf);
        break;
    case BLE_LL_CTRL_CHANNEL_MAP_REQ:
        ble_ll_ctrl_rx_chanmap_req(connsm, dptr);
        break;
    case BLE_LL_CTRL_LENGTH_REQ:
        /* Extract parameters and check if valid */
        if (ble_ll_ctrl_len_proc(connsm, dptr)) {
            goto rx_malformed_ctrl;
        }

        /*
         * If we have not started this procedure ourselves and it is
         * pending, no need to perform it.
         */
        if ((connsm->cur_ctrl_proc != BLE_LL_CTRL_PROC_DATA_LEN_UPD) &&
            IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_DATA_LEN_UPD)) {
            CLR_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_DATA_LEN_UPD);
        }

        /* Send a response */
        rsp_opcode = BLE_LL_CTRL_LENGTH_RSP;
        ble_ll_ctrl_datalen_upd_make(connsm, rspbuf);
        break;
    case BLE_LL_CTRL_LENGTH_RSP:
        /* According to specification, process this only if we asked for it. */
        if (connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_DATA_LEN_UPD) {
            /* Process the received data */
            if (ble_ll_ctrl_len_proc(connsm, dptr)) {
                goto rx_malformed_ctrl;
            }

            /* Stop the control procedure */
            ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_DATA_LEN_UPD);
        }
        break;
    case BLE_LL_CTRL_UNKNOWN_RSP:
        ble_ll_ctrl_proc_unk_rsp(connsm, dptr);
        break;
    case BLE_LL_CTRL_FEATURE_REQ:
        rsp_opcode = ble_ll_ctrl_rx_feature_req(connsm, dptr, rspbuf, opcode);
        break;
    /* XXX: check to see if ctrl procedure was running? Do we care? */
    case BLE_LL_CTRL_FEATURE_RSP:
        /* Stop the control procedure */
        connsm->common_features = dptr[0];
        if (IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_FEATURE_XCHG)) {
            ble_ll_hci_ev_rd_rem_used_feat(connsm, BLE_ERR_SUCCESS);
            ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_FEATURE_XCHG);
        }
        break;
    case BLE_LL_CTRL_VERSION_IND:
        rsp_opcode = ble_ll_ctrl_rx_version_ind(connsm, dptr, rspbuf + 1);
        break;
    case BLE_LL_CTRL_SLAVE_FEATURE_REQ:
        rsp_opcode = ble_ll_ctrl_rx_feature_req(connsm, dptr, rspbuf, opcode);
        break;
#if defined(BLE_LL_CFG_FEAT_LE_ENCYPTION)
    case BLE_LL_CTRL_ENC_REQ:
        rsp_opcode = ble_ll_ctrl_rx_enc_req(connsm, dptr, rspbuf);
    case BLE_LL_CTRL_START_ENC_REQ:
    case BLE_LL_CTRL_PAUSE_ENC_REQ:
        break;
#endif
    case BLE_LL_CTRL_PING_REQ:
        rsp_opcode = BLE_LL_CTRL_UNKNOWN_RSP;
        break;
    case BLE_LL_CTRL_CONN_PARM_REQ:
        rsp_opcode = ble_ll_ctrl_rx_conn_param_req(connsm, dptr, rspbuf);
        break;
    case BLE_LL_CTRL_CONN_PARM_RSP:
        rsp_opcode = ble_ll_ctrl_rx_conn_param_rsp(connsm, dptr, rspbuf);
        break;
    case BLE_LL_CTRL_REJECT_IND_EXT:
        /* XXX: not sure what other control procedures to check out, but
           add them when needed */
        /* XXX: should I check to make sure that the rejected opcode is sane? */
        if (connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_CONN_PARAM_REQ) {
            ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CONN_PARAM_REQ);
            ble_ll_hci_ev_conn_update(connsm, dptr[1]);
        }
        break;
    default:
        /* Nothing to do here */
        break;
    }

    /* Free mbuf or send response */
ll_ctrl_send_rsp:
    if (rsp_opcode == 255) {
        os_mbuf_free_chain(om);
    } else {
        rspbuf[0] = rsp_opcode;
        if (rsp_opcode == BLE_LL_CTRL_UNKNOWN_RSP) {
            rspbuf[1] = opcode;
        }
        len = g_ble_ll_ctrl_pkt_lengths[rsp_opcode] + 1;
        ble_ll_conn_enqueue_pkt(connsm, om, BLE_LL_LLID_CTRL, len);
    }
    return 0;

rx_malformed_ctrl:
    os_mbuf_free_chain(om);
    return -1;
}

/**
 * Called to creeate and send a REJECT_IND_EXT control PDU
 *
 *
 * @param connsm
 * @param rej_opcode
 * @param err
 *
 * @return int
 */
int
ble_ll_ctrl_reject_ind_ext_send(struct ble_ll_conn_sm *connsm,
                                uint8_t rej_opcode, uint8_t err)
{
    int rc;
    uint8_t len;
    uint8_t *rspbuf;
    struct os_mbuf *om;

    om = os_msys_get_pkthdr(BLE_LL_CTRL_MAX_PDU_LEN,
                            sizeof(struct ble_mbuf_hdr));
    if (om) {
        rspbuf = om->om_data;
        rspbuf[0] = BLE_LL_CTRL_REJECT_IND_EXT;
        rspbuf[1] = rej_opcode;
        rspbuf[2] = err;
        len = BLE_LL_CTRL_REJECT_IND_EXT_LEN + 1;
        ble_ll_conn_enqueue_pkt(connsm, om, BLE_LL_LLID_CTRL, len);
        rc = 0;
    } else {
        rc = 1;
    }
    return rc;
}
