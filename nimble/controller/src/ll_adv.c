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
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "controller/phy.h"
#include "controller/ll.h"
#include "controller/ll_adv.h"
#include "controller/ll_sched.h"
#include "controller/ll_scan.h"
#include "hal/hal_cputime.h"

/* 
 * Advertising configuration parameters. These are parameters that I have
 * fixed for now but could be considered "configuration" parameters for either
 * the device or the stack.
 */
#define BLE_LL_CFG_ADV_PDU_ITVL_HD_USECS    (5000)
#define BLE_LL_CFG_ADV_PDU_ITVL_LD_USECS    (10000)

/* XXX: need to think about advertising and scan response pdus. Do I get
 * these from their own pools? Shouldnt I just always allocate one and thus
 * dont need to free them or worry about not being able to allocate one?
 * I want them to be smaller mbufs and not have to be allocated from the
 * pool
 */ 

/* XXX: TODO
 * 1) Process the stop advertising event. Make sure you stop advertising and
 * all that.
 * 2) The random address and initiator address (if doing directed adv) is not
 * set yet. Determine how that is to be done.
 * 3) How do features get supported? What happens if device does not support
 * advertising? (for example)
 * 4) Correct calculation of schedule start and end times for the various
 * scheduled advertising activities.
 * 5) How to determine the advertising interval we will actually use.
 * 6) Currently, when we set scheduling events, we dont take into account
 * processor overhead/delays. We will want to do that.
 * 7) Use a static buffer as opposed to a mbuf.
 * 8) Need to implement the whole HCI command done stuff.
 */

/* Advertising state machine */
struct ll_adv_sm
{
    uint8_t enabled;
    uint8_t adv_params_set;
    uint8_t adv_type;
    uint8_t adv_len;
    uint8_t adv_chanmask;
    uint8_t adv_filter_policy;
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint8_t adv_chan;
    uint8_t scan_rsp_len;
    uint16_t adv_itvl_min;
    uint16_t adv_itvl_max;
    uint32_t adv_itvl_usecs;
    uint32_t adv_event_start_time;
    uint32_t adv_pdu_start_time;
    uint8_t adv_addr[BLE_DEV_ADDR_LEN];
    uint8_t random_addr[BLE_DEV_ADDR_LEN];
    uint8_t initiator_addr[BLE_DEV_ADDR_LEN];
    uint8_t adv_data[BLE_ADV_DATA_MAX_LEN];
    uint8_t scan_rsp_data[BLE_SCAN_RSP_DATA_MAX_LEN];
    struct os_mbuf *adv_pdu;
    struct os_mbuf *scan_rsp_pdu;
};

/* The advertising state machine global object */
struct ll_adv_sm g_ll_adv_sm;

struct ll_adv_stats
{
    uint32_t late_tx_done;
    uint32_t cant_set_sched;
};

struct ll_adv_stats g_ll_adv_stats;

/* XXX: for now, just use a global adv tx done event */
struct os_event g_ll_adv_tx_done_ev;

/* 
 * Worst case time to end an advertising event. This is the longest possible
 * time it takes to send an advertisement, receive a scan request and send
 * a scan response (with the appropriate IFS time between them). This number is
 * calculated using the following formula:
 *  ADV_PDU + IFS + SCAN_REQ + IFS + SCAN_RSP = 376 + 150 + 176 + 150 + 376
 */
#define BLE_LL_ADV_EVENT_MAX_USECS      (1228)

/**
 * ble ll adv first chan 
 *  
 * Calculate the first channel that we should advertise upon when we start 
 * an advertising event. 
 * 
 * @param advsm 
 * 
 * @return uint8_t 
 */
static uint8_t
ll_adv_first_chan(struct ll_adv_sm *advsm)
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
 * ble ll adv tx done cb 
 *  
 * Scheduler callback when an advertising PDU has been sent. 
 * 
 * @param arg 
 */
static int
ll_adv_tx_done_cb(struct ll_sched_item *sch)
{
    /* XXX: when we get here, we should not be done. We need
     * to check to see if we are getting a receive. If so, extend
     * wait. For now, just set to done.
     */
    ble_phy_disable();
    os_eventq_put(&g_ll_data.ll_evq, &g_ll_adv_tx_done_ev);
    return BLE_LL_SCHED_STATE_DONE;
}

