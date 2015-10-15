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
#include "os/os.h"
#include "controller/phy.h"
#include "controller/ll.h"
#include "controller/ll_adv.h"
#include "controller/ll_sched.h"
#include "controller/ll_scan.h"

/* XXX: use the sanity task! */
/* XXX: Remember to check rx packet lengths and report errors if they
 * are wrong. Need to check CRC too.
 */

/* XXX: find a place for this */
void ll_hci_cmd_proc(struct os_event *ev);

/* Connection related define */
#define BLE_LL_CONN_INIT_MAX_REMOTE_OCTETS  (27)
#define BLE_LL_CONN_INIT_MAX_REMOTE_TIME    (238)

/* The global BLE LL data object */
struct ll_obj g_ll_data;

/* Global link layer statistics */
struct ll_stats g_ll_stats;

/* The BLE LL task data structure */
#define BLE_LL_TASK_PRI     (OS_TASK_PRI_HIGHEST)
#define BLE_LL_STACK_SIZE   (128)
struct os_task g_ll_task;
os_stack_t g_ll_stack[BLE_LL_STACK_SIZE];

/* XXX: this is just temporary; used to calculate the channel index */
struct ll_sm_connection
{
    /* Data channel index for connection */
    uint8_t unmapped_chan;
    uint8_t last_unmapped_chan;
    uint8_t num_used_channels;

    /* Flow control */
    uint8_t tx_seq;
    uint8_t next_exp_seq;

    /* Parameters kept by the link-layer per connection */
    uint8_t max_tx_octets;
    uint8_t max_rx_octets;
    uint8_t max_tx_time;
    uint8_t max_rx_time;
    uint8_t remote_max_tx_octets;
    uint8_t remote_max_rx_octets;
    uint8_t remote_max_tx_time;
    uint8_t remote_max_rx_time;
    uint8_t effective_max_tx_octets;
    uint8_t effective_max_rx_octets;
    uint8_t effective_max_tx_time;
    uint8_t effective_max_rx_time;

    /* The connection request data */
    struct ble_conn_req_data req_data; 
};

uint8_t
ll_next_data_channel(struct ll_sm_connection *cnxn)
{
    int     i;
    int     j;
    uint8_t curchan;
    uint8_t remap_index;
    uint8_t bitpos;
    uint8_t cntr;
    uint8_t mask;
    uint8_t usable_chans;

    /* Get next un mapped channel */
    curchan = (cnxn->last_unmapped_chan + cnxn->req_data.hop_inc) % 
              BLE_PHY_NUM_DATA_CHANS;

    /* Set the current unmapped channel */
    cnxn->unmapped_chan = curchan;

    /* Is this a valid channel? */
    bitpos = 1 << (curchan & 0x07);
    if ((cnxn->req_data.chanmap[curchan >> 3] & bitpos) == 0) {

        /* Calculate remap index */
        remap_index = curchan % cnxn->num_used_channels;

        /* Iterate through channel map to find this channel */
        cntr = 0;
        for (i = 0; i < 5; i++) {
            usable_chans = cnxn->req_data.chanmap[i];
            if (usable_chans != 0) {
                mask = 0x01;
                for (j = 0; j < 8; j++) {
                    if (usable_chans & mask) {
                        if (cntr == remap_index) {
                            return cntr;
                        }
                        ++cntr;
                    }
                    mask <<= 1;
                }
            }
        }
    }

    return curchan;
}

/* Called when a connection gets initialized */
int
ble_init_conn_sm(struct ll_sm_connection *cnxn)
{
    cnxn->max_tx_time = g_ll_data.ll_params.conn_init_max_tx_time;
    cnxn->max_rx_time = g_ll_data.ll_params.supp_max_rx_time;
    cnxn->max_tx_octets = g_ll_data.ll_params.conn_init_max_tx_octets;
    cnxn->max_rx_octets = g_ll_data.ll_params.supp_max_rx_octets;
    cnxn->remote_max_rx_octets = BLE_LL_CONN_INIT_MAX_REMOTE_OCTETS;
    cnxn->remote_max_tx_octets = BLE_LL_CONN_INIT_MAX_REMOTE_OCTETS;
    cnxn->remote_max_rx_time = BLE_LL_CONN_INIT_MAX_REMOTE_TIME;
    cnxn->remote_max_tx_time = BLE_LL_CONN_INIT_MAX_REMOTE_TIME;

    return 0;
}

