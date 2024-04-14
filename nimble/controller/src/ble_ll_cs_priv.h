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

#ifndef H_BLE_LL_CS_PRIV
#define H_BLE_LL_CS_PRIV

#include <stdint.h>
#include "controller/ble_ll_conn.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ble_ll_cs_supp_cap {
    uint8_t mode_types;
    uint8_t roles_supported;
    uint8_t rtt_capability;
    uint8_t rtt_aa_only_n;
    uint8_t rtt_sounding_n;
    uint8_t rtt_random_sequence_n;
    uint16_t nadm_sounding_capability;
    uint16_t nadm_random_sequence_capability;
    uint8_t cs_sync_phy_capability;
    uint8_t number_of_antennas;
    uint8_t max_number_of_antenna_paths;
    uint8_t no_fae;
    uint8_t channel_selection;
    uint8_t sounding_pct_estimate;
    uint8_t max_number_of_configs;
    uint16_t max_number_of_procedures;
    uint16_t t_ip1_capability;
    uint16_t t_ip2_capability;
    uint16_t t_fcs_capability;
    uint16_t t_pm_capability;
    uint8_t t_sw;
    uint8_t tx_snr_capablity;
};

struct ble_ll_cs_sm {
    struct ble_ll_conn_sm *connsm;
};

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_LL_CS_PRIV */
