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

#ifndef H_BLE_STORE_
#define H_BLE_STORE_

#include <inttypes.h>

/* XXX: It probably doesn't make sense to persist all security material as a
 * single record.  We may need to separate IRK from LTK and CSRK.
 *
 * Also, the master / slave distinction is not right.  It makes sense for the
 * LTK in legacy pairing, but not for other security operations.
 */
#define BLE_STORE_OBJ_TYPE_MST_SEC      1
#define BLE_STORE_OBJ_TYPE_SLV_SEC      2
#define BLE_STORE_OBJ_TYPE_CCCD         3

#define BLE_STORE_ADDR_TYPE_NONE        0xff

struct ble_store_key_sec {
    /**
     * Key by peer identity address;
     * Valid peer_addr_type values;
     *    o BLE_ADDR_TYPE_PUBLIC
     *    o BLE_ADDR_TYPE_RANDOM
     *    o BLE_STORE_ADDR_TYPE_NONE
     * peer_addr_type=BLE_STORE_ADDR_TYPE_NONE means don't key off peer.
     */
    uint8_t peer_addr[6];
    uint8_t peer_addr_type;

    /** Key by ediv; ediv_rand_present=0 means don't key off ediv. */
    uint16_t ediv;

    /** Key by rand_num; ediv_rand_present=0 means don't key off rand_num. */
    uint64_t rand_num;

    unsigned ediv_rand_present:1;

    /** Number of results to skip; 0 means retrieve the first match. */
    uint8_t idx;
};

struct ble_store_value_sec {
    uint8_t peer_addr[6];
    uint8_t peer_addr_type;

    uint16_t ediv;
    uint64_t rand_num;
    uint8_t ltk[16];
    unsigned ltk_present:1;

    uint8_t irk[16];
    unsigned irk_present:1;

    uint8_t csrk[16];
    unsigned csrk_present:1;

    unsigned authenticated:1;
    unsigned sc:1;
};

struct ble_store_key_cccd {
    /**
     * Key by peer identity address;
     * peer_addr_type=BLE_STORE_ADDR_TYPE_NONE means don't key off peer.
     */
    uint8_t peer_addr[6];
    uint8_t peer_addr_type;

    /**
     * Key by characteristic value handle;
     * chr_val_handle=0 means don't key off characteristic handle.
     */
    uint16_t chr_val_handle;

    /** Number of results to skip; 0 means retrieve the first match. */
    uint8_t idx;
};

struct ble_store_value_cccd {
    uint8_t peer_addr[6];
    uint8_t peer_addr_type;
    uint16_t chr_val_handle;
    uint16_t flags;
    unsigned value_changed:1;
};

union ble_store_key {
    struct ble_store_key_sec sec;
    struct ble_store_key_cccd cccd;
};

union ble_store_value {
    struct ble_store_value_sec sec;
    struct ble_store_value_cccd cccd;
};

typedef int ble_store_read_fn(int obj_type, union ble_store_key *key,
                              union ble_store_value *dst);

typedef int ble_store_write_fn(int obj_type, union ble_store_value *val);

typedef int ble_store_delete_fn(int obj_type, union ble_store_key *key);

int ble_store_read(int obj_type, union ble_store_key *key,
                   union ble_store_value *val);
int ble_store_write(int obj_type, union ble_store_value *val);
int ble_store_delete(int obj_type, union ble_store_key *key);

int ble_store_read_slv_sec(struct ble_store_key_sec *key_sec,
                           struct ble_store_value_sec *value_sec);
int ble_store_write_slv_sec(struct ble_store_value_sec *value_sec);
int ble_store_read_mst_sec(struct ble_store_key_sec *key_sec,
                           struct ble_store_value_sec *value_sec);
int ble_store_write_mst_sec(struct ble_store_value_sec *value_sec);

int ble_store_read_cccd(struct ble_store_key_cccd *key,
                        struct ble_store_value_cccd *out_value);
int ble_store_write_cccd(struct ble_store_value_cccd *value);
int ble_store_delete_cccd(struct ble_store_key_cccd *key);

void ble_store_key_from_value_sec(struct ble_store_key_sec *out_key,
                                  struct ble_store_value_sec *value);
void ble_store_key_from_value_cccd(struct ble_store_key_cccd *out_key,
                                   struct ble_store_value_cccd *value);

typedef int ble_store_iterator_fn(int obj_type,
                                  union ble_store_value *val,
                                  void *cookie);

void ble_store_iterate(int obj_type,
                       ble_store_iterator_fn *callback,
                       void *cookie);
#endif
