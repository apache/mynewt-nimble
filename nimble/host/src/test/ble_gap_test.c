/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include "os/os.h"
#include "testutil/testutil.h"
#include "nimble/hci_common.h"
#include "nimble/hci_transport.h"
#include "host/ble_hs.h"
#include "host/ble_hs_test.h"
#include "host/ble_gap.h"
#include "ble_hs_test_util.h"
#include "ble_hs_conn.h"
#include "ble_hs_work.h"
#include "ble_gap_conn.h"

#ifdef ARCH_sim
#define BLE_GAP_TEST_STACK_SIZE     1024
#else
#define BLE_GAP_TEST_STACK_SIZE     256
#endif

#define BLE_GAP_TEST_HS_PRIO        10

static struct os_task ble_gap_test_task;
static os_stack_t ble_gap_test_stack[OS_STACK_ALIGN(BLE_GAP_TEST_STACK_SIZE)];

static uint8_t ble_gap_test_peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

static void
ble_gap_test_misc_rx_ack(uint16_t ocf, uint8_t status)
{
    uint16_t opcode;
    uint8_t buf[BLE_HCI_EVENT_CMD_STATUS_LEN];
    int rc;

    opcode = (BLE_HCI_OGF_LE << 10) | ocf;
    ble_hs_test_util_build_cmd_status(buf, sizeof buf, status, 1, opcode);

    rc = ble_hci_transport_ctlr_event_send(buf);
    TEST_ASSERT(rc == 0);
}

static void
ble_gap_test_connect_cb(struct ble_gap_connect_desc *desc, void *arg)
{
    int *cb_called;

    cb_called = arg;
    *cb_called = 1;

    TEST_ASSERT(desc->status == BLE_ERR_SUCCESS);
    TEST_ASSERT(desc->handle == 2);
    TEST_ASSERT(memcmp(desc->peer_addr, ble_gap_test_peer_addr, 6) == 0);
}

static void 
ble_gap_test_task_handler(void *arg) 
{
    struct hci_le_conn_complete evt;
    uint8_t addr[6] = { 1, 2, 3, 4, 5, 6 };
    int cb_called;
    int rc;

    /* Set the connect callback so we can verify that it gets called with the
     * proper arguments.
     */
    cb_called = 0;
    ble_gap_set_connect_cb(ble_gap_test_connect_cb, &cb_called);

    /* Make sure there are no created connections and no connections in
     * progress.
     */
    TEST_ASSERT(!ble_hs_work_busy);
    TEST_ASSERT(ble_hs_conn_first() == NULL);

    /* Initiate a direct connection. */
    ble_gap_direct_connection_establishment(0, addr);
    TEST_ASSERT(ble_hs_work_busy);
    TEST_ASSERT(ble_hs_conn_first() == NULL);
    TEST_ASSERT(!cb_called);

    /* Receive an ack for the HCI create-connection command. */
    ble_gap_test_misc_rx_ack(BLE_HCI_OCF_LE_CREATE_CONN, 0);
    TEST_ASSERT(!ble_hs_work_busy);
    TEST_ASSERT(ble_hs_conn_first() == NULL);
    TEST_ASSERT(!cb_called);

    /* Receive an HCI connection-complete event. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    memcpy(evt.peer_addr, addr, 6);
    rc = ble_gap_conn_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);

    /* The connection should now be created. */
    TEST_ASSERT(ble_hs_conn_find(2) != NULL);
    TEST_ASSERT(cb_called);

    tu_restart();
}

TEST_CASE(ble_gap_test_case)
{
    os_init();

    os_task_init(&ble_gap_test_task, "ble_gap_test_task",
                 ble_gap_test_task_handler, NULL,
                 BLE_GAP_TEST_HS_PRIO + 1, OS_WAIT_FOREVER, ble_gap_test_stack,
                 OS_STACK_ALIGN(BLE_GAP_TEST_STACK_SIZE));

    ble_hs_init(BLE_GAP_TEST_HS_PRIO);

    os_start();
}

TEST_SUITE(ble_gap_test_suite)
{
    ble_gap_test_case();
}

int
ble_gap_test_all(void)
{
    ble_gap_test_suite();
    return tu_any_failed;
}
