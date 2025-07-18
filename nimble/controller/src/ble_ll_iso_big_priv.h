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

#ifndef H_BLE_LL_ISO_BIG_PRIV_
#define H_BLE_LL_ISO_BIG_PRIV_

#include "ble_ll_iso_priv.h"
#include <controller/ble_ll.h>
#include <controller/ble_ll_iso.h>
#include <controller/ble_ll_sched.h>
#include <sys/queue.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ble_ll_iso_big;

struct ble_ll_iso_bis {
    struct ble_ll_iso_big *big;
    uint8_t num;

    uint32_t aa;
    uint32_t crc_init;
    uint16_t chan_id;
    uint16_t chan_idx;
    uint8_t iv[8];

    uint16_t prn_sub_lu;
    uint16_t remap_idx;

    /**
     * Core 6.0, Vol 6, Part B, 4.4.6.6
     * The subevents of each BIS event are partitioned into groups of BN subevents each.
     * The groups of subevents are numbered using g from 0 to GC – 1 in order, where GC = NSE ÷ BN.
     */
    uint8_t subevent_num;
    /* subevent number within group */
    uint8_t n;
    /* group number */
    uint8_t g;

    union {
        struct ble_ll_iso_rx rx;
        struct ble_ll_iso_tx tx;
    };
    struct ble_ll_iso_conn conn;

    STAILQ_ENTRY(ble_ll_iso_bis) bis_q_next;
};

STAILQ_HEAD(ble_ll_iso_bis_q, ble_ll_iso_bis);

struct ble_ll_iso_big {
    struct ble_ll_adv_sm *advsm;

    uint8_t handle;
    uint8_t num_bis;
    uint16_t iso_interval;
    uint16_t bis_spacing;
    uint16_t sub_interval;
    uint8_t phy;
    uint8_t max_pdu;
    uint16_t max_sdu;
    uint16_t mpt;
    uint8_t bn;  /* 1-7, mandatory 1 */
    uint8_t pto; /* 0-15, mandatory 0 */
    uint8_t irc; /* 1-15  */
    uint8_t nse; /* 1-31 */
    uint8_t interleaved : 1;
    uint8_t framed : 1;
    uint8_t framing_mode : 1;
    uint8_t encrypted : 1;
    uint8_t giv[8];
    uint8_t gskd[16];
    uint8_t gsk[16];
    uint8_t iv[8];
    uint8_t gc;

    uint32_t sdu_interval;

    uint32_t ctrl_aa;
    uint16_t crc_init;
    uint8_t chan_map[BLE_LL_CHAN_MAP_LEN];
    uint8_t chan_map_used;

    uint8_t biginfo[33];

    uint64_t big_counter;
    uint64_t bis_counter;

    uint32_t sync_delay;
    uint32_t transport_latency_us;
    uint32_t event_start;
    uint8_t event_start_us;
    uint32_t anchor_base_ticks;
    uint8_t anchor_base_rem_us;
    uint16_t anchor_offset;
    struct ble_ll_sched_item sch;
    struct ble_npl_event event_done;

    uint16_t subevents_rem;
    struct ble_ll_iso_bis *bis;

    struct ble_ll_iso_bis_q bis_q;

#if MYNEWT_VAL(BLE_LL_ISO_HCI_FEEDBACK_INTERVAL_MS)
    uint32_t last_feedback;
#endif

    uint8_t cstf : 1;
    uint8_t cssn : 4;
    uint8_t control_active : 3;
    uint16_t control_instant;

    uint8_t chan_map_new_pending : 1;
    uint8_t chan_map_new[BLE_LL_CHAN_MAP_LEN];

    uint8_t term_pending : 1;
    uint8_t term_reason : 7;

    uint32_t sync_timeout;
    uint8_t mse;

    uint8_t sca;
    uint16_t wfr_us;

    uint16_t tx_win_us;

    struct ble_ll_iso_params params;

    STAILQ_ENTRY(ble_ll_iso_big) big_q_next;
};

void ble_ll_iso_big_calculate_gsk(struct ble_ll_iso_big *big,
                                  const uint8_t *broadcast_code);

void ble_ll_iso_big_calculate_iv(struct ble_ll_iso_big *big);

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_LL_ISO_BIG_PRIV_ */
