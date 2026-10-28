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

#include "ble_ll_iso_priv.h"
#include "ble_ll_iso_big_priv.h"
#include "ble_ll_priv.h"
#include <controller/ble_ll.h>
#include <controller/ble_ll_hci.h>
#include <controller/ble_ll_isoal.h>
#include <controller/ble_ll_pdu.h>
#include <controller/ble_ll_rfmgmt.h>
#include <controller/ble_ll_sched.h>
#include <controller/ble_ll_sync.h>
#include <controller/ble_ll_tmr.h>
#include <controller/ble_ll_utils.h>
#include <controller/ble_ll_whitelist.h>
#include <errno.h>
#include <nimble/ble.h>
#include <nimble/hci_common.h>
#include <nimble/transport.h>
#include <stdint.h>
#include <syscfg/syscfg.h>

#if MYNEWT_VAL(BLE_LL_ISO_BROADCAST_SYNC)

#define BIG_POOL_SIZE (MYNEWT_VAL(BLE_LL_ISO_BROADCAST_SYNC_MAX_BIG))
#define BIS_POOL_SIZE                                                         \
    (MYNEWT_VAL(BLE_LL_ISO_BROADCAST_SYNC_MAX_BIG) *                          \
     MYNEWT_VAL(BLE_LL_ISO_BROADCAST_SYNC_MAX_BIS))

#define BIG_CONTROL_ACTIVE_CHAN_MAP 1
#define BIG_CONTROL_ACTIVE_TERM     2

#define BIG_CTRL_PDU_MAX_LEN                                                  \
    (1 + max(sizeof(struct ble_ll_big_ctrl_chan_map_ind),                     \
             sizeof(struct ble_ll_big_ctrl_term_ind)))

/* CSSN initial value, that is out of the allowed range (1-7) */
#define CSSN_INITIAL                       0xF

#define BIG_SYNC_TIMEOUT_US(_sync_timeout) ((_sync_timeout) * 10 * 1000)

struct ble_ll_iso_big_sync_params {
    struct ble_ll_iso_big *big;
    struct ble_ll_sync_sm *syncsm;
    uint8_t broadcast_code[16];
    uint32_t bis_mask;
};

union iso_pdu_user_data {
    struct {
        uint8_t pdu_idx;
        uint8_t big_handle;
    };
    uint32_t value;
};

static os_membuf_t mb_big[OS_MEMPOOL_SIZE(BIG_POOL_SIZE, sizeof(struct ble_ll_iso_big))];
static struct os_mempool mp_big;
static os_membuf_t mb_bis[OS_MEMPOOL_SIZE(BIS_POOL_SIZE, sizeof(struct ble_ll_iso_bis))];
static struct os_mempool mp_bis;

STAILQ_HEAD(ble_ll_iso_big_q, ble_ll_iso_big);
static struct ble_ll_iso_big_q big_q;

static struct ble_ll_iso_big_sync_params g_ble_ll_iso_big_sync_params;
static struct ble_ll_iso_big *g_ble_ll_iso_big_curr;

STATS_SECT_START(ble_ll_iso_big_sync_stats)
    STATS_SECT_ENTRY(wfr_expirations)
    STATS_SECT_ENTRY(no_big)
    STATS_SECT_ENTRY(rx_ctrl_pdus)
    STATS_SECT_ENTRY(rx_data_pdus)
    STATS_SECT_ENTRY(rx_data_bytes)
    STATS_SECT_ENTRY(rx_malformed_ctrl_pdus)
    STATS_SECT_ENTRY(rx_pdus_no_mem)
STATS_SECT_END
STATS_SECT_DECL(ble_ll_iso_big_sync_stats) ble_ll_iso_big_sync_stats;

STATS_NAME_START(ble_ll_iso_big_sync_stats)
    STATS_NAME(ble_ll_iso_big_sync_stats, wfr_expirations)
    STATS_NAME(ble_ll_iso_big_sync_stats, no_big)
    STATS_NAME(ble_ll_iso_big_sync_stats, rx_ctrl_pdus)
    STATS_NAME(ble_ll_iso_big_sync_stats, rx_data_pdus)
    STATS_NAME(ble_ll_iso_big_sync_stats, rx_data_bytes)
    STATS_NAME(ble_ll_iso_big_sync_stats, rx_malformed_ctrl_pdus)
    STATS_NAME(ble_ll_iso_big_sync_stats, rx_pdus_no_mem)
STATS_NAME_END(ble_ll_iso_big_sync_stats)

static struct ble_ll_iso_big *
big_ll_iso_big_find(uint8_t big_handle)
{
    struct ble_ll_iso_big *big;

    STAILQ_FOREACH(big, &big_q, big_q_next) {
        if (big->handle == big_handle) {
            return big;
        }
    }

    return NULL;
}

static struct ble_ll_iso_big *
ble_ll_iso_big_alloc(uint8_t big_handle)
{
    struct ble_ll_iso_big *big = NULL;

    big = os_memblock_get(&mp_big);
    if (!big) {
        return NULL;
    }

    memset(big, 0, sizeof(*big));
    big->handle = big_handle;
    STAILQ_INIT(&big->bis_q);

    STAILQ_INSERT_HEAD(&big_q, big, big_q_next);

    return big;
}

static struct ble_ll_iso_bis *
ble_ll_iso_big_alloc_bis(struct ble_ll_iso_big *big)
{
    struct ble_ll_iso_bis *bis;

    bis = os_memblock_get(&mp_bis);
    if (!bis) {
        return NULL;
    }

    memset(bis, 0, sizeof(*bis));
    bis->big = big;
    big->num_bis++;

    STAILQ_INSERT_TAIL(&big->bis_q, bis, bis_q_next);

    return bis;
}

static void
ble_ll_iso_big_sync_hci_evt_established(struct ble_ll_iso_big *big, uint8_t status)
{
    struct ble_hci_ev_le_subev_big_sync_established *evt;
    struct ble_ll_iso_bis *bis;
    struct ble_hci_ev *hci_ev;
    uint32_t transport_latency;
    uint32_t iso_interval_us;
    uint8_t idx;

    hci_ev = ble_transport_alloc_evt(0);
    if (!hci_ev) {
        BLE_LL_ASSERT(0);
        /* XXX should we retry later? */
        return;
    }
    hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
    hci_ev->length = sizeof(*evt);
    if (status == 0) {
        hci_ev->length += big->num_bis * sizeof(evt->conn_handle[0]);
    }

    evt = (void *)hci_ev->data;
    memset(evt, 0, hci_ev->length);
    evt->subev_code = BLE_HCI_LE_SUBEV_BIG_SYNC_ESTABLISHED;
    evt->status = status;

    evt->big_handle = big->handle;

    if (status == 0) {
        /* Core 5.3, Vol 6, Part G, 3.2.2 */
        iso_interval_us = big->iso_interval * 1250;
        transport_latency =
            big->sync_delay +
            (big->pto * (big->nse / big->bn - big->irc) + 1) * iso_interval_us;
        if (big->framed) {
            transport_latency += big->sdu_interval;
        } else {
            transport_latency -= big->sdu_interval;
        }
        put_le24(evt->transport_latency_big, transport_latency);
        evt->nse = big->nse;
        evt->bn = big->bn;
        evt->pto = big->pto;
        evt->irc = big->irc;
        evt->max_pdu = htole16(big->max_pdu);
        evt->iso_interval = htole16(big->iso_interval);
        evt->num_bis = big->num_bis;

        idx = 0;
        STAILQ_FOREACH(bis, &big->bis_q, bis_q_next) {
            evt->conn_handle[idx] = htole16(bis->conn.handle);
            idx++;
        }
    }

    ble_ll_hci_event_send(hci_ev);
}

