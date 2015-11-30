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
#include "os/os.h"
#include "nimble/ble.h"
#include "controller/ble_phy.h"
#include "controller/ble_ll.h"
#include "controller/ble_ll_adv.h"
#include "controller/ble_ll_sched.h"
#include "controller/ble_ll_scan.h"
#include "controller/ble_ll_conn.h"
#include "controller/ble_ll_hci.h"
#include "hal/hal_cputime.h"

/* 
 * XXX: Just thought of something! Can I always set the DEVMATCH bit at
 * the lower layer? This way the LL just looks at devmatch when it wants
 * to see if a packet is "for us". I am referring to packets that pass
 * whitelisting but also need to be "for us" (connect requests, scan requests,
 * scan responses, etc). Look into this. This way I would not need to do
 * additional whitelist checks at the upper layer.
 */

/* 
 * XXX: I need to re-think the whoele LL state code and how I deal with it.
 * I dont think I am handling it very well. The LL state should only change
 * at the LL task I think. I am also not sure how to make sure that any packets
 * handled by the LL are handled by the appropriate state. For example, can I
 * get a scan window end and then process packets? When the schedule event for
 * the scan window ends, what do I do to the LL state? Check all this out.
 */

/* XXX:
 * 
 * 1) use the sanity task!
 * 2) Need to figure out what to do with packets that we hand up that did
 * not pass the filter policy for the given state. Currently I count all
 * packets I think. Need to figure out what to do with this.
 * 
 */

/* The global BLE LL data object */
struct ble_ll_obj g_ble_ll_data;

/* Global link layer statistics */
struct ble_ll_stats g_ble_ll_stats;

/* The BLE LL task data structure */
#define BLE_LL_TASK_PRI     (OS_TASK_PRI_HIGHEST)
#define BLE_LL_STACK_SIZE   (128)
struct os_task g_ble_ll_task;
os_stack_t g_ble_ll_stack[BLE_LL_STACK_SIZE];

/**
 * Counts the number of advertising PDU's by type. 
 * 
 * @param pdu_type 
 */
static void
ble_ll_count_rx_adv_pdus(uint8_t pdu_type)
{
    /* Count received packet types  */
    switch (pdu_type) {
    case BLE_ADV_PDU_TYPE_ADV_IND:
        ++g_ble_ll_stats.rx_adv_ind;
        break;
    case BLE_ADV_PDU_TYPE_ADV_DIRECT_IND:
        /* XXX: Do I want to count these if they are not for me? */
        ++g_ble_ll_stats.rx_adv_direct_ind;
        break;
    case BLE_ADV_PDU_TYPE_ADV_NONCONN_IND:
        ++g_ble_ll_stats.rx_adv_nonconn_ind;
        break;
    case BLE_ADV_PDU_TYPE_SCAN_REQ:
        /* XXX: Do I want to count these if they are not for me? */
        ++g_ble_ll_stats.rx_scan_reqs;
        break;
    case BLE_ADV_PDU_TYPE_SCAN_RSP:
        ++g_ble_ll_stats.rx_scan_rsps;
        break;
    case BLE_ADV_PDU_TYPE_CONNECT_REQ:
        /* XXX: Do I want to count these if they are not for me? */
        ++g_ble_ll_stats.rx_connect_reqs;
        break;
    case BLE_ADV_PDU_TYPE_ADV_SCAN_IND:
        ++g_ble_ll_stats.rx_scan_ind;
        break;
    default:
        ++g_ble_ll_stats.rx_unk_pdu;
        break;
    }
}

int
ble_ll_is_resolvable_priv_addr(uint8_t *addr)
{
    /* XXX: implement this */
    return 0;
}

/* Checks to see that the device is a valid random address */
int
ble_ll_is_valid_random_addr(uint8_t *addr)
{
    int i;
    int rc;
    uint16_t sum;
    uint8_t addr_type;

    /* Make sure all bits are neither one nor zero */
    sum = 0;
    for (i = 0; i < (BLE_DEV_ADDR_LEN -1); ++i) {
        sum += addr[i];
    }
    sum += addr[5] & 0x3f;

    if ((sum == 0) || (sum == ((5*255) + 0x3f))) {
        return 0;
    }

    /* Get the upper two bits of the address */
    rc = 1;
    addr_type = addr[5] & 0xc0;
    if (addr_type == 0xc0) {
        /* Static random address. No other checks needed */
    } else if (addr_type == 0x40) {
        /* Resolvable */
        sum = addr[3] + addr[4] + (addr[5] & 0x3f);
        if ((sum == 0) || (sum == (255 + 255 + 0x3f))) {
            rc = 0;
        }
    } else if (addr_type == 0) {
        /* non-resolvable. Cant be equal to public */
        if (!memcmp(g_dev_addr, addr, BLE_DEV_ADDR_LEN)) {
            rc = 0;
        }
    } else {
        /* Invalid upper two bits */
        rc = 0;
    }

    return rc;
}

