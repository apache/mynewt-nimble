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

#ifndef H_BLE_L2CAP_SIG_
#define H_BLE_L2CAP_SIG_

#define BLE_L2CAP_SIG_MTU           100  /* This is our own default. */

#define BLE_L2CAP_SIG_HDR_SZ                4
struct ble_l2cap_sig_hdr {
    uint8_t op;
    uint8_t identifier;
    uint16_t length;
} __attribute__((packed));

#define BLE_L2CAP_SIG_REJECT_MIN_SZ         2
struct ble_l2cap_sig_reject {
    uint16_t reason;
} __attribute__((packed));

#define BLE_L2CAP_SIG_UPDATE_REQ_SZ         8
struct ble_l2cap_sig_update_req {
    uint16_t itvl_min;
    uint16_t itvl_max;
    uint16_t slave_latency;
    uint16_t timeout_multiplier;
} __attribute__((packed));

#define BLE_L2CAP_SIG_UPDATE_RSP_SZ         2
struct ble_l2cap_sig_update_rsp {
    uint16_t result;
} __attribute__((packed));

#define BLE_L2CAP_SIG_UPDATE_RSP_RESULT_ACCEPT  0x0000
#define BLE_L2CAP_SIG_UPDATE_RSP_RESULT_REJECT  0x0001

int ble_l2cap_sig_init_cmd(uint8_t op, uint8_t id, uint8_t payload_len,
                           struct os_mbuf **out_om, void **out_payload_buf);
void ble_l2cap_sig_hdr_parse(void *payload, uint16_t len,
                             struct ble_l2cap_sig_hdr *hdr);
void ble_l2cap_sig_hdr_write(void *payload, uint16_t len,
                             struct ble_l2cap_sig_hdr *hdr);
int ble_l2cap_sig_reject_tx(struct ble_hs_conn *conn,
                            struct ble_l2cap_chan *chan,
                            uint8_t id, uint16_t reason,
                            void *data, int data_len);
void ble_l2cap_sig_update_req_parse(void *payload, int len,
                                    struct ble_l2cap_sig_update_req *req);
void ble_l2cap_sig_update_req_write(void *payload, int len,
                                    struct ble_l2cap_sig_update_req *src);
int ble_l2cap_sig_update_req_tx(struct ble_hs_conn *conn,
                                struct ble_l2cap_chan *chan, uint8_t id,
                                struct ble_l2cap_sig_update_req *req);
void ble_l2cap_sig_update_rsp_parse(void *payload, int len,
                                    struct ble_l2cap_sig_update_rsp *cmd);
void ble_l2cap_sig_update_rsp_write(void *payload, int len,
                                    struct ble_l2cap_sig_update_rsp *src);
int ble_l2cap_sig_update_rsp_tx(struct ble_hs_conn *conn,
                                struct ble_l2cap_chan *chan, uint8_t id,
                                uint16_t result);

int ble_l2cap_sig_reject_invalid_cid_tx(struct ble_hs_conn *conn,
                                        struct ble_l2cap_chan *chan,
                                        uint8_t id,
                                        uint16_t src_cid, uint16_t dst_cid);

uint32_t ble_l2cap_sig_heartbeat(void);
struct ble_l2cap_chan *ble_l2cap_sig_create_chan(void);
int ble_l2cap_sig_init(void);

#endif
