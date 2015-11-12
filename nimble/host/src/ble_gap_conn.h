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

#ifndef H_BLE_GAP_CONN_
#define H_BLE_GAP_CONN_

struct ble_hs_ack;

#define BLE_GAP_CONN_STATE_NULL                     0

#define BLE_GAP_CONN_STATE_MASTER_DIRECT_UNACKED    1
#define BLE_GAP_CONN_STATE_MASTER_DIRECT_ACKED      2

#define BLE_GAP_CONN_STATE_SLAVE_DIRECT_UNACKED     1
#define BLE_GAP_CONN_STATE_SLAVE_DIRECT_ACKED       2

int ble_gap_conn_initiate_direct(int addr_type, uint8_t *addr);
int ble_gap_conn_advertise_direct(int addr_type, uint8_t *addr);
int ble_gap_conn_rx_ack_create_conn(struct ble_hs_ack *ack);
int ble_gap_conn_rx_conn_complete(struct hci_le_conn_complete *evt);
int ble_gap_conn_rx_ack_set_adv_params(struct ble_hs_ack *ack);
int ble_gap_conn_init(void);

extern int ble_gap_conn_state_master;
extern int ble_gap_conn_state_slave;

#endif
