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

#ifndef H_HOST_HCI_
#define H_HOST_HCI_

#include "nimble/hci_common.h"
struct ble_hs_conn;
struct os_mbuf;

int host_hci_evt_process(uint8_t *data);
uint16_t host_hci_opcode_join(uint8_t ogf, uint16_t ocf);
void host_hci_write_hdr(uint8_t ogf, uint8_t ocf, uint8_t len, void *buf);
int host_hci_cmd_send(uint8_t ogf, uint8_t ocf, uint8_t len,
                      const void *cmddata);
int host_hci_cmd_send_buf(void *cmddata);
void host_hci_cmd_build_read_bd_addr(uint8_t *dst, int dst_len);
void host_hci_cmd_build_set_event_mask(uint64_t event_mask,
                                       uint8_t *dst, int dst_len);
void host_hci_cmd_build_set_event_mask2(uint64_t event_mask, uint8_t *dst,
                                        int dst_len);
void host_hci_cmd_build_disconnect(uint16_t handle, uint8_t reason,
                                   uint8_t *dst, int dst_len);
int host_hci_cmd_disconnect(uint16_t handle, uint8_t reason);
void host_hci_cmd_build_read_rssi(uint16_t handle, uint8_t *dst, int dst_len);
int host_hci_cmd_read_rssi(uint16_t handle);
int host_hci_cmd_build_le_set_scan_rsp_data(const uint8_t *data, uint8_t len,
                                            uint8_t *dst, int dst_len);
int host_hci_cmd_build_le_set_adv_data(const uint8_t *data, uint8_t len,
                                       uint8_t *dst, int dst_len);
int host_hci_cmd_build_le_set_adv_params(const struct hci_adv_params *adv,
                                         uint8_t *dst, int dst_len);
void host_hci_cmd_build_le_set_event_mask(uint64_t event_mask,
                                          uint8_t *dst, int dst_len);
void host_hci_cmd_build_le_read_buffer_size(uint8_t *dst, int dst_len);
int host_hci_cmd_le_read_buffer_size(void);
void host_hci_cmd_build_le_read_loc_supp_feat(uint8_t *dst, uint8_t dst_len);
void host_hci_cmd_build_le_set_adv_enable(uint8_t enable, uint8_t *dst,
                                          int dst_len);
int host_hci_cmd_le_set_adv_enable(uint8_t enable);
int host_hci_cmd_build_le_set_scan_params(uint8_t scan_type,
                                          uint16_t scan_itvl,
                                          uint16_t scan_window,
                                          uint8_t own_addr_type,
                                          uint8_t filter_policy,
                                          uint8_t *dst, int dst_len);
void host_hci_cmd_build_le_set_scan_enable(uint8_t enable,
                                           uint8_t filter_dups,
                                           uint8_t *dst, uint8_t dst_len);
int host_hci_cmd_le_set_scan_enable(uint8_t enable, uint8_t filter_dups);
int host_hci_cmd_build_le_create_connection(const struct hci_create_conn *hcc,
                                            uint8_t *cmd, int cmd_len);
int host_hci_cmd_le_create_connection(const struct hci_create_conn *hcc);
void host_hci_cmd_build_le_clear_whitelist(uint8_t *dst, int dst_len);
int host_hci_cmd_build_le_add_to_whitelist(const uint8_t *addr,
                                           uint8_t addr_type,
                                           uint8_t *dst, int dst_len);
void host_hci_cmd_build_reset(uint8_t *dst, int dst_len);
int host_hci_cmd_reset(void);
void host_hci_cmd_build_read_adv_pwr(uint8_t *dst, int dst_len);
int host_hci_cmd_read_adv_pwr(void);
void host_hci_cmd_build_le_create_conn_cancel(uint8_t *dst, int dst_len);
int host_hci_cmd_le_create_conn_cancel(void);
int host_hci_cmd_build_le_conn_update(const struct hci_conn_update *hcu,
                                      uint8_t *dst, int dst_len);
int host_hci_cmd_le_conn_update(const struct hci_conn_update *hcu);
void host_hci_cmd_build_le_lt_key_req_reply(
    const struct hci_lt_key_req_reply *hkr, uint8_t *dst, int dst_len);
void host_hci_cmd_build_le_lt_key_req_neg_reply(uint16_t conn_handle,
                                                uint8_t *dst, int dst_len);
void host_hci_cmd_build_le_conn_param_reply(
    const struct hci_conn_param_reply *hcr, uint8_t *dst, int dst_len);
int host_hci_cmd_le_conn_param_reply(const struct hci_conn_param_reply *hcr);
void host_hci_cmd_build_le_conn_param_neg_reply(
    const struct hci_conn_param_neg_reply *hcn, uint8_t *dst, int dst_len);
int host_hci_cmd_le_conn_param_neg_reply(
    const struct hci_conn_param_neg_reply *hcn);
void host_hci_cmd_build_le_rand(uint8_t *dst, int dst_len);
void host_hci_cmd_build_le_start_encrypt(const struct hci_start_encrypt *cmd,
                                         uint8_t *dst, int dst_len);
int host_hci_set_buf_size(uint16_t pktlen, uint8_t max_pkts);

uint16_t host_hci_handle_pb_bc_join(uint16_t handle, uint8_t pb, uint8_t bc);

int host_hci_data_rx(struct os_mbuf *om);
int host_hci_data_tx(struct ble_hs_conn *connection, struct os_mbuf *txom);

int host_hci_cmd_build_set_data_len(uint16_t connection_handle,
                                    uint16_t tx_octets, uint16_t tx_time,
                                    uint8_t *dst, int dst_len);
int host_hci_cmd_build_add_to_resolv_list(
    const struct hci_add_dev_to_resolving_list *padd,
    uint8_t *dst, int dst_len);
int host_hci_cmd_build_remove_from_resolv_list(
    uint8_t addr_type, const uint8_t *addr, uint8_t *dst, int dst_len);
int host_hci_cmd_build_read_resolv_list_size(uint8_t *dst, int dst_len);
int host_hci_cmd_build_clear_resolv_list(uint8_t *dst, int dst_len);
int host_hci_cmd_build_read_peer_resolv_addr(
    uint8_t peer_identity_addr_type, const uint8_t *peer_identity_addr,
    uint8_t *dst, int dst_len);
int host_hci_cmd_build_read_lcl_resolv_addr(
    uint8_t local_identity_addr_type, const uint8_t *local_identity_addr,
    uint8_t *dst, int dst_len);
int host_hci_cmd_build_set_addr_res_en(
    uint8_t enable, uint8_t *dst, int dst_len);
int host_hci_cmd_build_set_resolv_priv_addr_timeout(
    uint16_t timeout, uint8_t *dst, int dst_len);

void host_hci_timer_set(void);

int host_hci_cmd_build_set_random_addr(const uint8_t *addr,
                                       uint8_t *dst, int dst_len);

#endif /* H_HOST_HCI_ */
