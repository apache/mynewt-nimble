/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef H_BLE_HS_ADV_PRIV_
#define H_BLE_HS_ADV_PRIV_

#include "host/ble_hs_adv.h"

struct ble_hs_adv {
    uint8_t event_type;
    uint8_t addr_type;
    uint8_t length_data;
    int8_t rssi;
    uint8_t addr[6];
    uint8_t *data;
    struct ble_hs_adv_fields *fields;
};

#endif
