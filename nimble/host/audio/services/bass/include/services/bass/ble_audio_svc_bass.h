/**
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

#ifndef H_BLE_AUDIO_SVC_BASS_
#define H_BLE_AUDIO_SVC_BASS_

#include <stdint.h>
#include "audio/ble_audio.h"
#include "syscfg/syscfg.h"

/**
 * @file ble_audio_svc_bass.h
 *
 * @brief Bluetooth LE Audio BAS Service
 *
 * This header file provides the public API for interacting with the BASS package.
 *
 * @defgroup ble_audio_svc_bass Bluetooth LE Audio BASS package
 * @ingroup bt_host
 * @{
 *
 * This package implements BASS service. Receiver states can be modified with setter functions
 * or GATT writes to Control Point characteristic. Operations on Control Point like Add Source or
 * Modify Source can be accepted or rejected by application by registering accept function callback.
 * Accessing Control Point characteristic, or Successful modification or Receiver State will lead to
 * emission of one of BLE_SVC_AUDIO_BASS events.
 *
 */

/** BLE AUDIO BASS Maximum Subgroup Number */
#define BLE_SVC_AUDIO_BASS_SUB_NUM_MAX \
    MYNEWT_VAL(BLE_SVC_AUDIO_BASS_SUB_NUM_MAX)

/** BLE AUDIO BASS characteristic UUID */
#define BLE_SVC_AUDIO_BASS_UUID16                                        0x184F

/** BLE AUDIO BASS Control Point characteristic UUID */
#define BLE_SVC_AUDIO_BASS_CHR_UUID16_BASS_CP                            0x2BC7

/** BLE AUDIO BASS Broadcast Receiver State characteristic UUID */
#define BLE_SVC_AUDIO_BASS_CHR_UUID16_BROADCAST_RECEIVE_STATE            0x2BC8

/** BLE AUDIO BASS Add Source operation OP code */
#define BLE_SVC_AUDIO_BASS_OPERATION_ADD_SOURCE                          0x01

/** BLE AUDIO BASS Modify Source operation OP code */
#define BLE_SVC_AUDIO_BASS_OPERATION_MODIFY_SOURCE                       0x02

/** BLE AUDIO BASS Remove Source operation OP code */
#define BLE_SVC_AUDIO_BASS_OPERATION_REMOVE_SOURCE                       0x03

/** BLE AUDIO BASS Error: OP Code not supported */
#define BLE_SVC_AUDIO_BASS_ERR_OPCODE_NOT_SUPPORTED                      0x80

/** BLE AUDIO BASS Error: Invalid Source ID */
#define BLE_SVC_AUDIO_BASS_ERR_INVALID_SOURCE_ID                         0x81

/** BLE AUDIO BASS Encryption States */
enum ble_svc_audio_bass_big_enc {
    /** BLE AUDIO BASS BIG Encryption: Not Encrypted */
    BLE_SVC_AUDIO_BASS_BIG_ENC_NOT_ENCRYPTED,

    /** BLE AUDIO BASS BIG Encryption: Broadcast Code Required */
    BLE_SVC_AUDIO_BASS_BIG_ENC_BROADCAST_CODE_REQ,

    /** BLE AUDIO BASS BIG Encryption: Decrypting */
    BLE_SVC_AUDIO_BASS_BIG_ENC_DECRYPTING,

    /** BLE AUDIO BASS BIG Encryption: Bad Code */
    BLE_SVC_AUDIO_BASS_BIG_ENC_BAD_CODE
};

/** BLE AUDIO BASS PA Sync parameters, valid fo Modify Source operation */
enum ble_svc_audio_bass_pa_sync {
    /** BLE AUDIO BASS PA Sync: Do not synchronize to PA */
    BLE_SVC_AUDIO_BASS_PA_SYNC_DO_NOT_SYNC,

    /** BLE AUDIO BASS PA Sync: Synchronize to PA – PAST available */
    BLE_SVC_AUDIO_BASS_PA_SYNC_SYNC_PAST_AVAILABLE,

    /** BLE AUDIO BASS PA Sync: Synchronize to PA – PAST not available */
    BLE_SVC_AUDIO_BASS_PA_SYNC_SYNC_PAST_NOT_AVAILABLE,