static void
ble_ll_iso_big_sync_hci_evt_lost(struct ble_ll_iso_big *big)
{
    struct ble_hci_ev_le_subev_big_sync_lost *evt;
    struct ble_hci_ev *hci_ev;

    hci_ev = ble_transport_alloc_evt(0);
    if (!hci_ev) {
        BLE_LL_ASSERT(0);
        /* XXX should we retry later? */
        return;
    }
    hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
    hci_ev->length = sizeof(*evt);

    evt = (void *)hci_ev->data;
    memset(evt, 0, hci_ev->length);
    evt->subev_code = BLE_HCI_LE_SUBEV_BIG_SYNC_LOST;
    evt->big_handle = big->handle;
    evt->reason = big->term_reason;

    ble_ll_hci_event_send(hci_ev);
}

static void
ble_ll_iso_big_free(struct ble_ll_iso_big *big)
{
    struct ble_ll_iso_bis *bis;

    ble_ll_sched_rmv_elem(&big->sch);
    if (ble_npl_event_is_queued(&big->event_done)) {
        ble_ll_event_remove(&big->event_done);
    }

    bis = STAILQ_FIRST(&big->bis_q);
    while (bis != NULL) {
        ble_ll_iso_conn_reset(&bis->conn);
        STAILQ_REMOVE_HEAD(&big->bis_q, bis_q_next);
        os_memblock_put(&mp_bis, bis);
        bis = STAILQ_FIRST(&big->bis_q);
    }

    STAILQ_REMOVE(&big_q, big, ble_ll_iso_big, big_q_next);
    os_memblock_put(&mp_big, big);
}

static bool
ble_ll_iso_big_sync_pending(struct ble_ll_iso_big *big)
{
    struct ble_ll_iso_big_sync_params *sync_params = &g_ble_ll_iso_big_sync_params;

    return sync_params->big == big;
}

static bool
ble_ll_iso_big_term_pending(struct ble_ll_iso_big *big)
{
    return big->term_reason != BLE_ERR_SUCCESS;
}

static void
ble_ll_iso_big_sync_done(struct ble_ll_iso_big *big, uint8_t status)
{
    struct ble_ll_iso_big_sync_params *sync_params = &g_ble_ll_iso_big_sync_params;

    sync_params->big = NULL;

    if (sync_params->syncsm != NULL) {
        ble_ll_sync_biginfo_cb_set(sync_params->syncsm, NULL, NULL);
        sync_params->syncsm = NULL;
    }

    ble_ll_iso_big_sync_hci_evt_established(big, status);

    /* Core 6.0 | Vol 4, Part E, 7.8.106
     * When the Controller establishes synchronization and if the
     * BIG_Sync_Timeout set by the Host is less than 6 × ISO_Interval, the
     * Controller shall set the timeout to 6 × ISO_Interval.
     */
    big->sync_timeout = max(big->sync_timeout, (6 * big->iso_interval + 7) / 8);
}

static int
ble_ll_iso_big_sync_phy_to_phy(uint8_t big_phy, uint8_t *phy)
{
    switch (big_phy) {
    case 0:
        *phy = BLE_PHY_1M;
        break;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_2M_PHY)
    case 1:
        *phy = BLE_PHY_2M;
        break;
#endif
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_CODED_PHY)
    case 2:
        *phy = BLE_PHY_CODED;
        break;
#endif
    default:
        return -1;
    }

    return 0;
}

/* A Control PDU announced and not received yet */
static inline bool
ctrl_pdu_expected(const struct ble_ll_iso_big *big)
{
    return big->cssn_next != big->cssn;
}

static uint32_t
subevent_offset_us(const struct ble_ll_iso_big *big,
                   const struct ble_ll_iso_bis *bis, uint8_t n)
{
    return (bis->num - 1) * big->bis_spacing + n * big->sub_interval;
}

static uint32_t
ctrl_subevent_offset_us(const struct ble_ll_iso_big *big)
{
    uint32_t offset_us;

    offset_us = (big->num_bis_big - 1) * big->bis_spacing +
                (big->nse - 1) * big->sub_interval;

    if (big->interleaved) {
        offset_us += big->bis_spacing;
    } else {
        offset_us += big->sub_interval;
    }

    return offset_us;
}

/* The longest PHY transition between the subevents of the synced BISes */
static uint32_t
max_transition_us(const struct ble_ll_iso_big *big)
{
    const struct ble_ll_iso_bis *last;
    const struct ble_ll_iso_bis *bis;
    const struct ble_ll_iso_bis *next;
    uint32_t max_us;
    uint32_t us;
    uint8_t n;

    if (big->interleaved) {
        n = 0;
    } else {
        n = big->nse - 1;
    }

    max_us = big->sub_interval;
    last = NULL;
    STAILQ_FOREACH(bis, &big->bis_q, bis_q_next) {
        next = STAILQ_NEXT(bis, bis_q_next);
        if (next != NULL) {
            us = subevent_offset_us(big, next, 0) - subevent_offset_us(big, bis, n);
            max_us = max(max_us, us);
        }
        last = bis;
    }

    us = ctrl_subevent_offset_us(big) - subevent_offset_us(big, last, big->nse - 1);

    return max(max_us, us);
}

static int
big_sync_sched_set(struct ble_ll_iso_big *big, uint32_t offset_us)
{
    uint32_t max_ww_us;
    uint32_t ww_us;
    uint32_t end_us;
    uint8_t ctrl_len;
    uint8_t phy_mode;

    big->sch.start_time = big->anchor_base_ticks;
    big->sch.remainder = big->anchor_base_rem_us;

    ble_ll_tmr_add(&big->sch.start_time, &big->sch.remainder, offset_us);

    big->event_anchor_ticks = big->sch.start_time;
    big->event_anchor_rem_us = big->sch.remainder;

    if (big->nse < 3) {
        max_ww_us = (big->iso_interval * 1250 / 2) - BLE_LL_IFS;
    } else {
        max_ww_us = big->sub_interval;
    }

    ww_us = ble_ll_utils_calc_window_widening(big->sch.start_time,
                                              big->anchor_base_ticks, big->sca);
    if (ww_us >= max_ww_us) {
        return -1;
    }

    ww_us += BLE_LL_JITTER_USECS;
    if (offset_us <= ww_us) {
        /* Receive window would open before the reference anchor */
        return 1;
    }

    /* Reset anchor base before anchor offset wraps-around.
     * This happens much earlier than the possible overflow in calculations.
     */
    if (big->anchor_offset == UINT16_MAX) {
        big->anchor_base_ticks = big->sch.start_time;
        big->anchor_base_rem_us = big->sch.remainder;
        big->anchor_base_offset_us = 0;
        big->anchor_offset = 0;
    }

    end_us = big->sync_delay;
    if (ctrl_pdu_expected(big)) {
        /* Repeated by the broadcaster until the instant */
        ctrl_len = BIG_CTRL_PDU_MAX_LEN;
        if (big->encrypted) {
            ctrl_len += BLE_LL_DATA_MIC_LEN;
        }
        phy_mode = ble_ll_phy_to_phy_mode(big->phy, BLE_HCI_LE_PHY_CODED_S8_PREF);
        end_us = ctrl_subevent_offset_us(big) + ble_ll_pdu_us(ctrl_len, phy_mode);
    }

    big->sch.end_time = big->sch.start_time + ble_ll_tmr_u2t_up(end_us) + 1;
    big->sch.start_time -= g_ble_ll_sched_offset_ticks;

    /* Adjust the start time to include window widening */
    ble_ll_tmr_sub(&big->sch.start_time, &big->sch.remainder, ww_us);

    /* Adjust end time to include window widening */
    big->sch.end_time += ble_ll_tmr_u2t_up(ww_us);

    big->wfr_us = big->tx_win_us + 2 * ww_us;

    return 0;
}

