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

#include <errno.h>
#include <stdint.h>
#include <syscfg/syscfg.h>
#include <nimble/ble.h>
#include <nimble/ble_hci_trans.h>
#include <nimble/hci_common.h>
#include <controller/ble_ll.h>
#include <controller/ble_ll_adv.h>
#include <controller/ble_ll_hci.h>
#include <controller/ble_ll_iso_big.h>
#include <controller/ble_ll_sched.h>
#include <controller/ble_ll_tmr.h>
#include <controller/ble_ll_utils.h>
#include <controller/ble_ll_whitelist.h>

#if MYNEWT_VAL(BLE_LL_ISO_BROADCASTER)

/* XXX make those configurable */
#define BIG_POOL_SIZE           (10)
#define BIS_POOL_SIZE           (20)

#define BIG_HANDLE_INVALID      (0xff)

struct ble_ll_iso_big;

struct ble_ll_iso_bis {
    struct ble_ll_iso_big *big;
    uint8_t num;
    uint16_t handle;
    uint64_t payload_cntr;

    uint32_t aa;
    uint32_t crc_init;
    uint16_t chan_id;

    struct {
        uint16_t prn_sub_lu;
        uint16_t remap_idx;

        uint8_t subevent_num;
        uint8_t n;
        uint8_t g;
    } tx;


    STAILQ_ENTRY(ble_ll_iso_bis) bis_q_next;
};

STAILQ_HEAD(ble_ll_iso_bis_q, ble_ll_iso_bis);

struct big_params {
    uint8_t bn; /* 1-7, mandatory 1 */
    uint8_t pto; /* 0-15, mandatory 0 */
    uint8_t irc; /* 1-15  */
    uint8_t nse; /* 1-31 */
    uint32_t sdu_interval;
    uint16_t iso_interval;
    uint16_t max_transport_latency;
    uint16_t max_sdu;
    uint8_t max_pdu;
    uint8_t interleaved : 1;
    uint8_t framed : 1;
    uint8_t encrypted : 1;
};

struct ble_ll_iso_big {
    struct ble_ll_adv_sm *advsm;

    uint8_t handle;
    uint8_t num_bis;
    uint16_t iso_interval;
    uint16_t bis_spacing;
    uint16_t sub_interval;
    uint8_t max_pdu;
    uint16_t max_sdu;
    uint16_t mpt;
    uint8_t bn; /* 1-7, mandatory 1 */
    uint8_t pto; /* 0-15, mandatory 0 */
    uint8_t irc; /* 1-15  */
    uint8_t nse; /* 1-31 */
    uint8_t interleaved : 1;
    uint8_t framed : 1;
    uint8_t encrypted : 1;
    uint8_t gc;

    uint32_t sdu_interval;

    uint32_t ctrl_aa;
    uint16_t crc_init;
    uint8_t chan_map[5];
    uint8_t chan_map_used;

    uint8_t biginfo[33];

    uint64_t big_counter;
    uint32_t bis_counter;

    uint32_t sync_delay;
    uint32_t event_start;
    uint8_t event_start_us;
    struct ble_ll_sched_item sch;
    struct ble_npl_event event_done;

    struct {
        uint16_t subevents_rem;
        uint8_t xxx;
        struct ble_ll_iso_bis *bis;
    } tx;

    struct ble_ll_iso_bis_q bis_q;
};

static struct ble_ll_iso_big big_pool[BIG_POOL_SIZE];
static struct ble_ll_iso_bis bis_pool[BIS_POOL_SIZE];
static uint8_t big_pool_free = BIG_POOL_SIZE;
static uint8_t bis_pool_free = BIS_POOL_SIZE;

static struct ble_ll_iso_big *big_pending;
static struct ble_ll_iso_big *big_active;

