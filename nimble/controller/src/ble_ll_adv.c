/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "syscfg/syscfg.h"
#include "os/os.h"
#include "os/os_cputime.h"
#include "bsp/bsp.h"
#include "ble/xcvr.h"
#include "nimble/ble.h"
#include "nimble/nimble_opt.h"
#include "nimble/hci_common.h"
#include "nimble/hci_vendor.h"
#include "nimble/ble_hci_trans.h"
#include "controller/ble_phy.h"
#include "controller/ble_hw.h"
#include "controller/ble_ll.h"
#include "controller/ble_ll_hci.h"
#include "controller/ble_ll_adv.h"
#include "controller/ble_ll_sched.h"
#include "controller/ble_ll_scan.h"
#include "controller/ble_ll_whitelist.h"
#include "controller/ble_ll_resolv.h"
#include "ble_ll_conn_priv.h"
#include "hal/hal_gpio.h"

/* XXX: TODO
 * 1) Need to look at advertising and scan request PDUs. Do I allocate these
 * once? Do I use a different pool for smaller ones? Do I statically declare
 * them?
 * 3) How do features get supported? What happens if device does not support
 * advertising? (for example)
 * 4) How to determine the advertising interval we will actually use. As of
 * now, we set it to max.
 * 5) How does the advertising channel tx power get set? I dont implement
 * that currently.
 */

/*
 * Advertising state machine
 *
 * The advertising state machine data structure.
 *
 *  adv_pdu_len
 *      The length of the advertising PDU that will be sent. This does not
 *      include the preamble, access address and CRC.
 *
 *  initiator_addr:
 *      This is the address that we send in directed advertisements (the
 *      INITA field). If we are using Privacy this is a RPA that we need to
 *      generate. We reserve space in the advsm to save time when creating
 *      the ADV_DIRECT_IND. If own address type is not 2 or 3, this is simply
 *      the peer address from the set advertising parameters.
 */
struct ble_ll_adv_sm
{
    uint8_t adv_enabled;
    uint8_t adv_instance;
    uint8_t adv_type;
    uint8_t adv_len;
    uint8_t adv_chanmask;
    uint8_t adv_filter_policy;
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint8_t adv_chan;
    uint8_t scan_rsp_len;
    uint8_t adv_pdu_len;
    int8_t adv_rpa_index;
    uint8_t adv_directed;           /* note: can be 1 bit */
    uint8_t adv_txadd;              /* note: can be 1 bit */
    uint8_t adv_rxadd;              /* note: can be 1 bit */
    int8_t adv_txpwr;
    uint16_t adv_itvl_min;
    uint16_t adv_itvl_max;
    uint32_t adv_itvl_usecs;
    uint32_t adv_event_start_time;
    uint32_t adv_pdu_start_time;
    uint32_t adv_dir_hd_end_time;
    uint32_t adv_rpa_timer;
    uint8_t adva[BLE_DEV_ADDR_LEN];
    uint8_t adv_rpa[BLE_DEV_ADDR_LEN];
    uint8_t peer_addr[BLE_DEV_ADDR_LEN];
    uint8_t initiator_addr[BLE_DEV_ADDR_LEN];
    uint8_t adv_data[BLE_ADV_DATA_MAX_LEN];
    uint8_t scan_rsp_data[BLE_SCAN_RSP_DATA_MAX_LEN];
    uint8_t *conn_comp_ev;
    struct os_event adv_txdone_ev;
    struct ble_ll_sched_item adv_sch;
#if MYNEWT_VAL(BLE_MULTI_ADV_SUPPORT)
    uint8_t adv_random_addr[BLE_DEV_ADDR_LEN];
#endif
};

/* The advertising state machine global object */
struct ble_ll_adv_sm g_ble_ll_adv_sm[BLE_LL_ADV_INSTANCES];
struct ble_ll_adv_sm *g_ble_ll_cur_adv_sm;

static void ble_ll_adv_done(struct ble_ll_adv_sm *advsm);

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
/**
 * Called to change advertisers ADVA and INITA (for directed advertisements)
 * as an advertiser needs to adhere to the resolvable private address generation
 * timer.
 *
 * NOTE: the resolvable private address code uses its own timer to regenerate
 * local resolvable private addresses. The advertising code uses its own
 * timer to reset the INITA (for directed advertisements). This code also sets
 * the appropriate txadd and rxadd bits that will go into the advertisement.
 *
 * Another thing to note: it is possible that an IRK is all zeroes in the
 * resolving list. That is why we need to check if the generated address is
 * in fact a RPA as a resolving list entry with all zeroes will use the
 * identity address (which may be a private address or public).
 *
 * @param advsm
 */
void
ble_ll_adv_chk_rpa_timeout(struct ble_ll_adv_sm *advsm)
{
    uint32_t now;

    if (advsm->own_addr_type > BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        now = os_time_get();
        if ((int32_t)(now - advsm->adv_rpa_timer) >= 0) {
            ble_ll_resolv_gen_rpa(advsm->peer_addr, advsm->peer_addr_type,
                                  advsm->adva, 1);

            if (advsm->adv_directed) {
                ble_ll_resolv_gen_rpa(advsm->peer_addr, advsm->peer_addr_type,
                                      advsm->initiator_addr, 0);
                if (ble_ll_is_rpa(advsm->initiator_addr, 1)) {
                    advsm->adv_rxadd = 1;
                } else {
                    if (advsm->own_addr_type & 1) {
                        advsm->adv_rxadd = 1;
                    } else {
                        advsm->adv_rxadd = 0;
                    }
                }
            }
            advsm->adv_rpa_timer = now + ble_ll_resolv_get_rpa_tmo();

            /* May have to reset txadd bit */
            if (ble_ll_is_rpa(advsm->adva, 1)) {
                advsm->adv_txadd = 1;
            } else {
                if (advsm->own_addr_type & 1) {
                    advsm->adv_txadd = 1;
                } else {
                    advsm->adv_txadd = 0;
                }
            }
        }
    }
}
#endif

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
 * Calculate the final channel that we should advertise upon when we start
 * an advertising event.
 *
 * @param advsm
 *
 * @return uint8_t The number of the final channel usable for advertising.
 */