static uint16_t
bis_sync_conn_handle_get(void)
{
    uint16_t conn_handle;
    uint16_t idx;

    for (idx = 0; idx <= BLE_LL_CONN_HANDLE_IDX_MASK; idx++) {
        conn_handle = BLE_LL_CONN_HANDLE(BLE_LL_CONN_HANDLE_TYPE_BIS_SYNC, idx);
        if (ble_ll_iso_conn_find_by_handle(conn_handle) == NULL) {
            return conn_handle;
        }
    }

    /* There are fewer BISes than handles */
    BLE_LL_ASSERT(0);

    return 0;
}

static void
biginfo_func(struct ble_ll_sync_sm *syncsm, uint8_t sca, uint32_t sync_ticks,
             uint8_t sync_rem_us, const uint8_t *data, uint8_t len, void *arg)
{
    struct ble_ll_iso_big_sync_params *sync_params = &g_ble_ll_iso_big_sync_params;
    struct ble_ll_iso_params *iso_params;
    struct ble_ll_iso_big *big;
    struct ble_ll_iso_bis *bis;
    uint64_t big_counter;
    uint32_t big_offset;
    uint32_t seed_aa;
    uint8_t offset_unit;
    uint32_t u32;
    uint64_t u64;
    uint8_t status;
    uint8_t bis_cnt;
    uint8_t big_phy;
    uint32_t big_offset_us;
    uint32_t offset_us;
    uint16_t conn_handle;
    bool encrypted;
    int pdu_idx_max;
    uint8_t bis_n;
    int rc;

    big = sync_params->big;
    BLE_LL_ASSERT(big);

    ble_ll_sync_biginfo_cb_set(sync_params->syncsm, NULL, NULL);

    if (data == NULL) {
        /* The periodic sync was lost or terminated before the BIGInfo */
        status = BLE_ERR_CONN_ESTABLISHMENT;
        goto fail;
    }

    encrypted = len == 57;
    if (encrypted ^ !!big->encrypted) {
        status = BLE_ERR_ENCRYPTION_MODE;
        goto fail;
    }

    u32 = get_le32(&data[0]);
    big_offset = u32 & 0x3FFF;
    offset_unit = u32 & 0x4000;
    big->iso_interval = (u32 >> 15) & 0x0FFF;

    bis_cnt = (u32 >> 27) & 0x1F;
    big->num_bis_big = bis_cnt;
    if (bis_cnt < __builtin_popcount(sync_params->bis_mask)) {
        status = BLE_ERR_UNSUPPORTED;
        goto fail;
    }

    u32 = get_le32(&data[4]);
    big->nse = u32 & 0x1F;

    if (big->mse == 0) {
        /* The Controller can schedule reception of any number of subevents up to NSE. */
        big->mse = big->nse;
    }

    big->bn = (u32 >> 5) & 0x07;
    if (big->bn > big->mse) {
        status = BLE_ERR_INV_HCI_CMD_PARMS;
        goto fail;
    }

    if (big->bn > MYNEWT_VAL(BLE_LL_ISO_BROADCAST_SYNC_MAX_BN)) {
        status = BLE_ERR_UNSUPPORTED;
        goto fail;
    }

    big->sub_interval = (u32 >> 8) & 0x0FFFFF;
    big->pto = (u32 >> 28) & 0x0F;

    u32 = get_le32(&data[8]);
    big->bis_spacing = u32 & 0x0FFFFF;
    big->irc = (u32 >> 20) & 0x0F;
    big->max_pdu = (u32 >> 24) & 0xFF;

    /* The PHY transition between subevents takes a 16-bit time, and the PDU
     * index of a pretransmission is kept in 8 bits.
     */
    pdu_idx_max =
        big->bn * big->pto * ((int)(big->nse / big->bn) - big->irc) + big->bn - 1;
    if (big->sub_interval > UINT16_MAX || big->bis_spacing > UINT16_MAX ||
        pdu_idx_max > UINT8_MAX) {
        status = BLE_ERR_UNSUPPORTED;
        goto fail;
    }

    big->framing_mode = data[12] >> 7;

    seed_aa = get_le32(&data[13]);

    u32 = get_le32(&data[17]);
    big->sdu_interval = u32 & 0x0FFFFF;
    big->max_sdu = (u32 >> 20) & 0x0FFF;

    big->crc_init = get_le16(&data[21]);
    memcpy(big->chan_map, &data[23], BLE_LL_CHAN_MAP_LEN);
    big->chan_map[4] &= 0x1f;
    big->chan_map_used = ble_ll_utils_chan_map_used_get(big->chan_map);

    big_phy = (data[27] >> 5) & 0x07;
    if (ble_ll_iso_big_sync_phy_to_phy(big_phy, &big->phy) < 0) {
        status = BLE_ERR_UNSUPPORTED;
        goto fail;
    }

    u64 = get_le64(&data[28]);
    big->bis_counter = u64 & 0x7FFFFFFFFF;
    big->framed = (u64 >> 39) & 0x01;

    big->big_counter = big->bis_counter / big->bn;
    big->interleaved = big->sub_interval > big->bis_spacing;
    big->ctrl_aa = ble_ll_utils_calc_big_aa(seed_aa, 0);

    u32 = sync_params->bis_mask;

    /* TODO: Fix duplicated data */
    iso_params = &big->params;
    iso_params->iso_interval = big->iso_interval;
    iso_params->sdu_interval = big->sdu_interval;
    iso_params->max_sdu = big->max_sdu;
    iso_params->max_pdu = big->max_pdu;
    iso_params->bn = big->bn;
    iso_params->pte = 0;
    iso_params->framed = big->framed;
    iso_params->framing_mode = big->framing_mode;

    STAILQ_INIT(&big->bis_q);
    while (u32 != 0) {
        bis_n = __builtin_ctz(u32);
        if (bis_n >= bis_cnt) {
            /* A requested BIS is not in the BIG */
            status = BLE_ERR_UNSUPPORTED;
            goto fail;
        }

        bis = ble_ll_iso_big_alloc_bis(big);
        if (bis == NULL) {
            status = BLE_ERR_CONN_REJ_RESOURCES;
            goto fail;
        }

        bis->num = bis_n + 1;
        bis->crc_init = (big->crc_init << 8) | (bis->num);
        bis->aa = ble_ll_utils_calc_big_aa(seed_aa, bis->num);
        bis->chan_id = bis->aa ^ (bis->aa >> 16);

        /* The handle must be unique, also among the BISes of other BIGs */
        conn_handle = bis_sync_conn_handle_get();

        ble_ll_iso_conn_init(&bis->conn, conn_handle, &bis->rx, NULL);
        ble_ll_iso_rx_init(&bis->rx, &bis->conn, &big->params);

        u32 &= ~(1 << bis_n);
    }

    if (max_transition_us(big) > UINT16_MAX) {
        status = BLE_ERR_UNSUPPORTED;
        goto fail;
    }

    if (big->encrypted) {
        memcpy(big->gskd, &data[41], 16);
        ble_ll_iso_big_calculate_gsk(big, sync_params->broadcast_code);

        memcpy(big->giv, &data[33], 8);
        ble_ll_iso_big_calculate_iv(big);
    }

    big->cssn = CSSN_INITIAL;
    big->cssn_next = CSSN_INITIAL;

    /* Core 5.3, Vol 6, Part B, 4.4.6.3 */
    big->mpt = ble_ll_pdu_us(
        big->max_pdu + (big->encrypted ? 4 : 0),
        ble_ll_phy_to_phy_mode(big->phy, BLE_HCI_LE_PHY_CODED_S8_PREF));

    /* Core 5.3, Vol 6, Part B, 4.4.6.5 */
    big->sync_delay = (big->num_bis_big - 1) * big->bis_spacing +
                      (big->nse - 1) * big->sub_interval + big->mpt;

    big->sca = sca;

    /* Actual offset */
    big_offset_us = big_offset * (offset_unit ? 300 : 30);
    /* Transmit window */
    big->tx_win_us = offset_unit ? 300 : 30;

    /* The anchor base stays at the BIGInfo until a BIG PDU is received, so
     * that the window widening covers the drift since then.
     */
    big->anchor_offset = 0;
    big->anchor_base_ticks = sync_ticks;
    big->anchor_base_rem_us = sync_rem_us;
    big->anchor_base_offset_us = big_offset_us;

    big_counter = big->big_counter;
    while (true) {
        offset_us = big->anchor_offset * big->iso_interval * 1250;

        rc = big_sync_sched_set(big, big->anchor_base_offset_us + offset_us);
        if (rc < 0) {
            status = BLE_ERR_UNSPECIFIED;
            goto fail;
        }

        if (rc > 0 || LL_TMR_LEQ(big->sch.start_time, ble_ll_tmr_get())) {
            rc = -1;
        } else {
            rc = ble_ll_sched_iso_big_sync(&big->sch);
        }
        if (rc >= 0) {
            break;
        }

        big->anchor_offset++;
        big_counter++;
    }

    big->bis_counter += (big_counter - big->big_counter) * big->bn;
    big->big_counter = big_counter;

    return;
fail:
    ble_ll_iso_big_sync_done(big, status);
    ble_ll_iso_big_free(big);
}

