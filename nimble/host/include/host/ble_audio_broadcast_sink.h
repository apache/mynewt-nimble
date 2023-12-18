/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for add   itional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.the NOTICE filediscovery
 * completehe
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
#include "host/ble_audio_common.h"

#define BLE_AUDIO_BROADCAST_CODEC_SPEC_CONF_MAX_SZ                19

/*
 * Broadcast audio stream scanner filter policy. Values are powers of 2 so
 * filter types may be combined, creating filter mask.
 *  NO_FILTER:
 *      All found Broadcast Audio Announcements are reported.
 *  USE_UUID:
 *      Only Broadcast Audio Announcements that contain additional, specified
 *      Service UUID are reported.
 *  USE_BROADCAST_ID:
 *      Only Broadcast Audio Announcements that are identified by specified
 *      Broadcast ID are reported.
 */
#define BLE_BROADCAST_FILT_NO_FILTER                            (0)
#define BLE_BROADCAST_FILT_USE_UUID                             (1)
#define BLE_BROADCAST_FILT_USE_BROADCAST_ID                     (2)

/** Broadcast event: Broadcast Audio Announcement found */
#define BLE_BROADCAST_EVENT_BAA_FOUND                           (0)

/** Broadcast event: Broadcast Audio Announcement discovery complete */
#define BLE_BROADCAST_EVENT_DISC_COMPLETE                       (1)

/** Broadcast event: Established synchronisation with audio stream */
#define BLE_BROADCAST_EVENT_SYNC_ESTABLISHED                    (2)

/** Broadcast event: Lost synchronisation with audio stream */
#define BLE_BROADCAST_EVENT_SYNC_LOST                           (3)

/** Broadcast event: Read Broadcast Audio Source Endpoint information */
#define BLE_BROADCAST_EVENT_BASE_READ                           (4)

struct ble_broadcast_sync_info {
    /** Peer address to synchronize with */
    ble_addr_t addr;

    /** Advertiser Set ID */
    uint8_t adv_sid;
};

struct ble_broadcast_stream_desc {
    /** Broadcast ID */
    uint32_t broadcast_id;

    /** Synchronisation information */
    struct ble_broadcast_sync_info sync_info;

    /** PBAS features */
    uint8_t pbas_features;

    /** PBAS Metadata */
    const uint8_t *pbas_metadata;

    /** PBAS Metadata length*/
    uint8_t pbas_metadata_len;
};

struct bis_desc {
    uint8_t subgroup;

    uint8_t bis_idx;

    uint8_t codec_specific_conf_len;

    uint8_t codec_specific_conf[BLE_AUDIO_BROADCAST_CODEC_SPEC_CONF_MAX_SZ];
};

struct subgroup_desc {
    uint8_t num_bis;

    struct ble_audio_codec_id codec_id;

    uint8_t codec_specific_conf_len;

    uint8_t codec_specific_conf[BLE_AUDIO_BROADCAST_CODEC_SPEC_CONF_MAX_SZ];

    uint8_t metadata_len;

    uint8_t metadata[MYNEWT_VAL(BLE_AUDIO_BROADCAST_PBA_METADATA_MAX_SZ)];

    struct bis_desc bis[MYNEWT_VAL(BLE_MAX_BIS)];
};

struct ble_audio_sink_base {
    /** Presentation Delay */
    uint32_t presentation_delay;

    /** Number of subgroups in BIG */
    uint8_t num_subgroups;

    struct subgroup_desc subgroup[MYNEWT_VAL(BLE_MAX_BIS)];
};

/**
 * Represents a broadcast related event.  When such an event occurs, the
 * host notifies the application by passing an instance of this structure to an
 * application-specified callback.
 */
struct ble_broadcast_event {
    /**
     * Indicates the type of Broadcast event that occurred.  This is one of the
     * BLE_BROADCAST_EVENT codes.
     */
    uint8_t type;

    /**
     * A discriminated union containing additional details concerning the ISO
     * event. The 'type' field indicates which member of the union is valid.
     */
    union {
        /**
         * Represents a completion discovery of Broadcast Audio Announcement.
         * Valid for the following event types:
         *     o BLE_BROADCAST_EVENT_BAA_FOUND
         */
        struct {
            struct ble_broadcast_stream_desc desc;
        } announcement_found;

