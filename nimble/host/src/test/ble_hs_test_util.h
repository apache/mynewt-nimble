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

#ifndef H_BLE_HS_TEST_UTIL_
#define H_BLE_HS_TEST_UTIL_

#include <inttypes.h>
#include "host/ble_gap.h"
#include "ble_hs_priv.h"
#include "ble_hs_test_util_store.h"
struct ble_hs_conn;
struct ble_l2cap_chan;
struct hci_disconn_complete;

struct os_eventq ble_hs_test_util_evq;

struct ble_hs_test_util_num_completed_pkts_entry {
    uint16_t handle_id; /* 0 for terminating entry in array. */
    uint16_t num_pkts;
};

void ble_hs_test_util_prev_tx_enqueue(struct os_mbuf *om);
struct os_mbuf *ble_hs_test_util_prev_tx_dequeue(void);
struct os_mbuf *ble_hs_test_util_prev_tx_dequeue_pullup(void);
int ble_hs_test_util_prev_tx_queue_sz(void);
void ble_hs_test_util_prev_tx_queue_clear(void);

void ble_hs_test_util_set_ack_params(uint16_t opcode, uint8_t status,
                                     void *params, uint8_t params_len);
void ble_hs_test_util_set_ack(uint16_t opcode, uint8_t status);
void *ble_hs_test_util_get_first_hci_tx(void);
void *ble_hs_test_util_get_last_hci_tx(void);
void ble_hs_test_util_enqueue_hci_tx(void *cmd);
void ble_hs_test_util_prev_hci_tx_clear(void);
void ble_hs_test_util_build_cmd_complete(uint8_t *dst, int len,
                                         uint8_t param_len, uint8_t num_pkts,
                                         uint16_t opcode);
void ble_hs_test_util_build_cmd_status(uint8_t *dst, int len,
                                       uint8_t status, uint8_t num_pkts,
                                       uint16_t opcode);
void ble_hs_test_util_create_conn(uint16_t handle, uint8_t *addr,
                                  ble_gap_event_fn *cb, void *cb_arg);
int ble_hs_test_util_conn_initiate(int addr_type, uint8_t *addr,
                                   struct ble_gap_crt_params *params,
                                   ble_gap_event_fn *cb, void *cb_arg,
                                   uint8_t ack_status);
int ble_hs_test_util_conn_cancel(uint8_t ack_status);
int ble_hs_test_util_conn_terminate(uint16_t conn_handle, uint8_t hci_status);
void ble_hs_test_util_conn_disconnect(uint16_t conn_handle);
int ble_hs_test_util_disc(uint32_t duration_ms, uint8_t discovery_mode,
                          uint8_t scan_type, uint8_t filter_policy,
                          ble_gap_disc_fn *cb, void *cb_arg, int fail_idx,
                          uint8_t fail_status);
int ble_hs_test_util_adv_start(uint8_t discoverable_mode,
                               uint8_t connectable_mode,
                               uint8_t *peer_addr, uint8_t peer_addr_type,
                               struct ble_gap_adv_params *adv_params,
                               ble_gap_event_fn *cb, void *cb_arg,
                               int fail_idx, uint8_t fail_status);
int ble_hs_test_util_adv_stop(uint8_t hci_status);
int ble_hs_test_util_wl_set(struct ble_gap_white_entry *white_list,
                            uint8_t white_list_count,
                            int fail_idx, uint8_t fail_status);
int ble_hs_test_util_conn_update(uint16_t conn_handle,
                                 struct ble_gap_upd_params *params,
                                 uint8_t hci_status);
int ble_hs_test_util_security_initiate(uint16_t conn_handle,
                                       uint8_t hci_status);
int ble_hs_test_util_l2cap_rx_first_frag(uint16_t conn_handle, uint16_t cid,
                                         struct hci_data_hdr *hci_hdr,
                                         struct os_mbuf *om);
int ble_hs_test_util_l2cap_rx(uint16_t conn_handle,
                              struct hci_data_hdr *hci_hdr,
                              struct os_mbuf *om);
int ble_hs_test_util_l2cap_rx_payload_flat(uint16_t conn_handle, uint16_t cid,
                                           const void *data, int len);
void ble_hs_test_util_rx_hci_buf_size_ack(uint16_t buf_size);
void ble_hs_test_util_rx_att_err_rsp(uint16_t conn_handle, uint8_t req_op,
                                     uint8_t error_code, uint16_t err_handle);
void ble_hs_test_util_set_startup_acks(void);
void ble_hs_test_util_rx_num_completed_pkts_event(
    struct ble_hs_test_util_num_completed_pkts_entry *entries);
void ble_hs_test_util_rx_disconn_complete_event(
    struct hci_disconn_complete *evt);
uint8_t *ble_hs_test_util_verify_tx_hci(uint8_t ogf, uint16_t ocf,
                                        uint8_t *out_param_len);
void ble_hs_test_util_tx_all(void);
void ble_hs_test_util_set_public_addr(uint8_t *addr);
void ble_hs_test_util_verify_tx_exec_write(uint8_t expected_flags);
void ble_hs_test_util_init(void);

#endif