static uint32_t sdu_sync_ref_get(struct ble_ll_iso_big *big);
static bool ble_ll_iso_big_sync_is_lost(struct ble_ll_iso_big *big);

static void
ble_ll_iso_big_sync_event_done(struct ble_ll_iso_big *big)
{
    struct ble_ll_iso_bis *bis;
    uint64_t big_counter;
    uint32_t offset_us;
    int rc;

    ble_ll_rfmgmt_release();

    /* The CSTF of an event is known only from the PDUs of that event */
    big->cstf = 0;

    if (ble_ll_iso_big_term_pending(big)) {
        goto term;
    }

    big_counter = big->big_counter;

    while (true) {
        big_counter++;
        big->anchor_offset++;

        if (big->control_active &&
            (int16_t)((uint16_t)big_counter - big->control_instant) >= 0) {
            switch (big->control_active) {
            case BIG_CONTROL_ACTIVE_TERM:
                big->term_reason = BLE_ERR_REM_USER_CONN_TERM;
                break;
            case BIG_CONTROL_ACTIVE_CHAN_MAP:
                big->chan_map_new_pending = true;
                break;
            default:
                break;
            }

            big->control_active = 0;
            big->cstf = 0;
        }

        if (ble_ll_iso_big_term_pending(big)) {
            goto term;
        }

        if (big->chan_map_new_pending) {
            big->chan_map_new_pending = false;
            memcpy(big->chan_map, big->chan_map_new, BLE_LL_CHAN_MAP_LEN);
            big->chan_map_used = ble_ll_utils_chan_map_used_get(big->chan_map);
        }

        /* Also covers the events removed from or not inserted in the
         * scheduler, for which the wait for response timer does not run
         */
        if (ble_ll_iso_big_sync_is_lost(big)) {
            big->term_reason = BLE_ERR_CONN_SPVN_TMO;
            goto term;
        }

        offset_us = big->anchor_base_offset_us +
                    big->anchor_offset * big->iso_interval * 1250;

        rc = big_sync_sched_set(big, offset_us);
        if (rc < 0) {
            big->term_reason = BLE_ERR_CONN_SPVN_TMO;
            goto term;
        }

        /* Skip the events already started, e.g. if the LL task was delayed.
         * Starting one late would receive at the wrong time on the channel
         * of this event, and a PDU of another event received there would
         * set the anchor with a wrong event counter.
         */
        if (rc > 0 || LL_TMR_LEQ(big->sch.start_time, ble_ll_tmr_get())) {
            rc = -1;
        } else {
            rc = ble_ll_sched_iso_big_sync(&big->sch);
        }
        if (rc >= 0) {
            break;
        }

        STAILQ_FOREACH(bis, &big->bis_q, bis_q_next) {
            ble_ll_iso_rx_event_start(&bis->rx, sdu_sync_ref_get(big));
            ble_ll_iso_rx_event_done(&bis->rx);
        }
    }

    big->bis_counter += (big_counter - big->big_counter) * big->bn;
    big->big_counter = big_counter;

    return;
term:
    if (ble_ll_iso_big_sync_pending(big)) {
        ble_ll_iso_big_sync_done(big, big->term_reason);
    } else if (big->term_reason != BLE_ERR_CONN_TERM_LOCAL) {
        ble_ll_iso_big_sync_hci_evt_lost(big);
    }
    ble_ll_iso_big_free(big);
}

static void
ble_ll_iso_big_sync_event_done_ev(struct ble_npl_event *ev)
{
    struct ble_ll_iso_big *big;
    struct ble_ll_iso_bis *bis;

    big = CONTAINER_OF(ev, struct ble_ll_iso_big, event_done);

    if (!ble_ll_iso_big_term_pending(big)) {
        STAILQ_FOREACH(bis, &big->bis_q, bis_q_next) {
            ble_ll_iso_rx_event_done(&bis->rx);
        }
    }

    ble_ll_iso_big_sync_event_done(big);
}

static void
ble_ll_iso_big_sync_event_done_to_ll(struct ble_ll_iso_big *big)
{
    ble_ll_event_add(&big->event_done);
}

void
ble_ll_iso_big_sync_halt(void)
{
    ble_phy_disable();
    ble_ll_state_set(BLE_LL_STATE_STANDBY);

    if (g_ble_ll_iso_big_curr) {
        ble_ll_iso_big_sync_event_done_to_ll(g_ble_ll_iso_big_curr);
        g_ble_ll_iso_big_curr = NULL;
    }
}

uint32_t
ble_ll_iso_big_sync_get_event_end_time(void)
{
    if (g_ble_ll_iso_big_curr) {
        return g_ble_ll_iso_big_curr->sch.end_time;
    }

    return ble_ll_tmr_get();
}

