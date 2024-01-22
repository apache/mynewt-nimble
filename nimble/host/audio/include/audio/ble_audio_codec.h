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

#ifndef H_BLE_AUDIO_CODEC_
#define H_BLE_AUDIO_CODEC_

/**
 * @file ble_audio_codec.h
 *
 * @brief Bluetooth LE Audio Codec
 *
 * This header file provides the public API for managing LE Audio Codecs
 *
 * @defgroup ble_audio_codec Bluetooth LE Audio Codec
 * @ingroup bt_host
 * @{
 *
 * This API allows to create and manage list of LE Audio codecs with their capabilities and
 * metadata. This list can be used by higher level services, like PACS. Memory management of
 * codec entries is left to application and neither static nor dynamic allocation is enforced.
 *
 */

#include "stdint.h"
#include "ble_audio.h"

/** Bit describing direction of codec configuration - source */
#define BLE_AUDIO_CODEC_DIR_SOURCE_BIT             (1 << 0)
/** Bit describing direction of codec configuration - sink */
#define BLE_AUDIO_CODEC_DIR_SINK_BIT               (1 << 1)

/** Codec list entry */
struct ble_audio_codec_record {
    /* Pointer to next codec list entry */
    STAILQ_ENTRY(ble_audio_codec_record) next;

    /* Codec ID */
    struct ble_audio_codec_id codec_id;

    /* Length of Codec Specific Capabilities */
    uint8_t codec_spec_caps_len;

    /* Codec Specific Capabilities data */
    const uint8_t *codec_spec_caps;

    /* Metadata length */
    uint8_t metadata_len;

    /* Metadata */
    const uint8_t *metadata;

    /* Bitfield describing direction that codec is acting on. It is a logical OR of:
     *  - BLE_AUDIO_CODEC_DIR_SOURCE_BIT
     *  - BLE_AUDIO_CODEC_DIR_SINK_BIT
     */
    uint8_t direction;
};

/** Type definition codec iteration callback function. */
typedef int ble_audio_codec_foreach_fn(const struct ble_audio_codec_record *record, void *arg);

struct ble_audio_codec_register_params {
    /* Codec ID structure */
    struct ble_audio_codec_id codec_id;

    /* Codec Specific Capabilities length */
    uint8_t codec_spec_caps_len;

    /* Codec Specific Capabilities data */
    uint8_t *codec_spec_caps;

    /* Metadata length */
    uint8_t metadata_len;

    /* Metadata */
    uint8_t *metadata;

    /* Bitfield describing direction that codec is acting on. It is a logical OR of:
     *  - BLE_AUDIO_CODEC_DIR_SOURCE_BIT
     *  - BLE_AUDIO_CODEC_DIR_SINK_BIT
     */
    uint8_t direction;
};

/**
 * @brief Register codec entry
 *
 * @param[in] params                    Pointer to a `ble_audio_codec_register_params`
 *                                      structure that defines Codec Specific Capabilities
 * @param[out] out_record               Pointer to registered codec entry.
 *
 * @return                              0 on success;
 *                                      A non-zero value on failure.
 */
int ble_audio_codec_register(const struct ble_audio_codec_register_params
                             *params,
                             struct ble_audio_codec_record *out_record);
/**
 * @brief Remove codec entry from register
 *
 * @param[in] codec_record              Pointer to registered codec entry.
 *
 * @return                              0 on success;
 *                                      A non-zero value on failure.
 */
int ble_audio_codec_unregister(struct ble_audio_codec_record *codec_record);

/**
 * @brief Iterate through all registered codecs and call function on every
 *        one of them.
 *
 * @param[in] direction                 Codec entry direction. It is any
 *                                      combination of following bits:
 *                                          - BLE_AUDIO_CODEC_DIR_SOURCE_BIT
 *                                          - BLE_AUDIO_CODEC_DIR_SINK_BIT
 *                                      This filters entries so the callback is called
 *                                      only on these have matching direction bit set.
 * @param[in] cb                        Callback to be called on codec entries.
 * @param[in] arg                       Optional callback argument.
 *
 * @return                              0 on success;
 *                                      A non-zero value on failure.
 */
int ble_audio_codec_foreach(uint8_t direction, ble_audio_codec_foreach_fn *cb, void *arg);

/**
 * @}
 */

#endif /* H_BLE_AUDIO_CODEC_ */