static void
ble_ll_iso_big_biginfo_calc(struct ble_ll_iso_big *big, uint32_t seed_aa)
{
    uint8_t *buf;

    buf = big->biginfo;

    /* big_offset, big_offset_units, iso_interval, num_bis */
    put_le32(buf, (big->num_bis << 27) | (big->iso_interval << 15));
    buf += 4;

    /* nse, bn */
    *(uint8_t *)buf = (big->bn << 5) | (big->nse);
    buf += 1;

    /* sub_interval, pto */
    put_le24(buf,(big->pto << 20) | (big->sub_interval));
    buf += 3;

    /* bis_spacing, irc */
    put_le24(buf, (big->irc << 20) | (big->bis_spacing));
    buf += 3;

    /* max_pdu, rfu */
    put_le16(buf, big->max_pdu);
    buf += 2;

    /* seed_access_address */
    put_le32(buf, seed_aa);
    buf += 4;

    /* sdu_interval, max_sdu */
    put_le32(buf, (big->max_sdu << 20) | (big->sdu_interval));
    buf += 4;

    /* base_crc_init */
    put_le16(buf, big->crc_init);
    buf += 2;

    /* chm, phy */
    memcpy(buf, big->chan_map, 5);
    buf += 5;

    /* bis_payload_cnt, framing */
    memset(buf, 0x00, 5);
}

int
ble_ll_iso_big_biginfo_copy(struct ble_ll_iso_big *big, uint8_t *dptr,
                            uint32_t base_ticks, uint8_t base_rem_us)
{
    uint64_t counter;
    uint32_t offset_us;
    uint32_t offset;
    uint32_t d_ticks;
    uint8_t d_rem_us;

    counter = big->big_counter * big->bn;

    d_ticks = big->event_start - base_ticks;
    d_rem_us = big->event_start_us;
    ble_ll_tmr_sub(&d_ticks, &d_rem_us, base_rem_us);

    offset_us = ble_ll_tmr_t2u(d_ticks) + d_rem_us;
    if (offset_us <= 600) {
        counter++;
        offset_us += big->iso_interval * 1250;
    }
    if (offset_us >= 491460) {
        offset = 0x4000 | (offset_us / 300);
    } else {
        offset = offset_us / 30;
    }

    *dptr++ = 1 + (big->encrypted ? 57 : 33);
    *dptr++ = 0x2c;

    memcpy(dptr, big->biginfo, 33);
    put_le32(dptr, get_le32(dptr) | (offset & 0x7fff));
    dptr += 28;

    *dptr++ = counter & 0xff;
    *dptr++ = (counter >> 8) & 0xff;
    *dptr++ = (counter >> 16) & 0xff;
    *dptr++ = (counter >> 24) & 0xff;
    *dptr++ = (counter >> 32) & 0xff;

    if (big->encrypted) {
        dptr += 8;
        dptr += 16;
    }

    return 0;
}

int
ble_ll_iso_big_biginfo_len(struct ble_ll_iso_big *big)
{
    return 2 + (big->encrypted ? 57 : 33);
}

static void
ble_ll_iso_big_update_event_start(struct ble_ll_iso_big *big)
{
    os_sr_t sr;

    OS_ENTER_CRITICAL(sr);
    big->event_start = big->sch.start_time + g_ble_ll_sched_offset_ticks;
    big->event_start_us = big->sch.remainder;
    OS_EXIT_CRITICAL(sr);
}

static void
ble_ll_iso_big_event_done(struct ble_ll_iso_big *big)
{
    int rc;

    big->sch.start_time = big->event_start;
    big->sch.remainder = big->event_start_us;

    do {
        big->big_counter++;
        big->bis_counter += big->bn;

        /* XXX precalculate some data here? */

        ble_ll_tmr_add(&big->sch.start_time, &big->sch.remainder,
                       big->iso_interval * 1250);
        big->sch.end_time = big->sch.start_time +
                            ble_ll_tmr_u2t_up(big->sync_delay) + 1;
        big->sch.start_time -= g_ble_ll_sched_offset_ticks;

        rc = ble_ll_sched_iso_big(&big->sch, 0);
    } while (rc < 0);

    ble_ll_iso_big_update_event_start(big);
}

static void
ble_ll_iso_big_event_done_ev(struct ble_npl_event *ev)
{
    struct ble_ll_iso_big *big;

    big = ble_npl_event_get_arg(ev);

    ble_ll_iso_big_event_done(big);
}

static void
ble_ll_iso_big_event_done_to_ll(struct ble_ll_iso_big *big)
{
    big_active = NULL;
    ble_ll_state_set(BLE_LL_STATE_STANDBY);
    ble_npl_eventq_put(&g_ble_ll_data.ll_evq, &big->event_done);
}

