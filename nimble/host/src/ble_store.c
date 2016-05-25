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

    store_key = (void *)key_sec;
    store_value = (void *)value_sec;
    rc = ble_store_read(BLE_STORE_OBJ_TYPE_SLV_SEC, store_key, store_value);
    return rc;
}

int
ble_store_write_slv_sec(struct ble_store_value_sec *value_sec)
{
    union ble_store_value *store_value;
    int rc;

    store_value = (void *)value_sec;
    rc = ble_store_write(BLE_STORE_OBJ_TYPE_SLV_SEC, store_value);
    return rc;
}

int
ble_store_read_mst_sec(struct ble_store_key_sec *key_sec,
                       struct ble_store_value_sec *value_sec)
{
    union ble_store_value *store_value;
    union ble_store_key *store_key;
    int rc;

    store_key = (void *)key_sec;
    store_value = (void *)value_sec;
    rc = ble_store_read(BLE_STORE_OBJ_TYPE_MST_SEC, store_key, store_value);
    return rc;
}

int
ble_store_write_mst_sec(struct ble_store_value_sec *value_sec)
{
    union ble_store_value *store_value;
    int rc;

    store_value = (void *)value_sec;
    rc = ble_store_write(BLE_STORE_OBJ_TYPE_MST_SEC, store_value);
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
}