static uint32_t
curr_subevent_offset_us(const struct ble_ll_iso_big *big)
{
    if (big->subevents_rem > 0) {
        return subevent_offset_us(big, big->bis, big->bis->subevent_num);
    }

    return ctrl_subevent_offset_us(big);
}

static bool
next_subevent_offset_us(const struct ble_ll_iso_big *big, uint32_t *offset_us)
{
    const struct ble_ll_iso_bis *bis = big->bis;
    const struct ble_ll_iso_bis *next;
    uint8_t n;

    if (big->subevents_rem > 1) {
        if (!big->interleaved && bis->subevent_num + 1 < big->nse) {
            *offset_us = subevent_offset_us(big, bis, bis->subevent_num + 1);
        } else {
            next = STAILQ_NEXT(bis, bis_q_next);
            if (next == NULL) {
                next = STAILQ_FIRST(&big->bis_q);
            }
            n = next->subevent_num;
            if (next == bis) {
                /* The only BIS synced, interleaved: its next subevent */
                n++;
            }
            *offset_us = subevent_offset_us(big, next, n);
        }
        return true;
    }

    if (big->subevents_rem == 1) {
        *offset_us = ctrl_subevent_offset_us(big);
        return true;
    }

    return false;
}

static void
subevent_transition_set(struct ble_ll_iso_big *big)
{
    uint32_t next_us;

    if (!next_subevent_offset_us(big, &next_us)) {
        ble_phy_transition_set(BLE_PHY_TRANSITION_NONE, 0);
        return;
    }

    ble_phy_transition_set(BLE_PHY_TRANSITION_TO_RX_ISO_SUBEVENT,
                           next_us - curr_subevent_offset_us(big));
}

static uint8_t
pdu_idx_get(struct ble_ll_iso_big *big, struct ble_ll_iso_bis *bis)
{
    /* Core 5.3, Vol 6, Part B, 4.4.6.6 */
    if (bis->g < big->irc) {
        return bis->n;
    }

    /* Pretransmission */
    return big->bn * big->pto * (bis->g - big->irc + 1) + bis->n;
}

static int
ble_ll_iso_big_sync_bis_subevent_rx(struct ble_ll_iso_big *big)
{
    struct ble_ll_iso_bis *bis;
    uint16_t chan_idx;
    int rc;

    bis = big->bis;

    if (bis->subevent_num == 0) {
        chan_idx = ble_ll_utils_dci_iso_event(big->big_counter, bis->chan_id,
                                              &bis->prn_sub_lu, big->chan_map_used,
                                              big->chan_map, &bis->remap_idx);
    } else {
        chan_idx = ble_ll_utils_dci_iso_subevent(bis->chan_id, &bis->prn_sub_lu,
                                                 big->chan_map_used,
                                                 big->chan_map, &bis->remap_idx);
    }

    bis->chan_idx = chan_idx;

    rc = ble_phy_setchan(chan_idx, bis->aa, bis->crc_init);
    if (rc != 0) {
        return rc;
    }

    ble_phy_rx_maxlen_set(big->max_pdu + (big->encrypted ? BLE_LL_DATA_MIC_LEN : 0));

    if (big->encrypted) {
        ble_phy_encrypt_enable(big->gsk);
        ble_phy_encrypt_header_mask_set(BLE_LL_PDU_HEADERMASK_BIS);
        ble_phy_encrypt_iv_set(bis->iv);
        ble_phy_encrypt_counter_set(big->bis_counter + pdu_idx_get(big, bis), 1);
    } else {
        ble_phy_encrypt_disable();
    }

    return 0;
}

static int
ble_ll_iso_big_sync_ctrl_subevent_rx(struct ble_ll_iso_big *big)
{
    uint16_t chan_idx;
    uint16_t chan_id;
    uint16_t foo, bar;
    int rc;

    chan_id = big->ctrl_aa ^ (big->ctrl_aa >> 16);

    chan_idx = ble_ll_utils_dci_iso_event(big->big_counter, chan_id, &foo,
                                          big->chan_map_used, big->chan_map, &bar);

    rc = ble_phy_setchan(chan_idx, big->ctrl_aa, big->crc_init << 8);
    if (rc != 0) {
        return rc;
    }

    ble_phy_rx_maxlen_set(BLE_LL_MAX_PAYLOAD_LEN);

    if (big->encrypted) {
        ble_phy_encrypt_enable(big->gsk);
        ble_phy_encrypt_header_mask_set(BLE_LL_PDU_HEADERMASK_BIS);
        ble_phy_encrypt_iv_set(big->iv);
        ble_phy_encrypt_counter_set(big->bis_counter, 1);
    } else {
        ble_phy_encrypt_disable();
    }

    return 0;
}

static int
ble_ll_iso_big_sync_rx_start(struct ble_ll_iso_big *big, uint32_t start_time,
                             uint32_t remainder)
{
    int rc;

    rc = ble_phy_rx_set_start_time(start_time, remainder);
    if (rc != 0 && rc != BLE_PHY_ERR_RX_LATE) {
        return rc;
    }

    ble_phy_wfr_enable(BLE_PHY_WFR_ENABLE_RX, 0, big->wfr_us);

    return 0;
}

static uint32_t
sdu_sync_ref_get(struct ble_ll_iso_big *big)
{
    uint32_t timestamp;

    timestamp = ble_ll_tmr_t2u(big->event_anchor_ticks) + big->event_anchor_rem_us;
    if (big->framed) {
        timestamp += big->sync_delay + big->sdu_interval + big->iso_interval * 1250;
    } else {
        timestamp += big->sync_delay;
    }

    return timestamp;
}

void
ble_ll_iso_big_sync_rmvd_from_sched(struct ble_ll_iso_big *big)
{
    struct ble_ll_iso_bis *bis;
    uint32_t timestamp;

    timestamp = sdu_sync_ref_get(big);
    STAILQ_FOREACH(bis, &big->bis_q, bis_q_next) {
        ble_ll_iso_rx_event_start(&bis->rx, timestamp);
    }

    ble_ll_iso_big_sync_event_done_to_ll(big);
}

static int
ble_ll_iso_big_sync_event_sched_cb(struct ble_ll_sched_item *sch)
{
    struct ble_ll_iso_big *big = sch->cb_arg;
    struct ble_ll_iso_bis *bis;
    uint32_t timestamp;
#if MYNEWT_VAL(BLE_LL_PHY)
    uint8_t phy_mode;
#endif
    int rc;

    BLE_LL_ASSERT(big);

    ble_ll_state_set(BLE_LL_STATE_BIG_SYNC);
    g_ble_ll_iso_big_curr = big;

    ble_ll_whitelist_disable();
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    ble_phy_resolv_list_disable();
#endif
#if MYNEWT_VAL(BLE_LL_PHY)
    phy_mode = ble_ll_phy_to_phy_mode(big->phy, 0);
    ble_phy_mode_set(phy_mode, phy_mode);
#endif

    ble_ll_tx_power_set(g_ble_ll_tx_power);

    timestamp = sdu_sync_ref_get(big);

    /* XXX calculate this in advance at the end of a previous event? */
    big->subevents_rem = big->num_bis * big->nse;
    STAILQ_FOREACH(bis, &big->bis_q, bis_q_next) {
        ble_ll_iso_rx_event_start(&bis->rx, timestamp);

        bis->subevent_num = 0;
        bis->n = 0;
        bis->g = 0;
        bis->rx_pdus = 0;
    }

    /* Select 1st BIS for reception */
    big->bis = STAILQ_FIRST(&big->bis_q);

    rc = ble_ll_iso_big_sync_bis_subevent_rx(big);
    if (rc != 0) {
        goto done;
    }

    subevent_transition_set(big);

    rc = ble_ll_iso_big_sync_rx_start(
        big, sch->start_time + g_ble_ll_sched_offset_ticks, sch->remainder);
    if (rc != 0) {
        goto done;
    }

    return BLE_LL_SCHED_STATE_RUNNING;
done:
    ble_ll_iso_big_sync_halt();
    return BLE_LL_SCHED_STATE_DONE;
}

