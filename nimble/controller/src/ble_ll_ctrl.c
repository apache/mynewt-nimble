/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "nimble/ble.h"
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
 *  3) NOTE: some procedures are allowed to run while others are
 *      running!!! Fix this in the code
 *  4) What about procedures that have been completed but try to restart?
 *  5) NOTE: there is a supported features procedure. However, in the case
 *  of data length extension, if the receiving device does not understand
 *  the pdu or it does not support data length extension, the LL_UNKNOWN_RSP
 *  pdu is sent. That needs to be processed...
 *  6) We are supposed to remember when we do the data length update proc if
 *  the device sent us an unknown rsp. We should not send it another len req.
 *  Implement this how? Through remote supported features?
 *  7) Remember: some procedures dont have timeout rules.
 *  8) remember to stop procedure when rsp received.
 *  9) Says that we should reset procedure timer whenever a LL control pdu
 *  is queued for transmission. I dont get it... do some procedures send
 *  multiple packets? I guess so.
 *  10) How to count control pdus sent. DO we count enqueued + sent, or only
 *  sent (actually attempted to tx). Do we count failures? How?
 */

 /* XXX: NOTE: we are not supposed to send a REJECT_IND_EXT unless we know the
   slave supports that feature */

/* XXX:
 * 1) One thing I need to make sure I do: if we initiated this procedure and
 * we stop it, we have to make sure we send the update complete event! I
 * am referring to the connection parameter request procedure. The code is
 * already there to send the event when the connection update procedure
 * is over.
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

/* XXX: Improvements
 * 1) We can inititalize the procedure timer once per connection state machine
 */
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

