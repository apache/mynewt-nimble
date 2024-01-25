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
#include <sys/queue.h>

/**
 * @file ble_audio.h
 *
 * @brief Bluetooth Low Energy Audio API
 *
 * @defgroup bt_le_audio Bluetooth LE Audio
 * @ingroup bt_host
 * @{
 */

/**
 * @cond
 * Helper macros for BLE_AUDIO_BUILD_CODEC_CONFIG
 * @private @{
 */
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

/**
 * @}
 * @endcond
 */

/** Broadcast Audio Broadcast Code Size. */
#define BLE_AUDIO_BROADCAST_CODE_SIZE                             16

/** Basic Audio Announcement Service UUID. */
#define BLE_BASIC_AUDIO_ANNOUNCEMENT_SVC_UUID                     0x1851

/** Broadcast Audio Announcement Service UUID. */
#define BLE_BROADCAST_AUDIO_ANNOUNCEMENT_SVC_UUID                 0x1852

/** Public Broadcast Announcement Service UUID. */
#define BLE_BROADCAST_PUB_ANNOUNCEMENT_SVC_UUID                   0x1856

/**
 * @defgroup ble_audio_codec_formats Bluetooth Low Energy Audio Codec Formats
 * @{
 */

/** BLE Audio codec format - μ-law log */
#define BLE_AUDIO_CODEC_FORMAT_MU_LAW_LOG                         0x00

/** BLE Audio codec format - A-law log */
#define BLE_AUDIO_CODEC_FORMAT_A_LAW_LOG                          0x01

/** BLE Audio codec format - CVSD */
#define BLE_AUDIO_CODEC_FORMAT_CVSD                               0x02

/** BLE Audio codec format - Transparent */
#define BLE_AUDIO_CODEC_FORMAT_TRANSPARENT                        0x03

/** BLE Audio codec format - Linear PCM */
#define BLE_AUDIO_CODEC_FORMAT_LINEAR_PCM                         0x04

/** BLE Audio codec format - mSBC  */
#define BLE_AUDIO_CODEC_FORMAT_MSBC                               0x05

/** BLE Audio codec format - LC3 */
#define BLE_AUDIO_CODEC_FORMAT_LC3                                0x06

/** BLE Audio codec format - G.729A */
#define BLE_AUDIO_CODEC_FORMAT_G_729A                             0x07

/** BLE Audio codec format - Vendor Specific */
#define BLE_AUDIO_CODEC_FORMAT_VENDOR_SPECIFIC                    0xFF

/** @} */

/**
 * @defgroup ble_audio_sampling_rates Bluetooth Low Energy Audio Sampling Rates
 * @{
 */

/** LE Audio Sampling Rate: 8000 Hz. */
#define BLE_AUDIO_SAMPLING_RATE_8000_HZ                           0x01

/** LE Audio Sampling Rate: 11025 Hz. */
#define BLE_AUDIO_SAMPLING_RATE_11025_HZ                          0x02

/** LE Audio Sampling Rate: 16000 Hz. */
#define BLE_AUDIO_SAMPLING_RATE_16000_HZ                          0x03

/** LE Audio Sampling Rate: 22050 Hz. */
#define BLE_AUDIO_SAMPLING_RATE_22050_HZ                          0x04

/** LE Audio Sampling Rate: 24000 Hz. */
#define BLE_AUDIO_SAMPLING_RATE_24000_HZ                          0x05

/** LE Audio Sampling Rate: 32000 Hz. */
#define BLE_AUDIO_SAMPLING_RATE_32000_HZ                          0x06

/** LE Audio Sampling Rate: 44100 Hz. */
#define BLE_AUDIO_SAMPLING_RATE_44100_HZ                          0x07

/** LE Audio Sampling Rate: 48000 Hz. */
#define BLE_AUDIO_SAMPLING_RATE_48000_HZ                          0x08

/** LE Audio Sampling Rate: 88200 Hz. */
#define BLE_AUDIO_SAMPLING_RATE_88200_HZ                          0x09

/** LE Audio Sampling Rate: 96000 Hz. */
#define BLE_AUDIO_SAMPLING_RATE_96000_HZ                          0x0A

/** LE Audio Sampling Rate: 176400 Hz. */
#define BLE_AUDIO_SAMPLING_RATE_176400_HZ                         0x0B

