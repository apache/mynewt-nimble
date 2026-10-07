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

#include <stdint.h>
#include <string.h>
#include <nimble/ble.h>
#include <nimble/hci_common.h>
#include <testutil/testutil.h>
#include "ble_ll_conn_priv.h"

/* Handle of a connection that does not exist */
#define TEST_CONN_HANDLE (0x0abc)

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_ENHANCED_CONN_UPDATE)
TEST_CASE_SELF(test_ll_hci_set_default_subrate_len)
{
    struct ble_hci_le_set_default_subrate_cp cmd = {
        .subrate_min = htole16(1),
        .subrate_max = htole16(2),
        .max_latency = htole16(0),
        .cont_num = htole16(0),
        .supervision_tmo = htole16(100),
    };
    uint8_t rsplen = 0;
    int rc;

    rc = ble_ll_conn_hci_set_default_subrate((uint8_t *)&cmd, 0, NULL, &rsplen);
    TEST_ASSERT(rc == BLE_ERR_INV_HCI_CMD_PARMS);

    rc = ble_ll_conn_hci_set_default_subrate((uint8_t *)&cmd, sizeof(cmd) - 1,
                                             NULL, &rsplen);
    TEST_ASSERT(rc == BLE_ERR_INV_HCI_CMD_PARMS);

    rc = ble_ll_conn_hci_set_default_subrate((uint8_t *)&cmd, sizeof(cmd),
                                             NULL, &rsplen);
    TEST_ASSERT(rc == 0);
}

TEST_CASE_SELF(test_ll_hci_subrate_req_len)
{
    struct ble_hci_le_subrate_req_cp cmd = {
        .conn_handle = htole16(TEST_CONN_HANDLE),
        .subrate_min = htole16(1),
        .subrate_max = htole16(2),
        .max_latency = htole16(0),
        .cont_num = htole16(0),
        .supervision_tmo = htole16(100),
    };
    uint8_t rsplen = 0;
    int rc;

    rc = ble_ll_conn_hci_subrate_req((uint8_t *)&cmd, 0, NULL, &rsplen);
    TEST_ASSERT(rc == BLE_ERR_INV_HCI_CMD_PARMS);

    rc = ble_ll_conn_hci_subrate_req((uint8_t *)&cmd, sizeof(cmd) - 1, NULL, &rsplen);
    TEST_ASSERT(rc == BLE_ERR_INV_HCI_CMD_PARMS);

    rc = ble_ll_conn_hci_subrate_req((uint8_t *)&cmd, sizeof(cmd), NULL, &rsplen);
    TEST_ASSERT(rc == BLE_ERR_UNK_CONN_ID);
}
#endif

TEST_SUITE(ble_ll_hci_test_suite)
{
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_ENHANCED_CONN_UPDATE)
    test_ll_hci_set_default_subrate_len();
    test_ll_hci_subrate_req_len();
#endif
}