/**
 * Called from the HCI command parser when the set random address command 
 * is received. 
 *  
 * Context: Link Layer task (HCI command parser) 
 * 
 * @param addr Pointer to address
 * 
 * @return int 0: success
 */
int
ble_ll_set_random_addr(uint8_t *addr)
{
    int rc;

    rc = BLE_ERR_INV_HCI_CMD_PARMS;
    if (ble_ll_is_valid_random_addr(addr)) {
        memcpy(g_random_addr, addr, BLE_DEV_ADDR_LEN);
        rc = BLE_ERR_SUCCESS;
    }

    return rc;
}

/**
 * Checks to see if an address is our device address (either public or 
 * random) 
 * 
 * @param addr 
 * @param addr_type 
 * 
 * @return int 
 */
int
ble_ll_is_our_devaddr(uint8_t *addr, int addr_type)
{
    int rc;
    uint8_t *our_addr;

    if (addr_type) {
        our_addr = g_random_addr;
    } else {
        our_addr = g_dev_addr;
    }

    rc = 0;
    if (!memcmp(our_addr, addr, BLE_DEV_ADDR_LEN)) {
        rc = 1;
    }

    return rc;
}

/**
 * ll pdu tx time get 
 *  
 * Returns the number of usecs it will take to transmit a PDU of length 'len' 
 * bytes. Each byte takes 8 usecs. 
 * 
 * @param len The number of PDU bytes to transmit
 * 
 * @return uint16_t The number of usecs it will take to transmit a PDU of 
 *                  length 'len' bytes. 
 */
uint16_t
ble_ll_pdu_tx_time_get(uint16_t len)
{
    len += BLE_LL_OVERHEAD_LEN;
    len = len << 3;
    return len;
}

/**
 * Wait for response timeout function 
 *  
 * Context: interrupt (ble scheduler) 
 * 
 * @param arg 
 */
void
ble_ll_wfr_timer_exp(void *arg)
{
    struct ble_ll_obj *lldata;

    lldata = &g_ble_ll_data;
    lldata->ll_wfr_func(arg);
}

/**
 * Enable the wait for response timer 
 * 
 * @param cputime 
 * @param wfr_cb 
 * @param arg 
 */
void
ble_ll_wfr_enable(uint32_t cputime, ble_ll_wfr_func wfr_cb, void *arg)
{
    /* XXX: should I reset func callback here too? Not use a global
       ble_ll_wfr_timer expiration? */
    g_ble_ll_data.ll_wfr_func = wfr_cb;
    g_ble_ll_data.ll_wfr_timer.arg = arg;
    cputime_timer_start(&g_ble_ll_data.ll_wfr_timer, cputime);
}

/**
 * Disable the wait for response timer
 */
void
ble_ll_wfr_disable(void)
{
    cputime_timer_stop(&g_ble_ll_data.ll_wfr_timer);
}

/**
 * ll rx pkt in proc
 *  
 * Process received packet from PHY.
 *  
 * Context: Link layer task
 *  
 */
