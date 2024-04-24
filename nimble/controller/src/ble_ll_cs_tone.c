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

#include <syscfg/syscfg.h>
#if MYNEWT_VAL(BLE_LL_CHANNEL_SOUNDING)
#include <stdint.h>
#include "controller/ble_ll.h"
#include "controller/ble_ll_sched.h"
#include "controller/ble_ll_tmr.h"
#include "ble_ll_priv.h"
#include "ble_ll_cs_priv.h"

static uint8_t
no_pdu_make(uint8_t *dptr, void *arg, uint8_t *hdr_byte)
{
    uint8_t pdu_len = 0;

    *hdr_byte = 0;

    return pdu_len;
}

static void
ble_ll_cs_tone_tx_end_cb(void *arg)
{
    struct ble_ll_cs_sm *cssm = (struct ble_ll_cs_sm *)arg;
    struct ble_ll_cs_step *step;
    struct ble_ll_cs_step_transmission *transm;

    BLE_LL_ASSERT(cssm != NULL);

    step = cssm->current_step;
    transm = step->next_transm;
    cssm->anchor_usecs += transm->duration_usecs + transm->end_tifs;
    ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
}

int
ble_ll_cs_tone_tx_start(struct ble_ll_cs_sm *cssm)
{
    int rc;
    struct ble_ll_cs_step *step = cssm->current_step;
    struct ble_ll_cs_step_transmission *transm = step->next_transm;
    uint32_t cputime;
    uint8_t ll_state;
    uint8_t rem_us;

    ll_state = ble_ll_state_get();
    BLE_LL_ASSERT(ll_state == BLE_LL_STATE_STANDBY || ll_state == BLE_LL_STATE_CS);

    ble_phy_cs_tone_configure(step->channel,
                              (step->mode == BLE_LL_CS_MODE0) ?
                              BLE_PHY_CS_TONE_MODE_FM : BLE_PHY_CS_TONE_MODE_PM,
                              transm->duration_usecs);

    ble_ll_tx_power_set(g_ble_ll_tx_power);

    cputime = ble_ll_tmr_u2t_r(cssm->anchor_usecs, &rem_us);
    ble_phy_transition_set(transm->end_transition, transm->end_tifs);
    ble_phy_set_txend_cb(ble_ll_cs_tone_tx_end_cb, cssm);

    /* At transition the radio is already scheduled to start at the right time */
    if (ll_state == BLE_LL_STATE_CS) {
        return 0;
    }

    rc = ble_phy_tx_set_start_time(cputime, rem_us);
    if (rc) {
        ble_ll_cs_proc_sync_lost(cssm);
        return 1;
    }

    rc = ble_phy_tx(no_pdu_make, NULL);
    if (rc) {
        ble_ll_cs_proc_sync_lost(cssm);
        return 1;
    }

    ble_ll_state_set(BLE_LL_STATE_CS);

    return 0;
}

int
ble_ll_cs_tone_rx_start(struct ble_ll_cs_sm *cssm)
{
    int rc;
    struct ble_ll_cs_step *step = cssm->current_step;
    struct ble_ll_cs_step_transmission *transm = step->next_transm;
    uint32_t cputime;
    uint8_t ll_state;
    uint8_t rem_us;

    ll_state = ble_ll_state_get();
    BLE_LL_ASSERT(ll_state == BLE_LL_STATE_STANDBY || ll_state == BLE_LL_STATE_CS);

    ble_phy_cs_tone_configure(step->channel,
                              (step->mode == BLE_LL_CS_MODE0) ?
                              BLE_PHY_CS_TONE_MODE_FM : BLE_PHY_CS_TONE_MODE_PM,
                              transm->duration_usecs);

    ble_phy_transition_set(transm->end_transition, transm->end_tifs);

    /* At transition the radio is already scheduled to start at the right time */
    if (ll_state == BLE_LL_STATE_CS) {
        return 0;
    }

    cputime = ble_ll_tmr_u2t_r(cssm->anchor_usecs, &rem_us);
    rc = ble_phy_rx_set_start_time(cputime, rem_us);
    if (rc) {
        ble_ll_cs_proc_sync_lost(cssm);
        return 1;
    }

    ble_ll_state_set(BLE_LL_STATE_CS);

    return 0;
}

void
ble_ll_cs_tone_rx_end_cb(struct ble_ll_cs_sm *cssm)
{
    struct ble_ll_cs_step_transmission *transm = cssm->current_step->next_transm;

    cssm->anchor_usecs += transm->duration_usecs + transm->end_tifs;
    ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
}

#endif /* BLE_LL_CHANNEL_SOUNDING */