/** LE Audio Sampling Rate: 192000 Hz. */
#define BLE_AUDIO_SAMPLING_RATE_192000_HZ                         0x0C

/** LE Audio Sampling Rate: 384000 Hz. */
#define BLE_AUDIO_SAMPLING_RATE_384000_HZ                         0x0D

/** @} */

/**
 * @defgroup ble_audio_frame_durations Bluetooth Low Energy Audio Frame Durations
 * @{
 */

/** LE Audio Frame Duration: 7.5 ms. */
#define BLE_AUDIO_SELECTED_FRAME_DURATION_7_5_MS                  0x00

/** LE Audio Frame Duration: 10 ms. */
#define BLE_AUDIO_SELECTED_FRAME_DURATION_10_MS                   0x01

/** @} */

/**
 * @defgroup ble_audio_locations Bluetooth Low Energy Audio Locations
 * @{
 */

/** LE Audio Location: Front Left. */
#define BLE_AUDIO_LOCATION_FRONT_LEFT                             (1ULL)

/** LE Audio Location: Front Right. */
#define BLE_AUDIO_LOCATION_FRONT_RIGHT                            (1ULL << 1)

/** LE Audio Location: Front Center. */
#define BLE_AUDIO_LOCATION_FRONT_CENTER                           (1ULL << 2)

/** LE Audio Location: Low Frequency Effects 1. */
#define BLE_AUDIO_LOCATION_LOW_FREQ_EFFECTS_1                     (1ULL << 3)

/** LE Audio Location: Back Left. */
#define BLE_AUDIO_LOCATION_BACK_LEFT                              (1ULL << 4)

/** LE Audio Location: Front Left Center. */
#define BLE_AUDIO_LOCATION_FRONT_LEFT_CENTER                      (1ULL << 5)

/** LE Audio Location: Front Right Center. */
#define BLE_AUDIO_LOCATION_FRONT_RIGHT_CENTER                     (1ULL << 6)

/** LE Audio Location: Back Center. */
#define BLE_AUDIO_LOCATION_BACK_CENTER                            (1ULL << 7)

/** LE Audio Location: Low Frequency Effects 2. */
#define BLE_AUDIO_LOCATION_LOW_FREQ_EFFECTS_2                     (1ULL << 8)

/** LE Audio Location: Side Left. */
#define BLE_AUDIO_LOCATION_SIDE_LEFT                              (1ULL << 9)

/** LE Audio Location: Side Right. */
#define BLE_AUDIO_LOCATION_SIDE_RIGHT                             (1ULL << 10)

/** LE Audio Location: Top Front Left. */
#define BLE_AUDIO_LOCATION_TOP_FRONT_LEFT                         (1ULL << 11)

/** LE Audio Location: Top Front Right. */
#define BLE_AUDIO_LOCATION_TOP_FRONT_RIGHT                        (1ULL << 12)

/** LE Audio Location: Top Front Center. */
#define BLE_AUDIO_LOCATION_TOP_FRONT_CENTER                       (1ULL << 13)

/** LE Audio Location: Top Center. */
#define BLE_AUDIO_LOCATION_TOP_CENTER                             (1ULL << 14)

/** LE Audio Location: Top Back Left. */
#define BLE_AUDIO_LOCATION_TOP_BACK_LEFT                          (1ULL << 15)

/** LE Audio Location: Top Back Right. */
#define BLE_AUDIO_LOCATION_TOP_BACK_RIGHT                         (1ULL << 16)

/** LE Audio Location: Top Side Left. */
#define BLE_AUDIO_LOCATION_TOP_SIDE_LEFT                          (1ULL << 17)

/** LE Audio Location: Top Side Right. */
#define BLE_AUDIO_LOCATION_TOP_SIDE_RIGHT                         (1ULL << 18)

/** LE Audio Location: Top Back Center. */
#define BLE_AUDIO_LOCATION_TOP_BACK_CENTER                        (1ULL << 19)

/** LE Audio Location: Bottom Front Center. */
#define BLE_AUDIO_LOCATION_BOTTOM_FRONT_CENTER                    (1ULL << 20)

/** LE Audio Location: Bottom Front Left. */
#define BLE_AUDIO_LOCATION_BOTTOM_FRONT_LEFT                      (1ULL << 21)

