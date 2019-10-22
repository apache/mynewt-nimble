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

#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

#include "syscfg/syscfg.h"

#include "controller/ble_ll.h"
#include "controller/ble_ll_hci.h"
#include "controller/ble_ll_sync.h"
#include "controller/ble_ll_utils.h"
#include "controller/ble_ll_sched.h"
#include "controller/ble_ll_whitelist.h"
#include "controller/ble_ll_scan.h"

#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "nimble/ble_hci_trans.h"

#include "stats/stats.h"

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PERIODIC_ADV)

/* defines number of events that can be lost during sync establishment
 * before failed to be established error is reported
 */
#define BLE_LL_SYNC_ESTABLISH_CNT 6

#define BLE_LL_SYNC_CNT MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PERIODIC_ADV_SYNC_CNT)
#define BLE_LL_SYNC_LIST_CNT MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PERIODIC_ADV_SYNC_LIST_CNT)

#define BLE_LL_SYNC_SM_FLAG_RESERVED        0x01
#define BLE_LL_SYNC_SM_FLAG_ESTABLISHING    0x02
#define BLE_LL_SYNC_SM_FLAG_ESTABLISHED     0x04
#define BLE_LL_SYNC_SM_FLAG_SET_ANCHOR      0x08
#define BLE_LL_SYNC_SM_FLAG_OFFSET_300      0x10
#define BLE_LL_SYNC_SM_FLAG_SYNC_INFO       0x20
#define BLE_LL_SYNC_SM_FLAG_DISABLED        0x40

#define BLE_LL_SYNC_CHMAP_LEN               5
#define BLE_LL_SYNC_ITVL_USECS              1250

struct ble_ll_sync_sm {
    uint8_t flags;

    uint8_t adv_sid;
    uint8_t adv_addr[BLE_DEV_ADDR_LEN];
    uint8_t adv_addr_type;

    uint8_t sca;
    uint8_t chanmap[BLE_LL_SYNC_CHMAP_LEN];
    uint8_t num_used_chans;

    uint8_t chan_index;
    uint8_t chan_chain;

    uint8_t phy_mode;

    uint8_t sync_pending_cnt;

    uint32_t timeout;
    uint16_t skip;

    uint16_t itvl;
    uint8_t  itvl_usecs;
    uint32_t itvl_ticks;

    uint32_t crcinit; /* only 3 bytes are used */
    uint32_t access_addr;
    uint16_t event_cntr;
    uint16_t channel_id;

    uint32_t window_widening;
    uint32_t last_anchor_point;
    uint32_t anchor_point;
    uint8_t anchor_point_usecs;

    struct ble_ll_sched_item sch;

    struct ble_npl_event sync_ev_end;

    uint8_t *next_report;
};

static struct ble_ll_sync_sm g_ble_ll_sync_sm[BLE_LL_SYNC_CNT];

static struct {
    uint8_t adv_sid;
    uint8_t adv_addr[BLE_DEV_ADDR_LEN];
    uint8_t adv_addr_type;
} ble_ll_sync_adv_list[BLE_LL_SYNC_LIST_CNT];

static uint32_t ble_ll_sync_create_timeout;
static uint16_t ble_ll_sync_create_max_skip;
static uint16_t ble_ll_sync_create_options;

/* if this is set HCI LE Sync Create is pending */
static uint8_t *g_ble_ll_sync_create_comp_ev;

static struct ble_ll_sync_sm *g_ble_ll_sync_sm_current;

static int
ble_ll_sync_on_list(const uint8_t *addr, uint8_t addr_type, uint8_t sid)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(ble_ll_sync_adv_list); i++) {
        if ((ble_ll_sync_adv_list[i].adv_sid == sid) &&
                (ble_ll_sync_adv_list[i].adv_addr_type == addr_type) &&
                !memcmp(ble_ll_sync_adv_list[i].adv_addr, addr, BLE_DEV_ADDR_LEN)) {
            return i;
        }
    }

    return -1;
}

static int
ble_ll_sync_list_get_free(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(ble_ll_sync_adv_list); i++) {
        if (ble_ll_sync_adv_list[i].adv_sid == 0xff) {
            return i;
        }
    }

    return -1;
}

static bool
ble_ll_sync_list_empty(void) {
    int i;

    for (i = 0; i < ARRAY_SIZE(ble_ll_sync_adv_list); i++) {
        if (ble_ll_sync_adv_list[i].adv_sid != 0xff) {
            return false;
        }
    }

    return true;
}

static uint8_t
ble_ll_sync_get_handle(struct ble_ll_sync_sm *sm)
{
    /* handle number is offset in global array */
    return sm - g_ble_ll_sync_sm;
}

static void
ble_ll_sync_sm_clear(struct ble_ll_sync_sm *sm)
{
    if (sm->flags & (BLE_LL_SYNC_SM_FLAG_ESTABLISHING |
                     BLE_LL_SYNC_SM_FLAG_ESTABLISHED)) {
        ble_ll_sched_rmv_elem(&sm->sch);
        ble_npl_eventq_remove(&g_ble_ll_data.ll_evq, &sm->sync_ev_end);
    }

    if (sm->next_report) {
        ble_hci_trans_buf_free(sm->next_report);
    }

    if (g_ble_ll_sync_sm_current == sm) {
        ble_phy_disable();
        ble_ll_wfr_disable();
        ble_ll_state_set(BLE_LL_STATE_STANDBY);
        g_ble_ll_sync_sm_current = NULL;
        ble_ll_scan_chk_resume();

#ifdef BLE_XCVR_RFCLK
        ble_ll_sched_rfclk_chk_restart();
#endif
    }

    BLE_LL_ASSERT(sm->sync_ev_end.ev.ev_queued == 0);
    BLE_LL_ASSERT(sm->sch.enqueued == 0);
    memset(sm, 0, sizeof(*sm));
}

static uint8_t
ble_ll_sync_phy_mode_to_hci(int8_t phy_mode)
{
#if (BLE_LL_BT5_PHY_SUPPORTED == 1)
    switch (phy_mode) {
    case BLE_PHY_MODE_1M:
        return BLE_HCI_LE_PHY_1M;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_2M_PHY)
    case BLE_PHY_MODE_2M:
        return BLE_HCI_LE_PHY_2M;
#endif
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_CODED_PHY)
    case BLE_PHY_MODE_CODED_125KBPS:
    case BLE_PHY_MODE_CODED_500KBPS:
        return BLE_HCI_LE_PHY_CODED;
#endif
    default:
        BLE_LL_ASSERT(false);
        return BLE_PHY_MODE_1M;
    }
#else
    return BLE_PHY_MODE_1M;
#endif
}

