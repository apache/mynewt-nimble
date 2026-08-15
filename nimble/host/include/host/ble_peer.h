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

#ifndef H_BLE_PEER_
#define H_BLE_PEER_

#include "os/mynewt.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Descriptor discovered on a peer device.
 *
 * Represents a single GATT descriptor belonging to a characteristic.
 */
struct ble_peer_dsc {
    /** Link to the next descriptor in the list. */
    SLIST_ENTRY(ble_peer_dsc) next;

    /** GATT descriptor definition. */
    struct ble_gatt_dsc dsc;
};

/**
 * @brief Singly-linked list of peer descriptors.
 */
SLIST_HEAD(ble_peer_dsc_list, ble_peer_dsc);

/**
 * @brief Characteristic discovered on a peer device.
 *
 * Contains the characteristic definition and its descriptors.
 */
struct ble_peer_chr {
    /** Link to the next characteristic in the list. */
    SLIST_ENTRY(ble_peer_chr) next;

    /** GATT characteristic definition. */
    struct ble_gatt_chr chr;

    /** List of descriptors belonging to this characteristic. */
    struct ble_peer_dsc_list dscs;
};

/**
 * @brief Singly-linked list of peer characteristics.
 */
SLIST_HEAD(ble_peer_chr_list, ble_peer_chr);

struct ble_peer_svc {
    /** Link to the next service in the list. */
    SLIST_ENTRY(ble_peer_svc) next;

    /** GATT service definition. */
    struct ble_gatt_svc svc;

    /** List of characteristics belonging to this service. */
    struct ble_peer_chr_list chrs;
};

/**
 * @brief Singly-linked list of peer services.
 */
SLIST_HEAD(ble_peer_svc_list, ble_peer_svc);

struct ble_peer;

/**
 * @brief Service discovery completion callback.
 *
 * @param peer   Pointer to the peer structure.
 * @param status Result of the discovery procedure (0 on success).
 * @param arg    User-provided argument.
 */
typedef void ble_peer_disc_fn(const struct ble_peer *peer, int status, void *arg);

/**
 * @brief Representation of a connected BLE peer.
 *
 * Stores discovered GATT services and discovery state.
 */
struct ble_peer {
    /** Link to the next peer in the list. */
    SLIST_ENTRY(ble_peer) next;

    /** Connection handle associated with this peer. */
    uint16_t conn_handle;

    /** List of discovered GATT services. */
    struct ble_peer_svc_list svcs;

    /** Keeps track of where we are in the service discovery process. */
    uint16_t disc_prev_chr_val;

    /** Currently processed service during discovery. */
    struct ble_peer_svc *cur_svc;

    /** Callback executed when service discovery completes. */
    ble_peer_disc_fn *disc_cb;

    /** User argument passed to the discovery callback. */
    void *disc_cb_arg;
};

/**
 * @brief Find a peer by connection handle.
 *
 * @param conn_handle Connection handle.
 *
 * @return Pointer to the peer structure or NULL if not found.
 */
struct ble_peer *ble_peer_find(uint16_t conn_handle);

/**
 * @brief Start discovery of all GATT services on a peer.
 *
 * @param conn_handle Connection handle.
 * @param disc_cb     Callback executed when discovery completes.
 * @param disc_cb_arg User argument passed to the callback.
 *
 * @return 0 on success, non-zero error code otherwise.
 */
int ble_peer_disc_all(uint16_t conn_handle, ble_peer_disc_fn *disc_cb,
                      void *disc_cb_arg);

/**
 * @brief Find a descriptor by UUIDs.
 *
 * @param peer     Peer to search.
 * @param svc_uuid Service UUID.
 * @param chr_uuid Characteristic UUID.
 * @param dsc_uuid Descriptor UUID.
 *
 * @return Pointer to the descriptor or NULL if not found.
 */
const struct ble_peer_dsc *ble_peer_dsc_find_uuid(const struct ble_peer *peer,
                                                  const ble_uuid_t *svc_uuid,
                                                  const ble_uuid_t *chr_uuid,
                                                  const ble_uuid_t *dsc_uuid);

/**
 * @brief Find a characteristic by UUIDs.
 *
 * @param peer     Peer to search.
 * @param svc_uuid Service UUID.
 * @param chr_uuid Characteristic UUID.
 *
 * @return Pointer to the characteristic or NULL if not found.
 */
const struct ble_peer_chr *ble_peer_chr_find_uuid(const struct ble_peer *peer,
                                                  const ble_uuid_t *svc_uuid,
                                                  const ble_uuid_t *chr_uuid);

/**
 * @brief Find a service by UUID.
 *
 * @param peer Peer to search.
 * @param uuid Service UUID.
 *
 * @return Pointer to the service or NULL if not found.
 */
const struct ble_peer_svc *ble_peer_svc_find_uuid(const struct ble_peer *peer,
                                                  const ble_uuid_t *uuid);

/**
 * @brief Remove a peer and free its resources.
 *
 * @param conn_handle Connection handle.
 *
 * @return 0 on success, non-zero error code otherwise.
 */
int ble_peer_delete(uint16_t conn_handle);

/**
 * @brief Add a new peer for a given connection handle.
 *
 * @param conn_handle Connection handle.
 *
 * @return 0 on success, non-zero error code otherwise.
 */
int ble_peer_add(uint16_t conn_handle);

#ifdef __cplusplus
}
#endif

#endif