static uint8_t
ble_ll_iso_big_subevent_pdu_cb(uint8_t *dptr, void *arg, uint8_t *hdr_byte)
{
    struct ble_ll_iso_big *big;
    struct ble_ll_iso_bis *bis;
    uint32_t counter_tx;

    big = arg;
    bis = big->tx.bis;

    /* Core 5.3, Vol 6, Part B, 4.4.6.6 */
    if (bis->tx.g < big->irc) {
        counter_tx = big->bis_counter + bis->tx.n;
    } else {
        counter_tx = big->bis_counter + big->pto * (bis->tx.g - big->irc + 1);
    }

    *hdr_byte = 0x00;

    /* XXX dummy data for testing */
    memset(dptr, bis->num | (bis->num << 4), big->max_pdu);
    dptr[0] = bis->tx.g;
    dptr[1] = bis->tx.n;
    put_be32(dptr + 2, counter_tx);
    dptr[6] = 0xff;

    return big->max_pdu;
}

static int
ble_ll_iso_big_subevent_tx(struct ble_ll_iso_big *big)
{
    struct ble_ll_iso_bis *bis;
    uint16_t chan_idx;
    int rc;

    bis = big->tx.bis;

    if (bis->tx.subevent_num == 1) {
        chan_idx = ble_ll_utils_dci_iso_event(big->big_counter, bis->chan_id,
                                              &bis->tx.prn_sub_lu,
                                              big->chan_map_used,
                                              big->chan_map,
                                              &bis->tx.remap_idx);
    } else {
        chan_idx = ble_ll_utils_dci_iso_subevent(bis->chan_id,
                                                 &bis->tx.prn_sub_lu,
                                                 big->chan_map_used,
                                                 big->chan_map,
                                                 &bis->tx.remap_idx);
    }

    ble_phy_setchan(chan_idx, bis->aa, bis->crc_init);

    rc = ble_phy_tx(ble_ll_iso_big_subevent_pdu_cb, big,
                    big->tx.subevents_rem > 1 ? BLE_PHY_TRANSITION_TX_TX
                                              : BLE_PHY_TRANSITION_NONE);
    return rc;
}

static void
ble_ll_iso_big_subevent_txend_cb(void *arg)
{
    struct ble_ll_iso_big *big;
    struct ble_ll_iso_bis *bis;
    int rc;

    big = arg;
    bis = big->tx.bis;

    bis->tx.n++;
    if (bis->tx.n == big->bn) {
        bis->tx.n = 0;
        bis->tx.g++;
    }

    /* Switch to next BIS if interleaved or all subevents for current BIS were
     * transmitted.
     */
    if (big->interleaved || (bis->tx.subevent_num == big->nse)) {
        bis = STAILQ_NEXT(bis, bis_q_next);
        if (!bis) {
            bis = STAILQ_FIRST(&big->bis_q);
        }
        big->tx.bis = bis;
    }

    bis->tx.subevent_num++;

    big->tx.subevents_rem--;

    if (big->tx.subevents_rem > 0) {
        rc = ble_ll_iso_big_subevent_tx(big);
        if (rc) {
            ble_phy_disable();
            ble_ll_iso_big_event_done_to_ll(big);
        }
        return;
    }

    ble_ll_iso_big_event_done_to_ll(big);
}

static int
ble_ll_iso_big_event_sched_cb(struct ble_ll_sched_item *sch)
{
    struct ble_ll_iso_big *big;
    struct ble_ll_iso_bis *bis;
    int rc;

    big = sch->cb_arg;

    ble_ll_state_set(BLE_LL_STATE_BIG);
    big_active = big;

    ble_ll_whitelist_disable();
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    ble_phy_resolv_list_disable();
#endif
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_ENCRYPTION)
    /* TODO add encryption support */
    ble_phy_encrypt_disable();
#endif
#if (BLE_LL_BT5_PHY_SUPPORTED == 1)
    /* TODO add support for more phys */
    ble_phy_mode_set(BLE_PHY_MODE_1M, BLE_PHY_MODE_1M);