static int
ble_ll_iso_big_sync_create(uint8_t big_handle, struct ble_ll_iso_big **out)
{
    struct ble_ll_iso_big *big;

    big = big_ll_iso_big_find(big_handle);
    if (big) {
        return -EALREADY;
    }

    big = ble_ll_iso_big_alloc(big_handle);
    if (!big) {
        return -ENOMEM;
    }

    big->sch.sched_type = BLE_LL_SCHED_TYPE_BIG_SYNC;
    big->sch.sched_cb = ble_ll_iso_big_sync_event_sched_cb;
    big->sch.cb_arg = big;
    ble_npl_event_init(&big->event_done, ble_ll_iso_big_sync_event_done_ev, NULL);

    *out = big;

    return 0;
}

static void
ble_ll_big_sync_cancel_complete(void *user_data)
{
    struct ble_ll_iso_big *big = user_data;

    ble_ll_iso_big_sync_done(big, big->term_reason);
    ble_ll_iso_big_free(big);
}

static int
ble_ll_big_sync_terminate(uint8_t big_handle)
{
    struct ble_ll_iso_big *big;
    os_sr_t sr;

    big = big_ll_iso_big_find(big_handle);
    if (big == NULL) {
        return -ENOENT;
    }

    OS_ENTER_CRITICAL(sr);

    /* Core 6.2, Vol 4, Part E, 7.8.107 */
    if (ble_ll_iso_big_sync_pending(big)) {
        big->term_reason = BLE_ERR_OPERATION_CANCELLED;
    } else {
        big->term_reason = BLE_ERR_CONN_TERM_LOCAL;
    }

    if (big == g_ble_ll_iso_big_curr) {
        /* Terminate the BIG once BIG event is complete */
        OS_EXIT_CRITICAL(sr);
        return 0;
    }

    /* Remove the event before the scheduler can start it */
    ble_ll_sched_rmv_elem(&big->sch);

    OS_EXIT_CRITICAL(sr);

    if (ble_ll_iso_big_sync_pending(big)) {
        /* The BIG Sync Established event follows the Command Complete */
        ble_ll_hci_post_cmd_cb_set(ble_ll_big_sync_cancel_complete, big);
        return 0;
    }

    ble_ll_iso_big_free(big);

    return 0;
}

int
ble_ll_iso_big_sync_rx_isr_start(uint8_t pdu_type, struct ble_mbuf_hdr *rxhdr)
{
    struct ble_ll_iso_big *big;

    big = g_ble_ll_iso_big_curr;
    BLE_LL_ASSERT(big);

    if (ble_ll_iso_big_term_pending(big)) {
        ble_ll_iso_big_sync_halt();
        return -1;
    }

    return 0;
}

static int
ble_ll_iso_big_sync_subevent_done(struct ble_ll_iso_big *big)
{
    struct ble_ll_iso_bis *bis;

    if (big->subevents_rem > 0) {
        bis = big->bis;

        bis->n++;
        if (bis->n == big->bn) {
            bis->n = 0;
            bis->g++;
        }

        bis->subevent_num++;

        /* Switch to next BIS if interleaved or all subevents for current BIS were transmitted. */
        if (big->interleaved || (bis->subevent_num == big->nse)) {
            bis = STAILQ_NEXT(bis, bis_q_next);
            if (!bis) {
                bis = STAILQ_FIRST(&big->bis_q);
            }
            big->bis = bis;
        }

        big->subevents_rem--;
        if (big->subevents_rem > 0) {
            return ble_ll_iso_big_sync_bis_subevent_rx(big);
        }

        if (big->cstf) {
            return ble_ll_iso_big_sync_ctrl_subevent_rx(big);
        }
    } else if (big->cstf) {
        big->cstf = false;
    } else {
        BLE_LL_ASSERT(0);
    }

    return -1;
}

static int
ble_ll_iso_big_sync_data_pdu_in(uint16_t conn_handle, uint8_t idx, struct os_mbuf *rxpdu)
{
    struct ble_ll_iso_conn *conn;

    conn = ble_ll_iso_conn_find_by_handle(conn_handle);
    if (conn == NULL) {
        os_mbuf_free_chain(rxpdu);
        return 0;
    }

    BLE_LL_ASSERT(conn->rx);

    return ble_ll_iso_rx_pdu_put(conn->rx, idx, rxpdu);
}

static void
ble_ll_iso_big_sync_chan_map_ind(struct ble_ll_iso_big *big, struct os_mbuf *rxpdu)
{
    struct ble_ll_big_ctrl_chan_map_ind ind;

    if (os_mbuf_copydata(rxpdu, 1, sizeof(ind), &ind) != 0) {
        return;
    }

    if (ble_ll_utils_chan_map_used_get(ind.chan_map) < 2) {
        STATS_INC(ble_ll_iso_big_sync_stats, rx_malformed_ctrl_pdus);
        return;
    }

    big->control_active = BIG_CONTROL_ACTIVE_CHAN_MAP;
    big->control_instant = le16toh(ind.instant);
    memcpy(big->chan_map_new, ind.chan_map, sizeof(big->chan_map_new));
}

static void
ble_ll_iso_big_sync_term_ind(struct ble_ll_iso_big *big, struct os_mbuf *rxpdu)
{
    struct ble_ll_big_ctrl_term_ind ind;

    if (os_mbuf_copydata(rxpdu, 1, sizeof(ind), &ind) != 0) {
        return;
    }

    big->control_active = BIG_CONTROL_ACTIVE_TERM;
    big->control_instant = le16toh(ind.instant);
}

static void
ble_ll_iso_big_sync_ctrl_pdu_in(struct ble_ll_iso_big *big, struct os_mbuf *rxpdu)
{
    uint8_t opcode;

    if (os_mbuf_copydata(rxpdu, 0, sizeof(opcode), &opcode) != 0) {
        return;
    }

    switch (opcode) {
    case BLE_LL_BIG_CTRL_CHAN_MAP_IND:
        ble_ll_iso_big_sync_chan_map_ind(big, rxpdu);
        break;
    case BLE_LL_BIG_CTRL_TERM_IND:
        ble_ll_iso_big_sync_term_ind(big, rxpdu);
        break;
    default:
        break;
    }

    STATS_INC(ble_ll_iso_big_sync_stats, rx_ctrl_pdus);
}

