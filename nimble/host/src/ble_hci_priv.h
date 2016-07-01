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

#ifndef H_BLE_HCI_PRIV_
#define H_BLE_HCI_PRIV_

struct ble_hci_ack {
    int bha_status;         /* A BLE_HS_E<...> error; NOT a naked HCI code. */
    uint8_t *bha_params;
    int bha_params_len;
    uint16_t bha_opcode;
    uint8_t bha_hci_handle;
};

int ble_hci_cmd_tx(void *cmd, void *evt_buf, uint8_t evt_buf_len,
                   uint8_t *out_evt_buf_len);
int ble_hci_cmd_tx_empty_ack(void *cmd);
void ble_hci_cmd_rx_ack(uint8_t *ack_ev);
void ble_hci_cmd_init(void);

#if PHONY_HCI_ACKS
typedef int ble_hci_cmd_phony_ack_fn(uint8_t *ack, int ack_buf_len);
void ble_hci_set_phony_ack_cb(ble_hci_cmd_phony_ack_fn *cb);
#endif

int ble_hci_util_read_adv_tx_pwr(int8_t *out_pwr);
int ble_hci_util_rand(void *dst, int len);
int ble_hci_util_read_rssi(uint16_t conn_handle, int8_t *out_rssi);
int ble_hci_util_set_random_addr(const uint8_t *addr);
int ble_hci_util_set_data_len(uint16_t conn_handle, uint16_t tx_octets,
                              uint16_t tx_time);

#endif
