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

#include <stdint.h>

#include "host/ble_gap.h"
#include "host/ble_iso.h"
#include "audio/ble_audio.h"

#include "nimble/ble.h"

/** @brief Audio Broadcast Sink create parameters */
struct ble_audio_broadcast_sink_create_params {
    /** Advertiser Address for the Broadcast Source */
    const ble_addr_t *adv_addr;

    /** Advertising Set ID */
    uint8_t adv_sid;

    /** Broadcast ID */
    uint32_t broadcast_id : 24;

    /** Callback function */
    ble_audio_event_fn *cb;

    /** The optional argument to pass to the callback function. */
    void *cb_arg;
};

/**
 * @brief Create Broadcast Audio Sink instance
 *
 * This function allocates Broadcast Audio Sink instance.
 *
 * @param[in] params            Pointer to a `ble_audio_broadcast_sink_create_params`
 *                              structure that provides information about
 *                              remote Audio Broadcast.
 * @param[out] instance_id      Newly allocated Broadcast Audio Sink instance ID.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_broadcast_sink_create(const struct ble_audio_broadcast_sink_create_params *params,
                                    uint8_t *instance_id);

/**
 * @brief Destroy Broadcast Audio Sink instance
 *
 * This function free's Broadcast Audio Sink instance.
 * The function terminates Periodic Advertisement Sync if any and terminates
 * BIG Sync if any.
 *
 * @param[in] instance_id       Broadcast Audio Sink instance ID.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_broadcast_sink_destroy(uint8_t instance_id);

/**
 * @brief Synchronize to Periodic Advertisement
 *
 * This function initiates Periodic Advertisement Sync to receive
 * the BIGInfo and Broadcast Audio Source Endpoint (BASE) reports.
 *
 * @param[in] instance_id       Broadcast Audio Sink instance ID.
 * @param[in] params            Periodic Advertisement Sync parameters to use.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_broadcast_sink_pa_sync(uint8_t instance_id,
                                     const struct ble_gap_periodic_sync_params *params);

/**
 * @brief Terminate or cancel pending Periodic Advertisement Sync
 *
 * This function terminates active Periodic Advertisement Sync or cancels the
 * pending sync.
 *
 * @param[in] instance_id       Broadcast Audio Sink instance ID.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_broadcast_sink_pa_sync_term(uint8_t instance_id);

/** @brief BIS Sync parameters */
struct ble_audio_broadcast_sink_bis_params {
    /** BIS index */
    uint8_t bis_index;

    /** ISO data received callback */
    ble_iso_event_fn *cb;

    /** Callback argument */
    void *cb_arg;
};

/* Forward declaration of the ble_audio_broadcast_sink_big_sync_params structure */
struct ble_audio_broadcast_sink_big_sync_params;

/** @typedef ble_audio_broadcast_sink_big_sync_params_destroy_t
 *  @brief Broadcast Sink Sync parameters destroy callback.
 *
 *  @param params Parameters to destroy.
 */
typedef void (*ble_audio_broadcast_sink_big_sync_params_destroy_t)(
        struct ble_audio_broadcast_sink_big_sync_params *params);

/** @brief Broadcast Sink Sync parameters */
struct ble_audio_broadcast_sink_big_sync_params {
    /** Maximum Subevents to be used to receive data payloads in each BIS event */
    uint8_t mse;

    /** The maximum permitted time between successful receptions of BIS PDUs */
    uint16_t sync_timeout;

    /** Number of BISes */
    uint8_t num_bis;

    /** BIS parameters */
    struct ble_audio_broadcast_sink_bis_params *bis_params;

    /** Parameters destroy callback */
    ble_audio_broadcast_sink_big_sync_params_destroy_t destroy;
};

/**
 * @brief Synchronize to Audio Broadcast
 *
 * This function is used to synchronize to Audio Broadcast to start
 * reception of audio data.
 *
 * @param[in] instance_id       Broadcast Audio Sink instance ID.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_broadcast_sink_big_sync(uint8_t instance_id,
                                      struct ble_audio_broadcast_sink_big_sync_params *params);

/**
 * @brief Terminate or cancel pending Audio Broadcast synchronization
 *
 * This function terminates active Audio Broadcast sync or cancels the
 * pending sync.
 *
 * @param[in] instance_id       Broadcast Audio Sink instance ID.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_broadcast_sink_big_sync_term(uint8_t instance_id);

/** Calculates sync timeout for the periodic advertising train in 10ms units */

/**
 * @brief Calculate sync timeout in 10ms units
 *
 * This helper function calculates sync timeout in 10ms unit.
 *
 * @param[in] interval
 * @param[in] retry_count
 *
 * @return                      timeout in 10ms.
 */
uint16_t ble_audio_broadcast_sink_sync_timeout_calc(uint16_t interval,
                                                    uint8_t retry_count);

/**
 * @brief Initialize Broadcast Sink role
 *
 * This function is restricted to be called by sysinit.
 */
int ble_audio_broadcast_sink_init(void);
#endif /* H_BLE_AUDIO_BROADCAST_SINK_ */
