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

#define BLE_LL_CS_ROLE_INITIATOR (0)
#define BLE_LL_CS_ROLE_REFLECTOR (1)

#define BLE_LL_CS_CONFIG_MAX_NUM 4
/* CS Event interval in units of the number of connection intervals */
#define BLE_LL_CS_EVENT_INTERVAL_MIN (0x0001)
#define BLE_LL_CS_EVENT_INTERVAL_MAX (0xFFFF)
#define BLE_LL_CS_SUBEVENTS_PER_PROCEDURE_MIN (1)
#define BLE_LL_CS_SUBEVENTS_PER_PROCEDURE_MAX (32)
/* CS Subevent interval in units of 625usec */
#define BLE_LL_CS_SUBEVENTS_INTERVAL_MIN (1)
#define BLE_LL_CS_SUBEVENTS_INTERVAL_MAX (32)
/* CS Subevent interval in microseconds */
#define BLE_LL_CS_SUBEVENT_LEN_MIN (1250)
#define BLE_LL_CS_SUBEVENT_LEN_MAX (4000000)

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

struct ble_ll_cs_pref_proc_params {
    uint16_t max_procedure_len;
    uint16_t min_procedure_interval;
    uint16_t max_procedure_interval;
    uint16_t max_procedure_count;
    uint32_t min_subevent_len;
    uint32_t max_subevent_len;
    uint8_t aci;
    uint8_t phy;
    uint8_t tx_power_delta;
    uint8_t preferred_peer_antenna;
    uint8_t snr_control_initiator;
    uint8_t snr_control_reflector;
    uint8_t params_ready;
};

/* Negotiated parameters for CS procedures */
struct ble_ll_cs_proc_params {
    /* The number of consecutive CS procedures to invoke */
    uint16_t max_procedure_count;
    /* The event counter that will be the anchor point to
     * the start of the first CS procedure.
     */
    uint16_t anchor_conn_event_cntr;
    /* The offset (usec) from the event counter to the CS subevent anchor point */
    uint32_t event_offset;
    uint32_t offset_min;
    uint32_t offset_max;
    /* Max CS procedure length in 0.625us units */
    uint16_t max_procedure_len;
    /* Max CS subevent length in microseconds */
    uint32_t subevent_len;
    uint16_t procedure_interval;
    uint16_t event_interval;
    uint16_t subevent_interval;
    uint8_t subevents_per_event;
    /* Selected Anntena Configuration ID */
    uint8_t aci;
    /* Bit map of preferred peer-ordered antenna elements */
    uint8_t preferred_peer_antenna;
    /* Bit map of the remote device’s Tx PHY */
    uint8_t phy;
    /* Transmit power delta, in signed dB, to indicate the recommended difference
     * between the remote device’s power level for the CS tones and RTT packets
     * and the existing power level for the PHY indicated by the PHY parameter.
     */
    uint8_t tx_power_delta;
    /* SNR Output Index (SOI) for SNR control adjustment */
    uint8_t tx_snr_i;
    uint8_t tx_snr_r;
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
    /* CS procedure parameters preferred by our Host */
    struct ble_ll_cs_pref_proc_params pref_proc_params;
    /* Final negationed CS procedure params */
    struct ble_ll_cs_proc_params proc_params;
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

    struct ble_ll_cs_config *active_config;
    uint8_t active_config_id;

    /* Arguments for ble_ll_cs_config_req_make */
    uint8_t config_req_id;
    uint8_t config_req_action;
    struct ble_ll_cs_config tmp_config;

    /* DRBG context, initialized onece per LE Connection */
    struct ble_ll_cs_drbg_ctx drbg_ctx;

    uint8_t measurement_enabled;
};

int ble_ll_cs_proc_scheduling_start(struct ble_ll_conn_sm *connsm, uint8_t config_id);

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_LL_CS_PRIV */
