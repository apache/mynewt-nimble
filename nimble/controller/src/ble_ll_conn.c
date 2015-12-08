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
#include "controller/ble_ll_ctrl.h"
#include "controller/ble_phy.h"
#include "hal/hal_cputime.h"
#include "hal/hal_gpio.h"

/* XXX TODO
 * 1) Add set channel map command and implement channel change procedure.
 * 2) Make sure we have implemented all ways a connection can die/end. Not
 * a connection event; I mean the termination of a connection.
 * 3) Link layer control procedures and timers
 * 4) We also need to check to see if we were asked to create a connection
 * when one already exists (the connection create command) Note that
 * an initiator should only send connection requests to devices it is not
 * already connected to. Make sure this cant happen.
 * 5) Make sure we check incoming data packets for size and all that. You
 * know, supported octets and all that. For both rx and tx.
 * 6) Make sure we are setting the schedule end time properly for both slave
 * and master. We should just set this to the end of the connection event.
 * We might want to guarantee a IFS time as well since the next event needs
 * to be scheduled prior to the start of the event to account for the time it
 * takes to get a frame ready (which is pretty much the IFS time).
 */

/* XXX: this does not belong here! Move to transport? */
extern int ble_hs_rx_data(struct os_mbuf *om);

/* 
 * XXX: Possible improvements
 * 1) See what connection state machine elements are purely master and
 * purely slave. We can make a union of them.
 */

/* Connection event timing */
#define BLE_LL_CONN_ITVL_USECS              (1250)
#define BLE_LL_CONN_TX_WIN_USECS            (1250)
#define BLE_LL_CONN_CE_USECS                (625)
#define BLE_LL_CONN_TX_WIN_MIN              (1)         /* in tx win units */
#define BLE_LL_CONN_SLAVE_LATENCY_MAX       (499)

/* 
 * The amount of time that we will wait to hear the start of a receive
 * packet in a connection event.
 * 
 * XXX: move this definition and make it more accurate.
 */
#define BLE_LL_WFR_USECS                    (100)

/* Configuration parameters */
#define BLE_LL_CONN_CFG_TX_WIN_SIZE         (1)
#define BLE_LL_CONN_CFG_TX_WIN_OFF          (0)
#define BLE_LL_CONN_CFG_MASTER_SCA          (BLE_MASTER_SCA_251_500_PPM << 5)
#define BLE_LL_CONN_CFG_MAX_CONNS           (8)
#define BLE_LL_CONN_CFG_OUR_SCA             (500)   /* in ppm */

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

/* Sleep clock accuracy table (in ppm) */
static const uint16_t g_ble_sca_ppm_tbl[8] =
{
    500, 250, 150, 100, 75, 50, 30, 20
};

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

/* Pointer to connection state machine we are trying to create */
struct ble_ll_conn_sm *g_ble_ll_conn_create_sm;

/* Pointer to current connection */
struct ble_ll_conn_sm *g_ble_ll_conn_cur_sm;

/* Connection state machine array */
struct ble_ll_conn_sm g_ble_ll_conn_sm[BLE_LL_CONN_CFG_MAX_CONNS];

/* List of active connections */
static SLIST_HEAD(, ble_ll_conn_sm) g_ble_ll_conn_active_list;

/* List of free connections */
static STAILQ_HEAD(, ble_ll_conn_sm) g_ble_ll_conn_free_list;

/* Statistics */
struct ble_ll_conn_stats
{
    uint32_t cant_set_sched;
    uint32_t conn_ev_late;
    uint32_t wfr_expirations;
    uint32_t handle_not_found;
    uint32_t no_tx_pdu;
    uint32_t no_conn_sm;
    uint32_t no_free_conn_sm;
    uint32_t slave_rxd_bad_conn_req_params;
    uint32_t slave_ce_failures;
    uint32_t rx_resent_pdus;
    uint32_t data_pdu_rx_valid;
    uint32_t data_pdu_rx_invalid;
    uint32_t data_pdu_rx_bad_llid;
    uint32_t data_pdu_rx_dup;
    uint32_t data_pdu_txd;
    uint32_t data_pdu_txg;
    uint32_t data_pdu_txf;
    uint32_t conn_req_txd;
    uint32_t ctrl_pdu_rxd;
    uint32_t l2cap_pdu_rxd;
};
struct ble_ll_conn_stats g_ble_ll_conn_stats;

/**
 * Get a connection state machine.
 */
struct ble_ll_conn_sm *
ble_ll_conn_sm_get(void)
{
    struct ble_ll_conn_sm *connsm;

    connsm = STAILQ_FIRST(&g_ble_ll_conn_free_list);
    if (connsm) {
        STAILQ_REMOVE_HEAD(&g_ble_ll_conn_free_list, free_stqe);
    } else {
        ++g_ble_ll_conn_stats.no_free_conn_sm;
    }

    return connsm;
}

/**
 * Calculate the amount of window widening for a given connection event. This 
 * is the amount of time that a slave has to account for when listening for 
 * the start of a connection event. 
 * 
 * @param connsm Pointer to connection state machine.
 * 
 * @return uint32_t The current window widening amount (in microseconds)
 */
