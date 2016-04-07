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

#ifndef H_BLE_HCI_SCHED_
#define H_BLE_HCI_SCHED_

#define BLE_HCI_SCHED_HANDLE_NONE       0

struct ble_hci_ack {
    int bha_status;         /* A BLE_HS_E<...> error; NOT a naked HCI code. */
    uint8_t *bha_params;
    int bha_params_len;
    uint16_t bha_opcode;
    uint8_t bha_hci_handle;
};

typedef int ble_hci_sched_tx_fn(void *arg);
typedef void ble_hci_sched_ack_fn(struct ble_hci_ack *ack, void *arg);

int ble_hci_sched_locked_by_cur_task(void);

int ble_hci_sched_enqueue(ble_hci_sched_tx_fn *tx_cb, void *tx_cb_arg,
                          uint8_t *out_hci_handle);
int ble_hci_sched_cancel(uint8_t handle);
void ble_hci_sched_wakeup(void);
void ble_hci_sched_rx_ack(struct ble_hci_ack *ack);
void ble_hci_sched_set_ack_cb(ble_hci_sched_ack_fn *cb, void *arg);
ble_hci_sched_ack_fn *ble_hci_sched_get_ack_cb(void);
int ble_hci_sched_init(void);

#endif
