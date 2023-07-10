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

/* btp.h - Bluetooth tester btp headers */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (C) 2023 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bttester.h"
#include "btp_core.h"
#include "btp_gap.h"
#include "btp_gatt.h"
#include "btp_gattc.h"
#include "btp_l2cap.h"
#include "btp_mesh.h"

#define BTP_MTU MYNEWT_VAL(BTTESTER_BTP_DATA_SIZE_MAX)
#define BTP_DATA_MAX_SIZE (BTP_MTU - sizeof(struct btp_hdr))

#define BTP_INDEX_NONE        0xff
#define BTP_INDEX             0x00

#define BTP_SERVICE_ID_CORE    0
#define BTP_SERVICE_ID_GAP    1
#define BTP_SERVICE_ID_GATT    2
#define BTP_SERVICE_ID_L2CAP    3
#define BTP_SERVICE_ID_MESH    4
#define BTP_SERVICE_ID_GATTC    6

#define BTP_SERVICE_ID_MAX    BTP_SERVICE_ID_GATTC

#define BTP_STATUS_SUCCESS    0x00
#define BTP_STATUS_FAILED    0x01
#define BTP_STATUS_UNKNOWN_CMD    0x02
#define BTP_STATUS_NOT_READY    0x03

/* TODO indicate delay response, should be removed when all commands are
 * converted to cmd+status+ev pattern
 */
#define BTP_STATUS_DELAY_REPLY	0xFF

#define SYS_LOG_DBG(fmt, ...) \
    if (MYNEWT_VAL(BTTESTER_DEBUG)) { \
        console_printf("[DBG] %s: " fmt "\n", \
                   __func__, ## __VA_ARGS__); \
    }
#define SYS_LOG_INF(fmt, ...)   console_printf("[INF] %s: " fmt "\n", \
                           __func__, ## __VA_ARGS__);
#define SYS_LOG_ERR(fmt, ...)   console_printf("[WRN] %s: " fmt "\n", \
                           __func__, ## __VA_ARGS__);

#define SYS_LOG_LEVEL SYS_LOG_LEVEL_DEBUG
#define SYS_LOG_DOMAIN "bttester"

struct btp_hdr {
    uint8_t service;
    uint8_t opcode;
    uint8_t index;
    uint16_t len;
    uint8_t data[0];
} __packed;

#define BTP_STATUS            0x00
struct btp_status {
    uint8_t code;
} __packed;
