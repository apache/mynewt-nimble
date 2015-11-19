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
#include <string.h>
#include <assert.h>
#include "bsp/bsp.h"
#include "os/os.h"
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "controller/ble_ll.h"
#include "controller/ble_ll_hci.h"
#include "controller/ble_ll_conn.h"
#include "controller/ble_ll_scan.h"
#include "controller/ble_ll_whitelist.h"
#include "controller/ble_ll_sched.h"
#include "controller/ble_phy.h"
#include "hal/hal_cputime.h"

/* XXX TODO
 * 1) Implemement connection supervisor timeout.
 *  -> Need to re-start when we receive a packet and set the proper timeout.
 * 2) Add set channel map command and implement channel change procedure.
 * 3) Closing the connection: the MD bit.
 * 4) notifying host about connections: how often do we need to do this? On
 * creation, establishment and/or loss?
 * 5) Deal with window widening. Easy...
 * 6) Close connection if window widening gets too big! 4.5.7
 * 7) How does state get set to ESTABLISHED? Do that...
 * 8) Set up later connection schedule events.
 * 8.5) How do we set the end event? Especially if we are receiving? Need
 * to figure this out.
 * 9) Implement flow control and all that.
 * 10) Implement getting a data packet. How do we do that?
 * 11) Send connection complete event when connection dies.
 * 12) Data channel index and all that code. Must make sure we are setting this
 * correctly. Set for 1st connection event but not for other events.
 * 13) Add more statistics
 * 14) Update event counter with each event!
 * 15) Link layer control procedures and timers
 * 16) Did we implement what happens if we receive a connection request from
 * a device we are already connected to? We also need to check to see if we
 * were asked to create a connection when one already exists. Note that
 * an initiator should only send connection requests to devices it is not
 * already connected to. Make sure this cant happen.
 * 17) Dont forget to set pkt_rxd flag for slave latency. Needs to get reset
 * after each M-S transaction.
 * 18) Make sure we check incoming data packets for size and all that. You
 * know, supported octets and all that. For both rx and tx.
 * 19) Make sure we handle rsp_rxd and pkt_rxd correctly (clearing/checking
 * setting them).
 */

/* Connection event timing */
#define BLE_LL_CONN_ITVL_USECS              (1250)
#define BLE_LL_CONN_TX_WIN_USECS            (1250)
#define BLE_LL_CONN_CE_USECS                (625)

/* XXX: probably should be moved and made more accurate */
#define BLE_LL_WFR_USECS                    (100)

/* Channel map size */
#define BLE_LL_CONN_CHMAP_LEN               (5)

/* Configuration parameters */
#define BLE_LL_CONN_CFG_TX_WIN_SIZE         (1)
#define BLE_LL_CONN_CFG_TX_WIN_OFF          (0)
#define BLE_LL_CONN_CFG_MASTER_SCA          (BLE_MASTER_SCA_251_500_PPM << 5)
#define BLE_LL_CONN_CFG_MAX_CONNS           (8)

/* LL configuration definitions */
#define BLE_LL_CFG_SUPP_MAX_RX_BYTES        (27)
#define BLE_LL_CFG_SUPP_MAX_TX_BYTES        (27)
#define BLE_LL_CFG_CONN_INIT_MAX_TX_BYTES   (27)

/* Roles */
#define BLE_LL_CONN_ROLE_NONE               (0)
#define BLE_LL_CONN_ROLE_MASTER             (1)
#define BLE_LL_CONN_ROLE_SLAVE              (2)

/* Connection states */
#define BLE_LL_CONN_STATE_IDLE              (0)
#define BLE_LL_CONN_STATE_CREATED           (1)
#define BLE_LL_CONN_STATE_ESTABLISHED       (2)

/* Data Lenth Procedure */
#define BLE_LL_CONN_SUPP_TIME_MIN           (328)   /* usecs */
#define BLE_LL_CONN_SUPP_BYTES_MIN          (27)    /* bytes */

/* Connection request */
#define BLE_LL_CONN_REQ_ADVA_OFF    (BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN)

/* 
 * XXX: I just had a thought. Why do I need to allocate connection handles?
 * For every connection state machine I have I should just give it a handle
 * inside it. They can sit on the pool and I can use them in round robin
 * order. This might save some code! This works only if I create the pool
 * of connections to start.
 */

/* Global Link Layer connection parameters */
struct ble_ll_conn_global_params
{
    uint8_t supp_max_tx_octets;
    uint8_t supp_max_rx_octets;
    uint8_t conn_init_max_tx_octets;
    uint16_t conn_init_max_tx_time;
    uint16_t supp_max_tx_time;
    uint16_t supp_max_rx_time;
};

struct ble_ll_conn_global_params g_ble_ll_conn_params;

/* Connection state machine */
struct ble_ll_conn_sm
{
    /* Current connection state and role */
    uint8_t conn_state;
    uint8_t conn_role;

    /* Connection data length management */
    uint8_t max_tx_octets;
    uint8_t max_rx_octets;
    uint8_t rem_max_tx_octets;
    uint8_t rem_max_rx_octets;
    uint8_t eff_max_tx_octets;
    uint8_t eff_max_rx_octets;
    uint16_t max_tx_time;
    uint16_t max_rx_time;
    uint16_t rem_max_tx_time;
    uint16_t rem_max_rx_time;
    uint16_t eff_max_tx_time;
    uint16_t eff_max_rx_time;

    /* Used to calculate data channel index for connection */
    uint8_t chanmap[BLE_LL_CONN_CHMAP_LEN];
    uint8_t hop_inc;
    uint8_t data_chan_index;
    uint8_t unmapped_chan;
    uint8_t last_unmapped_chan;
    uint8_t num_used_chans;

    /* connection event timing/mgmt */
    uint8_t rsp_rxd;
    uint8_t pkt_rxd;
    uint8_t master_sca;
    uint8_t tx_win_size;
    uint16_t conn_itvl;
    uint16_t slave_latency;
    uint16_t tx_win_off;
    uint16_t min_ce_len;
    uint16_t max_ce_len;
    uint16_t event_cntr;
    uint16_t supervision_tmo;
    uint16_t conn_handle;
    uint32_t access_addr;
    uint32_t crcinit;       /* only low 24 bits used */
    uint32_t anchor_point;

    /* address information */
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint8_t peer_addr[BLE_DEV_ADDR_LEN];

    /* connection supervisor timer */
    struct cpu_timer conn_spvn_timer;

    /* connection supervision timeout event */
    struct os_event conn_spvn_ev;

    /* Connection end event */
    struct os_event conn_ev_end;

    /* Packet transmit queue */
    STAILQ_HEAD(conn_txq_head, os_mbuf_pkthdr) conn_txq;
};

/* Pointer to connection state machine we are trying to create */
struct ble_ll_conn_sm *g_ble_ll_conn_create_sm;

