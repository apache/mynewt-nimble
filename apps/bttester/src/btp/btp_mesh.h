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

/* btp_mesh.h - Bluetooth tester MESH service headers */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (C) 2023 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* MESH Service */
/* commands */
#define BTP_MESH_READ_SUPPORTED_COMMANDS    0x01
struct btp_mesh_read_supported_commands_rp {
    uint8_t data[0];
} __packed;

#define BTP_MESH_OUT_BLINK            BIT(0)
#define BTP_MESH_OUT_BEEP            BIT(1)
#define BTP_MESH_OUT_VIBRATE        BIT(2)
#define BTP_MESH_OUT_DISPLAY_NUMBER        BIT(3)
#define BTP_MESH_OUT_DISPLAY_STRING        BIT(4)

#define BTP_MESH_IN_PUSH            BIT(0)
#define BTP_MESH_IN_TWIST            BIT(1)
#define BTP_MESH_IN_ENTER_NUMBER        BIT(2)
#define BTP_MESH_IN_ENTER_STRING        BIT(3)

#define BTP_MESH_CONFIG_PROVISIONING    0x02
struct btp_mesh_config_provisioning_cmd {
    uint8_t uuid[16];
    uint8_t static_auth[16];
    uint8_t out_size;
    uint16_t out_actions;
    uint8_t in_size;
    uint16_t in_actions;
} __packed;

#define BTP_MESH_PROVISION_NODE        0x03
struct btp_mesh_provision_node_cmd {
    uint8_t net_key[16];
    uint16_t net_key_idx;
    uint8_t flags;
    uint32_t iv_index;
    uint32_t seq_num;
    uint16_t addr;
    uint8_t dev_key[16];
} __packed;

#define BTP_MESH_INIT            0x04
#define BTP_MESH_RESET            0x05
#define BTP_MESH_INPUT_NUMBER        0x06
struct btp_mesh_input_number_cmd {
    uint32_t number;
} __packed;

#define BTP_MESH_INPUT_STRING        0x07
struct btp_mesh_input_string_cmd {
    uint8_t string_len;
    uint8_t string[0];
} __packed;

#define BTP_MESH_IVU_TEST_MODE        0x08
struct btp_mesh_ivu_test_mode_cmd {
    uint8_t enable;
} __packed;

#define BTP_MESH_IVU_TOGGLE_STATE            0x09

#define BTP_MESH_NET_SEND            0x0a
struct btp_mesh_net_send_cmd {
    uint8_t ttl;
    uint16_t src;
    uint16_t dst;
    uint8_t payload_len;
    uint8_t payload[0];
} __packed;

#define BTP_MESH_HEALTH_GENERATE_FAULTS    0x0b
struct btp_mesh_health_generate_faults_rp {
    uint8_t test_id;
    uint8_t cur_faults_count;
    uint8_t reg_faults_count;
    uint8_t current_faults[0];
    uint8_t registered_faults[0];
} __packed;

#define BTP_MESH_HEALTH_CLEAR_FAULTS    0x0c

#define BTP_MESH_LPN            0x0d
struct btp_mesh_lpn_set_cmd {
    uint8_t enable;
} __packed;

#define BTP_MESH_LPN_POLL            0x0e

#define BTP_MESH_MODEL_SEND            0x0f
struct btp_mesh_model_send_cmd {
    uint16_t src;
    uint16_t dst;
    uint8_t payload_len;
    uint8_t payload[0];
} __packed;

#define BTP_MESH_LPN_SUBSCRIBE        0x10
struct btp_mesh_lpn_subscribe_cmd {
    uint16_t address;
} __packed;

#define BTP_MESH_LPN_UNSUBSCRIBE        0x11
struct btp_mesh_lpn_unsubscribe_cmd {
    uint16_t address;
} __packed;

#define BTP_MESH_RPL_CLEAR            0x12
#define BTP_MESH_PROXY_IDENTITY        0x13

/* events */
#define BTP_MESH_EV_OUT_NUMBER_ACTION    0x80
struct btp_mesh_out_number_action_ev {
    uint16_t action;
    uint32_t number;
} __packed;

#define BTP_MESH_EV_OUT_STRING_ACTION    0x81
struct btp_mesh_out_string_action_ev {
    uint8_t string_len;
    uint8_t string[0];
} __packed;

#define BTP_MESH_EV_IN_ACTION        0x82
struct btp_mesh_in_action_ev {
    uint16_t action;
    uint8_t size;
} __packed;

#define BTP_MESH_EV_PROVISIONED        0x83

#define BTP_MESH_PROV_BEARER_PB_ADV        0x00
#define BTP_MESH_PROV_BEARER_PB_GATT    0x01
#define BTP_MESH_EV_PROV_LINK_OPEN        0x84
struct btp_mesh_prov_link_open_ev {
    uint8_t bearer;
} __packed;

#define BTP_MESH_EV_PROV_LINK_CLOSED    0x85
struct btp_mesh_prov_link_closed_ev {
    uint8_t bearer;
} __packed;

#define BTP_MESH_EV_NET_RECV        0x86
struct btp_mesh_net_recv_ev {
    uint8_t ttl;
    uint8_t ctl;
    uint16_t src;
    uint16_t dst;
    uint8_t payload_len;
    uint8_t payload[0];
} __packed;

#define BTP_MESH_EV_INVALID_BEARER        0x87
struct btp_mesh_invalid_bearer_ev {
    uint8_t opcode;
} __packed;

#define BTP_MESH_EV_INCOMP_TIMER_EXP    0x88

#define BTP_MESH_EV_LPN_ESTABLISHED        0x8b
struct btp_mesh_lpn_established_ev {
    uint16_t net_idx;
    uint16_t friend_addr;
    uint8_t queue_size;
    uint8_t recv_win;
} __packed;

#define BTP_MESH_EV_LPN_TERMINATED        0x8c
struct btp_mesh_lpn_terminated_ev {
    uint16_t net_idx;
    uint16_t friend_addr;
} __packed;

#define BTP_MESH_EV_LPN_POLLED            0x8d
struct btp_mesh_lpn_polled_ev {
    uint16_t net_idx;
    uint16_t friend_addr;
    uint8_t retry;
} __packed;
