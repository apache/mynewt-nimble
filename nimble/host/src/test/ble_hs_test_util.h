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

#ifndef H_BLE_HS_TEST_UTIL_
#define H_BLE_HS_TEST_UTIL_

#include <inttypes.h>
struct ble_hs_conn;
struct ble_l2cap_chan;

extern struct os_mbuf *ble_hs_test_util_prev_tx;
extern uint8_t *ble_hs_test_util_prev_hci_tx;

struct ble_hs_test_util_num_completed_pkts_entry {
    uint16_t handle_id; /* 0 for terminating entry in array. */
    uint16_t num_pkts;
};

void ble_hs_test_util_build_cmd_complete(uint8_t *dst, int len,
                                         uint8_t param_len, uint8_t num_pkts,
                                         uint16_t opcode);
void ble_hs_test_util_build_cmd_status(uint8_t *dst, int len,
                                       uint8_t status, uint8_t num_pkts,
                                       uint16_t opcode);
struct ble_hs_conn *ble_hs_test_util_create_conn(uint16_t handle,
                                                 uint8_t *addr);
void ble_hs_test_util_rx_ack(uint16_t opcode, uint8_t status);
void ble_hs_test_util_rx_ack_param(uint16_t opcode, uint8_t status,
                                   void *param, int param_len);
void ble_hs_test_util_rx_le_ack(uint16_t ocf, uint8_t status);
void ble_hs_test_util_rx_le_ack_param(uint16_t ocf, uint8_t status,
                                      void *param, int param_len);
int ble_hs_test_util_l2cap_rx_payload_flat(struct ble_hs_conn *conn,
                                           struct ble_l2cap_chan *chan,
                                           const void *data, int len);
void ble_hs_test_util_rx_hci_buf_size_ack(uint16_t buf_size);
void ble_hs_test_util_rx_att_err_rsp(struct ble_hs_conn *conn, uint8_t req_op,
                                     uint8_t error_code, uint16_t err_handle);
void ble_hs_test_util_rx_startup_acks(void);
void ble_hs_test_util_rx_num_completed_pkts_event(
    struct ble_hs_test_util_num_completed_pkts_entry *entries);
void ble_hs_test_util_rx_und_adv_acks(void);
void ble_hs_test_util_rx_und_adv_acks_count(int count);
void ble_hs_test_util_rx_dir_adv_acks(void);
void ble_hs_test_util_tx_all(void);
void ble_hs_test_util_init(void);

#endif
