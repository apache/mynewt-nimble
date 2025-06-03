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

/** Peer. */
struct ble_peer_dsc {
    SLIST_ENTRY(ble_peer_dsc) next;
    struct ble_gatt_dsc dsc;
};
SLIST_HEAD(ble_peer_dsc_list, ble_peer_dsc);

struct ble_peer_chr {
    SLIST_ENTRY(ble_peer_chr) next;
    struct ble_gatt_chr chr;

    struct ble_peer_dsc_list dscs;
};
SLIST_HEAD(ble_peer_chr_list, ble_peer_chr);

struct ble_peer_svc {
    SLIST_ENTRY(ble_peer_svc) next;
    struct ble_gatt_svc svc;

    struct ble_peer_chr_list chrs;
};
SLIST_HEAD(ble_peer_svc_list, ble_peer_svc);

struct ble_peer;
typedef void ble_peer_disc_fn(const struct ble_peer *peer, int status, void *arg);

struct ble_peer {
    SLIST_ENTRY(ble_peer) next;

    uint16_t conn_handle;

    /** List of discovered GATT services. */
    struct ble_peer_svc_list svcs;

    /** Keeps track of where we are in the service discovery process. */
    uint16_t disc_prev_chr_val;
    struct ble_peer_svc *cur_svc;

    /** Callback that gets executed when service discovery completes. */
    ble_peer_disc_fn *disc_cb;
    void *disc_cb_arg;
};

struct ble_peer *ble_peer_find(uint16_t conn_handle);
int ble_peer_disc_all(uint16_t conn_handle, ble_peer_disc_fn *disc_cb, void *disc_cb_arg);
const struct ble_peer_dsc *ble_peer_dsc_find_uuid(const struct ble_peer *peer,
        const ble_uuid_t *svc_uuid, const ble_uuid_t *chr_uuid, const ble_uuid_t *dsc_uuid);
const struct ble_peer_chr *ble_peer_chr_find_uuid(const struct ble_peer *peer,
        const ble_uuid_t *svc_uuid, const ble_uuid_t *chr_uuid);
const struct ble_peer_svc *ble_peer_svc_find_uuid(const struct ble_peer *peer, const ble_uuid_t *uuid);
int ble_peer_delete(uint16_t conn_handle);
int ble_peer_add(uint16_t conn_handle);

#ifdef __cplusplus
}
#endif

#endif
