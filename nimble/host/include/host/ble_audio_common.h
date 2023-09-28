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

#ifndef H_BLE_AUDIO_COMMON_
#define H_BLE_AUDIO_COMMON_

#include "stdint.h"
#include "os/queue.h"

/** Helper macros for BLE_AUDIO_BUILD_CODEC_CONFIG */
#define FIELD_LEN_2(_len, _type, _field)     _len, _type, _field,
#define FIELD_LEN_5(_len, _type, _field)     _len, _type, _field,       \
    _field >> 8, _field >> 16, \
    _field >> 24,

#define FIELD_TESTED_0(_len, _type, _field)
#define FIELD_TESTED_1(_len, _type, _field)     FIELD_LEN_ ## _len(_len,  \
                                                                   _type, \
                                                                   _field)
#define EMPTY()         FIELD_TESTED_0
#define PRESENT(X)      FIELD_TESTED_1
#define TEST(x, A, FUNC, ...)    FUNC
#define TEST_FIELD(...)              TEST(, ## __VA_ARGS__,     \
                                          PRESENT(__VA_ARGS__), \
                                          EMPTY(__VA_ARGS__))
#define FIELD_TESTED(_test, _len, _type, _field)    _test(_len, _type, _field)
#define OPTIONAL_FIELD(_len, _type, ...) FIELD_TESTED(TEST_FIELD     \
                                                      (__VA_ARGS__), \
                                                      _len,          \
                                                      _type,         \
                                                      __VA_ARGS__)

#define BLE_BROADCAST_AUDIO_ANNOUNCEMENT_SVC_UUID                 0x1852

#define BLE_AUDIO_SAMPLING_RATE_8000_HZ                           0x01
#define BLE_AUDIO_SAMPLING_RATE_11025_HZ                          0x02
#define BLE_AUDIO_SAMPLING_RATE_16000_HZ                          0x03
#define BLE_AUDIO_SAMPLING_RATE_22050_HZ                          0x04
#define BLE_AUDIO_SAMPLING_RATE_24000_HZ                          0x05
#define BLE_AUDIO_SAMPLING_RATE_32000_HZ                          0x06
#define BLE_AUDIO_SAMPLING_RATE_44100_HZ                          0x07
#define BLE_AUDIO_SAMPLING_RATE_48000_HZ                          0x08
#define BLE_AUDIO_SAMPLING_RATE_88200_HZ                          0x09
#define BLE_AUDIO_SAMPLING_RATE_96000_HZ                          0x0A
#define BLE_AUDIO_SAMPLING_RATE_176400_HZ                         0x0B
#define BLE_AUDIO_SAMPLING_RATE_192000_HZ                         0x0C
#define BLE_AUDIO_SAMPLING_RATE_384000_HZ                         0x0D

#define BLE_AUDIO_SELECTED_FRAME_DURATION_7_5_MS                  0x00
#define BLE_AUDIO_SELECTED_FRAME_DURATION_10_MS                   0x01

#define BLE_AUDIO_LOCATION_FRONT_LEFT                             (1ULL)
#define BLE_AUDIO_LOCATION_FRONT_RIGHT                            (1ULL << 1)
#define BLE_AUDIO_LOCATION_FRONT_CENTER                           (1ULL << 2)
#define BLE_AUDIO_LOCATION_LOW_FREQ_EFFECTS_1                     (1ULL << 3)
#define BLE_AUDIO_LOCATION_BACK_LEFT                              (1ULL << 4)
#define BLE_AUDIO_LOCATION_FRONT_LEFT_CENTER                      (1ULL << 5)
#define BLE_AUDIO_LOCATION_FRONT_RIGHT_CENTER                     (1ULL << 6)
#define BLE_AUDIO_LOCATION_BACK_CENTER                            (1ULL << 7)
#define BLE_AUDIO_LOCATION_LOW_FREQ_EFFECTS_2                     (1ULL << 8)
#define BLE_AUDIO_LOCATION_SIDE_LEFT                              (1ULL << 9)
#define BLE_AUDIO_LOCATION_SIDE_RIGHT                             (1ULL << 10)
#define BLE_AUDIO_LOCATION_TOP_FRONT_LEFT                         (1ULL << 11)
#define BLE_AUDIO_LOCATION_TOP_FRONT_RIGHT                        (1ULL << 12)
#define BLE_AUDIO_LOCATION_TOP_FRONT_CENTER                       (1ULL << 13)
#define BLE_AUDIO_LOCATION_TOP_CENTER                             (1ULL << 14)
#define BLE_AUDIO_LOCATION_TOP_BACK_LEFT                          (1ULL << 15)
#define BLE_AUDIO_LOCATION_TOP_BACK_RIGHT                         (1ULL << 16)
#define BLE_AUDIO_LOCATION_TOP_SIDE_LEFT                          (1ULL << 17)
#define BLE_AUDIO_LOCATION_TOP_SIDE_RIGHT                         (1ULL << 18)
#define BLE_AUDIO_LOCATION_TOP_BACK_CENTER                        (1ULL << 19)
#define BLE_AUDIO_LOCATION_BOTTOM_FRONT_CENTER                    (1ULL << 20)
#define BLE_AUDIO_LOCATION_BOTTOM_FRONT_LEFT                      (1ULL << 21)
#define BLE_AUDIO_LOCATION_BOTTOM_FRONT_RIGHT                     (1ULL << 22)
#define BLE_AUDIO_LOCATION_LEFT_SURROUND                          (1ULL << 23)
#define BLE_AUDIO_LOCATION_RIGHT_SURROUND                         (1ULL << 24)

