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
#include "controller/ble_ll_conn.h"
#include "controller/ble_ll_ctrl.h"

/* 
 * XXX: TODO
 *  1) Do I need to keep track of which procedures have already been done?
 *     Do I need to worry about repeating procedures?
 *  2) Should we create pool of control pdu's?. Dont need more
 *  than the # of connections and can probably deal with quite a few less
 *  if we have lots of connections.
 *  4) os_callout_func_init does not really need to be done every time I
 *  dont think. Do that once per connection when initialized?
 *  5) NOTE: some procedures are allowed to run while others are
 *      running!!! Fix this in the code
 *  8) What about procedures that have been completed but try to restart?
 *  12) NOTE: there is a supported features procedure. However, in the case
 *  of data length extension, if the receiving device does not understand
 *  the pdu or it does not support data length extension, the LL_UNKNOWN_RSP
 *  pdu is sent. That needs to be processed...
 *  14) Will I need to know which procedures actually completed?
 *  15) We are supposed to remember when we do the data length update proc if
 *  the device sent us an unknown rsp. We should not send it another len req.
 *  Implement this.
 *  16) Remember: some procedures dont have timeout rules.
 *  17) remember to stop procedure when rsp received.
 *  18) Says that we should reset procedure timer whenever a LL control pdu
 *  is queued for transmission. I dont get it... do some procedures send
 *  multiple packets? I guess so.
 *  19) How to count control pdus sent. DO we count enqueued + sent, or only
 *  sent (actually attempted to tx). Do we count failures? How?
 * 
 */

/* XXX: Improvements
 * 1) We can inititalize the procedure timer once per connection state machine
 */

/* Checks if a particular control procedure is running */
#define IS_PENDING_CTRL_PROC_M(sm, proc) (sm->pending_ctrl_procs & (1 << proc))

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
    ctrl_req.max_rx_bytes = le16toh(dptr + 3); 
    ctrl_req.max_rx_time = le16toh(dptr + 5);
    ctrl_req.max_tx_bytes = le16toh(dptr + 7);
    ctrl_req.max_tx_time = le16toh(dptr + 9);

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
    opcode = dptr[3];

    /* Convert opcode to control procedure id */
    switch (opcode) {
    case BLE_LL_CTRL_LENGTH_REQ:
        ctrl_proc = BLE_LL_CTRL_PROC_DATA_LEN_UPD;
        break;
    default:
        ctrl_proc = BLE_LL_CTRL_PROC_NUM;
        break;
    }

    /* If we are running this one currently, stop it */
    if (connsm->cur_ctrl_proc == ctrl_proc) {
        /* Stop the control procedure */
        ble_ll_ctrl_proc_stop(connsm, ctrl_proc);
    }
}

/**
 * Send a data length change event for a connection to the host.
 * 
 * @param connsm Pointer to connection state machine
 */
void
ble_ll_ctrl_datalen_chg_event(struct ble_ll_conn_sm *connsm)
{
    uint8_t *evbuf;

    if (ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_DATA_LEN_CHG - 1)) {
        evbuf = os_memblock_get(&g_hci_cmd_pool);
        if (evbuf) {
            evbuf[0] = BLE_HCI_EVCODE_LE_META;
            evbuf[1] = BLE_HCI_LE_DATA_LEN_CHG_LEN;
            evbuf[2] = BLE_HCI_LE_SUBEV_DATA_LEN_CHG;
            htole16(evbuf + 3, connsm->conn_handle);
            htole16(evbuf + 5, connsm->eff_max_tx_octets);
            htole16(evbuf + 7, connsm->eff_max_tx_time);
            htole16(evbuf + 9, connsm->eff_max_rx_octets);
            htole16(evbuf + 11, connsm->eff_max_rx_time);
            ble_ll_hci_event_send(evbuf);
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
    htole16(dptr + 3, connsm->max_rx_octets);
    htole16(dptr + 5, connsm->max_rx_time);
    htole16(dptr + 7, connsm->max_tx_octets);
    htole16(dptr + 9, connsm->max_tx_time);
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
    ble_ll_conn_end((struct ble_ll_conn_sm *)arg, BLE_ERR_LMP_LL_RSP_TMO);
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

    /* XXX: assume for now that all procedures require an mbuf */
    om = os_mbuf_get_pkthdr(&g_mbuf_pool, sizeof(struct ble_mbuf_hdr));

    if (om) {
        dptr = om->om_data;
        switch (ctrl_proc) {
        case BLE_LL_CTRL_PROC_DATA_LEN_UPD:
            len = BLE_LL_CTRL_LENGTH_REQ_LEN;
            opcode = BLE_LL_CTRL_LENGTH_REQ;
            ble_ll_ctrl_datalen_upd_make(connsm, dptr);
            break;
        default:
            assert(0);
            break;
        }

        /* Set llid, length and opcode */
        ++len;
        dptr[0] = BLE_LL_LLID_CTRL;
        dptr[1] = len;
        dptr[2] = opcode;
        om->om_len = len + BLE_LL_PDU_HDR_LEN;
        OS_MBUF_PKTHDR(om)->omp_len = om->om_len;
    }

    return om;
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
        connsm->pending_ctrl_procs &= ~(1 << ctrl_proc);
    }

    /* If there are others, start them */
    ble_ll_ctrl_chk_proc_start(connsm);
}

