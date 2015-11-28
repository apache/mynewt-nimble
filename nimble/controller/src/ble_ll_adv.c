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
#include "os/os.h"
#include "bsp/bsp.h"
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "controller/ble_phy.h"
#include "controller/ble_ll.h"
#include "controller/ble_ll_adv.h"
#include "controller/ble_ll_sched.h"
#include "controller/ble_ll_scan.h"
#include "controller/ble_ll_conn.h"
#include "controller/ble_ll_whitelist.h"
#include "hal/hal_cputime.h"
#include "hal/hal_gpio.h"

/* 
 * Advertising configuration parameters. These are parameters that I have
 * fixed for now but could be considered "configuration" parameters for either
 * the device or the stack.
 */
#define BLE_LL_CFG_ADV_PDU_ITVL_HD_USECS    (5000)  /* usecs */
#define BLE_LL_CFG_ADV_PDU_ITVL_LD_USECS    (10000) /* usecs */
#define BLE_LL_CFG_ADV_TXPWR                (0)     /* dBm */

/* XXX: TODO
 * 1) Need to look at advertising and scan request PDUs. Do I allocate these
 * once? Do I use a different pool for smaller ones? Do I statically declare
 * them?
 * 2) The random address and initiator address (if doing directed adv) is not
 * set yet. Determine how that is to be done.
 * 3) How do features get supported? What happens if device does not support
 * advertising? (for example)
 * 4) Correct calculation of schedule start and end times for the various
 * scheduled advertising activities.
 * 5) How to determine the advertising interval we will actually use. As of
 * now, we set it to max.
 * 6) Currently, when we set scheduling events, we dont take into account
 * processor overhead/delays. We will want to do that.
 * 8) For set adv enable command: if we get a connection, or we time out,
 *    we need to send a CONNECTION COMPLETE event. Do this.
 * 9) How does the advertising channel tx power get set? I dont implement
 * that currently.
 * 10) Deal with whitelisting at LL when pdu is handed up to LL task.
 * 11) What about High duty cycle advertising? Do we exit the advertising state
 * after 1.28 seconds automatically? Seems like we do!!! 4.4.2.4.2 Vol 6 Part B
 * => YES! we have to implement this and set back a CONN COMPLETE event with
 *    error code DIRECTED ADV TIMEOUT.
 * 12) Something to consider: if we are attempting to advertise but dont have
 * any connection state machines available, I dont think we should enable
 * advertising.
 * 13) Implement high duty cycle advertising timeout.
 */

/* 
 * Advertising state machine 
 * 
 * The advertising state machine data structure.
 * 
 *  adv_pdu_len
 *      The length of the advertising PDU that will be sent. This does not
 *      include the preamble, access address and CRC.
 */
struct ble_ll_adv_sm
{
    uint8_t enabled;
    uint8_t adv_type;
    uint8_t adv_len;
    uint8_t adv_chanmask;
    uint8_t adv_filter_policy;
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint8_t adv_chan;
    uint8_t scan_rsp_len;
    uint8_t adv_pdu_len;
    uint16_t adv_itvl_min;
    uint16_t adv_itvl_max;
    uint32_t adv_itvl_usecs;
    uint32_t adv_event_start_time;
    uint32_t adv_pdu_start_time;
    uint8_t initiator_addr[BLE_DEV_ADDR_LEN];
    uint8_t adv_data[BLE_ADV_DATA_MAX_LEN];
    uint8_t scan_rsp_data[BLE_SCAN_RSP_DATA_MAX_LEN];
    struct os_mbuf *adv_pdu;
    struct os_mbuf *scan_rsp_pdu;
    struct os_event adv_txdone_ev;
};

/* The advertising state machine global object */
struct ble_ll_adv_sm g_ble_ll_adv_sm;

struct ble_ll_adv_stats
{
    uint32_t late_tx_done;
    uint32_t cant_set_sched;
    uint32_t scan_rsp_txg;
    uint32_t adv_txg;
};

struct ble_ll_adv_stats g_ble_ll_adv_stats;

/* XXX: We can calculate scan response time as well. */
/* 
 * Worst case time needed for scheduled advertising item. This is the longest
 * possible time to receive a scan request and send a scan response (with the
 * appropriate IFS time between them). This number is calculated using the
 * following formula: IFS + SCAN_REQ + IFS + SCAN_RSP = 150 + 176 + 150 + 376.
 * 
 * NOTE: The advertising PDU transmit time is NOT included here since we know
 * how long that will take.
 */
#define BLE_LL_ADV_SCHED_MAX_USECS  (852)

