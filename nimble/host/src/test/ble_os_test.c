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

#include <string.h>
#include "os/os.h"
#include "testutil/testutil.h"
#include "nimble/hci_common.h"
#include "nimble/hci_transport.h"
#include "host/ble_hs_test.h"
#include "host/ble_gap.h"
#include "ble_hs_test_util.h"

#ifdef ARCH_sim
#define BLE_OS_TEST_STACK_SIZE      1024
#else
#define BLE_OS_TEST_STACK_SIZE      256
#endif

#define BLE_OS_TEST_HS_PRIO         10

static struct os_task ble_os_test_task;
static os_stack_t ble_os_test_stack[OS_STACK_ALIGN(BLE_OS_TEST_STACK_SIZE)];

static uint8_t ble_os_test_peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

static void
ble_os_test_misc_rx_ack(uint8_t ogf, uint8_t ocf, uint8_t status)
{
    uint16_t opcode;
    uint8_t *cmd;
    int rc;

    cmd = os_memblock_get(&g_hci_cmd_pool);
    TEST_ASSERT_FATAL(cmd != NULL);

    opcode = (ogf << 10) | ocf;
    ble_hs_test_util_build_cmd_status(cmd, BLE_HCI_EVENT_CMD_STATUS_LEN,
                                      status, 1, opcode);

    rc = ble_hci_transport_ctlr_event_send(cmd);
    TEST_ASSERT(rc == 0);
}

static void
ble_os_test_misc_rx_le_ack(uint16_t ocf, uint8_t status)
{
    ble_os_test_misc_rx_ack(BLE_HCI_OGF_LE, ocf, status);
}

static int
ble_gap_direct_connect_test_connect_cb(int event, int status,
                                       struct ble_gap_conn_ctxt *ctxt,
                                       void *arg)
{
    int *cb_called;

    cb_called = arg;
    *cb_called = 1;

    TEST_ASSERT(event == BLE_GAP_EVENT_CONN);
    TEST_ASSERT(status == 0);
    TEST_ASSERT(ctxt->desc->conn_handle == 2);
    TEST_ASSERT(ctxt->desc->peer_addr_type == BLE_ADDR_TYPE_PUBLIC);
    TEST_ASSERT(memcmp(ctxt->desc->peer_addr, ble_os_test_peer_addr, 6) == 0);

    return 0;
}

static void
ble_gap_direct_connect_test_task_handler(void *arg)
{
    struct hci_le_conn_complete evt;
    uint8_t addr[6] = { 1, 2, 3, 4, 5, 6 };
    int cb_called;
    int rc;

    /* Receive acknowledgements for the startup sequence.  We sent the
     * corresponding requests when the host task was started.
     */
    ble_hs_test_util_rx_startup_acks();

    /* Set the connect callback so we can verify that it gets called with the
     * proper arguments.
     */
    cb_called = 0;

    /* Make sure there are no created connections and no connections in
     * progress.
     */
    TEST_ASSERT(ble_hs_conn_first() == NULL);

    /* Initiate a direct connection. */
    ble_hs_test_util_conn_initiate(0, addr, NULL,
                                   ble_gap_direct_connect_test_connect_cb,
                                   &cb_called, 0);
    TEST_ASSERT(ble_hs_conn_first() == NULL);
    TEST_ASSERT(!cb_called);

    /* Receive an HCI connection-complete event. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    memcpy(evt.peer_addr, addr, 6);
    rc = ble_gap_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);

    /* The connection should now be created. */
    TEST_ASSERT(ble_hs_conn_find(2) != NULL);
    TEST_ASSERT(cb_called);

    tu_restart();
}

TEST_CASE(ble_gap_direct_connect_test_case)
{
    ble_hs_test_util_init();

    os_task_init(&ble_os_test_task,
                 "ble_gap_direct_connect_test_task",
                 ble_gap_direct_connect_test_task_handler, NULL,
                 BLE_OS_TEST_HS_PRIO + 1, OS_WAIT_FOREVER, ble_os_test_stack,
                 OS_STACK_ALIGN(BLE_OS_TEST_STACK_SIZE));

    os_start();
}

static void
ble_gap_gen_disc_test_connect_cb(int event, int status,
                                 struct ble_gap_disc_desc *desc, void *arg)
{
    int *cb_called;

    cb_called = arg;
    *cb_called = 1;

    TEST_ASSERT(event == BLE_GAP_EVENT_DISC_FINISHED);
    TEST_ASSERT(status == 0);
}

static void
ble_gap_gen_disc_test_task_handler(void *arg)
{
    int cb_called;
    int rc;

    /* Receive acknowledgements for the startup sequence.  We sent the
     * corresponding requests when the host task was started.
     */
    ble_hs_test_util_rx_startup_acks();

    /* Set the connect callback so we can verify that it gets called with the
     * proper arguments.
     */
    cb_called = 0;

    /* Make sure there are no created connections and no connections in
     * progress.
     */
    TEST_ASSERT(ble_hs_conn_first() == NULL);
    TEST_ASSERT(!ble_gap_master_in_progress());

    /* Initiate the general discovery procedure with a 200 ms timeout. */
    rc = ble_hs_test_util_disc(200, BLE_GAP_DISC_MODE_GEN,
                               BLE_HCI_SCAN_TYPE_ACTIVE,
                               BLE_HCI_SCAN_FILT_NO_WL,
                               ble_gap_gen_disc_test_connect_cb,
                               &cb_called, 0, 0);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_hs_conn_first() == NULL);
    TEST_ASSERT(ble_gap_master_in_progress());
    TEST_ASSERT(!cb_called);

    /* Receive acks from the controller. */
    TEST_ASSERT(ble_hs_conn_first() == NULL);
    TEST_ASSERT(ble_gap_master_in_progress());
    TEST_ASSERT(!cb_called);

    /* Wait 100 ms; verify scan still in progress. */
    os_time_delay(100 * OS_TICKS_PER_SEC / 1000);
    TEST_ASSERT(ble_hs_conn_first() == NULL);
    TEST_ASSERT(ble_gap_master_in_progress());
    TEST_ASSERT(!cb_called);

    /* Wait 150 more ms; verify scan completed. */
    os_time_delay(150 * OS_TICKS_PER_SEC / 1000);
    ble_os_test_misc_rx_le_ack(BLE_HCI_OCF_LE_SET_SCAN_ENABLE, 0);
    TEST_ASSERT(ble_hs_conn_first() == NULL);
    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(cb_called);

    tu_restart();
}

