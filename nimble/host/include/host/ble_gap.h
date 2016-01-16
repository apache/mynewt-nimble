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

#define BLE_GAP_ADDR_TYPE_WL                0xff

#define BLE_GAP_EVENT_CONN                  0
#define BLE_GAP_EVENT_CANCEL_FAILURE        1
#define BLE_GAP_EVENT_TERM_FAILURE          2
#define BLE_GAP_EVENT_DISC_SUCCESS          3
#define BLE_GAP_EVENT_DISC_FINISHED         4
#define BLE_GAP_EVENT_ADV_FINISHED          5
#define BLE_GAP_EVENT_ADV_FAILURE           6
#define BLE_GAP_EVENT_ADV_STOP_FAILURE      7

struct ble_gap_conn_desc {
    uint16_t conn_handle;
    uint8_t peer_addr_type;
    uint8_t peer_addr[6];
};

typedef void ble_gap_conn_fn(int event, int status,
                             struct ble_gap_conn_desc *desc, void *arg);

struct ble_gap_disc_desc {
    uint8_t event_type;
    uint8_t addr_type;
    uint8_t length_data;
    int8_t rssi;
    uint8_t addr[6];
    uint8_t *data;
    struct ble_hs_adv_fields *fields;
};

typedef void ble_gap_disc_fn(int event, int status,
                             struct ble_gap_disc_desc *desc, void *arg);

typedef void ble_gap_wl_fn(int status, void *arg);

#define BLE_GAP_CONN_MODE_NON               0
#define BLE_GAP_CONN_MODE_DIR               1
#define BLE_GAP_CONN_MODE_UND               2

#define BLE_GAP_DISC_MODE_NON               0
#define BLE_GAP_DISC_MODE_LTD               1
#define BLE_GAP_DISC_MODE_GEN               2

struct ble_gap_white_entry {
    uint8_t addr_type;
    uint8_t addr[6];
};

int ble_gap_conn_adv_start(uint8_t discoverable_mode, uint8_t connectable_mode,
                           uint8_t *peer_addr, uint8_t peer_addr_type,
                           ble_gap_conn_fn *cb, void *cb_arg);
int ble_gap_conn_adv_stop(void);
int ble_gap_conn_set_adv_fields(struct ble_hs_adv_fields *adv_fields);
int ble_gap_conn_disc(uint32_t duration_ms, uint8_t discovery_mode,
                      ble_gap_disc_fn *cb, void *cb_arg);
int ble_gap_conn_initiate(int addr_type, uint8_t *addr,
                          ble_gap_conn_fn *cb, void *cb_arg);
int ble_gap_conn_terminate(uint16_t handle);
int ble_gap_conn_cancel(void);
int ble_gap_conn_wl_set(struct ble_gap_white_entry *white_list,
                        uint8_t white_list_count, ble_gap_wl_fn *cb,
                        void *cb_arg);

#endif
