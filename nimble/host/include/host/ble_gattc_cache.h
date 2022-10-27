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

#ifndef H_BLE_GATTC_CACHE_
#define H_BLE_GATTC_CACHE_

#include "modlog/modlog.h"
#include "sys/queue.h"
#include "host/ble_gatt.h"
#include "nimble/ble.h"
#include "host/ble_hash_function.h"
#include "host/ble_gattc_peer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ble_hs_adv_fields;
struct ble_gap_conn_desc;
struct ble_hs_cfg;
union ble_store_value;
union ble_store_key;

#define BLE_GATTC_DATABASE_HASH_UUID128             0x2b2a

void ble_gattc_cache_save(struct ble_gattc_p *peer, size_t num_attr);
int ble_gattc_cache_init(void *storage_cb);
int ble_gattc_cache_load(ble_addr_t peer_addr);
int ble_gattc_cache_check_hash(struct ble_gattc_p *peer,struct os_mbuf *om);
void ble_gattc_cacheReset(ble_addr_t *addr);
void ble_gattc_db_hash_chr_present(ble_uuid16_t uuid);

#ifdef __cplusplus
}
#endif

#endif
