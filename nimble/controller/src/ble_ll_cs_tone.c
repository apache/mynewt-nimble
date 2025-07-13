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
#include "ble_ll_cs_priv.h"

extern struct ble_ll_cs_sm *g_ble_ll_cs_sm_current;

void
ble_ll_cs_tone_tx_end(struct ble_phy_cs_tone_results *results)
{
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;
    struct ble_phy_cs_transmission *transm;
    struct ble_ll_cs_step *step;
    uint8_t i;

    BLE_LL_ASSERT(cssm != NULL);

    step = cssm->current_step;
    transm = &step->next_transm->phy_transm;

    if (step->mode == BLE_LL_CS_MODE0) {
        /* TODO: Read measured frequency offset.
         * For now set "Frequency offset is not available".
         */
        cssm->step_result.measured_freq_offset = 0xC000;
    } else if (step->mode == BLE_LL_CS_MODE2 ||
               step->mode == BLE_LL_CS_MODE3) {
        for (i = 0; i < cssm->n_ap; ++i) {
            cssm->step_result.tone_pct[i] = 0;
            cssm->step_result.tone_quality_ind[i] = 0;
        }
    }

    cssm->anchor_usecs += transm->duration_usecs + transm->end_tifs;
    ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
}

void
ble_ll_cs_tone_rx_end(struct ble_phy_cs_tone_results *results)
{
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;
    struct ble_phy_cs_transmission *transm;

    BLE_LL_ASSERT(cssm != NULL);

    transm = &cssm->current_step->next_transm->phy_transm;

    cssm->anchor_usecs += transm->duration_usecs + transm->end_tifs;
    ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
}

#endif /* BLE_LL_CHANNEL_SOUNDING */