/* For debug purposes */
extern void bletest_inc_adv_pkt_num(void);

/**
 * Calculate the first channel that we should advertise upon when we start 
 * an advertising event. 
 * 
 * @param advsm 
 * 
 * @return uint8_t The number of the first channel usable for advertising.
 */
static uint8_t
ble_ll_adv_first_chan(struct ble_ll_adv_sm *advsm)
{
    uint8_t adv_chan;

    /* Set first advertising channel */
    if (advsm->adv_chanmask & 0x01) {
        adv_chan = BLE_PHY_ADV_CHAN_START;
    } else if (advsm->adv_chanmask & 0x02) {
        adv_chan = BLE_PHY_ADV_CHAN_START + 1;
    } else {
        adv_chan = BLE_PHY_ADV_CHAN_START + 2;
    }

    return adv_chan;
}


/**
 * Compares the advertiser address in an advertising PDU with our
 * address to see if there is a match
 * 
 * @param rxbuf Pointer to received PDU
 * 
 * @return int Returns memcmp return values (0 = match).
 */
static int
ble_ll_adv_addr_cmp(uint8_t *rxbuf)
{
    int rc;
    uint8_t *adva;
    uint8_t *our_addr;
    uint8_t rxaddr_type;

    /* Determine if this is addressed to us */
    rxaddr_type = rxbuf[0] & BLE_ADV_PDU_HDR_RXADD_MASK;
    if (rxaddr_type) {
        our_addr = g_random_addr;
    } else {
        our_addr = g_dev_addr;
    }

    adva = rxbuf + BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN;
    rc = memcmp(our_addr, adva, BLE_DEV_ADDR_LEN);

    return rc;
}

/**
 * Create the advertising PDU 
 * 
 * @param advsm Pointer to advertisement state machine
 */
static void
ble_ll_adv_pdu_make(struct ble_ll_adv_sm *advsm)
{
    uint8_t     adv_data_len;
    uint8_t     *dptr;
    uint8_t     pdulen;
    uint8_t     pdu_type;
    uint8_t     *addr;
    struct os_mbuf *m;

    /* assume this is not a direct ind */
    adv_data_len = advsm->adv_len;
    pdulen = BLE_DEV_ADDR_LEN + adv_data_len;

    /* Must be an advertising type! */
    switch (advsm->adv_type) {
    case BLE_HCI_ADV_TYPE_ADV_IND:
        pdu_type = BLE_ADV_PDU_TYPE_ADV_IND;
        break;

    case BLE_HCI_ADV_TYPE_ADV_NONCONN_IND:
        pdu_type = BLE_ADV_PDU_TYPE_ADV_NONCONN_IND;
        break;

    case BLE_HCI_ADV_TYPE_ADV_SCAN_IND:
        pdu_type = BLE_ADV_PDU_TYPE_ADV_SCAN_IND;
        break;

    case BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD:
    case BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_LD:
        pdu_type = BLE_ADV_PDU_TYPE_ADV_DIRECT_IND;
        adv_data_len = 0;
        pdulen = BLE_ADV_DIRECT_IND_LEN;
        break;

        /* Set these to avoid compiler warnings */
    default:
        pdulen = 0;
        pdu_type = 0;
        adv_data_len = 0xFF;
        break;
    }

    /* An invalid advertising data length indicates a memory overwrite */
    assert(adv_data_len <= BLE_ADV_DATA_MAX_LEN);

    /* Set the PDU length in the state machine */
    advsm->adv_pdu_len = pdulen + BLE_LL_PDU_HDR_LEN;

    /* Get the advertising PDU */
    m = advsm->adv_pdu;
    assert(m != NULL);
    m->om_len = advsm->adv_pdu_len;
    OS_MBUF_PKTHDR(m)->omp_len = m->om_len;

    /* Construct scan response */
    if (advsm->own_addr_type == BLE_HCI_ADV_OWN_ADDR_PUBLIC) {
        addr = g_dev_addr;
    } else if (advsm->own_addr_type == BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        pdu_type |= BLE_ADV_PDU_HDR_TXADD_RAND;
        addr = g_random_addr;
    } else {
        /* XXX: unsupported for now  */
        addr = NULL;
        assert(0);
    }

    /* Construct advertisement */
    dptr = m->om_data;
    dptr[0] = pdu_type;
    dptr[1] = (uint8_t)pdulen;
    memcpy(dptr + BLE_LL_PDU_HDR_LEN, addr, BLE_DEV_ADDR_LEN);
    dptr += BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN;

    /* For ADV_DIRECT_IND, we need to put initiators address in there */
    if (pdu_type == BLE_ADV_PDU_TYPE_ADV_DIRECT_IND) {
        memcpy(dptr, advsm->initiator_addr, BLE_DEV_ADDR_LEN);
    }

    /* Copy in advertising data, if any */
    if (adv_data_len != 0) {
        memcpy(dptr, advsm->adv_data, adv_data_len);
    }
}

