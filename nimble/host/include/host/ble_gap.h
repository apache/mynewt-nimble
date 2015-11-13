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

#ifndef H_BLE_GAP_
#define H_BLE_GAP_

#include <inttypes.h>
struct hci_le_conn_complete;

struct ble_gap_connect_desc {
    uint16_t handle;
    uint8_t status;
    uint8_t peer_addr[6];
};

typedef void ble_gap_connect_fn(struct ble_gap_connect_desc *desc, void *arg);

void ble_gap_set_connect_cb(ble_gap_connect_fn *cb, void *arg);
int ble_gap_direct_connection_establishment(uint8_t addr_type, uint8_t *addr);
int ble_gap_directed_connectable(uint8_t addr_type, uint8_t *addr);

#endif
