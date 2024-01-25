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

#ifndef H_BLE_AUDIO_SCAN_DELEGATOR_
#define H_BLE_AUDIO_SCAN_DELEGATOR_

/**
 * @file ble_audio_scan_delegator.h
 *
 * @brief Bluetooth LE Audio BAP Scan Delegator API
 *
 * @defgroup ble_audio_scan_delegator Bluetooth LE Audio BAP Scan Delegator
 * @ingroup bt_host
 * @{
 *
 */

#include <stdint.h>
#include "audio/ble_audio.h"
#include "nimble/ble.h"

#if MYNEWT_VAL(BLE_AUDIO_SCAN_DELEGATOR)
#define BLE_AUDIO_SCAN_DELEGATOR_SUBGROUP_MAX \
        MYNEWT_VAL(BLE_AUDIO_SCAN_DELEGATOR_SUBGROUP_MAX)
#else
#define BLE_AUDIO_SCAN_DELEGATOR_SUBGROUP_MAX 0
#endif /* BLE_AUDIO_SCAN_DELEGATOR */

/** No preferred BIS Synchronization. Decision is left to application. */
#define BLE_AUDIO_SCAN_DELEGATOR_BIS_SYNC_ANY       0xFFFFFFFF

/** Unknown PA Interval */
#define BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_UNKNOWN    0xFFFF

/** Scan Delegator Source descriptor */
struct ble_audio_scan_delegator_source_desc {
    /** Scan Delegator Source: BLE Address */
    ble_addr_t addr;

    /** Scan Delegator Source: Advertising SID */
    uint8_t adv_sid;

    /** Scan Delegator Source: Broadcast ID */
    uint32_t broadcast_id;
};

/** Scan Delegator Broadcast Encryption States */
enum ble_audio_scan_delegator_big_enc {
    /** Scan Delegator BIG Encryption: Not Encrypted */
    BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_NONE,

    /** Scan Delegator BIG Encryption: Broadcast Code Required */
    BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_BROADCAST_CODE_MISSING,

    /** Scan Delegator BIG Encryption: Decrypting */
    BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_DECRYPTING,

    /** Scan Delegator BIG Encryption: Bad Code */
    BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_BROADCAST_CODE_INVALID
};

/** Scan Delegator PA Sync States */
enum ble_audio_scan_delegator_pa_sync_state {
    /** Scan Delegator PA Sync State: Not synchronized to PA */
    BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_NOT_SYNCED,

    /** Scan Delegator PA Sync State: SyncInfo Request */
    BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_SYNC_INFO_REQ,

    /** Scan Delegator PA Sync State: Synchronized to PA */
    BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_SYNCED,

    /** Scan Delegator PA Sync State: Failed to synchronize to PAA */
    BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_ERROR,

    /** Scan Delegator PA Sync State: No PAST */
    BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_NO_PAST
};

/** Scan Delegator Subgroup definition */
struct ble_audio_scan_delegator_subgroup {
    /** Scan Delegator Subgroup: BIS Synchronization */
    uint32_t bis_sync;

    /** Scan Delegator Subgroup: Metadata */
    uint8_t *metadata;

    /** Scan Delegator Subgroup: Metadata length */
    uint8_t metadata_length;
};

/** Scan Delegator PA Sync option */
enum ble_audio_scan_delegator_pa_sync {
    /** Scan Delegator PA Sync: Do not synchronize to PA */
    BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_DO_NOT_SYNC,

    /** Scan Delegator PA Sync: Synchronize to PA – PAST available */
    BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_PAST_AVAILABLE,

    /** Scan Delegator PA Sync: Synchronize to PA – PAST not available */
    BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_PAST_NOT_AVAILABLE,
};

/** Scan Delegator Broadcast Source Synchronization option */
struct ble_audio_scan_delegator_sync_opt {
    /** PA Sync option */
    enum ble_audio_scan_delegator_pa_sync pa_sync;

    /** PA Sync interval */
    uint16_t pa_interval;