/** LE Audio Location: Bottom Front Right. */
#define BLE_AUDIO_LOCATION_BOTTOM_FRONT_RIGHT                     (1ULL << 22)

/** LE Audio Location: Left Surround. */
#define BLE_AUDIO_LOCATION_LEFT_SURROUND                          (1ULL << 23)

/** LE Audio Location: Right Surround. */
#define BLE_AUDIO_LOCATION_RIGHT_SURROUND                         (1ULL << 24)

/** @} */

/**
 * @defgroup ble_audio_codec_config Bluetooth Low Energy Audio Codec Specific Config
 * @{
 */

/** LE Audio Codec Config Type: Sampling Frequency. */
#define BLE_AUDIO_CODEC_CONF_SAMPLING_FREQ_TYPE                   0x01

/** LE Audio Codec Config Type: Frame Duration. */
#define BLE_AUDIO_CODEC_CONF_FRAME_DURATION_TYPE                  0x02

/** LE Audio Codec Config Type: Channel Allocation. */
#define BLE_AUDIO_CODEC_CONF_AUDIO_CHANNEL_ALLOCATION_TYPE        0x03

/** LE Audio Codec Config Type: Octets Per Codec Frame. */
#define BLE_AUDIO_CODEC_CONF_OCTETS_PER_CODEC_FRAME_TYPE          0x04

/** LE Audio Codec Config Type: Frame Blocks Per SDU. */
#define BLE_AUDIO_CODEC_CONF_FRAME_BLOCKS_PER_SDU_TYPE            0x05

/** @} */

/**
 * @defgroup ble_audio_codec_caps Bluetooth Low Energy Audio Codec Specific Capabilities
 * @{
 */


/** LE Audio Codec Specific Capability: Supported Sampling Frequencies. */
#define BLE_AUDIO_CODEC_CAPS_SAMPLING_FREQ_TYPE                   0x01

/** LE Audio Codec Specific Capability: Supported Frame Durations. */
#define BLE_AUDIO_CODEC_CAPS_FRAME_DURATION_TYPE                  0x02

/** LE Audio Codec Specific Capability: Supported Audio Channel Counts. */
#define BLE_AUDIO_CODEC_CAPS_SUP_AUDIO_CHANNEL_COUNTS_TYPE        0x03

/** LE Audio Codec Specific Capability: Supported Octets Per Codec Frame. */
#define BLE_AUDIO_CODEC_CAPS_OCTETS_PER_CODEC_FRAME_TYPE          0x04

/** LE Audio Codec Specific Capability: Supported Codec Frames Per SDU. */
#define BLE_AUDIO_CODEC_CAPS_FRAMES_PER_SDU_TYPE                  0x05

/** @} */

/**
 * @defgroup ble_audio_contexts Bluetooth Low Energy Audio Context Types
 * @{
 */

/** LE Audio Codec Context Type: Prohibited. */
#define BLE_AUDIO_CONTEXT_TYPE_PROHIBITED                         0x0000

/** LE Audio Codec Context Type: Unspecified. */
#define BLE_AUDIO_CONTEXT_TYPE_UNSPECIFIED                        0x0001

/** LE Audio Codec Context Type: Conversational. */
#define BLE_AUDIO_CONTEXT_TYPE_CONVERSATIONAL                     0x0002

/** LE Audio Codec Context Type: Media. */
#define BLE_AUDIO_CONTEXT_TYPE_MEDIA                              0x0004

/** LE Audio Codec Context Type: Game. */
#define BLE_AUDIO_CONTEXT_TYPE_GAME                               0x0008

/** LE Audio Codec Context Type: Instructional. */
#define BLE_AUDIO_CONTEXT_TYPE_INSTRUCTIONAL                      0x0010

/** LE Audio Codec Context Type: Voice Assistants. */
#define BLE_AUDIO_CONTEXT_TYPE_VOICE_ASSISTANTS                   0x0020

/** LE Audio Codec Context Type: Live. */
#define BLE_AUDIO_CONTEXT_TYPE_LIVE                               0x0040

/** LE Audio Codec Context Type: Sound Effects. */
#define BLE_AUDIO_CONTEXT_TYPE_SOUND_EFFECTS                      0x0080

/** LE Audio Codec Context Type: Notifications. */
#define BLE_AUDIO_CONTEXT_TYPE_NOTIFICATIONS                      0x0100