static uint8_t
ble_ll_adv_final_chan(struct ble_ll_adv_sm *advsm)
{
    uint8_t adv_chan;

    if (advsm->adv_chanmask & 0x04) {
        adv_chan = BLE_PHY_ADV_CHAN_START + 2;
    } else if (advsm->adv_chanmask & 0x02) {
        adv_chan = BLE_PHY_ADV_CHAN_START + 1;
    } else {
        adv_chan = BLE_PHY_ADV_CHAN_START;
    }

    return adv_chan;
}

/**
 * Create the advertising PDU
 *
 * @param advsm Pointer to advertisement state machine
 */
static void
ble_ll_adv_pdu_make(struct ble_ll_adv_sm *advsm, struct os_mbuf *m)
{
    uint8_t     adv_data_len;
    uint8_t     *dptr;
    uint8_t     pdulen;
    uint8_t     pdu_type;

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
        if (advsm->adv_rxadd) {
            pdu_type |= BLE_ADV_PDU_HDR_RXADD_RAND;
        }
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

    /* Set the PDU length in the state machine (includes header) */
    advsm->adv_pdu_len = pdulen + BLE_LL_PDU_HDR_LEN;

    /* Set TxAdd to random if needed. */
    if (advsm->adv_txadd) {
        pdu_type |= BLE_ADV_PDU_HDR_TXADD_RAND;
    }

    /* Get the advertising PDU and initialize it*/
    ble_ll_mbuf_init(m, pdulen, pdu_type);

    /* Construct advertisement */
    dptr = m->om_data;
    memcpy(dptr, advsm->adva, BLE_DEV_ADDR_LEN);
    dptr += BLE_DEV_ADDR_LEN;

    /* For ADV_DIRECT_IND add inita */
    if (advsm->adv_directed) {
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
static struct os_mbuf *
ble_ll_adv_scan_rsp_pdu_make(struct ble_ll_adv_sm *advsm)
{
    uint8_t     scan_rsp_len;
    uint8_t     *dptr;
    uint8_t     pdulen;
    uint8_t     hdr;
    struct os_mbuf *m;

    /* Obtain scan response buffer */
    m = os_msys_get_pkthdr(BLE_SCAN_RSP_DATA_MAX_LEN + BLE_DEV_ADDR_LEN,
                           sizeof(struct ble_mbuf_hdr));
    if (!m) {
        return NULL;
    }

    /* Make sure that the length is valid */
    scan_rsp_len = advsm->scan_rsp_len;
    assert(scan_rsp_len <= BLE_SCAN_RSP_DATA_MAX_LEN);

    /* Set BLE transmit header */
    pdulen = BLE_DEV_ADDR_LEN + scan_rsp_len;
    hdr = BLE_ADV_PDU_TYPE_SCAN_RSP;
    if (advsm->adv_txadd) {
        hdr |= BLE_ADV_PDU_HDR_TXADD_RAND;
    }

    ble_ll_mbuf_init(m, pdulen, hdr);

    /*
     * XXX: Am I sure this is correct? The adva in this packet will be the
     * same one that was being advertised and is based on the peer identity
     * address in the set advertising parameters. If a different peer sends
     * us a scan request (for some reason) we will reply with an adva that
     * was not generated based on the local irk of the peer sending the scan
     * request.
     */

    /* Construct scan response */
    dptr = m->om_data;
    memcpy(dptr, advsm->adva, BLE_DEV_ADDR_LEN);
    if (scan_rsp_len != 0) {
        memcpy(dptr + BLE_DEV_ADDR_LEN, advsm->scan_rsp_data, scan_rsp_len);
    }

    return m;
}

/**
 * Called to indicate the advertising event is over.
 *
 * Context: Interrupt
 *
 * @param advsm
 *
 */
static void
ble_ll_adv_tx_done(void *arg)
{
    struct ble_ll_adv_sm *advsm;

    /* XXX: for now, reset power to max after advertising */
    ble_phy_txpwr_set(MYNEWT_VAL(BLE_LL_TX_PWR_DBM));

    advsm = (struct ble_ll_adv_sm *)arg;
    os_eventq_put(&g_ble_ll_data.ll_evq, &advsm->adv_txdone_ev);

    ble_ll_log(BLE_LL_LOG_ID_ADV_TXDONE, ble_ll_state_get(),
               advsm->adv_instance, 0);

    ble_ll_state_set(BLE_LL_STATE_STANDBY);

    /* We no longer have a current state machine */
    g_ble_ll_cur_adv_sm = NULL;
}

/*
 * Called when an advertising event has been removed from the scheduler
 * without being run.
 */
void
ble_ll_adv_event_rmvd_from_sched(struct ble_ll_adv_sm *advsm)
{
    /*
     * Need to set advertising channel to final chan so new event gets
     * scheduled.
     */
    advsm->adv_chan = ble_ll_adv_final_chan(advsm);
    os_eventq_put(&g_ble_ll_data.ll_evq, &advsm->adv_txdone_ev);
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
    uint32_t txstart;
    struct ble_ll_adv_sm *advsm;
    struct os_mbuf *adv_pdu;

    /* Get the state machine for the event */
    advsm = (struct ble_ll_adv_sm *)sch->cb_arg;

    /* Set the current advertiser */
    g_ble_ll_cur_adv_sm = advsm;

    /* Set the power */
    ble_phy_txpwr_set(advsm->adv_txpwr);

    /* Set channel */
    rc = ble_phy_setchan(advsm->adv_chan, 0, 0);
    assert(rc == 0);

    /* Set transmit start time. */
    txstart = sch->start_time + os_cputime_usecs_to_ticks(XCVR_PROC_DELAY_USECS);
    rc = ble_phy_tx_set_start_time(txstart);
    if (rc) {
        STATS_INC(ble_ll_stats, adv_late_starts);
        goto adv_tx_done;
    }

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_ENCRYPTION) == 1)
    /* XXX: automatically do this in the phy based on channel? */
    ble_phy_encrypt_disable();
#endif

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    advsm->adv_rpa_index = -1;
    if (ble_ll_resolv_enabled()) {
        ble_phy_resolv_list_enable();
    } else {
        ble_phy_resolv_list_disable();
    }
#endif

    /* Set phy mode based on type of advertisement */
    if (advsm->adv_type == BLE_HCI_ADV_TYPE_ADV_NONCONN_IND) {
        end_trans = BLE_PHY_TRANSITION_NONE;
        ble_phy_set_txend_cb(ble_ll_adv_tx_done, advsm);
    } else {
        end_trans = BLE_PHY_TRANSITION_TX_RX;
        ble_phy_set_txend_cb(NULL, NULL);
    }

    /* Get an advertising mbuf (packet header)  */
    adv_pdu = os_msys_get_pkthdr(BLE_ADV_MAX_PKT_LEN,
                                 sizeof(struct ble_mbuf_hdr));
    if (!adv_pdu) {
        ble_phy_disable();
        goto adv_tx_done;
    }

    ble_ll_adv_pdu_make(advsm, adv_pdu);

    /* Transmit advertisement */
    rc = ble_phy_tx(adv_pdu, end_trans);
    os_mbuf_free_chain(adv_pdu);
    if (rc) {
        goto adv_tx_done;
    }

    /* Enable/disable whitelisting based on filter policy */
    if (advsm->adv_filter_policy != BLE_HCI_ADV_FILT_NONE) {
        ble_ll_whitelist_enable();
    } else {
        ble_ll_whitelist_disable();
    }

    /* Set link layer state to advertising */
    ble_ll_state_set(BLE_LL_STATE_ADV);

    /* Count # of adv. sent */
    STATS_INC(ble_ll_stats, adv_txg);

    return BLE_LL_SCHED_STATE_RUNNING;

adv_tx_done:
    ble_ll_adv_tx_done(advsm);
    return BLE_LL_SCHED_STATE_DONE;
}