/* Pointer to current connection */
struct ble_ll_conn_sm *g_ble_ll_conn_cur_sm;

/* Connection handles */
static uint8_t g_ble_ll_conn_handles[(BLE_LL_CONN_CFG_MAX_CONNS + 7) / 8];
static uint16_t g_ble_ll_conn_handles_out;

/* Connection pool elements */
struct os_mempool g_ble_ll_conn_pool;
os_membuf_t g_ble_ll_conn_buf[OS_MEMPOOL_SIZE(BLE_LL_CONN_CFG_MAX_CONNS, 
                                              sizeof(struct ble_ll_conn_sm))];

/* List of active connections */
static SLIST_HEAD(, ble_hs_conn) g_ble_ll_conn_active_list;

/* Statistics */
struct ble_ll_conn_stats
{
    uint32_t cant_set_sched;
    uint32_t conn_ev_late;
    uint32_t wfr_expirations;
};
struct ble_ll_conn_stats g_ble_ll_conn_stats;

/* "Dummy" mbuf containing an "Empty PDU" */
#define BLE_LL_DUMMY_EMPTY_PDU_SIZE     \
    ((sizeof(struct os_mbuf) + sizeof(struct os_mbuf_pkthdr) + 4) / 4)

static uint32_t g_ble_ll_empty_pdu[BLE_LL_DUMMY_EMPTY_PDU_SIZE];

/**
 * Calculates the number of used channels in the channel map 
 * 
 * @param chmap 
 * 
 * @return uint8_t Number of used channels
 */
uint8_t
ble_ll_conn_calc_used_chans(uint8_t *chmap)
{
    int i;
    int j;
    uint8_t mask;
    uint8_t chanbyte;
    uint8_t used_channels;

    used_channels = 0;
    for (i = 0; i < BLE_LL_CONN_CHMAP_LEN; ++i) {
        chanbyte = chmap[i];
        if (chanbyte) {
            if (chanbyte == 0xff) {
                used_channels += 8;
            } else {
                mask = 0x01;
                for (j = 0; j < 8; ++j) {
                    if (chanbyte & mask) {
                        ++used_channels;
                    }
                    mask <<= 1;
                }
            }
        }
    }
    return used_channels;
}

/**
 * Free an allocated connection handle 
 * 
 * @param handle The connection handle to free.
 */
static void
ble_ll_conn_handle_free(uint16_t handle)
{
    uint8_t bytepos;
    uint8_t mask;

    /* Make sure handle is valid */
    assert(handle < BLE_LL_CONN_CFG_MAX_CONNS);
    assert(g_ble_ll_conn_handles_out != 0);

    /* Get byte position and bit position */
    bytepos = handle / 8;
    mask = 1 << (handle & 0x7);

    assert((g_ble_ll_conn_handles[bytepos] & mask) != 0);

    g_ble_ll_conn_handles[bytepos] &= ~mask;
    --g_ble_ll_conn_handles_out;
}

/**
 * Allocate a connection handle. This will allocate an unused connection 
 * handle. 
 * 
 * @return uint16_t 
 */
static uint16_t
ble_ll_conn_handle_alloc(void)
{
    uint8_t bytepos;
    uint8_t bitpos;
    int handle;

    /* Make sure there is a handle that exists */
    if (g_ble_ll_conn_handles_out == BLE_LL_CONN_CFG_MAX_CONNS) {
        /* Handles can only be in range 0 to 0x0EFF */
        return 0xFFFF;
    }

    /* Return an unused handle */
    for (handle = 0; handle < BLE_LL_CONN_CFG_MAX_CONNS; ++handle) {
        /* Get byte position and bit position */
        bytepos = handle / 8;
        bitpos = handle & 0x7;

        /* See if handle avaiable */
        if ((g_ble_ll_conn_handles[bytepos] & (1 << bitpos)) == 0) {
            g_ble_ll_conn_handles[bytepos] |= (1 << bitpos);
            ++g_ble_ll_conn_handles_out;
            break;
        }
    }

    /* We better have a free one! If not, something wrong! */
    assert(handle < BLE_LL_CONN_CFG_MAX_CONNS);

    return handle;
}

static uint32_t
ble_ll_conn_calc_access_addr(void)
{
    uint32_t aa;
    uint16_t aa_low;
    uint16_t aa_high;
    uint32_t temp;
    uint32_t mask;
    uint32_t prev_bit;
    uint8_t bits_diff;
    uint8_t consecutive;
    uint8_t transitions;

    /* Calculate a random access address */
    aa = 0;
    while (1) {
        /* Get two, 16-bit random numbers */
        aa_low = rand() & 0xFFFF;
        aa_high = rand() & 0xFFFF;

        /* All four bytes cannot be equal */
        if (aa_low == aa_high) {
            continue;
        }

        /* Upper 6 bits must have 2 transitions */
        temp = aa_high & 0xFC00;
        if ((temp == 0) || (temp == 0xFC00)) {
            continue;
        }

        /* Cannot be access address or be 1 bit different */
        aa = aa_high;
        aa = (aa_high << 16) | aa_low;
        bits_diff = 0;
        mask = 0x00000001;
        temp = aa ^ BLE_ACCESS_ADDR_ADV;
        for (mask = 0; mask <= 0x80000000; mask <<= 1) {
            if (mask & temp) {
                ++bits_diff;
                if (bits_diff > 1) {
                    break;
                }
            }
        }
        if (bits_diff <= 1) {
            continue;
        }

        /* Cannot have more than 24 transitions */
        transitions = 0;
        consecutive = 0;
        mask = 0x00000001;
        prev_bit = aa & mask;
        while (mask < 0x80000000) {
            mask <<= 1;
            if (mask & aa) {
                if (prev_bit == 0) {
                    ++transitions;
                    consecutive = 0;
                } else {
                    ++consecutive;
                }
            } else {
                if (prev_bit == 0) {
                    ++consecutive;
                } else {
                    ++transitions;
                    consecutive = 0;
                }
            }

            /* This is invalid! */
            if (consecutive > 6) {
                break;
            }
        }

        /* Cannot be more than 24 transitions */
        if ((consecutive > 6) || (transitions > 24)) {
            continue;
        }
    }
    return aa;
}

/**
 * Determine data channel index to be used for the upcoming/current 
 * connection event 
 * 
 * @param conn 
 * 
 * @return uint8_t 
 */