static void
ble_ll_sync_est_event_success(struct ble_ll_sync_sm *sm)
{
    struct ble_hci_ev_le_subev_periodic_adv_sync_estab *ev;
    struct ble_hci_ev *hci_ev;

    BLE_LL_ASSERT(g_ble_ll_sync_create_comp_ev);

    if (ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_PERIODIC_ADV_SYNC_ESTAB)) {
        hci_ev = (void *) g_ble_ll_sync_create_comp_ev;

        hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
        hci_ev->length = sizeof(*ev);
        ev = (void *) hci_ev->data;

        ev->subev_code = BLE_HCI_LE_SUBEV_PERIODIC_ADV_SYNC_ESTAB;
        ev->status = BLE_ERR_SUCCESS;
        ev->sync_handle = htole16(ble_ll_sync_get_handle(sm));
        ev->sid = sm->adv_sid;
        ev->peer_addr_type = sm->adv_addr_type;
        memcpy(ev->peer_addr, sm->adv_addr, BLE_DEV_ADDR_LEN);
        ev->phy = ble_ll_sync_phy_mode_to_hci(sm->phy_mode);
        ev->interval = htole16(sm->itvl);
        ev->aca = sm->sca;

        ble_ll_hci_event_send(hci_ev);
    } else {
        ble_hci_trans_buf_free(g_ble_ll_sync_create_comp_ev);
    }

    g_ble_ll_sync_create_comp_ev = NULL;
}

static void
ble_ll_sync_est_event_failed(uint8_t status)
{
    struct ble_hci_ev_le_subev_periodic_adv_sync_estab *ev;
    struct ble_hci_ev *hci_ev;

    BLE_LL_ASSERT(g_ble_ll_sync_create_comp_ev);

    if (ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_PERIODIC_ADV_SYNC_ESTAB)) {
        hci_ev = (void *) g_ble_ll_sync_create_comp_ev;

        hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
        hci_ev->length = sizeof(*ev);
        ev = (void *) hci_ev->data;

        memset(ev, 0, sizeof(*ev));

        ev->subev_code = BLE_HCI_LE_SUBEV_PERIODIC_ADV_SYNC_ESTAB;
        ev->status = status;

        ble_ll_hci_event_send(hci_ev);
    } else {
        ble_hci_trans_buf_free(g_ble_ll_sync_create_comp_ev);
    }

    g_ble_ll_sync_create_comp_ev = NULL;
}

static void
ble_ll_sync_lost_event(struct ble_ll_sync_sm *sm)
{
    struct ble_hci_ev_le_subev_periodic_adv_sync_lost *ev;
    struct ble_hci_ev *hci_ev;

    if (ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_PERIODIC_ADV_SYNC_LOST)) {
        hci_ev = (void *) ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_HI);
        if (hci_ev) {
            hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
            hci_ev->length = sizeof(*ev);
            ev = (void *) hci_ev->data;

            ev->subev_code = BLE_HCI_LE_SUBEV_PERIODIC_ADV_SYNC_LOST;
            ev->sync_handle = htole16(ble_ll_sync_get_handle(sm));

            ble_ll_hci_event_send(hci_ev);
        }
    }
}

static struct ble_ll_sync_sm *
ble_ll_sync_find(const uint8_t *addr, uint8_t addr_type, uint8_t sid)
{
    struct ble_ll_sync_sm *sm;
    int i;

    for (i = 0; i < BLE_LL_SYNC_CNT; i++) {
        sm = &g_ble_ll_sync_sm[i];

        if (!sm->flags) {
            continue;
        }
        if ((sm->adv_sid == sid) && (sm->adv_addr_type == addr_type) &&
                !memcmp(&sm->adv_addr, addr, BLE_DEV_ADDR_LEN)) {
            return sm;
        }
    }

    return NULL;
}

static void
ble_ll_sync_current_sm_over(void)
{
    /* Disable the PHY */
    ble_phy_disable();

    /* Disable the wfr timer */
    ble_ll_wfr_disable();

    /* Link-layer is in standby state now */
    ble_ll_state_set(BLE_LL_STATE_STANDBY);

    /* Set current LL sync to NULL */
    g_ble_ll_sync_sm_current = NULL;
}

static int
ble_ll_sync_event_start_cb(struct ble_ll_sched_item *sch)
{
    struct ble_ll_sync_sm *sm;
    uint32_t wfr_usecs;
    uint32_t start;
    int rc;

    /* Set current connection state machine */
    sm = sch->cb_arg;
    BLE_LL_ASSERT(sm);

    g_ble_ll_sync_sm_current = sm;

    /* Disable whitelisting */
    ble_ll_whitelist_disable();

    /* Set LL state */
    ble_ll_state_set(BLE_LL_STATE_SYNC);

    /* Set channel */
    ble_phy_setchan(sm->chan_index, sm->access_addr, sm->crcinit);

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    ble_phy_resolv_list_disable();
#endif

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_ENCRYPTION)
    ble_phy_encrypt_disable();
#endif

#if (BLE_LL_BT5_PHY_SUPPORTED == 1)
    ble_phy_mode_set(sm->phy_mode, sm->phy_mode);
#endif

    start = sch->start_time + g_ble_ll_sched_offset_ticks;
    rc = ble_phy_rx_set_start_time(start, sch->remainder);
    if (rc && rc != BLE_PHY_ERR_RX_LATE) {
        STATS_INC(ble_ll_stats, sync_event_failed);
        rc = BLE_LL_SCHED_STATE_DONE;
        ble_ll_event_send(&sm->sync_ev_end);
        ble_ll_sync_current_sm_over();
    } else {
        /*
         * Set flag that tells to set last anchor point if a packet
         * has been received.
         */
        sm->flags |= BLE_LL_SYNC_SM_FLAG_SET_ANCHOR;

        /* set wfr timer, for first PDU pointed with AuxPtr we use offset
         * unit, for other PDU we use current window widening
         */
        if (sm->flags & BLE_LL_SYNC_SM_FLAG_SYNC_INFO) {
            wfr_usecs = (sm->flags & BLE_LL_SYNC_SM_FLAG_OFFSET_300) ? 300 : 30;
        } else {
            wfr_usecs = 2 * sm->window_widening;
        }
        ble_phy_wfr_enable(BLE_PHY_WFR_ENABLE_RX, 0, wfr_usecs);

        rc = BLE_LL_SCHED_STATE_RUNNING;
    }

    sm->flags &= ~BLE_LL_SYNC_SM_FLAG_SYNC_INFO;

    return rc;
}

/**
 * Called when a receive PDU has started.
 *
 * Context: interrupt
 *
 * @return int
 *   < 0: A frame we dont want to receive.
 *   = 0: Continue to receive frame. Dont go from rx to tx
 */
