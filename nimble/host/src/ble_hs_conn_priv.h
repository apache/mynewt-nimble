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

#ifndef H_BLE_HS_CONN_
#define H_BLE_HS_CONN_

#include <inttypes.h>
#include "ble_l2cap_priv.h"
#include "ble_gatt_priv.h"
#include "ble_att_priv.h"
struct hci_le_conn_complete;
struct hci_create_conn;
struct ble_l2cap_chan;

typedef uint8_t ble_hs_conn_flags_t;

#define BLE_HS_CONN_F_MASTER        0x01
#define BLE_HS_CONN_F_UPDATE        0x02

struct ble_hs_conn {
    SLIST_ENTRY(ble_hs_conn) bhc_next;
    uint16_t bhc_handle;
    uint8_t bhc_peer_addr_type;
    uint8_t bhc_our_addr_type;
    uint8_t bhc_peer_addr[6];
    uint8_t bhc_our_rpa_addr[6];
    uint8_t bhc_peer_rpa_addr[6];

    uint16_t bhc_itvl;
    uint16_t bhc_latency;
    uint16_t bhc_supervision_timeout;
    uint8_t bhc_master_clock_accuracy;

    ble_hs_conn_flags_t bhc_flags;

    struct ble_l2cap_chan_list bhc_channels;
    struct ble_l2cap_chan *bhc_rx_chan; /* Channel rxing current packet. */
    uint16_t bhc_outstanding_pkts;

    struct ble_att_svr_conn bhc_att_svr;
    struct ble_gatts_conn bhc_gatt_svr;

    struct ble_gap_sec_state bhc_sec_state;

    ble_gap_event_fn *bhc_cb;
    void *bhc_cb_arg;
};

struct ble_hs_conn_addrs {
    uint8_t our_ota_addr_type;
    uint8_t our_id_addr_type;
    uint8_t peer_ota_addr_type;
    uint8_t peer_id_addr_type;
    const uint8_t *our_ota_addr;
    const uint8_t *our_id_addr;
    const uint8_t *peer_ota_addr;
    const uint8_t *peer_id_addr;
};

int ble_hs_conn_can_alloc(void);
struct ble_hs_conn *ble_hs_conn_alloc(void);
void ble_hs_conn_free(struct ble_hs_conn *conn);
void ble_hs_conn_insert(struct ble_hs_conn *conn);
void ble_hs_conn_remove(struct ble_hs_conn *conn);
struct ble_hs_conn *ble_hs_conn_find(uint16_t conn_handle);
struct ble_hs_conn *ble_hs_conn_find_assert(uint16_t conn_handle);
struct ble_hs_conn *ble_hs_conn_find_by_addr(uint8_t addr_type, uint8_t *addr);
struct ble_hs_conn *ble_hs_conn_find_by_idx(int idx);
int ble_hs_conn_exists(uint16_t conn_handle);
struct ble_hs_conn *ble_hs_conn_first(void);
struct ble_l2cap_chan *ble_hs_conn_chan_find(struct ble_hs_conn *conn,
                                             uint16_t cid);
int ble_hs_conn_chan_insert(struct ble_hs_conn *conn,
                            struct ble_l2cap_chan *chan);
void ble_hs_conn_addrs(const struct ble_hs_conn *conn,
                       struct ble_hs_conn_addrs *addrs);

int ble_hs_conn_init(void);

#endif
