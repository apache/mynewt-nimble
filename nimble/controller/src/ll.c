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
#include "controller/phy.h"
#include "controller/ll.h"
#include "controller/ll_adv.h"
#include "controller/ll_sched.h"
#include "controller/ll_scan.h"
#include "controller/ll_hci.h"

/* XXX: use the sanity task! */

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

static void
ble_ll_count_rx_pkts(uint8_t pdu_type)
{
    /* Count received packet types  */
    switch (pdu_type) {
    case BLE_ADV_PDU_TYPE_ADV_IND:
        ++g_ll_stats.rx_adv_ind;
        break;
    case BLE_ADV_PDU_TYPE_ADV_DIRECT_IND:
        /* XXX: Do I want to count these if they are not for me? */
        ++g_ll_stats.rx_adv_direct_ind;
        break;
    case BLE_ADV_PDU_TYPE_ADV_NONCONN_IND:
        ++g_ll_stats.rx_adv_nonconn_ind;
        break;
    case BLE_ADV_PDU_TYPE_SCAN_REQ:
        /* XXX: Do I want to count these if they are not for me? */
        ++g_ll_stats.rx_scan_reqs;
        break;
    case BLE_ADV_PDU_TYPE_SCAN_RSP:
        ++g_ll_stats.rx_scan_rsps;
        break;
    case BLE_ADV_PDU_TYPE_CONNECT_REQ:
        /* XXX: Do I want to count these if they are not for me? */
        ++g_ll_stats.rx_connect_reqs;
        break;
    case BLE_ADV_PDU_TYPE_ADV_SCAN_IND:
        ++g_ll_stats.rx_scan_ind;
        break;
    default:
        ++g_ll_stats.rx_unk_pdu;
        break;
    }
}