void
ble_ll_rx_pkt_in_proc(void)
{
    os_sr_t sr;
    uint8_t pdu_type;
    uint8_t *rxbuf;
    struct os_mbuf_pkthdr *pkthdr;
    struct ble_mbuf_hdr *ble_hdr;
    struct os_mbuf *m;

    /* Drain all packets off the queue */
    while (STAILQ_FIRST(&g_ble_ll_data.ll_rx_pkt_q)) {
        /* Get mbuf pointer from packet header pointer */
        pkthdr = STAILQ_FIRST(&g_ble_ll_data.ll_rx_pkt_q);
        m = (struct os_mbuf *)((uint8_t *)pkthdr - sizeof(struct os_mbuf));

        /* Remove from queue */
        OS_ENTER_CRITICAL(sr);
        STAILQ_REMOVE_HEAD(&g_ble_ll_data.ll_rx_pkt_q, omp_next);
        OS_EXIT_CRITICAL(sr);

        /* Count statistics */
        rxbuf = m->om_data;
        ble_hdr = BLE_MBUF_HDR_PTR(m); 
        if (ble_hdr->crcok) {
            /* The total bytes count the PDU header and PDU payload */
            g_ble_ll_stats.rx_bytes += pkthdr->omp_len;
            ++g_ble_ll_stats.rx_crc_ok;
        } else {
            ++g_ble_ll_stats.rx_crc_fail;
        }

        if (ble_hdr->channel < BLE_PHY_NUM_DATA_CHANS) {
            ble_ll_conn_rx_data_pdu(m, ble_hdr->crcok);
        } else {
            if (ble_hdr->crcok) {
                /* Get advertising PDU type */
                pdu_type = rxbuf[0] & BLE_ADV_PDU_HDR_TYPE_MASK;
                ble_ll_count_rx_adv_pdus(pdu_type);

                /* Process the PDU */
                switch (g_ble_ll_data.ll_state) {
                case BLE_LL_STATE_ADV:
                    if (pdu_type == BLE_ADV_PDU_TYPE_CONNECT_REQ) {
                        ble_ll_adv_conn_req_rxd(rxbuf, ble_hdr->flags);
                    }
                    break;
                case BLE_LL_STATE_SCANNING:
                    ble_ll_scan_rx_pdu_proc(pdu_type, rxbuf, ble_hdr->rssi,
                                            ble_hdr->flags);

                    /* We need to re-enable the PHY if we are in idle state */
                    if (ble_phy_state_get() == BLE_PHY_STATE_IDLE) {
                        /* XXX: If this returns error, we will need to attempt
                         * to re-start scanning! */
                        ble_phy_rx();
                    }
                    break;
                case BLE_LL_STATE_INITIATING:
                    ble_ll_init_rx_pdu_proc(rxbuf, ble_hdr);
                    break;
                default:
                    /* Any other state should never occur */
                    assert(0);
                    break;
                }
            }

            /* Free the packet buffer */
            os_mbuf_free(m);
        }
    }
}

/**
 * Called to put a packet on the Link Layer receive packet queue. 
 * 
 * @param rxpdu Pointer to received PDU
 */
void
ble_ll_rx_pdu_in(struct os_mbuf *rxpdu)
{
    struct os_mbuf_pkthdr *pkthdr;

    pkthdr = OS_MBUF_PKTHDR(rxpdu);
    STAILQ_INSERT_TAIL(&g_ble_ll_data.ll_rx_pkt_q, pkthdr, omp_next);
    os_eventq_put(&g_ble_ll_data.ll_evq, &g_ble_ll_data.ll_rx_pkt_ev);
}

/** 
 * Called upon start of received PDU 
 * 
 * @param rxpdu 
 *        chan 
 * 
 * @return int 
 *   < 0: A frame we dont want to receive.
 *   = 0: Continue to receive frame. Dont go from rx to tx
 *   > 0: Continue to receive frame and go from rx to tx when done
 */
int
ble_ll_rx_start(struct os_mbuf *rxpdu, uint8_t chan)
{
    int rc;
    uint8_t pdu_type;
    uint8_t *rxbuf;

    /* Check channel type */
    rxbuf = rxpdu->om_data;
    if (chan < BLE_PHY_NUM_DATA_CHANS) {
        /* 
         * Data channel pdu. We should be in CONNECTION state with an
         * ongoing connection
         */
        /* XXX: check access address for surety? What to do... */
        if (g_ble_ll_data.ll_state == BLE_LL_STATE_CONNECTION) {
            /* Call conection pdu rx start function */
            ble_ll_conn_rx_pdu_start();

            /* Set up to go from rx to tx */
            rc = 1;
        } else {
            ++g_ble_ll_stats.bad_ll_state;
            rc = 0;
        }
        return rc;
    } 

    /* Advertising channel PDU */
    pdu_type = rxbuf[0] & BLE_ADV_PDU_HDR_TYPE_MASK;

    switch (g_ble_ll_data.ll_state) {
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
    case BLE_LL_STATE_INITIATING:
        if ((pdu_type == BLE_ADV_PDU_TYPE_ADV_IND) ||
            (pdu_type == BLE_ADV_PDU_TYPE_ADV_DIRECT_IND)) {
            rc = 1;
        } else {
            rc = 0;
        }
        break;
    case BLE_LL_STATE_SCANNING:
        rc = ble_ll_scan_rx_pdu_start(pdu_type, rxpdu);
        break;
    case BLE_LL_STATE_CONNECTION:
        /* XXX: This should not happen. What to do? */
        rc = 0;
        break;
    default:
        /* Should not be in this state! */
        rc = -1;
        ++g_ble_ll_stats.bad_ll_state;
        break;
    }

    return rc;
}

