/*
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

#ifndef H_BLE_HS_HCI_PRIV_
#define H_BLE_HS_HCI_PRIV_

#include "nimble/hci_common.h"
#ifdef __cplusplus
extern "C" {
#endif

struct ble_hs_conn;
struct os_mbuf;

struct ble_hs_hci_ack {
    int bha_status;         /* A BLE_HS_E<...> error; NOT a naked HCI code. */
    uint8_t *bha_params;
    int bha_params_len;
    uint16_t bha_opcode;
    uint8_t bha_hci_handle;
};

int ble_hs_hci_cmd_tx(void *cmd, void *evt_buf, uint8_t evt_buf_len,
                      uint8_t *out_evt_buf_len);
int ble_hs_hci_cmd_tx_empty_ack(void *cmd);
void ble_hs_hci_rx_ack(uint8_t *ack_ev);
void ble_hs_hci_init(void);

#if MYNEWT_VAL(BLE_HS_PHONY_HCI_ACKS)
typedef int ble_hs_hci_phony_ack_fn(uint8_t *ack, int ack_buf_len);
void ble_hs_hci_set_phony_ack_cb(ble_hs_hci_phony_ack_fn *cb);
#endif

int ble_hs_hci_util_read_adv_tx_pwr(int8_t *out_pwr);
int ble_hs_hci_util_rand(void *dst, int len);
int ble_hs_hci_util_read_rssi(uint16_t conn_handle, int8_t *out_rssi);
int ble_hs_hci_util_set_random_addr(const uint8_t *addr);
int ble_hs_hci_util_set_data_len(uint16_t conn_handle, uint16_t tx_octets,
                                 uint16_t tx_time);
int ble_hs_hci_util_data_hdr_strip(struct os_mbuf *om,
                                   struct hci_data_hdr *out_hdr);

int ble_hs_hci_evt_process(uint8_t *data);
uint16_t ble_hs_hci_util_opcode_join(uint8_t ogf, uint16_t ocf);
void ble_hs_hci_cmd_write_hdr(uint8_t ogf, uint16_t ocf, uint8_t len,
                              void *buf);
int ble_hs_hci_cmd_send(uint8_t ogf, uint16_t ocf, uint8_t len,
                        const void *cmddata);
int ble_hs_hci_cmd_send_buf(void *cmddata);
void ble_hs_hci_cmd_build_read_bd_addr(uint8_t *dst, int dst_len);
void ble_hs_hci_cmd_build_set_event_mask(uint64_t event_mask,
                                         uint8_t *dst, int dst_len);
void ble_hs_hci_cmd_build_set_event_mask2(uint64_t event_mask, uint8_t *dst,
                                          int dst_len);
void ble_hs_hci_cmd_build_disconnect(uint16_t handle, uint8_t reason,
                                     uint8_t *dst, int dst_len);
void ble_hs_hci_cmd_build_read_rssi(uint16_t handle, uint8_t *dst,
                                    int dst_len);
void ble_hs_hci_cmd_build_le_set_host_chan_class(const uint8_t *chan_map,
                                                 uint8_t *dst, int dst_len);
