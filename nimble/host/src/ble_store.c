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

#include <string.h>

#include "host/ble_store.h"
#include "ble_hs_priv.h"

int
ble_store_read(int obj_type, union ble_store_key *key,
               union ble_store_value *val)
{
    int rc;

    if (ble_hs_cfg.store_read_cb == NULL) {
        rc = BLE_HS_ENOTSUP;
    } else {
        rc = ble_hs_cfg.store_read_cb(obj_type, key, val);
    }

    return rc;
}

int
ble_store_write(int obj_type, union ble_store_value *val)
{
    int rc;

    if (ble_hs_cfg.store_write_cb == NULL) {
        rc = BLE_HS_ENOTSUP;
    } else {
        rc = ble_hs_cfg.store_write_cb(obj_type, val);
    }

    return rc;
}

int
ble_store_delete(int obj_type, union ble_store_key *key)
{
    int rc;

    if (ble_hs_cfg.store_delete_cb == NULL) {
        rc = BLE_HS_ENOTSUP;
    } else {
        rc = ble_hs_cfg.store_delete_cb(obj_type, key);
    }

    return rc;
}

int
ble_store_read_slv_sec(struct ble_store_key_sec *key_sec,
                       struct ble_store_value_sec *value_sec)
{
    union ble_store_value *store_value;
    union ble_store_key *store_key;
    int rc;

    BLE_HS_DBG_ASSERT(key_sec->peer_addr_type == BLE_ADDR_TYPE_PUBLIC ||
                      key_sec->peer_addr_type == BLE_ADDR_TYPE_RANDOM ||
                      key_sec->peer_addr_type == BLE_STORE_ADDR_TYPE_NONE);

    store_key = (void *)key_sec;
    store_value = (void *)value_sec;
    rc = ble_store_read(BLE_STORE_OBJ_TYPE_SLV_SEC, store_key, store_value);
    return rc;
}

static int
ble_store_persist_sec(int obj_type, struct ble_store_value_sec *value_sec)
{
    struct ble_store_key_sec key_sec;
    union ble_store_value *store_value;
    union ble_store_key *store_key;
    int rc;

    BLE_HS_DBG_ASSERT(value_sec->peer_addr_type == BLE_ADDR_TYPE_PUBLIC ||
                      value_sec->peer_addr_type == BLE_ADDR_TYPE_RANDOM);

    /* If the value contains no keys, delete the corresponding entry.
     * Otherwise, write it.
     */
    if (!value_sec->ltk_present &&
        !value_sec->irk_present &&
        !value_sec->csrk_present) {

        ble_store_key_from_value_sec(&key_sec, value_sec);
        store_key = (void *)&key_sec;
        rc = ble_store_delete(obj_type, store_key);
    } else {
        store_value = (void *)value_sec;
        rc = ble_store_write(obj_type, store_value);
    }

    return rc;
}

int
ble_store_write_slv_sec(struct ble_store_value_sec *value_sec)
{
    int rc;

    rc = ble_store_persist_sec(BLE_STORE_OBJ_TYPE_SLV_SEC, value_sec);
    return rc;
}

int
ble_store_read_mst_sec(struct ble_store_key_sec *key_sec,
                       struct ble_store_value_sec *value_sec)
{
    union ble_store_value *store_value;
    union ble_store_key *store_key;
    int rc;

    BLE_HS_DBG_ASSERT(key_sec->peer_addr_type == BLE_ADDR_TYPE_PUBLIC ||
                      key_sec->peer_addr_type == BLE_ADDR_TYPE_RANDOM ||
                      key_sec->peer_addr_type == BLE_STORE_ADDR_TYPE_NONE);