#define BLE_AUDIO_CODEC_SAMPLING_FREQ_TYPE                        0x01
#define BLE_AUDIO_CODEC_FRAME_DURATION_TYPE                       0x02
#define BLE_AUDIO_CODEC_AUDIO_CHANNEL_ALLOCATION_TYPE             0x03
#define BLE_AUDIO_CODEC_OCTETS_PER_CODEC_FRAME_TYPE               0x04
#define BLE_AUDIO_CODEC_FRAME_BLOCKS_PER_SDU_TYPE                 0x05

/**
 * @brief Helper macro used to build LTV array of Codec_Specific_Configuration.
 *
 * @param _sampling_freq               Sampling_Frequency - single octet value
 * @param _frame_duration              Frame_Duration - single octet value
 * @param _audio_channel_alloc         Audio_Channel_Allocation -
 *                                     four octet value
 * @param _octets_per_codec_frame      Octets_Per_Codec_Frame -
 *                                     two octet value
 * @param _codec_frame_blocks_per_sdu  Codec_Frame_Blocks_Per_SDU -
 *                                     single octet value
 *
 * @return          Pointer to a `ble_uuid16_t` structure.
 */
#define BLE_AUDIO_BUILD_CODEC_CONFIG(_sampling_freq,                          \
                                     _frame_duration,                         \
                                     _audio_channel_alloc,                    \
                                     _octets_per_codec_frame,                 \
                                     _codec_frame_blocks_per_sdu)             \
    {                                                                         \
        2, BLE_AUDIO_CODEC_SAMPLING_FREQ_TYPE, _sampling_freq,                \
        2, BLE_AUDIO_CODEC_FRAME_DURATION_TYPE, _frame_duration,              \
        OPTIONAL_FIELD(5, BLE_AUDIO_CODEC_AUDIO_CHANNEL_ALLOCATION_TYPE,      \
                       _audio_channel_alloc)                                  \
        3, BLE_AUDIO_CODEC_OCTETS_PER_CODEC_FRAME_TYPE,                       \
        (_octets_per_codec_frame), ((_octets_per_codec_frame) >> 8),          \
        OPTIONAL_FIELD(2, BLE_AUDIO_CODEC_FRAME_BLOCKS_PER_SDU_TYPE,          \
                       _codec_frame_blocks_per_sdu)                           \
    }

struct ble_audio_codec_id {
    /** Coding Fromat */
    uint8_t format;

    /** Company ID */
    uint16_t company_id;

    /** Vendor Specific Codec ID */
    uint16_t vendor_specific;
};

struct ble_audio_bis {
    /** Pointer to next BIS in subgroup */
    STAILQ_ENTRY(ble_audio_bis) next;

    /** BIS index */
    uint8_t idx;

    /** BIS level Codec Specific Configuration */
    uint8_t codec_spec_config_len;

    /** BIS level Codec Specific Configuration length */
    uint8_t *codec_spec_config;
};

struct ble_audio_big_subgroup {
    /** Pointer to next subgroup in BIG */
    STAILQ_ENTRY(ble_audio_big_subgroup) next;

    /** Number of BISes in subgroup */
    uint8_t bis_cnt;

    /** Codec ID */
    struct ble_audio_codec_id codec_id;

    /** Subgroup level Codec Specific Configuration */
    uint8_t *codec_spec_config;

    /** Subgroup level Codec Specific Configuration length */
    uint8_t codec_spec_config_len;

    /** Subgroup Metadata */
    uint8_t *metadata;

    /** Subgroup Metadata length*/
    uint8_t metadata_len;

    /** Link list of BISes */
    STAILQ_HEAD(, ble_audio_bis) bises;
};

struct ble_audio_base {
    /** Broadcast ID */
    uint32_t broadcast_id;

    /** Presentation Delay */
    uint32_t presentation_delay;

    /** Number of subgroups in BIG */
    uint8_t num_subgroups;

    /** Link list of subgroups */
    STAILQ_HEAD(, ble_audio_big_subgroup) subs;
};

#endif /* H_BLE_AUDIO_COMMON_ */
