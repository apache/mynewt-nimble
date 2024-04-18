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
#include "controller/ble_ll_conn.h"
#include "controller/ble_ll_sched.h"
#include "controller/ble_ll_tmr.h"
#include "controller/ble_hw.h"
#include "controller/ble_ll_hci.h"
#include "mbedtls/aes.h"
#include "ble_ll_cs_priv.h"

extern struct ble_ll_cs_supp_cap g_ble_ll_cs_local_cap;
extern struct ble_ll_cs_sm *g_ble_ll_cs_sm_current;

void
ble_ll_cs_sync_results_set(struct ble_ll_cs_step *step,
                           struct ble_phy_cs_sync_results *results)
{
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;

    BLE_LL_ASSERT(cssm != NULL);

    cssm->step_result.time_of_departure_ns = results->time_of_departure_ns;
    cssm->step_result.time_of_arrival_ns = results->time_of_arrival_ns;
    cssm->step_result.packet_rssi = results->rssi;
    cssm->step_result.packet_quality = 0;
    cssm->step_result.packet_nadm = 0xFF;

    if (g_ble_ll_cs_local_cap.sounding_pct_estimate &&
        cssm->active_config->rtt_type != BLE_LL_CS_RTT_AA_ONLY) {
        /* TODO: Read PCT estimates from sounding sequence.
         * For now set "Phase Correction Term is not available".
         */
        cssm->step_result.packet_pct1 = 0xFFFFFFFF;
        cssm->step_result.packet_pct2 = 0xFFFFFFFF;
    }
}

#endif /* BLE_LL_CHANNEL_SOUNDING */