/** LE Audio Codec Context Type: Ringtone. */
#define BLE_AUDIO_CONTEXT_TYPE_RINGTONE                           0x0200

/** LE Audio Codec Context Type: Alerts. */
#define BLE_AUDIO_CONTEXT_TYPE_ALERTS                             0x0400

/** LE Audio Codec Context Type: EmergencyAlarm. */
#define BLE_AUDIO_CONTEXT_TYPE_EMERGENCY_ALARM                    0x0800

/** @} */

/**
 * @defgroup ble_audio_sup_frame_durations Bluetooth Low Energy Audio Supported Frame Durations
 * @{
 */

/** LE Audio Codec Supported Frame Duration: 7.5 ms frame duration. */
#define BLE_AUDIO_CODEC_SUPPORTED_FRAME_DURATION_7_5_MS           0x0001

/** LE Audio Codec Supported Frame Duration: 10 ms frame duration. */
#define BLE_AUDIO_CODEC_SUPPORTED_FRAME_DURATION_10_MS            0x0002

/** LE Audio Codec Supported Frame Duration: 7.5 ms preferred. */
#define BLE_AUDIO_CODEC_PREFERED_FRAME_DURATION_7_5_MS            0x0010

/** LE Audio Codec Supported Frame Duration: 10 ms preferred. */
#define BLE_AUDIO_CODEC_PREFERED_FRAME_DURATION_10_MS             0x0020

/** @} */

/**
 * @defgroup ble_audio_sup_sampling_freqs Bluetooth Low Energy Audio Supported Sampling Frequencies
 * @{
 */

/** LE Audio Codec Supported Sampling Frequency: 8000 Hz. */
#define BLE_AUDIO_CODEC_SUPPORTED_SAMPLING_RATE_8000_HZ           (1ULL << 0)

/** LE Audio Codec Supported Sampling Frequency: 11025 Hz. */
#define BLE_AUDIO_CODEC_SUPPORTED_SAMPLING_RATE_11025_HZ          (1ULL << 1)

/** LE Audio Codec Supported Sampling Frequency: 16000 Hz. */
#define BLE_AUDIO_CODEC_SUPPORTED_SAMPLING_RATE_16000_HZ          (1ULL << 2)

/** LE Audio Codec Supported Sampling Frequency: 22050 Hz. */
#define BLE_AUDIO_CODEC_SUPPORTED_SAMPLING_RATE_22050_HZ          (1ULL << 3)

/** LE Audio Codec Supported Sampling Frequency: 24000 Hz. */
#define BLE_AUDIO_CODEC_SUPPORTED_SAMPLING_RATE_24000_HZ          (1ULL << 4)

/** LE Audio Codec Supported Sampling Frequency: 32000 Hz. */
#define BLE_AUDIO_CODEC_SUPPORTED_SAMPLING_RATE_32000_HZ          (1ULL << 5)

/** LE Audio Codec Supported Sampling Frequency: 44100 Hz. */
#define BLE_AUDIO_CODEC_SUPPORTED_SAMPLING_RATE_44100_HZ          (1ULL << 6)

/** LE Audio Codec Supported Sampling Frequency: 48000 Hz. */
#define BLE_AUDIO_CODEC_SUPPORTED_SAMPLING_RATE_48000_HZ          (1ULL << 7)

/** LE Audio Codec Supported Sampling Frequency: 88200 Hz. */
#define BLE_AUDIO_CODEC_SUPPORTED_SAMPLING_RATE_88200_HZ          (1ULL << 8)

/** LE Audio Codec Supported Sampling Frequency: 96000 Hz. */
#define BLE_AUDIO_CODEC_SUPPORTED_SAMPLING_RATE_96000_HZ          (1ULL << 9)

/** LE Audio Codec Supported Sampling Frequency: 176400 Hz. */
#define BLE_AUDIO_CODEC_SUPPORTED_SAMPLING_RATE_176400_HZ         (1ULL << 10)

/** LE Audio Codec Supported Sampling Frequency: 192000 Hz. */
#define BLE_AUDIO_CODEC_SUPPORTED_SAMPLING_RATE_192000_HZ         (1ULL << 11)

/** LE Audio Codec Supported Sampling Frequency: 384000 Hz. */
#define BLE_AUDIO_CODEC_SUPPORTED_SAMPLING_RATE_384000_HZ         (1ULL << 12)

