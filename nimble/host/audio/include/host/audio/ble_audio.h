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

#ifndef H_BLE_AUDIO_
#define H_BLE_AUDIO_

#include <stdint.h>

#include "host/ble_audio_common.h"

/**
 * BASE iterator
 *
 * The iterator structure used by @ref ble_audio_base_subgroup_iter and
 * @ble_audio_base_bis_iter functions to iterate the BASE Level 2 and 3 elements
 * (Subgroups and BISes).
 * This should be used as an opaque structure and not modified manually.
 *
 * Example:
 * @code{.c}
 * struct ble_audio_base_iter subgroup_iter;
 * struct ble_audio_base_iter bis_iter;
 * struct ble_audio_base_group group;
 * struct ble_audio_base_subgroup subgroup;
 * struct ble_audio_base_bis bis;
 *
 * rc = ble_audio_base_parse(data, data_size, &group, &subgroup_iter);
 * if (rc == 0) {
 *     for (uint8_t i = 0; i < group->num_subgroups; i++) {
 *         rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
 *         if (rc == 0) {
 *             for (uint8_t j = 0; j < subgroup->num_bis; j++) {
 *                 rc = ble_audio_base_bis_iter(&bis_iter, &bis);
 *                 if (rc == 0) {
 *                     foo(&group, &subgroup, &bis);
 *                 }
 *             }
 *         }
 *     }
 * }
 * @endcode
 */
struct ble_audio_base_iter {
    /** Data pointer */
    const uint8_t *data;

    /** Base length */
    uint8_t buf_len;

    /** Original BASE pointer */
    const uint8_t *buf;

    /** Remaining number of elements */
    uint8_t num_elements;
};

/** @brief Broadcast Audio Source Endpoint Group structure */
struct ble_audio_base_group {
    /** Presentation Delay */
    uint32_t presentation_delay;

    /** Number of subgroups */
    uint8_t num_subgroups;
};

/**
 * Parse the BASE received from Basic Audio Announcement data.
 *
 * @param[in] data              Pointer to the BASE data buffer to parse.
 * @param[in] data_len          Length of the BASE data buffer.
 * @param[out] group            Group object.
 * @param[out] subgroup_iter    Subgroup iterator object.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_audio_base_parse(const uint8_t *data, uint8_t data_len,
                         struct ble_audio_base_group *group,
                         struct ble_audio_base_iter *subgroup_iter);

/** @brief Broadcast Audio Source Endpoint Subgroup structure */
struct ble_audio_base_subgroup {
    /** Codec information for the subgroup */
    struct ble_audio_codec_id codec_id;

    /** Length of the Codec Specific Configuration for the subgroup */
    uint8_t codec_spec_config_len;

    /** Codec Specific Configuration for the subgroup */
    const uint8_t *codec_spec_config;

    /** Length of the Metadata for the subgroup */
    uint8_t metadata_len;

    /** Series of LTV structures containing Metadata */
    const uint8_t *metadata;

    /** Number of BISes in the subgroup */
    uint8_t num_bis;
};

/**
 * @brief Basic Audio Announcement Subgroup information
 *
 * @param[in] subgroup_iter     Subgroup iterator object.
 * @param[out] subgroup         Subgroup object.
 * @param[out] bis_iter         BIS iterator object.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_base_subgroup_iter(struct ble_audio_base_iter *subgroup_iter,
                                 struct ble_audio_base_subgroup *subgroup,
                                 struct ble_audio_base_iter *bis_iter);

/** @brief Broadcast Audio Source Endpoint BIS structure */
struct ble_audio_base_bis {
    /** BIS_index value for the BIS */
    uint8_t index;

    /** Length of the Codec Specific Configuration for the BIS */
    uint8_t codec_spec_config_len;

    /** Codec Specific Configuration for the BIS */
    const uint8_t *codec_spec_config;
};

/**
 * @brief Basic Audio Announcement Subgroup information
 *
 * @param[in] bis_iter          BIS iterator object.
 * @param[out] bis              BIS object.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_base_bis_iter(struct ble_audio_base_iter *bis_iter,
                            struct ble_audio_base_bis *bis);

#endif /* H_BLE_AUDIO_ */