/**
 * ll rx pkt in proc
 *  
 * Process received packet from PHY 
 *  
 * Callers: LL task. 
 *  
 */
void
ll_rx_pkt_in_proc(void)
{
    os_sr_t sr;
    uint8_t pdu_type;
    uint8_t *rxbuf;
    struct os_mbuf_pkthdr *pkthdr;
    struct os_mbuf *m;

    /* Drain all packets off the queue */
    do {
        pkthdr = STAILQ_FIRST(&g_ll_data.ll_rx_pkt_q);
        if (pkthdr) {
            /* Get mbuf pointer from packet header pointer */
            m = (struct os_mbuf *)((uint8_t *)pkthdr - sizeof(struct os_mbuf));

            /* Remove from queue */
            OS_ENTER_CRITICAL(sr);
            STAILQ_REMOVE_HEAD(&g_ll_data.ll_rx_pkt_q, omp_next);
            OS_EXIT_CRITICAL(sr);

            /* XXX: call processing function here */

            /* Set the rx buffer pointer to the start of the received data */
            rxbuf = m->om_data;

            /* XXX: need to check if this is an adv channel or data channel */
            /* Count received packet types  */
            pdu_type = rxbuf[0] & BLE_ADV_HDR_PDU_TYPE_MASK;
            switch (pdu_type) {
            case BLE_ADV_PDU_TYPE_ADV_IND:
                ++g_ll_stats.rx_adv_ind;
                break;
            case BLE_ADV_PDU_TYPE_ADV_DIRECT_IND:
                ++g_ll_stats.rx_adv_direct_ind;
                break;
            case BLE_ADV_PDU_TYPE_ADV_NONCONN_IND:
                ++g_ll_stats.rx_adv_nonconn_ind;
                break;
            case BLE_ADV_PDU_TYPE_SCAN_REQ:
                ++g_ll_stats.rx_scan_reqs;
                break;
            case BLE_ADV_PDU_TYPE_SCAN_RSP:
                ++g_ll_stats.rx_scan_rsps;
                break;
            case BLE_ADV_PDU_TYPE_CONNECT_REQ:
                ++g_ll_stats.rx_connect_reqs;
                break;
            case BLE_ADV_PDU_TYPE_ADV_SCAN_IND:
                ++g_ll_stats.rx_scan_ind;
                break;
            default:
                ++g_ll_stats.rx_unk_pdu;
                break;
            }



            /* XXX: Free the mbuf for now */
            os_mbuf_free(&g_mbuf_pool, m);
        }
    } while (m != NULL);
}

/**
 * ll rx pdu in 
 *  
 * Called to put a packet on the Link Layer receive packet queue. 
 * 
 * 
 * @param rxpdu Pointer to received PDU
 */
void
ll_rx_pdu_in(struct os_mbuf *rxpdu)
{
    struct os_mbuf_pkthdr *pkthdr;

    pkthdr = OS_MBUF_PKTHDR(rxpdu);
    STAILQ_INSERT_TAIL(&g_ll_data.ll_rx_pkt_q, pkthdr, omp_next);
    os_eventq_put(&g_ll_data.ll_evq, &g_ll_data.ll_rx_pkt_ev);
}

/* 
 * Called when packet has started to be received. This occurs 1 byte
 * after the access address (we always receive one byte of the PDU)
 */
int
ll_rx_start(struct os_mbuf *rxpdu)
{
    int rc;
    uint8_t pdu_type;
    uint8_t *rxbuf;

    /* XXX: need to check if this is an adv channel or data channel */
    rxbuf = rxpdu->om_data;
    pdu_type = rxbuf[0] & BLE_ADV_HDR_PDU_TYPE_MASK;

    switch (g_ll_data.ll_state) {
    case BLE_LL_STATE_ADV:
        /* If we get a scan request we must tell the phy to go from rx to tx */
        if (pdu_type == BLE_ADV_PDU_TYPE_SCAN_REQ) {
            rc = 1;
        } else if (pdu_type == BLE_ADV_PDU_TYPE_CONNECT_REQ) {
            rc = 0;
        } else {
            /* This is a frame we dont want. Just abort it */
            rc = -1;
        }
        break;
    default:
        rc = -1;
        assert(0);
        break;
    }

    return rc;
}