#endif

    /* XXX calculate this in advance at the end of previous event? */
    big->tx.subevents_rem = big->num_bis * big->nse;
    big->tx.bis = STAILQ_FIRST(&big->bis_q);
    STAILQ_FOREACH(bis, &big->bis_q, bis_q_next) {
        bis->tx.subevent_num = 1;
        bis->tx.n = 0;
        bis->tx.g = 0;
    }

    rc = ble_phy_tx_set_start_time(sch->start_time + g_ble_ll_sched_offset_ticks,
                                   sch->remainder);
    if (rc) {
        ble_phy_disable();
        ble_ll_iso_big_event_done_to_ll(big);
        return BLE_LL_SCHED_STATE_DONE;
    }

    ble_phy_set_txend_cb(ble_ll_iso_big_subevent_txend_cb, big);

    rc = ble_ll_iso_big_subevent_tx(big);
    if (rc) {
        ble_phy_disable();
        ble_ll_iso_big_event_done_to_ll(big);
        return BLE_LL_SCHED_STATE_DONE;
    }

    return BLE_LL_SCHED_STATE_RUNNING;
}

static int
ble_ll_iso_big_create(uint8_t big_handle, uint8_t adv_handle, uint8_t num_bis,
                      struct big_params *bp)
{
    struct ble_ll_iso_big *big = NULL;
    struct ble_ll_iso_bis *bis;
    struct ble_ll_adv_sm *advsm;
    uint32_t seed_aa;
    uint8_t idx;
    int rc;

    if ((big_pool_free == 0) || (bis_pool_free < num_bis)) {
        return -ENOMEM;
    }

    /* Find free BIG */
    for (idx = 0; idx < BIG_POOL_SIZE; idx++) {
        if (!big && big_pool[idx].handle == BIG_HANDLE_INVALID) {
            big = &big_pool[idx];
        }
        if (big_pool[idx].handle == big_handle) {
            return -EALREADY;
        }
    }

    BLE_LL_ASSERT(big);

    advsm = ble_ll_adv_sync_get(adv_handle);
    if (!advsm) {
        return -ENOENT;
    }

    if (ble_ll_adv_sync_add_big(advsm, big) < 0) {
        return -ENOENT;
    }

    big->advsm = advsm;
    big->handle = big_handle;

    seed_aa = ble_ll_utils_calc_seed_aa();
    big->ctrl_aa = ble_ll_utils_calc_big_aa(seed_aa, 0);
    big->crc_init = ble_ll_rand();
    big->chan_map[0] = 0xff;
    big->chan_map[1] = 0xff;
    big->chan_map[2] = 0xff;
    big->chan_map[3] = 0xff;
    big->chan_map[4] = 0x1f;
    big->chan_map_used = 37;

    big->big_counter = 0;
    big->bis_counter = 0;

    /* Allocate BISes */
    STAILQ_INIT(&big->bis_q);
    big->num_bis = 0;
    for (idx = 0; (big->num_bis < num_bis) && (idx < BIS_POOL_SIZE); idx++) {
        bis = &bis_pool[idx];
        if (bis->big) {
            continue;
        }

        big->num_bis++;
        STAILQ_INSERT_TAIL(&big->bis_q, bis, bis_q_next);

        bis->big = big;
        bis->num = big->num_bis;
        bis->aa = ble_ll_utils_calc_big_aa(seed_aa, big->num_bis);
        bis->crc_init = (big->crc_init << 8) | (big->num_bis);
        bis->chan_id = bis->aa ^ (bis->aa >> 16);
    }

    big_pool_free--;
    bis_pool_free -= num_bis;

    big->bn = bp->bn;
    big->pto = bp->pto;
    big->irc = bp->irc;
    big->nse = bp->nse;
    big->interleaved = bp->interleaved;
    big->framed = bp->framed;
    big->encrypted = bp->encrypted;
    big->sdu_interval = bp->sdu_interval;
    big->iso_interval = bp->iso_interval;
    big->max_sdu = bp->max_sdu;
    big->max_pdu = bp->max_pdu;
    big->mpt = ble_ll_pdu_tx_time_get(big->max_pdu, BLE_PHY_MODE_1M);