int
ble_ll_iso_big_sync_rx_isr_end(uint8_t *rxbuf, struct ble_mbuf_hdr *rxhdr)
{
    int rc = 0;
    struct ble_ll_iso_big *big;
    uint8_t rx_pyld_len;
    struct os_mbuf *rxpdu = NULL;
    bool alloc_rxpdu;
    bool to_rx;

    big = g_ble_ll_iso_big_curr;
    BLE_LL_ASSERT(big);

    rx_pyld_len = rxbuf[1];

    /**
     * No need to alloc rxpdu for BIG Control PDU with invalid CRC.
     * ISO Data PDUs with CRC errors shall be reported as per Core 6.0 Vol 6, Part G.
     */
    alloc_rxpdu = BLE_MBUF_HDR_CRC_OK(rxhdr) && !BLE_MBUF_HDR_MIC_FAILURE(rxhdr);

    if (alloc_rxpdu && big->rx_bis && big->rx_pdu_idx < 32 &&
        (big->rx_bis->rx_pdus & (1UL << big->rx_pdu_idx))) {
        alloc_rxpdu = false;
    }

    if (alloc_rxpdu) {
        /* Allocate buffer to copy the Broadcast Isochronous PDU header and payload */
        rxpdu = ble_ll_rxpdu_alloc(BLE_LL_PDU_HDR_LEN + rx_pyld_len);
        if (rxpdu == NULL) {
            STATS_INC(ble_ll_iso_big_sync_stats, rx_pdus_no_mem);
        } else if (big->rx_bis && big->rx_pdu_idx < 32) {
            big->rx_bis->rx_pdus |= 1UL << big->rx_pdu_idx;
        }
    }

    if (rxpdu && big->ctrl_rx) {
        big->cssn = big->cssn_next;
    }

    if (rxpdu) {
        ble_phy_rxpdu_copy(rxbuf, rxpdu);

        /* Copy the packet header, used to dispatch data and control PDUs */
        memcpy(BLE_MBUF_HDR_PTR(rxpdu), rxhdr, sizeof(struct ble_mbuf_hdr));

        ble_ll_rx_pdu_in(rxpdu);
    }

    to_rx = big->subevents_rem > 0 || big->cstf;
    subevent_transition_set(big);

    if (!to_rx || ble_ll_iso_big_term_pending(big)) {
        ble_ll_iso_big_sync_halt();
        rc = -1;
    }

    return rc;
}

void
ble_ll_iso_big_sync_rx_isr_early_end(uint8_t *rxbuf, struct ble_mbuf_hdr *rxhdr)
{
    union iso_pdu_user_data pdu_user_data = { 0 };
    struct ble_ll_iso_big *big;
    struct ble_ll_iso_bis *bis;
    uint8_t hdr_byte;
    uint8_t cssn;
    uint8_t cstf;
    bool data_subevent;
    bool crc_ok;
    bool mic_ok;

    big = g_ble_ll_iso_big_curr;
    BLE_LL_ASSERT(big);

    crc_ok = BLE_MBUF_HDR_CRC_OK(rxhdr);
    mic_ok = !BLE_MBUF_HDR_MIC_FAILURE(rxhdr);
    data_subevent = big->subevents_rem > 0;
    big->rx_bis = NULL;
    big->ctrl_rx = 0;

    if (data_subevent && !crc_ok) {
        big->bis->rx.link_quality.crc_error_pkts++;
    }

    if (crc_ok && !mic_ok && ble_ll_iso_big_sync_pending(big)) {
        big->term_reason = BLE_ERR_CONN_TERM_MIC;
    }

    /* The wait for response timer, which checks the sync timeout, does not
     * expire if a PDU is received with a CRC error
     */
    if (!crc_ok && ble_ll_iso_big_sync_is_lost(big)) {
        big->term_reason = BLE_ERR_CONN_SPVN_TMO;
    }

    if (crc_ok) {
        /* Only a PDU with a valid CRC is known to be from the BIG, so it
         * alone sets the anchor and restarts the sync timeout.
         */
        big->anchor_offset = 0;
        big->anchor_base_offset_us = 0;
        big->anchor_base_ticks = rxhdr->beg_cputime;
        big->anchor_base_rem_us = rxhdr->rem_usecs;
        ble_ll_tmr_sub(&big->anchor_base_ticks, &big->anchor_base_rem_us,
                       curr_subevent_offset_us(big));

        hdr_byte = rxbuf[0];

        /* Control Subevent Sequence Number */
        cssn = BLE_LL_BIS_PDU_HDR_CSSN(hdr_byte);

        /* Control Subevent Transmission Flag */
        cstf = BLE_LL_BIS_PDU_HDR_CSTF(hdr_byte);

        if (cstf > 0 && cssn != big->cssn) {
            big->cstf = cstf;
            big->cssn_next = cssn;
        } else if (data_subevent && cstf == 0) {
            /* CSTF is set in all the PDUs of an event with a control subevent:
             * not announced anymore, if not received
             */
            big->cssn_next = big->cssn;
        }

        if (!data_subevent && big->cstf && mic_ok &&
            BLE_LL_BIS_LLID_IS_CTRL(hdr_byte)) {
            big->ctrl_rx = 1;
        }

        if (BLE_LL_BIS_LLID_IS_DATA(hdr_byte) && mic_ok) {
            bis = big->bis;

            /* Save the BIS connection handle and PDU index */
            rxhdr->rxinfo.handle = BLE_LL_CONN_HANDLE_IDX(bis->conn.handle);
            pdu_user_data.pdu_idx = pdu_idx_get(big, bis);

            big->rx_bis = bis;
            big->rx_pdu_idx = pdu_user_data.pdu_idx;
        }

        pdu_user_data.big_handle = big->handle;
        rxhdr->rxinfo.user_data = UINT_TO_POINTER(pdu_user_data.value);
    }

    ble_ll_iso_big_sync_subevent_done(big);
}

void
ble_ll_iso_big_sync_rx_pdu_in(struct os_mbuf **rxpdu, struct ble_mbuf_hdr *rxhdr)
{
    union iso_pdu_user_data pdu_user_data = { 0 };
    struct ble_ll_iso_big *big;
    uint8_t *rxbuf;
    uint8_t hdr_byte;
    uint8_t rx_pyld_len;

    BLE_LL_ASSERT(BLE_MBUF_HDR_CRC_OK(rxhdr));

    pdu_user_data.value = POINTER_TO_UINT(rxhdr->rxinfo.user_data);

    big = big_ll_iso_big_find(pdu_user_data.big_handle);
    if (big == NULL) {
        STATS_INC(ble_ll_iso_big_sync_stats, no_big);
        /* rxpdu will be free'd */
        return;
    }

    /* Validate rx data pdu */
    rxbuf = (*rxpdu)->om_data;
    hdr_byte = rxbuf[0];
    rx_pyld_len = rxbuf[1];

    /* An empty PDU has no MIC, so it does not prove the Broadcast_Code of
     * an encrypted BIG. A pending termination reports its own status.
     */
    if (ble_ll_iso_big_sync_pending(big) && !ble_ll_iso_big_term_pending(big) &&
        (!big->encrypted || rx_pyld_len > 0)) {
        ble_ll_iso_big_sync_done(big, BLE_ERR_SUCCESS);
    }

    if (BLE_LL_BIS_LLID_IS_CTRL(hdr_byte)) {
        os_mbuf_adj(*rxpdu, BLE_LL_PDU_HDR_LEN);
        BLE_LL_ASSERT(rx_pyld_len == os_mbuf_len(*rxpdu));
        ble_ll_iso_big_sync_ctrl_pdu_in(big, *rxpdu);
    } else if (BLE_LL_BIS_LLID_IS_DATA(hdr_byte)) {
        ble_ll_iso_big_sync_data_pdu_in(
            BLE_LL_CONN_HANDLE(BLE_LL_CONN_HANDLE_TYPE_BIS_SYNC, rxhdr->rxinfo.handle),
            pdu_user_data.pdu_idx, *rxpdu);

        /* Do not free the buffer */
        *rxpdu = NULL;
    }
}

