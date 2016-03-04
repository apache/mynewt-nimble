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
#include "host/host_hci.h"
#include "host/ble_hs_test.h"
#include "testutil/testutil.h"
#include "ble_hs_test_util.h"

TEST_CASE(ble_host_hci_test_event_bad)
{
    uint8_t buf[2];
    int rc;

    /*** Invalid event code. */
    buf[0] = 0xff;
    buf[1] = 0;
    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == BLE_HS_ENOTSUP);
}

TEST_CASE(ble_host_hci_test_event_cmd_complete)
{
    uint8_t buf[BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN];
    int rc;

    ble_hs_test_util_init();

    /*** Unsent OCF. */
    ble_hs_test_util_build_cmd_complete(buf, sizeof buf, 1, 1, 12345);
    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == BLE_HS_ENOENT);

    /*** No error on NOP. */
    ble_hs_test_util_build_cmd_complete(buf, sizeof buf, 1, 1,
                                        BLE_HCI_OPCODE_NOP);
    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == 0);

    /*** Acknowledge sent command. */
    rc = host_hci_cmd_le_set_adv_enable(0);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_build_cmd_complete(
        buf, sizeof buf, 1, 1,
        (BLE_HCI_OGF_LE << 10) | BLE_HCI_OCF_LE_SET_ADV_ENABLE);
    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == 0);

    /*** Duplicate ack is error. */
    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == BLE_HS_ENOENT);
}

TEST_CASE(ble_host_hci_test_event_cmd_status)
{
    uint8_t buf[BLE_HCI_EVENT_CMD_STATUS_LEN];
    int rc;

    ble_hs_test_util_init();

    /*** Unsent OCF. */
    ble_hs_test_util_build_cmd_status(buf, sizeof buf, 0, 1, 12345);
    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == BLE_HS_ENOENT);

    /*** No error on NOP. */
    ble_hs_test_util_build_cmd_complete(buf, sizeof buf, 0, 1,
                                        BLE_HCI_OPCODE_NOP);
    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == 0);

    /*** Acknowledge sent command. */
    rc = host_hci_cmd_le_set_adv_enable(0);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_build_cmd_status(
        buf, sizeof buf, BLE_ERR_SUCCESS, 1,
        (BLE_HCI_OGF_LE << 10) | BLE_HCI_OCF_LE_SET_ADV_ENABLE);
    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == 0);

    /*** Duplicate ack is error. */
    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == BLE_HS_ENOENT);
}

static int ble_host_hci_test_tx_called;
static uint8_t ble_host_hci_test_handle;

static int
ble_host_hci_test_util_tx_cb(void *arg)
{
    ble_host_hci_test_tx_called++;
    return 0;
}

static void
ble_host_hci_test_util_ack_cb(struct ble_hci_ack *ack, void *arg)
{ }

TEST_CASE(ble_host_hci_test_cancel_bad)
{
    int rc;

    /*** Nonexistant handle. */
    rc = ble_hci_sched_cancel(123);
    TEST_ASSERT(rc == BLE_HS_ENOENT);
}

TEST_CASE(ble_host_hci_test_cancel_pending)
{
    uint8_t hci_handle;
    int rc;

    rc = ble_hci_sched_enqueue(ble_host_hci_test_util_tx_cb, NULL,
                               &hci_handle);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(hci_handle != BLE_HCI_SCHED_HANDLE_NONE);

    rc = ble_hci_sched_cancel(hci_handle);
    TEST_ASSERT(rc == 0);

    ble_hci_sched_wakeup();
    TEST_ASSERT(ble_host_hci_test_tx_called == 0);
}

TEST_CASE(ble_host_hci_test_cancel_cur)
{
    uint8_t hci_handle;
    int rc;

    ble_hci_sched_set_ack_cb(ble_host_hci_test_util_ack_cb, NULL);
    rc = ble_hci_sched_enqueue(ble_host_hci_test_util_tx_cb, NULL,
                               &hci_handle);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(hci_handle != BLE_HCI_SCHED_HANDLE_NONE);

    ble_host_hci_test_handle = hci_handle;

    ble_hci_sched_wakeup();

    TEST_ASSERT(ble_host_hci_test_tx_called == 1);
    TEST_ASSERT(ble_hci_sched_get_ack_cb() != NULL);

    rc = ble_hci_sched_cancel(hci_handle);
    TEST_ASSERT_FATAL(rc == 0);

    TEST_ASSERT(ble_hci_sched_get_ack_cb() == NULL);
}

TEST_SUITE(ble_host_hci_suite)
{
    ble_host_hci_test_event_bad();
    ble_host_hci_test_event_cmd_complete();
    ble_host_hci_test_event_cmd_status();
    ble_host_hci_test_cancel_bad();
    ble_host_hci_test_cancel_pending();
    ble_host_hci_test_cancel_cur();
}

int
ble_host_hci_test_all(void)
{
    ble_host_hci_suite();
    return tu_any_failed;
}