static int
ble_ll_ctrl_conn_param_pdu_proc(struct ble_ll_conn_sm *connsm, uint8_t *dptr,
                                uint8_t *rspbuf, uint8_t opcode)
{
    int rc;
    int indicate;
    uint8_t rsp_opcode;
    uint8_t ble_err;
    struct ble_ll_conn_params cp;
    struct ble_ll_conn_params *req;
    struct hci_conn_update *hcu;

    /* Extract parameters and check if valid */
    cp.interval_min = le16toh(dptr);
    cp.interval_max = le16toh(dptr + 2);
    cp.latency = le16toh(dptr + 4);
    cp.timeout = le16toh(dptr + 6);
    cp.pref_periodicity = dptr[8];
    cp.ref_conn_event_cnt  = le16toh(dptr + 9);
    cp.offset0 = le16toh(dptr + 11);
    cp.offset1 = le16toh(dptr + 13);
    cp.offset2 = le16toh(dptr + 15);
    cp.offset3 = le16toh(dptr + 17);
    cp.offset4 = le16toh(dptr + 19);
    cp.offset5 = le16toh(dptr + 21);

    /* Check if parameters are valid */
    ble_err = BLE_ERR_SUCCESS;
    rc = ble_ll_conn_hci_chk_conn_params(cp.interval_min, 
                                         cp.interval_max,
                                         cp.latency, 
                                         cp.timeout);
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
    req = NULL;
    indicate = 1;
    if ((connsm->conn_itvl >= cp.interval_min) && 
        (connsm->conn_itvl <= cp.interval_max) &&
        (connsm->supervision_tmo == cp.timeout) &&
        (connsm->slave_latency == cp.latency)) {
        indicate = 0;
        /* XXX: For now, if we are a master, we wont send a response that
         * needs to remember the request. That might change with connection
           update pdu. Not sure. */
        if (connsm->conn_role == BLE_LL_CONN_ROLE_SLAVE) {
            req = &cp;
        }
        goto conn_parm_req_do_indicate;
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
            if (!((cp.interval_min < hcu->conn_itvl_min) ||
                  (cp.interval_min > hcu->conn_itvl_max) ||
                  (cp.interval_max < hcu->conn_itvl_min) ||
                  (cp.interval_max > hcu->conn_itvl_max) ||
                  (cp.latency != hcu->conn_latency) ||
                  (cp.timeout != hcu->supervision_timeout))) {
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
        ble_ll_hci_ev_rem_conn_parm_req(connsm, &cp);
        connsm->host_reply_opcode = opcode;
        connsm->awaiting_host_reply = 1;
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
            ble_ll_hci_ev_conn_update(connsm, BLE_ERR_UNSUPP_FEATURE);
        } else if (ctrl_proc == BLE_LL_CTRL_PROC_FEATURE_XCHG) {
            /* XXX: should only get this if a slave initiated this */
            ble_ll_hci_ev_read_rem_used_feat(connsm, BLE_ERR_UNSUPP_FEATURE);
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
    uint16_t invalid_offset;
    struct hci_conn_update *hcu;

    invalid_offset = 0xFFFF;
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
    htole16(dptr + 11, invalid_offset);
    htole16(dptr + 13, invalid_offset);
    htole16(dptr + 15, invalid_offset);
    htole16(dptr + 17, invalid_offset);
    htole16(dptr + 19, invalid_offset);
    htole16(dptr + 21, invalid_offset);
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
ble_ll_ctrl_conn_upd_make(struct ble_ll_conn_sm *connsm, uint8_t *pyld)
{
    uint16_t instant;
    struct hci_conn_update *hcu;
    struct ble_ll_conn_upd_req *req;

    /* Make sure we have the parameters! */
    assert(connsm->conn_param_req.handle != 0);


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
    req->winsize = connsm->tx_win_size;
    req->winoffset = 0;
    req->interval = hcu->conn_itvl_max;
    req->timeout = hcu->supervision_timeout;
    req->latency = hcu->conn_latency;
    req->instant = instant;

    /* XXX: make sure this works for the connection parameter request proc. */
    pyld[0] = req->winsize;
    htole16(pyld + 1, req->winoffset);
    htole16(pyld + 3, req->interval);
    htole16(pyld + 5, req->latency);
    htole16(pyld + 7, req->timeout);
    htole16(pyld + 9, instant);

    /* Set flag in state machine to denote we have scheduled an update */
    connsm->conn_update_scheduled = 1;
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
        ble_ll_ctrl_conn_upd_make(connsm, rsp + 1);
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

#if 0
    /* Deal with receiving this when in this state. I think we are done */
    if (IS_PENDING_CTRL_PROC_M(connsm, BLE_LL_CTRL_PROC_CONN_PARAM_REQ)) {
    }
#endif

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
        connsm->conn_update_scheduled = 1;
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
 * 
 * @return int 
 */
static int
ble_ll_ctrl_rx_feature_req(struct ble_ll_conn_sm *connsm, uint8_t *dptr,
                              uint8_t *rspbuf)
{
    uint8_t rsp_opcode;

    /* Add put logical AND of their features and our features*/
    /* XXX: right now, there is only one byte of supported features */
    /* XXX: Used proper macros later */
    rsp_opcode = BLE_LL_CTRL_FEATURE_RSP;
    connsm->common_features = dptr[0] & ble_ll_read_supp_features();
    memset(rspbuf + 1, 0, 8);
    rspbuf[1] = connsm->common_features;

    return rsp_opcode;
}

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
    if (connsm->awaiting_host_reply) {
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
    if (IS_PENDING_CTRL_PROC_M(connsm, BLE_LL_CTRL_PROC_CONN_PARAM_REQ)) {
        if (connsm->conn_role == BLE_LL_CONN_ROLE_SLAVE) {
            if (connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_CONN_PARAM_REQ) {
                ble_ll_ctrl_proc_stop(connsm,
                                      BLE_LL_CTRL_PROC_CONN_PARAM_REQ);
            } else {
                connsm->pending_ctrl_procs &= 
                    ~BLE_LL_CTRL_PROC_CONN_PARAM_REQ;
            }
            ble_ll_hci_ev_conn_update(connsm, BLE_ERR_LMP_COLLISION);
        } else {
            /* The master sends reject ind ext w/error code 0x23 */
            rsp_opcode = BLE_LL_CTRL_REJECT_IND_EXT;
            rspbuf[1] = BLE_LL_CTRL_CONN_PARM_REQ;
            rspbuf[2] = BLE_ERR_LMP_COLLISION;
            return rsp_opcode;
        }
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
    if (connsm->awaiting_host_reply) {
        connsm->awaiting_host_reply = 0;
    }

    /* If we receive a response and no procedure is pending, just leave */
    if (!IS_PENDING_CTRL_PROC_M(connsm, BLE_LL_CTRL_PROC_CONN_PARAM_REQ)) {
        return BLE_ERR_MAX;
    }

    /* Process the received connection parameter response */
    rsp_opcode = ble_ll_ctrl_conn_param_pdu_proc(connsm, dptr, rspbuf,
                                                 BLE_LL_CTRL_CONN_PARM_RSP);
    return rsp_opcode;
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
    om = os_mbuf_get_pkthdr(&g_mbuf_pool, sizeof(struct ble_mbuf_hdr));

    if (om) {
        dptr = om->om_data;

        switch (ctrl_proc) {
        case BLE_LL_CTRL_PROC_DATA_LEN_UPD:
            opcode = BLE_LL_CTRL_LENGTH_REQ;
            ble_ll_ctrl_datalen_upd_make(connsm, dptr);
            break;
        case BLE_LL_CTRL_PROC_FEATURE_XCHG:
            if (connsm->conn_role == BLE_LL_CONN_ROLE_MASTER) {
                opcode = BLE_LL_CTRL_FEATURE_REQ;
            } else {
                opcode = BLE_LL_CTRL_SLAVE_FEATURE_REQ;
            }
            dptr[1] = ble_ll_read_supp_features();
            break;
        case BLE_LL_CTRL_PROC_TERMINATE:
            opcode = BLE_LL_CTRL_TERMINATE_IND;
            dptr[1] = connsm->disconnect_reason;
            break;
        case BLE_LL_CTRL_PROC_CONN_PARAM_REQ:
            opcode = BLE_LL_CTRL_CONN_PARM_REQ;
            ble_ll_ctrl_conn_param_pdu_make(connsm, dptr + 1, NULL);
            break;
        case BLE_LL_CTRL_PROC_CONN_UPDATE:
            opcode = BLE_LL_CTRL_CONN_UPDATE_REQ;
            ble_ll_ctrl_conn_upd_make(connsm, dptr + 1);
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
    assert(connsm->cur_ctrl_proc == ctrl_proc);
    
    os_callout_stop(&connsm->ctrl_proc_rsp_timer.cf_c);
    connsm->cur_ctrl_proc = BLE_LL_CTRL_PROC_IDLE;
    connsm->pending_ctrl_procs &= ~(1 << ctrl_proc);

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
            os_callout_func_init(&connsm->ctrl_proc_rsp_timer, 
                                 &g_ble_ll_data.ll_evq, 
                                 ble_ll_ctrl_proc_rsp_timer_cb, 
                                 connsm);

            /* Re-start the timer. Control procedure timeout is 40 seconds */
            os_callout_reset(&connsm->ctrl_proc_rsp_timer.cf_c, 
                             OS_TICKS_PER_SEC * BLE_LL_CTRL_PROC_TIMEOUT);
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
            if (IS_PENDING_CTRL_PROC_M(connsm, i)) {
                ble_ll_ctrl_proc_start(connsm, i);
                break;
            }
        }
    }
}

/**
 * Called when the Link Layer receives a LL control PDU. This function 
 * must either free the received pdu or re-use it for the response. 
 *  
 * Context: Link Layer 
 * 
 * @param om 
 * @param connsm 
 */
void
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
    default:
        feature = 0;
        break;
    }

    if (feature) {
        features = ble_ll_read_supp_features();
        if ((features & feature) == 0) {
            /* Construct unknown rsp pdu */
            rspbuf[1] = opcode;
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
            IS_PENDING_CTRL_PROC_M(connsm, BLE_LL_CTRL_PROC_DATA_LEN_UPD)) {
            connsm->pending_ctrl_procs &= ~BLE_LL_CTRL_PROC_DATA_LEN_UPD;
        }

        /* Send a response */
        rsp_opcode = BLE_LL_CTRL_LENGTH_RSP;
        ble_ll_ctrl_datalen_upd_make(connsm, rspbuf);
        break;
    case BLE_LL_CTRL_LENGTH_RSP:
        /* 
         * According to specification, we should only process this if we
         * asked for it.
         */
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
        if (connsm->conn_role == BLE_LL_CONN_ROLE_SLAVE) {
            rsp_opcode = ble_ll_ctrl_rx_feature_req(connsm, dptr, rspbuf);
        } else {
            /* XXX: not sure this is correct but do it anyway */
            /* Construct unknown pdu */
            rspbuf[1] = opcode;
            rsp_opcode = BLE_LL_CTRL_UNKNOWN_RSP;
        }
        break;

    /* XXX: check to see if ctrl procedure was running? Do we care? */
    case BLE_LL_CTRL_FEATURE_RSP:
        /* Stop the control procedure */
        connsm->common_features = dptr[0];
        if (IS_PENDING_CTRL_PROC_M(connsm, BLE_LL_CTRL_PROC_FEATURE_XCHG)) {
            ble_ll_hci_ev_read_rem_used_feat(connsm, BLE_ERR_SUCCESS);
        }
        ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_FEATURE_XCHG);
        break;

    case BLE_LL_CTRL_SLAVE_FEATURE_REQ:
        if (connsm->conn_role == BLE_LL_CONN_ROLE_MASTER) {
            rsp_opcode = ble_ll_ctrl_rx_feature_req(connsm, dptr, rspbuf);
        } else {
            /* Construct unknown pdu */
            rspbuf[1] = opcode;
            rsp_opcode = BLE_LL_CTRL_UNKNOWN_RSP;
        }
        break;

    /* XXX: remember to check if feature supported */
    case BLE_LL_CTRL_CHANNEL_MAP_REQ:
    case BLE_LL_CTRL_ENC_REQ:
    case BLE_LL_CTRL_START_ENC_REQ:
    case BLE_LL_CTRL_PAUSE_ENC_REQ:
        /* Construct unknown pdu */
        rspbuf[1] = opcode;
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
        /* We really should never get here */
        break;
    }

    /* Free mbuf or send response */
ll_ctrl_send_rsp:
    if (rsp_opcode == 255) {
        os_mbuf_free(om);
    } else {
        rspbuf[0] = rsp_opcode;
        len = g_ble_ll_ctrl_pkt_lengths[rsp_opcode] + 1;
        ble_ll_conn_enqueue_pkt(connsm, om, BLE_LL_LLID_CTRL, len);
    }
    return;

rx_malformed_ctrl:
    os_mbuf_free(om);
    ++g_ble_ll_stats.rx_malformed_ctrl_pdus;
    return;
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

    om = os_mbuf_get_pkthdr(&g_mbuf_pool, sizeof(struct ble_mbuf_hdr));
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
