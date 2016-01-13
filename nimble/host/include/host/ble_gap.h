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
#include "host/ble_hs.h"
struct hci_le_conn_complete;

#define BLE_GAP_CONN_EVENT_TYPE_CONNECT     1
#define BLE_GAP_CONN_EVENT_TYPE_ADV_RPT     2
#define BLE_GAP_CONN_EVENT_TYPE_SCAN_DONE   3
#define BLE_GAP_CONN_EVENT_TYPE_TERMINATE   4
#define BLE_GAP_CONN_EVENT_TYPE_ADV_DONE    5

struct ble_gap_conn_connect_rpt {
    int status;
    uint16_t handle;
    uint8_t peer_addr[6];
};

struct ble_gap_conn_adv_rpt {
    uint8_t event_type;
    uint8_t addr_type;
    uint8_t length_data;
    int8_t rssi;
    uint8_t addr[6];
    uint8_t *data;

    struct ble_hs_adv_fields fields;
};

struct ble_gap_conn_terminate_rpt {
    int status;
    uint16_t handle;
    uint8_t reason;
};

struct ble_gap_conn_adv_done {
    int status;
};

struct ble_gap_conn_event {
    uint8_t type;

    union {
        struct ble_gap_conn_connect_rpt conn;
        struct ble_gap_conn_adv_rpt adv;
        struct ble_gap_conn_terminate_rpt term;
        struct ble_gap_conn_adv_done adv_done;
    };
};

typedef void ble_gap_connect_fn(struct ble_gap_conn_event *event, void *arg);

#define BLE_GAP_CONN_MODE_NULL              0
#define BLE_GAP_CONN_MODE_NON               1
#define BLE_GAP_CONN_MODE_DIR               2
#define BLE_GAP_CONN_MODE_UND               3

#define BLE_GAP_DISC_MODE_NULL              0
#define BLE_GAP_DISC_MODE_NON               1
#define BLE_GAP_DISC_MODE_LTD               2
#define BLE_GAP_DISC_MODE_GEN               3

struct ble_gap_white_entry {
    uint8_t addr_type;
    uint8_t addr[6];
};

void ble_gap_conn_set_cb(ble_gap_connect_fn *cb, void *arg);
int ble_gap_conn_advertise(uint8_t discoverable_mode, uint8_t connectable_mode,
                           uint8_t *peer_addr, uint8_t peer_addr_type);
int ble_gap_conn_set_adv_fields(struct ble_hs_adv_fields *adv_fields);
int ble_gap_conn_disc(uint32_t duration_ms, uint8_t discovery_mode);
int ble_gap_conn_auto_connect(struct ble_gap_white_entry *white_list,
                              int white_list_count);
int ble_gap_conn_direct_connect(int addr_type, uint8_t *addr);
int ble_gap_conn_terminate(uint16_t handle);
int ble_gap_conn_cancel(void);

#endif