/**
 * ll adv tx start cb
 *  
 * This is the scheduler callback (called from interrupt context) which 
 * transmits an advertisement. 
 * 
 * @param sch 
 * 
 * @return int 
 */
static int
ll_adv_tx_start_cb(struct ll_sched_item *sch)
{
    int rc;
    uint8_t phy_mode;
    struct ll_adv_sm *advsm;

    /* Get the state machine for the event */
    advsm = (struct ll_adv_sm *)sch->cb_cookie;

    /* Set channel */
    rc = ble_phy_setchan(advsm->adv_chan);
    assert(rc == 0);

    /* Get phy mode */
    if (advsm->adv_type == BLE_ADV_TYPE_ADV_NONCONN_IND) {
        phy_mode = BLE_PHY_MODE_TX;
    } else {
        phy_mode = BLE_PHY_MODE_TX_RX;
    }

    /* XXX: we should not really assert on error here! */
    /* Transmit advertisement */
    rc = ble_phy_tx(advsm->adv_pdu, phy_mode);
    assert(rc == 0);

    /* Set link layer state */
    g_ll_data.ll_state = BLE_LL_STATE_ADV;

    /* XXX: need to prepare for a receive after the tx */

    /* 
     * XXX: The next wakeup time for this event is really
     *  next_wake = start_time + txtime + IFS + jitter +
     *              time_to_detect_rx_pkt
     * 
     *  For now, we will cheat and make the next wake time the end
     *  time.
     */
    sch->next_wakeup = sch->end_time;
    sch->sched_cb = ll_adv_tx_done_cb;

    return BLE_LL_SCHED_STATE_RUNNING;
}

static int
ll_adv_pdu_make(struct ll_adv_sm *advsm)
{
    uint8_t     adv_data_len;
    uint8_t     *dptr;
    uint8_t     pdulen;
    uint8_t     pdu_type;
    struct os_mbuf *m;

    /* assume this is not a direct ind */
    adv_data_len = advsm->adv_len;
    pdulen = BLE_DEV_ADDR_LEN + adv_data_len;

    /* Must be an advertising type! */
    switch (advsm->adv_type) {
    case BLE_ADV_TYPE_ADV_IND:
        pdu_type = BLE_ADV_PDU_TYPE_ADV_IND;
        break;

    case BLE_ADV_TYPE_ADV_NONCONN_IND:
        pdu_type = BLE_ADV_PDU_TYPE_ADV_NONCONN_IND;
        break;

    case BLE_ADV_TYPE_ADV_SCAN_IND:
        pdu_type = BLE_ADV_PDU_TYPE_ADV_SCAN_IND;
        break;

    case BLE_ADV_TYPE_ADV_DIRECT_IND_HD:
    case BLE_ADV_TYPE_ADV_DIRECT_IND_LD:
        pdu_type = BLE_ADV_PDU_TYPE_ADV_DIRECT_IND;
        adv_data_len = 0;
        pdulen = BLE_ADV_DIRECT_IND_LEN;
        break;

        /* Put an invalid data length here so we leave */
    default:
        pdulen = 0;
        adv_data_len = 0xFF;
        break;
    }

    /* Make sure that the length is valid */
    if (adv_data_len > BLE_ADV_DATA_MAX_LEN) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Get the advertising PDU */
    m = advsm->adv_pdu;
    assert(m != NULL);
    m->om_len = BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN + adv_data_len;

    /* Construct advertisement */
    dptr = m->om_data;
    dptr[0] = pdu_type;
    dptr[1] = (uint8_t)pdulen;
    memcpy(dptr + BLE_LL_PDU_HDR_LEN, advsm->adv_addr, BLE_DEV_ADDR_LEN);
    dptr += BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN;

    /* For ADV_DIRECT_IND, we need to put initiators address in there */
    if (pdu_type == BLE_ADV_PDU_TYPE_ADV_DIRECT_IND) {
        memcpy(dptr, advsm->initiator_addr, BLE_DEV_ADDR_LEN);
        m->om_len = BLE_DEV_ADDR_LEN;
    }

    /* Copy in advertising data, if any */
    if (adv_data_len != 0) {
        memcpy(dptr, advsm->adv_data, adv_data_len);
    }

    /* Set overall packet header length */
    OS_MBUF_PKTHDR(m)->omp_len = m->om_len;

    return 0;
}