int
ble_ll_sync_rx_isr_start(uint8_t pdu_type, struct ble_mbuf_hdr *rxhdr)
{
    BLE_LL_ASSERT(g_ble_ll_sync_sm_current);

    /* this also handles chains as those have same PDU type */
    if (pdu_type != BLE_ADV_PDU_TYPE_AUX_SYNC_IND) {
        ble_ll_event_send(&g_ble_ll_sync_sm_current->sync_ev_end);
        ble_ll_sync_current_sm_over();
        STATS_INC(ble_ll_stats, sched_invalid_pdu);
        return -1;
    }

    STATS_INC(ble_ll_stats, sync_received);
    return 0;
}

int
ble_ll_sync_parse_ext_hdr(struct os_mbuf *om, uint8_t **aux, int8_t *tx_power)
{
    uint8_t *rxbuf = om->om_data;
    uint8_t ext_hdr_flags;
    uint8_t ext_hdr_len;
    uint8_t *ext_hdr;
    uint8_t pdu_len;
    int i;

    pdu_len = rxbuf[1];
    if (pdu_len == 0) {
        return -1;
    }
    ext_hdr_len = rxbuf[2] & 0x3F;
    if (ext_hdr_len > pdu_len) {
        return -1;
    }

    os_mbuf_adj(om, 3);

    if (ext_hdr_len) {
        ext_hdr_flags = rxbuf[3];
        ext_hdr = &rxbuf[4];

        i = 0;

        /* TODO should we ignore packets with extra data instead of just
         * skipping fields?
         */

        /* there should be no AdvA in Sync or chain, skip it */
        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_ADVA_BIT)) {
            i += BLE_LL_EXT_ADV_ADVA_SIZE;
        }

        /* there should be no TargetA in Sync or chain, skip it */
        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_TARGETA_BIT)) {
            i += BLE_LL_EXT_ADV_TARGETA_SIZE;
        }

        /* there should be no RFU in Sync or chain, skip it */
        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_CTE_INFO_BIT)) {
            i += 1;
        }

        /* there should be no ADI in Sync or chain, skip it */
        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_DATA_INFO_BIT)) {
            i += BLE_LL_EXT_ADV_DATA_INFO_SIZE;
        }

        /* get AuXPTR if present */
        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_AUX_PTR_BIT)) {
            *aux = ext_hdr + i;
            i += BLE_LL_EXT_ADV_AUX_PTR_SIZE;
        } else {
            *aux = NULL;
        }

        /* there should be no SyncInfo in Sync or chain, skip it */
        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_SYNC_INFO_BIT)) {
            i += BLE_LL_EXT_ADV_SYNC_INFO_SIZE;
        }

        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_TX_POWER_BIT)) {
            *tx_power = *(ext_hdr + i);
            i += BLE_LL_EXT_ADV_TX_POWER_SIZE;
        } else {
            *tx_power = 127; /* not available */
        }

        /* TODO Handle ACAD if needed */
    }

    if (ext_hdr_len) {
        /* Adjust mbuf to contain advertising data only */
        os_mbuf_adj(om, ext_hdr_len);
    }

    return pdu_len - ext_hdr_len - 1;
}

static void
ble_ll_sync_send_truncated_per_adv_rpt(struct ble_ll_sync_sm *sm, uint8_t *evbuf)
{
    struct ble_hci_ev_le_subev_periodic_adv_rpt *ev;
    struct ble_hci_ev *hci_ev;

    if (!ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_PERIODIC_ADV_RPT)) {
        ble_hci_trans_buf_free(evbuf);
        return;
    }

    hci_ev = (void *) evbuf;

    hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
    hci_ev->length = sizeof(*ev);
    ev = (void *) hci_ev->data;

    ev->subev_code = BLE_HCI_LE_SUBEV_PERIODIC_ADV_RPT;
    ev->sync_handle = htole16(ble_ll_sync_get_handle(sm));
    ev->tx_power = 127; /* not available */
    ev->rssi = 127; /* not available */
    ev->cte_type = 0xff;
    ev->data_status = BLE_HCI_PERIODIC_DATA_STATUS_TRUNCATED;
    ev->data_len = 0;

    ble_ll_hci_event_send(hci_ev);
}

static int
ble_ll_sync_send_per_adv_rpt(struct ble_ll_sync_sm *sm, struct os_mbuf *rxpdu,
                             struct ble_mbuf_hdr *hdr, uint8_t **aux)
{
    struct ble_hci_ev_le_subev_periodic_adv_rpt *ev;
    struct ble_hci_ev *hci_ev;
    struct ble_hci_ev *hci_ev_next = NULL;
    uint8_t max_data_len;
    int8_t tx_power;
    int datalen;
    int offset;
    int rc;

    if (!ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_PERIODIC_ADV_RPT)) {
        return -1;
    }

    /* spec is not clear if we should truncate chain or just stop sending
     * reports... for now just truncate
     */
    if (sm->flags & BLE_LL_SYNC_SM_FLAG_DISABLED) {
        if (sm->next_report) {
            ble_ll_sync_send_truncated_per_adv_rpt(sm, sm->next_report);
            sm->next_report = NULL;
        }
        return -1;
    }

    datalen = ble_ll_sync_parse_ext_hdr(rxpdu, aux, &tx_power);
    if (datalen < 0) {
        /* we got bad packet but were chaining, send truncated report */
        if (sm->next_report) {
            ble_ll_sync_send_truncated_per_adv_rpt(sm, sm->next_report);
            sm->next_report = NULL;
        }
        return -1;
    }

    /* use next report buffer if present, this means we are chaining */
    if (sm->next_report) {
        hci_ev = (void *) sm->next_report;
        sm->next_report = NULL;
    } else {
        hci_ev = (void * )ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_LO);
        if (!hci_ev) {
            return -1;
        }
    }

    max_data_len = BLE_LL_MAX_EVT_LEN - sizeof(*hci_ev) - sizeof(*ev);
    offset = 0;

    do {
        if (hci_ev_next) {
            hci_ev = hci_ev_next;
            hci_ev_next = NULL;
        }

        hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
        hci_ev->length = sizeof(*ev);

        ev = (void *) hci_ev->data;

        ev->subev_code = BLE_HCI_LE_SUBEV_PERIODIC_ADV_RPT;
        ev->sync_handle = htole16(ble_ll_sync_get_handle(sm));
        ev->tx_power = tx_power;
        ev->rssi = hdr->rxinfo.rssi;
        ev->cte_type = 0xff;

        ev->data_len = min(max_data_len, datalen - offset);
        /* adjust event length */
        hci_ev->length += ev->data_len;

        os_mbuf_copydata(rxpdu, offset, ev->data_len, ev->data);
        offset += ev->data_len;

        /* Need another event for next fragment of this PDU */
        if ((offset < datalen) || *aux) {
            hci_ev_next = (void *) ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_LO);
            if (hci_ev_next) {
                ev->data_status = BLE_HCI_PERIODIC_DATA_STATUS_INCOMPLETE;
                rc = 0;
            } else {
                ev->data_status = BLE_HCI_PERIODIC_DATA_STATUS_TRUNCATED;
                rc = -1;
            }
        } else {
            ev->data_status = BLE_HCI_PERIODIC_DATA_STATUS_COMPLETE;
            rc = 0;
        }

        ble_ll_hci_event_send(hci_ev);
    } while ((offset < datalen) && hci_ev_next);

    /* store for chain */
    sm->next_report = (void *) hci_ev_next;

    return rc;
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
ble_ll_sync_rx_isr_end(uint8_t *rxbuf, struct ble_mbuf_hdr *rxhdr)
{
    struct ble_mbuf_hdr *ble_hdr;
    struct os_mbuf *rxpdu;

    BLE_LL_ASSERT(g_ble_ll_sync_sm_current);

    /* type was verified in isr_start */

    rxpdu = ble_ll_rxpdu_alloc(rxbuf[1] + BLE_LL_PDU_HDR_LEN);
    if (rxpdu) {
        ble_phy_rxpdu_copy(rxbuf, rxpdu);

        ble_hdr = BLE_MBUF_HDR_PTR(rxpdu);
        ble_hdr->rxinfo.user_data = g_ble_ll_sync_sm_current;

        ble_ll_rx_pdu_in(rxpdu);
    } else {
        STATS_INC(ble_ll_stats, sync_rx_buf_err);
        ble_ll_event_send(&g_ble_ll_sync_sm_current->sync_ev_end);
    }

    ble_ll_sync_current_sm_over();

    return -1;
}