/**
 * Called by the PHY when a receive packet has ended. 
 *  
 * NOTE: Called from interrupt context!
 * 
 * @param rxbuf 
 * 
 * @return int 
 *       < 0: Disable the phy after reception.
 *      == 0: Success. Do not disable the PHY.
 *       > 0: Do not disable PHY as that has already been done.
 */
int
ble_ll_rx_end(struct os_mbuf *rxpdu, uint8_t chan, uint8_t crcok)
{
    int rc;
    int badpkt;
    uint8_t pdu_type;
    uint8_t len;
    uint16_t mblen;
    uint8_t *rxbuf;

    /* Set the rx buffer pointer to the start of the received data */
    rxbuf = rxpdu->om_data;

    /* Check channel type */
    if (chan < BLE_PHY_NUM_DATA_CHANS) {
        /* Better be in connection state */
        assert(g_ble_ll_data.ll_state == BLE_LL_STATE_CONNECTION);

        /* Set length in the received PDU */
        mblen = rxbuf[1] + BLE_LL_PDU_HDR_LEN;
        OS_MBUF_PKTHDR(rxpdu)->omp_len = mblen;
        rxpdu->om_len = mblen;

        /* 
         * NOTE: this looks a bit odd, and it is, but for now we place the
         * received PDU on the Link Layer task before calling the rx end
         * function. We do this to guarantee connection event end ordering
         * and receive PDU processing.
         */
        ble_ll_rx_pdu_in(rxpdu);

        /* 
         * Data channel pdu. We should be in CONNECTION state with an
         * ongoing connection.
         */
        /* XXX: check access address for surety? What to do... */
        rc = ble_ll_conn_rx_pdu_end(rxpdu, crcok);
        return rc;
    } 

    /* Get advertising PDU type */
    pdu_type = rxbuf[0] & BLE_ADV_PDU_HDR_TYPE_MASK;
    len = rxbuf[1] & BLE_ADV_PDU_HDR_LEN_MASK;

    /* XXX: Should I do this at LL task context? */
    /* If the CRC checks, make sure lengths check! */
    badpkt = 0;
    if (crcok) {
        switch (pdu_type) {
        case BLE_ADV_PDU_TYPE_SCAN_REQ:
        case BLE_ADV_PDU_TYPE_ADV_DIRECT_IND:
            if (len != BLE_SCAN_REQ_LEN) {
                badpkt = 1;
            }
            break;
        case BLE_ADV_PDU_TYPE_SCAN_RSP:
        case BLE_ADV_PDU_TYPE_ADV_IND:
        case BLE_ADV_PDU_TYPE_ADV_SCAN_IND:
        case BLE_ADV_PDU_TYPE_ADV_NONCONN_IND:
            if ((len < BLE_DEV_ADDR_LEN) || (len > BLE_ADV_SCAN_IND_MAX_LEN)) {
                badpkt = 1;
            }
            break;
        case BLE_ADV_PDU_TYPE_CONNECT_REQ:
            if (len != BLE_CONNECT_REQ_LEN) {
                badpkt = 1;
            }
            break;
        default:
            badpkt = 1;
            break;
        }
    }

    /* If this is a malformed packet, just kill it here */
    if (badpkt) {
        ++g_ble_ll_stats.rx_malformed_pkts;
        os_mbuf_free(rxpdu);
        return -1;
    }

    /* Setup the mbuf */
    mblen = len + BLE_LL_PDU_HDR_LEN;
    OS_MBUF_PKTHDR(rxpdu)->omp_len = mblen;
    rxpdu->om_len = mblen;

    rc = -1;
    switch (g_ble_ll_data.ll_state) {
    case BLE_LL_STATE_ADV:
        if (crcok) {
            rc = ble_ll_adv_rx_pdu_end(pdu_type, rxpdu);
        }
        break;
    case BLE_LL_STATE_SCANNING:
        if (crcok) {
            rc = ble_ll_scan_rx_pdu_end(rxpdu);
        }
        break;
    case BLE_LL_STATE_INITIATING:
        if (crcok) {
            rc = ble_ll_init_rx_pdu_end(rxpdu);
        }
        break;
    case BLE_LL_STATE_CONNECTION:
        /* XXX: we should never get here! What to do??? */
        break;
    default:
        /* This is an invalid state. */
        assert(0);
        break;
    }

    /* Hand packet up to higher layer */
    ble_ll_rx_pdu_in(rxpdu);

    return rc;
}

