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

#define BLE_CONN_HANDLE_INVALID (0xFFFF)

#define BLE_HS_CS_MODE0        (0)
#define BLE_HS_CS_MODE1        (1)
#define BLE_HS_CS_MODE2        (2)
#define BLE_HS_CS_MODE3        (3)
#define BLE_HS_CS_MODE_UNUSED  (0xff)
#define BLE_HS_CS_SUBMODE_TYPE BLE_HS_CS_MODE_UNUSED

#define BLE_HS_CS_ROLE_INITIATOR (0)
#define BLE_HS_CS_ROLE_REFLECTOR (1)

#define BLE_CS_EVENT_CS_PROCEDURE_COMPLETE (0)
#define BLE_CS_EVENT_CS_STEP_DATA          (1)

#define BLE_HS_CS_TOA_TOD_NOT_AVAILABLE (0x00008000)
#define BLE_HS_CS_N_AP_MAX              (4)

/**
 * Represents results of a mode0 step.
 * Step_Data parameters are in format defined in BT specification
 * for HCI_LE_CS_Subevent_Result.
 */
struct ble_cs_mode0_result {
    /** Measured_Freq_Offset of Step_Data. */
    uint16_t measured_freq_offset;

    /** Packet_Quality of Step_Data. */
    uint8_t packet_quality;

    /** Packet_RSSI of Step_Data. */
    uint8_t packet_rssi;

    /** Packet_RSSI of Step_Data. */
    uint8_t packet_antenna;

    /** ID of a CS channel used in this step. */
    uint8_t step_channel;
};

/** Represents results of a mode1 step. */
struct ble_cs_mode1_result {
    /** Packet_PCT1 of Step_Data. */
    uint32_t packet_pct1;

    /** Packet_PCT2 of Step_Data. */
    uint32_t packet_pct2;

    /** ToA_ToD_Initiator or ToD_ToA_Reflector of Step_Data. */
    int16_t toa_tod;

    /** Packet_Quality of Step_Data. */
    uint8_t packet_quality;

    /** Packet_NADM of Step_Data. */
    uint8_t packet_nadm;

    /** Packet_RSSI of Step_Data. */
    uint8_t packet_rssi;

    /** Packet_Antenna of Step_Data. */
    uint8_t packet_antenna;

    /** ID of a CS channel used in this step. */
    uint8_t step_channel;
};

/** Represents results of a mode2 step. */
struct ble_cs_mode2_result {
    /** Tone_PCT[k] of Step_Data. */
    uint32_t tone_pct[BLE_HS_CS_N_AP_MAX + 1];

    /** Tone_Quality_Indicator[k] of Step_Data. */
    uint8_t tone_quality_ind[BLE_HS_CS_N_AP_MAX + 1];

    /** Antenna path permutation used in this step. */
    uint8_t antenna_paths[4];

    /** Antenna_Permutation_Index of Step_Data. */
    uint8_t antenna_path_permutation_id;

    /** ID of a CS channel used in this step. */
    uint8_t step_channel;
};

/** Represents results of a mode3 step. */
struct ble_cs_mode3_result {
    /** Packet_PCT1 of Step_Data. */
    uint32_t packet_pct1;

    /** Packet_PCT2 of Step_Data. */
    uint32_t packet_pct2;

    /** Tone_PCT[k] of Step_Data. */
    uint32_t tone_pct[BLE_HS_CS_N_AP_MAX + 1];

    /** Tone_Quality_Indicator[k] of Step_Data. */
    uint8_t tone_quality_ind[BLE_HS_CS_N_AP_MAX + 1];

    /** Antenna path permutation used in this step. */
    uint8_t antenna_paths[4];

    /** ToA_ToD_Initiator or ToD_ToA_Reflector of Step_Data. */
    int16_t toa_tod;

    /** Antenna_Permutation_Index of Step_Data. */
    uint8_t antenna_path_permutation_id;

    /** Packet_Quality of Step_Data. */
    uint8_t packet_quality;

    /** Packet_NADM of Step_Data. */
    uint8_t packet_nadm;

    /** Packet_RSSI of Step_Data. */
    uint8_t packet_rssi;

    /** Packet_Antenna of Step_Data. */
    uint8_t packet_antenna;

    /** ID of a CS channel used in this step. */
    uint8_t step_channel;
};

/** Represents a Channel Sounding related event. */
struct ble_cs_event {
    /** Indicates the type of Channel Sounding event that occurred. */
    uint8_t type;

    /** A union containing additional details concerning the Channel Sounding event. */
    union {
        /** A struct containing details of the BLE_CS_EVENT_CS_PROCEDURE_COMPLETE event. */
        struct {
            /* A status code. */
            uint8_t status;
        } procedure_complete;
        /** A struct containing details of the BLE_CS_EVENT_CS_STEP_DATA event. */
        struct {
            /* A status code. */
            uint8_t status;

            /* For which role the step result is reported. */
            uint8_t role;

            /* Mode of the step. */
            uint8_t mode;

            /* Pointer to a data structure with step results. */
            uint8_t *data;
        } step_data;
    };
};

/** Function prototype for a Channel Sounding event callback. */
typedef int ble_cs_event_fn(struct ble_cs_event *event, void *arg, uint16_t conn_handle);

/** Start Channel Sounding procedure parameters. */
struct ble_cs_procedure_start_params {};

/** Terminate Channel Sounding procedure parameters. */
struct ble_cs_procedure_terminate_params {};

/** Setup Channel Sounding procedure parameters. */
struct ble_cs_setup_params {
    /** Callback function for reporting the Channel Sounding events. */
    ble_cs_event_fn *cb;

    /** An optional user-defined argument to be passed to the callback function. */
    void *cb_arg;

    /** Callback function for reporting the Channel Sounding events. */
    uint8_t local_role;
};

/**
 * @brief Start Channel Sounding.
 *
 * This function starts a Channel Sounding measurements. The process begins
 * with CS setup phase and, if it is successful, continues by executing
 * CS procedures.
 *
 * @param params                The parameters for the CS start.
 * @param conn_handle           The connection handle.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_cs_procedure_start(const struct ble_cs_procedure_start_params *params,
                           uint16_t conn_handle);

/**
 * @brief Terminate Channel Sounding.
 *
 * This function terminates a Channel Sounding measurements.
 *
 * @param params                The parameters with details of the CS termination.
 * @param conn_handle           The connection handle.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_cs_procedure_terminate(const struct ble_cs_procedure_terminate_params *params,
                               uint16_t conn_handle);

/**
 * @brief Setup Channel Sounding
 *
 * This function setups Channel Sounding event callback and resources.
 *
 * @param params                The parameters for the CS setup.
 * @param conn_handle           The connection handle.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_cs_setup(const struct ble_cs_setup_params *params, uint16_t conn_handle);
#endif