/**
 * Called when the wait for response timer expires while in the sync state.
 *
 * Context: Interrupt.
 */
void
ble_ll_sync_wfr_timer_exp(void)
{
    struct ble_ll_sync_sm *sm = g_ble_ll_sync_sm_current;

    BLE_LL_ASSERT(g_ble_ll_sync_sm_current);
    STATS_INC(ble_ll_stats, sync_missed_err);

    ble_ll_sync_current_sm_over();
    ble_ll_event_send(&sm->sync_ev_end);
}

/**
 * Called when sync event needs to be halted. This normally should not be called
 * and is only called when a scheduled item executes but scanning for sync/chain
 * is stil ongoing
 * Context: Interrupt
 */
void
ble_ll_sync_halt(void)
{
    struct ble_ll_sync_sm *sm = g_ble_ll_sync_sm_current;

    ble_ll_sync_current_sm_over();

    if (sm) {
        ble_ll_event_send(&sm->sync_ev_end);
    }
}

uint32_t
ble_ll_sync_get_event_end_time(void)
{
    uint32_t end_time;

    if (g_ble_ll_sync_sm_current) {
        end_time = g_ble_ll_sync_sm_current->sch.end_time;
    } else {
        end_time = os_cputime_get32();
    }
    return end_time;
}

static uint8_t
ble_ll_sync_phy_mode_to_aux_phy(uint8_t phy_mode)
{
    switch (phy_mode) {
    case BLE_PHY_MODE_1M:
        return 0x00;
    case BLE_PHY_MODE_2M:
        return 0x01;
    case BLE_PHY_MODE_CODED_125KBPS:
    case BLE_PHY_MODE_CODED_500KBPS:
        return 0x02;
    default:
        BLE_LL_ASSERT(false);
        return 0x00;
    }
}

static void
ble_ll_sync_parse_aux_ptr(const uint8_t *buf, uint8_t *chan, uint32_t *offset,
                          uint8_t *offset_units, uint8_t *phy)
{
    uint32_t aux_ptr_field = get_le32(buf) & 0x00FFFFFF;

    *chan = aux_ptr_field & 0x3F;

    /* TODO use CA aux_ptr_field >> 6 */

    if ((aux_ptr_field >> 7) & 0x01) {
        *offset = 300 * ((aux_ptr_field >> 8) & 0x1FFF);
        *offset_units = 1;
    } else {
        *offset = 30 * ((aux_ptr_field >> 8) & 0x1FFF);
        *offset_units = 0;
    }

    *phy = (aux_ptr_field >> 21) & 0x07;
}

static int
ble_ll_sync_chain_start_cb(struct ble_ll_sched_item *sch)
{
    struct ble_ll_sync_sm *sm;
    uint32_t wfr_usecs;
    uint32_t start;
    int rc;

    /* Set current connection state machine */
    sm = sch->cb_arg;
    g_ble_ll_sync_sm_current = sm;
    BLE_LL_ASSERT(sm);
    BLE_LL_ASSERT(sm->next_report);

    /* Disable whitelisting */
    ble_ll_whitelist_disable();

    /* Set LL state */
    ble_ll_state_set(BLE_LL_STATE_SYNC);

    /* Set channel */
    ble_phy_setchan(sm->chan_chain, sm->access_addr, sm->crcinit);

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    ble_phy_resolv_list_disable();
#endif

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_ENCRYPTION)
    ble_phy_encrypt_disable();
#endif

#if (BLE_LL_BT5_PHY_SUPPORTED == 1)
    ble_phy_mode_set(sm->phy_mode, sm->phy_mode);
#endif

    start = sch->start_time + g_ble_ll_sched_offset_ticks;
    rc = ble_phy_rx_set_start_time(start, sch->remainder);
    if (rc && rc != BLE_PHY_ERR_RX_LATE) {
        STATS_INC(ble_ll_stats, sync_chain_failed);
        rc = BLE_LL_SCHED_STATE_DONE;
        ble_ll_event_send(&sm->sync_ev_end);
        ble_ll_sync_current_sm_over();
    } else {
        /*
         * Clear flag that tells to set last anchor point if a packet
         * has been received, this is chain and we don't need it.
         */
        sm->flags &= ~BLE_LL_SYNC_SM_FLAG_SET_ANCHOR;

        wfr_usecs = (sm->flags & BLE_LL_SYNC_SM_FLAG_OFFSET_300) ? 300 : 30;

        ble_phy_wfr_enable(BLE_PHY_WFR_ENABLE_RX, 0, wfr_usecs);
        rc = BLE_LL_SCHED_STATE_RUNNING;
    }

    return rc;
}

static int
ble_ll_sync_schedule_chain(struct ble_ll_sync_sm *sm, struct ble_mbuf_hdr *hdr,
                           const uint8_t *aux)
{
    uint8_t offset_units;
    uint32_t offset;
    uint8_t chan;
    uint8_t phy;

    ble_ll_sync_parse_aux_ptr(aux, &chan, &offset, &offset_units, &phy);

    if (chan >= BLE_PHY_NUM_DATA_CHANS) {
        return -1;
    }

    if (offset < BLE_LL_MAFS) {
        return -1;
    }

    /* chain should use same PHY as master PDU */
    if (phy != ble_ll_sync_phy_mode_to_aux_phy(sm->phy_mode)) {
        return -1;
    }

    if (offset_units) {
        sm->flags |= BLE_LL_SYNC_SM_FLAG_OFFSET_300;
    } else {
        sm->flags &= ~BLE_LL_SYNC_SM_FLAG_OFFSET_300;
    }

    sm->chan_chain = chan;

    sm->sch.sched_cb = ble_ll_sync_chain_start_cb;
    sm->sch.cb_arg = sm;
    sm->sch.sched_type = BLE_LL_SCHED_TYPE_SYNC;

    return ble_ll_sched_sync(&sm->sch, hdr, offset, sm->phy_mode);
}