static void
ble_ll_adv_set_sched(struct ble_ll_adv_sm *advsm, int sched_new)
{
    uint32_t max_usecs;
    struct ble_ll_sched_item *sch;

    sch = &advsm->adv_sch;
    sch->cb_arg = advsm;
    sch->sched_cb = ble_ll_adv_tx_start_cb;
    sch->sched_type = BLE_LL_SCHED_TYPE_ADV;

    /* Set end time to maximum time this schedule item may take */
    max_usecs = BLE_TX_DUR_USECS_M(advsm->adv_pdu_len);
    switch (advsm->adv_type) {
    case BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_LD:
    case BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD:
        max_usecs += BLE_LL_SCHED_DIRECT_ADV_MAX_USECS;
        break;
    case BLE_HCI_ADV_TYPE_ADV_IND:
    case BLE_HCI_ADV_TYPE_ADV_SCAN_IND:
        max_usecs += BLE_LL_SCHED_ADV_MAX_USECS;
        break;
    default:
        break;
    }

    /*
     * XXX: For now, just schedule some additional time so we insure we have
     * enough time to do everything we want.
     */
    max_usecs += XCVR_PROC_DELAY_USECS;

    if (sched_new) {
        /*
         * We have to add the scheduling delay and tx start delay to the max
         * time of the event since the pdu does not start at the scheduled start.
         */
        max_usecs += XCVR_TX_SCHED_DELAY_USECS;
        sch->start_time = os_cputime_get32();
        sch->end_time = sch->start_time + os_cputime_usecs_to_ticks(max_usecs);
    } else {
        sch->start_time = advsm->adv_pdu_start_time -
            os_cputime_usecs_to_ticks(XCVR_TX_SCHED_DELAY_USECS);
        sch->end_time = advsm->adv_pdu_start_time +
            os_cputime_usecs_to_ticks(max_usecs);
    }
}

/**
 * Called when advertising need to be halted. This normally should not be called
 * and is only called when a scheduled item executes but advertising is still
 * running.
 *
 * Context: Interrupt
 */
