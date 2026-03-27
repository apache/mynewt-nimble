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

#ifndef H_BLE_SVC_RAS_
#define H_BLE_SVC_RAS_

#include <inttypes.h>
#include "host/ble_cs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_SVC_RAS_SVC_RANGING_SERVICE_UUID          0x185B
#define BLE_SVC_RAS_CHR_RAS_FEATURES_UUID             0x2C14
#define BLE_SVC_RAS_CHR_REAL_TIME_RANGING_DATA_UUID   0x2C15
#define BLE_SVC_RAS_CHR_ON_DEMAND_RANGING_DATA_UUID   0x2C16
#define BLE_SVC_RAS_CHR_RAS_CONTROL_POINT_UUID        0x2C17
#define BLE_SVC_RAS_CHR_RANGING_DATA_READY_UUID       0x2C18
#define BLE_SVC_RAS_CHR_RANGING_DATA_OVERWRITTEN_UUID 0x2C19

#define BLE_SVC_RAS_FILTER_MODE0_POSITION (0)
#define BLE_SVC_RAS_FILTER_MODE1_POSITION                                     \
    (BLE_SVC_RAS_FILTER_MODE0_POSITION << 14)
#define BLE_SVC_RAS_FILTER_MODE2_POSITION                                     \
    (BLE_SVC_RAS_FILTER_MODE1_POSITION << 14)
#define BLE_SVC_RAS_FILTER_MODE3_POSITION                                     \
    (BLE_SVC_RAS_FILTER_MODE3_POSITION << 14)

#define BLE_SVC_RAS_FILTER_MODE0_MASK (0xF)
#define BLE_SVC_RAS_FILTER_MODE1_MASK (0x7F)
#define BLE_SVC_RAS_FILTER_MODE2_MASK (0x7F)
#define BLE_SVC_RAS_FILTER_MODE3_MASK (0x3FFF)

#define BLE_SVC_RAS_FILTER_MODE0_PACKET_QUALITY       (0)
#define BLE_SVC_RAS_FILTER_MODE0_PACKET_RSSI          (1)
#define BLE_SVC_RAS_FILTER_MODE0_PACKET_ANTENNA       (2)
#define BLE_SVC_RAS_FILTER_MODE0_MEASURED_FREQ_OFFSET (3)

#define BLE_SVC_RAS_FILTER_MODE1_PACKET_QUALITY (0)
#define BLE_SVC_RAS_FILTER_MODE1_PACKET_NADM    (1)
#define BLE_SVC_RAS_FILTER_MODE1_PACKET_RSSI    (2)
#define BLE_SVC_RAS_FILTER_MODE1_TOD_TOA        (3)
#define BLE_SVC_RAS_FILTER_MODE1_PACKET_ANTENNA (4)
#define BLE_SVC_RAS_FILTER_MODE1_PACKET_PCT1    (5)
#define BLE_SVC_RAS_FILTER_MODE1_PACKET_PCT2    (6)

#define BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PERMUTATION_ID (0)
#define BLE_SVC_RAS_FILTER_MODE2_TONE_PCT               (1)
#define BLE_SVC_RAS_FILTER_MODE2_TONE_QUALITY           (2)
#define BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_1         (3)
#define BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_2         (4)
#define BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_3         (5)
#define BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_4         (6)

#define BLE_SVC_RAS_FILTER_MODE3_PACKET_QUALITY         (0)
#define BLE_SVC_RAS_FILTER_MODE3_PACKET_NADM            (1)
#define BLE_SVC_RAS_FILTER_MODE3_PACKET_RSSI            (2)
#define BLE_SVC_RAS_FILTER_MODE3_TOD_TOA                (3)
#define BLE_SVC_RAS_FILTER_MODE3_PACKET_ANTENNA         (4)
#define BLE_SVC_RAS_FILTER_MODE3_PACKET_PCT1            (5)
#define BLE_SVC_RAS_FILTER_MODE3_PACKET_PCT2            (6)
#define BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PERMUTATION_ID (7)
#define BLE_SVC_RAS_FILTER_MODE3_TONE_PCT               (8)
#define BLE_SVC_RAS_FILTER_MODE3_TONE_QUALITY           (9)
#define BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_1         (10)
#define BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_2         (11)
#define BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_3         (12)
#define BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_4         (13)

#define BLE_SVC_RAS_CP_CMD_GET_RANGING_DATA      (0x00)
#define BLE_SVC_RAS_CP_CMD_ACK_RANGING_DATA      (0x01)
#define BLE_SVC_RAS_CP_CMD_RETRIEVE_LOST_SEGMENT (0x02)
#define BLE_SVC_RAS_CP_CMD_ABORT_OPERATION       (0x03)
#define BLE_SVC_RAS_CP_CMD_SET_FILTER            (0x04)