    /* Core 5.3, Vol 6, Part B, 4.4.6.4 */
    if (big->interleaved) {
        big->bis_spacing = big->mpt + BLE_LL_MSS;
        big->sub_interval = big->num_bis * big->bis_spacing;
    } else {
        big->sub_interval = big->mpt + BLE_LL_MSS;
        big->bis_spacing = big->nse * big->sub_interval;
    }
    big->sync_delay = (big->num_bis - 1) * big->bis_spacing +
                      (big->nse - 1) * big->sub_interval + big->mpt;

    /* Core 5.3, Vol 6, Part B, 4.4.6.6 */
    big->gc = big->nse / big->bn;
    if (big->irc == big->gc) {
        big->pto = 0;
    }

    ble_ll_iso_big_biginfo_calc(big, seed_aa);

    /* For now we will schedule complete event as single item. This allows for
     * shortest possible subevent space (150us) but can create sequence of long
     * events that will block scheduler from other activities. To mitigate this
     * we use preempt_none strategy so scheudling is opportunistic and depending
     * on other activities this may create gaps in stream.
     * Eventually we should allow for some more robust scheduling, e.g. per-BIS
     * for sequential arrangement or per-subevent for interleaved, or event
     * per-BIS-subevent but this requires larger subevent space since 150us is
     * not enough for some phys to run scheduler item.
     */

    /* Schedule 1st event a bit in future */
    big->sch.start_time = ble_ll_tmr_get() + ble_ll_tmr_u2t(1000);
    big->sch.remainder = 0;
    big->sch.end_time = big->sch.start_time +
                        ble_ll_tmr_u2t_up(big->sync_delay) + 1;
    big->sch.start_time -= g_ble_ll_sched_offset_ticks;

    rc = ble_ll_sched_iso_big(&big->sch, 1);
    if (rc < 0) {
        /* TODO free resources */
        return -ENOMEM;
    }

    ble_ll_iso_big_update_event_start(big);

    big_pending = big;

    return 0;
}

void
ble_ll_iso_big_hci_evt_complete(void)
{
    struct ble_ll_iso_big *big;
    struct ble_ll_iso_bis *bis;
    struct ble_hci_ev_le_subev_create_big_complete *evt;
    struct ble_hci_ev *hci_ev;
    uint8_t idx;

    big = big_pending;
    big_pending = NULL;

    if (!big) {
        return;
    }

    hci_ev = (void *)ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_HI);
    if (!hci_ev) {
        BLE_LL_ASSERT(0);
        /* XXX should we retry later? */
        return;
    }
    hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
    hci_ev->length = sizeof(*evt) + big->num_bis * sizeof(evt->bis[0]);

    evt = (void *)hci_ev->data;
    memset(evt, 0, hci_ev->length);
    evt->subev_code = BLE_HCI_LE_SUBEV_CREATE_BIG_COMPLETE;
    evt->status = 0x00;

    evt->big_handle = big->handle;
    put_le24(evt->big_sync_delay, big->sync_delay);
    put_le24(evt->transport_latency, big->sync_delay);
    evt->phy = 0x01;
    evt->nse = big->nse;
    evt->bn = big->bn;
    evt->pto = big->pto;
    evt->irc = big->irc;
    evt->max_pdu = htole16(big->max_pdu);
    evt->iso_interval = htole16(big->iso_interval);
    evt->bis_cnt = big->num_bis;

    idx = 0;
    STAILQ_FOREACH(bis, &big->bis_q, bis_q_next) {
        evt->bis[idx] = htole16(bis->handle);
        idx++;
    }

    ble_ll_hci_event_send(hci_ev);
}