static int
ll_adv_scan_rsp_pdu_make(struct ll_adv_sm *advsm)
{
    uint8_t     scan_rsp_len;
    uint8_t     *dptr;
    uint8_t     pdulen;
    struct os_mbuf *m;

    /* assume this is not a direct ind */
    scan_rsp_len = advsm->scan_rsp_len;
    pdulen = BLE_DEV_ADDR_LEN + scan_rsp_len;

    /* Make sure that the length is valid */
    if (scan_rsp_len > BLE_SCAN_RSP_DATA_MAX_LEN) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Get the advertising PDU */
    m = advsm->scan_rsp_pdu;
    assert(m != NULL);
    m->om_len = BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN + scan_rsp_len;

    /* Construct scan response */
    dptr = m->om_data;
    dptr[0] = BLE_ADV_PDU_TYPE_SCAN_RSP;
    dptr[1] = (uint8_t)pdulen;
    memcpy(dptr + BLE_LL_PDU_HDR_LEN, advsm->adv_addr, BLE_DEV_ADDR_LEN);
    dptr += BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN;

    /* Copy in scan response data, if any */
    if (scan_rsp_len != 0) {
        memcpy(dptr, advsm->scan_rsp_data, scan_rsp_len);
    }

    /* Set overall packet header length */
    OS_MBUF_PKTHDR(m)->omp_len = m->om_len;

    return 0;
}

static struct ll_sched_item *
ll_adv_sched_set(struct ll_adv_sm *advsm)
{
    int rc;
    struct ll_sched_item *sch;

    /* Set packet in schedule */
    sch = ll_sched_get_item();
    if (sch) {
        /* Set sched type */
        sch->sched_type = BLE_LL_SCHED_TYPE_ADV;

        /* Set the start time of the event */
        sch->start_time = advsm->adv_pdu_start_time;

        /* We dont care about start of advertising event, only end */
        sch->cb_cookie = advsm;
        sch->sched_cb = ll_adv_tx_start_cb;

        /* XXX: NONCONN should be a different time (not the worst-case)
           and we dont account for start delay */
        /* Set end time to worst-case time to send the advertisement,  */
        sch->end_time = sch->start_time + 
            cputime_usecs_to_ticks(BLE_LL_ADV_EVENT_MAX_USECS);

        /* XXX: for now, we cant get an overlap so assert on error. */
        /* Add the item to the scheduler */
        rc = ll_sched_add(sch);
        assert(rc == 0);
    } else {
        ++g_ll_adv_stats.cant_set_sched;
    }

    return sch;
}