/** @} */

/**
 * @defgroup ble_audio_sup_chan_counts Bluetooth Low Energy Audio Supported Channel Counts
 * @{
 */

/** LE Audio Codec Supported Channel Count: 1. */
#define BLE_AUDIO_CODEC_SUPPORTED_CHANNEL_COUNT_1                 0x0001

/** LE Audio Codec Supported Channel Count: 2. */
#define BLE_AUDIO_CODEC_SUPPORTED_CHANNEL_COUNT_2                 0x0002

/** LE Audio Codec Supported Channel Count: 3. */
#define BLE_AUDIO_CODEC_SUPPORTED_CHANNEL_COUNT_3                 0x0004

/** LE Audio Codec Supported Channel Count: 4. */
#define BLE_AUDIO_CODEC_SUPPORTED_CHANNEL_COUNT_4                 0x0008

/** LE Audio Codec Supported Channel Count: 5. */
#define BLE_AUDIO_CODEC_SUPPORTED_CHANNEL_COUNT_5                 0x0010

/** LE Audio Codec Supported Channel Count: 6. */
#define BLE_AUDIO_CODEC_SUPPORTED_CHANNEL_COUNT_6                 0x0020

/** LE Audio Codec Supported Channel Count: 7. */
#define BLE_AUDIO_CODEC_SUPPORTED_CHANNEL_COUNT_7                 0x0040

/** LE Audio Codec Supported Channel Count: 8. */
#define BLE_AUDIO_CODEC_SUPPORTED_CHANNEL_COUNT_8                 0x0080

/** @} */

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
        2, BLE_AUDIO_CODEC_CONF_SAMPLING_FREQ_TYPE, _sampling_freq,          \
        2, BLE_AUDIO_CODEC_CONF_FRAME_DURATION_TYPE, _frame_duration,        \
        OPTIONAL_FIELD(5, BLE_AUDIO_CODEC_CONF_AUDIO_CHANNEL_ALLOCATION_TYPE, \
                       _audio_channel_alloc)                                  \
        3, BLE_AUDIO_CODEC_CONF_OCTETS_PER_CODEC_FRAME_TYPE,                  \
        (_octets_per_codec_frame), ((_octets_per_codec_frame) >> 8),          \
        OPTIONAL_FIELD(2, BLE_AUDIO_CODEC_CONF_FRAME_BLOCKS_PER_SDU_TYPE,     \
                       _codec_frame_blocks_per_sdu)                           \
    }

/**
 * @brief Helper macro used to build LTV array of Codec_Specific_Capabilities.
 *
 * @param _sampling_freq               Supported_Sampling_Frequencies -
 *                                     two octet value
 * @param _frame_duration              Supported_Frame_Durations - single
 *                                     octet value
 * @param _audio_channel_counts        Supported_Audio_Channel_Counts -
 *                                     single octet value
 * @param _min_octets_per_codec_frame  minimum value of
 *                                     Supported_Octets_Per_Codec_Frame -
 *                                     two octet value
 * @param _max_octets_per_codec_frame  maximum value of
 *                                     Supported_Octets_Per_Codec_Frame -
 *                                     two octet value
 * @param _codec_frames_per_sdu  Supported_Max_Codec_Frames_Per_SDU -
 *                                     single octet value
 *
 * @return          Pointer to a `ble_uuid16_t` structure.
 */
#define BLE_AUDIO_BUILD_CODEC_CAPS(_sampling_freq,                            \
                                   _frame_duration,                           \
                                   _audio_channel_counts,                     \
                                   _min_octets_per_codec_frame,               \
                                   _max_octets_per_codec_frame,               \
                                   _codec_frames_per_sdu)                     \
    {                                                                         \
        3, BLE_AUDIO_CODEC_CAPS_SAMPLING_FREQ_TYPE,                           \
        (_sampling_freq), ((_sampling_freq) >> 8),                            \
        2, BLE_AUDIO_CODEC_CAPS_FRAME_DURATION_TYPE, _frame_duration,         \
        OPTIONAL_FIELD(2, BLE_AUDIO_CODEC_CAPS_SUP_AUDIO_CHANNEL_COUNTS_TYPE, \
                       _audio_channel_counts)                                 \
        5, BLE_AUDIO_CODEC_CAPS_OCTETS_PER_CODEC_FRAME_TYPE,                  \
        (_min_octets_per_codec_frame), ((_min_octets_per_codec_frame) >> 8),  \
        (_max_octets_per_codec_frame), ((_max_octets_per_codec_frame) >> 8),  \
        OPTIONAL_FIELD(2, BLE_AUDIO_CODEC_CAPS_FRAMES_PER_SDU_TYPE,           \
                       _codec_frames_per_sdu)                                 \
    }