/**
 * Create a scan response PDU 
 * 
 * @param advsm 
 */
static void
ble_ll_adv_scan_rsp_pdu_make(struct ble_ll_adv_sm *advsm)
{
    uint8_t     scan_rsp_len;
    uint8_t     *dptr;
    uint8_t     *addr;
    uint8_t     pdulen;
    uint8_t     hdr;
    struct os_mbuf *m;

    /* Make sure that the length is valid */
    scan_rsp_len = advsm->scan_rsp_len;
    assert(scan_rsp_len <= BLE_SCAN_RSP_DATA_MAX_LEN);

    /* Set PDU payload length */
    pdulen = BLE_DEV_ADDR_LEN + scan_rsp_len;

    /* Get the advertising PDU */
    m = advsm->scan_rsp_pdu;
    assert(m != NULL);
    m->om_len = BLE_LL_PDU_HDR_LEN + pdulen;
    OS_MBUF_PKTHDR(m)->omp_len = m->om_len;

    /* Construct scan response */
    if (advsm->own_addr_type == BLE_HCI_ADV_OWN_ADDR_PUBLIC) {
        hdr = BLE_ADV_PDU_TYPE_SCAN_RSP;
        addr = g_dev_addr;
    } else if (advsm->own_addr_type == BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        hdr = BLE_ADV_PDU_TYPE_SCAN_RSP | BLE_ADV_PDU_HDR_TXADD_RAND;
        addr = g_random_addr;
    } else {
        /* XXX: unsupported for now  */
        hdr = 0;
        addr = NULL;
        assert(0);
    }

    /* Write into the pdu buffer */
    dptr = m->om_data;
    dptr[0] = hdr;
    dptr[1] = pdulen;
    memcpy(dptr + BLE_LL_PDU_HDR_LEN, addr, BLE_DEV_ADDR_LEN);

    /* Copy in scan response data, if any */
    if (scan_rsp_len != 0) {
        dptr += BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN;  
        memcpy(dptr, advsm->scan_rsp_data, scan_rsp_len);
    }
}

/**
 * Scheduler callback used after advertising PDU sent. 
 * 
 * @param arg 
 */
static int
ble_ll_adv_rx_cb(struct ble_ll_sched_item *sch)
{
    /* Disable the PHY as we might be receiving */
    ble_phy_disable();
    os_eventq_put(&g_ble_ll_data.ll_evq, &g_ble_ll_adv_sm.adv_txdone_ev);
    return BLE_LL_SCHED_STATE_DONE;
}

/**
 * Scheduler callback when an advertising PDU has been sent. 
 * 
 * @param arg 
 */
static int
ble_ll_adv_tx_done_cb(struct ble_ll_sched_item *sch)
{
    os_eventq_put(&g_ble_ll_data.ll_evq, &g_ble_ll_adv_sm.adv_txdone_ev);
    return BLE_LL_SCHED_STATE_DONE;
}

/**
 * This is the scheduler callback (called from interrupt context) which 
 * transmits an advertisement. 
 *  
 * Context: Interrupt (scheduler) 
 *  
 * @param sch 
 * 
 * @return int 
 */
