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

#include <stdint.h>
#include "host/ble_gap.h"
#include "host/ble_audio_common.h"
#include "host/ble_audio_broadcast.h"

struct ble_svc_auracast_create_params {
    /** Broadcast Audio Source Endpoint */
    struct ble_audio_base *base;

    /** BIG parameters */
    struct ble_iso_big_params *big_params;

    /** Broadcast name - null terminated.
     * Set NULL to not include in advertising.
     * Length must be in range of 4 to 32 chars.
     */
    const char *name;

    /** Own address type to be used by advertising instance */
    uint8_t own_addr_type;

    /** PHY to be used for auxiliary advertisements  */
    uint8_t secondary_phy;

    /** Advertising Set ID */
    uint8_t sid;

    /** Frame duration, in us */
    uint16_t frame_duration;

    /** sampling frequency, in Hz */
    uint16_t sampling_frequency;

    /** bitrate, in Hz */
    uint32_t bitrate;

    /** Program info - null terminated.
     * Set NULL to not include in advertising.
     */
    const char *program_info;
};

/**
 * @brief Create Auracast Endpoint and configure advertising instance
 *
 * This function configures advertising instance for extended and periodic
 * advertisements to be ready for Auracast broadcast.
 *
 * @param[in] params                Pointer to a `ble_svc_auracast_create_params`
 *                                  structure that defines BIG and broadcast name.
 * @param[out] auracast_instance    Pointer to a advertising instance used by
 *                                  created Auracast.
 * @param[in] destroy_cb            Optional callback to be called when Auracast
 *                                  advertisement is destroyed.
 * @param[in] args                  Optional arguments to be passed to `destroy_cb`
 * @param[in] gap_cb                GAP event callback to be associated with
 *                                  Auracast advertisement.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_svc_auracast_create(const struct
                            ble_svc_auracast_create_params *params,
                            uint8_t *auracast_instance,
                            ble_audio_broadcast_destroy_fn *destroy_cb,
                            void *args,
                            ble_gap_event_fn *gap_cb);
/**
 * @brief Terminate all active advertisements and free resources associated
 * with given Auracast broadcast.
 *
 * This function stops Auracast advertisement by disabling extended and
 * periodic advertising and terminates them. After return advertising instance
 * is free and must be configured again for future advertisements.
 *
 * @param[in] auracast_instance     Pointer to a advertising instance used by
 *                                  Auracast.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_svc_auracast_terminate(uint8_t auracast_instance);

/**
 * @brief Start advertisements for given Auracast broadcast
 *
 * This function starts Auracast broadcast on by enabling extended and periodic
 * advertising.
 *
 * @param[in] auracast_instance      Pointer to a advertising instance used by
 *                                  Auracast.
 * @param[in] cb                    Pointer to an ISO event handler.
 * @param[in] cb_arg                Arguments to an ISO event handler.
 *
 * @return                          0 on success;
 *                                  A non-zero value on failure.
 */
int ble_svc_auracast_start(uint8_t auracast_instance,
                           ble_iso_event_fn *cb, void *cb_arg);

/**
 * @brief Stop advertisements for given Auracast broadcast
 *
 * This function stops Auracast broadcast by disabling extended and periodic
 * advertising. Advertising instance is still configured and ready for resume.
 *
 * @param[in] auracast_instance     Pointer to a advertising instance used by
 *                                  Auracast.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_svc_auracast_stop(uint8_t auracast_instance);