    /** Number of Subgroups */
    uint8_t num_subgroups;

    /** Subgroup sync option */
    struct ble_audio_scan_delegator_subgroup subgroups[
        BLE_AUDIO_SCAN_DELEGATOR_SUBGROUP_MAX];
};

enum ble_audio_scan_delegator_action_type {
    /** Scan Delegator Action Type: Add Source */
    BLE_AUDIO_SCAN_DELEGATOR_ACTION_SOURCE_ADD,

    /** Scan Delegator Action Type: Modify Source */
    BLE_AUDIO_SCAN_DELEGATOR_ACTION_SOURCE_MODIFY,

    /** Scan Delegator Action Type: Remove Source */
    BLE_AUDIO_SCAN_DELEGATOR_ACTION_SOURCE_REMOVE,
};

struct ble_audio_scan_delegator_action {
    /**
     * Indicates the type of action that is requested.
     */
    enum ble_audio_scan_delegator_action_type type;

    /**
     * A union containing additional details concerning the action.
     * The 'type' field indicates which member of the union is valid.
     */
    union {
        /**
         * Represents remote Add Source operation request.
         *
         * Valid for the following action types:
         *     o BLE_AUDIO_SCAN_DELEGATOR_ACTION_SOURCE_ADD
         *
         * @note The @ref ble_audio_scan_delegator_subgroup.metadata object is temporary, therefore must be copied to in
         *       order to cache its information.
         *
         * Return:
         *     o 0 on success;
         *     o A non-zero value to reject.
         */
        struct {
            /** Source ID */
            uint8_t source_id;

            /** Broadcast Source descriptor */
            struct ble_audio_scan_delegator_source_desc source_desc;

            /** Broadcast synchronization option */
            struct ble_audio_scan_delegator_sync_opt sync_opt;

            /**
             * Valid pointer to provide source ID to be swapped or NULL.
             *
             * If there are insufficient resources to handle the operation, the application is requested to provide
             * source ID to be removed once accepted.
             */
            uint8_t *out_source_id_to_swap;
        } source_add;

        /**
         * Represents remote Modify Source operation request.
         *
         * Valid for the following action types:
         *     o BLE_AUDIO_SCAN_DELEGATOR_ACTION_SOURCE_MODIFY
         *
         * @note The @ref ble_audio_scan_delegator_subgroup.metadata object is temporary, therefore must be copied to in
         *       order to cache its information.
         *
         * Return:
         *     o 0 on success;
         *     o A non-zero value to reject.
         */
        struct {
            /** Source ID */
            uint8_t source_id;

            /** Broadcast synchronization option */
            struct ble_audio_scan_delegator_sync_opt sync_opt;
        } source_modify;

        /**
         * Represents remote Remove Source operation request.
         *
         * Valid for the following action types:
         *     o BLE_AUDIO_SCAN_DELEGATOR_ACTION_SOURCE_REMOVE
         *
         * Return:
         *     o 0 on success;
         *     o A non-zero value to reject.
         */
        struct {
            /** Source ID */
            uint8_t source_id;
        } source_remove;

        /**
         * Represents remote Broadcast Code Set operation request.
         *
         * Valid for the following action types:
         *     o BLE_AUDIO_SCAN_DELEGATOR_ACTION_BROADCAST_CODE
         *
         * Return:
         *     o 0 on success;
         *     o A non-zero value on failure.
         */
        struct {
            /** Source ID */
            uint8_t source_id;

            /** Broadcast Code value to be stored. */
            const uint8_t value[BLE_AUDIO_BROADCAST_CODE_SIZE];
        } broadcast_code;
    };
};

/**
 * Prototype of Scan Delegator action callback.
 * This function shall return 0 if operation is accepted, and error code if rejected.
 */
typedef int ble_audio_scan_delegator_action_fn(
    struct ble_audio_scan_delegator_action *action, void *arg);

