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

#define BLE_LL_CS_MODE0 (0)
#define BLE_LL_CS_MODE1 (1)
#define BLE_LL_CS_MODE2 (2)
#define BLE_LL_CS_MODE3 (3)

/* States within step */
#define STEP_STATE_INIT          (0)
#define STEP_STATE_CS_SYNC_I     (1)
#define STEP_STATE_CS_SYNC_R     (2)
#define STEP_STATE_CS_TONE_I     (3)
#define STEP_STATE_CS_TONE_R     (4)
#define STEP_STATE_COMPLETE      (5)

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

/* CS Event interval in units of the number of connection intervals */
#define BLE_LL_CS_SUBEVENTS_INTERVAL_UNIT_US (625)
#define BLE_LL_CS_PROCEDURE_LEN_UNIT_US (625)

#define BLE_LL_CS_STEPS_PER_SUBEVENT_MIN  (2)
#define BLE_LL_CS_STEPS_PER_SUBEVENT_MAX  (160)
#define BLE_LL_CS_STEPS_PER_PROCEDURE_MAX (256)

#define BLE_LL_CS_SYNC_PHY_1M (0x01)
#define BLE_LL_CS_SYNC_PHY_2M (0x02)
/* The duration of the CS_SYNC (T_SY) without sequence in usec */
#define BLE_LL_CS_SYNC_TIME_1M (44)
#define BLE_LL_CS_SYNC_TIME_2M (26)

typedef int (*ble_ll_cs_sched_cb_func)(struct ble_ll_cs_sm *cssm);

struct ble_ll_cs_step_transmission {
    struct ble_phy_cs_transmission phy_transm;
    uint8_t state;
};

struct ble_ll_cs_step {
    uint8_t mode;
    uint8_t channel;
    uint8_t tone_ext_presence_i;
    uint8_t tone_ext_presence_r;
    uint32_t initiator_aa;
    uint32_t reflector_aa;
    struct ble_ll_cs_step_transmission *next_transm;
    struct ble_ll_cs_step_transmission *last_transm;
    uint8_t *rtt_tx;
    uint8_t *rtt_rx;
    uint8_t rtt_tx_len;
    uint8_t rtt_rx_len;
};

struct ble_ll_cs_aci {
    uint8_t n_ap;
    uint8_t n_a_antennas;
    uint8_t n_b_antennas;
};

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
    uint8_t filtered_channels[72];
    uint8_t filtered_channels_count;
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

struct ble_ll_cs_step_result {
    uint8_t sounding_pct_estimate;
    uint8_t packet_rssi;
    uint8_t packet_quality;
    uint8_t packet_nadm;
    uint32_t time_of_departure_ns;
    uint64_t time_of_arrival_ns;
    uint32_t packet_pct1;
    uint32_t packet_pct2;
    uint16_t measured_freq_offset;
    uint32_t tone_pct[5];
    uint8_t tone_quality_ind[5];
    uint8_t tone_count;
};

struct ble_ll_cs_subevent {
    struct ble_hci_ev *hci_ev;
    unsigned int subev;
    uint8_t num_steps_reported;
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

    /* Arguments for ble_ll_cs_hci_proc_enable */
    uint8_t terminate_config_id;
    uint8_t terminate_error_code;

    /* DRBG context, initialized onece per LE Connection */
    struct ble_ll_cs_drbg_ctx drbg_ctx;

    /* Helper flags */
    uint8_t measurement_enabled;
    uint8_t terminate_measurement;

    /* Scheduling data for current CS procedure */
    struct ble_ll_sched_item sch;
    ble_ll_cs_sched_cb_func sched_cb;
    uint32_t anchor_usecs;
    struct ble_ll_cs_step *current_step;
    struct ble_ll_cs_step *last_step;
    uint8_t *next_rtt_ptr;
    /* Cached main mode channels that will be used in repetition steps */
    uint8_t repetition_channels[3];
    uint8_t n_ap;

    /* Counters of complete CS procedures/events/subevents/steps */
    uint16_t procedure_count;
    uint16_t events_in_procedure_count;
    uint16_t subevents_in_procedure_count;
    uint16_t subevents_in_event_count;
    uint16_t steps_in_procedure_count;
    uint8_t steps_in_subevent_count;

    /* Down-counters of remaining steps */
    uint8_t mode0_step_count;
    uint8_t repetition_count;
    uint8_t main_step_count;

    /* Anchor time of current CS procedure */
    uint32_t procedure_anchor_usecs;
    /* Anchor time of current CS event */
    uint32_t event_anchor_usecs;
    /* Anchor time of current CS subevent */
    uint32_t subevent_anchor_usecs;
    /* Anchor time of next CS step */
    uint32_t step_anchor_usecs;
    uint32_t event_interval_usecs;
    uint32_t subevent_interval_usecs;
    uint32_t procedure_interval_usecs;
    /* Estimated time of step modes (ToF not included) */
    uint32_t mode_duration_usecs[4];
    /* Time of antenna swith */
    uint8_t t_sw;
    /* Time of CS_SYNC packet without sequence */
    uint8_t t_sy;
    /* Time of CS_SYNC sequence only */
    uint8_t t_sy_seq;
    uint8_t cs_sync_antenna;

    /* Cache for HCI Subevent Result event */
    struct ble_ll_cs_subevent buffered_subevent;
    struct ble_ll_cs_step_result step_result;
    uint8_t cs_schedule_status;
    uint8_t proc_abort_reason;
    uint8_t subev_abort_reason;

    /* Channel selection stuff */
    uint8_t mode0_channels[72];
    uint8_t non_mode0_channels[72];
    uint8_t mode0_next_chan_id;
    uint8_t non_mode0_next_chan_id;

    /* Procedure counter for backtracking resistance */
    uint16_t start_procedure_count;
};

int ble_ll_cs_proc_next_state(struct ble_ll_cs_sm *cssm, struct ble_phy_cs_transmission *transm);
int ble_ll_cs_proc_scheduling_start(struct ble_ll_conn_sm *connsm, uint8_t config_id);
void ble_ll_cs_proc_sync_lost(struct ble_ll_cs_sm *cssm);

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_LL_CS_PRIV */