void
ble_ll_adv_halt(struct ble_ll_adv_sm *advsm)
{
    ble_ll_adv_tx_done(advsm);
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
ble_ll_adv_set_adv_params(uint8_t *cmd, uint8_t instance, int is_multi)
{
    uint8_t adv_type;
    uint8_t adv_filter_policy;
    uint8_t adv_chanmask;
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint8_t offset;
    uint16_t adv_itvl_min;
    uint16_t adv_itvl_max;
    uint16_t min_itvl;
    struct ble_ll_adv_sm *advsm;

    /* If already enabled, we return an error */
    if (instance >= BLE_LL_ADV_INSTANCES) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    advsm = &g_ble_ll_adv_sm[instance];
    if (advsm->adv_enabled) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* Add offset if this is a multi-advertising command */
    if (is_multi) {
        offset = 6;
    } else {
        offset = 0;
    }

    /* Make sure intervals are OK (along with advertising type */
    adv_itvl_min = get_le16(cmd);
    adv_itvl_max = get_le16(cmd + 2);
    adv_type = cmd[4];

    /*
     * Get the filter policy now since we will ignore it if we are doing
     * directed advertising
     */
    adv_filter_policy = cmd[14 + offset];

    /* Assume min interval based on low duty cycle/indirect advertising */
    min_itvl = BLE_LL_ADV_ITVL_MIN;

    advsm->adv_directed = 0;
    switch (adv_type) {
    /* Fall through intentional */
    case BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD:
        /* Ignore min/max interval */
        min_itvl = 0;
        adv_itvl_min = 0;
        adv_itvl_max = 0;

    case BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_LD:
        adv_filter_policy = BLE_HCI_ADV_FILT_NONE;
        advsm->adv_directed = 1;
        memcpy(advsm->peer_addr, cmd + 7, BLE_DEV_ADDR_LEN);
        break;
    case BLE_HCI_ADV_TYPE_ADV_IND:
        /* Nothing to do */
        break;
    case BLE_HCI_ADV_TYPE_ADV_NONCONN_IND:
    case BLE_HCI_ADV_TYPE_ADV_SCAN_IND:
        min_itvl = BLE_LL_ADV_ITVL_NONCONN_MIN;
        break;
    default:
        /* This will cause an invalid parameter error */
        min_itvl = 0xFFFF;
        break;
    }

    /* Make sure interval minimum is valid for the advertising type */
    if ((adv_itvl_min > adv_itvl_max) || (adv_itvl_min < min_itvl) ||
        (adv_itvl_min > BLE_HCI_ADV_ITVL_MAX) ||
        (adv_itvl_max > BLE_HCI_ADV_ITVL_MAX)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check own and peer address type */
    own_addr_type =  cmd[5];
    peer_addr_type = cmd[6 + offset];

    if ((own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) ||
        (peer_addr_type > BLE_HCI_ADV_PEER_ADDR_MAX)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Deal with multi-advertising command specific*/
#if MYNEWT_VAL(BLE_MULTI_ADV_SUPPORT)
    if (is_multi) {
        /* Get and check advertising power */
        advsm->adv_txpwr = cmd[22];
        if (advsm->adv_txpwr > 20) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        /* Get own address if it is there. */
        if (own_addr_type > BLE_HCI_ADV_OWN_ADDR_RANDOM) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        } else {
            if (own_addr_type == BLE_HCI_ADV_OWN_ADDR_RANDOM) {
                /* Use this random address if set in own address */
                if (ble_ll_is_valid_random_addr(cmd + 6)) {
                    memcpy(advsm->adv_random_addr, cmd + 6, BLE_DEV_ADDR_LEN);
                }
            }
        }
    } else {
        advsm->adv_txpwr = MYNEWT_VAL(BLE_LL_TX_PWR_DBM);
    }
#else
    advsm->adv_txpwr = MYNEWT_VAL(BLE_LL_TX_PWR_DBM);
#endif

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    if (own_addr_type > BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        /* Copy peer address */
        memcpy(advsm->peer_addr, cmd + 7 + offset, BLE_DEV_ADDR_LEN);

        /* Reset RPA timer so we generate a new RPA */
        advsm->adv_rpa_timer = os_time_get();
    }
#else
    /* If we dont support privacy some address types wont work */
    if (own_addr_type > BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        return BLE_ERR_UNSUPPORTED;
    }
#endif

    /* There are only three adv channels, so check for any outside the range */
    adv_chanmask = cmd[13 + offset];
    if (((adv_chanmask & 0xF8) != 0) || (adv_chanmask == 0)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check for valid filter policy */
    if (adv_filter_policy > BLE_HCI_ADV_FILT_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Fill out rest of advertising state machine */
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
 * Context: Link Layer task.
 *
 * @param advsm
 */
static void
ble_ll_adv_sm_stop(struct ble_ll_adv_sm *advsm)
{
    os_sr_t sr;

    if (advsm->adv_enabled) {
        /* Remove any scheduled advertising items */
        ble_ll_sched_rmv_elem(&advsm->adv_sch);

        /* Set to standby if we are no longer advertising */
        OS_ENTER_CRITICAL(sr);
#if MYNEWT_VAL(BLE_MULTI_ADV_SUPPORT)
        if (g_ble_ll_cur_adv_sm == advsm) {
            ble_phy_disable();
            ble_ll_wfr_disable();
            ble_ll_state_set(BLE_LL_STATE_STANDBY);
            g_ble_ll_cur_adv_sm = NULL;
        }
#else
        if (ble_ll_state_get() == BLE_LL_STATE_ADV) {
            ble_phy_disable();
            ble_ll_wfr_disable();
            ble_ll_state_set(BLE_LL_STATE_STANDBY);
            g_ble_ll_cur_adv_sm = NULL;
        }
#endif
        OS_EXIT_CRITICAL(sr);

        os_eventq_remove(&g_ble_ll_data.ll_evq, &advsm->adv_txdone_ev);

        /* If there is an event buf we need to free it */
        if (advsm->conn_comp_ev) {
            ble_hci_trans_buf_free(advsm->conn_comp_ev);
            advsm->conn_comp_ev = NULL;
        }

        /* Disable advertising */
        advsm->adv_enabled = 0;
    }
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
    uint8_t *addr;
    uint8_t *evbuf;

    /*
     * This is not in the specification. I will reject the command with a
     * command disallowed error if no random address has been sent by the
     * host. All the parameter errors refer to the command
     * parameter (which in this case is just enable or disable) so that
     * is why I chose command disallowed.
     */
    if (advsm->own_addr_type == BLE_HCI_ADV_OWN_ADDR_RANDOM) {
#if MYNEWT_VAL(BLE_MULTI_ADV_SUPPORT)
        if (!ble_ll_is_valid_random_addr(advsm->adv_random_addr)) {
            return BLE_ERR_CMD_DISALLOWED;
        }
#else
        if (!ble_ll_is_valid_random_addr(g_random_addr)) {
            return BLE_ERR_CMD_DISALLOWED;
        }
#endif
    }

    /*
     * Get an event with which to send the connection complete event if
     * this is connectable
     */
    switch (advsm->adv_type) {
    case BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD:
    case BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_LD:
    case BLE_HCI_ADV_TYPE_ADV_IND:
        /* We expect this to be NULL but if not we wont allocate one... */
        if (advsm->conn_comp_ev == NULL) {
            evbuf = ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_HI);
            if (!evbuf) {
                return BLE_ERR_MEM_CAPACITY;
            }
            advsm->conn_comp_ev = evbuf;
        }
        break;
    default:
        break;
    }

    /* Set advertising address */
    if ((advsm->own_addr_type & 1) == 0) {
        addr = g_dev_addr;
        advsm->adv_txadd = 0;
    } else {
#if MYNEWT_VAL(BLE_MULTI_ADV_SUPPORT)
        addr = advsm->adv_random_addr;
#else
        addr = g_random_addr;
#endif
        advsm->adv_txadd = 1;
    }
    memcpy(advsm->adva, addr, BLE_DEV_ADDR_LEN);

    if (advsm->adv_directed) {
        memcpy(advsm->initiator_addr, advsm->peer_addr, BLE_DEV_ADDR_LEN);
        if (advsm->peer_addr_type & 1) {
            advsm->adv_rxadd = 1;
        } else {
            advsm->adv_rxadd = 0;
        }
    }

    /* This will generate an RPA for both initiator addr and adva */
#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    ble_ll_adv_chk_rpa_timeout(advsm);
#endif

    /* Set flag telling us that advertising is enabled */
    advsm->adv_enabled = 1;

    /* Determine the advertising interval we will use */
    if (advsm->adv_type == BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD) {
        /* Set it to max. allowed for high duty cycle advertising */
        advsm->adv_itvl_usecs = BLE_LL_ADV_PDU_ITVL_HD_MS_MAX;
    } else {
        advsm->adv_itvl_usecs = (uint32_t)advsm->adv_itvl_max;
        advsm->adv_itvl_usecs *= BLE_LL_ADV_ITVL;
    }

    /* Set first advertising channel */
    adv_chan = ble_ll_adv_first_chan(advsm);
    advsm->adv_chan = adv_chan;

    /*
     * Schedule advertising. We set the initial schedule start and end
     * times to the earliest possible start/end.
     */
    ble_ll_adv_set_sched(advsm, 1);
    ble_ll_sched_adv_new(&advsm->adv_sch);

    return BLE_ERR_SUCCESS;
}

void
ble_ll_adv_scheduled(struct ble_ll_adv_sm *advsm, uint32_t sch_start)
{
    /* The event start time is when we start transmission of the adv PDU */
    advsm->adv_event_start_time = sch_start +
        os_cputime_usecs_to_ticks(XCVR_TX_SCHED_DELAY_USECS);

    advsm->adv_pdu_start_time = advsm->adv_event_start_time;

    /*
     * Set the time at which we must end directed, high-duty cycle advertising.
     * Does not matter that we calculate this value if we are not doing high
     * duty cycle advertising.
     */
    advsm->adv_dir_hd_end_time = advsm->adv_event_start_time +
        os_cputime_usecs_to_ticks(BLE_LL_ADV_STATE_HD_MAX * 1000);
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
    rspbuf[0] = MYNEWT_VAL(BLE_LL_TX_PWR_DBM);
    *rsplen = 1;
    return BLE_ERR_SUCCESS;
}

/**
 * Turn advertising on/off.
 *
 * Context: Link Layer task
 *
 * @param cmd
 *
 * @return int
 */
int
ble_ll_adv_set_enable(uint8_t *cmd, uint8_t instance)
{
    int rc;
    uint8_t enable;
    struct ble_ll_adv_sm *advsm;

    if (instance >= BLE_LL_ADV_INSTANCES) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    advsm = &g_ble_ll_adv_sm[instance];

    rc = BLE_ERR_SUCCESS;
    enable = cmd[0];
    if (enable == 1) {
        /* If already enabled, do nothing */
        if (!advsm->adv_enabled) {
            /* Start the advertising state machine */
            rc = ble_ll_adv_sm_start(advsm);
        }
    } else if (enable == 0) {
        ble_ll_adv_sm_stop(advsm);
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
ble_ll_adv_set_scan_rsp_data(uint8_t *cmd, uint8_t instance)
{
    uint8_t datalen;
    struct ble_ll_adv_sm *advsm;

    /* Check for valid scan response data length */
    datalen = cmd[0];
    if (datalen > BLE_SCAN_RSP_DATA_MAX_LEN) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (instance >= BLE_LL_ADV_INSTANCES) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Copy the new data into the advertising structure. */
    advsm = &g_ble_ll_adv_sm[instance];
    advsm->scan_rsp_len = datalen;
    memcpy(advsm->scan_rsp_data, cmd + 1, datalen);

    return BLE_ERR_SUCCESS;
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
ble_ll_adv_set_adv_data(uint8_t *cmd, uint8_t instance)
{
    uint8_t datalen;
    struct ble_ll_adv_sm *advsm;

    /* Check for valid advertising data length */
    datalen = cmd[0];
    if (datalen > BLE_ADV_DATA_MAX_LEN) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (instance >= BLE_LL_ADV_INSTANCES) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Copy the new data into the advertising structure. */
    advsm = &g_ble_ll_adv_sm[instance];
    advsm->adv_len = datalen;
    memcpy(advsm->adv_data, cmd + 1, datalen);

    return BLE_ERR_SUCCESS;
}

#if MYNEWT_VAL(BLE_MULTI_ADV_SUPPORT)
int
ble_ll_adv_set_random_addr(uint8_t *addr, uint8_t instance)
{
    struct ble_ll_adv_sm *advsm;

    if (instance >= BLE_LL_ADV_INSTANCES) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }
    advsm = &g_ble_ll_adv_sm[instance];
    memcpy(advsm->adv_random_addr, addr, BLE_DEV_ADDR_LEN);
    return BLE_ERR_SUCCESS;
}

/**
 * Process the multi-advertising command
 *
 * NOTE: the command length was already checked to make sure it is non-zero.
 *
 * @param cmdbuf    Pointer to command buffer
 * @param cmdlen    The length of the command data
 * @param rspbuf    Pointer to response buffer
 * @param rsplen    Pointer to response length
 *
 * @return int
 */
int
ble_ll_adv_multi_adv_cmd(uint8_t *cmdbuf, uint8_t cmdlen, uint8_t *rspbuf,
                         uint8_t *rsplen)
{
    int rc;
    uint8_t subcmd;

    /* NOTE: the command length includes the sub command byte */
    rc = BLE_ERR_INV_HCI_CMD_PARMS;
    subcmd = cmdbuf[0];
    ++cmdbuf;
    switch (subcmd) {
    case BLE_HCI_MULTI_ADV_PARAMS:
        if (cmdlen == BLE_HCI_MULTI_ADV_PARAMS_LEN) {
            rc = ble_ll_adv_set_adv_params(cmdbuf, cmdbuf[21], 1);
        }
        break;
    case BLE_HCI_MULTI_ADV_DATA:
        if (cmdlen == BLE_HCI_MULTI_ADV_DATA_LEN) {
            rc = ble_ll_adv_set_adv_data(cmdbuf, cmdbuf[32]);
        }
        break;
    case BLE_HCI_MULTI_ADV_SCAN_RSP_DATA:
        if (cmdlen == BLE_HCI_MULTI_ADV_SCAN_RSP_DATA_LEN) {
            rc = ble_ll_adv_set_scan_rsp_data(cmdbuf, cmdbuf[32]);
        }
        break;
    case BLE_HCI_MULTI_ADV_SET_RAND_ADDR:
        if (cmdlen == BLE_HCI_MULTI_ADV_SET_RAND_ADDR_LEN) {
            rc = ble_ll_adv_set_random_addr(cmdbuf, cmdbuf[6]);
        }
        break;
    case BLE_HCI_MULTI_ADV_ENABLE:
        if (cmdlen == BLE_HCI_MULTI_ADV_ENABLE_LEN) {
            rc = ble_ll_adv_set_enable(cmdbuf, cmdbuf[1]);
        }
        break;
    default:
        rc = BLE_ERR_UNKNOWN_HCI_CMD;
        break;
    }

    rspbuf[0] = subcmd;
    *rsplen = 1;

    return rc;
}
#endif

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
    int resolved;
    uint8_t chk_wl;
    uint8_t txadd;
    uint8_t peer_addr_type;
    uint8_t *rxbuf;
    uint8_t *adva;
    uint8_t *peer;
    struct ble_mbuf_hdr *ble_hdr;
    struct ble_ll_adv_sm *advsm;
    struct os_mbuf *scan_rsp;

    /* See if adva in the request (scan or connect) matches what we sent */
    advsm = g_ble_ll_cur_adv_sm;
    rxbuf = rxpdu->om_data;
    adva = rxbuf + BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN;
    if (memcmp(advsm->adva, adva, BLE_DEV_ADDR_LEN)) {
        return -1;
    }

    /* Set device match bit if we are whitelisting */
    if (pdu_type == BLE_ADV_PDU_TYPE_SCAN_REQ) {
        chk_wl = advsm->adv_filter_policy & 1;
    } else {
        chk_wl = advsm->adv_filter_policy & 2;
    }

    /* Get the peer address type */
    if (rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK) {
        txadd = BLE_ADDR_RANDOM;
    } else {
        txadd = BLE_ADDR_PUBLIC;
    }

    ble_hdr = BLE_MBUF_HDR_PTR(rxpdu);
    peer = rxbuf + BLE_LL_PDU_HDR_LEN;
    peer_addr_type = txadd;
    resolved = 0;

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    if (ble_ll_is_rpa(peer, txadd) && ble_ll_resolv_enabled()) {
        advsm->adv_rpa_index = ble_hw_resolv_list_match();
        if (advsm->adv_rpa_index >= 0) {
            ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_RESOLVED;
            if (chk_wl) {
                peer = g_ble_ll_resolv_list[advsm->adv_rpa_index].rl_identity_addr;
                peer_addr_type = g_ble_ll_resolv_list[advsm->adv_rpa_index].rl_addr_type;
                resolved = 1;
            }
        } else {
            if (chk_wl) {
                return -1;
            }
        }
    }
#endif

    /* Set device match bit if we are whitelisting */
    if (chk_wl && !ble_ll_whitelist_match(peer, peer_addr_type, resolved)) {
        return -1;
    }

    /*
     * We set the device match bit to tell the upper layer that we will
     * accept the request
     */
    ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_DEVMATCH;

    /* Setup to transmit the scan response if appropriate */
    rc = -1;
    if (pdu_type == BLE_ADV_PDU_TYPE_SCAN_REQ) {
        scan_rsp = ble_ll_adv_scan_rsp_pdu_make(advsm);
        if (scan_rsp) {
            ble_phy_set_txend_cb(ble_ll_adv_tx_done, advsm);
            rc = ble_phy_tx(scan_rsp, BLE_PHY_TRANSITION_NONE);
            if (!rc) {
                ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_SCAN_RSP_TXD;
                STATS_INC(ble_ll_stats, scan_rsp_txg);
            }
            os_mbuf_free_chain(scan_rsp);
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
 *
 * @return 0: no connection started. 1: connection started
 */
static int
ble_ll_adv_conn_req_rxd(uint8_t *rxbuf, struct ble_mbuf_hdr *hdr,
                        struct ble_ll_adv_sm *advsm)
{
    int valid;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    uint8_t resolved;
#endif
    uint8_t addr_type;
    uint8_t *inita;
    uint8_t *ident_addr;

    /* Check filter policy. */
    valid = 0;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    resolved = BLE_MBUF_HDR_RESOLVED(hdr);
#endif
    inita = rxbuf + BLE_LL_PDU_HDR_LEN;
    if (hdr->rxinfo.flags & BLE_MBUF_HDR_F_DEVMATCH) {

        valid = 1;
        if (rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK) {
            addr_type = BLE_ADDR_RANDOM;
        } else {
            addr_type = BLE_ADDR_PUBLIC;
        }

        /*
         * Only accept connect requests from the desired address if we
         * are doing directed advertising
         */
        if ((advsm->adv_type == BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD) ||
            (advsm->adv_type == BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_LD)) {
            ident_addr = inita;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
            if (resolved) {
                ident_addr = g_ble_ll_resolv_list[advsm->adv_rpa_index].rl_identity_addr;
                addr_type = g_ble_ll_resolv_list[advsm->adv_rpa_index].rl_addr_type;
            }
#endif
            if ((addr_type != advsm->peer_addr_type) ||
                memcmp(advsm->peer_addr, ident_addr, BLE_DEV_ADDR_LEN)) {
                valid = 0;
            }
        }
    }

    if (valid) {
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
        if (resolved) {
            /* Retain the resolvable private address that we received. */
            memcpy(advsm->adv_rpa, inita, BLE_DEV_ADDR_LEN);

            /*
             * Overwrite received inita with identity address since that
             * is used from now on.
             */
            memcpy(inita,
                   g_ble_ll_resolv_list[advsm->adv_rpa_index].rl_identity_addr,
                   BLE_DEV_ADDR_LEN);

            /* Peer address type is an identity address */
            addr_type = g_ble_ll_resolv_list[advsm->adv_rpa_index].rl_addr_type;
            addr_type += 2;
        }
#endif

        /* Try to start slave connection. If successful, stop advertising */
        valid = ble_ll_conn_slave_start(rxbuf, addr_type, hdr);
        if (valid) {
            ble_ll_adv_sm_stop(advsm);
        }
    }

    return valid;
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
ble_ll_adv_rx_isr_end(uint8_t pdu_type, struct os_mbuf *rxpdu, int crcok)
{
    int rc;
#if MYNEWT_VAL(BLE_MULTI_ADV_SUPPORT)
    struct ble_mbuf_hdr *rxhdr;
#endif

    rc = -1;
    if (rxpdu == NULL) {
        ble_ll_adv_tx_done(g_ble_ll_cur_adv_sm);
    } else {
#if MYNEWT_VAL(BLE_MULTI_ADV_SUPPORT)
        rxhdr = BLE_MBUF_HDR_PTR(rxpdu);
        rxhdr->rxinfo.advsm = g_ble_ll_cur_adv_sm;
#endif
        if (crcok) {
            if ((pdu_type == BLE_ADV_PDU_TYPE_SCAN_REQ) ||
                (pdu_type == BLE_ADV_PDU_TYPE_CONNECT_REQ)) {
                /* Process request */
                rc = ble_ll_adv_rx_req(pdu_type, rxpdu);
            }
        }

        if (rc) {
            /* We no longer have a current state machine */
            g_ble_ll_cur_adv_sm = NULL;
        }
    }

    if (rc) {
        ble_ll_state_set(BLE_LL_STATE_STANDBY);
    }

    return rc;
}

/**
 * Process a received packet at the link layer task when in the advertising
 * state
 *
 * Context: Link Layer
 *
 *
 * @param ptype
 * @param rxbuf
 * @param hdr
 *
 * @return int
 */
void
ble_ll_adv_rx_pkt_in(uint8_t ptype, uint8_t *rxbuf, struct ble_mbuf_hdr *hdr)
{
    int adv_event_over;
    struct ble_ll_adv_sm *advsm;

#if MYNEWT_VAL(BLE_MULTI_ADV_SUPPORT)
    advsm = (struct ble_ll_adv_sm *)hdr->rxinfo.advsm;
#else
    advsm = &g_ble_ll_adv_sm[0];
#endif

    /*
     * It is possible that advertising was stopped and a packet plcaed on the
     * LL receive packet queue. In this case, just ignore the received packet
     * as the advertising state machine is no longer "valid"
     */
    if (!advsm->adv_enabled) {
        return;
    }

    /*
     * If we have received a scan request and we are transmitting a response
     * or we have received a valid connect request, dont "end" the advertising
     * event. In the case of a connect request we will stop advertising. In
     * the case of the scan response transmission we will get a transmit
     * end callback.
     */
    adv_event_over = 1;
    if (BLE_MBUF_HDR_CRC_OK(hdr)) {
        if (ptype == BLE_ADV_PDU_TYPE_CONNECT_REQ) {
            if (ble_ll_adv_conn_req_rxd(rxbuf, hdr, advsm)) {
                adv_event_over = 0;
            }
        } else {
            if ((ptype == BLE_ADV_PDU_TYPE_SCAN_REQ) &&
                (hdr->rxinfo.flags & BLE_MBUF_HDR_F_SCAN_RSP_TXD)) {
                adv_event_over = 0;
            }
        }
    }

    if (adv_event_over) {
        ble_ll_adv_done(advsm);
    }
}

/**
 * Called when a receive PDU has started and we are advertising.
 *
 * Context: interrupt
 *
 * @param pdu_type
 * @param rxpdu
 *
 * @return int
 *   < 0: A frame we dont want to receive.
 *   = 0: Continue to receive frame. Dont go from rx to tx
 *   > 0: Continue to receive frame and go from rx to tx when done
 */
int
ble_ll_adv_rx_isr_start(uint8_t pdu_type)
{
    int rc;
    struct ble_ll_adv_sm *advsm;

    /* Assume we will abort the frame */
    rc = -1;

    /* If we get a scan request we must tell the phy to go from rx to tx */
    advsm = g_ble_ll_cur_adv_sm;
    if (pdu_type == BLE_ADV_PDU_TYPE_SCAN_REQ) {
        /* Only accept scan requests if we are indirect adv or scan adv */
        if ((advsm->adv_type == BLE_HCI_ADV_TYPE_ADV_SCAN_IND) ||
            (advsm->adv_type == BLE_HCI_ADV_TYPE_ADV_IND)) {
            rc = 1;
        }
    } else {
        /* Only accept connect requests if connectable advertising event */
        if (pdu_type == BLE_ADV_PDU_TYPE_CONNECT_REQ) {
            if ((advsm->adv_type == BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD) ||
                (advsm->adv_type == BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_LD) ||
                (advsm->adv_type == BLE_HCI_ADV_TYPE_ADV_IND)) {
                rc = 0;
            }
        }
    }

    /*
     * If we abort the frame, we need to post the LL task to check if the
     * advertising event is over.
     */
    if (rc < 0) {
        ble_ll_adv_tx_done(advsm);
    }

    return rc;
}

/**
 * Called when an advertising event is over.
 *
 * Context: Link Layer task.
 *
 * @param arg Pointer to advertising state machine.
 */
static void
ble_ll_adv_done(struct ble_ll_adv_sm *advsm)
{
    int rc;
    int resched_pdu;
    uint8_t mask;
    uint8_t final_adv_chan;
    int32_t delta_t;
    uint32_t itvl;
    uint32_t tick_itvl;
    uint32_t start_time;
    uint32_t max_delay_ticks;

    assert(advsm->adv_enabled);

    /* Remove the element from the schedule if it is still there. */
    ble_ll_sched_rmv_elem(&advsm->adv_sch);
    os_eventq_remove(&g_ble_ll_data.ll_evq, &advsm->adv_txdone_ev);

    /*
     * Check if we have ended our advertising event. If our last advertising
     * packet was sent on the last channel, it means we are done with this
     * event.
     */
    final_adv_chan = ble_ll_adv_final_chan(advsm);

    if (advsm->adv_chan == final_adv_chan) {
        /* Check if we need to resume scanning */
        ble_ll_scan_chk_resume();

        /* This event is over. Set adv channel to first one */
        advsm->adv_chan = ble_ll_adv_first_chan(advsm);

        /*
         * Calculate start time of next advertising event. NOTE: we do not
         * add the random advDelay as the scheduling code will do that.
         */
        itvl = advsm->adv_itvl_usecs;
        if (advsm->adv_type != BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD) {
            max_delay_ticks =
                os_cputime_usecs_to_ticks(BLE_LL_ADV_DELAY_MS_MAX * 1000);
        } else {
            max_delay_ticks = 0;
        }
        tick_itvl = os_cputime_usecs_to_ticks(itvl);
        advsm->adv_event_start_time += tick_itvl;
        advsm->adv_pdu_start_time = advsm->adv_event_start_time;

        /*
         * The scheduled time better be in the future! If it is not, we will
         * just keep advancing until we the time is in the future
         */
        start_time = advsm->adv_pdu_start_time -
            os_cputime_usecs_to_ticks(XCVR_TX_SCHED_DELAY_USECS);

        delta_t = (int32_t)(start_time - os_cputime_get32());
        if (delta_t < 0) {
            /*
             * NOTE: we just the same interval that we calculated earlier.
             * No real need to keep recalculating a new interval.
             */
            while (delta_t < 0) {
                advsm->adv_event_start_time += tick_itvl;
                advsm->adv_pdu_start_time = advsm->adv_event_start_time;
                delta_t += (int32_t)tick_itvl;
            }
        }
        resched_pdu = 0;
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

        /*
         * We will transmit right away. Set next pdu start time to now
         * plus a xcvr start delay just so we dont count late adv starts
         */
        advsm->adv_pdu_start_time = os_cputime_get32() +
            os_cputime_usecs_to_ticks(XCVR_TX_SCHED_DELAY_USECS);

        resched_pdu = 1;
    }

    /*
     * Stop high duty cycle directed advertising if we have been doing
     * it for more than 1.28 seconds
     */
    if (advsm->adv_type == BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD) {
        if (advsm->adv_pdu_start_time >= advsm->adv_dir_hd_end_time) {
            /* Disable advertising */
            advsm->adv_enabled = 0;
            ble_ll_conn_comp_event_send(NULL, BLE_ERR_DIR_ADV_TMO,
                                        advsm->conn_comp_ev, advsm);
            advsm->conn_comp_ev = NULL;
            ble_ll_scan_chk_resume();
            return;
        }
    }

    /* We need to regenerate our RPA's if we have passed timeout */
#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    ble_ll_adv_chk_rpa_timeout(advsm);
#endif

    /* Schedule advertising transmit */
    ble_ll_adv_set_sched(advsm, 0);

    /*
     * In the unlikely event we cant reschedule this, just post a done
     * event and we will reschedule the next advertising event
     */
    if (resched_pdu) {
        rc = ble_ll_sched_adv_resched_pdu(&advsm->adv_sch);
    } else {
        rc = ble_ll_sched_adv_reschedule(&advsm->adv_sch, &start_time,
                                         max_delay_ticks);
        if (!rc) {
            advsm->adv_event_start_time = start_time;
            advsm->adv_pdu_start_time = start_time;
        }
    }

    if (rc) {
        advsm->adv_chan = final_adv_chan;
        os_eventq_put(&g_ble_ll_data.ll_evq, &advsm->adv_txdone_ev);
    }
}

static void
ble_ll_adv_event_done(struct os_event *ev)
{
    ble_ll_adv_done(ev->ev_arg);
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

#if MYNEWT_VAL(BLE_MULTI_ADV_SUPPORT)
    int i;

    rc = 1;
    for (i = 0; i < BLE_LL_ADV_INSTANCES; ++i) {
        advsm = &g_ble_ll_adv_sm[i];
        if (advsm->adv_enabled &&
            (advsm->adv_filter_policy != BLE_HCI_ADV_FILT_NONE)) {
            rc = 0;
            break;
        }
    }
#else
    advsm = &g_ble_ll_adv_sm[0];
    if (advsm->adv_enabled &&
        (advsm->adv_filter_policy != BLE_HCI_ADV_FILT_NONE)) {
        rc = 0;
    } else {
        rc = 1;
    }
#endif

    return rc;
}

/**
 * Sends the connection complete event when advertising a connection starts.
 *
 * @return uint8_t* Pointer to event buffer
 */
void
ble_ll_adv_send_conn_comp_ev(struct ble_ll_conn_sm *connsm,
                             struct ble_mbuf_hdr *rxhdr)
{
    uint8_t *evbuf;
    struct ble_ll_adv_sm *advsm;

#if MYNEWT_VAL(BLE_MULTI_ADV_SUPPORT)
    advsm = (struct ble_ll_adv_sm *)rxhdr->rxinfo.advsm;
    evbuf = ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_HI);
    if (evbuf) {
        evbuf[0] = BLE_HCI_EVCODE_LE_META;
        evbuf[1] = 5;   /* Length of event, including sub-event code */
        evbuf[2] = BLE_HCI_LE_SUBEV_ADV_STATE_CHG;
        evbuf[3] = advsm->adv_instance;
        evbuf[4] = 0x00;    /* status code */
        put_le16(evbuf + 5, connsm->conn_handle);
        ble_ll_hci_event_send(evbuf);
    }
#else
    advsm = &g_ble_ll_adv_sm[0];
#endif

    evbuf = advsm->conn_comp_ev;
    assert(evbuf != NULL);
    advsm->conn_comp_ev = NULL;

    ble_ll_conn_comp_event_send(connsm, BLE_ERR_SUCCESS, evbuf, advsm);
}

/**
 * Returns the local resolvable private address currently being using by
 * the advertiser
 *
 * @return uint8_t*
 */
uint8_t *
ble_ll_adv_get_local_rpa(struct ble_ll_adv_sm *advsm)
{
    uint8_t *rpa;

    rpa = NULL;
    if (advsm->own_addr_type > BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        rpa = advsm->adva;
    }

    return rpa;
}

/**
 * Returns the peer resolvable private address of last device connecting to us
 *
 * @return uint8_t*
 */
uint8_t *
ble_ll_adv_get_peer_rpa(struct ble_ll_adv_sm *advsm)
{
    /* XXX: should this go into IRK list or connection? */
    return advsm->adv_rpa;
}

/**
 * Called when the LL wait for response timer expires while in the advertising
 * state. Disables the phy and
 *
 */
void
ble_ll_adv_wfr_timer_exp(void)
{
    ble_phy_disable();
    ble_ll_adv_tx_done(g_ble_ll_cur_adv_sm);
}

/**
 * Reset the advertising state machine.
 *
 * Context: Link Layer task
 *
 */
void
ble_ll_adv_reset(void)
{
    int i;
    struct ble_ll_adv_sm *advsm;

    for (i = 0; i < BLE_LL_ADV_INSTANCES; ++i) {
        /* Stop advertising state machine */
        advsm = &g_ble_ll_adv_sm[i];
        ble_ll_adv_sm_stop(advsm);
    }

    /* re-initialize the advertiser state machine */
    ble_ll_adv_init();
}

/* Called to determine if advertising is enabled.
 *
 * NOTE: this currently only applies to the default advertising index.
 */
uint8_t
ble_ll_adv_enabled(void)
{
    return g_ble_ll_adv_sm[0].adv_enabled;
}

/**
 * Initialize the advertising functionality of a BLE device. This should
 * be called once on initialization
 */
void
ble_ll_adv_init(void)
{
    int i;
    struct ble_ll_adv_sm *advsm;

    /* Set default advertising parameters */
    for (i = 0; i < BLE_LL_ADV_INSTANCES; ++i) {
        advsm = &g_ble_ll_adv_sm[i];

        memset(advsm, 0, sizeof(struct ble_ll_adv_sm));

        advsm->adv_instance = i;
        advsm->adv_itvl_min = BLE_HCI_ADV_ITVL_DEF;
        advsm->adv_itvl_max = BLE_HCI_ADV_ITVL_DEF;
        advsm->adv_chanmask = BLE_HCI_ADV_CHANMASK_DEF;

        /* Initialize advertising tx done event */
        advsm->adv_txdone_ev.ev_cb = ble_ll_adv_event_done;
        advsm->adv_txdone_ev.ev_arg = advsm;
    }
}

