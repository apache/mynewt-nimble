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

#ifndef H_BLE_AUDIO_BROADCAST_SINK_
#define H_BLE_AUDIO_BROADCAST_SINK_

/**
 * @file ble_audio_broadcast_sink.h
 *
 * @brief Bluetooth LE Audio BAP Broadcast Sink API
 *
 * @defgroup ble_audio_broadcast_sink Bluetooth LE Audio BAP Broadcast Sink
 * @ingroup bt_host
 * @{
 *
 */

#include <stdint.h>
#include "host/ble_gap.h"
#include "host/ble_iso.h"
#include "audio/ble_audio.h"
#include "audio/ble_audio_scan_delegator.h"
#include "nimble/ble.h"

enum ble_audio_broadcast_sink_action_type {
    /** Broadcast Sink Action Type: PA sync */
    BLE_AUDIO_BROADCAST_SINK_ACTION_PA_SYNC,

    /** Broadcast Sink Action Type: BIG sync */
    BLE_AUDIO_BROADCAST_SINK_ACTION_BIG_SYNC,

    /** Broadcast Sink Action Type: BIS sync */
    BLE_AUDIO_BROADCAST_SINK_ACTION_BIS_SYNC,

    /** Broadcast Sink Action Type: Start discovery (scan) */
    BLE_AUDIO_BROADCAST_SINK_ACTION_DISC_START,

    /** Broadcast Sink Action Type: Start discovery (scan) */
    BLE_AUDIO_BROADCAST_SINK_ACTION_DISC_STOP,
};

struct ble_audio_broadcast_sink_action {
    /**
     * Indicates the type of action that is requested.
     */
    enum ble_audio_broadcast_sink_action_type type;

    /**
     * A discriminated union containing additional details concerning the action.
     * The 'type' field indicates which member of the union is valid.
     */
    union {
        /**
         * Represents PA Sync parameters request.
         *
         * The action triggered on locally or remotely initiated PA synchronization request.
         * The application should initialize the `out_parameters`, or abort the process.
         *
         * Valid for the following action types:
         *     o BLE_AUDIO_BROADCAST_SINK_ACTION_PA_SYNC
         *
         * Return:
         *     o 0 on success;
         *     o A non-zero value to abort.
         */
        struct {
            /** Pointer to Periodic Sync parameters to initialize. */
            struct ble_gap_periodic_sync_params *out_params;
        } pa_sync;

        /**
         * Represents BIG Sync request.
         *
         * The action triggered on locally or remotely initiated BIG synchronization request.
         * The application should provide the `out_mse` and `out_sync_timeout`,
         * or reject the request.
         *
         * Valid for the following action types:
         *     o BLE_AUDIO_BROADCAST_SINK_ACTION_BIG_SYNC
         *
         * Return:
         *     o 0 on success;
         *     o A non-zero value to abort.
         */
        struct {
            /** Source ID. */
            uint8_t source_id;

            /** ISO Interval. */
            uint16_t iso_interval;

            /** Presentation delay. */
            uint32_t presentation_delay;

            /** Number of SubEvents. The total number of subevents that are used to transmit BIS Data. */
            uint8_t nse;

            /** Burst Number. The number of new payloads for each BIS in a BIS event. */
            uint8_t bn;

            /**
             * Pointer to Maximum subevents value to initialize.
             * Range: 0x00 to 0x1F.
             * Default: 0x00, meaning the Controller can schedule reception of any number of subevents up to NSE.
             */
            uint8_t *out_mse;

            /**
             * Pointer to Sync Timeout value to initialize.
             * Range: 0x000A to 0x4000.
             * Default: @ref iso_interval * 6.
             */
            uint16_t *out_sync_timeout;
        } big_sync;

        /**
         * Represents BIS Sync request.
         *
         * The action triggered on locally or remotely initiated BIS synchronization request.
         * The application should provide the `out_cb` and optionally `out_cb_arg`,
         * or reject the request.
         *
         * @note The `subgroup` object as well as it's `base` object,
         *       therefore must be copied to in order to cache its information.
         *
         * Valid for the following action types:
         *     o BLE_AUDIO_BROADCAST_SINK_ACTION_BIS_SYNC
         *
         * Return:
         *     o 0 on success;
         *     o A non-zero value to abort.
         */
        struct {
            /** Source ID. */
            uint8_t source_id;

            /** Subgroup index. */
            uint8_t subgroup_index;

