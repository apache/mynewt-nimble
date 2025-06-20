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

/* btp_gatts.h - Bluetooth tester GATT Server service headers */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (C) 2025 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* GATT Server Service */
#define BTP_MAX_PTS_SVCS               1
#define BTP_GATT_HL_MAX_CNT            16
struct btp_notify_hlv {
    uint16_t handle;
    uint16_t len;
};

struct btp_gatt_service {
    uint16_t start_handle;
    uint16_t end_handle;
    uint8_t uuid_length;
    uint8_t uuid[0];
} __packed;

struct btp_gatt_included {
    uint16_t included_handle;
    struct btp_gatt_service service;
} __packed;

struct btp_gatt_read_uuid_chr {
    uint16_t handle;
    uint8_t data[0];
} __packed;

struct btp_gatt_characteristic {
    uint16_t characteristic_handle;
    uint16_t value_handle;
    uint8_t properties;
    uint8_t uuid_length;
    uint8_t uuid[0];
} __packed;

struct btp_gatt_descriptor {
    uint16_t descriptor_handle;
    uint8_t uuid_length;
    uint8_t uuid[0];
} __packed;

struct btp_gatt_read_rp {
    uint8_t att_response;
    uint16_t data_length;
    uint8_t data[0];
} __packed;

/* commands */
#define BTP_GATTS_READ_SUPPORTED_COMMANDS    0x01
struct btp_gatts_read_supported_commands_rp {
    uint8_t data[0];
} __packed;

#define BTP_GATTS_INITIALIZE_DATABASE      0x02
struct btp_gatts_initialize_database_cmd {
    uint8_t id;
    uint32_t flags;
} __packed;

#define BTP_GATTS_GET_ATTRIBUTES        0x03
struct btp_gatts_get_attributes_cmd {
    uint16_t start_handle;
    uint16_t end_handle;
    uint8_t type_length;
    uint8_t type[0];
} __packed;
struct btp_gatts_get_attributes_rp {
    uint8_t attrs_count;
    uint8_t attrs[0];
} __packed;
struct btp_gatts_attr {
    uint16_t handle;
    uint8_t permission;
    uint8_t type_length;
    uint8_t type[0];
} __packed;

#define BTP_GATTS_GET_ATTRIBUTE_VALUE    0x04
struct btp_gatts_get_attribute_value_cmd {
    ble_addr_t address;
    uint16_t handle;
} __packed;
struct btp_gatts_get_attribute_value_rp {
    uint8_t att_response;
    uint16_t value_length;
    uint8_t value[0];
} __packed;

#define BTP_GATTS_SET_CHRC_VALUE        0x05
struct btp_gatts_set_chrc_value_cmd {
    ble_addr_t address;
    uint16_t count;
    struct btp_notify_hlv hl[BTP_GATT_HL_MAX_CNT];
    uint8_t value[0];
} __packed;

#define BTP_GATTS_CHANGE_DATABASE        0x06
struct btp_gatts_change_database_cmd {
    uint16_t start_handle;
    uint16_t end_handle;
    uint8_t visibility;
} __packed;

#define BTP_GATTS_START_SERVER        0x07
struct btp_gatts_start_server_rp {
    uint16_t db_attr_off;
    uint8_t db_attr_cnt;
} __packed;

/* GATTS events */
#define BTP_GATTS_EV_ATTR_VALUE_CHANGED    0x81
struct btp_gatts_attr_value_changed_ev {
    uint16_t handle;
    uint16_t data_length;
    uint8_t data[0];
} __packed;
