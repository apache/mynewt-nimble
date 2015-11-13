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

#ifndef H_BLE_WORK_
#define H_BLE_WORK_

#include <inttypes.h>
#include "os/queue.h"

#define BLE_HS_WORK_TYPE_DIRECT_CONNECT     0
#define BLE_HS_WORK_TYPE_DIRECT_ADVERTISE   1
#define BLE_HS_WORK_TYPE_MAX                2

struct ble_hs_work_direct_connect {
    uint8_t bwdc_peer_addr[8];
    uint8_t bwdc_peer_addr_type;
};

struct ble_hs_work_direct_advertise {
    uint8_t bwda_peer_addr[8];
    uint8_t bwda_peer_addr_type;
};

struct ble_hs_work_entry {
    int bwe_type;
    STAILQ_ENTRY(ble_hs_work_entry) bwe_next;

    union {
        struct ble_hs_work_direct_connect bwe_direct_connect;
        struct ble_hs_work_direct_advertise bwe_direct_advertise;
    };
};

struct ble_hs_work_entry *ble_hs_work_entry_alloc(void);
void ble_hs_work_enqueue(struct ble_hs_work_entry *entry);
void ble_hs_work_process_next(void);
void ble_hs_work_done(void);
int ble_hs_work_init(void);

extern uint8_t ble_hs_work_busy;

#endif