        /**
         * Represents a completion of Broadcast Audio Announcement discovery
         * procedure. Valid for the following event types:
         *     o BLE_BROADCAST_EVENT_DISC_COMPLETE
         */
        struct {
            /** The reason the discovery procedure stopped. */
            uint8_t reason;
        } disc_complete;

        /**
         * Represents a synchronisation established with periodic
         * advertisement associated with Broadcast Audio Announcement.
         * Valid for the following event types:
         *     o BLE_BROADCAST_EVENT_SYNC_ESTABLISHED
         */
        struct {
            struct ble_broadcast_sync_info sync_info;
        } sync_established;

        /**
         * Represents a synchronisation lost with periodic
         * advertisement associated with Broadcast Audio Announcement.
         * Valid for the following event types:
         *     o BLE_BROADCAST_EVENT_SYNC_LOST
         */
        struct {
            /** Periodic sync handle */
            uint16_t sync_handle;

            /** Reason for sync lost, can be BLE_HS_ETIMEOUT for timeout or
             * BLE_HS_EDONE for locally terminated sync
             */
            int reason;
        } sync_lost;

        /**
         * Represents BASE read from Periodic Advertisement report received
         * after establishing synchronisation with audio stream.
         * Valid for the following event types:
         *     o BLE_BROADCAST_EVENT_BASE_READ
         */
        struct {
            /** Broadcast Audio Source Endpoint */
            struct ble_audio_sink_base base;
        } base_read;
    };
};

/** Callback function type for handling BLE Broadcast events. */
typedef int ble_audio_broadcast_fn(struct ble_broadcast_event *event,
                                   void *arg);

/**
 * Performs the Broadcast Audio Announcement discovery procedure.
 *
 * @param[in] own_addr_type         The type of address the stack should use
 *                                  for itself when sending scan requests.
 *                                  Valid values are:
 *                                          - BLE_ADDR_TYPE_PUBLIC
 *                                          - BLE_ADDR_TYPE_RANDOM
 *                                          - BLE_ADDR_TYPE_RPA_PUB_DEFAULT
 *                                          - BLE_ADDR_TYPE_RPA_RND_DEFAULT
 *                                  This parameter is ignored unless active
 *                                  scanning is being used.
 * @param[in] duration              The duration of the discovery procedure.
 *                                  Units are 10 milliseconds.
 *                                  Specify 0 for no expiration.
 * @param[in] filter_policy         Set the used filter policy.
 *                                  Valid values are:
 *                                      - BLE_BROADCAST_FILT_NO_FILTER
 *                                      - BLE_BROADCAST_FILT_USE_UUID
 *                                      - BLE_BROADCAST_FILT_USE_BROADCAST_ID
 * @param[in] uuid                  The 16-bit Service UUID of the Broadcast
 *                                  Audio Announcement to discover. This
 *                                  parameter is ignored if
 *                                  BLE_BROADCAST_FILT_USE_UUID filter policy
 *                                  is not used.
 * @param[in] cb                    The callback to associate with this
 *                                  discovery procedure. Events associated with
 *                                  process of Broadcast Audio Announcement
 *                                  discovery are reported through this
 *                                  callback.
 * @param[in] cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                          0 on success; nonzero on failure.
 */
int ble_audio_broadcast_baa_disc(uint8_t own_addr_type, uint16_t duration,
                                 uint8_t filter_policy,
                                 const struct ble_gap_ext_disc_params *uncoded_params,
                                 const struct ble_gap_ext_disc_params *coded_params,
                                 const ble_uuid_t *uuid,
                                 uint32_t broadcast_id,
                                 ble_audio_broadcast_fn *cb, void *cb_arg);

/**
 * Synchronises to Periodic Advertisement associated with Broadcast stream.
 *
 * @param[in] addr                  Peer address to synchronize with
 * @param[in] adv_sid               Advertiser Set ID
 *
 * @return                          0 on success; nonzero on failure.
 */
int ble_audio_broadcast_sync(const ble_addr_t *addr, uint8_t adv_sid);

/**
 * Cancel pending synchronization procedure.
 *
 * @return                   0 on success; nonzero on failure.
 */
int ble_audio_broadcast_sync_cancel(void);

/**
 * Cancel pending synchronization procedure.
 *
 * @param sync_handle        Handle identifying synchronization to terminate.
 *
 * @return                   0 on success; nonzero on failure.
 */
int ble_audio_broadcast_sync_terminate(uint16_t sync_handle);

#endif
