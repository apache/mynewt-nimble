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
#include "testutil/testutil.h"
#include "nimble/hci_common.h"
#include "host/ble_hs_test.h"
#include "host/host_hci.h"
#include "ble_hs_test_util.h"

static int
ble_hs_conn_test_util_any()
{
    struct ble_hs_conn *conn;

    ble_hs_lock();
    conn = ble_hs_conn_first();
    ble_hs_unlock();

    return conn != NULL;
}

TEST_CASE(ble_hs_conn_test_direct_connect_success)
{
    struct hci_le_conn_complete evt;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint8_t addr[6] = { 1, 2, 3, 4, 5, 6 };
    int rc;

    ble_hs_test_util_init();

    /* Ensure no current or pending connections. */
    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(!ble_hs_conn_test_util_any());

    /* Initiate connection. */
    rc = ble_hs_test_util_conn_initiate(0, addr, NULL, NULL, NULL, 0);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(ble_gap_master_in_progress());

    /* Receive successful connection complete event. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    evt.role = BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER;
    memcpy(evt.peer_addr, addr, 6);
    rc = ble_gap_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(!ble_gap_master_in_progress());

    ble_hs_lock();

    conn = ble_hs_conn_first();
    TEST_ASSERT_FATAL(conn != NULL);
    TEST_ASSERT(conn->bhc_handle == 2);
    TEST_ASSERT(memcmp(conn->bhc_addr, addr, 6) == 0);

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    TEST_ASSERT_FATAL(chan != NULL);
    TEST_ASSERT(chan->blc_my_mtu == BLE_ATT_MTU_PREFERRED_DFLT);
    TEST_ASSERT(chan->blc_peer_mtu == 0);
    TEST_ASSERT(chan->blc_default_mtu == BLE_ATT_MTU_DFLT);

    ble_hs_unlock();
}

TEST_CASE(ble_hs_conn_test_direct_connect_hci_errors)
{
    uint8_t addr[6] = { 1, 2, 3, 4, 5, 6 };
    int rc;

    ble_hs_test_util_init();

    /* Ensure no current or pending connections. */
    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(!ble_hs_conn_test_util_any());

    /* Initiate connection; receive no HCI ack. */
    rc = ble_gap_conn_initiate(0, addr, NULL, NULL, NULL);
    TEST_ASSERT(rc == BLE_HS_ETIMEOUT_HCI);

    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(!ble_hs_conn_test_util_any());
}

TEST_CASE(ble_hs_conn_test_direct_connectable_success)
{
    struct hci_le_conn_complete evt;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint8_t addr[6] = { 1, 2, 3, 4, 5, 6 };
    int rc;

    ble_hs_test_util_init();

    /* Ensure no current or pending connections. */
    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(!ble_gap_slave_in_progress());
    TEST_ASSERT(!ble_hs_conn_test_util_any());

    /* Initiate advertising. */
    rc = ble_hs_test_util_adv_start(BLE_GAP_DISC_MODE_NON,
                                    BLE_GAP_CONN_MODE_DIR, addr,
                                    BLE_HCI_ADV_PEER_ADDR_PUBLIC, NULL, NULL,
                                    NULL, 0, 0);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(ble_gap_slave_in_progress());

    /* Receive successful connection complete event. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    evt.role = BLE_HCI_LE_CONN_COMPLETE_ROLE_SLAVE;
    memcpy(evt.peer_addr, addr, 6);
    rc = ble_gap_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(!ble_gap_slave_in_progress());

    ble_hs_lock();

    conn = ble_hs_conn_first();
    TEST_ASSERT_FATAL(conn != NULL);
    TEST_ASSERT(conn->bhc_handle == 2);
    TEST_ASSERT(memcmp(conn->bhc_addr, addr, 6) == 0);

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    TEST_ASSERT_FATAL(chan != NULL);
    TEST_ASSERT(chan->blc_my_mtu == BLE_ATT_MTU_PREFERRED_DFLT);
    TEST_ASSERT(chan->blc_peer_mtu == 0);
    TEST_ASSERT(chan->blc_default_mtu == BLE_ATT_MTU_DFLT);

    ble_hs_unlock();
}

TEST_CASE(ble_hs_conn_test_direct_connectable_hci_errors)
{
    struct hci_le_conn_complete evt;
    uint8_t addr[6] = { 1, 2, 3, 4, 5, 6 };
    int rc;

    ble_hs_test_util_init();

    /* Ensure no current or pending connections. */
    TEST_ASSERT(!ble_gap_slave_in_progress());
    TEST_ASSERT(!ble_hs_conn_test_util_any());

    /* Initiate connection. */
    rc = ble_hs_test_util_adv_start(BLE_GAP_DISC_MODE_NON,
                                    BLE_GAP_CONN_MODE_DIR, addr,
                                    BLE_HCI_ADV_PEER_ADDR_PUBLIC, NULL, NULL,
                                    NULL, 0, 0);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_slave_in_progress());

    /* Receive failure connection complete event. */
    evt.status = BLE_ERR_UNSPECIFIED;
    rc = ble_gap_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_slave_in_progress());
    TEST_ASSERT(!ble_hs_conn_test_util_any());
}

TEST_CASE(ble_hs_conn_test_undirect_connectable_success)
{
    struct ble_hs_adv_fields adv_fields;
    struct hci_le_conn_complete evt;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint8_t addr[6] = { 1, 2, 3, 4, 5, 6 };
    int rc;

    ble_hs_test_util_init();

    /* Ensure no current or pending connections. */
    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(!ble_gap_slave_in_progress());
    TEST_ASSERT(!ble_hs_conn_test_util_any());

    /* Initiate advertising. */
    memset(&adv_fields, 0, sizeof adv_fields);
    adv_fields.tx_pwr_lvl_is_present = 1;
    rc = ble_gap_adv_set_fields(&adv_fields);
    TEST_ASSERT_FATAL(rc == 0);

    rc = ble_hs_test_util_adv_start(BLE_GAP_DISC_MODE_NON,
                                    BLE_GAP_CONN_MODE_UND, NULL, 0, NULL,
                                    NULL, NULL, 0, 0);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(ble_gap_slave_in_progress());

    /* Receive successful connection complete event. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    evt.role = BLE_HCI_LE_CONN_COMPLETE_ROLE_SLAVE;
    memcpy(evt.peer_addr, addr, 6);
    rc = ble_gap_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(!ble_gap_slave_in_progress());

    ble_hs_lock();

    conn = ble_hs_conn_first();
    TEST_ASSERT_FATAL(conn != NULL);
    TEST_ASSERT(conn->bhc_handle == 2);
    TEST_ASSERT(memcmp(conn->bhc_addr, addr, 6) == 0);

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    TEST_ASSERT_FATAL(chan != NULL);
    TEST_ASSERT(chan->blc_my_mtu == BLE_ATT_MTU_PREFERRED_DFLT);
    TEST_ASSERT(chan->blc_peer_mtu == 0);
    TEST_ASSERT(chan->blc_default_mtu == BLE_ATT_MTU_DFLT);

    ble_hs_unlock();
}

TEST_SUITE(conn_suite)
{
    ble_hs_conn_test_direct_connect_success();
    ble_hs_conn_test_direct_connect_hci_errors();
    ble_hs_conn_test_direct_connectable_success();
    ble_hs_conn_test_direct_connectable_hci_errors();
    ble_hs_conn_test_undirect_connectable_success();
}

int
ble_hs_conn_test_all(void)
{
    conn_suite();

    return tu_any_failed;
}