/**
 * Called to start a LL control procedure. We always set the control 
 * procedure pending bit even if the input control procedure has been 
 * initiated. 
 *  
 * Context: Link Layer task. 
 * 
 * @param connsm Pointer to connection state machine.
 */
void
ble_ll_ctrl_proc_start(struct ble_ll_conn_sm *connsm, int ctrl_proc)
{
    struct os_mbuf *om;

    om = NULL;
    if (connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_IDLE) {
        /* Initiate the control procedure. */
        om = ble_ll_ctrl_proc_init(connsm, ctrl_proc);
        if (om) {
            /* Set the current control procedure */
            connsm->cur_ctrl_proc = ctrl_proc;

            /* Add packet to transmit queue of connection */
            ble_ll_conn_enqueue_pkt(connsm, om);

            /* Initialize the procedure response timeout */
            os_callout_func_init(&connsm->ctrl_proc_rsp_timer, 
                                 &g_ble_ll_data.ll_evq, 
                                 ble_ll_ctrl_proc_rsp_timer_cb, 
                                 connsm);

            /* Re-start the timer. Control procedure timeout is 40 seconds */
            os_callout_reset(&connsm->ctrl_proc_rsp_timer.cf_c, 
                             OS_TICKS_PER_SEC * 40);
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
    uint8_t len;
    uint8_t opcode;
    uint8_t *dptr;

    /* XXX: where do we validate length received and packet header length?
     * do this in LL task when received. Someplace!!! What I mean
     * is we should validate the over the air length with the mbuf length.
       Should the PHY do that???? */

    /* Get length and opcode from PDU */
    dptr = om->om_data;
    len = dptr[1];
    opcode = dptr[2];

    /* opcode must be good */
    if ((opcode > BLE_LL_CTRL_LENGTH_RSP) || (len < 1) || 
        (len > BLE_LL_CTRL_MAX_PAYLOAD)) {
        goto rx_malformed_ctrl;
    }

    /* Subtract the opcode from the length */
    --len;

    switch (opcode) {
    case BLE_LL_CTRL_LENGTH_REQ:
        /* Check length */
        if (len != BLE_LL_CTRL_LENGTH_REQ_LEN) {
            goto rx_malformed_ctrl;
        }

        /* Check parameters for validity */
        features = ble_ll_read_supp_features();
        if (features & BLE_LL_FEAT_DATA_LEN_EXT) {
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
            opcode = BLE_LL_CTRL_LENGTH_RSP;
            ble_ll_ctrl_datalen_upd_make(connsm, dptr);
        } else {
            /* XXX: construct unknown pdu */
            opcode = BLE_LL_CTRL_UNKNOWN_RSP;
            len = BLE_LL_CTRL_UNK_RSP_LEN;
            dptr[3] = BLE_LL_CTRL_LENGTH_REQ;
        }
        break;
    case BLE_LL_CTRL_LENGTH_RSP:
        /* Check length (response length same as request length) */
        if (len != BLE_LL_CTRL_LENGTH_REQ_LEN) {
            goto rx_malformed_ctrl;
        }

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
        opcode = 255;
        break;
    case BLE_LL_CTRL_UNKNOWN_RSP:
        /* Check length (response length same as request length) */
        if (len != BLE_LL_CTRL_UNK_RSP_LEN) {
            goto rx_malformed_ctrl;
        }

        ble_ll_ctrl_proc_unk_rsp(connsm, dptr);
        opcode = 255;
        break;
    default:
        /* XXX: this is an un-implemented control procedure. What to do? */
        opcode = 255;
        break;
    }

    /* Free mbuf or send response */
    if (opcode == 255) {
        os_mbuf_free(om);
    } else {
        ++len;
        dptr[0] = BLE_LL_LLID_CTRL;
        dptr[1] = len;
        dptr[2] = opcode;
        om->om_len = len + BLE_LL_PDU_HDR_LEN;
        OS_MBUF_PKTHDR(om)->omp_len = om->om_len;
        ble_ll_conn_enqueue_pkt(connsm, om);
    }
    return;

rx_malformed_ctrl:
    os_mbuf_free(om);
    ++g_ble_ll_stats.rx_malformed_ctrl_pdus;
    return;
}