static int
ble_ll_iso_big_sync_rx_restart(struct ble_ll_iso_big *big)
{
    uint32_t ticks;
    uint8_t rem_us;

    ticks = big->sch.start_time;
    rem_us = big->sch.remainder;
    ble_ll_tmr_add(&ticks, &rem_us, curr_subevent_offset_us(big));

    subevent_transition_set(big);

    return ble_ll_iso_big_sync_rx_start(big, ticks + g_ble_ll_sched_offset_ticks,
                                        rem_us);
}

static bool
ble_ll_iso_big_sync_is_lost(struct ble_ll_iso_big *big)
{
    uint32_t ticks;
    uint8_t rem_us;

    ticks = big->anchor_base_ticks;
    rem_us = big->anchor_base_rem_us;

    /* Until a BIG PDU is received, the timeout counts from the first event */
    ble_ll_tmr_add(&ticks, &rem_us,
                   big->anchor_base_offset_us + BIG_SYNC_TIMEOUT_US(big->sync_timeout));

    return LL_TMR_LEQ(ticks, ble_ll_tmr_get());
}

void
ble_ll_iso_big_sync_wfr_timer_exp(void)
{
    struct ble_ll_iso_big *big = g_ble_ll_iso_big_curr;
    int rc;

    STATS_INC(ble_ll_iso_big_sync_stats, wfr_expirations);

    ble_phy_disable();

    if (big == NULL) {
        return;
    }

    if (ble_ll_iso_big_term_pending(big)) {
        goto event_done;
    }

    if (ble_ll_iso_big_sync_is_lost(big)) {
        big->term_reason = BLE_ERR_CONN_SPVN_TMO;
        goto event_done;
    }

    /* PDU not received. Mark the subevent as done, prepare for the next RX if needed. */
    rc = ble_ll_iso_big_sync_subevent_done(big);
    if (rc != 0) {
        goto event_done;
    }

    /* Restart RX. Start time shall be adjusted to the next expected subevent. */
    rc = ble_ll_iso_big_sync_rx_restart(big);
    if (rc != 0) {
        goto event_done;
    }

    return;
event_done:
    ble_ll_iso_big_sync_halt();
}

int
ble_ll_iso_big_sync_hci_create(const uint8_t *cmdbuf, uint8_t len)
{
    struct ble_ll_iso_big_sync_params *sync_params = &g_ble_ll_iso_big_sync_params;
    const struct ble_hci_le_big_create_sync_cp *cmd = (void *)cmdbuf;
    struct ble_ll_sync_sm *syncsm;
    struct ble_ll_iso_big *big;
    uint16_t sync_timeout;
    uint16_t sync_handle;
    uint32_t bis_mask;
    uint8_t bis;
    int rc;
    int i;

    if (len < sizeof(*cmd) ||
        len != sizeof(*cmd) + cmd->num_bis * sizeof(cmd->bis[0])) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (sync_params->big) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    sync_timeout = le16toh(cmd->sync_timeout);
    sync_handle = le16toh(cmd->sync_handle);

    if (!IN_RANGE(cmd->big_handle, 0x00, 0xef) ||
        !IN_RANGE(sync_handle, 0x00, 0x0eff) || !IN_RANGE(cmd->mse, 0x00, 0x1f) ||
        !IN_RANGE(sync_timeout, 0x000a, 0x4000) ||
        !IN_RANGE(cmd->num_bis, 0x01, 0x1f) || (cmd->encryption) > 1) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (cmd->num_bis > MYNEWT_VAL(BLE_LL_ISO_BROADCAST_SYNC_MAX_BIS)) {
        return BLE_ERR_CONN_REJ_RESOURCES;
    }

    syncsm = ble_ll_sync_get(sync_handle);
    if (!syncsm) {
        return BLE_ERR_UNK_ADV_INDENT;
    }

    bis_mask = 0;
    for (i = 0; i < cmd->num_bis; i++) {
        bis = cmd->bis[i];
        if (!IN_RANGE(bis, 0x01, 0x1f)) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        bis_mask |= 1 << (bis - 1);
    }

    rc = ble_ll_iso_big_sync_create(cmd->big_handle, &big);
    if (rc == -EALREADY) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    if (rc == -ENOMEM) {
        return BLE_ERR_CONN_REJ_RESOURCES;
    }

    if (big == NULL) {
        return BLE_ERR_UNSPECIFIED;
    }

    memcpy(sync_params->broadcast_code, cmd->broadcast_code,
           sizeof(sync_params->broadcast_code));
    sync_params->big = big;
    sync_params->syncsm = syncsm;
    sync_params->bis_mask = bis_mask;

    big->sync_timeout = sync_timeout;
    big->encrypted = cmd->encryption ? 1 : 0;
    big->mse = cmd->mse;

    ble_ll_sync_biginfo_cb_set(sync_params->syncsm, biginfo_func, NULL);

    return 0;
}

int
ble_ll_iso_big_sync_hci_terminate(const uint8_t *cmdbuf, uint8_t len,
                                  uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_big_terminate_sync_cp *cmd = (void *)cmdbuf;
    struct ble_hci_le_big_terminate_sync_rp *rsp = (void *)rspbuf;
    int err;

    if (len != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    rsp->big_handle = cmd->big_handle;
    *rsplen = sizeof(*rsp);

    err = ble_ll_big_sync_terminate(cmd->big_handle);
    switch (err) {
    case 0:
        break;
    case -ENOENT:
        return BLE_ERR_UNK_ADV_INDENT;
    default:
        return BLE_ERR_UNSPECIFIED;
    }

    return 0;
}

void
ble_ll_iso_big_sync_init(void)
{
    int rc;

    /* Register ISO BIG sync statistics */
    rc = stats_init_and_reg(
        STATS_HDR(ble_ll_iso_big_sync_stats),
        STATS_SIZE_INIT_PARMS(ble_ll_iso_big_sync_stats, STATS_SIZE_32),
        STATS_NAME_INIT_PARMS(ble_ll_iso_big_sync_stats), "ble_ll_iso_big_sync");
    BLE_LL_ASSERT(rc == 0);

    rc = os_mempool_init(&mp_big, BIG_POOL_SIZE, sizeof(struct ble_ll_iso_big),
                         mb_big, "sbig");
    BLE_LL_ASSERT(rc == 0);
    rc = os_mempool_init(&mp_bis, BIS_POOL_SIZE, sizeof(struct ble_ll_iso_bis),
                         mb_bis, "sbis");
    BLE_LL_ASSERT(rc == 0);

    STAILQ_INIT(&big_q);
}

void
ble_ll_iso_big_sync_reset(void)
{
    struct ble_ll_iso_big *big;

    /* Reset statistics */
    STATS_RESET(ble_ll_iso_big_sync_stats);

    big = STAILQ_FIRST(&big_q);
    while (big) {
        ble_ll_iso_big_free(big);
        big = STAILQ_FIRST(&big_q);
    }

    memset(&g_ble_ll_iso_big_sync_params, 0, sizeof(g_ble_ll_iso_big_sync_params));
    g_ble_ll_iso_big_curr = NULL;
}

#endif /* BLE_LL_ISO_BROADCAST_SYNC */
