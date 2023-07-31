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

static uint8_t registered_services[((BTP_SERVICE_ID_MAX - 1) / 8) + 1];

static uint8_t
supported_commands(const void *cmd, uint16_t cmd_len,
                   void *rsp, uint16_t *rsp_len)
{
    struct btp_core_read_supported_commands_rp *rp = rsp;

    tester_set_bit(rp->data, BTP_CORE_READ_SUPPORTED_COMMANDS);
    tester_set_bit(rp->data, BTP_CORE_READ_SUPPORTED_SERVICES);
    tester_set_bit(rp->data, BTP_CORE_REGISTER_SERVICE);
    tester_set_bit(rp->data, BTP_CORE_UNREGISTER_SERVICE);

    *rsp_len = sizeof(*rp) + 1;

    return BTP_STATUS_SUCCESS;
}

static uint8_t
supported_services(const void *cmd, uint16_t cmd_len,
                   void *rsp, uint16_t *rsp_len)
{
    struct btp_core_read_supported_services_rp *rp = rsp;

    /* octet 0 */
    tester_set_bit(rp->data, BTP_SERVICE_ID_CORE);
    tester_set_bit(rp->data, BTP_SERVICE_ID_GAP);
    tester_set_bit(rp->data, BTP_SERVICE_ID_GATT);
#if MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM)
    tester_set_bit(rp->data, BTP_SERVICE_ID_L2CAP);
#endif /* MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM) */
#if MYNEWT_VAL(BLE_MESH)
    tester_set_bit(rp->data, BTP_SERVICE_ID_MESH);
#endif /* MYNEWT_VAL(BLE_MESH) */
    tester_set_bit(rp->data, BTP_SERVICE_ID_GATTC);

    *rsp_len = sizeof(*rp) + 2;

    return BTP_STATUS_SUCCESS;
}

static uint8_t
register_service(const void *cmd, uint16_t cmd_len,
                 void *rsp, uint16_t *rsp_len)
{
    const struct btp_core_register_service_cmd *cp = cmd;
    uint8_t status;

    /* invalid service */
    if ((cp->id == BTP_SERVICE_ID_CORE) || (cp->id > BTP_SERVICE_ID_MAX)) {
        return BTP_STATUS_FAILED;
    }

    /* already registered */
    if (tester_test_bit(registered_services, cp->id)) {
        return BTP_STATUS_FAILED;
    }

    switch (cp->id) {
    case BTP_SERVICE_ID_GAP:
        status = tester_init_gap();
        break;
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
    case BTP_SERVICE_ID_GATTC:
        status = tester_init_gatt_cl();
        break;
    default:
        status = BTP_STATUS_FAILED;
        break;
    }

    if (status == BTP_STATUS_SUCCESS) {
        tester_set_bit(registered_services, cp->id);
    }

    return status;
}

static uint8_t
unregister_service(const void *cmd, uint16_t cmd_len,
                   void *rsp, uint16_t *rsp_len)
{
    const struct btp_core_unregister_service_cmd *cp = cmd;
    uint8_t status;

    /* invalid service ID */
    if ((cp->id == BTP_SERVICE_ID_CORE) || (cp->id > BTP_SERVICE_ID_MAX)) {
        return BTP_STATUS_FAILED;
    }

    /* not registered */
    if (!tester_test_bit(registered_services, cp->id)) {
        return BTP_STATUS_FAILED;
    }

    switch (cp->id) {
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
    case BTP_SERVICE_ID_GATTC:
        status = tester_unregister_gatt_cl();
        break;
    default:
        status = BTP_STATUS_FAILED;
        break;
    }

    if (status == BTP_STATUS_SUCCESS) {
        tester_clear_bit(registered_services, cp->id);
    }

    return status;
}

static const struct btp_handler handlers[] = {
    {
        .opcode = BTP_CORE_READ_SUPPORTED_COMMANDS,
        .index = BTP_INDEX_NONE,
        .expect_len = 0,
        .func = supported_commands,
    },
    {
        .opcode = BTP_CORE_READ_SUPPORTED_SERVICES,
        .index = BTP_INDEX_NONE,
        .expect_len = 0,
        .func = supported_services,
    },
    {
        .opcode = BTP_CORE_REGISTER_SERVICE,
        .index = BTP_INDEX_NONE,
        .expect_len = sizeof(struct btp_core_register_service_cmd),
        .func = register_service,
    },
    {
        .opcode = BTP_CORE_UNREGISTER_SERVICE,
        .index = BTP_INDEX_NONE,
        .expect_len = sizeof(struct btp_core_unregister_service_cmd),
        .func = unregister_service,
    },
};

void
tester_init_core(void)
{
    tester_register_command_handlers(BTP_SERVICE_ID_CORE, handlers,
                                     ARRAY_SIZE(handlers));
    tester_set_bit(registered_services, BTP_SERVICE_ID_CORE);
}