#define BLE_SVC_RAS_CP_RSP_COMPLETE_RANGING_DATA (0x00)
#define BLE_SVC_RAS_CP_RSP_COMPLETE_LOST_SEGMENT (0x01)
#define BLE_SVC_RAS_CP_RSP_RESPONSE_CODE         (0x02)

#define BLE_SVC_RAS_CP_RSPCODE_SUCCESS                 (0x01)
#define BLE_SVC_RAS_CP_RSPCODE_OP_CODE_NOT_SUPPORTED   (0x02)
#define BLE_SVC_RAS_CP_RSPCODE_INVALID_PARAMETER       (0x03)
#define BLE_SVC_RAS_CP_RSPCODE_SUCCESS_PERSISTED       (0x04)
#define BLE_SVC_RAS_CP_RSPCODE_ABORT_UNSUCCESSFUL      (0x05)
#define BLE_SVC_RAS_CP_RSPCODE_PROCEDURE_NOT_COMPLETED (0x06)
#define BLE_SVC_RAS_CP_RSPCODE_SERVER_BUSY             (0x07)
#define BLE_SVC_RAS_CP_RSPCODE_NO_RECORDS_FOUND        (0x08)

#define BLE_SVC_RAS_MODE_REAL_TIME (0)
#define BLE_SVC_RAS_MODE_ON_DEMAND (1)

#if MYNEWT_VAL(BLE_SVC_RAS_SERVER)
void ble_svc_ras_init(void);
int ble_svc_ras_ranging_data_body_init(uint16_t conn_handle, uint16_t procedure_counter,
                                       uint8_t config_id, uint8_t tx_power,
                                       uint8_t antenna_paths_mask);
int ble_svc_ras_ranging_subevent_init(
    uint16_t conn_handle, uint16_t start_acl_conn_event, uint16_t frequency_compensation,
    uint8_t ranging_done_status, uint8_t subevent_done_status,
    uint8_t ranging_abort_reason, uint8_t subevent_abort_reason,
    uint8_t reference_power_level, uint8_t number_of_steps_reported);
int ble_svc_ras_ranging_subevent_update_status(uint16_t conn_handle,
                                               uint8_t number_of_steps_reported,
                                               uint8_t ranging_done_status,
                                               uint8_t subevent_done_status,
                                               uint8_t ranging_abort_reason,
                                               uint8_t subevent_abort_reason);
int ble_svc_ras_add_step_mode(uint16_t conn_handle, uint8_t mode, uint8_t status);
int ble_svc_ras_add_mode0_result(struct ble_cs_mode0_result *result,
                                 uint16_t conn_handle, uint8_t local_role);
int ble_svc_ras_add_mode1_result(struct ble_cs_mode1_result *result,
                                 uint16_t conn_handle, uint8_t rtt_pct_included);
int ble_svc_ras_add_mode2_result(struct ble_cs_mode2_result *result,
                                 uint16_t conn_handle, uint8_t n_ap);
int ble_svc_ras_add_mode3_result(struct ble_cs_mode3_result *result,
                                 uint16_t conn_handle, uint8_t n_ap,
                                 uint8_t rtt_pct_included);
int ble_svc_ras_ranging_data_ready(uint16_t conn_handle);
#endif

#if MYNEWT_VAL(BLE_SVC_RAS_CLIENT)
struct ble_svc_ras_clt_ranging_header {
    uint16_t ranging_counter;
    uint8_t config_id;
    uint8_t tx_power;
    uint8_t antenna_paths_mask;
};

struct ble_svc_ras_clt_subevent_header {
    uint16_t start_acl_conn_event;
    uint16_t frequency_compensation;
    uint8_t done_status;
    uint8_t abort_reason;
    uint8_t reference_power_level;
    uint8_t number_of_steps_reported;
};

typedef void ble_svc_ras_clt_subscribe_cb(uint16_t conn_handle);
typedef void ble_svc_ras_clt_step_data_received_cb(void *data, uint16_t conn_handle,
                                                   uint8_t step_mode);
int ble_svc_ras_clt_config_set(uint16_t conn_handle, uint8_t rtt_pct_included,
                               uint8_t n_ap, uint8_t local_role);
int ble_svc_ras_clt_subscribe(ble_svc_ras_clt_subscribe_cb *subscribe_cb,
                              ble_svc_ras_clt_step_data_received_cb *step_data_cb,
                              uint16_t conn_handle, uint8_t mode);
#endif
#ifdef __cplusplus
}
#endif

#endif
