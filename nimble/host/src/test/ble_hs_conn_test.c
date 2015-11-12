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

#include <stddef.h>
#include <errno.h>
#include <string.h>
#include "nimble/hci_common.h"
#include "host/ble_hs.h"
#include "host/ble_hs_test.h"
#include "host/host_hci.h"
#include "ble_l2cap.h"
#include "ble_hs_att.h"
#include "ble_hs_conn.h"
#include "ble_hs_ack.h"
#include "ble_gap_conn.h"
#include "ble_hs_test_util.h"
#include "testutil/testutil.h"

TEST_CASE(ble_hs_conn_test_master_direct_success)
{
    struct hci_le_conn_complete evt;
    struct ble_hs_conn *conn;
    uint8_t addr[6] = { 1, 2, 3, 4, 5, 6 };
    int rc;

    rc = ble_hs_init();
    TEST_ASSERT_FATAL(rc == 0);

    /* Ensure no current or pending connections. */
    TEST_ASSERT(!ble_gap_conn_master_in_progress());
    TEST_ASSERT(ble_hs_conn_first() == NULL);

    /* Initiate connection. */
    rc = ble_gap_conn_initiate_direct(0, addr);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_conn_master_in_progress());

    /* Receive command status event. */
    ble_hs_test_util_rx_ack(BLE_HCI_OCF_LE_CREATE_CONN, BLE_ERR_SUCCESS);
    TEST_ASSERT(ble_gap_conn_master_in_progress());

    /* Receive successful connection complete event. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    memcpy(evt.peer_addr, addr, 6);
    rc = ble_gap_conn_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    conn = ble_hs_conn_first();
    TEST_ASSERT_FATAL(conn != NULL);
    TEST_ASSERT(conn->bhc_handle == 2);
    TEST_ASSERT(conn->bhc_att_mtu == BLE_HS_ATT_MTU_DFLT);
    TEST_ASSERT(memcmp(conn->bhc_addr, addr, 6) == 0);
}

TEST_CASE(ble_hs_conn_test_master_direct_hci_errors)
{
    struct hci_le_conn_complete evt;
    uint8_t addr[6] = { 1, 2, 3, 4, 5, 6 };
    int rc;

    rc = ble_hs_init();
    TEST_ASSERT_FATAL(rc == 0);

    /* Ensure no current or pending connections. */
    TEST_ASSERT(!ble_gap_conn_master_in_progress());
    TEST_ASSERT(ble_hs_conn_first() == NULL);

    /* Initiate connection. */
    rc = ble_gap_conn_initiate_direct(0, addr);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_conn_master_in_progress());

    /* Receive connection complete event without intervening command status. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    memcpy(evt.peer_addr, addr, 6);
    rc = ble_gap_conn_rx_conn_complete(&evt);
    TEST_ASSERT(rc != 0);
    TEST_ASSERT(ble_gap_conn_master_in_progress());

    /* Receive success command status event. */
    ble_hs_test_util_rx_ack(BLE_HCI_OCF_LE_CREATE_CONN, BLE_ERR_SUCCESS);
    TEST_ASSERT(ble_gap_conn_master_in_progress());

    /* Receive failure connection complete event. */
    evt.status = BLE_ERR_UNSPECIFIED;
    rc = ble_gap_conn_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(!ble_gap_conn_master_in_progress());
    TEST_ASSERT(ble_hs_conn_first() == NULL);
}

TEST_CASE(ble_hs_conn_test_slave_direct_success)
{
    struct hci_le_conn_complete evt;
    struct ble_hs_conn *conn;
    uint8_t addr[6] = { 1, 2, 3, 4, 5, 6 };
    int rc;

    rc = ble_hs_init();
    TEST_ASSERT_FATAL(rc == 0);

    /* Ensure no current or pending connections. */
    TEST_ASSERT(!ble_gap_conn_master_in_progress());
    TEST_ASSERT(!ble_gap_conn_slave_in_progress());
    TEST_ASSERT(ble_hs_conn_first() == NULL);

    /* Initiate advertising. */
    rc = ble_gap_conn_advertise_direct(0, addr);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(!ble_gap_conn_master_in_progress());
    TEST_ASSERT(ble_gap_conn_slave_in_progress());

    /* Receive set-adv-params ack. */
    ble_hs_test_util_rx_ack(BLE_HCI_OCF_LE_SET_ADV_PARAMS, BLE_ERR_SUCCESS);
    TEST_ASSERT(!ble_gap_conn_master_in_progress());
    TEST_ASSERT(ble_gap_conn_slave_in_progress());

    /* Receive set-adv-enable ack. */
    ble_hs_test_util_rx_ack(BLE_HCI_OCF_LE_SET_ADV_ENABLE, BLE_ERR_SUCCESS);
    TEST_ASSERT(!ble_gap_conn_master_in_progress());
    TEST_ASSERT(ble_gap_conn_slave_in_progress());

    /* Receive successful connection complete event. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    memcpy(evt.peer_addr, addr, 6);
    rc = ble_gap_conn_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(!ble_gap_conn_master_in_progress());
    TEST_ASSERT(!ble_gap_conn_slave_in_progress());

    conn = ble_hs_conn_first();
    TEST_ASSERT_FATAL(conn != NULL);
    TEST_ASSERT(conn->bhc_handle == 2);
    TEST_ASSERT(conn->bhc_att_mtu == BLE_HS_ATT_MTU_DFLT);
    TEST_ASSERT(memcmp(conn->bhc_addr, addr, 6) == 0);
}

TEST_SUITE(conn_suite)
{
    ble_hs_conn_test_master_direct_success();
    ble_hs_conn_test_master_direct_hci_errors();
    ble_hs_conn_test_slave_direct_success();
}

int
ble_hs_conn_test_all(void)
{
    conn_suite();

    return tu_any_failed;
}