static void
ble_ll_sync_established(struct ble_ll_sync_sm *sm)
{
    BLE_LL_ASSERT(sm->sync_pending_cnt);

    /* mark as established */
    ble_ll_sync_est_event_success(sm);
    sm->flags |= BLE_LL_SYNC_SM_FLAG_ESTABLISHED;
    sm->flags &= ~BLE_LL_SYNC_SM_FLAG_ESTABLISHING;

    sm->sync_pending_cnt = 0;
}

static void
ble_ll_sync_check_failed(struct ble_ll_sync_sm *sm)
{
    BLE_LL_ASSERT(sm->sync_pending_cnt);

    /* if we can retry on next event */
    if (--sm->sync_pending_cnt) {
        return;
    }

    ble_ll_sync_est_event_failed(BLE_ERR_CONN_ESTABLISHMENT);

    sm->flags &= ~BLE_LL_SYNC_SM_FLAG_ESTABLISHING;
}

void
ble_ll_sync_rx_pkt_in(struct os_mbuf *rxpdu, struct ble_mbuf_hdr *hdr)
{
    struct ble_ll_sync_sm *sm = hdr->rxinfo.user_data;
    uint8_t *aux = NULL;

    BLE_LL_ASSERT(sm);

    /* this could happen if sync was cancelled or terminated while pkt_in was
     * already in LL queue, just drop in that case
     */
    if (!sm->flags) {
        ble_ll_scan_chk_resume();
        return;
    }

    /* Set anchor point (and last) if 1st rxd frame in sync event.
     * According to spec this should be done even if CRC is not valid so we
     * can store it here
     */
    if (sm->flags & BLE_LL_SYNC_SM_FLAG_SET_ANCHOR) {
        sm->flags &= ~BLE_LL_SYNC_SM_FLAG_SET_ANCHOR;

        sm->anchor_point = hdr->beg_cputime;
        sm->anchor_point_usecs = hdr->rem_usecs;
        sm->last_anchor_point = sm->anchor_point;
    }

    /* CRC error, end event */
    if (!BLE_MBUF_HDR_CRC_OK(hdr)) {
        STATS_INC(ble_ll_stats, sync_crc_err);
        goto end_event;
    }

    /* if packet is good we send sync established here */
    if (sm->flags & BLE_LL_SYNC_SM_FLAG_ESTABLISHING) {
        ble_ll_sync_established(sm);
    }

    /* send report to host, if this fails we end event */
    if (ble_ll_sync_send_per_adv_rpt(sm, rxpdu, hdr, &aux) < 0) {
        goto end_event;
    }

    /* schedule for chain packet if AUX pointer was present */
    if (sm->next_report && aux) {
        if (ble_ll_sync_schedule_chain(sm, hdr, aux) < 0) {
            goto end_event;
        }

        /* if chain was scheduled we don't end event yet */
        /* TODO should we check resume only if offset is high? */
        ble_ll_scan_chk_resume();
        return;
    }

end_event:
    ble_ll_event_send(&sm->sync_ev_end);
}

static int
ble_ll_sync_next_event(struct ble_ll_sync_sm *sm)
{
    uint32_t cur_ww;
    uint32_t max_ww;
    uint32_t ticks;
    uint32_t itvl;
    uint8_t usecs;
    uint16_t skip = sm->skip;

    /* don't skip if are establishing sync or we missed last event */
    if (skip && ((sm->flags & BLE_LL_SYNC_SM_FLAG_ESTABLISHING) ||
                  CPUTIME_LT(sm->last_anchor_point, sm->anchor_point))) {
        skip = 0;
    }

    /* Set next event start time, we can use pre-calculated values for one
     * interval if not skipping
     */
    if (skip == 0) {
        ticks = sm->itvl_ticks;
        usecs = sm->itvl_usecs;
    } else {
        itvl = sm->itvl * BLE_LL_SYNC_ITVL_USECS * (1 + skip);
        ticks = os_cputime_usecs_to_ticks(itvl);
        usecs = itvl - os_cputime_ticks_to_usecs(ticks);
    }

    sm->anchor_point += ticks;
    sm->anchor_point_usecs += usecs;
    if (sm->anchor_point_usecs >= 31) {
        sm->anchor_point++;
        sm->anchor_point_usecs -= 31;
    }

    /* Set event counter to the next event */
    sm->event_cntr += 1 + skip;

    /* Calculate channel index of next event */
    sm->chan_index = ble_ll_utils_calc_dci_csa2(sm->event_cntr, sm->channel_id,
                                                sm->num_used_chans, sm->chanmap);

    cur_ww = ble_ll_utils_calc_window_widening(sm->anchor_point,
                                               sm->last_anchor_point,
                                               sm->sca);

    max_ww = (sm->itvl * (BLE_LL_SYNC_ITVL_USECS / 2)) - BLE_LL_IFS;
    if (cur_ww >= max_ww) {
        return -1;
    }

    /* if we are establishing, adjust window widening with offset unit
     * uncertainty, since we set anchor point in the middle of offset window
     * it is enough to adjust for half of offset
     */
    if (sm->flags & BLE_LL_SYNC_SM_FLAG_ESTABLISHING) {
        if (sm->flags & BLE_LL_SYNC_SM_FLAG_OFFSET_300) {
            cur_ww += 150;
        } else {
            cur_ww += 15;
        }
    }

    cur_ww += BLE_LL_JITTER_USECS;

    /* if updated anchor is pass last anchor + timeout it means we will not be
     * able to get it in time and hit sync timeout
     *
     * note that this may result in sync timeout being sent before real
     * timeout but we won't be able to fit in time anyway..
     *
     * We don't do that when establishing since we try up to
     * BLE_LL_SYNC_ESTABLISH_CNT events before failing regardless of timeout
     */
    if (!(sm->flags & BLE_LL_SYNC_SM_FLAG_ESTABLISHING)) {
        if (CPUTIME_GT(sm->anchor_point - os_cputime_usecs_to_ticks(cur_ww),
                       sm->last_anchor_point + sm->timeout )) {
            return -1;
        }
    }

    sm->window_widening = cur_ww;

    return 0;
}

