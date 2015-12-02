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

#ifndef H_BLE_HCI_BATCH_
#define H_BLE_HCI_BATCH_

#include <inttypes.h>
#include "os/queue.h"

#define BLE_HS_HCI_BATCH_TYPE_DIRECT_CONNECT     0
#define BLE_HS_HCI_BATCH_TYPE_DIRECT_ADVERTISE   1
#define BLE_HS_HCI_BATCH_TYPE_READ_HCI_BUF_SIZE  2
#define BLE_HS_HCI_BATCH_TYPE_MAX                3

struct ble_hs_hci_batch_direct_connect {
    uint8_t bwdc_peer_addr[8];
    uint8_t bwdc_peer_addr_type;
};

struct ble_hs_hci_batch_direct_advertise {
    uint8_t bwda_peer_addr[8];
    uint8_t bwda_peer_addr_type;
};

struct ble_hs_hci_batch_entry {
    int bhb_type;
    STAILQ_ENTRY(ble_hs_hci_batch_entry) bhb_next;

    union {
        struct ble_hs_hci_batch_direct_connect bhb_direct_connect;
        struct ble_hs_hci_batch_direct_advertise bhb_direct_advertise;
    };
};

struct ble_hs_hci_batch_entry *ble_hs_hci_batch_entry_alloc(void);
void ble_hs_hci_batch_enqueue(struct ble_hs_hci_batch_entry *entry);
void ble_hs_hci_batch_done(void);
void ble_hs_hci_batch_process_next(void);
int ble_hs_hci_batch_init(void);

#endif