/** Codec Information */
struct ble_audio_codec_id {
    /** Coding Format */
    uint8_t format;

    /** Company ID */
    uint16_t company_id;

    /** Vendor Specific Codec ID */
    uint16_t vendor_specific;
};

/** @brief Public Broadcast Announcement features bits */
enum ble_audio_pub_broadcast_announcement_feat {
    /** Broadcast Stream Encryption */
    BLE_AUDIO_PUB_BROADCAST_ANNOUNCEMENT_FEAT_ENCRYPTION = 1 << 0,

    /** Standard Quality Public Broadcast Audio */
    BLE_AUDIO_PUB_BROADCAST_ANNOUNCEMENT_FEAT_SQ = 1 << 1,

    /** High Quality Public Broadcast Audio */
    BLE_AUDIO_PUB_BROADCAST_ANNOUNCEMENT_FEAT_HQ = 1 << 2,
};

/** @brief Public Broadcast Announcement structure */
struct ble_audio_pub_broadcast_announcement {
    /** Public Broadcast Announcement features bitfield */
    enum ble_audio_pub_broadcast_announcement_feat features;

    /** Metadata length */
    uint8_t metadata_len;

    /** Metadata */
    const uint8_t *metadata;
};

/** Broadcast Name */
struct ble_audio_broadcast_name {
    /** Broadcast Name length */
    uint8_t name_len;

    /** Broadcast Name */
    const char *name;
};

/**
 * @defgroup ble_audio_events Bluetooth Low Energy Audio Events
 * @{
 */

/** BLE Audio event: Broadcast Announcement */
#define BLE_AUDIO_EVENT_BROADCAST_ANNOUNCEMENT               0

/** BLE Audio event: Codec Registered */
#define BLE_AUDIO_EVENT_CODEC_REGISTERED                     1

/** BLE Audio event: Codec Unregistered */
#define BLE_AUDIO_EVENT_CODEC_UNREGISTERED                   2

/** BLE Audio event: BASS - Remote Scan Stopped */
#define BLE_AUDIO_EVENT_BASS_REMOTE_SCAN_STOPPED             3

/** BLE Audio event: BASS - Remote Scan Started */
#define BLE_AUDIO_EVENT_BASS_REMOTE_SCAN_STARTED             4

/** BLE Audio event: BASS - Operation status */
#define BLE_AUDIO_EVENT_BASS_OPERATION_STATUS                5

/** BLE Audio event: BASS - Set Broadcast Code */
#define BLE_AUDIO_EVENT_BASS_BROADCAST_CODE_SET              6

/** BLE Audio event: Broadcast Sink - PA Sync state */
#define BLE_AUDIO_EVENT_BROADCAST_SINK_PA_SYNC_STATE         7

/** BLE Audio event: Broadcast Sink - BIS Sync state */
#define BLE_AUDIO_EVENT_BROADCAST_SINK_BIS_SYNC_STATE        8

/** BLE Audio event: Broadcast Sink - Metadata Updated */
#define BLE_AUDIO_EVENT_BROADCAST_SINK_METADATA              9

/** @} */

/** @brief Broadcast Announcement */
struct ble_audio_event_broadcast_announcement {
    /** Extended advertising report */
    const struct ble_gap_ext_disc_desc *ext_disc;

    /** Broadcast ID */
    uint32_t broadcast_id;

    /** Additional service data included in Broadcast Audio Announcement */
    const uint8_t *svc_data;

    /** Additional service data length  */
    uint16_t svc_data_len;

    /** Optional Public Broadcast Announcement data */
    struct ble_audio_pub_broadcast_announcement *pub_announcement_data;

    /** Optional Broadcast Name */
    struct ble_audio_broadcast_name *name;
};

/** @brief Codec Registered */
struct ble_audio_event_codec_registered {
    /** Codec Record */
    const struct ble_audio_codec_record *record;
};

