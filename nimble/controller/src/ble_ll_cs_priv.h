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
#include "ble_ll_cs_drbg_priv.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_LL_CS_CONFIG_MAX_NUM 4

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

struct ble_ll_cs_config {
    uint8_t config_in_use;
    uint8_t config_enabled;
    /* The role to use in CS procedure
     * 0x00 = Initiator,
     * 0x01 = Reflector
     */
    uint8_t role;
    /* Map of allowed channels for this CS config */
    uint8_t chan_map[10];
    /* The number of times the map represented by the Channel_Map field is to
     * be cycled through for non-mode 0 steps within a CS procedure
     */
    uint8_t chan_map_repetition;
    uint8_t main_mode;
    uint8_t sub_mode;
    uint8_t main_mode_min_steps;
    uint8_t main_mode_max_steps;
    uint8_t main_mode_repetition;
    uint8_t mode_0_steps;
    /* PHY used for mode 0, 1 and 3 (use LE 1M PHY)*/
    uint8_t cs_sync_phy;
    /* Type of RTT (Round-Trip Time) packets */
    uint8_t rtt_type;
    /* The Channel Selection Algorithm to use */
    uint8_t chan_sel;
    /* Parameters for #3c algorithm */
    uint8_t ch3cshape;
    uint8_t ch3cjump;
    /* Timings (indexes) selected from capabilities */
    uint8_t t_ip1_index;
    uint8_t t_ip2_index;
    uint8_t t_fcs_index;
    uint8_t t_pm_index;
    /* Timings (usec) selected from capabilities */
    uint8_t t_ip1;
    uint8_t t_ip2;
    uint8_t t_fcs;
    uint8_t t_pm;
};

struct ble_ll_cs_sm {
    struct ble_ll_conn_sm *connsm;
    struct ble_ll_cs_supp_cap remote_cap;
    struct ble_ll_cs_config config[BLE_LL_CS_CONFIG_MAX_NUM];

    /* Default Settings */
    uint8_t roles_enabled;
    uint8_t cs_sync_antenna_selection;
    uint8_t max_tx_power;

    /* Cached FAE tables */
    uint8_t remote_fae_table[72];
    uint8_t local_fae_table[72];

    /* Arguments for ble_ll_cs_config_req_make */
    uint8_t config_req_id;
    uint8_t config_req_action;
    struct ble_ll_cs_config tmp_config;
};

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_LL_CS_PRIV */
