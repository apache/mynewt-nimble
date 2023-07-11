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

/* btp_l2cap.h - Bluetooth tester L2CAP service headers */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (C) 2023 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* L2CAP Service */
/* commands */
#define BTP_L2CAP_READ_SUPPORTED_COMMANDS    0x01
struct btp_l2cap_read_supported_commands_rp {
    uint8_t data[0];
} __packed;

#define BTP_L2CAP_CONNECT_OPT_ECFC        0x01
#define BTP_L2CAP_CONNECT_OPT_HOLD_CREDIT    0x02

#define BTP_L2CAP_CONNECT            0x02
struct btp_l2cap_connect_cmd {
    ble_addr_t address;
    uint16_t psm;
    uint16_t mtu;
    uint8_t num;
    uint8_t options;
} __packed;

struct btp_l2cap_connect_rp {
    uint8_t num;
    uint8_t chan_ids[0];
} __packed;

#define BTP_L2CAP_DISCONNECT        0x03
struct btp_l2cap_disconnect_cmd {
    uint8_t chan_id;
} __packed;

#define BTP_L2CAP_SEND_DATA            0x04
struct btp_l2cap_send_data_cmd {
    uint8_t chan_id;
    uint16_t data_len;
    uint8_t data[];
} __packed;

#define BTP_L2CAP_TRANSPORT_BREDR        0x00
#define BTP_L2CAP_TRANSPORT_LE        0x01

#define BTP_L2CAP_LISTEN            0x05
struct btp_l2cap_listen_cmd {
    uint16_t psm;
    uint8_t transport;
    uint16_t mtu;
    uint16_t response;
} __packed;

#define BTP_L2CAP_ACCEPT_CONNECTION        0x06
struct l2cap_accept_connection_cmd {
    uint8_t chan_id;
    uint16_t result;
} __packed;

#define BTP_L2CAP_RECONFIGURE        0x07
struct btp_l2cap_reconfigure_cmd {
    ble_addr_t address;
    uint16_t mtu;
    uint8_t num;
    uint8_t idxs[];
} __packed;

#define BTP_L2CAP_CREDITS        0x08
struct btp_l2cap_credits_cmd {
    uint8_t chan_id;
} __packed;

/* events */
#define BTP_L2CAP_EV_CONNECTION_REQ        0x80
struct btp_l2cap_connection_req_ev {
    uint8_t chan_id;
    uint16_t psm;
    ble_addr_t address;
} __packed;

#define BTP_L2CAP_EV_CONNECTED        0x81
struct btp_l2cap_connected_ev {
    uint8_t chan_id;
    uint16_t psm;
    uint16_t peer_mtu;
    uint16_t peer_mps;
    uint16_t our_mtu;
    uint16_t our_mps;
    ble_addr_t address;
} __packed;

#define BTP_L2CAP_EV_DISCONNECTED        0x82
struct btp_l2cap_disconnected_ev {
    uint16_t result;
    uint8_t chan_id;
    uint16_t psm;
    ble_addr_t address;
} __packed;

#define BTP_L2CAP_EV_DATA_RECEIVED        0x83
struct btp_l2cap_data_received_ev {
    uint8_t chan_id;
    uint16_t data_length;
    uint8_t data[0];
} __packed;

#define BTP_L2CAP_EV_RECONFIGURED        0x84
struct btp_l2cap_reconfigured_ev {
    uint8_t chan_id;
    uint16_t peer_mtu;
    uint16_t peer_mps;
    uint16_t our_mtu;
    uint16_t our_mps;
} __packed;