/*  Called from HCI command parser */
/* XXX: I dont know how to deal with public or random addresses here */
int
ll_adv_set_adv_params(uint8_t *cmd)
{
    uint8_t adv_type;
    uint8_t adv_filter_policy;
    uint8_t adv_chanmask;
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint16_t adv_itvl_min;
    uint16_t adv_itvl_max;
    uint16_t min_itvl;
    struct ll_adv_sm *advsm;

    /* If already enabled, we return an error */
    advsm = &g_ll_adv_sm;
    if (advsm->enabled) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* We no longer have valid advertising parameters */
    advsm->adv_params_set = 0;

    /* Make sure intervals are OK (along with advertising type */
    adv_itvl_min = le16toh(cmd);
    adv_itvl_max = le16toh(cmd + 2);
    adv_type = cmd[4];

    /* Min has to be less than max and cannot equal max */
    if ((adv_itvl_min > adv_itvl_max) || (adv_itvl_min == adv_itvl_max)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    switch (adv_type) {
    case BLE_ADV_TYPE_ADV_IND:
    case BLE_ADV_TYPE_ADV_DIRECT_IND_HD:
    case BLE_ADV_TYPE_ADV_DIRECT_IND_LD:
        min_itvl = BLE_LL_ADV_ITVL_MIN;
        break;
    case BLE_ADV_TYPE_ADV_NONCONN_IND:
    case BLE_ADV_TYPE_ADV_SCAN_IND:
        min_itvl = BLE_LL_ADV_ITVL_NONCONN_MIN;
        break;
    default:
        min_itvl = 0xFFFF;
        break;
    }

    /* Make sure interval minimum is valid for the advertising type */
    if ((adv_itvl_min < min_itvl) || (adv_itvl_min > BLE_LL_ADV_ITVL_MAX)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }
    advsm->adv_itvl_min = adv_itvl_min;
    advsm->adv_itvl_max = adv_itvl_max;
    advsm->adv_type = adv_type;

    /* Check own and peer address type */
    own_addr_type =  cmd[5];
    peer_addr_type = cmd[6];

    if ((own_addr_type > BLE_ADV_OWN_ADDR_MAX) ||
        (peer_addr_type > BLE_ADV_PEER_ADDR_MAX)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    advsm->own_addr_type = own_addr_type;
    advsm->peer_addr_type = peer_addr_type;

    /* XXX: We have to deal with own address type and peer address type.
       For now, we just use public device address */
    if (own_addr_type == BLE_ADV_OWN_ADDR_PUBLIC) {
        memcpy(advsm->adv_addr, g_dev_addr, BLE_DEV_ADDR_LEN);
    }

    /* There are only three adv channels, so check for any outside the range */
    adv_chanmask = cmd[13];
    if (((adv_chanmask & 0xF8) != 0) || (adv_chanmask == 0)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }
    advsm->adv_chanmask = adv_chanmask;

    /* Check for valid filter policy */
    adv_filter_policy = cmd[14];
    if (adv_filter_policy > BLE_ADV_FILT_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }
    advsm->adv_filter_policy = adv_filter_policy;

    /* Set this to denote that we have valid advertising parameters */
    advsm->adv_params_set = 1;

    return 0;
}

/* Stop the advertising state machine */
static void
ll_adv_sm_stop(struct ll_adv_sm *advsm)
{
    /* XXX: Stop any timers */

    /* Remove any scheduled advertising items */
    ll_sched_rmv(BLE_LL_SCHED_TYPE_ADV);

    /* Disable advertising */
    advsm->enabled = 0;

    /* If a scan response pdu is attached, free it */
    if (g_ll_adv_sm.scan_rsp_pdu) {
        os_mbuf_free(&g_mbuf_pool, g_ll_adv_sm.scan_rsp_pdu);
        g_ll_adv_sm.scan_rsp_pdu = NULL;
    }
}

/* Start the advertising state machine */
static int
ll_adv_sm_start(struct ll_adv_sm *advsm)
{
    int rc;
    uint8_t adv_chan;
    struct ll_sched_item *sch;
    struct os_mbuf *m;

    /* Set flag telling us that advertising is enabled */
    advsm->enabled = 1;

    /* XXX: not sure how to really do this. For now, we will use max */
    /* Determine the advertising interval we will use */
    advsm->adv_itvl_usecs = (uint32_t)advsm->adv_itvl_max * BLE_LL_ADV_ITVL;

    /* XXX: for now, just set the start time to now. Does not matter if we
     * are late for this though we may want to set it a bit in the future.
     * What about processor delay? Need to know this I think.
     */

    /* 
     * Set start time for the advertising event. This time is the same
     * as the time we will send the first PDU
     */ 
    advsm->adv_event_start_time = cputime_get32();
    advsm->adv_pdu_start_time = advsm->adv_event_start_time;

    /* Create the advertising PDU */
    rc = ll_adv_pdu_make(advsm);
    assert(rc == 0);

    /* Create scan response PDU (if needed) */
    if ((advsm->adv_type != BLE_ADV_TYPE_ADV_NONCONN_IND) &&
        (advsm->scan_rsp_len != 0)) {
        m = os_mbuf_get_pkthdr(&g_mbuf_pool);

        /* XXX: should not need to do this once we get the get packet header
           mbuf thingy going */
        if (!m) {
            return BLE_ERR_MEM_CAPACITY;
        }
        g_ll_adv_sm.scan_rsp_pdu = m;

        rc = ll_adv_scan_rsp_pdu_make(advsm);
        assert(rc == 0);
    }

    /* Set first advertising channel */
    adv_chan = ll_adv_first_chan(advsm);
    advsm->adv_chan = adv_chan;

    /* Set packet in schedule */
    sch = ll_adv_sched_set(advsm);
    if (!sch) {
        /* XXX: set a wakeup timer to deal with this. For now, assert */
        assert(0);
    }

    return rc;
}

int
ll_adv_set_enable(uint8_t *cmd)
{
    int rc;
    uint8_t enable;
    struct ll_adv_sm *advsm;

    advsm = &g_ll_adv_sm;

    rc = 0;
    enable = cmd[0];
    if (enable == 1) {
        /* If already enabled, do nothing */
        if (!advsm->enabled) {
            if (advsm->adv_params_set) {
                /* Start the advertising state machine */
                rc = ll_adv_sm_start(advsm);
            } else {
                rc = BLE_ERR_CMD_DISALLOWED;
            }
        }
    } else if (enable == 0) {
        if (advsm->enabled) {
            ll_adv_sm_stop(advsm);
        }
    } else {
        rc = BLE_ERR_INV_HCI_CMD_PARMS;
    }

    return rc;
}

/* XXX: how do I protect the scan response data? When I create the scan
   response PDU, how do I make sure this task is not half-written? */
/* XXX: where do I create the scan response pdu? */
int
ll_adv_set_scan_rsp_data(uint8_t *cmd, uint8_t len)
{
    uint8_t datalen;
    struct ll_adv_sm *advsm;

    /* Check for valid scan response data length */
    datalen = cmd[0];
    if ((datalen > BLE_SCAN_RSP_DATA_MAX_LEN) || (datalen != len)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Copy the new data into the advertising structure. */
    advsm = &g_ll_adv_sm;
    advsm->scan_rsp_len = datalen;
    memcpy(advsm->scan_rsp_data, cmd + 1, datalen);

    /* XXX: if we are enabled and have a scan rsp pdu, we need to rebuild
     * it. I guess we could do this every time within the adv. state
     * machine
     */

    return 0;
}

/**
 * ble ll adv set adv data 
 *  
 * Called by the LL HCI command parser when a set advertising 
 * data command has been sent from the host to the controller. 
 * 
 * @param cmd Pointer to command data
 * @param len Length of command data
 * 
 * @return int 0: success; BLE_ERR_INV_HCI_CMD_PARMS otherwise.
 */
int
ll_adv_set_adv_data(uint8_t *cmd, uint8_t len)
{
    uint8_t datalen;
    struct ll_adv_sm *advsm;
    struct ll_sched_item *sch;

    /* Check for valid advertising data length */
    datalen = cmd[0];
    if ((datalen > BLE_ADV_DATA_MAX_LEN) || (datalen != len)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Copy the new data into the advertising structure. */
    advsm = &g_ll_adv_sm;
    advsm->adv_len = datalen;
    memcpy(advsm->adv_data, cmd + 1, datalen);

    /* XXX: should I just make the pdu when I go to transmit it? */
    /* 
     * If the state machine is enabled and we are going to send a
     * PDU soon, just send the current one. Otherwise, delete the
     * scheduled event so we can re-make the adv PDU.
     */
    if (advsm->enabled && 
        (advsm->adv_type != BLE_ADV_TYPE_ADV_DIRECT_IND_HD) &&
        (advsm->adv_type != BLE_ADV_TYPE_ADV_DIRECT_IND_LD) &&
        ((int32_t)(advsm->adv_pdu_start_time - cputime_get32()) >
                   cputime_usecs_to_ticks(1000))) {
         /* Remove any scheduled events */
         ll_sched_rmv(BLE_LL_SCHED_TYPE_ADV);

         /* Remake the advertising PDU */
         ll_adv_pdu_make(advsm);

         /* Schedule the transmission */
         sch = ll_adv_sched_set(advsm);
         if (!sch) {
             /* XXX: set a wakeup timer to deal with this. For now, assert */
             assert(0);
         }
    }

    return 0;
}

/**
 * ble ll adv set rand addr 
 *  
 * Called from the HCI command parser when the set random address command 
 * is received. 
 * 
 * @param addr Pointer to address
 * 
 * @return int 0: success
 */
int
ll_adv_set_rand_addr(uint8_t *addr)
{
    memcpy(g_ll_adv_sm.random_addr, addr, BLE_DEV_ADDR_LEN);
    return 0;
}

/**
 * ll adv rx scan req 
 *  
 * Called when the LL receives a scan request.  
 *  
 * NOTE: Called from interrupt context. 
 * 
 * @param rxbuf 
 * 
 * @return -1: scan request not for us or failed to start tx; 0 otherwise
 */
int
ll_adv_rx_scan_req(uint8_t *rxbuf)
{
    int rc;
    uint8_t *our_addr;

    rc = -1;
    our_addr = rxbuf + BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN;
    if (!memcmp(our_addr, g_dev_addr, BLE_DEV_ADDR_LEN)) {
        /* Setup to transmit the scan response */
        rc = ble_phy_tx(g_ll_adv_sm.scan_rsp_pdu, BLE_PHY_MODE_TX);

        /* XXX: do this at higher layer? */
        /* Count received scan requests */
        ++g_ll_stats.rx_scan_reqs;
    }

    return rc;
}

/**
 * ll adv tx done proc
 *  
 * Process advertistement tx done event. 
 *  
 * Callers: LL task. 
 * 
 * @param arg Pointer to advertising state machine.
 */
void
ll_adv_tx_done_proc(void *arg)
{
    uint8_t mask;
    uint8_t final_adv_chan;
    uint32_t itvl;
    struct ll_adv_sm *advsm;

    /* XXX: What about processor delay when starting an event? Not sure how
       to deal with this. For now, I will assume it is zero */

    /* Free the advertising packet */
    advsm = (struct ll_adv_sm *)arg;

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
        advsm->adv_chan = ll_adv_first_chan(advsm);

        /* Calculate start time of next advertising event */
        itvl = advsm->adv_itvl_usecs;
        itvl += rand() % (BLE_LL_ADV_DELAY_MS_MAX * 1000);
        advsm->adv_event_start_time += cputime_usecs_to_ticks(itvl);
        advsm->adv_pdu_start_time = advsm->adv_event_start_time;
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
        if (advsm->adv_type == BLE_ADV_TYPE_ADV_DIRECT_IND_HD) {
            itvl = BLE_LL_CFG_ADV_PDU_ITVL_HD_USECS;
        } else {
            itvl = BLE_LL_CFG_ADV_PDU_ITVL_LD_USECS;
        }
        itvl = cputime_usecs_to_ticks(itvl);
        advsm->adv_pdu_start_time += itvl;
    }

    /* XXX: I dont like how this is done (for both adv pdu's and
     * scan response pdu's).
     *  -> One problem is that I might be using the PDU for transmission,
     *  which is why I cannot overwrite it.
     */
    /* Re-make the advertising PDU since data may have changed */
    ll_adv_pdu_make(advsm);

    /* Re-make the scan response PDU since data may have changed */
    if (advsm->scan_rsp_pdu) {
        ll_adv_scan_rsp_pdu_make(advsm);
    }
    /* XXX */

    /* 
     * The scheduled time better be in the future! If it is not, we will
     * count a statistic and close the current advertising event. We will
     * then setup the next advertising event.
     */
    itvl = cputime_get32();
    if ((int32_t)(itvl - advsm->adv_pdu_start_time) >= 0) {
        /* Count times we were late */
        ++g_ll_adv_stats.late_tx_done;

        /* Set back to first adv channel */
        advsm->adv_chan = ll_adv_first_chan(advsm);

        /* Calculate start time of next advertising event */
        itvl = advsm->adv_itvl_usecs;
        itvl += rand() % (BLE_LL_ADV_DELAY_MS_MAX * 1000);
        advsm->adv_event_start_time += cputime_usecs_to_ticks(itvl);
        advsm->adv_pdu_start_time = advsm->adv_event_start_time;
    }

    if (!ll_adv_sched_set(advsm)) {
        /* XXX: we will need to set a timer here to wake us up */
        assert(0);
    }
}

/**
 * ll adv init 
 *  
 * Initialize the advertising functionality of a BLE device. This should 
 * be called once on initialization
 */
void
ll_adv_init(void)
{
    /* Initialize advertising tx done event */
    g_ll_adv_tx_done_ev.ev_type = BLE_LL_EVENT_ADV_TXDONE;
    g_ll_adv_tx_done_ev.ev_arg = &g_ll_adv_sm;

    /* Get an advertising mbuf (packet header) and attach to state machine */
    g_ll_adv_sm.adv_pdu = os_mbuf_get_pkthdr(&g_mbuf_pool);
    assert(g_ll_adv_sm.adv_pdu != NULL);
}