/** @brief Codec Unregistered */
struct ble_audio_event_codec_unregistered {
    /** Codec Record */
    const struct ble_audio_codec_record *record;
};

/** @brief BASS Source Removed */
struct ble_audio_event_bass_remote_scan {
    /** Connection Handle of Broadcast Assistant that is performing Scan procedure for us */
    uint16_t conn_handle;
};

/** BASS Operation status OP code */
enum ble_audio_event_bass_operation_statu_op {
    /** BLE Audio event: BASS - Add Source */
    BLE_AUDIO_EVENT_BASS_SOURCE_ADDED,

    /** BLE Audio event: BASS - Modify Source */
    BLE_AUDIO_EVENT_BASS_SOURCE_MODIFIED,

    /** BLE Audio event: BASS - Remove Source */
    BLE_AUDIO_EVENT_BASS_SOURCE_REMOVED,
};

/** @brief BASS operation status */
struct ble_audio_event_bass_op_status {
    /** Source ID */
    uint8_t source_id;

    /** Operation */
    enum ble_audio_event_bass_operation_statu_op op;

    /** Event status. 0 on success, BLE_HS Error code otherwise */
    uint8_t status;
};

/** @brief BASS Set Broadcast Code */
struct ble_audio_event_bass_set_broadcast_code {
    /** Source ID */
    uint8_t source_id;

    /** Source ID */
    uint8_t broadcast_code[BLE_AUDIO_BROADCAST_CODE_SIZE];
};

enum ble_audio_broadcast_sink_sync_state {
    /** Broadcast Sink Sync State: Not Synced */
    BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_NOT_SYNCED,

    /** Broadcast Sink Sync State: Sync initiated */
    BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_INITIATED,

    /** Broadcast Sink Sync State: Sync established */
    BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_ESTABLISHED,
};

/** @brief PA Synchronization state */
struct ble_audio_event_broadcast_sink_pa_sync_state {
    /** Source ID */
    uint8_t source_id;

    /** Sync state */
    enum ble_audio_broadcast_sink_sync_state state;
};

/** @brief BIS Synchronization state */
struct ble_audio_event_broadcast_sink_bis_sync_state {
    /** Source ID */
    uint8_t source_id;

    /** BIS Index */
    uint8_t bis_index;

    /** Sync state */
    enum ble_audio_broadcast_sink_sync_state state;

    /** Connection Handle. Valid if `state` is @ref BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_ESTABLISHED */
    uint16_t conn_handle;
};

/** @brief Broadcast Sink Metadata changed */
struct ble_audio_event_broadcast_sink_metadata {
    /** Source ID */
    uint8_t source_id;

    /** BIS Index Mask */
    uint8_t bis_sync;

    /** Scan Delegator Subgroup: Metadata */
    const uint8_t *metadata;

    /** Scan Delegator Subgroup: Metadata length */
    uint8_t metadata_length;
};

/**
 * Represents a BLE Audio related event. When such an event occurs, the host
 * notifies the application by passing an instance of this structure to an
 * application-specified callback.
 */
struct ble_audio_event {
    /**
     * Indicates the type of BLE Audio event that occurred. This is one of the
     * BLE_AUDIO_EVENT codes.
     */
    uint8_t type;

    /**
     * A discriminated union containing additional details concerning the event.
     * The 'type' field indicates which member of the union is valid.
     */
    union {
        /**
         * @ref BLE_AUDIO_EVENT_BROADCAST_ANNOUNCEMENT
         *
         * Represents a received Broadcast Announcement.
         */
        struct ble_audio_event_broadcast_announcement broadcast_announcement;

        /**
         * @ref BLE_AUDIO_EVENT_CODEC_REGISTERED
         *
         * Represents a codec registration.
         */
        struct ble_audio_event_codec_registered codec_registered;

        /**
         * @ref BLE_AUDIO_EVENT_CODEC_UNREGISTERED
         *
         * Represents a codec registration.
         */
        struct ble_audio_event_codec_unregistered codec_unregistered;

        /**
         * @ref BLE_AUDIO_EVENT_BASS_REMOTE_SCAN_STOPPED
         *
         * Represents a Scan procedure termination by Broadcast Assistant.
         */
        struct ble_audio_event_bass_remote_scan remote_scan_stopped;

