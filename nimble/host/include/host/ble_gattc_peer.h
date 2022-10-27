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

#ifndef H_BLE_GATTC_PEER_
#define H_BLE_GATTC_PEER_

#include "modlog/modlog.h"

#if MYNEWT_VAL(BLE_GATT_CACHING)
#include "host/ble_gattc_cache.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Misc. */
void print_bytes(const uint8_t *bytes, int len);
void print_mbuf(const struct os_mbuf *om);
void print_mbuf_data(const struct os_mbuf *om);
char *addr_str(const void *addr);
void print_uuid(const ble_uuid_t *uuid);
void print_conn_desc(const struct ble_gap_conn_desc *desc);
void print_adv_fields(const struct ble_hs_adv_fields *fields);
void ext_print_adv_report(const void *param);

/** ble_gattc_p. */
struct ble_gattc_p_dsc {
    SLIST_ENTRY(ble_gattc_p_dsc) next;
    struct ble_gatt_dsc dsc;
};
SLIST_HEAD(ble_gattc_p_dsc_list, ble_gattc_p_dsc);

struct ble_gattc_p_chr {
    SLIST_ENTRY(ble_gattc_p_chr) next;
    struct ble_gatt_chr chr;

    struct ble_gattc_p_dsc_list dscs;
};
SLIST_HEAD(ble_gattc_p_chr_list, ble_gattc_p_chr);

struct ble_gattc_p_svc {
    SLIST_ENTRY(ble_gattc_p_svc) next;
    struct ble_gatt_svc svc;

    struct ble_gattc_p_chr_list chrs;
};
SLIST_HEAD(ble_gattc_p_svc_list, ble_gattc_p_svc);

struct ble_gattc_p;
typedef void ble_gattc_p_disc_fn(const struct ble_gattc_p *ble_gattc_p, int status, void *arg);
struct ble_gattc_p {
    SLIST_ENTRY(ble_gattc_p) next;

    uint16_t conn_handle;
    ble_addr_t ble_gattc_p_addr;

#if MYNEWT_VAL(BLE_GATT_CACHING)
    ble_hash_key database_hash;
#endif

    /** List of discovered GATT services. */
    struct ble_gattc_p_svc_list svcs;

    /** Keeps track of where we are in the service discovery process. */
    uint16_t disc_prev_chr_val;
    struct ble_gattc_p_svc *cur_svc;

    /** Callback that gets executed when service discovery completes. */
    ble_gattc_p_disc_fn *disc_cb;
    void *disc_cb_arg;
};

int ble_gattc_p_delete(uint16_t conn_handle);
int ble_gattc_p_init(int max_ble_gattc_ps, int max_svcs, int max_chrs, int max_dscs,
                     void *storage_cb);
int
ble_gattc_p_disc_all(uint16_t conn_handle, ble_addr_t peer_addr, ble_gattc_p_disc_fn *disc_cb,
                     void *disc_cb_arg);
int ble_gattc_p_add(uint16_t conn_handle, ble_addr_t ble_gattc_p_addr);
struct ble_gattc_p *ble_gattc_p_find(uint16_t conn_handle);
const struct ble_gattc_p_chr * ble_gattc_p_chr_find_uuid(const struct ble_gattc_p *ble_gattc_p,
                                                         const ble_uuid_t *svc_uuid,
                                                         const ble_uuid_t *chr_uuid);
int ble_gattc_p_svc_add(ble_addr_t peer_addr, const struct ble_gatt_svc *gatt_svc);
int ble_gattc_p_chr_add(ble_addr_t peer_addr, uint16_t svc_start_handle,
                        const struct ble_gatt_chr *gatt_chr);
int ble_gattc_p_dsc_add(ble_addr_t peer_addr, uint16_t chr_val_handle,
                        const struct ble_gatt_dsc *gatt_dsc);
void ble_gattc_p_free_mem(void);

#if MYNEWT_VAL(BLE_GATT_CACHING)
void ble_gattc_p_load_hash(ble_addr_t peer_addr, ble_hash_key hash_key);
int ble_gattc_p_disc_after_ind(uint16_t conn_handle, uint16_t attr_handle,
                               struct os_mbuf *om);
#endif

#ifdef __cplusplus
}
#endif

#endif
