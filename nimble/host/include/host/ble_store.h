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

#define BLE_STORE_OBJ_TYPE_OUR_SEC      1
#define BLE_STORE_OBJ_TYPE_PEER_SEC     2
#define BLE_STORE_OBJ_TYPE_CCCD         3

#define BLE_STORE_ADDR_TYPE_NONE        0xff

/**
 * Used as a key for lookups of security material.  This struct corresponds to
 * the following store object types:
 *     o BLE_STORE_OBJ_TYPE_OUR_SEC
 *     o BLE_STORE_OBJ_TYPE_PEER_SEC
 */
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

/**
 * Represents stored security material.  This struct corresponds to the
 * following store object types:
 *     o BLE_STORE_OBJ_TYPE_OUR_SEC
 *     o BLE_STORE_OBJ_TYPE_PEER_SEC
 */
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

/**
 * Used as a key for lookups of stored client characteristic configuration
 * descriptors (CCCDs).  This struct corresponds to the BLE_STORE_OBJ_TYPE_CCCD
 * store object type.
 */
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

/**
 * Represents a stored client characteristic configuration descriptor (CCCD).
 * This struct corresponds to the BLE_STORE_OBJ_TYPE_CCCD store object type.
 */
struct ble_store_value_cccd {
    uint8_t peer_addr[6];
    uint8_t peer_addr_type;
    uint16_t chr_val_handle;
    uint16_t flags;
    unsigned value_changed:1;
};

/**
 * Used as a key for store lookups.  This union must be accompanied by an
 * object type code to indicate which field is valid.
 */
union ble_store_key {
    struct ble_store_key_sec sec;
    struct ble_store_key_cccd cccd;
};

/**
 * Represents stored data.  This union must be accompanied by an object type
 * code to indicate which field is valid.
 */
union ble_store_value {
    struct ble_store_value_sec sec;
    struct ble_store_value_cccd cccd;
};

/**
 * Searches the store for an object matching the specified criteria.  If a
 * match is found, it is read from the store and the dst parameter is populated
 * with the retrieved object.
 *
 * @param obj_type              The type of object to search for; one of the
 *                                  BLE_STORE_OBJ_TYPE_[...] codes.
 * @param key                   Specifies properties of the object to search
 *                                  for.  An object is retrieved if it matches
 *                                  these criteria.
 * @param dst                   On success, this is populated with the
 *                                  retrieved object.
 *
 * @return                      0 if an object was successfully retreived;
 *                              BLE_HS_ENOENT if no matching object was found;
 *                              Other nonzero on error.
 */
typedef int ble_store_read_fn(int obj_type, union ble_store_key *key,
                              union ble_store_value *dst);

/**
 * Writes the specified object to the store.  If an object with the same
 * identity is already in the store, it is replaced.  If the store lacks
 * sufficient capacity to write the object, this function may remove previously
 * stored values to make room.
 *
 * @param obj_type              The type of object being written; one of the
 *                                  BLE_STORE_OBJ_TYPE_[...] codes.
 * @param val                   The object to persist.
 *
 * @return                      0 if the object was successfully written;
 *                              Other nonzero on error.
 */
typedef int ble_store_write_fn(int obj_type, union ble_store_value *val);

/**
 * Searches the store for the first object matching the specified criteria.  If
 * a match is found, it is deleted from the store.
 *
 * @param obj_type              The type of object to delete; one of the
 *                                  BLE_STORE_OBJ_TYPE_[...] codes.
 * @param key                   Specifies properties of the object to search
 *                                  for.  An object is deleted if it matches
 *                                  these criteria.
 * @return                      0 if an object was successfully retreived;
 *                              BLE_HS_ENOENT if no matching object was found;
 *                              Other nonzero on error.
 */
typedef int ble_store_delete_fn(int obj_type, union ble_store_key *key);

int ble_store_read(int obj_type, union ble_store_key *key,
                   union ble_store_value *val);
int ble_store_write(int obj_type, union ble_store_value *val);
int ble_store_delete(int obj_type, union ble_store_key *key);

int ble_store_read_our_sec(struct ble_store_key_sec *key_sec,
                           struct ble_store_value_sec *value_sec);
int ble_store_write_our_sec(struct ble_store_value_sec *value_sec);
int ble_store_read_peer_sec(struct ble_store_key_sec *key_sec,
                            struct ble_store_value_sec *value_sec);
int ble_store_write_peer_sec(struct ble_store_value_sec *value_sec);

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
