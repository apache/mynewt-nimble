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

/* All Channel Sounding APIs are experimental and subject to change at any time */

#ifndef H_BLE_CS_
#define H_BLE_CS_
#include "syscfg/syscfg.h"

#define BLE_HS_CS_MODE0 (0)
#define BLE_HS_CS_MODE1 (1)
#define BLE_HS_CS_MODE2 (2)
#define BLE_HS_CS_MODE3 (3)
#define BLE_HS_CS_MODE_UNUSED (0xff)
#define BLE_HS_CS_SUBMODE_TYPE BLE_HS_CS_MODE_UNUSED

#define BLE_HS_CS_ROLE_INITIATOR (0)
#define BLE_HS_CS_ROLE_REFLECTOR (1)

#define BLE_CS_EVENT_CS_PROCEDURE_COMPLETE (0)
#define BLE_CS_EVENT_CS_STEP_DATA          (1)

#define BLE_HS_CS_TOA_TOD_NOT_AVAILABLE  (0x00008000)
#define BLE_HS_CS_N_AP_MAX (4)

struct ble_cs_mode0_result {
    uint16_t measured_freq_offset;
    uint8_t packet_quality;
    uint8_t packet_rssi;
    uint8_t packet_antenna;
    uint8_t step_channel;
};

struct ble_cs_mode1_result {
    uint32_t packet_pct1;
    uint32_t packet_pct2;
    int16_t toa_tod;
    uint8_t packet_quality;
    uint8_t packet_nadm;
    uint8_t packet_rssi;
    uint8_t packet_antenna;
    uint8_t step_channel;
};

struct ble_cs_mode2_result {
    uint32_t tone_pct[BLE_HS_CS_N_AP_MAX + 1];
    uint8_t tone_quality_ind[BLE_HS_CS_N_AP_MAX + 1];
    uint8_t antenna_paths[4];
    uint8_t antenna_path_permutation_id;
    uint8_t step_channel;
};

struct ble_cs_mode3_result {
    uint32_t packet_pct1;
    uint32_t packet_pct2;
    uint32_t tone_pct[BLE_HS_CS_N_AP_MAX + 1];
    uint8_t tone_quality_ind[BLE_HS_CS_N_AP_MAX + 1];
    uint8_t antenna_paths[4];
    int16_t toa_tod;
    uint8_t antenna_path_permutation_id;
    uint8_t packet_quality;
    uint8_t packet_nadm;
    uint8_t packet_rssi;
    uint8_t packet_antenna;
    uint8_t step_channel;
};

struct ble_cs_event {
    uint16_t conn_handle;
    uint8_t status;
    uint8_t type;
    union {
        struct {
            uint8_t role;
            uint8_t mode;
            uint8_t *data;
        } step_data;
    };
};

typedef int ble_cs_event_fn(struct ble_cs_event *event, void *arg);

struct ble_cs_procedure_start_params {
    uint16_t conn_handle;
};

struct ble_cs_setup_params {
    uint16_t conn_handle;
    ble_cs_event_fn *cb;
    void *cb_arg;
    uint8_t local_role;
};

int ble_cs_procedure_start(const struct ble_cs_procedure_start_params *params);
int ble_cs_procedure_terminate(uint16_t conn_handle);
int ble_cs_setup(struct ble_cs_setup_params *params);
#endif