int
ble_ll_iso_big_hci_create(const uint8_t *cmdbuf, uint8_t len)
{
    const struct ble_hci_le_create_big_cp *cmd = (void *)cmdbuf;
    struct big_params bp;

    if (len != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (!IN_RANGE(cmd->big_handle, 0x00, 0xef) ||
        !IN_RANGE(cmd->adv_handle, 0x00, 0xef) ||
        !IN_RANGE(cmd->num_bis, 0x01, 0x1f) ||
        !IN_RANGE(get_le24(cmd->sdu_interval), 0x0000ff, 0x0fffff) ||
        !IN_RANGE(le16toh(cmd->max_sdu), 0x0001, 0x0fff) ||
        !IN_RANGE(le16toh(cmd->max_transport_latency), 0x0005, 0x0fa0) ||
        !IN_RANGE(cmd->rtn, 0x00, 0x1e) ||
        (cmd->packing > 1) || (cmd->framing > 1) || (cmd->encryption) > 1) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    bp.sdu_interval = get_le24(cmd->sdu_interval);
    bp.max_transport_latency = le16toh(cmd->max_transport_latency);
    bp.max_sdu = le16toh(cmd->max_sdu);
    bp.interleaved = cmd->packing;
    bp.framed = cmd->framing;
    bp.encrypted = cmd->encryption;

    /* TODO calculate stuff */
    (void)bp;

    return BLE_ERR_CONN_REJ_RESOURCES;
}

int
ble_ll_iso_big_hci_create_test(const uint8_t *cmdbuf, uint8_t len)
{
    const struct ble_hci_le_create_big_test_cp *cmd = (void *)cmdbuf;
    struct big_params bp;
    int err;

    if (len != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (!IN_RANGE(cmd->big_handle, 0x00, 0xef) ||
        !IN_RANGE(cmd->adv_handle, 0x00, 0xef) ||
        !IN_RANGE(cmd->num_bis, 0x01, 0x1f) ||
        !IN_RANGE(get_le24(cmd->sdu_interval), 0x0000ff, 0x0fffff) ||
        !IN_RANGE(le16toh(cmd->iso_interval), 0x0004, 0x0c80) ||
        !IN_RANGE(cmd->nse, 0x01, 0x1f) ||
        !IN_RANGE(le16toh(cmd->max_sdu), 0x0001, 0x0fff) ||
        !IN_RANGE(le16toh(cmd->max_pdu), 0x0001, 0x00fb) ||
        /* phy */
        (cmd->packing > 1) || (cmd->framing > 1) ||
        !IN_RANGE(cmd->bn, 0x01, 0x07) ||
        !IN_RANGE(cmd->irc, 0x01, 0x0f) ||
        !IN_RANGE(cmd->pto, 0x00, 0x0f) ||
        (cmd->encryption) > 1) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    bp.bn = cmd->bn;
    bp.pto = cmd->pto;
    bp.irc = cmd->irc;
    bp.nse = cmd->nse;
    bp.sdu_interval = get_le24(cmd->sdu_interval);
    bp.iso_interval = le16toh(cmd->iso_interval);
    bp.max_sdu = le16toh(cmd->max_sdu);
    bp.max_pdu = le16toh(cmd->max_pdu);
    bp.interleaved = cmd->packing;
    bp.framed = cmd->framing;
    bp.encrypted = cmd->encryption;

    err = ble_ll_iso_big_create(cmd->big_handle, cmd->adv_handle, cmd->num_bis,
                                &bp);
    switch (err) {
    case 0:
        break;
    case -EINVAL:
        return BLE_ERR_INV_HCI_CMD_PARMS;
    case -ENOMEM:
        return BLE_ERR_CONN_REJ_RESOURCES;
    case -ENOENT:
        return BLE_ERR_UNK_ADV_INDENT;
    default:
        return BLE_ERR_UNSPECIFIED;
    }

    return 0;
}

int
ble_ll_iso_big_hci_terminate(const uint8_t *cmdbuf, uint8_t len)
{
    return BLE_ERR_UNSUPPORTED;
}

void
ble_ll_iso_big_init(void)
{
    struct ble_ll_iso_big *big;
    struct ble_ll_iso_bis *bis;
    uint8_t idx;

    memset(big_pool, 0, sizeof(big_pool));
    memset(bis_pool, 0, sizeof(bis_pool));

    for (idx = 0; idx < BIG_POOL_SIZE; idx++) {
        big = &big_pool[idx];

        big->handle = BIG_HANDLE_INVALID;

        big->sch.sched_type = BLE_LL_SCHED_TYPE_BIG;
        big->sch.sched_cb = ble_ll_iso_big_event_sched_cb;
        big->sch.cb_arg = big;

        ble_npl_event_init(&big->event_done, ble_ll_iso_big_event_done_ev, big);
    }

    for (idx = 0; idx < BIS_POOL_SIZE; idx++) {
        bis = &bis_pool[idx];
        bis->handle = 0x0100 | idx;
    }
}

#endif /* BLE_LL_ISO_BROADCASTER */
