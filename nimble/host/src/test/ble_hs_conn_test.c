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
    TEST_ASSERT(ble_hs_conn_first() == NULL);

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

    conn = ble_hs_conn_first();
    TEST_ASSERT_FATAL(conn != NULL);
    TEST_ASSERT(conn->bhc_handle == 2);
    TEST_ASSERT(memcmp(conn->bhc_addr, addr, 6) == 0);

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    TEST_ASSERT_FATAL(chan != NULL);
    TEST_ASSERT(chan->blc_my_mtu == BLE_ATT_MTU_PREFERRED_DFLT);
    TEST_ASSERT(chan->blc_peer_mtu == 0);
    TEST_ASSERT(chan->blc_default_mtu == BLE_ATT_MTU_DFLT);
}

TEST_CASE(ble_hs_conn_test_direct_connect_hci_errors)
{
    uint8_t addr[6] = { 1, 2, 3, 4, 5, 6 };
    int rc;

    ble_hs_test_util_init();

    /* Ensure no current or pending connections. */
    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(ble_hs_conn_first() == NULL);

    /* Initiate connection; receive no HCI ack. */
    rc = ble_gap_conn_initiate(0, addr, NULL, NULL, NULL);
    TEST_ASSERT(rc == BLE_HS_ETIMEOUT);

    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(ble_hs_conn_first() == NULL);
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
    TEST_ASSERT(ble_hs_conn_first() == NULL);

    /* Initiate advertising. */
    rc = ble_hs_test_util_adv_start(BLE_GAP_DISC_MODE_NON,
                                    BLE_GAP_CONN_MODE_DIR, addr,
                                    BLE_HCI_ADV_PEER_ADDR_PUBLIC, NULL, NULL,
                                    NULL, 0, 0);
    TEST_ASSERT(rc == 0);

    ble_hci_sched_wakeup();

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

    conn = ble_hs_conn_first();
    TEST_ASSERT_FATAL(conn != NULL);
    TEST_ASSERT(conn->bhc_handle == 2);
    TEST_ASSERT(memcmp(conn->bhc_addr, addr, 6) == 0);

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    TEST_ASSERT_FATAL(chan != NULL);
    TEST_ASSERT(chan->blc_my_mtu == BLE_ATT_MTU_PREFERRED_DFLT);
    TEST_ASSERT(chan->blc_peer_mtu == 0);
    TEST_ASSERT(chan->blc_default_mtu == BLE_ATT_MTU_DFLT);
}

TEST_CASE(ble_hs_conn_test_direct_connectable_hci_errors)
{
    struct hci_le_conn_complete evt;
    uint8_t addr[6] = { 1, 2, 3, 4, 5, 6 };
    int rc;

    ble_hs_test_util_init();

   /* Ensure no current or pending connections. */
    TEST_ASSERT(!ble_gap_slave_in_progress());
    TEST_ASSERT(ble_hs_conn_first() == NULL);

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
    TEST_ASSERT(ble_hs_conn_first() == NULL);
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
    TEST_ASSERT(ble_hs_conn_first() == NULL);

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

    conn = ble_hs_conn_first();
    TEST_ASSERT_FATAL(conn != NULL);
    TEST_ASSERT(conn->bhc_handle == 2);
    TEST_ASSERT(memcmp(conn->bhc_addr, addr, 6) == 0);

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    TEST_ASSERT_FATAL(chan != NULL);
    TEST_ASSERT(chan->blc_my_mtu == BLE_ATT_MTU_PREFERRED_DFLT);
    TEST_ASSERT(chan->blc_peer_mtu == 0);
    TEST_ASSERT(chan->blc_default_mtu == BLE_ATT_MTU_DFLT);
}
TEST_CASE(ble_hs_conn_test_completed_pkts)
{
    struct ble_hs_conn *conn1;
    struct ble_hs_conn *conn2;

    ble_hs_test_util_init();

    conn1 = ble_hs_test_util_create_conn(1, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                         NULL, NULL);
    conn2 = ble_hs_test_util_create_conn(2, ((uint8_t[]){3,4,5,6,7,8,9,10}),
                                         NULL, NULL);

    conn1->bhc_outstanding_pkts = 5;
    conn2->bhc_outstanding_pkts = 5;

    /*** Event specifies nonexistent connection; no effect. */
    ble_hs_test_util_rx_num_completed_pkts_event(
        (struct ble_hs_test_util_num_completed_pkts_entry []) {
            { 5, 5 },
            { 0 }});
    TEST_ASSERT(conn1->bhc_outstanding_pkts == 5);
    TEST_ASSERT(conn2->bhc_outstanding_pkts == 5);

    /*** Event specifies connection 1. */
    ble_hs_test_util_rx_num_completed_pkts_event(
        (struct ble_hs_test_util_num_completed_pkts_entry []) {
            { 1, 1 },
            { 0 }});
    TEST_ASSERT(conn1->bhc_outstanding_pkts == 4);
    TEST_ASSERT(conn2->bhc_outstanding_pkts == 5);

    /*** Event specifies connection 2. */
    ble_hs_test_util_rx_num_completed_pkts_event(
        (struct ble_hs_test_util_num_completed_pkts_entry []) {
            { 2, 1 },
            { 0 }});
    TEST_ASSERT(conn1->bhc_outstanding_pkts == 4);
    TEST_ASSERT(conn2->bhc_outstanding_pkts == 4);

    /*** Event specifies connections 1 and 2. */
    ble_hs_test_util_rx_num_completed_pkts_event(
        (struct ble_hs_test_util_num_completed_pkts_entry []) {
            { 1, 2 },
            { 2, 2 },
            { 0 }});
    TEST_ASSERT(conn1->bhc_outstanding_pkts == 2);
    TEST_ASSERT(conn2->bhc_outstanding_pkts == 2);

    /*** Event specifies connections 1, 2, and nonexistent. */
    ble_hs_test_util_rx_num_completed_pkts_event(
        (struct ble_hs_test_util_num_completed_pkts_entry []) {
            { 1, 1 },
            { 2, 1 },
            { 10, 50 },
            { 0 }});
    TEST_ASSERT(conn1->bhc_outstanding_pkts == 1);
    TEST_ASSERT(conn2->bhc_outstanding_pkts == 1);

    /*** Don't wrap when count gets out of sync. */
    ble_hs_test_util_rx_num_completed_pkts_event(
        (struct ble_hs_test_util_num_completed_pkts_entry []) {
            { 1, 10 },
            { 0 }});
    TEST_ASSERT(conn1->bhc_outstanding_pkts == 0);
    TEST_ASSERT(conn2->bhc_outstanding_pkts == 1);
}

TEST_SUITE(conn_suite)
{
    ble_hs_conn_test_direct_connect_success();
    ble_hs_conn_test_direct_connect_hci_errors();
    ble_hs_conn_test_direct_connectable_success();
    ble_hs_conn_test_direct_connectable_hci_errors();
    ble_hs_conn_test_undirect_connectable_success();
    ble_hs_conn_test_completed_pkts();
}

int
ble_hs_conn_test_all(void)
{
    conn_suite();

    return tu_any_failed;
}