int
ble_ll_is_on_whitelist(uint8_t *addr, int addr_type)
{
    /* XXX: implement this */
    return 1;
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

int
ble_ll_is_our_devaddr(uint8_t *addr, int addr_type)
{
    int rc;
    uint8_t *our_addr;

    rc = 0;
    if (addr_type) {
        our_addr = g_dev_addr;
    } else {
        our_addr = g_random_addr;
    }

    rc = 0;
    if (!memcmp(our_addr, g_random_addr, BLE_DEV_ADDR_LEN)) {
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
ll_pdu_tx_time_get(uint16_t len)
{
    len += BLE_LL_OVERHEAD_LEN;
    len = len << 3;
    return len;
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
    struct ble_mbuf_hdr *ble_hdr;
    struct os_mbuf *m;

    /* Drain all packets off the queue */
    while (STAILQ_FIRST(&g_ll_data.ll_rx_pkt_q)) {
        /* Get mbuf pointer from packet header pointer */
        pkthdr = STAILQ_FIRST(&g_ll_data.ll_rx_pkt_q);
        m = (struct os_mbuf *)((uint8_t *)pkthdr - sizeof(struct os_mbuf));

        /* Remove from queue */
        OS_ENTER_CRITICAL(sr);
        STAILQ_REMOVE_HEAD(&g_ll_data.ll_rx_pkt_q, omp_next);
        OS_EXIT_CRITICAL(sr);

        /* XXX: need to check if this is an adv channel or data channel */

        /* Count statistics */
        rxbuf = m->om_data;
        pdu_type = rxbuf[0] & BLE_ADV_PDU_HDR_TYPE_MASK;
        ble_hdr = BLE_MBUF_HDR_PTR(m); 
        if (ble_hdr->crcok) {
            /* The total bytes count the PDU header and PDU payload */
            g_ll_stats.rx_bytes += pkthdr->omp_len;
            ++g_ll_stats.rx_crc_ok;
            ble_ll_count_rx_pkts(pdu_type);
        } else {
            ++g_ll_stats.rx_crc_fail;
        }

        /* 
         * XXX: The reason I dont bail earlier on bad CRC is that
         * there may be some connection stuff I need to do with a packet
         * that has failed the CRC.
         */ 

        /* Process the PDU */
        switch (g_ll_data.ll_state) {
        case BLE_LL_STATE_ADV:
            /* XXX: implement this */
            break;
        case BLE_LL_STATE_SCANNING:
            if (ble_hdr->crcok) {
                ble_ll_scan_rx_pdu_proc(pdu_type, rxbuf, ble_hdr->rssi);
            }

            /* We need to re-enable the PHY if we are in idle state */
            if (ble_phy_state_get() == BLE_PHY_STATE_IDLE) {
                /* XXX: If this returns error, we will need to attempt to
                   re-start scanning! */
                ble_phy_rx();
            }
            break;
        default:
            /* XXX: implement */
            assert(0);
            break;
        }

        /* XXX: Free the mbuf for now */
        os_mbuf_free(&g_mbuf_pool, m);
    }
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

/**
 * ll rx start
 * 
 * 
 * @param rxpdu 
 * 
 * @return int 
 *   < 0: A frame we dont want to receive.
 *   = 0: Continue to receive frame. Dont go from rx to tx
 *   > 1: Continue to receive frame and go from rx to idle when done
 */
int
ll_rx_start(struct os_mbuf *rxpdu)
{
    int rc;
    uint8_t pdu_type;
    uint8_t *rxbuf;

    /* XXX: need to check if this is an adv channel or data channel */
    rxbuf = rxpdu->om_data;
    pdu_type = rxbuf[0] & BLE_ADV_PDU_HDR_TYPE_MASK;

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
    case BLE_LL_STATE_SCANNING:
        rc = ble_ll_scan_rx_pdu_start(pdu_type, rxpdu);
        break;
    default:
        /* XXX: should we really assert here? What to do... */
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
 *       < 0: Disable the phy after reception.
 *      == 0: Success. Do not disable the PHY.
 *       > 0: Do not disable PHY as that has already been done.
 */
int
ll_rx_end(struct os_mbuf *rxpdu, uint8_t crcok)
{
    int rc;
    int badpkt;
    uint8_t pdu_type;
    uint8_t len;
    uint16_t mblen;
    uint8_t *rxbuf;

    /* Set the rx buffer pointer to the start of the received data */
    rxbuf = rxpdu->om_data;

    /* XXX: need to check if this is an adv channel or data channel */
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
        ++g_ll_stats.rx_malformed_pkts;
        os_mbuf_free(&g_mbuf_pool, rxpdu);
        return -1;
    }

    /* Setup the mbuf */
    mblen = len + BLE_LL_PDU_HDR_LEN;
    OS_MBUF_PKTHDR(rxpdu)->omp_len = mblen;
    rxpdu->om_len = mblen;

    rc = -1;
    switch (g_ll_data.ll_state) {
    case BLE_LL_STATE_ADV:
        /* If we get a scan request*/
        if (pdu_type == BLE_ADV_PDU_TYPE_SCAN_REQ) {
            /* Just bail if CRC is not good */
            if (crcok) {
                rc = ble_ll_adv_rx_scan_req(rxbuf);
                if (rc) {
                    /* XXX: One thing left to reconcile here. We have
                     * the advertisement schedule element still running.
                     * How to deal with the end of the advertising event?
                     * Need to figure that out.
                     */
                }
            }
        } else {
            if (pdu_type == BLE_ADV_PDU_TYPE_CONNECT_REQ) {
                rc = 0;
                /* XXX: deal with this */
            }
        }
        break;
    case BLE_LL_STATE_SCANNING:
        if (crcok) {
            /* 
             * NOTE: If this returns a positive number there was an error but
             * there is no need to disable the PHY on return as that was
             * done already.
             */
            rc = ble_ll_scan_rx_pdu_end(rxbuf);
        }
        break;
    default:
        assert(0);
        break;
    }

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
            ble_ll_hci_cmd_proc(ev);
            break;
        case BLE_LL_EVENT_ADV_TXDONE:
            ll_adv_tx_done_proc(ev->ev_arg);
            break;
        case BLE_LL_EVENT_SCAN_WIN_END:
            ble_ll_scan_win_end_proc(ev->ev_arg);
            break;
        case BLE_LL_EVENT_RX_PKT_IN:
            ll_rx_pkt_in_proc();
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
    g_ll_data.ll_state = ll_state;
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
    os_eventq_put(&g_ll_data.ll_evq, ev);
}

/**
 * Initialize the Link Layer. Should be called only once 
 * 
 * @return int 
 */
int
ll_init(void)
{
    /* Initialize the receive queue */
    STAILQ_INIT(&g_ll_data.ll_rx_pkt_q);

    /* Initialize eventq */
    os_eventq_init(&g_ll_data.ll_evq);

    /* Initialize receive packet (from phy) event */
    g_ll_data.ll_rx_pkt_ev.ev_type = BLE_LL_EVENT_RX_PKT_IN;

    /* Initialize LL HCI */
    ble_ll_hci_init();

    /* Init the scheduler */
    ll_sched_init();

    /* Initialize advertiser */
    ll_adv_init();

    /* Initialize a scanner */
    ble_ll_scan_init();

    /* Initialize the LL task */
    os_task_init(&g_ll_task, "ble_ll", ll_task, NULL, BLE_LL_TASK_PRI, 
                 OS_WAIT_FOREVER, g_ll_stack, BLE_LL_STACK_SIZE);

    return 0;
}

