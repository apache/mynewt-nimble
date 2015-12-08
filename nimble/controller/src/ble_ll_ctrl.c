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
 *  Do I need to worry about repeating procedures?
 *  2) If we fail to get a LL control PDU we will probably need to know that
 *  right? Should I keep a procedure state variable? I could save memory
 *  by only using the low 4 bits of the current procedure.
 *  3) Probably create my own pool of control pdu's here. Dont need more
 *  than the # of connections and can probably deal with quite a few less
 *  if we have lots of connections.
 *  4) os_callout_func_init does not really need to be done every time I
 *  dont think. Do that once per connection when initialized?
 *  5) NOTE: some procedures are allowed to run while others are
 *      running!!! Fix this in the code
 *  6) Make sure we send data length change event to host.
 *  7) We need to start the control procedure for data length change!
 *  Deal with this.
 *  8) What about procedures that have been completed but try to restart?
 *  9) How do I deal with control procedures that are pending? What happens
 *  if we cant get an mbuf? There will be pending control procedures without
 *  one running. WHen/how do I deal with that?
 *  10) Make sure that we stop the data length update procedure if we receive
 *  a response. Need to look at that.
 */

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
 * Callback when LL control procedure times out (for a given connection). If 
 * this is called, it means that we need to end the connection because it 
 * has not responded to a LL control request. 
 *  
 * Context: Link Layer 
 * 
 * @param arg Pointer to connection state machine.
 */
void
ble_ll_ctrl_proc_timer_cb(void *arg)
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
            htole16(dptr + 3, connsm->max_rx_octets);
            htole16(dptr + 5, connsm->max_rx_time);
            htole16(dptr + 7, connsm->max_tx_octets);
            htole16(dptr + 9, connsm->max_tx_time);
            om->om_len = BLE_LL_CTRL_LENGTH_REQ_LEN + BLE_LL_PDU_HDR_LEN;
            OS_MBUF_PKTHDR(om)->omp_len = om->om_len;
            break;
        default:
            assert(0);
            break;
        }

        /* Set llid, length and opcode */
        dptr[0] = BLE_LL_LLID_CTRL;
        dptr[1] = len;
        dptr[2] = opcode;
    }

    return om;
}

/**
 * Stops the LL control procedure indicated by 'ctrl_proc'. 
 * 
 * @param connsm 
 * @param ctrl_proc 
 */
void
ble_ll_ctrl_proc_stop(struct ble_ll_conn_sm *connsm, int ctrl_proc)
{
    if (connsm->cur_ctrl_proc == ctrl_proc) {
        os_callout_stop(&connsm->ctrl_proc_timer.cf_c);
        connsm->cur_ctrl_proc = BLE_LL_CTRL_PROC_IDLE;
        connsm->pending_ctrl_procs &= ~(1 << ctrl_proc);
    }
    /* XXX: if there are more pending ones, what do we do? */
}

/**
 * Called to start a LL control procedure. We always set the control 
 * procedure pending bit even if the input control procedure has been 
 * initiated. 
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

            /* Initialize the host timer */
            os_callout_func_init(&connsm->ctrl_proc_timer, 
                                 &g_ble_ll_data.ll_evq, 
                                 ble_ll_ctrl_proc_timer_cb, 
                                 connsm);

            /* Re-start the timer. Control procedure timeout is 40 seconds */
            os_callout_reset(&connsm->ctrl_proc_timer.cf_c, 
                             OS_TICKS_PER_SEC * 40);
        }
    }

    /* Set bitmask denoting control procedure is pending */
    connsm->pending_ctrl_procs |= (1 << ctrl_proc);
}

/**
 * Called when the Link Layer receives a LL control PDU. 
 *  
 * NOTE: the calling function frees the mbuf! 
 *  
 * Context: Link Layer 
 * 
 * @param om 
 * @param connsm 
 */
void
ble_ll_ctrl_rx_pdu(struct ble_ll_conn_sm *connsm, struct os_mbuf *om)
{
    /* XXX: implement */
}

