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

/* btp_gattc.h - Bluetooth tester GATT Client service headers */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (C) 2023 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* GATT Client Service */
/* commands */
#define BTP_GATTC_READ_SUPPORTED_COMMANDS    0x01
struct btp_gattc_read_supported_commands_rp {
    uint8_t data[0];
} __packed;

#define BTP_GATTC_EXCHANGE_MTU        0x02
struct btp_gattc_exchange_mtu_cmd {
    ble_addr_t address;
} __packed;

#define BTP_GATTC_DISC_ALL_PRIM_SVCS    0x03
struct btp_gattc_disc_all_prim_svcs_cmd {
    ble_addr_t address;
} __packed;

#define BTP_GATTC_DISC_PRIM_UUID        0x04
struct btp_gattc_disc_prim_uuid_cmd {
    ble_addr_t address;
    uint8_t uuid_length;
    uint8_t uuid[0];
} __packed;

#define BTP_GATTC_FIND_INCLUDED        0x05
struct btp_gattc_find_included_cmd {
    ble_addr_t address;
    uint16_t start_handle;
    uint16_t end_handle;
} __packed;

#define BTP_GATTC_DISC_ALL_CHRC        0x06
struct btp_gattc_disc_all_chrc_cmd {
    ble_addr_t address;
    uint16_t start_handle;
    uint16_t end_handle;
} __packed;

#define BTP_GATTC_DISC_CHRC_UUID        0x07
struct btp_gattc_disc_chrc_uuid_cmd {
    ble_addr_t address;
    uint16_t start_handle;
    uint16_t end_handle;
    uint8_t uuid_length;
    uint8_t uuid[0];
} __packed;

#define BTP_GATTC_DISC_ALL_DESC        0x08
struct btp_gattc_disc_all_desc_cmd {
    ble_addr_t address;
    uint16_t start_handle;
    uint16_t end_handle;
} __packed;

#define BTP_GATTC_READ            0x09
struct btp_gattc_read_cmd {
    ble_addr_t address;
    uint16_t handle;
} __packed;

#define BTP_GATTC_READ_UUID            0x0a
struct btp_gattc_read_uuid_cmd {
    ble_addr_t address;
    uint16_t start_handle;
    uint16_t end_handle;
    uint8_t uuid_length;
    uint8_t uuid[0];
} __packed;
struct btp_gattc_read_uuid_rp {
    ble_addr_t address;
    uint8_t status;
    uint16_t data_length;
    uint8_t value_length;
    uint8_t data[0];
} __packed;

#define BTP_GATTC_READ_LONG            0x0b
struct btp_gattc_read_long_cmd {
    ble_addr_t address;
    uint16_t handle;
    uint16_t offset;
} __packed;

#define BTP_GATTC_READ_MULTIPLE        0x0c
struct btp_gattc_read_multiple_cmd {
    ble_addr_t address;
    uint8_t handles_count;
    uint16_t handles[0];
} __packed;

#define BTP_GATTC_WRITE_WITHOUT_RSP        0x0d
struct btp_gattc_write_without_rsp_cmd {
    ble_addr_t address;
    uint16_t handle;
    uint16_t data_length;
    uint8_t data[0];
} __packed;

#define BTP_GATTC_SIGNED_WRITE_WITHOUT_RSP    0x0e
struct btp_gattc_signed_write_without_rsp_cmd {
    ble_addr_t address;
    uint16_t handle;
    uint16_t data_length;
    uint8_t data[0];
} __packed;

#define BTP_GATTC_WRITE            0x0f
struct btp_gattc_write_cmd {
    ble_addr_t address;
    uint16_t handle;
    uint16_t data_length;
    uint8_t data[0];
} __packed;

#define BTP_GATTC_WRITE_LONG        0x10
struct btp_gattc_write_long_cmd {
    ble_addr_t address;
    uint16_t handle;
    uint16_t offset;
    uint16_t data_length;
    uint8_t data[0];
} __packed;

#define BTP_GATTC_RELIABLE_WRITE        0x11
struct btp_gattc_reliable_write_cmd {
    ble_addr_t address;
    uint16_t handle;
    uint16_t offset;
    uint16_t data_length;
    uint8_t data[0];
} __packed;

#define BTP_GATTC_CFG_NOTIFY        0x12
#define BTP_GATTC_CFG_INDICATE        0x13
struct btp_gattc_cfg_notify_cmd {
    ble_addr_t address;
    uint8_t enable;
    uint16_t ccc_handle;
} __packed;

/* events */
#define BTP_GATTC_EV_MTU_EXCHANGED    0x80
struct btp_gattc_exchange_mtu_ev {
    ble_addr_t address;
    uint16_t mtu;
} __packed;

#define BTP_GATTC_DISC_ALL_PRIM_RP    0x81
struct btp_gattc_disc_prim_svcs_rp {
    ble_addr_t address;
    uint8_t status;
    uint8_t services_count;
    uint8_t data[0];
} __packed;

#define BTP_GATTC_DISC_PRIM_UUID_RP    0x82

#define BTP_GATTC_FIND_INCLUDED_RP    0x83
struct btp_gattc_find_included_rp {
    ble_addr_t address;
    uint8_t status;
    uint8_t services_count;
    struct btp_gatt_included included[0];
} __packed;

#define BTP_GATTC_DISC_ALL_CHRC_RP    0x84
#define BTP_GATTC_DISC_CHRC_UUID_RP    0x85
struct btp_gattc_disc_chrc_rp {
    ble_addr_t address;
    uint8_t status;
    uint8_t characteristics_count;
    struct btp_gatt_characteristic characteristics[0];
} __packed;

#define BTP_GATTC_DISC_ALL_DESC_RP    0x86
struct btp_gattc_disc_all_desc_rp {
    ble_addr_t address;
    uint8_t status;
    uint8_t descriptors_count;
    struct btp_gatt_descriptor descriptors[0];
} __packed;

#define BTP_GATTC_READ_RP            0x87
#define BTP_GATTC_READ_UUID_RP        0x88
#define BTP_GATTC_READ_LONG_RP        0x89
#define BTP_GATTC_READ_MULTIPLE_RP    0x8a
struct btp_gattc_read_rp {
    ble_addr_t address;
    uint8_t status;
    uint16_t data_length;
    uint8_t data[0];
} __packed;

#define BTP_GATTC_WRITE_RP            0x8b
struct btp_gattc_write_rp {
    ble_addr_t address;
    uint8_t status;
} __packed;
#define BTP_GATTC_WRITE_LONG_RP        0x8c
#define BTP_GATTC_RELIABLE_WRITE_RP    0x8d
#define BTP_GATTC_CFG_NOTIFY_RP        0x8e
#define BTP_GATTC_CFG_INDICATE_RP    0x8f
struct btp_subscribe_rp {
    ble_addr_t address;
    uint8_t status;
} __packed;

#define BTP_GATTC_EV_NOTIFICATION_RXED        0x90
struct btp_gattc_notification_ev {
    ble_addr_t address;
    uint8_t type;
    uint16_t handle;
    uint16_t data_length;
    uint8_t data[0];
} __packed;