static void
ble_ll_sync_event_end(struct ble_npl_event *ev)
{
    struct ble_ll_sync_sm *sm;

    /* Better be a connection state machine! */
    sm = ble_npl_event_get_arg(ev);
    BLE_LL_ASSERT(sm);

    if (sm->flags & BLE_LL_SYNC_SM_FLAG_ESTABLISHING) {
        ble_ll_sync_check_failed(sm);
    }

    /* Check if we need to resume scanning */
    ble_ll_scan_chk_resume();

#ifdef BLE_XCVR_RFCLK
    ble_ll_sched_rfclk_chk_restart();
#endif

    /* Remove any end events that might be enqueued */
    ble_npl_eventq_remove(&g_ble_ll_data.ll_evq, &sm->sync_ev_end);

    /* don't schedule next event if sync is not established nor establishing
     * at this point SM is no longer valid
     */
    if (!(sm->flags & (BLE_LL_SYNC_SM_FLAG_ESTABLISHED |
                       BLE_LL_SYNC_SM_FLAG_ESTABLISHING))) {
        ble_ll_sync_sm_clear(sm);
        return;
    }

    /* if we had prepared buffer for next even it means we were chaining and
     * must send truncated report to host
     */
    if (sm->next_report) {
        ble_ll_sync_send_truncated_per_adv_rpt(sm, sm->next_report);
        sm->next_report = NULL;
    }

    sm->sch.sched_cb = ble_ll_sync_event_start_cb;
    sm->sch.cb_arg = sm;
    sm->sch.sched_type = BLE_LL_SCHED_TYPE_SYNC;

    do {
        if (ble_ll_sync_next_event(sm) < 0) {
            if (sm->flags & BLE_LL_SYNC_SM_FLAG_ESTABLISHING) {
                /* don't allow any retry if this failed */
                sm->sync_pending_cnt = 1;
                ble_ll_sync_check_failed(sm);
            } else {
                ble_ll_sync_lost_event(sm);
            }

            /* at this point SM is no longer valid */
            ble_ll_sync_sm_clear(sm);
            return;
        }
    } while (ble_ll_sched_sync_reschedule(&sm->sch, sm->anchor_point,
                                          sm->anchor_point_usecs,
                                          sm->window_widening, sm->phy_mode));
}

static uint16_t
get_max_skip(uint32_t interval_us, uint32_t timeout_us)
{
    BLE_LL_ASSERT(interval_us);
    BLE_LL_ASSERT(timeout_us);

    if (timeout_us <= interval_us) {
        return 0;
    }

    return (timeout_us / interval_us) - 1;
}

void
ble_ll_sync_info_event(const uint8_t *addr, uint8_t addr_type, uint8_t sid,
                       struct ble_mbuf_hdr *rxhdr, const uint8_t *syncinfo)
{
    struct ble_ll_sync_sm *sm = NULL;
    uint16_t max_skip;
    uint32_t offset;
    uint32_t usecs;
    uint16_t itvl;
    int i;

    /* ignore if not synchronizing */
    if (!g_ble_ll_sync_create_comp_ev) {
        return;
    }

    /* get reserved SM */
    for (i = 0; i < BLE_LL_SYNC_CNT; i++) {
        if (g_ble_ll_sync_sm[i].flags & BLE_LL_SYNC_SM_FLAG_RESERVED) {
            sm = &g_ble_ll_sync_sm[i];
            break;
        }
    }

    /* this means we already got sync info event and pending sync */
    if (!sm) {
        return;
    }

    /* check peer */
    if (ble_ll_sync_create_options & BLE_HCI_LE_PERIODIC_ADV_CREATE_SYNC_OPT_FILTER) {
        if (ble_ll_sync_on_list(addr, addr_type, sid) < 0) {
            return;
        }

        /* set addr and sid in sm */
        sm->adv_sid = sid;
        sm->adv_addr_type = addr_type;
        memcpy(sm->adv_addr, addr, BLE_DEV_ADDR_LEN);
    } else {
        if ((sm->adv_sid != sid) || (sm->adv_addr_type != addr_type) ||
                memcmp(sm->adv_addr, addr, BLE_DEV_ADDR_LEN)) {
            return;
        }
    }

    /* Sync Packet Offset (13 bits), Offset Units (1 bit), RFU (2 bits) */
    offset = syncinfo[0];
    offset |= (uint16_t)(syncinfo[1] & 0x1f) << 8;

    /* ignore if offset is not valid */
    if (!offset) {
        return;
    }

    /* Interval (2 bytes), ignore if invalid */
    itvl = get_le16(&syncinfo[2]);
    if (itvl < 6) {
        return;
    }

    /* set params from HCI LE Create Periodic Sync */
    sm->timeout = ble_ll_sync_create_timeout;
    sm->skip = ble_ll_sync_create_max_skip;
    sm->sync_pending_cnt = BLE_LL_SYNC_ESTABLISH_CNT;

    if (syncinfo[1] & 0x20) {
        offset *= 300;
        sm->flags |= BLE_LL_SYNC_SM_FLAG_OFFSET_300;
    } else {
        offset *= 30;
        sm->flags &= ~BLE_LL_SYNC_SM_FLAG_OFFSET_300;
    }

    /* sync end event */
    ble_npl_event_init(&sm->sync_ev_end, ble_ll_sync_event_end, sm);

    sm->itvl = itvl;

    /* precalculate interval ticks and usecs */
    usecs = sm->itvl * BLE_LL_SYNC_ITVL_USECS;
    sm->itvl_ticks = os_cputime_usecs_to_ticks(usecs);
    sm->itvl_usecs = (uint8_t)(usecs -
                               os_cputime_ticks_to_usecs(sm->itvl_ticks));
    if (sm->itvl_usecs == 31) {
        sm->itvl_usecs = 0;
        sm->itvl_ticks++;
    }

    /* Channels Mask (37 bits) */
    sm->chanmap[0] = syncinfo[4];
    sm->chanmap[1] = syncinfo[5];
    sm->chanmap[2] = syncinfo[6];
    sm->chanmap[3] = syncinfo[7];
    sm->chanmap[4] = syncinfo[8] & 0x1f;
    sm->num_used_chans = ble_ll_utils_calc_num_used_chans(sm->chanmap);

    /* SCA (3 bits) */
    sm->sca = syncinfo[8] >> 5;

    /* AA (4 bytes) */
    sm->access_addr = get_le32(&syncinfo[9]);
    sm->channel_id = ((sm->access_addr & 0xffff0000) >> 16) ^
                      (sm->access_addr & 0x0000ffff);

    /* CRCInit (3 bytes) */
    sm->crcinit = syncinfo[15];
    sm->crcinit = (sm->crcinit << 8) | syncinfo[14];
    sm->crcinit = (sm->crcinit << 8) | syncinfo[13];

    /* Event Counter (2 bytes) */
    sm->event_cntr = get_le16(&syncinfo[16]);

    /* adjust skip if pass timeout */
    max_skip = get_max_skip(sm->itvl * BLE_LL_SYNC_ITVL_USECS, sm->timeout);
    if (sm->skip > max_skip) {
        sm->skip = max_skip;
    }