/**
 * @brief Sets the application callback function.
 *
 * This function sets the callback function that will be called on remote device request.
 *
 * @param[in] cb                Pointer to the callback function.
 * @param[in] arg               Pointer to any additional arguments that need to
 *                              be passed to the callback function.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_scan_delegator_action_fn_set(ble_audio_scan_delegator_action_fn *cb, void *arg);

/** Scan Delegator Receive State definition */
struct ble_audio_scan_delegator_receive_state {
    /** Scan Delegator Receive State: PA Sync state */
    enum ble_audio_scan_delegator_pa_sync_state pa_sync_state;

    /** Scan Delegator Receive State: BIG Encryption */
    enum ble_audio_scan_delegator_big_enc big_enc;

    /**
     * Incorrect Bad Broadcast Code.
     * Valid for @ref BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_BROADCAST_CODE_INVALID.
     */
    uint8_t bad_code[BLE_AUDIO_BROADCAST_CODE_SIZE];

    /** Scan Delegator Receive State: Number of subgroups */
    uint8_t num_subgroups;

    /** Scan Delegator Receive State: subgroup entries */
    struct ble_audio_scan_delegator_subgroup subgroups[
        MYNEWT_VAL(BLE_AUDIO_SCAN_DELEGATOR_SUBGROUP_MAX)];
};

/** Receive State Add function parameters */
struct ble_audio_scan_delegator_receive_state_add_params {
    /** Broadcast Source descriptor */
    struct ble_audio_scan_delegator_source_desc source_desc;

    /** Receive state */
    struct ble_audio_scan_delegator_receive_state state;
};

/**
 * @brief Adds the receive state.
 *
 * This function allocates receive state and returns it's source ID.
 *
 * @param[in] params                Parameters to be used.
 * @param[in,out] source_id         Unique source ID of receive state added.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_scan_delegator_receive_state_add(const struct ble_audio_scan_delegator_receive_state_add_params *params,
                                               uint8_t *source_id);

/**
 * @brief Removes the receive state.
 *
 * This function removes the specific receive state identified by source ID.
 *
 * @param[in] source_id         Source ID of receive state to be removed.
 * @param[in] params            Parameters to be used.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_scan_delegator_receive_state_remove(uint8_t source_id);

/**
 * @brief Set the receive state.
 *
 * This function updates the specific receive state identified by source ID.
 *
 * @param[in] source_id         Source ID of receive state to be updated.
 * @param[in] state             Receive state to be set.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_scan_delegator_receive_state_set(uint8_t source_id,
                                               const struct ble_audio_scan_delegator_receive_state *state);

/**
 * @brief get the receive state.
 *
 * This function returns the specific receive state identified by source ID.
 *
 * @param[in] source_id         Source ID of receive state to be updated.
 * @param[in,out] state         Pointer to receive state to be populate.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_scan_delegator_receive_state_get(uint8_t source_id,
                                               struct ble_audio_scan_delegator_receive_state *state);

/** Scan Delegator Receive State entry definition */
struct ble_audio_scan_delegator_receive_state_entry {
    /** Source ID */
    uint8_t source_id;

    /** Broadcast Source descriptor */
    struct ble_audio_scan_delegator_source_desc source_desc;

    /** Receive state */
    struct ble_audio_scan_delegator_receive_state state;
};

/**
 * Type definition Receive State iteration callback function.
 *
 * @note Return 0 to continue, or a non-zero to abort foreach loop.
 */
typedef int ble_audio_scan_delegator_receive_state_foreach_fn(
    struct ble_audio_scan_delegator_receive_state_entry *entry, void *arg);

/**
 * @brief Iterate receive states.
 *
 * @param[in] cb                Callback to be called on codec entries.
 * @param[in] arg               Optional callback argument.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
void ble_audio_scan_delegator_receive_state_foreach(ble_audio_scan_delegator_receive_state_foreach_fn *cb, void *arg);

/**
 * @brief Initialize Scan Delegator
 *
 * This function is restricted to be called by sysinit.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_scan_delegator_init(void);
#endif /* H_BLE_AUDIO_SCAN_DELEGATOR_ */
