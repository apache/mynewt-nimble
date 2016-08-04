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

#include <stddef.h>
#include <errno.h>
#include <string.h>
#include "nimble/hci_common.h"
#include "nimble/ble_hci_trans.h"
#include "host/host_hci.h"
#include "host/ble_hs_test.h"
#include "testutil/testutil.h"
#include "ble_hs_test_util.h"

TEST_CASE(ble_host_hci_test_event_bad)
{
    uint8_t *buf;
    int rc;

    /*** Invalid event code. */
    buf = ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_HI);
    TEST_ASSERT_FATAL(buf != NULL);

    buf[0] = 0xff;
    buf[1] = 0;
    rc = host_hci_evt_process(buf);
    TEST_ASSERT(rc == BLE_HS_ENOTSUP);
}

TEST_CASE(ble_host_hci_test_rssi)
{
    uint8_t params[BLE_HCI_READ_RSSI_ACK_PARAM_LEN];
    uint16_t opcode;
    int8_t rssi;
    int rc;

    opcode = host_hci_opcode_join(BLE_HCI_OGF_STATUS_PARAMS,
                                  BLE_HCI_OCF_RD_RSSI);

    /*** Success. */
    /* Connection handle. */
    htole16(params + 0, 1);

    /* RSSI. */
    params[2] = -8;

    ble_hs_test_util_set_ack_params(opcode, 0, params, sizeof params);

    rc = ble_hci_util_read_rssi(1, &rssi);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(rssi == -8);

    /*** Failure: incorrect connection handle. */
    htole16(params + 0, 99);

    ble_hs_test_util_set_ack_params(opcode, 0, params, sizeof params);

    rc = ble_hci_util_read_rssi(1, &rssi);
    TEST_ASSERT(rc == BLE_HS_ECONTROLLER);

    /*** Failure: params too short. */
    ble_hs_test_util_set_ack_params(opcode, 0, params, sizeof params - 1);
    rc = ble_hci_util_read_rssi(1, &rssi);
    TEST_ASSERT(rc == BLE_HS_ECONTROLLER);

    /*** Failure: params too long. */
    ble_hs_test_util_set_ack_params(opcode, 0, params, sizeof params + 1);
    rc = ble_hci_util_read_rssi(1, &rssi);
    TEST_ASSERT(rc == BLE_HS_ECONTROLLER);
}

TEST_SUITE(ble_host_hci_suite)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_host_hci_test_event_bad();
    ble_host_hci_test_rssi();
}

int
ble_host_hci_test_all(void)
{
    ble_host_hci_suite();
    return tu_any_failed;
}