    /**
     * BLE AUDIO BASS PA Sync: reserved for future use.
     * This shall be always last value in this enum
     */
    BLE_SVC_AUDIO_BASS_PA_SYNC_RFU
};

/** BLE AUDIO BASS Broadcast Receiver: PA Sync States */
enum ble_svc_audio_bass_pa_sync_state {
    /** BLE AUDIO BASS PA Sync State: Not synchronized to PA */
    BLE_SVC_AUDIO_BASS_PA_SYNC_STATE_NOT_SYNCED,

    /** BLE AUDIO BASS PA Sync State: SyncInfo Request */
    BLE_SVC_AUDIO_BASS_PA_SYNC_STATE_SYNC_INFO_REQ,

    /** BLE AUDIO BASS PA Sync State: Synchronized to PA */
    BLE_SVC_AUDIO_BASS_PA_SYNC_STATE_SYNCED,

    /** BLE AUDIO BASS PA Sync State: Failed to synchronize to PAA */
    BLE_SVC_AUDIO_BASS_PA_SYNC_STATE_SYNCED_FAILED,

    /** BLE AUDIO BASS PA Sync State: No PAST */
    BLE_SVC_AUDIO_BASS_PA_SYNC_STATE_NO_PAST
};

/** BLE AUDIO BASS Broadcast Receiver State: Subgroup entry */
struct ble_svc_audio_bass_subgroup {
    /** BLE AUDIO BASS Subgroup entry: Bis Synchronization State */
    uint32_t bis_sync_state;

    /** BLE AUDIO BASS Subgroup entry: Metadata length */
    uint8_t metadata_length;

    /** BLE AUDIO BASS Subgroup entry: Metadata */
    uint8_t *metadata;
};

/** BLE AUDIO BASS Broadcast Receiver State */
struct ble_svc_audio_bass_receiver_state {
    /** BLE AUDIO BASS Broadcast Receiver State: Source ID */
    uint8_t source_id;

    /** BLE AUDIO BASS Broadcast Receiver State: Source BLE Address */
    ble_addr_t source_addr;

    /** BLE AUDIO BASS Broadcast Receiver State: Source Advertising SID */
    uint8_t source_adv_sid;

    /** BLE AUDIO BASS Broadcast Receiver State: Broadcast ID */
    uint32_t broadcast_id;

    /** BLE AUDIO BASS Broadcast Receiver State: PA Sync state */
    enum ble_svc_audio_bass_pa_sync_state pa_sync_state;

    /** BLE AUDIO BASS Broadcast Receiver State: BIG Encryption */
    enum ble_svc_audio_bass_big_enc big_encryption;

    /**
     * BLE AUDIO BASS Broadcast Receiver State: Bad Code.
     * On GATT Read access, this value is ignored if big_encryption
     * is not set to BLE_SVC_AUDIO_BASS_BIG_ENC_BAD_CODE
     */
    uint8_t bad_code[BLE_AUDIO_BROADCAST_CODE_SIZE];

    /** BLE AUDIO BASS Broadcast Receiver State: Number of subgroups */
    uint8_t num_subgroups;

    /** BLE AUDIO BASS Broadcast Receiver State: subgroup entries */
    struct ble_svc_audio_bass_subgroup
        subgroups[BLE_SVC_AUDIO_BASS_SUB_NUM_MAX];
};

/** BLE AUDIO BASS Broadcast Receiver State add parameters */
struct ble_svc_audio_bass_receiver_state_add_params {
    /** BLE AUDIO BASS Broadcast Receiver State: Source BLE Address */
    ble_addr_t source_addr;

    /** BLE AUDIO BASS Broadcast Receiver State: Source Advertising SID */
    uint8_t source_adv_sid;

    /** BLE AUDIO BASS Broadcast Receiver State: Broadcast ID */
    uint32_t broadcast_id;

    /** BLE AUDIO BASS Broadcast Receiver State: PA Sync state */
    enum ble_svc_audio_bass_pa_sync_state pa_sync_state;

    /** BLE AUDIO BASS Broadcast Receiver State: BIG Encryption */
    enum ble_svc_audio_bass_big_enc big_encryption;

    /**
     * BLE AUDIO BASS Broadcast Receiver State: Bad Code.
     * On GATT Read access, this value is ignored if big_encryption
     * is not set to BLE_SVC_AUDIO_BASS_BIG_ENC_BAD_CODE
     */
    const uint8_t *bad_code;