            /** Broadcast Audio Source Endpoint BIS. */
            const struct ble_audio_base_bis *bis;

            /** Broadcast Audio Source Endpoint Subgroup. */
            const struct ble_audio_base_subgroup *subgroup;
        } bis_sync;

        /**
         * Represents discovery start request.
         *
         * The action triggered on locally as part of PA synchronization process.
         *
         * Valid for the following action types:
         *     o BLE_AUDIO_BROADCAST_SINK_ACTION_SCAN_START
         *
         * Return:
         *     o 0 on success;
         *     o A non-zero value to abort.
         */
        struct {
            /** Preferred extended discovery parameters. */
            const struct ble_gap_ext_disc_params *params_preferred;
        } disc_start;
    };
};

/**
 * Prototype of Broadcast Sink action callback.
 * This function shall return 0 if operation is accepted, and error code if rejected.
 */
typedef int ble_audio_broadcast_sink_action_fn(struct ble_audio_broadcast_sink_action *action,
                                               void *arg);

/**
 * @brief Sets the application callback function.
 *
 * This function sets the callback function and its argument that will be called
 * when a Broadcast Sink action is triggered.
 *
 * @param cb Pointer to the callback function of type ble_audio_scan_delegator_ev_cb.
 * @param arg Pointer to the argument to be passed to the callback function.
 *
 * @return Returns 0 on success, or a non-zero error code otherwise.
 */
int ble_audio_broadcast_sink_cb_set(ble_audio_broadcast_sink_action_fn *cb, void *arg);

/** Sink Add function parameters */
struct ble_audio_broadcast_sink_add_params {
    /** Broadcast Code */
    uint8_t broadcast_code[BLE_AUDIO_BROADCAST_CODE_SIZE];

    /** Broadcast Code parameter is valid */
    uint8_t broadcast_code_is_valid : 1;
};

/**
 * @brief Start audio broadcast sink synchronization with the source.
 *
 * This function synchronizes the audio broadcast sink with the source
 * identified by the given source ID.
 * The source can be added locally using @ref ble_svc_audio_bass_receive_state_add function
 * or requested by remote device.
 *
 * @param source_id             Source ID of Broadcast Source to synchronize to.
 * @param params                Parameters to be used.
 *
 * @return                      0 on success;
 *                              BLE_HS_ENOENT if the source ID is invalid;
 *                              BLE_HS_EDONE if synced already;
 *                              BLE_HS_EALREADY if the synchronization is in progress;
 *                              BLE_HS_ENOMEM if memory allocation fails;
 *                              Any other non-zero value on failure.
 */
int ble_audio_broadcast_sink_start(uint8_t source_id,
                                   const struct ble_audio_broadcast_sink_add_params *params);

/**
 * @brief Stop audio broadcast sink synchronization.
 *
 * This function terminates or aborts the pending synchronization with the source
 * identified by the given source ID.
 *
 * @param source_id             Source ID of Broadcast Source to synchronize to.
 *
 * @return                      0 on success;
 *                              BLE_HS_ENOENT if the source ID is invalid;
 *                              Any other non-zero value on failure.
 */
int ble_audio_broadcast_sink_stop(uint8_t source_id);

/** Metadata Update function parameters */
struct ble_audio_broadcast_sink_metadata_update_params {
    /** Subgroup index */
    uint8_t subgroup_index;

    /** Scan Delegator Subgroup: Metadata */
    uint8_t *metadata;

    /** Scan Delegator Subgroup: Metadata length */
    uint8_t metadata_length;
};

/**
 * @brief Sets audio broadcast sink metadata.
 *
 * This function updates the broadcast sink metadata identified by the given source ID.
 *
 * @param source_id             Source ID of Broadcast Source.
 * @param params                Parameters to be used.
 *
 * @return                      0 on success;
 *                              BLE_HS_ENOENT if the source ID is invalid;
 *                              Any other non-zero value on failure.
 */
int ble_audio_broadcast_sink_metadata_update(uint8_t source_id,
                                             const struct ble_audio_broadcast_sink_metadata_update_params *params);

/**
 * @brief Initialize Broadcast Sink
 *
 * This function is restricted to be called by sysinit.
 *
 * @return Returns 0 on success, or a non-zero error code otherwise.
 */
int ble_audio_broadcast_sink_init(void);
#endif /* H_BLE_AUDIO_BROADCAST_SINK_ */