    store_key = (void *)key_sec;
    store_value = (void *)value_sec;
    rc = ble_store_read(BLE_STORE_OBJ_TYPE_MST_SEC, store_key, store_value);

    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_store_write_mst_sec(struct ble_store_value_sec *value_sec)
{
    int rc;

    rc = ble_store_persist_sec(BLE_STORE_OBJ_TYPE_MST_SEC, value_sec);
    if (rc != 0) {
        return rc;
    }

    if (value_sec->peer_addr_type != BLE_STORE_ADDR_TYPE_NONE &&
        value_sec->irk_present) {

        /* Write the peer IRK to the controller keycache
         * There is not much to do here if it fails */
        rc = ble_keycache_write_irk_entry(value_sec->peer_addr,
                                          value_sec->peer_addr_type,
                                          value_sec->irk);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

int
ble_store_delete_mst_sec(struct ble_store_key_sec *key_sec)
{
    union ble_store_key *store_key;
    int rc;

    store_key = (void *)key_sec;
    rc = ble_store_delete(BLE_STORE_OBJ_TYPE_MST_SEC, store_key);

    if(key_sec->peer_addr_type == BLE_STORE_ADDR_TYPE_NONE) {
        /* don't error check this since we don't know without looking up
         * the value whether it had a valid IRK */
        ble_keycache_remove_irk_entry(key_sec->peer_addr_type,
                                    key_sec->peer_addr);
    }

    return rc;
}

int
ble_store_read_cccd(struct ble_store_key_cccd *key,
                    struct ble_store_value_cccd *out_value)
{
    union ble_store_value *store_value;
    union ble_store_key *store_key;
    int rc;

    store_key = (void *)key;
    store_value = (void *)out_value;
    rc = ble_store_read(BLE_STORE_OBJ_TYPE_CCCD, store_key, store_value);
    return rc;
}

int
ble_store_write_cccd(struct ble_store_value_cccd *value)
{
    union ble_store_value *store_value;
    int rc;

    store_value = (void *)value;
    rc = ble_store_write(BLE_STORE_OBJ_TYPE_CCCD, store_value);
    return rc;
}

int
ble_store_delete_cccd(struct ble_store_key_cccd *key)
{
    union ble_store_key *store_key;
    int rc;

    store_key = (void *)key;
    rc = ble_store_delete(BLE_STORE_OBJ_TYPE_CCCD, store_key);
    return rc;
}

void
ble_store_key_from_value_cccd(struct ble_store_key_cccd *out_key,
                              struct ble_store_value_cccd *value)
{
    out_key->peer_addr_type = value->peer_addr_type;
    memcpy(out_key->peer_addr, value->peer_addr, 6);
    out_key->chr_val_handle = value->chr_val_handle;
    out_key->idx = 0;
}

void
ble_store_key_from_value_sec(struct ble_store_key_sec *out_key,
                             struct ble_store_value_sec *value)
{
    out_key->peer_addr_type = value->peer_addr_type;
    memcpy(out_key->peer_addr, value->peer_addr, sizeof out_key->peer_addr);

    out_key->ediv = value->ediv;
    out_key->rand_num = value->rand_num;
    out_key->ediv_rand_present = 1;
    out_key->idx = 0;
}

void ble_store_iterate(int obj_type,
                       ble_store_iterator_fn *callback,
                       void *cookie)
{
    union ble_store_key key;
    union ble_store_value value;
    int idx = 0;
    uint8_t *pidx;

    /* a magic value to retrieve anything */
    memset(&key, 0, sizeof(key));
    switch(obj_type) {
        case BLE_STORE_OBJ_TYPE_MST_SEC:
        case BLE_STORE_OBJ_TYPE_SLV_SEC:
            key.sec.peer_addr_type = BLE_STORE_ADDR_TYPE_NONE;
            pidx = &key.sec.idx;
            break;
        case BLE_STORE_OBJ_TYPE_CCCD:
            key.cccd.peer_addr_type = BLE_STORE_ADDR_TYPE_NONE;
            pidx = &key.cccd.idx;
        default:
            return;
    }

    while (1) {
        int rc;
        *pidx = idx;
        rc = ble_store_read(obj_type, &key, &value);
        if (rc != 0) {
            /* read error or no more entries */
            break;
        } else if (callback) {
            callback(obj_type, &value, cookie);
        }
        idx++;
    }
}