    /** BLE AUDIO BASS Broadcast Receiver State: Number of subgroups */
    uint8_t num_subgroups;

    /** BLE AUDIO BASS Broadcast Receiver State: subgroup entries */
    struct ble_svc_audio_bass_subgroup
        subgroups[BLE_SVC_AUDIO_BASS_SUB_NUM_MAX];
};

/** Parameters used for updating Metadata in Receiver State. */
struct ble_svc_audio_bass_metadata_params {
    /** Subgroup index */
    uint8_t subgroup_idx;

    /** Metadata length */
    uint8_t metadata_length;

    /** Metadata */
    const uint8_t *metadata;
};

/** Parameters used for updating Receiver State. */
struct ble_svc_audio_bass_update_params {
    /** PA Sync state */
    enum ble_svc_audio_bass_pa_sync_state pa_sync_state;

    /** BIG encryption state */
    enum ble_svc_audio_bass_big_enc big_encryption;

    /** Incorrect Bad Broadcast Code. Valid for BLE_SVC_AUDIO_BASS_BIG_ENC_BAD_CODE */
    const uint8_t *bad_code;

    /** BLE AUDIO BASS Broadcast Receiver State: Number of subgroups */
    uint8_t num_subgroups;

    /** BLE AUDIO BASS Broadcast Receiver State: subgroup entries */
    struct ble_svc_audio_bass_subgroup
        subgroups[BLE_SVC_AUDIO_BASS_SUB_NUM_MAX];
};

/**
 *  Structure describing operation attempted by write on
 *  BASS Control Point characteristic
 */
struct ble_svc_audio_bass_operation {
    /**
     * Indicates the type of BASS operation that occurred.  This is one of the
     * ble_svc_audio_bass_operation codes.
     */
    uint8_t op;

    /** Connection handle for which the operation was performed */
    uint16_t conn_handle;

    /**
     * A discriminated union containing additional details concerning the BASS Control Point
     * event.  The 'type' field indicates which member of the union is valid.
     */
    union {
        /**
         * Represents Add Source operation.  Valid for the following event
         * types:
         *     o BLE_SVC_AUDIO_BASS_OPERATION_ADD_SOURCE
         * Application can accept or reject Add Source operation. If no application callback is set
         * and free Receive State characteristic exists operation is automatically accepted.
         * If application callback exists and returns 0 operation is accepted.
         * Otherwise, operation is rejected.
         * If operation is accepted by application, it may select receiver state to be filled.
         * If application doesnt select characteristic, BASS Server falls back
         * to searching free one. If none is found, operation is rejected.
         * After Add Source operation is accepted, BLE_AUDIO_EVENT is emitted.
         */
        struct {
            /** Source ID */
            uint8_t source_id;

            /** Advertiser Address */
            ble_addr_t adv_addr;

            /** Advertising SID */
            uint8_t adv_sid;

            /** Broadcast ID */
            uint32_t broadcast_id : 24;

            /** PA Sync */
            enum ble_svc_audio_bass_pa_sync pa_sync;

            /** PA Interval */
            uint16_t pa_interval;

            /** Number of subgroups */
            uint8_t num_subgroups;

            /** Subgroup entries */
            struct ble_svc_audio_bass_subgroup
                subgroups[BLE_SVC_AUDIO_BASS_SUB_NUM_MAX];

            /**
             * Pointer to provide source ID to be swapped or NULL.
             *
             * Valid only if all other receive states are used.
             *
             * If there are insufficient resources to handle the operation,
             * the application is requested to provide source ID to be
             * removed once accepted.
             */
            uint8_t *out_source_id_to_swap;
        } add_source;

        /**
         * Represents Modify Source operation. Valid for the following event
         * types:
         *     o BLE_SVC_AUDIO_BASS_OPERATION_MODIFY_SOURCE
         * Application can accept or reject Add Source operation.
         *  If no application callback is set
         * or application callback returns 0 operation is automatically accepted.
         * If application callback returns non-zero value operation is rejected.
         */
        struct {
            /** Source ID */
            uint8_t source_id;

            /** PA Sync */
            enum ble_svc_audio_bass_pa_sync pa_sync;

            /** PA Interval */
            uint16_t pa_interval;

            /** Number of subgroups */
            uint16_t num_subgroups;