    /* from now on we only need timeout in ticks */
    sm->timeout = os_cputime_usecs_to_ticks(sm->timeout);

    sm->phy_mode = rxhdr->rxinfo.phy_mode;
    sm->window_widening = BLE_LL_JITTER_USECS;

    /* Calculate channel index of first event */
    sm->chan_index = ble_ll_utils_calc_dci_csa2(sm->event_cntr, sm->channel_id,
                                                sm->num_used_chans, sm->chanmap);

    sm->sch.sched_cb = ble_ll_sync_event_start_cb;
    sm->sch.cb_arg = sm;
    sm->sch.sched_type = BLE_LL_SCHED_TYPE_SYNC;

    if (ble_ll_sched_sync(&sm->sch, rxhdr, offset, sm->phy_mode)) {
        return;
    }

    sm->anchor_point = sm->sch.start_time + g_ble_ll_sched_offset_ticks;
    sm->anchor_point_usecs = sm->sch.remainder;

    /* set anchor point in middle of offset window */
    if (sm->flags & BLE_LL_SYNC_SM_FLAG_OFFSET_300) {
        sm->anchor_point_usecs += 150;
    } else {
        sm->anchor_point_usecs += 15;
    }

    while (sm->anchor_point_usecs >= 31) {
        sm->anchor_point++;
        sm->anchor_point_usecs -= 31;
    }

    sm->last_anchor_point = sm->anchor_point;

#if MYNEWT_VAL(BLE_VERSION) >= 51
    if (ble_ll_sync_create_options & BLE_HCI_LE_PERIODIC_ADV_CREATE_SYNC_OPT_DISABLED) {
        sm->flags |= BLE_LL_SYNC_SM_FLAG_DISABLED;
    }
#endif

    sm->flags &= ~BLE_LL_SYNC_SM_FLAG_RESERVED;
    sm->flags |= BLE_LL_SYNC_SM_FLAG_ESTABLISHING;
    sm->flags |= BLE_LL_SYNC_SM_FLAG_SYNC_INFO;
}

static struct ble_ll_sync_sm *
ble_ll_sync_reserve(void)
{
    struct ble_ll_sync_sm *sm;
    int i;

    for (i = 0; i < BLE_LL_SYNC_CNT; i++) {
        sm = &g_ble_ll_sync_sm[i];

        if (!sm->flags) {
            sm->flags |= BLE_LL_SYNC_SM_FLAG_RESERVED;
            return sm;
        }
    }

    return NULL;
}