static int
ble_ll_adv_tx_start_cb(struct ble_ll_sched_item *sch)
{
    int rc;
    uint8_t end_trans;
    struct ble_ll_adv_sm *advsm;

    /* Get the state machine for the event */
    advsm = (struct ble_ll_adv_sm *)sch->cb_arg;

    /* Toggle the LED */
    gpio_toggle(LED_BLINK_PIN);

    /* Set channel */
    rc = ble_phy_setchan(advsm->adv_chan, 0, 0);
    assert(rc == 0);

    /* Set phy mode based on type of advertisement */
    if (advsm->adv_type == BLE_HCI_ADV_TYPE_ADV_NONCONN_IND) {
        end_trans = BLE_PHY_TRANSITION_NONE;
    } else {
        end_trans = BLE_PHY_TRANSITION_TX_RX;
    }

    /* Transmit advertisement */
    rc = ble_phy_tx(advsm->adv_pdu, BLE_PHY_TRANSITION_NONE, end_trans);
    if (rc) {
        /* Transmit failed. */
        rc = ble_ll_adv_tx_done_cb(sch);
    } else {
        /* Set link layer state to advertising */
        ble_ll_state_set(BLE_LL_STATE_ADV);

        /* Count # of adv. sent */
        ++g_ble_ll_adv_stats.adv_txg;

        /* Set schedule item next wakeup time */
        if (advsm->adv_type == BLE_HCI_ADV_TYPE_ADV_NONCONN_IND) {
            sch->next_wakeup = sch->end_time;
            sch->sched_cb = ble_ll_adv_tx_done_cb;
        } else {
            /* XXX: set next wakeup time. We have to look for either a
             * connect request or scan request or both, depending on
             * advertising type. For now, we will just wait until the
             * end of the scheduled event, which was set to the worst case
             * time to send a scan response PDU. Note that I use the time
             * now to insure the callback occurs after we are dont transmitting
             * the scan response, as we may have been late starting the tx.
             */
            sch->next_wakeup = cputime_get32() +
                (sch->end_time - sch->start_time);
            sch->sched_cb = ble_ll_adv_rx_cb;
        }

        rc = BLE_LL_SCHED_STATE_RUNNING;
    }

    return rc;
}

static struct ble_ll_sched_item *
ble_ll_adv_sched_set(struct ble_ll_adv_sm *advsm)
{
    int rc;
    uint32_t max_usecs;
    struct ble_ll_sched_item *sch;

    sch = ble_ll_sched_get_item();
    if (sch) {
        /* Set sched type */
        sch->sched_type = BLE_LL_SCHED_TYPE_ADV;

        /* XXX: HW output compare to trigger tx start? Look into this */
        /* Set the start time of the event */
        sch->start_time = advsm->adv_pdu_start_time - 
            cputime_usecs_to_ticks(XCVR_TX_SCHED_DELAY_USECS);

        /* Set the callback and argument */
        sch->cb_arg = advsm;
        sch->sched_cb = ble_ll_adv_tx_start_cb;

        /* Set end time to maximum time this schedule item may take */
        max_usecs = ble_ll_pdu_tx_time_get(advsm->adv_pdu_len);
        if (advsm->adv_type != BLE_HCI_ADV_TYPE_ADV_NONCONN_IND) {
            max_usecs += BLE_LL_ADV_SCHED_MAX_USECS;
        }
        sch->end_time = advsm->adv_pdu_start_time +
            cputime_usecs_to_ticks(max_usecs);

        /* XXX: for now, we cant get an overlap so assert on error. */
        /* Add the item to the scheduler */
        rc = ble_ll_sched_add(sch);
        assert(rc == 0);
    } else {
        ++g_ble_ll_adv_stats.cant_set_sched;
    }

    return sch;
}

/**
 * Called by the HCI command parser when a set advertising parameters command 
 * has been received. 
 *  
 * Context: Link Layer task (HCI command parser) 
 * 
 * @param cmd 
 * 
 * @return int 
 */