        /**
         * @ref BLE_AUDIO_EVENT_BASS_REMOTE_SCAN_STARTED
         *
         * Represents a Scan procedure start by Broadcast Assistant.
         */
        struct ble_audio_event_bass_remote_scan remote_scan_started;

        /**
         * @ref BLE_AUDIO_EVENT_BASS_SOURCE_ADDED
         *
         * Represents a Broadcast Source being added to BASS.
         */
        struct ble_audio_event_bass_op_status bass_operation_status;

        /**
         * @ref BLE_AUDIO_EVENT_BASS_BROADCAST_CODE_SET
         *
         * Represents a Broadcast Code baing set in BASS.
         */
        struct ble_audio_event_bass_set_broadcast_code bass_set_broadcast_code;

        /**
         * @ref BLE_AUDIO_EVENT_BROADCAST_SINK_PA_SYNC_STATE
         *
         * Represents a update of Broadcast Sink PA sync state.
         */
        struct ble_audio_event_broadcast_sink_pa_sync_state broadcast_sink_pa_sync_state;

        /**
         * @ref BLE_AUDIO_EVENT_BROADCAST_SINK_BIS_SYNC_STATE
         *
         * Represents an update of Broadcast Sink BIS sync state.
         */
        struct ble_audio_event_broadcast_sink_bis_sync_state broadcast_sink_bis_sync_state;

        /**
         * @ref BLE_AUDIO_EVENT_BROADCAST_SINK_METADATA
         *
         * Represents an update in Broadcast Sink Metadata.
         */
        struct ble_audio_event_broadcast_sink_metadata broadcast_sink_metadata;
    };
};

/** Callback function type for handling BLE Audio events. */
typedef int ble_audio_event_fn(struct ble_audio_event *event, void *arg);

/**
 * Event listener structure
 *
 * This should be used as an opaque structure and not modified manually.
 */
struct ble_audio_event_listener {
    /** The function to call when a BLE Audio event occurs. */
    ble_audio_event_fn *fn;

    /** An optional argument to pass to the event handler function. */
    void *arg;

    /** Singly-linked list entry. */
    SLIST_ENTRY(ble_audio_event_listener) next;
};

/**
 * Registers listener for BLE Audio events
 *
 * On success listener structure will be initialized automatically and does not
 * need to be initialized prior to calling this function. To change callback
 * and/or argument unregister listener first and register it again.
 *
 * @param[in] listener          Listener structure
 * @param[in] fn                Callback function
 * @param[in] arg               Optional callback argument
 *
 * @return                      0 on success
 *                              BLE_HS_EINVAL if no callback is specified
 *                              BLE_HS_EALREADY if listener is already registered
 */
int ble_audio_event_listener_register(struct ble_audio_event_listener *listener,
                                      ble_audio_event_fn *fn, void *arg);

/**
 * Unregisters listener for BLE Audio events
 *
 * @param[in] listener          Listener structure
 *
 * @return                      0 on success
 *                              BLE_HS_ENOENT if listener was not registered
 */
int ble_audio_event_listener_unregister(struct ble_audio_event_listener *listener);

/**
 * BASE iterator
 *
 * The iterator structure used by @ref ble_audio_base_subgroup_iter and
 * @ref ble_audio_base_bis_iter functions to iterate the BASE Level 2 and 3 elements
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

/** Broadcast Isochronous Streams (BIS) */
struct ble_audio_bis {
    /** Pointer to next BIS in subgroup */
    STAILQ_ENTRY(ble_audio_bis) next;

    /** BIS index */
    uint8_t idx;

    /** BIS level Codec Specific Configuration length */
    uint8_t codec_spec_config_len;

    /** BIS level Codec Specific Configuration */
    uint8_t *codec_spec_config;
};

/** Broadcast Isochronous Group (BIG) Subgroup */
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

/** Broadcast Audio Source Endpoint */
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

static inline const char *
ble_audio_broadcast_sink_sync_state_str(enum ble_audio_broadcast_sink_sync_state state)
{
    switch (state) {
    case BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_NOT_SYNCED:
        return "not synced";
    case BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_INITIATED:
        return "initiated";
    case BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_ESTABLISHED:
        return "established";
    default:
        return "invalid";
    }
}

/** @} */

#endif /* H_BLE_AUDIO_ */
