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
ble_store_write(int obj_type, union ble_store_key *key,
                union ble_store_value *val)
{
    int rc;

    if (ble_hs_cfg.store_write_cb == NULL) {
        rc = BLE_HS_ENOTSUP;
    } else {
        rc = ble_hs_cfg.store_write_cb(obj_type, key, val); 
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