int
ble_ll_adv_set_adv_params(uint8_t *cmd)
{
    uint8_t adv_type;
    uint8_t adv_filter_policy;
    uint8_t adv_chanmask;
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint16_t adv_itvl_min;
    uint16_t adv_itvl_max;
    uint16_t min_itvl;
    struct ble_ll_adv_sm *advsm;

    /* If already enabled, we return an error */
    advsm = &g_ble_ll_adv_sm;
    if (advsm->enabled) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* Make sure intervals are OK (along with advertising type */
    adv_itvl_min = le16toh(cmd);
    adv_itvl_max = le16toh(cmd + 2);
    adv_type = cmd[4];

    /* Min has to be less than max */
    if (adv_itvl_min >= adv_itvl_max) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    switch (adv_type) {
    case BLE_HCI_ADV_TYPE_ADV_IND:
    case BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD:
    case BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_LD:
        min_itvl = BLE_LL_ADV_ITVL_MIN;
        break;
    case BLE_HCI_ADV_TYPE_ADV_NONCONN_IND:
    case BLE_HCI_ADV_TYPE_ADV_SCAN_IND:
        min_itvl = BLE_LL_ADV_ITVL_NONCONN_MIN;
        break;
    default:
        min_itvl = 0xFFFF;
        break;
    }

    /* Make sure interval minimum is valid for the advertising type */
    if ((adv_itvl_min < min_itvl) || (adv_itvl_min > BLE_HCI_ADV_ITVL_MAX)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check own and peer address type */
    own_addr_type =  cmd[5];
    peer_addr_type = cmd[6];

    if ((own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) ||
        (peer_addr_type > BLE_HCI_ADV_PEER_ADDR_MAX)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* There are only three adv channels, so check for any outside the range */
    adv_chanmask = cmd[13];
    if (((adv_chanmask & 0xF8) != 0) || (adv_chanmask == 0)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check for valid filter policy */
    adv_filter_policy = cmd[14];
    if (adv_filter_policy > BLE_HCI_ADV_FILT_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* XXX: determine if there is anything that needs to be done for
       own address type or peer address type */
    advsm->own_addr_type = own_addr_type;
    advsm->peer_addr_type = peer_addr_type;

    advsm->adv_filter_policy = adv_filter_policy;
    advsm->adv_chanmask = adv_chanmask;
    advsm->adv_itvl_min = adv_itvl_min;
    advsm->adv_itvl_max = adv_itvl_max;
    advsm->adv_type = adv_type;

    return 0;
}

/**
 * Stop advertising state machine 
 * 
 * @param advsm 
 */
static void
ble_ll_adv_sm_stop(struct ble_ll_adv_sm *advsm)
{
    /* XXX: Stop any timers we may have started */

    /* Disable whitelisting (just in case) */
    ble_ll_whitelist_disable();

    /* Remove any scheduled advertising items */
    ble_ll_sched_rmv(BLE_LL_SCHED_TYPE_ADV, NULL);

    /* Disable advertising */
    advsm->enabled = 0;
}

/**
 * Start the advertising state machine. This is called when the host sends 
 * the "enable advertising" command and is not called again while in the 
 * advertising state. 
 *  
 * Context: Link-layer task. 
 * 
 * @param advsm Pointer to advertising state machine
 * 
 * @return int 
 */
static int
ble_ll_adv_sm_start(struct ble_ll_adv_sm *advsm)
{
    uint8_t adv_chan;
    struct ble_ll_sched_item *sch;

    /* 
     * XXX: not sure if I should do this or just report whatever random
     * address the host sent. For now, I will reject the command with a
     * command disallowed error. All the parameter errors refer to the command
     * parameter (which in this case is just enable or disable).
     */ 
    if (advsm->own_addr_type != BLE_HCI_ADV_OWN_ADDR_PUBLIC) {
        if (!ble_ll_is_valid_random_addr(g_random_addr)) {
            return BLE_ERR_CMD_DISALLOWED;
        }

        /* XXX: support these other types */
        if (advsm->own_addr_type != BLE_HCI_ADV_OWN_ADDR_RANDOM) {
            assert(0);
        }
    }

    /* Set flag telling us that advertising is enabled */
    advsm->enabled = 1;

    /* Determine the advertising interval we will use */
    advsm->adv_itvl_usecs = (uint32_t)advsm->adv_itvl_max * BLE_LL_ADV_ITVL;

    /* Create the advertising PDU */
    ble_ll_adv_pdu_make(advsm);

    /* Create scan response PDU (if needed) */
    if (advsm->adv_type != BLE_HCI_ADV_TYPE_ADV_NONCONN_IND) {
        ble_ll_adv_scan_rsp_pdu_make(advsm);
    }

    /* Set first advertising channel */
    adv_chan = ble_ll_adv_first_chan(advsm);
    advsm->adv_chan = adv_chan;

    /* Enable/disable whitelisting based on filter policy */
    if (advsm->adv_filter_policy != BLE_HCI_ADV_FILT_NONE) {
        ble_ll_whitelist_enable();
    } else {
        ble_ll_whitelist_disable();
    }

    /* 
     * Set start time for the advertising event. This time is the same
     * as the time we will send the first PDU. Since there does not seem
     * to be any requirements as to when we start, we just set the time to
     * now.
     */ 
    advsm->adv_event_start_time = cputime_get32();
    advsm->adv_pdu_start_time = advsm->adv_event_start_time;

    /* Set packet in schedule */
    sch = ble_ll_adv_sched_set(advsm);
    if (!sch) {
        /* XXX: set a wakeup timer to deal with this. For now, assert */
        assert(0);
    }

    return 0;
}

/**
 * Called when the LE HCI command read advertising channel tx power command 
 * has been received. Returns the current advertising transmit power. 
 *  
 * Context: Link Layer task (HCI command parser) 
 * 
 * @return int 
 */
int
ble_ll_adv_read_txpwr(uint8_t *rspbuf, uint8_t *rsplen)
{
    rspbuf[0] = BLE_LL_CFG_ADV_TXPWR;
    *rsplen = 1;
    return BLE_ERR_SUCCESS;
}

/**
 * Turn advertising on/off.
 * 
 * @param cmd 
 * 
 * @return int 
 */
int
ble_ll_adv_set_enable(uint8_t *cmd)
{
    int rc;
    uint8_t enable;
    struct ble_ll_adv_sm *advsm;

    advsm = &g_ble_ll_adv_sm;

    rc = 0;
    enable = cmd[0];
    if (enable == 1) {
        /* If already enabled, do nothing */
        if (!advsm->enabled) {
            /* Start the advertising state machine */
            rc = ble_ll_adv_sm_start(advsm);
        }
    } else if (enable == 0) {
        if (advsm->enabled) {
            ble_ll_adv_sm_stop(advsm);
        }
    } else {
        rc = BLE_ERR_INV_HCI_CMD_PARMS;
    }

    return rc;
}

/**
 * Set the scan response data that the controller will send.
 * 
 * @param cmd 
 * @param len 
 * 
 * @return int 
 */
int
ble_ll_adv_set_scan_rsp_data(uint8_t *cmd, uint8_t len)
{
    uint8_t datalen;
    os_sr_t sr;
    struct ble_ll_adv_sm *advsm;

    /* Check for valid scan response data length */
    datalen = cmd[0];
    if ((datalen > BLE_SCAN_RSP_DATA_MAX_LEN) || (datalen != len)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Copy the new data into the advertising structure. */
    advsm = &g_ble_ll_adv_sm;
    advsm->scan_rsp_len = datalen;
    memcpy(advsm->scan_rsp_data, cmd + 1, datalen);

    /* Re-make the scan response PDU since data may have changed */
    OS_ENTER_CRITICAL(sr);
    /* 
     * XXX: there is a chance, even with interrupts disabled, that
     * we are transmitting the scan response PDU while writing to it.
     */ 
    ble_ll_adv_scan_rsp_pdu_make(advsm);
    OS_EXIT_CRITICAL(sr);

    return 0;
}

/**
 * Called by the LL HCI command parser when a set advertising 
 * data command has been sent from the host to the controller. 
 * 
 * @param cmd Pointer to command data
 * @param len Length of command data
 * 
 * @return int 0: success; BLE_ERR_INV_HCI_CMD_PARMS otherwise.
 */
int
ble_ll_adv_set_adv_data(uint8_t *cmd, uint8_t len)
{
    uint8_t datalen;
    struct ble_ll_adv_sm *advsm;
    os_sr_t sr;

    /* Check for valid advertising data length */
    datalen = cmd[0];
    if ((datalen > BLE_ADV_DATA_MAX_LEN) || (datalen != len)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Copy the new data into the advertising structure. */
    advsm = &g_ble_ll_adv_sm;
    advsm->adv_len = datalen;
    memcpy(advsm->adv_data, cmd + 1, datalen);

    /* If the state machine is enabled, we need to re-make the adv PDU */
    if (advsm->enabled) {
        /* 
         * XXX: currently, even with interrupts disabled, there is a chance
         * that we are transmitting the advertising PDU while writing into
         * it.
         */
        OS_ENTER_CRITICAL(sr);
        ble_ll_adv_pdu_make(advsm);
        OS_EXIT_CRITICAL(sr);
    }

    return 0;
}

/**
 * Called when the LL receives a scan request or connection request
 *  
 * Context: Called from interrupt context. 
 * 
 * @param rxbuf 
 * 
 * @return -1: request not for us or is a connect request. 
 *          0: request (scan) is for us and we successfully went from rx to tx.
 *        > 0: PHY error attempting to go from rx to tx.
 */
static int
ble_ll_adv_rx_req(uint8_t pdu_type, struct os_mbuf *rxpdu)
{
    int rc;
    uint8_t chk_whitelist;
    uint8_t txadd;
    uint8_t *rxbuf;
    struct ble_mbuf_hdr *ble_hdr;
    struct ble_ll_adv_sm *advsm;

    rxbuf = rxpdu->om_data;
    if (ble_ll_adv_addr_cmp(rxbuf)) {
        return -1;
    }

    /* Set device match bit if we are whitelisting */
    advsm = &g_ble_ll_adv_sm;
    if (pdu_type == BLE_ADV_PDU_TYPE_SCAN_REQ) {
        chk_whitelist = advsm->adv_filter_policy & 1;
    } else {
        chk_whitelist = advsm->adv_filter_policy & 2;
    }

    /* Set device match bit if we are whitelisting */
    if (chk_whitelist) {
        /* Get the scanners address type */
        if (rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK) {
            txadd = BLE_ADDR_TYPE_RANDOM;
        } else {
            txadd = BLE_ADDR_TYPE_PUBLIC;
        }

        /* Check for whitelist match */
        if (!ble_ll_whitelist_match(rxbuf + BLE_LL_PDU_HDR_LEN, txadd)) {
            return -1;
        } else {
            ble_hdr = BLE_MBUF_HDR_PTR(rxpdu);
            ble_hdr->flags |= BLE_MBUF_HDR_F_DEVMATCH;
        }
    }

    /* Setup to transmit the scan response if appropriate */
    rc = -1;
    if (pdu_type == BLE_ADV_PDU_TYPE_SCAN_REQ) {
        rc = ble_phy_tx(advsm->scan_rsp_pdu, BLE_PHY_TRANSITION_RX_TX, 
                        BLE_PHY_TRANSITION_NONE);
        if (!rc) {
            ++g_ble_ll_adv_stats.scan_rsp_txg;
        }
    }

    return rc;
}

/**
 * Called when a connect request has been received. 
 *  
 * Context: Link Layer 
 * 
 * @param rxbuf 
 * @param flags 
 */
void
ble_ll_adv_conn_req_rxd(uint8_t *rxbuf, uint8_t flags)
{
    int valid;
    struct ble_ll_adv_sm *advsm;

    /* Check filter policy. */
    valid = 0;
    advsm = &g_ble_ll_adv_sm;
    if (advsm->adv_filter_policy & 2) {
        if (flags & BLE_MBUF_HDR_F_DEVMATCH) {
            /* valid connection request received */
            valid = 1;
        }
    } else {
        /* If this is for us? */
        if (!ble_ll_adv_addr_cmp(rxbuf)) {
            valid = 1;
        }
    }

    if (valid) {
        /* Stop advertising and start connection */
        ble_ll_adv_sm_stop(advsm);
        ble_ll_conn_slave_start(rxbuf);
    }
}

/**
 * Called on phy rx pdu end when in advertising state. 
 *  
 * There are only two pdu types we care about in this state: scan requests 
 * and connection requests. When we receive a scan request we must determine if 
 * we need to send a scan response and that needs to be acted on within T_IFS.
 *  
 * When we receive a connection request, we need to determine if we will allow 
 * this device to start a connection with us. However, no immediate response is 
 * sent so we handle this at the link layer task. 
 * 
 * Context: Interrupt 
 *  
 * @param pdu_type Type of pdu received.
 * @param rxpdu Pointer to received PDU
 *  
 * @return int 
 *       < 0: Disable the phy after reception.
 *      == 0: Do not disable the PHY
 *       > 0: Do not disable PHY as that has already been done.
 */
int
ble_ll_adv_rx_pdu_end(uint8_t pdu_type, struct os_mbuf *rxpdu)
{
    int rc;

    rc = -1;
    if ((pdu_type == BLE_ADV_PDU_TYPE_SCAN_REQ) ||
        (pdu_type == BLE_ADV_PDU_TYPE_CONNECT_REQ)) {
        /* Process request */
        rc = ble_ll_adv_rx_req(pdu_type, rxpdu);
        if (rc) {
            /* XXX: One thing left to reconcile here. We have
             * the advertisement schedule element still running.
             * How to deal with the end of the advertising event?
             * Need to figure that out.
             * 
             * XXX: This applies to connection requests as well.
             */
        }
    }

    return rc;
}

/**
 * Process advertistement tx done event. 
 *  
 * Context: Link Layer task. 
 * 
 * @param arg Pointer to advertising state machine.
 */
void
ble_ll_adv_tx_done_proc(void *arg)
{
    uint8_t mask;
    uint8_t final_adv_chan;
    int32_t delta_t;
    uint32_t itvl;
    struct ble_ll_adv_sm *advsm;

    /* Free the advertising packet */
    advsm = (struct ble_ll_adv_sm *)arg;
    ble_ll_state_set(BLE_LL_STATE_STANDBY);

    /* For debug purposes */
    bletest_inc_adv_pkt_num();

    /* 
     * Check if we have ended our advertising event. If our last advertising
     * packet was sent on the last channel, it means we are done with this
     * event.
     */
    if (advsm->adv_chanmask & 0x04) {
        final_adv_chan = BLE_PHY_ADV_CHAN_START + 2;
    } else if (advsm->adv_chanmask & 0x02) {
        final_adv_chan = BLE_PHY_ADV_CHAN_START + 1;
    } else {
        final_adv_chan = BLE_PHY_ADV_CHAN_START;
    }

    if (advsm->adv_chan == final_adv_chan) {
        /* This event is over. Set adv channel to first one */
        advsm->adv_chan = ble_ll_adv_first_chan(advsm);

        /* Calculate start time of next advertising event */
        itvl = advsm->adv_itvl_usecs;
        itvl += rand() % (BLE_LL_ADV_DELAY_MS_MAX * 1000);
        advsm->adv_event_start_time += cputime_usecs_to_ticks(itvl);
        advsm->adv_pdu_start_time = advsm->adv_event_start_time;

        /* Toggle the LED */
        gpio_toggle(LED_BLINK_PIN);
    } else {
        /* 
         * Move to next advertising channel. If not in the mask, just
         * increment by 1. We can do this because we already checked if we
         * just transmitted on the last advertising channel
         */
        ++advsm->adv_chan;
        mask = 1 << (advsm->adv_chan - BLE_PHY_ADV_CHAN_START);
        if ((mask & advsm->adv_chanmask) == 0) {
            ++advsm->adv_chan;
        }

        /* Set next start time to next pdu transmit time */
        if (advsm->adv_type == BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD) {
            itvl = BLE_LL_CFG_ADV_PDU_ITVL_HD_USECS;
        } else {
            itvl = BLE_LL_CFG_ADV_PDU_ITVL_LD_USECS;
        }
        itvl = cputime_usecs_to_ticks(itvl);
        advsm->adv_pdu_start_time += itvl;
    }

    /* 
     * The scheduled time better be in the future! If it is not, we will
     * count a statistic and close the current advertising event. We will
     * then setup the next advertising event.
     */
    delta_t = (int32_t)(advsm->adv_pdu_start_time - cputime_get32());
    if (delta_t < 0) {
        /* Count times we were late */
        ++g_ble_ll_adv_stats.late_tx_done;

        /* Set back to first adv channel */
        advsm->adv_chan = ble_ll_adv_first_chan(advsm);

        /* Calculate start time of next advertising event */
        while (delta_t < 0) {
            itvl = advsm->adv_itvl_usecs;
            itvl += rand() % (BLE_LL_ADV_DELAY_MS_MAX * 1000);
            itvl = cputime_usecs_to_ticks(itvl);
            advsm->adv_event_start_time += itvl;
            advsm->adv_pdu_start_time = advsm->adv_event_start_time;
            delta_t += (int32_t)itvl;
        }
    }

    if (!ble_ll_adv_sched_set(advsm)) {
        /* XXX: we will need to set a timer here to wake us up */
        assert(0);
    }
}

/**
 * Checks if the controller can change the whitelist. If advertising is enabled 
 * and is using the whitelist the controller is not allowed to change the 
 * whitelist. 
 * 
 * @return int 0: not allowed to change whitelist; 1: change allowed.
 */
int
ble_ll_adv_can_chg_whitelist(void)
{
    int rc;
    struct ble_ll_adv_sm *advsm;

    advsm = &g_ble_ll_adv_sm;
    if (advsm->enabled && (advsm->adv_filter_policy != BLE_HCI_ADV_FILT_NONE)) {
        rc = 0;
    } else {
        rc = 1;
    }

    return rc;
}

/**
 * Initialize the advertising functionality of a BLE device. This should 
 * be called once on initialization
 */
void
ble_ll_adv_init(void)
{
    struct ble_ll_adv_sm *advsm;

    /* Set default advertising parameters */
    advsm = &g_ble_ll_adv_sm;
    memset(advsm, 0, sizeof(struct ble_ll_adv_sm));

    advsm->adv_itvl_min = BLE_HCI_ADV_ITVL_DEF;
    advsm->adv_itvl_max = BLE_HCI_ADV_ITVL_DEF;
    advsm->adv_chanmask = BLE_HCI_ADV_CHANMASK_DEF;

    /* Initialize advertising tx done event */
    advsm->adv_txdone_ev.ev_type = BLE_LL_EVENT_ADV_TXDONE;
    advsm->adv_txdone_ev.ev_arg = advsm;

    /* Get an advertising mbuf (packet header) and attach to state machine */
    advsm->adv_pdu = os_mbuf_get_pkthdr(&g_mbuf_pool);
    assert(advsm->adv_pdu != NULL);

    /* Get a scan response mbuf (packet header) and attach to state machine */
    advsm->scan_rsp_pdu = os_mbuf_get_pkthdr(&g_mbuf_pool);
    assert(advsm->scan_rsp_pdu != NULL);
}