int
ble_ll_sync_create(const uint8_t *cmdbuf, uint8_t len)
{
    const struct ble_hci_le_periodic_adv_create_sync_cp *cmd = (const void *) cmdbuf;
    struct ble_ll_sync_sm *sm;
    uint16_t timeout;
    os_sr_t sr;

    if (g_ble_ll_sync_create_comp_ev) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    if (len != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

#if MYNEWT_VAL(BLE_VERSION) >= 51
    if (cmd->options > BLE_HCI_LE_PERIODIC_ADV_CREATE_SYNC_OPT_DISABLED) {
#else
    if (cmd->options > BLE_HCI_LE_PERIODIC_ADV_CREATE_SYNC_OPT_FILTER) {
#endif
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (cmd->skip > 0x01f3) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    timeout = le16toh(cmd->sync_timeout);
    if (timeout < 0x000a || timeout > 0x4000) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

#if MYNEWT_VAL(BLE_VERSION) >= 51
    /* we don't support any CTE yet */
    if (cmd->sync_cte_type) {
        if (cmd->sync_cte_type > 4) {
           return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        return BLE_ERR_UNSUPPORTED;
    }
#endif

    if (cmd->options & BLE_HCI_LE_PERIODIC_ADV_CREATE_SYNC_OPT_FILTER) {
        if (ble_ll_sync_list_empty()) {
            return BLE_ERR_CMD_DISALLOWED;
        }
    } else {
        if (cmd->sid > 0x0f) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        if (cmd->peer_addr_type > BLE_HCI_ADV_PEER_ADDR_MAX) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        OS_ENTER_CRITICAL(sr);
        sm = ble_ll_sync_find(cmd->peer_addr, cmd->peer_addr_type, cmd->sid);
        OS_EXIT_CRITICAL(sr);

        if (sm) {
            return BLE_ERR_ACL_CONN_EXISTS;
        }
    }

    /* reserve buffer for sync complete event */
    g_ble_ll_sync_create_comp_ev = ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_HI);
    if (!g_ble_ll_sync_create_comp_ev) {
        return BLE_ERR_MEM_CAPACITY;
    }

    OS_ENTER_CRITICAL(sr);

    /* reserve 1 SM for created sync */
    sm = ble_ll_sync_reserve();
    if (!sm) {
        ble_hci_trans_buf_free(g_ble_ll_sync_create_comp_ev);
        g_ble_ll_sync_create_comp_ev = NULL;
        OS_EXIT_CRITICAL(sr);
        return BLE_ERR_MEM_CAPACITY;
    }

    /* if we don't use list, store expected address in reserved SM */
    if (!(cmd->options & BLE_HCI_LE_PERIODIC_ADV_CREATE_SYNC_OPT_FILTER)) {
        sm->adv_sid = cmd->sid;
        sm->adv_addr_type = cmd->peer_addr_type;
        memcpy(&sm->adv_addr, cmd->peer_addr, BLE_DEV_ADDR_LEN);
    }

    ble_ll_sync_create_timeout = timeout * 10000; /* 10ms units, store in us */;
    ble_ll_sync_create_max_skip = cmd->skip;
    ble_ll_sync_create_options = cmd->options;

    OS_EXIT_CRITICAL(sr);
    return BLE_ERR_SUCCESS;
}

static void
ble_ll_sync_cancel_complete_event(void)
{
    ble_ll_sync_est_event_failed(BLE_ERR_OPERATION_CANCELLED);
}

int
ble_ll_sync_cancel(ble_ll_hci_post_cmd_complete_cb *post_cmd_cb)
{
    struct ble_ll_sync_sm *sm;
    os_sr_t sr;
    int i;

    if (!g_ble_ll_sync_create_comp_ev) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    OS_ENTER_CRITICAL(sr);

    for (i = 0; i < BLE_LL_SYNC_CNT; i++) {
        sm = &g_ble_ll_sync_sm[i];

        /* cancelled before fist sync info packet */
        if (sm->flags & BLE_LL_SYNC_SM_FLAG_RESERVED) {
            memset(sm, 0, sizeof(*sm));
            break;
        }

        /* cancelled while pending sync */
        if (sm->flags & BLE_LL_SYNC_SM_FLAG_ESTABLISHING) {
            ble_ll_sync_sm_clear(sm);
            break;
        }
    }

    OS_EXIT_CRITICAL(sr);

    /* g_ble_ll_sync_create_comp_ev will be cleared by this callback */
    *post_cmd_cb = ble_ll_sync_cancel_complete_event;

    return BLE_ERR_SUCCESS;
}

int
ble_ll_sync_terminate(const uint8_t *cmdbuf, uint8_t len)
{
    const struct ble_hci_le_periodic_adv_term_sync_cp *cmd = (const void *) cmdbuf;
    struct ble_ll_sync_sm *sm;
    uint16_t handle;
    os_sr_t sr;

    if (g_ble_ll_sync_create_comp_ev) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    if (len != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    handle = le16toh(cmd->sync_handle);
    if (handle > 0xeff) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (handle >= BLE_LL_SYNC_CNT) {
        return BLE_ERR_UNK_ADV_INDENT;
    }

    sm = &g_ble_ll_sync_sm[handle];

    OS_ENTER_CRITICAL(sr);

    if (!(sm->flags & BLE_LL_SYNC_SM_FLAG_ESTABLISHED)) {
        OS_EXIT_CRITICAL(sr);
        return BLE_ERR_UNK_ADV_INDENT;
    }

    ble_ll_sync_sm_clear(sm);

    OS_EXIT_CRITICAL(sr);

    return BLE_ERR_SUCCESS;
}

int
ble_ll_sync_list_add(const uint8_t *cmdbuf, uint8_t len)
{
    const struct ble_hci_le_add_dev_to_periodic_adv_list_cp *cmd = (const void *)cmdbuf;
    int i;

    if (g_ble_ll_sync_create_comp_ev) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    if (len != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (cmd->peer_addr_type > BLE_HCI_ADV_PEER_ADDR_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }
    if (cmd->sid > 0x0f) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    i = ble_ll_sync_on_list(cmd->peer_addr, cmd->peer_addr_type, cmd->sid);
    if (i >= 0) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    i = ble_ll_sync_list_get_free();
    if (i < 0) {
        return BLE_ERR_MEM_CAPACITY;
    }

    ble_ll_sync_adv_list[i].adv_sid = cmd->sid;
    ble_ll_sync_adv_list[i].adv_addr_type = cmd->peer_addr_type;
    memcpy(&ble_ll_sync_adv_list[i].adv_addr, cmd->peer_addr, BLE_DEV_ADDR_LEN);

    return BLE_ERR_SUCCESS;
}

int
ble_ll_sync_list_remove(const uint8_t *cmdbuf, uint8_t len)
{
    const struct ble_hci_le_rem_dev_from_periodic_adv_list_cp *cmd = (const void *)cmdbuf;
    int i;

    if (len != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (g_ble_ll_sync_create_comp_ev) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    if (cmd->peer_addr_type > BLE_HCI_ADV_PEER_ADDR_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (cmd->sid > 0x0f) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    i = ble_ll_sync_on_list(cmd->peer_addr, cmd->peer_addr_type, cmd->sid);
    if (i < 0) {
        return BLE_ERR_UNK_ADV_INDENT;
    }

    memset(&ble_ll_sync_adv_list[i], 0, sizeof(ble_ll_sync_adv_list[i]));
    ble_ll_sync_adv_list[i].adv_sid = 0xff;

    return BLE_ERR_SUCCESS;
}

int
ble_ll_sync_list_clear(void)
{
    int i;

    if (g_ble_ll_sync_create_comp_ev) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    for (i = 0; i < ARRAY_SIZE(ble_ll_sync_adv_list); i++) {
        memset(&ble_ll_sync_adv_list[i], 0, sizeof(ble_ll_sync_adv_list[i]));
        ble_ll_sync_adv_list[i].adv_sid = 0xff;
    }

    return BLE_ERR_SUCCESS;
}

int
ble_ll_sync_list_size(uint8_t *rspbuf, uint8_t *rsplen)
{
    struct ble_hci_le_rd_periodic_adv_list_size_rp *rsp = (void *) rspbuf;

    rsp->list_size = ARRAY_SIZE(ble_ll_sync_adv_list);

    *rsplen = sizeof(*rsp);
    return BLE_ERR_SUCCESS;
}

#if MYNEWT_VAL(BLE_VERSION) >= 51
int
ble_ll_sync_receive_enable(const uint8_t *cmdbuf, uint8_t len)
{
    const struct ble_hci_le_periodic_adv_receive_enable_cp *cmd = (const void *)cmdbuf;
    struct ble_ll_sync_sm *sm;
    uint16_t handle;
    os_sr_t sr;

    if (len != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (cmd->enable > 0x01) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    handle = le16toh(cmd->sync_handle);
    if (handle > 0xeff) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (handle >= BLE_LL_SYNC_CNT) {
        return BLE_ERR_UNK_ADV_INDENT;
    }

    sm = &g_ble_ll_sync_sm[handle];

    OS_ENTER_CRITICAL(sr);

    if (!(sm->flags & BLE_LL_SYNC_SM_FLAG_ESTABLISHED)) {
        OS_EXIT_CRITICAL(sr);
        return BLE_ERR_UNK_ADV_INDENT;
    }

    if (cmd->enable) {
        sm->flags &= ~BLE_LL_SYNC_SM_FLAG_DISABLED;
    } else {
        sm->flags |= BLE_LL_SYNC_SM_FLAG_DISABLED;
    }

    OS_EXIT_CRITICAL(sr);
    return BLE_ERR_SUCCESS;
}
#endif

/*
 * Called when a sync scan event has been removed from the scheduler
 * without being run.
 */
void
ble_ll_sync_rmvd_from_sched(struct ble_ll_sync_sm *sm)
{
    ble_ll_event_send(&sm->sync_ev_end);
}

/**
 * Called to reset the sync module. When this function is called the
 * scheduler has been stopped and the phy has been disabled. The LL should
 * be in the standby state.
 */
void
ble_ll_sync_reset(void)
{
    int i;

    for (i = 0; i < BLE_LL_SYNC_CNT; i++) {
        ble_ll_sync_sm_clear(&g_ble_ll_sync_sm[i]);
    }

    for (i = 0; i < ARRAY_SIZE(ble_ll_sync_adv_list); i++) {
        memset(&ble_ll_sync_adv_list[i], 0, sizeof(ble_ll_sync_adv_list[i]));
        ble_ll_sync_adv_list[i].adv_sid = 0xff;
    }

    ble_ll_sync_create_timeout = 0;
    ble_ll_sync_create_max_skip = 0;
    ble_ll_sync_create_options = 0;

    g_ble_ll_sync_sm_current = NULL;

    if (g_ble_ll_sync_create_comp_ev) {
        ble_hci_trans_buf_free(g_ble_ll_sync_create_comp_ev);
        g_ble_ll_sync_create_comp_ev = NULL;
    }
}

void
ble_ll_sync_init(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(ble_ll_sync_adv_list); i++) {
        ble_ll_sync_adv_list[i].adv_sid = 0xff;
    }
}
#endif