            /** Subgroup entries */
            struct ble_svc_audio_bass_subgroup
                subgroups[BLE_SVC_AUDIO_BASS_SUB_NUM_MAX];
        } modify_source;

        /**
         * Represents Remove Source operation. Valid for the following event
         * types:
         *     o BLE_SVC_AUDIO_BASS_OPERATION_REMOVE_SOURCE
         */
        struct {
            /** Source ID */
            uint8_t source_id;
        } remove_source;
    };
};

/**
 * Prototype of Accept Function callback for BASS Control Point operations.
 * This function shall return 0 if operation is accepted,
 * and error code if rejected.
 */
typedef int ble_svc_audio_bass_accept_fn(struct ble_svc_audio_bass_operation
                                         *operation, void *arg);

/**
 * @brief Set Accept Function callback.
 *
 * Set Accept Function callback that will be used to accept or reject
 * operations queried on BASS Control Point characteristic. If no function
 * is registered, operations are accepted by default. Only one Accept
 * Function can be registered at once.
 *
 * @param[in] fn                        ble_svc_audio_bass_accept_fn
 *                                      to be registered.
 * @param[in] arg                       Optional ble_svc_audio_bass_accept_fn
 *                                      argument.
 *
 * @return                              0 on success;
 *                                      A non-zero value on failure.
 */
int
ble_svc_audio_bass_accept_fn_set(ble_svc_audio_bass_accept_fn *fn, void *arg);

/**
 * @brief Add Broadcast Receive State.
 *
 * Add new Broadcast Receive State to BASS.
 *
 * @param[in] params                    Parameters of new
 *                                      Broadcast Receive State.
 * @param[out] source_id                Source ID assigned by BASS to new
 *                                      Broadcast Receive State
 *
 * @return                              0 on success;
 *                                      A non-zero value on failure.
 */
int
ble_svc_audio_bass_receive_state_add(const struct ble_svc_audio_bass_receiver_state_add_params *params,
                                     uint8_t *source_id);

/**
 * @brief Remove Broadcast Receive State.
 *
 * Remove Broadcast Receive State from BASS.
 *
 * @param[in] source_id                 Source ID of Broadcast Receive State
 *                                      to be removed
 *
 * @return                              0 on success;
 *                                      A non-zero value on failure.
 */
int
ble_svc_audio_bass_receive_state_remove(uint8_t source_id);

/**
 * @brief Update Broadcast Receive State metadata.
 *
 * Set Broadcast Receive State metadata to new value.
 *
 * @param[in] params                   Parameter structure with new metadata.
 * @param[in] source_id                Source ID of Broadcast Receive State
 *
 * @return                              0 on success;
 *                                      A non-zero value on failure.
 */
int
ble_svc_audio_bass_update_metadata(const struct ble_svc_audio_bass_metadata_params *params,
                                   uint8_t source_id);

/**
 * @brief Update Broadcast Receive State.
 *
 * Set Broadcast Receive State to new value.
 *
 * @param[in] params                   Parameter structure with new
 *                                     Receive State.
 * @param[in] source_id                Source ID of Broadcast Receive State
 *
 * @return                              0 on success;
 *                                      A non-zero value on failure.
 */
int
ble_svc_audio_bass_receive_state_update(const struct
                                        ble_svc_audio_bass_update_params *params,
                                        uint8_t source_id);

/**
 * @brief Find Broadcast Receive State by Source ID.
 *
 * Get Broadcast Receive State characteristic value by Source ID.
 *
 * @param[in] source_id                Source ID of Broadcast Receive State
 * @param[out] state                   Pointer to Broadcast Receive State
 *                                     characteristic value
 *
 * @return                              0 on success;
 *                                      A non-zero value on failure.
 */
int
ble_svc_audio_bass_receiver_state_get(uint8_t source_id,
                                      struct ble_svc_audio_bass_receiver_state **state);

/**
 * @brief Get the source ID for given Receive State index.
 *
 * @param[in] index                     Receive State index.
 * @param[in,out] source_id             Pointer to the variable where the
 *                                      Source ID will be stored.
 *
 * @return                              0 on success;
 *                                      A non-zero value on failure.
 */
int
ble_svc_audio_bass_source_id_get(uint8_t index, uint8_t *source_id);

/**
 * @}
 */

#endif /* H_BLE_AUDIO_SVC_BASS_ */