uint32_t
ble_ll_conn_calc_window_widening(struct ble_ll_conn_sm *connsm)
{
    uint32_t total_sca_ppm;
    uint32_t window_widening;
    int32_t time_since_last_anchor;
    uint32_t delta_msec;

    window_widening = 0;

    time_since_last_anchor = (int32_t)(connsm->anchor_point - 
                                       connsm->last_anchor_point);
    if (time_since_last_anchor > 0) {
        delta_msec = cputime_ticks_to_usecs(time_since_last_anchor) / 1000;
        total_sca_ppm = g_ble_sca_ppm_tbl[connsm->master_sca] + 
                        BLE_LL_CONN_CFG_OUR_SCA;
        window_widening = (total_sca_ppm * delta_msec) / 1000;
    }

    /* XXX: spec gives 16 usecs error btw. Probably should add that in */
    return window_widening;
}

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
        aa = (aa << 16) | aa_low;
        bits_diff = 0;
        temp = aa ^ BLE_ACCESS_ADDR_ADV;
        for (mask = 0x00000001; mask != 0; mask <<= 1) {
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

        /* We have a valid access address */
        break;
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
 * Callback for connection wait for response timer. 
 *  
 * Context: Interrupt 
 *
 * @param sch 
 * 
 */
static void
ble_ll_conn_wfr_timer_exp(void *arg)
{
    struct ble_ll_conn_sm *connsm;

    connsm = (struct ble_ll_conn_sm *)arg;
    if (connsm->rsp_rxd == 0) {
        ble_ll_event_send(&connsm->conn_ev_end);
        ++g_ble_ll_conn_stats.wfr_expirations;
    }
}

/**
 * Callback for slave when it transmits a data pdu and the connection event
 * ends after the transmission.
 *  
 * Context: Interrupt 
 *
 * @param sch 
 * 
 */
static void
ble_ll_conn_wait_txend(void *arg)
{
    struct ble_ll_conn_sm *connsm;

    connsm = (struct ble_ll_conn_sm *)arg;
    ble_ll_event_send(&connsm->conn_ev_end);
}

/**
 * Called when we want to send a data channel pdu inside a connection event. 
 *  
 * Context: interrupt 
 *  
 * @param connsm 
 * 
 * @return int 0: success; otherwise failure to transmit
 */
static int
ble_ll_conn_tx_data_pdu(struct ble_ll_conn_sm *connsm, int beg_transition)
{
    int rc;
    uint8_t md;
    uint8_t hdr_byte;
    uint8_t end_transition;
    uint32_t wfr_time;
    uint32_t ticks;
    struct os_mbuf *m;
    struct ble_mbuf_hdr *ble_hdr;
    struct os_mbuf_pkthdr *pkthdr;
    struct os_mbuf_pkthdr *nextpkthdr;

    /*
     * Get packet off transmit queue. If not available, send empty PDU. Set
     * the more data bit as well. For a slave, we will set it regardless of
     * connection event end timing (master will deal with that for us or the
     * connection event will be terminated locally). For a master, we only
     * set the MD bit if we have enough time to send the current PDU, get
     * a response and send the next packet and get a response.
     */
    md = 0;
    pkthdr = STAILQ_FIRST(&connsm->conn_txq);
    if (pkthdr) {
        m = OS_MBUF_PKTHDR_TO_MBUF(pkthdr);
        nextpkthdr = STAILQ_NEXT(pkthdr, omp_next);
        if (nextpkthdr) {
            md = 1;
            if (connsm->conn_role == BLE_LL_CONN_ROLE_MASTER) {
                /* 
                 * XXX: this calculation is based on using the current time
                 * and assuming the transmission will occur an IFS time from
                 * now. This is not the most accurate especially if we have
                 * received a frame and we are replying to it.
                 */ 
                ticks = (BLE_LL_IFS * 4) + (2 * connsm->eff_max_rx_time) +
                    ble_ll_pdu_tx_time_get(pkthdr->omp_len) + 
                    ble_ll_pdu_tx_time_get(nextpkthdr->omp_len);
                ticks = cputime_usecs_to_ticks(ticks);
                if ((cputime_get32() + ticks) >= connsm->ce_end_time) {
                    md = 0;
                }
            }
        }
        ble_hdr = BLE_MBUF_HDR_PTR(m);
    } else {
        /* Send empty pdu */
        m = (struct os_mbuf *)&connsm->conn_empty_pdu;
        ble_hdr = BLE_MBUF_HDR_PTR(m);
        ble_hdr->flags = 0;
        m->om_data[0] = BLE_LL_LLID_DATA_FRAG;
        pkthdr = OS_MBUF_PKTHDR(m);
        STAILQ_INSERT_HEAD(&connsm->conn_txq, pkthdr, omp_next);
    }

    /* XXX: how does the master end the connection event if we are getting
     * too close the end? We do this if we have a data packet but not
       if we send the empty pdu*/

    /* Set SN, MD, NESN in transmit PDU */
    hdr_byte = m->om_data[0];

    /* 
     * If this is a retry, we keep the LLID and SN. If not, we presume
     * that the LLID is set and all other bits are 0 in the first header
     * byte.
     */
    if (ble_hdr->flags & BLE_MBUF_HDR_F_TXD) {
        hdr_byte &= ~(BLE_LL_DATA_HDR_MD_MASK | BLE_LL_DATA_HDR_NESN_MASK);
    } else {
        if (connsm->tx_seqnum) {
            hdr_byte |= BLE_LL_DATA_HDR_SN_MASK;
        }
        ble_hdr->flags |= BLE_MBUF_HDR_F_TXD;
    }

    /* If we have more data, set the bit */
    if (md) {
        hdr_byte |= BLE_LL_DATA_HDR_MD_MASK;
    }

    /* Set NESN (next expected sequence number) bit */
    if (connsm->next_exp_seqnum) {
        hdr_byte |= BLE_LL_DATA_HDR_NESN_MASK;
    }

    /* Set the header byte in the outgoing frame */
    m->om_data[0] = hdr_byte;

    /* 
     * If we are a slave, check to see if this transmission will end the
     * connection event. We will end the connection event if we have
     * received a valid frame with the more data bit set to 0 and we dont
     * have more data.
     */
    end_transition = BLE_PHY_TRANSITION_TX_RX;
    if ((connsm->conn_role == BLE_LL_CONN_ROLE_SLAVE) && (md == 0) && 
        (connsm->cons_rxd_bad_crc == 0) &&
        ((connsm->last_rxd_hdr_byte & BLE_LL_DATA_HDR_MD_MASK) == 0)) {
        end_transition = BLE_PHY_TRANSITION_NONE;
    }

    rc = ble_phy_tx(m, beg_transition, end_transition);
    if (!rc) {
        /* Set flag denoting we transmitted a pdu */
        connsm->pdu_txd = 1;

        /* Set last transmitted MD bit */
        connsm->last_txd_md = md;

        /* Reset response received flag */
        connsm->rsp_rxd = 0;

        /* XXX: at some point I should implement the transmit end interrupt
           from the PHY. When I do that I will change this code */
        /* Set wait for response timer */
        wfr_time = cputime_get32();
        if (end_transition == BLE_PHY_TRANSITION_TX_RX) {
            wfr_time += cputime_usecs_to_ticks(XCVR_TX_START_DELAY_USECS +
                                       ble_ll_pdu_tx_time_get(m->om_len) +
                                       BLE_LL_IFS +
                                       BLE_LL_WFR_USECS);
            ble_ll_wfr_enable(wfr_time, ble_ll_conn_wfr_timer_exp, connsm);
        } else {
            wfr_time += cputime_usecs_to_ticks(XCVR_TX_START_DELAY_USECS +
                                       ble_ll_pdu_tx_time_get(m->om_len));
            ble_ll_wfr_enable(wfr_time, ble_ll_conn_wait_txend, connsm);
        }

        ++g_ble_ll_conn_stats.data_pdu_txd;
    }

    return rc;
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
    struct ble_ll_conn_sm *connsm;

    connsm = (struct ble_ll_conn_sm *)sch->cb_arg;
    ble_ll_event_send(&connsm->conn_ev_end);
    return BLE_LL_SCHED_STATE_DONE;
}

/**
 * Schedule callback for start of connection event 
 *  
 * Context: Interrupt 
 * 
 * @param sch 
 * 
 * @return int 0: scheduled item is still running. 1: schedule item is done.
 */
static int
ble_ll_conn_event_start_cb(struct ble_ll_sched_item *sch)
{
    int rc;
    uint32_t usecs;
    uint32_t wfr_time;
    struct ble_ll_conn_sm *connsm;

    /* set led */
    gpio_clear(LED_BLINK_PIN);

    /* Set current connection state machine */
    connsm = (struct ble_ll_conn_sm *)sch->cb_arg;
    g_ble_ll_conn_cur_sm = connsm;

    ble_ll_log(BLE_LL_LOG_ID_CONN_EV_START, connsm->data_chan_index, 0, 0);

    /* Set LL state */
    ble_ll_state_set(BLE_LL_STATE_CONNECTION);

    /* Set channel */
    rc = ble_phy_setchan(connsm->data_chan_index, connsm->access_addr, 
                         connsm->crcinit);
    assert(rc == 0);

    if (connsm->conn_role == BLE_LL_CONN_ROLE_MASTER) {
        rc = ble_ll_conn_tx_data_pdu(connsm, BLE_PHY_TRANSITION_NONE);
        if (!rc) {
            sch->next_wakeup = sch->end_time;
            sch->sched_cb = ble_ll_conn_ev_end_sched_cb;
            rc = BLE_LL_SCHED_STATE_RUNNING;
        } else {
            /* Inform LL task of connection event end */
            ble_ll_event_send(&connsm->conn_ev_end);
            rc = BLE_LL_SCHED_STATE_DONE;
        }
    } else {
        rc = ble_phy_rx();
        if (rc) {
            /* End the connection event as we have no more buffers */
            ++g_ble_ll_conn_stats.slave_ce_failures;
            ble_ll_event_send(&connsm->conn_ev_end);
            rc = BLE_LL_SCHED_STATE_DONE;
        } else {
            /* 
             * Set flag that tell slave to set last anchor point if a packet
             * has been received.
             */ 
            connsm->slave_set_last_anchor = 1;

            usecs = connsm->slave_cur_tx_win_usecs + BLE_LL_WFR_USECS +
                connsm->slave_cur_window_widening;
            wfr_time = connsm->anchor_point + cputime_usecs_to_ticks(usecs);
            ble_ll_wfr_enable(wfr_time, ble_ll_conn_wfr_timer_exp, connsm);

            /* Set next wakeup time to connection event end time */
            sch->next_wakeup = sch->end_time;
            sch->sched_cb = ble_ll_conn_ev_end_sched_cb;
            rc = BLE_LL_SCHED_STATE_RUNNING;
        }
    }
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
            /* Include window widening and scheduling delay */
            usecs = connsm->slave_cur_window_widening+XCVR_RX_SCHED_DELAY_USECS;
            sch->start_time = connsm->anchor_point - 
                cputime_usecs_to_ticks(usecs);

            /* Schedule entire connection interval for slave */
            usecs = connsm->conn_itvl * BLE_LL_CONN_ITVL_USECS;
        }

        /* XXX: we will need to subtract more than just the IFS since we need
         * some time to deal with stuff. Gotta make sure we dont miss
           next connection event. */
        usecs -= BLE_LL_IFS;
        sch->end_time = connsm->anchor_point + cputime_usecs_to_ticks(usecs);
        connsm->ce_end_time = sch->end_time;

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

    connsm = (struct ble_ll_conn_sm *)arg;
    ble_ll_event_send(&connsm->conn_spvn_ev);
}