/**
 * ll rx end 
 *  
 * Called by the PHY when a receive packet has ended. 
 *  
 * NOTE: Called from interrupt context! 
 * 
 * @param rxbuf 
 * 
 * @return int 
 */
int
ll_rx_end(struct os_mbuf *rxpdu, int crcok)
{
    int rc;
    uint8_t pdu_type;
    uint8_t pdu_len;
    uint16_t mblen;
    uint8_t *rxbuf;

    /* Set the rx buffer pointer to the start of the received data */
    rxbuf = rxpdu->om_data;

    /* XXX: need to check if this is an adv channel or data channel */
    pdu_type = rxbuf[0] & BLE_ADV_HDR_PDU_TYPE_MASK;
    pdu_len = rxbuf[1] & BLE_ADV_HDR_LEN_MASK;

    /* Setup the mbuf */
    mblen = pdu_len + BLE_LL_PDU_HDR_LEN;
    OS_MBUF_PKTHDR(rxpdu)->omp_len = mblen;
    rxpdu->om_len = mblen;

    rc = -1;
    switch (g_ll_data.ll_state) {
    case BLE_LL_STATE_ADV:
        /* If we get a scan request*/
        if (pdu_type == BLE_ADV_PDU_TYPE_SCAN_REQ) {
            /* Just bail if CRC is not good */
            if (crcok) {
                /* XXX: this does not deal with random address yet */
                if (pdu_len == BLE_SCAN_REQ_LEN) {
                    rc = ll_adv_rx_scan_req(rxbuf);
                    if (rc) {
                        /* XXX: One thing left to reconcile here. We have
                         * the advertisement schedule element still running.
                         * How to deal with the end of the advertising event?
                         * Need to figure that out.
                         */
                        /* XXX: not sure I need to do anything here on success
                           */
                    }
                } else {
                    ++g_ll_stats.rx_malformed_pkts;
                }
            }
        } else {
            if (pdu_type == BLE_ADV_PDU_TYPE_CONNECT_REQ) {
                rc = 0;
                /* XXX: deal with this */
            }
        }

        break;
    default:
        assert(0);
        break;
    }

    /* 
     * XXX: could move these to task level. Need to determine that. For
     * now, just keep here. We need to create the ble mbuf header
     */
    /* Count statistics */
    if (crcok) {
        ++g_ll_stats.rx_crc_ok;
    } else {
        ++g_ll_stats.rx_crc_fail;
    }

    /* The total bytes count the PDU header and PDU payload */
    g_ll_stats.rx_bytes += BLE_LL_PDU_HDR_LEN + pdu_len;
    /* XXX */

    /* Hand packet up to higher layer */
    ll_rx_pdu_in(rxpdu);

    return rc;
}

void
ll_task(void *arg)
{
    struct os_event *ev;

    /* Init ble phy */
    ble_phy_init();

    /* Set output power to 1mW (0 dBm) */
    ble_phy_txpwr_set(0);

    /* Wait for an event */
    while (1) {
        ev = os_eventq_get(&g_ll_data.ll_evq);
        switch (ev->ev_type) {
        case OS_EVENT_T_TIMER:
            break;
        case BLE_LL_EVENT_HCI_CMD:
            /* Process HCI command */
            ll_hci_cmd_proc(ev);
            break;
        case BLE_LL_EVENT_ADV_TXDONE:
            ll_adv_tx_done_proc(ev->ev_arg);
            break;
        case BLE_LL_EVENT_RX_PKT_IN:
            ll_rx_pkt_in_proc();
            break;
        default:
            /* XXX: unknown event? What should we do? */
            break;
        }

        /* XXX: we can possibly take any finished schedule items and
           free them here. Have a queue for them. */
    }
}

int
ll_init(void)
{
    /* Initialize the receive queue */
    STAILQ_INIT(&g_ll_data.ll_rx_pkt_q);

    /* Initialize eventq */
    os_eventq_init(&g_ll_data.ll_evq);

    /* Init the scheduler */
    ll_sched_init();

    /* Initialize advertising code */
    ll_adv_init();

    /* Initialize the LL task */
    os_task_init(&g_ll_task, "ble_ll", ll_task, NULL, BLE_LL_TASK_PRI, 
                 OS_WAIT_FOREVER, g_ll_stack, BLE_LL_STACK_SIZE);

    return 0;
}

