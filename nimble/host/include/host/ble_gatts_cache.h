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

#ifndef H_BLE_GATTS_CACHE_
#define H_BLE_GATTS_CACHE_

#pragma once

#include "modlog/modlog.h"
#include "host/ble_hash_function.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLE_GATT_DB_FORMING,
    BLE_GATT_DB_FORMED,
    BLE_GATT_DB_CHANGED
} ble_gatt_db_states;

extern ble_gatt_db_states ble_gatt_db_state;

void ble_gatts_calc_hash(ble_hash_key hash_key);

#ifdef __cplusplus
}
#endif

#endif