void
ble_ll_task(void *arg)
{
    struct os_event *ev;

    /* Init ble phy */
    ble_phy_init();

    /* Set output power to 1mW (0 dBm) */
    ble_phy_txpwr_set(0);

    /* Wait for an event */
    while (1) {
        ev = os_eventq_get(&g_ble_ll_data.ll_evq);
        switch (ev->ev_type) {
        case OS_EVENT_T_TIMER:
            break;
        case BLE_LL_EVENT_HCI_CMD:
            /* Process HCI command */
            ble_ll_hci_cmd_proc(ev);
            break;
        case BLE_LL_EVENT_ADV_TXDONE:
            ble_ll_adv_tx_done_proc(ev->ev_arg);
            break;
        case BLE_LL_EVENT_SCAN_WIN_END:
            ble_ll_scan_win_end_proc(ev->ev_arg);
            break;
        case BLE_LL_EVENT_RX_PKT_IN:
            ble_ll_rx_pkt_in_proc();
            break;
        case BLE_LL_EVENT_CONN_SPVN_TMO:
            ble_ll_conn_spvn_timeout(ev->ev_arg);
            break;
        case BLE_LL_EVENT_CONN_EV_END:
            ble_ll_conn_event_end(ev->ev_arg);
            break;
        default:
            assert(0);
            break;
        }

        /* XXX: we can possibly take any finished schedule items and
           free them here. Have a queue for them. */
    }
}

/**
 * ble ll state set
 *  
 * Called to set the current link layer state. 
 *  
 * Context: Interrupt and Link Layer task
 * 
 * @param ll_state 
 */
void
ble_ll_state_set(int ll_state)
{
    g_ble_ll_data.ll_state = ll_state;
}

/**
 * ble ll event send
 *  
 * Send an event to the Link Layer task 
 * 
 * @param ev Event to add to the Link Layer event queue.
 */
void
ble_ll_event_send(struct os_event *ev)
{
    os_eventq_put(&g_ble_ll_data.ll_evq, ev);
}

/**
 * Initialize the Link Layer. Should be called only once 
 * 
 * @return int 
 */
int
ble_ll_init(void)
{
    struct ble_ll_obj *lldata;

    /* Get pointer to global data object */
    lldata = &g_ble_ll_data;

    /* Initialize the receive queue */
    STAILQ_INIT(&lldata->ll_rx_pkt_q);

    /* Initialize eventq */
    os_eventq_init(&lldata->ll_evq);

    /* Initialize receive packet (from phy) event */
    lldata->ll_rx_pkt_ev.ev_type = BLE_LL_EVENT_RX_PKT_IN;

    /* Initialize wait for response timer */
    cputime_timer_init(&g_ble_ll_data.ll_wfr_timer, ble_ll_wfr_timer_exp, 
                       NULL);

    /* Initialize LL HCI */
    ble_ll_hci_init();

    /* Init the scheduler */
    ble_ll_sched_init();

    /* Initialize advertiser */
    ble_ll_adv_init();

    /* Initialize a scanner */
    ble_ll_scan_init();

    /* Initialize the connection module */
    ble_ll_conn_init();

    /* Initialize the LL task */
    os_task_init(&g_ble_ll_task, "ble_ll", ble_ll_task, NULL, BLE_LL_TASK_PRI, 
                 OS_WAIT_FOREVER, g_ble_ll_stack, BLE_LL_STACK_SIZE);

    return 0;
}