/**
 * Called when a create connection command has been received. This initializes 
 * a connection state machine in the master role. 
 *  
 * NOTE: Must be called before the state machine is started 
 * 
 * @param connsm 
 * @param hcc 
 */
static void
ble_ll_conn_master_init(struct ble_ll_conn_sm *connsm, 
                        struct hci_create_conn *hcc)
{
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
}

/**
 * Start the connection state machine. This is done once per connection 
 * when the HCI command "create connection" is issued to the controller or 
 * when a slave receives a connect request, 
 * 
 * @param connsm 
 */
static void
ble_ll_conn_sm_start(struct ble_ll_conn_sm *connsm)
{
    struct ble_ll_conn_global_params *conn_params;

    /* Reset event counter and last unmapped channel */
    connsm->last_unmapped_chan = 0;
    connsm->event_cntr = 0;
    connsm->conn_state = BLE_LL_CONN_STATE_IDLE;
    connsm->allow_slave_latency = 0;

    /* Reset current control procedure */
    connsm->cur_ctrl_proc = BLE_LL_CTRL_PROC_IDLE;
    connsm->pending_ctrl_procs = 0;

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

    /* Initialize transmit queue and ack/flow control elements */
    STAILQ_INIT(&connsm->conn_txq);
    connsm->tx_seqnum = 0;
    connsm->next_exp_seqnum = 0;
    connsm->last_txd_md = 0;
    connsm->cons_rxd_bad_crc = 0;
    connsm->last_rxd_sn = 1;

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

    /* Add to list of active connections */
    SLIST_INSERT_HEAD(&g_ble_ll_conn_active_list, connsm, act_sle);
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
                htole16(evbuf + 18, connsm->supervision_tmo);
                evbuf[20] = connsm->master_sca;
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
void
ble_ll_conn_end(struct ble_ll_conn_sm *connsm, uint8_t ble_err)
{
    struct os_mbuf *m;
    struct os_mbuf_pkthdr *pkthdr;

    /* Disable the PHY */
    ble_phy_disable();

    /* Remove scheduler events just in case */
    ble_ll_sched_rmv(BLE_LL_SCHED_TYPE_CONN, connsm);

    /* Stop supervision timer */
    cputime_timer_stop(&connsm->conn_spvn_timer);

    /* Disable any wait for response interrupt that might be running */
    ble_ll_wfr_disable();

    /* Stop any control procedures that might be running */
    os_callout_stop(&connsm->ctrl_proc_timer.cf_c);

    /* Remove from the active connection list */
    SLIST_REMOVE(&g_ble_ll_conn_active_list, connsm, ble_ll_conn_sm, act_sle);

    /* Free all packets on transmit queue */
    while (1) {
        /* Get mbuf pointer from packet header pointer */
        pkthdr = STAILQ_FIRST(&connsm->conn_txq);
        if (!pkthdr) {
            break;
        }
        STAILQ_REMOVE_HEAD(&connsm->conn_txq, omp_next);

        m = (struct os_mbuf *)((uint8_t *)pkthdr - sizeof(struct os_mbuf));
        os_mbuf_free(m);
    }

    /* Make sure events off queue */
    os_eventq_remove(&g_ble_ll_data.ll_evq, &connsm->conn_spvn_ev);
    os_eventq_remove(&g_ble_ll_data.ll_evq, &connsm->conn_ev_end);

    /* Connection state machine is now idle */
    connsm->conn_state = BLE_LL_CONN_STATE_IDLE;

    /* Set Link Layer state to standby */
    ble_ll_state_set(BLE_LL_STATE_STANDBY);

    /* Send connection complete event */
    ble_ll_conn_comp_event_send(connsm, ble_err);

    /* Put connection state machine back on free list */
    STAILQ_INSERT_TAIL(&g_ble_ll_conn_free_list, connsm, free_stqe);
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
        /* XXX: move this into the phy transmit pdu end callback for the
           connect request */
        /* We have yet to transmit the connect request so add that time */
        usecs += ble_ll_pdu_tx_time_get(BLE_CONNECT_REQ_PDU_LEN) + BLE_LL_IFS;
    } else {
        connsm->slave_cur_tx_win_usecs = 
            connsm->tx_win_size * BLE_LL_CONN_TX_WIN_USECS;
    }
    connsm->last_anchor_point = cputime_get32();
    connsm->anchor_point = connsm->last_anchor_point + 
        cputime_usecs_to_ticks(usecs);

    /* Send connection complete event to inform host of connection */
    ble_ll_conn_comp_event_send(connsm, BLE_ERR_SUCCESS);

    /* Start the scheduler for the first connection event */
    ble_ll_conn_sched_set(connsm);
}

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
    uint32_t cur_ww;
    uint32_t max_ww;
    struct ble_ll_conn_sm *connsm;

    connsm = (struct ble_ll_conn_sm *)arg;

    /* Disable the PHY */
    ble_phy_disable();

    /* Disable the wfr timer */
    ble_ll_wfr_disable();

    /* Remove any scheduled items for this connection */
    ble_ll_sched_rmv(BLE_LL_SCHED_TYPE_CONN, connsm);
    os_eventq_remove(&g_ble_ll_data.ll_evq, &connsm->conn_ev_end);

    /* 
     * If we have received a packet, we can set the current transmit window
     * usecs to 0 since we dont need to listen in the transmit window.
     */
    if (connsm->pkt_rxd) {
        connsm->slave_cur_tx_win_usecs = 0;
    }

    /* 
     * XXX: not quite sure I am interpreting slave latency correctly here.
     * The spec says if you applied slave latency and you dont hear a packet,
     * you dont apply slave latency. Does that mean you dont apply slave
     * latency until you hear a packet or on the next interval if you listen
     * and dont hear anything, can you apply slave latency?
     */
    /* Set event counter to the next connection event that we will tx/rx in */
    itvl = connsm->conn_itvl * BLE_LL_CONN_ITVL_USECS;
    latency = 1;
    if (connsm->allow_slave_latency) {
        if (connsm->pkt_rxd) {
            latency += connsm->slave_latency;
            itvl = itvl * latency;
        }
    }
    connsm->event_cntr += latency;

    /* Set next connection event start time */
    connsm->anchor_point += cputime_usecs_to_ticks(itvl);

    /* Calculate data channel index of next connection event */
    connsm->last_unmapped_chan = connsm->unmapped_chan;
    while (latency > 0) {
        --latency;
        connsm->data_chan_index = ble_ll_conn_calc_dci(connsm);
    }

    /* XXX: we will need more time here for a slave I bet. IFS time is not
       enough to re-schedule. End before it a bit.*/
    /* We better not be late for the anchor point. If so, skip events */
    while ((int32_t)(connsm->anchor_point - cputime_get32()) <= 0) {
        ++connsm->event_cntr;
        connsm->data_chan_index = ble_ll_conn_calc_dci(connsm);
        connsm->anchor_point += 
            cputime_usecs_to_ticks(connsm->conn_itvl * BLE_LL_CONN_ITVL_USECS);
        ++g_ble_ll_conn_stats.conn_ev_late;
    }

    /* Reset "per connection event" variables */
    connsm->cons_rxd_bad_crc = 0;
    connsm->pkt_rxd = 0;

    /* Link-layer is in standby state now */
    ble_ll_state_set(BLE_LL_STATE_STANDBY);

    /* Set current LL connection to NULL */
    g_ble_ll_conn_cur_sm = NULL;

    /* Calculate window widening for next event. If too big, end conn */
    if (connsm->conn_role == BLE_LL_CONN_ROLE_SLAVE) {
        cur_ww = ble_ll_conn_calc_window_widening(connsm);
        max_ww = (connsm->conn_itvl * (BLE_LL_CONN_ITVL_USECS/2)) - BLE_LL_IFS;
        if (cur_ww >= max_ww) {
            ble_ll_conn_end(connsm, BLE_ERR_CONN_TERM_LOCAL);
            return;
        }
        connsm->slave_cur_window_widening = cur_ww;
    }

    ble_ll_log(BLE_LL_LOG_ID_CONN_EV_END, 0, 0, connsm->event_cntr);

    /* Schedule the next connection event */
    ble_ll_conn_sched_set(connsm);

    /* turn led off */
    gpio_set(LED_BLINK_PIN);
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
    dptr[4] = (uint8_t)connsm->crcinit;
    dptr[5] = (uint8_t)(connsm->crcinit >> 8);
    dptr[6] = (uint8_t)(connsm->crcinit >> 16);
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
    uint32_t spvn_tmo_usecs;
    uint32_t min_spvn_tmo_usecs;
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
    spvn_tmo_usecs = hcc->supervision_timeout;
    spvn_tmo_usecs *= (BLE_HCI_CONN_SPVN_TMO_UNITS * 1000);
    min_spvn_tmo_usecs = (uint32_t)hcc->conn_itvl_max * 2 * 
        BLE_LL_CONN_ITVL_USECS;
    min_spvn_tmo_usecs *= (1 + hcc->conn_latency);
    if (spvn_tmo_usecs <= min_spvn_tmo_usecs) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Min/max connection event lengths */
    hcc->min_ce_len = le16toh(cmdbuf + 21);
    hcc->max_ce_len = le16toh(cmdbuf + 23);
    if (hcc->min_ce_len > hcc->max_ce_len) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Make sure we can accept a connection! */
    connsm = ble_ll_conn_sm_get();
    if (connsm == NULL) {
        return BLE_ERR_CONN_LIMIT;
    }

    /* Initialize state machine in master role and start state machine */
    ble_ll_conn_master_init(connsm, hcc);
    ble_ll_conn_sm_start(connsm);

    /* Create the connection request */
    ble_ll_conn_req_pdu_make(connsm);

    /* Start scanning */
    rc = ble_ll_scan_initiator_start(hcc);
    if (rc) {
        SLIST_REMOVE(&g_ble_ll_conn_active_list,connsm,ble_ll_conn_sm,act_sle);
        STAILQ_INSERT_TAIL(&g_ble_ll_conn_free_list, connsm, free_stqe);
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

    /* XXX: Deal with different types of random addresses here! */
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
    if (ble_hdr->flags & BLE_MBUF_HDR_F_CONN_REQ_TXD) {
        /* Set address of advertiser to which we are connecting. */
        if (!ble_ll_scan_whitelist_enabled()) {
            /* 
             * XXX: need to see if the whitelist tells us exactly what peer
             * addr type we should use? Not sure it matters. If whitelisting
             * is not used the peer addr and type already set
             */ 
            /* Get address type of advertiser */
            if (rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK) {
                addr_type = BLE_HCI_CONN_PEER_ADDR_RANDOM;
            } else {
                addr_type = BLE_HCI_CONN_PEER_ADDR_PUBLIC;
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
            addr_type = BLE_HCI_CONN_PEER_ADDR_RANDOM;
        } else {
            addr_type = BLE_HCI_CONN_PEER_ADDR_PUBLIC;
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
            ++g_ble_ll_conn_stats.conn_req_txd;
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
ble_ll_conn_rx_pdu_start(void)
{
    struct ble_ll_conn_sm *connsm;

    /* 
     * Disable wait for response timer since we receive a response. We dont
     * care if this is the response we were waiting for or not; the code
     * called at receive end will deal with ending the connection event
     * if needed
     */ 
    ble_ll_wfr_disable();
    connsm = g_ble_ll_conn_cur_sm;
    if (connsm) {
        connsm->rsp_rxd = 1;
        connsm->pkt_rxd = 1;

        /* Set last anchor point if 1st received frame in connection event */
        if (connsm->slave_set_last_anchor) {
            connsm->slave_set_last_anchor = 0;
            /* 
             * XXX: for now, just use the current time. Make more accurate.
             * We subtract 40 usecs since we get the start 5 bytes after
             * actual start. We add another 10 just in case.
             */
            connsm->last_anchor_point = cputime_get32() - 
                cputime_usecs_to_ticks(50);
        }
    }
}

/**
 * Called from the Link Layer task when a data PDU has been received
 *  
 * Context: Link layer task 
 * 
 * @param rxpdu 
 */
void
ble_ll_conn_rx_data_pdu(struct os_mbuf *rxpdu, uint8_t crcok)
{
    uint8_t hdr_byte;
    uint8_t rxd_sn;
    uint8_t *rxbuf;
    uint16_t acl_len;
    uint16_t acl_hdr;
    uint32_t tmo;
    struct ble_ll_conn_sm *connsm;

    if (crcok) {
        /* Count valid received data pdus */
        ++g_ble_ll_conn_stats.data_pdu_rx_valid;

        /* We better have a connection state machine */
        connsm = g_ble_ll_conn_cur_sm;
        if (connsm) {
            /* Reset the connection supervision timeout */
            cputime_timer_stop(&connsm->conn_spvn_timer);
            tmo = connsm->supervision_tmo * BLE_HCI_CONN_SPVN_TMO_UNITS * 1000;
            cputime_timer_relative(&connsm->conn_spvn_timer, tmo);

            rxbuf = rxpdu->om_data;
            hdr_byte = rxbuf[0];
            acl_len = rxbuf[1];
            acl_hdr = hdr_byte & BLE_LL_DATA_HDR_LLID_MASK;

            /* Check that the LLID is reasonable */
            if ((acl_hdr == 0) || 
                ((acl_hdr == BLE_LL_LLID_DATA_START) && (acl_len == 0))) {
                ++g_ble_ll_conn_stats.data_pdu_rx_bad_llid;
                goto conn_rx_data_pdu_end;
            }

            /* 
             * If we are a slave, we can only start to use slave latency
             * once we have received a NESN of 1 from the master
             */ 
            if (connsm->conn_role == BLE_LL_CONN_ROLE_SLAVE) {
                if (hdr_byte & BLE_LL_DATA_HDR_NESN_MASK) {
                    connsm->allow_slave_latency = 1;
                }
            }

            /* 
             * Discard the received PDU if the sequence number is the same
             * as the last received sequence number
             */ 
            rxd_sn = hdr_byte & BLE_LL_DATA_HDR_SN_MASK;
            if (rxd_sn != connsm->last_rxd_sn) {
                /* Update last rxd sn */
                connsm->last_rxd_sn = rxd_sn;

                /* No need to do anything if empty pdu */
                if ((acl_hdr == BLE_LL_LLID_DATA_FRAG) && (acl_len == 0)) {
                    goto conn_rx_data_pdu_end;
                }

                if (acl_hdr == BLE_LL_LLID_CTRL) {
                    /* XXX: Process control frame! For now just free */
                    ++g_ble_ll_conn_stats.ctrl_pdu_rxd;
                    ble_ll_ctrl_rx_pdu(connsm, rxpdu);
                } else {

                    /* Count # of data frames */
                    ++g_ble_ll_conn_stats.l2cap_pdu_rxd;

                    /* NOTE: there should be at least two bytes available */
                    assert(OS_MBUF_LEADINGSPACE(rxpdu) >= 2);
                    os_mbuf_prepend(rxpdu, 2);
                    rxbuf = rxpdu->om_data;

                    acl_hdr = (acl_hdr << 12) | connsm->conn_handle;
                    htole16(rxbuf, acl_hdr);
                    htole16(rxbuf + 2, acl_len);
                    ble_hs_rx_data(rxpdu);
                    return;
                }
            } else {
                ++g_ble_ll_conn_stats.data_pdu_rx_dup;
            }
        } else {
            ++g_ble_ll_conn_stats.no_conn_sm;
        }
    } else {
        ++g_ble_ll_conn_stats.data_pdu_rx_invalid;
    }

    /* Free buffer */
conn_rx_data_pdu_end:
    os_mbuf_free(rxpdu);
}

/**
 * Called when a packet has been received while in the connection state.
 *  
 * Context: Interrupt
 * 
 * @param rxpdu 
 * @param crcok 
 * 
 * @return int 
 *       < 0: Disable the phy after reception.
 *      == 0: Success. Do not disable the PHY.
 *       > 0: Do not disable PHY as that has already been done.
 */
int
ble_ll_conn_rx_pdu_end(struct os_mbuf *rxpdu, uint8_t crcok)
{
    int rc;
    uint8_t hdr_byte;
    uint8_t hdr_sn;
    uint8_t hdr_nesn;
    uint8_t conn_sn;
    uint8_t conn_nesn;
    uint8_t reply;
    uint32_t ticks;
    struct os_mbuf *txpdu;
    struct os_mbuf_pkthdr *pkthdr;
    struct ble_ll_conn_sm *connsm;

    /* 
     * We should have a current connection state machine. If we dont, we just
     * hand the packet to the higher layer to count it.
     */
    connsm = g_ble_ll_conn_cur_sm;
    if (!connsm) {
        return -1;
    }

    /* 
     * Check the packet CRC. A connection event can continue even if the
     * received PDU does not pass the CRC check. If we receive two consecutive
     * CRC errors we end the conection event.
     */
    if (!crcok) {
        /* 
         * Increment # of consecutively received CRC errors. If more than
         * one we will end the connection event.
         */ 
        ++connsm->cons_rxd_bad_crc;
        if (connsm->cons_rxd_bad_crc >= 2) {
            reply = 0;
        } else {
            if (connsm->conn_role == BLE_LL_CONN_ROLE_MASTER) {
                reply = connsm->last_txd_md;
            } else {
                /* A slave always responds with a packet */
                reply = 1;
            }
        }
    } else {
        /* Reset consecutively received bad crcs (since this one was good!) */
        connsm->cons_rxd_bad_crc = 0;

        /* Store received header byte in state machine  */
        hdr_byte = rxpdu->om_data[0];
        connsm->last_rxd_hdr_byte = hdr_byte;

        /* 
         * If SN bit from header does not match NESN in connection, this is
         * a resent PDU and should be ignored.
         */ 
        hdr_sn = hdr_byte & BLE_LL_DATA_HDR_SN_MASK;
        conn_nesn = connsm->next_exp_seqnum;
        if ((hdr_sn && conn_nesn) || (!hdr_sn && !conn_nesn)) {
            connsm->next_exp_seqnum ^= 1;
        } else {
            /* Count re-sent PDUs. */
            ++g_ble_ll_conn_stats.rx_resent_pdus;
        }

        /* 
         * Check NESN bit from header. If same as tx seq num, the transmission
         * is acknowledged. Otherwise we need to resend this PDU.
         */
        if (connsm->pdu_txd) {
            hdr_nesn = hdr_byte & BLE_LL_DATA_HDR_NESN_MASK;
            conn_sn = connsm->tx_seqnum;
            if ((hdr_nesn && conn_sn) || (!hdr_nesn && !conn_sn)) {
                /* We did not get an ACK. Must retry the PDU */
                ++g_ble_ll_conn_stats.data_pdu_txf;
            } else {
                /* Transmit success */
                connsm->tx_seqnum ^= 1;
                ++g_ble_ll_conn_stats.data_pdu_txg;

                /* We can remove this packet from the queue now */
                pkthdr = STAILQ_FIRST(&connsm->conn_txq);
                if (pkthdr) {
                    STAILQ_REMOVE_HEAD(&connsm->conn_txq, omp_next);
                    txpdu = OS_MBUF_PKTHDR_TO_MBUF(pkthdr);
                    os_mbuf_free(txpdu);
                } else {
                    /* No packet on queue? This is an error! */
                    ++g_ble_ll_conn_stats.no_tx_pdu;
                }
            }
        }

        /* Should we continue connection event? */
        if (connsm->conn_role == BLE_LL_CONN_ROLE_MASTER) {
            reply = connsm->last_txd_md || (hdr_byte & BLE_LL_DATA_HDR_MD_MASK);
            if (reply) {
                pkthdr = STAILQ_FIRST(&connsm->conn_txq);
                if (pkthdr) {
                    ticks = ble_ll_pdu_tx_time_get(pkthdr->omp_len);
                } else {
                    /* We will send empty pdu (just a LL header) */
                    ticks = ble_ll_pdu_tx_time_get(BLE_LL_PDU_HDR_LEN);
                }
                ticks += (BLE_LL_IFS * 2) + connsm->eff_max_rx_time;
                ticks = cputime_usecs_to_ticks(ticks);
                /* XXX: should really use packet receive end time here */
                if ((cputime_get32() + ticks) >= connsm->ce_end_time) {
                    reply = 0;
                }
            }
        } else {
            /* A slave always replies */
            reply = 1;
        }
    }

    /* If reply flag set, send data pdu and continue connection event */
    rc = -1;
    if (reply) {
        rc = ble_ll_conn_tx_data_pdu(connsm, BLE_PHY_TRANSITION_RX_TX);
    }

    if (rc) {
        ble_ll_event_send(&connsm->conn_ev_end);
    }

    return 0;
}

/**
 * Called to enqueue a packet on the transmit queue of a connection. Should 
 * only be called by the controller. 
 *  
 * Context: Link Layer 
 * 
 * 
 * @param connsm 
 * @param om 
 */
void 
ble_ll_conn_enqueue_pkt(struct ble_ll_conn_sm *connsm, struct os_mbuf *om)
{
    os_sr_t sr;
    struct ble_mbuf_hdr *ble_hdr;
    struct os_mbuf_pkthdr *pkthdr;

    /* Clear flags field in BLE header */
    ble_hdr = BLE_MBUF_HDR_PTR(om);
    ble_hdr->flags = 0;

    /* Add to transmit queue for the connection */
    pkthdr = OS_MBUF_PKTHDR(om);
    OS_ENTER_CRITICAL(sr);
    STAILQ_INSERT_TAIL(&connsm->conn_txq, pkthdr, omp_next);
    OS_EXIT_CRITICAL(sr);
}

/**
 * Data packet from host. 
 *  
 * Context: Link Layer task 
 * 
 * @param om 
 * @param handle 
 * @param length 
 * 
 * @return int 
 */
void
ble_ll_conn_tx_pkt_in(struct os_mbuf *om, uint16_t handle, uint16_t length)
{
    struct ble_ll_conn_sm *connsm;
    uint16_t conn_handle;
    uint16_t pb;

    /* See if we have an active matching connection handle */
    conn_handle = handle & 0x0FFF;
    SLIST_FOREACH(connsm, &g_ble_ll_conn_active_list, act_sle) {
        if (connsm->conn_handle == conn_handle) {
            /* Construct LL header in buffer */
            /* XXX: deal with length later */
            assert(length <= 251);
            pb = handle & 0x3000;
            if (pb == 0) {
                om->om_data[0] = BLE_LL_LLID_DATA_START;
            } else if (pb == 0x1000) {
                om->om_data[0] = BLE_LL_LLID_DATA_FRAG;
            } else {
                /* This should never happen! */
                break;
            }
            om->om_data[1] = (uint8_t)length;

            /* Clear flags field in BLE header */
            ble_ll_conn_enqueue_pkt(connsm, om);
            return;
        }
    }

    /* No connection found! */
    ++g_ble_ll_conn_stats.handle_not_found;
    os_mbuf_free(om);
}

/**
 * Called when a device has received a connect request while advertising and 
 * the connect request has passed the advertising filter policy and is for 
 * us. This will start a connection in the slave role assuming that we dont 
 * already have a connection with this device and that the connect request 
 * parameters are valid. 
 *  
 * Context: Link Layer 
 *  
 * @param rxbuf Pointer to received PDU 
 *  
 * @return 0: connection started. 
 */
int
ble_ll_conn_slave_start(uint8_t *rxbuf)
{
    uint32_t temp;
    uint32_t crcinit;
    uint8_t *inita;
    uint8_t *dptr;
    uint8_t addr_type;
    struct ble_ll_conn_sm *connsm;

    /* Ignore the connection request if we are already connected*/
    inita = rxbuf + BLE_LL_PDU_HDR_LEN;
    SLIST_FOREACH(connsm, &g_ble_ll_conn_active_list, act_sle) {
        if (!memcmp(&connsm->peer_addr, inita, BLE_DEV_ADDR_LEN)) {
            if (rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK) {
                addr_type = BLE_HCI_CONN_PEER_ADDR_RANDOM;
            } else {
                addr_type = BLE_HCI_CONN_PEER_ADDR_PUBLIC;
            }
            if (connsm->peer_addr_type == addr_type) {
                return 0;
            }
        }
    }

    /* Allocate a connection. If none available, dont do anything */
    connsm = ble_ll_conn_sm_get();
    if (connsm == NULL) {
        return 0;
    }

    /* Set the pointer at the start of the connection data */
    dptr = rxbuf + BLE_LL_CONN_REQ_ADVA_OFF + BLE_DEV_ADDR_LEN;

    /* Set connection state machine information */
    connsm->access_addr = le32toh(dptr);
    crcinit = dptr[6];
    crcinit = (crcinit << 8) | dptr[5];
    crcinit = (crcinit << 8) | dptr[4];
    connsm->crcinit = crcinit;
    connsm->tx_win_size = dptr[7];
    connsm->tx_win_off = le16toh(dptr + 8);
    connsm->conn_itvl = le16toh(dptr + 10);
    connsm->slave_latency = le16toh(dptr + 12);
    connsm->supervision_tmo = le16toh(dptr + 14);
    memcpy(&connsm->chanmap, dptr + 16, BLE_LL_CONN_CHMAP_LEN);
    connsm->hop_inc = dptr[21] & 0x1F;
    connsm->master_sca = dptr[21] >> 5;

    /* Error check parameters */
    if ((connsm->tx_win_off > connsm->conn_itvl) ||
        (connsm->conn_itvl < BLE_HCI_CONN_ITVL_MIN) ||
        (connsm->conn_itvl > BLE_HCI_CONN_ITVL_MAX) ||
        (connsm->tx_win_size < BLE_LL_CONN_TX_WIN_MIN) ||
        (connsm->slave_latency > BLE_LL_CONN_SLAVE_LATENCY_MAX)) {
        goto err_slave_start;
    }

    /* Slave latency cannot cause a supervision timeout */
    temp = (connsm->slave_latency + 1) * (connsm->conn_itvl * 2) * 
            BLE_LL_CONN_ITVL_USECS;
    if ((connsm->supervision_tmo * 10000) <= temp ) {
        goto err_slave_start;
    }

    /* 
     * The transmit window must be less than or equal to the lesser of 10 
     * msecs or the connection interval minus 1.25 msecs.
     */ 
    temp = connsm->conn_itvl - 1;
    if (temp > 8) {
        temp = 8;
    }
    if (connsm->tx_win_size > temp) {
        goto err_slave_start;
    }

    /* XXX: might want to set this differently based on adv. filter policy! */
    /* Set the address of device that we are connecting with */
    memcpy(&connsm->peer_addr, rxbuf + BLE_LL_PDU_HDR_LEN, BLE_DEV_ADDR_LEN);
    if (rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK) {
        connsm->peer_addr_type = BLE_HCI_CONN_PEER_ADDR_RANDOM;
    } else {
        connsm->peer_addr_type = BLE_HCI_CONN_PEER_ADDR_PUBLIC;
    }

    /* Calculate number of used channels; make sure it meets min requirement */
    connsm->num_used_chans = ble_ll_conn_calc_used_chans(connsm->chanmap);
    if (connsm->num_used_chans < 2) {
        goto err_slave_start;
    }

    /* Start the connection state machine */
    connsm->conn_role = BLE_LL_CONN_ROLE_SLAVE;
    ble_ll_conn_sm_start(connsm);

    /* The connection has been created. */
    ble_ll_conn_created(connsm);

    return 1;

err_slave_start:
    STAILQ_INSERT_TAIL(&g_ble_ll_conn_free_list, connsm, free_stqe);
    ++g_ble_ll_conn_stats.slave_rxd_bad_conn_req_params;
    return 0;
}

/* Initialize the connection module */
void
ble_ll_conn_init(void)
{
    uint16_t i;
    uint16_t maxbytes;
    struct os_mbuf *m;
    struct ble_ll_conn_sm *connsm;
    struct ble_ll_conn_global_params *conn_params;

    /* Initialize list of active conections */
    SLIST_INIT(&g_ble_ll_conn_active_list);
    STAILQ_INIT(&g_ble_ll_conn_free_list);

    /* 
     * Take all the connections off the free memory pool and add them to
     * the free connection list, assigning handles in linear order. Note:
     * the specification allows a handle of zero; we just avoid using it.
     */
    connsm = &g_ble_ll_conn_sm[0];
    for (i = 0; i < BLE_LL_CONN_CFG_MAX_CONNS; ++i) {
        connsm->conn_handle = i;
        STAILQ_INSERT_TAIL(&g_ble_ll_conn_free_list, connsm, free_stqe);

        /* Initialize empty pdu */
        m = (struct os_mbuf *)&connsm->conn_empty_pdu;
        m->om_data = (uint8_t *)&connsm->conn_empty_pdu[0];
        m->om_data += BLE_MBUF_PKT_OVERHEAD;
        m->om_pkthdr_len = sizeof(struct ble_mbuf_hdr) + 
            sizeof(struct os_mbuf_pkthdr);
        m->om_len = 2;
        OS_MBUF_PKTHDR(m)->omp_len = 2;
        m->om_data[0] = BLE_LL_LLID_DATA_FRAG;
        m->om_omp = NULL;
        m->om_flags = 0;
        ++connsm;
    }

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
}

