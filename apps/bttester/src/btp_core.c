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

/* btp_core.c - Bluetooth BTP Core service */

/*
 * Copyright (C) 2023 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "btp/btp.h"

static void
supported_commands(uint8_t *data, uint16_t len)
{
    uint8_t buf[1];
    struct core_read_supported_commands_rp *rp = (void *) buf;

    memset(buf, 0, sizeof(buf));

    tester_set_bit(buf, BTP_CORE_READ_SUPPORTED_COMMANDS);
    tester_set_bit(buf, BTP_CORE_READ_SUPPORTED_SERVICES);
    tester_set_bit(buf, BTP_CORE_REGISTER_SERVICE);
    tester_set_bit(buf, BTP_CORE_UNREGISTER_SERVICE);

    tester_send(BTP_SERVICE_ID_CORE, BTP_CORE_READ_SUPPORTED_COMMANDS,
                BTP_INDEX_NONE, (uint8_t *) rp, sizeof(buf));
}

static void
supported_services(uint8_t *data, uint16_t len)
{
    uint8_t buf[1];
    struct core_read_supported_services_rp *rp = (void *) buf;

    memset(buf, 0, sizeof(buf));

    tester_set_bit(buf, BTP_SERVICE_ID_CORE);
    tester_set_bit(buf, BTP_SERVICE_ID_GAP);
    tester_set_bit(buf, BTP_SERVICE_ID_GATT);
#if MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM)
    tester_set_bit(buf, BTP_SERVICE_ID_L2CAP);
#endif /* MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM) */
#if MYNEWT_VAL(BLE_MESH)
    tester_set_bit(buf, BTP_SERVICE_ID_MESH);
#endif /* MYNEWT_VAL(BLE_MESH) */
    tester_set_bit(buf, BTP_SERVICE_ID_GATTC);

    tester_send(BTP_SERVICE_ID_CORE, BTP_CORE_READ_SUPPORTED_SERVICES,
                BTP_INDEX_NONE, (uint8_t *) rp, sizeof(buf));
}

static void
register_service(uint8_t *data, uint16_t len)
{
    struct core_register_service_cmd *cmd = (void *) data;
    uint8_t status;

    switch (cmd->id) {
    case BTP_SERVICE_ID_GAP:
        status = tester_init_gap();
        /* Rsp with success status will be handled by bt enable cb */
        if (status == BTP_STATUS_FAILED) {
            goto rsp;
        }
        return;
    case BTP_SERVICE_ID_GATT:
        status = tester_init_gatt();
        break;
#if MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM)
        case BTP_SERVICE_ID_L2CAP:
        status = tester_init_l2cap();
        break;
#endif /* MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM) */
#if MYNEWT_VAL(BLE_MESH)
        case BTP_SERVICE_ID_MESH:
        status = tester_init_mesh();
        break;
#endif /* MYNEWT_VAL(BLE_MESH) */
    default:
        status = BTP_STATUS_FAILED;
        break;
    }

rsp:
    tester_rsp(BTP_SERVICE_ID_CORE, BTP_CORE_REGISTER_SERVICE, BTP_INDEX_NONE,
               status);
}

static void
unregister_service(uint8_t *data, uint16_t len)
{
    struct core_unregister_service_cmd *cmd = (void *) data;
    uint8_t status;

    switch (cmd->id) {
    case BTP_SERVICE_ID_GAP:
        status = tester_unregister_gap();
        break;
    case BTP_SERVICE_ID_GATT:
        status = tester_unregister_gatt();
        break;
#if MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM)
        case BTP_SERVICE_ID_L2CAP:
        status = tester_unregister_l2cap();
        break;
#endif /* MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM) */
#if MYNEWT_VAL(BLE_MESH)
        case BTP_SERVICE_ID_MESH:
        status = tester_unregister_mesh();
        break;
#endif /* MYNEWT_VAL(BLE_MESH) */
    default:
        status = BTP_STATUS_FAILED;
        break;
    }

    tester_rsp(BTP_SERVICE_ID_CORE, BTP_CORE_UNREGISTER_SERVICE, BTP_INDEX_NONE,
               status);
}

void
tester_handle_core(uint8_t opcode, uint8_t index, uint8_t *data,
                   uint16_t len)
{
    if (index != BTP_INDEX_NONE) {
        tester_rsp(BTP_SERVICE_ID_CORE, opcode, index,
                   BTP_STATUS_FAILED);
        return;
    }

    switch (opcode) {
    case BTP_CORE_READ_SUPPORTED_COMMANDS:
        supported_commands(data, len);
        return;
    case BTP_CORE_READ_SUPPORTED_SERVICES:
        supported_services(data, len);
        return;
    case BTP_CORE_REGISTER_SERVICE:
        register_service(data, len);
        return;
    case BTP_CORE_UNREGISTER_SERVICE:
        unregister_service(data, len);
        return;
    default:
        tester_rsp(BTP_SERVICE_ID_CORE, opcode, BTP_INDEX_NONE,
                   BTP_STATUS_UNKNOWN_CMD);
        return;
    }
}