uint8_t
ble_ll_conn_calc_dci(struct ble_ll_conn_sm *conn)
{
    int     i;
    int     j;
    uint8_t curchan;
    uint8_t remap_index;
    uint8_t bitpos;
    uint8_t cntr;
    uint8_t mask;
    uint8_t usable_chans;

    /* Get next unmapped channel */
    curchan = conn->last_unmapped_chan + conn->hop_inc;
    if (curchan > BLE_PHY_NUM_DATA_CHANS) {
        curchan -= BLE_PHY_NUM_DATA_CHANS;
    }

    /* Set the current unmapped channel */
    conn->unmapped_chan = curchan;

    /* Is this a valid channel? */
    bitpos = 1 << (curchan & 0x07);
    if ((conn->chanmap[curchan >> 3] & bitpos) == 0) {

        /* Calculate remap index */
        remap_index = curchan % conn->num_used_chans;

        /* NOTE: possible to build a map but this would use memory. For now,
           we just calculate */
        /* Iterate through channel map to find this channel */
        cntr = 0;
        for (i = 0; i < BLE_LL_CONN_CHMAP_LEN; i++) {
            usable_chans = conn->chanmap[i];
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

/**
 * Connection end schedule callback. Called when the scheduled connection 
 * event ends. 
 *  
 * Context: Interrupt 
 *  
 * @param sch 
 * 
 * @return int 
 */
static int
ble_ll_conn_ev_end_sched_cb(struct ble_ll_sched_item *sch)
{
    int rc;
    struct ble_ll_conn_sm *connsm;

    connsm = (struct ble_ll_conn_sm *)sch->cb_arg;
    ble_ll_event_send(&connsm->conn_ev_end);
    rc = BLE_LL_SCHED_STATE_DONE;
    return rc;
}

/**
 * Callback for connection wait for response timer. 
 *  
 * Context: Interrupt 
 *
 * @param sch 
 * 
 * @return int 
 */
static int
ble_ll_conn_wfr_sched_cb(struct ble_ll_sched_item *sch)
{
    int rc;
    struct ble_ll_conn_sm *connsm;

    connsm = (struct ble_ll_conn_sm *)sch->cb_arg;
    if (connsm->rsp_rxd) {
        /* For now just set to end time */
        sch->next_wakeup = sch->end_time;
        sch->sched_cb = ble_ll_conn_ev_end_sched_cb;
        rc = BLE_LL_SCHED_STATE_RUNNING;
    } else {
        ble_ll_event_send(&connsm->conn_ev_end);
        rc = BLE_LL_SCHED_STATE_DONE;
        ++g_ble_ll_conn_stats.wfr_expirations;
    }
    return rc;
}

/**
 * Schedule callback for start of connection event 
 *  
 * Context: Interrupt 
 * 
 * @param sch 
 * 
 * @return int 
 */
static int
ble_ll_conn_event_start_cb(struct ble_ll_sched_item *sch)
{
    int rc;
    struct os_mbuf *m;
    struct os_mbuf_pkthdr *pkthdr;
    struct ble_ll_conn_sm *connsm;

    /* Set current connection state machine */
    connsm = (struct ble_ll_conn_sm *)sch->cb_arg;
    g_ble_ll_conn_cur_sm = connsm;

    /* Set LL state */
    ble_ll_state_set(BLE_LL_STATE_CONNECTION);

    /* Set channel */
    rc = ble_phy_setchan(connsm->data_chan_index, connsm->access_addr, 
                         connsm->crcinit);
    assert(rc == 0);

    if (connsm->conn_role == BLE_LL_CONN_ROLE_MASTER) {
        /* Get packet off transmit queue. If none there, what to do? */
        pkthdr = STAILQ_FIRST(&connsm->conn_txq);
        if (pkthdr) {
            /* Remove from queue */
            m = (struct os_mbuf *)((uint8_t *)pkthdr - sizeof(struct os_mbuf));
            STAILQ_REMOVE_HEAD(&connsm->conn_txq, omp_next);
        } else {
            /* Send empty pdu */
            m = (struct os_mbuf *)&g_ble_ll_empty_pdu;
        }

        /* WWW: must modify the header */

        rc = ble_phy_tx(m, BLE_PHY_TRANSITION_NONE, BLE_PHY_TRANSITION_RX_TX);
        if (!rc) {
            /* Set next schedule wakeup to check for wait for response */
            sch->next_wakeup = cputime_get32() + 
                cputime_usecs_to_ticks(XCVR_TX_START_DELAY_USECS +
                                       ble_ll_pdu_tx_time_get(m->om_len) +
                                       BLE_LL_IFS +
                                       BLE_LL_WFR_USECS);
            sch->sched_cb = ble_ll_conn_wfr_sched_cb;
            rc = BLE_LL_SCHED_STATE_RUNNING;
        } else {
            /* Error transmitting! Put back on transmit queue */
            STAILQ_INSERT_HEAD(&connsm->conn_txq, pkthdr, omp_next);

            /* Inform LL task of connection event end */
            ble_ll_event_send(&connsm->conn_ev_end);
            rc = BLE_LL_SCHED_STATE_DONE;

            /* XXX: Is there anything else we need to do on failure? */
        }
    } else {
        rc = ble_phy_rx();
        if (rc) {
            /* XXX: what happens if this fails? */
        }
    }

    /* XXX: set state to connection here? */

    /* XXX: make sure we set the end time of the event (if needed) not sure
     * it is for master role. Have to think about it.
     */

    /* XXX: The return code here tells the scheduler whether
     * to use the next wakeup time or determines if the scheduled event is
       over. */
    return rc;
}

/**
 * Set the schedule for connection events
 *  
 * Context: Link Layer task 
 * 
 * @param connsm 
 * 
 * @return struct ble_ll_sched_item * 
 */
static struct ble_ll_sched_item *
ble_ll_conn_sched_set(struct ble_ll_conn_sm *connsm)
{
    int rc;
    uint32_t usecs;
    struct ble_ll_sched_item *sch;

    sch = ble_ll_sched_get_item();
    if (sch) {
        /* Set sched type, arg and callback function */
        sch->sched_type = BLE_LL_SCHED_TYPE_CONN;
        sch->cb_arg = connsm;
        sch->sched_cb = ble_ll_conn_event_start_cb;

        /* Set the start time of the event */
        if (connsm->conn_role == BLE_LL_CONN_ROLE_MASTER) {
            sch->start_time = connsm->anchor_point - 
                cputime_usecs_to_ticks(XCVR_TX_SCHED_DELAY_USECS);

            /* We will attempt to schedule the maximum CE length */
            usecs = connsm->max_ce_len * BLE_LL_CONN_CE_USECS;
        } else {
            /* XXX: account for window widening/drift */
            sch->start_time = connsm->anchor_point - 
                cputime_usecs_to_ticks(XCVR_RX_SCHED_DELAY_USECS);

            /* XXX: what to do for slave? Not sure how much to schedule here.
               For now, just schedule entire connection interval */
            usecs = connsm->conn_itvl * BLE_LL_CONN_ITVL_USECS;
        }
        sch->end_time = connsm->anchor_point + cputime_usecs_to_ticks(usecs);

        /* XXX: for now, we cant get an overlap so assert on error. */
        /* Add the item to the scheduler */
        rc = ble_ll_sched_add(sch);
        assert(rc == 0);
    } else {
        /* Count # of times we could not set schedule */
        ++g_ble_ll_conn_stats.cant_set_sched;

        /* XXX: for now just assert; must handle this later though */
        assert(0);
    }

    return sch;
}

/* WWW:
 *  -> deal with data channel index (calculation it).
 *  -> deal with slave latency                      .
 */

/**
 * Called upon end of connection event
 *  
 * Context: Link-layer task 
 * 
 * @param void *arg Pointer to connection state machine
 * 
 */
void
ble_ll_conn_event_end(void *arg)
{
    uint16_t latency;
    uint32_t itvl;
    struct ble_ll_conn_sm *connsm;

    /* 
     * XXX: if we received a response we can apply slave latency. I am not
     * sure how this applies to the master. I guess the master could possibly 
     * keep sending each anchor if it does not receive a reply. Possible that
     * slave receives master but master does not get reply. This would cause
     * master to re-transmit every connection event even though slave was
     * asleep.
     */
    connsm = (struct ble_ll_conn_sm *)arg;

    /* Set event counter to the next connection event that we will tx/rx in */
    itvl = connsm->conn_itvl * BLE_LL_CONN_ITVL_USECS;
    latency = 1;
    if (connsm->pkt_rxd) {
        latency += connsm->slave_latency;
        itvl = itvl * latency;
        connsm->pkt_rxd = 0;
    }
    connsm->event_cntr += latency;

    /* Set next connection event start time */
    connsm->anchor_point += cputime_usecs_to_ticks(itvl);

    /* Calculate data channel index of next connection event */
    while (latency >= 0) {
        --latency;
        connsm->data_chan_index = ble_ll_conn_calc_dci(connsm);
    }

    /* We better not be late for the anchor point. If so, skip events */
    while ((int32_t)(connsm->anchor_point - cputime_get32()) <= 0) {
        ++connsm->event_cntr;
        connsm->data_chan_index = ble_ll_conn_calc_dci(connsm);
        connsm->anchor_point += 
            cputime_usecs_to_ticks(connsm->conn_itvl * BLE_LL_CONN_ITVL_USECS);
        ++g_ble_ll_conn_stats.conn_ev_late;
    }

    /* XXX: window widening for slave */

    /* Link-layer is in standby state now */
    ble_ll_state_set(BLE_LL_STATE_STANDBY);

    /* Set current LL connection to NULL */
    g_ble_ll_conn_cur_sm = NULL;

    /* Schedule the next connection event */
    ble_ll_conn_sched_set(connsm);
}

/**
 * Connection supervision timer callback; means that the connection supervision
 * timeout has been reached and we should perform the appropriate actions. 
 *  
 * Context: Interrupt (cputimer)
 * 
 * @param arg Pointer to connection state machine.
 */
void
ble_ll_conn_spvn_timer_cb(void *arg)
{
    struct ble_ll_conn_sm *connsm;

    /* 
     * WWW: What should we do with the PHY here? We could be transmitting or
     * receiving. Need to figure out what to do.
     */

    connsm = (struct ble_ll_conn_sm *)arg;
    ble_ll_event_send(&connsm->conn_spvn_ev);
}

/**
 * Initialize the connection state machine. This is done once per connection 
 * when the HCI command "create connection" is issued to the controller or 
 * when a slave receives a connect request, 
 * 
 * @param connsm 
 */
static void
ble_ll_conn_sm_init(struct ble_ll_conn_sm *connsm, struct hci_create_conn *hcc)
{
    struct ble_ll_conn_global_params *conn_params;

    /* Reset event counter and last unmapped channel; get a handle */
    connsm->last_unmapped_chan = 0;
    connsm->event_cntr = 0;
    connsm->conn_state = BLE_LL_CONN_STATE_IDLE;

    /* We better have an unused handle or we are in trouble! */
    connsm->conn_handle = ble_ll_conn_handle_alloc();
    assert(connsm->conn_handle < BLE_LL_CONN_CFG_MAX_CONNS);

    if (hcc != NULL) {
        /* Must be master */
        connsm->conn_role = BLE_LL_CONN_ROLE_MASTER;
        connsm->tx_win_size = BLE_LL_CONN_CFG_TX_WIN_SIZE;
        connsm->tx_win_off = BLE_LL_CONN_CFG_TX_WIN_OFF;
        connsm->master_sca = BLE_LL_CONN_CFG_MASTER_SCA;

        /* Hop increment is a random value between 5 and 16. */
        connsm->hop_inc = (rand() % 12) + 5;

        /* Set slave latency and supervision timeout */
        connsm->slave_latency = hcc->conn_latency;
        connsm->supervision_tmo = hcc->supervision_timeout;

        /* Set own address type and peer address if needed */
        connsm->own_addr_type = hcc->own_addr_type;
        if (hcc->filter_policy == 0) {
            memcpy(&connsm->peer_addr, &hcc->peer_addr, BLE_DEV_ADDR_LEN);
            connsm->peer_addr_type = hcc->peer_addr_type;
        }

        /* XXX: for now, just make connection interval equal to max */
        connsm->conn_itvl = hcc->conn_itvl_max;

        /* Check the min/max CE lengths are less than connection interval */
        if (hcc->min_ce_len > (connsm->conn_itvl * 2)) {
            connsm->min_ce_len = connsm->conn_itvl * 2;
        } else {
            connsm->min_ce_len = hcc->min_ce_len;
        }

        if (hcc->max_ce_len > (connsm->conn_itvl * 2)) {
            connsm->max_ce_len = connsm->conn_itvl * 2;
        } else {
            connsm->max_ce_len = hcc->max_ce_len;
        }

        /* 
         * XXX: for now, just set the channel map to all 1's. Needs to get
         * set to default or initialized or something
         */ 
        connsm->num_used_chans = BLE_PHY_NUM_DATA_CHANS;
        memset(connsm->chanmap, 0xff, BLE_LL_CONN_CHMAP_LEN - 1);
        connsm->chanmap[4] = 0x1f;

        /*  Calculate random access address and crc initialization value */
        connsm->access_addr = ble_ll_conn_calc_access_addr();
        connsm->crcinit = rand() & 0xffffff;
    } else {
        connsm->conn_role = BLE_LL_CONN_ROLE_SLAVE;
    }

    /* Initialize connection supervision timer */
    cputime_timer_init(&connsm->conn_spvn_timer, ble_ll_conn_spvn_timer_cb, 
                       connsm);

    /* Calculate the next data channel */
    connsm->data_chan_index = ble_ll_conn_calc_dci(connsm);

    /* Initialize event */
    connsm->conn_spvn_ev.ev_arg = connsm;
    connsm->conn_spvn_ev.ev_queued = 0;
    connsm->conn_spvn_ev.ev_type = BLE_LL_EVENT_CONN_SPVN_TMO;

    /* Connection end event */
    connsm->conn_ev_end.ev_arg = connsm;
    connsm->conn_ev_end.ev_queued = 0;
    connsm->conn_ev_end.ev_type = BLE_LL_EVENT_CONN_EV_END;

    /* Initialize transmit queue */
    STAILQ_INIT(&connsm->conn_txq);

    /* XXX: Section 4.5.10 Vol 6 PART B. If the max tx/rx time or octets
       exceeds the minimum, data length procedure needs to occur */
    /* initialize data length mgmt */
    conn_params = &g_ble_ll_conn_params;
    connsm->max_tx_octets = conn_params->conn_init_max_tx_octets;
    connsm->max_rx_octets = conn_params->supp_max_rx_octets;
    connsm->max_tx_time = conn_params->conn_init_max_tx_octets;
    connsm->max_rx_time = conn_params->supp_max_rx_time;
    connsm->rem_max_tx_time = BLE_LL_CONN_SUPP_TIME_MIN;
    connsm->rem_max_rx_time = BLE_LL_CONN_SUPP_TIME_MIN;
    connsm->eff_max_tx_time = BLE_LL_CONN_SUPP_TIME_MIN;
    connsm->eff_max_rx_time = BLE_LL_CONN_SUPP_TIME_MIN;
    connsm->rem_max_tx_octets = BLE_LL_CONN_SUPP_BYTES_MIN;
    connsm->rem_max_rx_octets = BLE_LL_CONN_SUPP_BYTES_MIN;
    connsm->eff_max_tx_octets = BLE_LL_CONN_SUPP_BYTES_MIN;
    connsm->eff_max_rx_octets = BLE_LL_CONN_SUPP_BYTES_MIN;

    /* XXX: Controller notifies host of changes to effective tx/rx time/bytes*/
}

/**
 * Send a connection complete event 
 * 
 * @param status The BLE error code associated with the event
 */
static void
ble_ll_conn_comp_event_send(struct ble_ll_conn_sm *connsm, uint8_t status)
{
    uint8_t *evbuf;

    if (ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_CONN_COMPLETE - 1)) {
        evbuf = os_memblock_get(&g_hci_cmd_pool);
        if (evbuf) {
            evbuf[0] = BLE_HCI_EVCODE_LE_META;
            evbuf[1] = BLE_HCI_LE_CONN_COMPLETE_LEN;
            evbuf[2] = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
            evbuf[3] = status;
            if (status == BLE_ERR_SUCCESS) {
                htole16(evbuf + 4, connsm->conn_handle);
                evbuf[6] = connsm->conn_role;
                evbuf[7] = connsm->peer_addr_type;
                memcpy(evbuf + 8, connsm->peer_addr, BLE_DEV_ADDR_LEN);
                htole16(evbuf + 14, connsm->conn_itvl);
                htole16(evbuf + 16, connsm->slave_latency);
                evbuf[18] = connsm->master_sca;
            }

            ble_ll_hci_event_send(evbuf);
        }
    }
}

/**
 * Called when a connection is terminated 
 *  
 * Context: Link Layer task. 
 * 
 * @param connsm 
 * @param ble_err 
 */
static void
ble_ll_conn_end(struct ble_ll_conn_sm *connsm, uint8_t ble_err)
{
    struct os_mbuf *m;
    struct os_mbuf_pkthdr *pkthdr;

    /* XXX: stop any connection schedule timers we created */
    /* XXX: make sure phy interrupts are off and all that. We want to
     * make sure that if a scheduled event fires off here we dont care;
     * we just need to clean up properly
     */

    /* Free all packets on transmit queue */
    while (1) {
        /* Get mbuf pointer from packet header pointer */
        pkthdr = STAILQ_FIRST(&connsm->conn_txq);
        if (!pkthdr) {
            break;
        }
        STAILQ_REMOVE_HEAD(&connsm->conn_txq, omp_next);

        m = (struct os_mbuf *)((uint8_t *)pkthdr - sizeof(struct os_mbuf));
        os_mbuf_free(&g_mbuf_pool, m);
    }

    /* Remove scheduler events just in case */
    ble_ll_sched_rmv(BLE_LL_SCHED_TYPE_CONN, connsm);

    /* Make sure events off queue and connection supervison timer stopped */
    cputime_timer_stop(&connsm->conn_spvn_timer);
    os_eventq_remove(&g_ble_ll_data.ll_evq, &connsm->conn_spvn_ev);
    os_eventq_remove(&g_ble_ll_data.ll_evq, &connsm->conn_ev_end);

    /* Connection state machine is now idle */
    connsm->conn_state = BLE_LL_CONN_STATE_IDLE;

    /* Set Link Layer state to standby */
    ble_ll_state_set(BLE_LL_STATE_STANDBY);

    /* Send connection complete event */
    ble_ll_conn_comp_event_send(connsm, ble_err);

    /* Free up the handle */
    ble_ll_conn_handle_free(connsm->conn_handle);

    /* Free the connection state machine */
    os_memblock_put(&g_ble_ll_conn_pool, connsm);
}

/**
 * Called when a connection has been created. This function will 
 *  -> Set the connection state to created.
 *  -> Start the connection supervision timer
 *  -> Set the Link Layer state to connection.
 *  -> Send a connection complete event.
 *  
 *  See Section 4.5.2 Vol 6 Part B
 *  
 *  Context: Link Layer
 * 
 * @param connsm 
 */
static void
ble_ll_conn_created(struct ble_ll_conn_sm *connsm)
{
    uint32_t usecs;

    /* Set state to created */
    connsm->conn_state = BLE_LL_CONN_STATE_CREATED;

    /* Set supervision timeout */
    usecs = connsm->conn_itvl * BLE_LL_CONN_ITVL_USECS * 6;
    cputime_timer_relative(&connsm->conn_spvn_timer, usecs);

    /* Clear packet received flag and response rcvd flag*/
    connsm->rsp_rxd = 0;
    connsm->pkt_rxd = 0;

    /* XXX: get real end timing. By this I mean the real packet rx end time
     * For now, we will just use time we got here as the start and hope we hit
       the tx window */

    /* Set first connection event time */
    usecs = 1250 + (connsm->tx_win_off * BLE_LL_CONN_TX_WIN_USECS);
    if (connsm->conn_role == BLE_LL_CONN_ROLE_MASTER) {
        usecs += ble_ll_pdu_tx_time_get(BLE_CONNECT_REQ_PDU_LEN) + BLE_LL_IFS;
    } else {
        /* XXX: need to deal with drift/window widening here */
        usecs += 0;
    }
    connsm->anchor_point = cputime_get32() + cputime_usecs_to_ticks(usecs);

    /* Send connection complete event to inform host of connection */
    ble_ll_conn_comp_event_send(connsm, BLE_ERR_SUCCESS);

    /* Start the scheduler for the first connection event */
    ble_ll_conn_sched_set(connsm);
}

/**
 * Update the connection request PDU with the address type and address of 
 * advertiser we are going to send connect request to. 
 * 
 * @param m 
 * @param adva 
 * @param addr_type 
 */
void
ble_ll_conn_req_pdu_update(struct os_mbuf *m, uint8_t *adva, uint8_t addr_type)
{
    uint8_t pdu_type;
    uint8_t *dptr;

    assert(m != NULL);

    dptr = m->om_data;
    pdu_type = dptr[0];
    if (addr_type) {
        /* Set random address */
        pdu_type |= BLE_ADV_PDU_HDR_RXADD_MASK;
    } else {
        /* Set public device address */
        pdu_type &= ~BLE_ADV_PDU_HDR_RXADD_MASK;
    }
    dptr[0] = pdu_type;
    memcpy(dptr + BLE_LL_CONN_REQ_ADVA_OFF, adva, BLE_DEV_ADDR_LEN); 
}

/**
 * Make a connect request PDU 
 * 
 * @param connsm 
 */
void
ble_ll_conn_req_pdu_make(struct ble_ll_conn_sm *connsm)
{
    uint8_t pdu_type;
    uint8_t *addr;
    uint8_t *dptr;
    struct os_mbuf *m;

    m = ble_ll_scan_get_pdu();
    assert(m != NULL);
    m->om_len = BLE_CONNECT_REQ_LEN + BLE_LL_PDU_HDR_LEN;
    OS_MBUF_PKTHDR(m)->omp_len = m->om_len;

    /* Construct first PDU header byte */
    pdu_type = BLE_ADV_PDU_TYPE_CONNECT_REQ;

    /* Get pointer to our device address */
    if (connsm->own_addr_type == BLE_HCI_ADV_OWN_ADDR_PUBLIC) {
        addr = g_dev_addr;
    } else if (connsm->own_addr_type == BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        pdu_type |= BLE_ADV_PDU_HDR_TXADD_RAND;
        addr = g_random_addr;
    } else {
        /* XXX: unsupported for now  */
        addr = NULL;
        assert(0);
    }

    /* Construct the connect request */
    dptr = m->om_data;
    dptr[0] = pdu_type;
    dptr[1] = BLE_CONNECT_REQ_LEN;
    memcpy(dptr + BLE_LL_PDU_HDR_LEN, addr, BLE_DEV_ADDR_LEN);

    /* Skip the advertiser's address as we dont know that yet */
    dptr += (BLE_LL_CONN_REQ_ADVA_OFF + BLE_DEV_ADDR_LEN);

    /* Access address */
    htole32(dptr, connsm->access_addr);
    dptr[4] = connsm->crcinit >> 16;
    dptr[5] = connsm->crcinit >> 8;
    dptr[6] = (uint8_t)connsm->crcinit;
    dptr[7] = connsm->tx_win_size;
    htole16(dptr + 8, connsm->tx_win_off);
    htole16(dptr + 10, connsm->conn_itvl);
    htole16(dptr + 12, connsm->slave_latency);
    htole16(dptr + 14, connsm->supervision_tmo);
    memcpy(dptr + 16, &connsm->chanmap, BLE_LL_CONN_CHMAP_LEN);
    dptr[21] = connsm->hop_inc | connsm->master_sca;
}

/**
 * Process the HCI command to create a connection. 
 * 
 * @param cmdbuf 
 * 
 * @return int 
 */
int
ble_ll_conn_create(uint8_t *cmdbuf)
{
    int rc;
    uint32_t spvn_tmo_msecs;
    uint32_t min_spvn_tmo_msecs;
    struct hci_create_conn ccdata;
    struct hci_create_conn *hcc;
    struct ble_ll_conn_sm *connsm;

    /* If we are already creating a connection we should leave */
    if (g_ble_ll_conn_create_sm) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* If already enabled, we return an error */
    if (ble_ll_scan_enabled()) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* Retrieve command data */
    hcc = &ccdata;
    hcc->scan_itvl = le16toh(cmdbuf);
    hcc->scan_window = le16toh(cmdbuf + 2);

    /* Check interval and window */
    if ((hcc->scan_itvl < BLE_HCI_SCAN_ITVL_MIN) || 
        (hcc->scan_itvl > BLE_HCI_SCAN_ITVL_MAX) ||
        (hcc->scan_window < BLE_HCI_SCAN_WINDOW_MIN) ||
        (hcc->scan_window > BLE_HCI_SCAN_WINDOW_MAX) ||
        (hcc->scan_itvl < hcc->scan_window)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check filter policy */
    hcc->filter_policy = cmdbuf[4];
    if (hcc->filter_policy > BLE_HCI_INITIATOR_FILT_POLICY_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Get peer address type and address only if no whitelist used */
    if (hcc->filter_policy == 0) {
        hcc->peer_addr_type = cmdbuf[5];
        if (hcc->peer_addr_type > BLE_HCI_CONN_PEER_ADDR_MAX) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }
        memcpy(&hcc->peer_addr, cmdbuf + 6, BLE_DEV_ADDR_LEN);
    }

    /* Get own address type (used in connection request) */
    hcc->own_addr_type = cmdbuf[12];
    if (hcc->own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check connection interval */
    hcc->conn_itvl_min = le16toh(cmdbuf + 13);
    hcc->conn_itvl_max = le16toh(cmdbuf + 15);
    hcc->conn_latency = le16toh(cmdbuf + 17);
    if ((hcc->conn_itvl_min > hcc->conn_itvl_max)       ||
        (hcc->conn_itvl_min < BLE_HCI_CONN_ITVL_MIN)    ||
        (hcc->conn_itvl_min > BLE_HCI_CONN_ITVL_MAX)    ||
        (hcc->conn_latency > BLE_HCI_CONN_LATENCY_MAX)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check supervision timeout */
    hcc->supervision_timeout = le16toh(cmdbuf + 19);
    if ((hcc->supervision_timeout < BLE_HCI_CONN_SPVN_TIMEOUT_MIN) ||
        (hcc->supervision_timeout > BLE_HCI_CONN_SPVN_TIMEOUT_MAX))
    {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* 
     * Supervision timeout (in msecs) must be more than:
     *  (1 + connLatency) * connIntervalMax * 1.25 msecs * 2.
     */
    spvn_tmo_msecs = hcc->supervision_timeout * BLE_HCI_CONN_SPVN_TMO_UNITS;
    min_spvn_tmo_msecs = (hcc->conn_itvl_max << 1) + (hcc->conn_itvl_max >> 1);
    min_spvn_tmo_msecs *= (1 + hcc->conn_latency);
    if (spvn_tmo_msecs <= min_spvn_tmo_msecs) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Min/max connection event lengths */
    hcc->min_ce_len = le16toh(cmdbuf + 21);
    hcc->max_ce_len = le16toh(cmdbuf + 23);
    if (hcc->min_ce_len > hcc->max_ce_len) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Make sure we can accept a connection! */
    connsm = (struct ble_ll_conn_sm *)os_memblock_get(&g_ble_ll_conn_pool);
    if (connsm == NULL) {
        return BLE_ERR_CONN_LIMIT;
    }

    /* Initialize the connection sm */
    ble_ll_conn_sm_init(connsm, hcc);

    /* Create the connection request */
    ble_ll_conn_req_pdu_make(connsm);

    /* Start scanning */
    rc = ble_ll_scan_initiator_start(hcc);
    if (rc) {
        os_memblock_put(&g_ble_ll_conn_pool, connsm);
    } else {
        /* Set the connection state machine we are trying to create. */
        g_ble_ll_conn_create_sm = connsm;
    }

    return rc;
}

/**
 * Called when HCI command to cancel a create connection command has been 
 * received. 
 *  
 * Context: Link Layer (HCI command parser) 
 * 
 * @return int 
 */
int
ble_ll_conn_create_cancel(void)
{
    int rc;
    struct ble_ll_conn_sm *connsm;

    /* 
     * If we receive this command and we have not got a connection
     * create command, we have to return disallowed. The spec does not say
     * what happens if the connection has already been established. We
     * return disallowed as well
     */
    connsm = g_ble_ll_conn_create_sm;
    if (connsm && (connsm->conn_state == BLE_LL_CONN_STATE_IDLE)) {
        /* stop scanning and end the connection event */
        g_ble_ll_conn_create_sm = NULL;
        ble_ll_scan_sm_stop(ble_ll_scan_sm_get());
        ble_ll_conn_end(connsm, BLE_ERR_UNK_CONN_ID);
        rc = BLE_ERR_SUCCESS;
    } else {
        /* If we are not attempting to create a connection*/
        rc = BLE_ERR_CMD_DISALLOWED;
    }

    return rc;
}

/* Returns true if the address matches the connection peer address */
int
ble_ll_conn_is_peer_adv(uint8_t addr_type, uint8_t *adva)
{
    int rc;
    struct ble_ll_conn_sm *connsm;

    connsm = g_ble_ll_conn_create_sm;
    if (connsm && (connsm->peer_addr_type == addr_type) &&
        !memcmp(adva, connsm->peer_addr, BLE_DEV_ADDR_LEN)) {
        rc = 1;
    } else {
        rc = 0;
    }

    return rc;
}

/**
 * Send a connection requestion to an advertiser 
 *  
 * Context: Interrupt 
 * 
 * @param addr_type Address type of advertiser
 * @param adva Address of advertiser
 */
int
ble_ll_conn_request_send(uint8_t addr_type, uint8_t *adva)
{
    int rc;
    struct os_mbuf *m;

    m = ble_ll_scan_get_pdu();
    ble_ll_conn_req_pdu_update(m, adva, addr_type);
    rc = ble_phy_tx(m, BLE_PHY_TRANSITION_RX_TX, BLE_PHY_TRANSITION_NONE);
    return rc;
}

/**
 * Process a received PDU while in the initiating state.
 *  
 * Context: Link Layer task. 
 * 
 * @param pdu_type 
 * @param rxbuf 
 */
void
ble_ll_init_rx_pdu_proc(uint8_t *rxbuf, struct ble_mbuf_hdr *ble_hdr)
{
    uint8_t addr_type;
    struct ble_ll_conn_sm *connsm;

    /* Get the connection state machine we are trying to create */
    connsm = g_ble_ll_conn_create_sm;
    if (!connsm) {
        return;
    }

    /* If we have sent a connect request, we need to enter CONNECTION state*/
    if (ble_hdr->crcok && (ble_hdr->flags & BLE_MBUF_HDR_F_CONN_REQ_TXD)) {
        /* Set address of advertiser to which we are connecting. */
        if (!ble_ll_scan_whitelist_enabled()) {
            /* 
             * XXX: need to see if the whitelist tells us exactly what peer
             * addr type we should use? Not sure it matters. If whitelisting
             * is not used the peer addr and type already set
             */ 
            /* Get address type of advertiser */
            if (rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK) {
                addr_type = BLE_ADDR_TYPE_RANDOM;
            } else {
                addr_type = BLE_ADDR_TYPE_PUBLIC;
            }

            connsm->peer_addr_type = addr_type;
            memcpy(connsm->peer_addr, rxbuf + BLE_LL_PDU_HDR_LEN, 
                   BLE_DEV_ADDR_LEN);
        }

        /* Connection has been created. Stop scanning */
        g_ble_ll_conn_create_sm = NULL;
        ble_ll_scan_sm_stop(ble_ll_scan_sm_get());
        ble_ll_conn_created(connsm);
    } else {
        /* We need to re-enable the PHY if we are in idle state */
        if (ble_phy_state_get() == BLE_PHY_STATE_IDLE) {
            /* XXX: If this returns error, we will need to attempt to
               re-start scanning! */
            ble_phy_rx();
        }
    }
}

/**
 * Called when a receive PDU has ended. 
 *  
 * Context: Interrupt 
 * 
 * @param rxpdu 
 * 
 * @return int 
 *       < 0: Disable the phy after reception.
 *      == 0: Success. Do not disable the PHY.
 *       > 0: Do not disable PHY as that has already been done.
 */
int
ble_ll_init_rx_pdu_end(struct os_mbuf *rxpdu)
{
    int rc;
    int chk_send_req;
    uint8_t pdu_type;
    uint8_t addr_type;
    uint8_t *adv_addr;
    uint8_t *init_addr;
    uint8_t *rxbuf;
    struct ble_mbuf_hdr *ble_hdr;

    /* Only interested in ADV IND or ADV DIRECT IND */
    rxbuf = rxpdu->om_data;
    pdu_type = rxbuf[0] & BLE_ADV_PDU_HDR_TYPE_MASK;

    switch (pdu_type) {
    case BLE_ADV_PDU_TYPE_ADV_IND:
        chk_send_req = 1;
        break;
    case BLE_ADV_PDU_TYPE_ADV_DIRECT_IND:
        init_addr = rxbuf + BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN;
        addr_type = rxbuf[0] & BLE_ADV_PDU_HDR_RXADD_MASK;
        if (ble_ll_is_our_devaddr(init_addr, addr_type)) {
            chk_send_req = 1;
        } else {
            chk_send_req = 0;
        }
        break;
    default:
        chk_send_req = 0;
        break;
    }

    /* Should we send a connect request? */
    rc = -1;
    if (chk_send_req) {
        /* Check filter policy */
        adv_addr = rxbuf + BLE_LL_PDU_HDR_LEN;
        if (rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK) {
            addr_type = BLE_ADDR_TYPE_RANDOM;
        } else {
            addr_type = BLE_ADDR_TYPE_PUBLIC;
        }

        /* Check filter policy */
        ble_hdr = BLE_MBUF_HDR_PTR(rxpdu);
        if (ble_ll_scan_whitelist_enabled()) {
            /* Check if device is on whitelist. If not, leave */
            if (!ble_ll_whitelist_match(adv_addr, addr_type)) {
                return -1;
            }

            /* Set BLE mbuf header flags */
            ble_hdr->flags |= BLE_MBUF_HDR_F_DEVMATCH;
        } else {
            /* XXX: Resolvable? Deal with those */
            /* XXX: HW device matching? If so, implement */
            /* Must match the connection address */
            if (!ble_ll_conn_is_peer_adv(addr_type, adv_addr)) {
                return -1;
            }
        }

        /* Setup to transmit the connect request */
        rc = ble_ll_conn_request_send(addr_type, adv_addr);
        if (!rc) {
            ble_hdr->flags |= BLE_MBUF_HDR_F_CONN_REQ_TXD;
        }
    }

    return rc;
}

/**
 * Called on pdu start (by link layer) when in initiating state. 
 * 
 * @param pdu_type 
 * 
 * @return int 
 */
int
ble_ll_init_rx_pdu_start(uint8_t pdu_type)
{
    int rc;

    /* We might respond to adv ind or adv direct ind only */
    rc = 0;
    if ((pdu_type == BLE_ADV_PDU_TYPE_ADV_IND) ||
        (pdu_type == BLE_ADV_PDU_TYPE_ADV_DIRECT_IND)) {
        rc = 1;
    }

    return rc;
}

/**
 * Connection supervision timeout. When called, it means that the connection 
 * supervision timeout has been reached. If reached, we end the connection. 
 *  
 * Context: Link Layer 
 * 
 * @param arg Pointer to connection state machine.
 */
void
ble_ll_conn_spvn_timeout(void *arg)
{
    ble_ll_conn_end((struct ble_ll_conn_sm *)arg, BLE_ERR_CONN_SPVN_TMO);
}

/**
 * Called when a data channel PDU has started that matches the access 
 * address of the current connection. Note that the CRC of the PDU has not 
 * been checked yet.
 */
void
ble_ll_conn_rsp_rxd(void)
{
    if (g_ble_ll_conn_cur_sm) {
        g_ble_ll_conn_cur_sm->rsp_rxd = 1;
    }
}

/**
 * Called when a device has received a connect request while advertising and 
 * the connect request has passed the advertising filter policy and is for 
 * us. This will start a connection in the slave role. 
 *  
 * Context: Link Layer 
 *  
 * @param rxbuf Pointer to received PDU
 */
void
ble_ll_conn_slave_start(uint8_t *rxbuf)
{
    uint32_t crcinit;
    uint8_t *dptr;
    struct ble_ll_conn_sm *connsm;

    /* XXX: Should we error check the parameters? */

    /* Allocate a connection. If none available, dont do anything */
    connsm = (struct ble_ll_conn_sm *)os_memblock_get(&g_ble_ll_conn_pool);
    if (!connsm) {
        return;
    }

    /* Set the pointer at the start of the connection data */
    dptr = rxbuf + BLE_LL_CONN_REQ_ADVA_OFF + BLE_DEV_ADDR_LEN;

    connsm->access_addr = le32toh(dptr);
    crcinit = dptr[4];
    crcinit <<= 16;
    crcinit |= dptr[5];
    crcinit <<= 8;
    crcinit |= dptr[6];
    connsm->crcinit = crcinit;
    connsm->tx_win_size = dptr[7];
    connsm->tx_win_off = le16toh(dptr + 8);
    connsm->conn_itvl = le16toh(dptr + 10);
    connsm->slave_latency = le16toh(dptr + 12);
    connsm->supervision_tmo = le16toh(dptr + 14);
    memcpy(&connsm->chanmap, dptr + 16, BLE_LL_CONN_CHMAP_LEN);
    connsm->hop_inc = dptr[21] & 0x1F;
    connsm->master_sca = dptr[21] >> 5;

    /* XXX: might want to set this differently based on adv. filter policy! */
    /* Set the address of device that we are connecting with */
    memcpy(&connsm->peer_addr, rxbuf + BLE_LL_PDU_HDR_LEN, BLE_DEV_ADDR_LEN);
    if (rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK) {
        connsm->peer_addr_type = BLE_ADDR_TYPE_RANDOM;
    } else {
        connsm->peer_addr_type = BLE_ADDR_TYPE_PUBLIC;
    }

    connsm->num_used_chans = ble_ll_conn_calc_used_chans(connsm->chanmap);

    /* Need to initialize the connection state machine */
    ble_ll_conn_sm_init(connsm, NULL);

    /* The connection has been created. */
    ble_ll_conn_created(connsm);
}

/* Initialize the connection module */
void
ble_ll_conn_init(void)
{
    int rc;
    uint16_t maxbytes;
    struct os_mbuf *m;
    struct ble_ll_conn_global_params *conn_params;

    /* Create connection memory pool */
    rc = os_mempool_init(&g_ble_ll_conn_pool, BLE_LL_CONN_CFG_MAX_CONNS, 
                         sizeof(struct ble_ll_conn_sm), &g_ble_ll_conn_buf, 
                         "LLConnPool");
    assert(rc == 0);

    /* Initialize list of active conections */
    SLIST_INIT(&g_ble_ll_conn_active_list);

    /* Configure the global LL parameters */
    conn_params = &g_ble_ll_conn_params;
    maxbytes = BLE_LL_CFG_SUPP_MAX_RX_BYTES + BLE_LL_DATA_MAX_OVERHEAD;
    conn_params->supp_max_rx_time = ble_ll_pdu_tx_time_get(maxbytes);
    conn_params->supp_max_rx_octets = BLE_LL_CFG_SUPP_MAX_RX_BYTES;

    maxbytes = BLE_LL_CFG_SUPP_MAX_TX_BYTES + BLE_LL_DATA_MAX_OVERHEAD;
    conn_params->supp_max_tx_time = ble_ll_pdu_tx_time_get(maxbytes);
    conn_params->supp_max_tx_octets = BLE_LL_CFG_SUPP_MAX_TX_BYTES;

    maxbytes = BLE_LL_CFG_CONN_INIT_MAX_TX_BYTES + BLE_LL_DATA_MAX_OVERHEAD;
    conn_params->conn_init_max_tx_time = ble_ll_pdu_tx_time_get(maxbytes);
    conn_params->conn_init_max_tx_octets = BLE_LL_CFG_SUPP_MAX_TX_BYTES;

    /* Initialize dummy empty pdu */
    m = (struct os_mbuf *)&g_ble_ll_empty_pdu;
    m->om_data = (uint8_t *)&g_ble_ll_empty_pdu[0];
    m->om_data += sizeof(struct os_mbuf) + sizeof(struct os_mbuf_pkthdr);
    m->om_len = 2;
    m->om_flags = OS_MBUF_F_MASK(OS_MBUF_F_PKTHDR);
    OS_MBUF_PKTHDR(m)->omp_len = 2;
}