TEST_CASE(ble_gap_gen_disc_test_case)
{
    ble_hs_test_util_init();

    os_task_init(&ble_os_test_task,
                 "ble_gap_gen_disc_test_task",
                 ble_gap_gen_disc_test_task_handler, NULL,
                 BLE_OS_TEST_HS_PRIO + 1, OS_WAIT_FOREVER, ble_os_test_stack,
                 OS_STACK_ALIGN(BLE_OS_TEST_STACK_SIZE));

    os_start();
}

static int
ble_gap_terminate_cb(int event, int status,
                     struct ble_gap_conn_ctxt *ctxt, void *arg)
{
    int *disconn_handle;

    TEST_ASSERT_FATAL(event == BLE_GAP_EVENT_CONN);
    if (status == 0) {
        return 0;
    }

    disconn_handle = arg;
    *disconn_handle = ctxt->desc->conn_handle;

    return 0;
}


static void
ble_gap_terminate_test_task_handler(void *arg)
{
    struct hci_disconn_complete disconn_evt;
    struct hci_le_conn_complete conn_evt;
    uint8_t addr1[6] = { 1, 2, 3, 4, 5, 6 };
    uint8_t addr2[6] = { 2, 3, 4, 5, 6, 7 };
    int disconn_handle;
    int rc;

    /* Receive acknowledgements for the startup sequence.  We sent the
     * corresponding requests when the host task was started.
     */
    ble_hs_test_util_rx_startup_acks();

    /* Set the connect callback so we can verify that it gets called with the
     * proper arguments.
     */
    disconn_handle = 0;

    /* Make sure there are no created connections and no connections in
     * progress.
     */
    TEST_ASSERT(ble_hs_conn_first() == NULL);
    TEST_ASSERT(!ble_gap_master_in_progress());

    /* Create two direct connections. */
    ble_hs_test_util_conn_initiate(0, addr1, NULL, ble_gap_terminate_cb,
                                   &disconn_handle, 0);
    memset(&conn_evt, 0, sizeof conn_evt);
    conn_evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    conn_evt.status = BLE_ERR_SUCCESS;
    conn_evt.connection_handle = 1;
    memcpy(conn_evt.peer_addr, addr1, 6);
    rc = ble_gap_rx_conn_complete(&conn_evt);
    TEST_ASSERT(rc == 0);

    ble_hs_test_util_conn_initiate(0, addr2, NULL, ble_gap_terminate_cb,
                                   &disconn_handle, 0);
    memset(&conn_evt, 0, sizeof conn_evt);
    conn_evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    conn_evt.status = BLE_ERR_SUCCESS;
    conn_evt.connection_handle = 2;
    memcpy(conn_evt.peer_addr, addr2, 6);
    rc = ble_gap_rx_conn_complete(&conn_evt);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT_FATAL(ble_hs_conn_find(1) != NULL);
    TEST_ASSERT_FATAL(ble_hs_conn_find(2) != NULL);

    /* Terminate the first one. */
    rc = ble_hs_test_util_conn_terminate(1, 0);
    TEST_ASSERT(rc == 0);
    disconn_evt.connection_handle = 1;
    disconn_evt.status = 0;
    disconn_evt.reason = BLE_ERR_REM_USER_CONN_TERM;
    ble_gap_rx_disconn_complete(&disconn_evt);
    TEST_ASSERT(disconn_handle == 1);
    TEST_ASSERT(ble_hs_conn_find(1) == NULL);
    TEST_ASSERT(ble_hs_conn_find(2) != NULL);

    /* Terminate the second one. */
    rc = ble_hs_test_util_conn_terminate(2, 0);
    TEST_ASSERT(rc == 0);
    disconn_evt.connection_handle = 2;
    disconn_evt.status = 0;
    disconn_evt.reason = BLE_ERR_REM_USER_CONN_TERM;
    ble_gap_rx_disconn_complete(&disconn_evt);
    TEST_ASSERT(disconn_handle == 2);
    TEST_ASSERT(ble_hs_conn_find(1) == NULL);
    TEST_ASSERT(ble_hs_conn_find(2) == NULL);

    tu_restart();
}

TEST_CASE(ble_gap_terminate_test_case)
{
    ble_hs_test_util_init();

    os_task_init(&ble_os_test_task,
                 "ble_gap_terminate_test_task",
                 ble_gap_terminate_test_task_handler, NULL,
                 BLE_OS_TEST_HS_PRIO + 1, OS_WAIT_FOREVER, ble_os_test_stack,
                 OS_STACK_ALIGN(BLE_OS_TEST_STACK_SIZE));

    os_start();
}

TEST_SUITE(ble_os_test_suite)
{
    ble_gap_gen_disc_test_case();
    ble_gap_direct_connect_test_case();
    ble_gap_terminate_test_case();
}

int
ble_os_test_all(void)
{
    ble_os_test_suite();
    return tu_any_failed;
}