void ble_hs_hci_cmd_build_le_read_chan_map(uint16_t conn_handle,
                                           uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_build_le_set_scan_rsp_data(const uint8_t *data, uint8_t len,
                                              uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_build_le_set_adv_data(const uint8_t *data, uint8_t len,
                                         uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_build_le_set_adv_params(const struct hci_adv_params *adv,
                                           uint8_t *dst, int dst_len);
void ble_hs_hci_cmd_build_le_set_event_mask(uint64_t event_mask,
                                            uint8_t *dst, int dst_len);
void ble_hs_hci_cmd_build_le_read_buffer_size(uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_le_read_buffer_size(void);
void ble_hs_hci_cmd_build_le_read_loc_supp_feat(uint8_t *dst, uint8_t dst_len);
void ble_hs_hci_cmd_build_le_set_adv_enable(uint8_t enable, uint8_t *dst,
                                            int dst_len);
int ble_hs_hci_cmd_le_set_adv_enable(uint8_t enable);
int ble_hs_hci_cmd_build_le_set_scan_params(uint8_t scan_type,
                                            uint16_t scan_itvl,
                                            uint16_t scan_window,
                                            uint8_t own_addr_type,
                                            uint8_t filter_policy,
                                            uint8_t *dst, int dst_len);
void ble_hs_hci_cmd_build_le_set_scan_enable(uint8_t enable,
                                             uint8_t filter_dups,
                                             uint8_t *dst, uint8_t dst_len);
int ble_hs_hci_cmd_le_set_scan_enable(uint8_t enable, uint8_t filter_dups);
int ble_hs_hci_cmd_build_le_create_connection(
    const struct hci_create_conn *hcc, uint8_t *cmd, int cmd_len);
int ble_hs_hci_cmd_le_create_connection(const struct hci_create_conn *hcc);
void ble_hs_hci_cmd_build_le_clear_whitelist(uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_build_le_add_to_whitelist(const uint8_t *addr,
                                             uint8_t addr_type,
                                             uint8_t *dst, int dst_len);
void ble_hs_hci_cmd_build_reset(uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_reset(void);
void ble_hs_hci_cmd_build_read_adv_pwr(uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_read_adv_pwr(void);
void ble_hs_hci_cmd_build_le_create_conn_cancel(uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_le_create_conn_cancel(void);
int ble_hs_hci_cmd_build_le_conn_update(const struct hci_conn_update *hcu,
                                        uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_le_conn_update(const struct hci_conn_update *hcu);
void ble_hs_hci_cmd_build_le_lt_key_req_reply(
    const struct hci_lt_key_req_reply *hkr, uint8_t *dst, int dst_len);
void ble_hs_hci_cmd_build_le_lt_key_req_neg_reply(uint16_t conn_handle,
                                                  uint8_t *dst, int dst_len);
void ble_hs_hci_cmd_build_le_conn_param_reply(
    const struct hci_conn_param_reply *hcr, uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_le_conn_param_reply(const struct hci_conn_param_reply *hcr);
void ble_hs_hci_cmd_build_le_conn_param_neg_reply(
    const struct hci_conn_param_neg_reply *hcn, uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_le_conn_param_neg_reply(
    const struct hci_conn_param_neg_reply *hcn);
void ble_hs_hci_cmd_build_le_rand(uint8_t *dst, int dst_len);
void ble_hs_hci_cmd_build_le_start_encrypt(const struct hci_start_encrypt *cmd,
                                           uint8_t *dst, int dst_len);
int ble_hs_hci_set_buf_sz(uint16_t pktlen, uint8_t max_pkts);

uint16_t ble_hs_hci_util_handle_pb_bc_join(uint16_t handle, uint8_t pb,
                                           uint8_t bc);

int ble_hs_hci_acl_tx(struct ble_hs_conn *connection, struct os_mbuf *txom);

int ble_hs_hci_cmd_build_set_data_len(uint16_t connection_handle,
                                      uint16_t tx_octets, uint16_t tx_time,
                                      uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_build_add_to_resolv_list(
    const struct hci_add_dev_to_resolving_list *padd,
    uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_build_remove_from_resolv_list(
    uint8_t addr_type, const uint8_t *addr, uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_build_read_resolv_list_size(uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_build_clear_resolv_list(uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_build_read_peer_resolv_addr(
    uint8_t peer_identity_addr_type, const uint8_t *peer_identity_addr,
    uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_build_read_lcl_resolv_addr(
    uint8_t local_identity_addr_type, const uint8_t *local_identity_addr,
    uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_build_set_addr_res_en(
    uint8_t enable, uint8_t *dst, int dst_len);
int ble_hs_hci_cmd_build_set_resolv_priv_addr_timeout(
    uint16_t timeout, uint8_t *dst, int dst_len);

int ble_hs_hci_cmd_build_set_random_addr(const uint8_t *addr,
                                         uint8_t *dst, int dst_len);

#ifdef __cplusplus
}
#endif

#endif
