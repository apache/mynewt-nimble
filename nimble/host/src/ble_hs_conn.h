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

#ifndef H_BLE_HS_CONN_
#define H_BLE_HS_CONN_

#include "os/queue.h"
#include "ble_l2cap.h"

struct ble_hs_conn *ble_host_find_connection(uint16_t con_handle);

struct ble_hs_conn {
    SLIST_ENTRY(ble_hs_conn) bhc_next;
    uint16_t bhc_handle;
    int bhc_fd; // XXX Temporary.
    uint16_t bhc_att_mtu;

    struct ble_l2cap_chan_list bhc_channels;
};

struct ble_hs_conn *ble_hs_conn_alloc(void);
void ble_hs_conn_free(struct ble_hs_conn *conn);
struct ble_hs_conn *ble_hs_conn_find(uint16_t con_handle);
struct ble_hs_conn *ble_hs_conn_first(void);
int ble_hs_conn_init(void);

#endif
