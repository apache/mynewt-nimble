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
#include <errno.h>
#include "testutil/testutil.h"
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "ble_hs_priv.h"
#include "host/ble_hs_test.h"
#include "ble_hs_conn.h"
#include "ble_hci_sched.h"
#include "ble_gatt_priv.h"
#include "ble_gap_conn.h"
#include "ble_hs_test_util.h"

struct ble_gap_conn_event ble_gap_test_conn_event;

static void
ble_gap_test_misc_connect_cb(struct ble_gap_conn_event *event, void *arg)
{
    ble_gap_test_conn_event = *event;
}

static void
ble_gap_test_misc_init(void)
{
    ble_hs_test_util_init();
    ble_gap_conn_set_cb(ble_gap_test_misc_connect_cb, NULL);
}

static void
ble_gap_test_misc_verify_tx_clear_wl(void)
{
    uint8_t param_len;

    TEST_ASSERT_FATAL(ble_hs_test_util_prev_hci_tx != NULL);

    ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                   BLE_HCI_OCF_LE_CLEAR_WHITE_LIST,
                                   &param_len);
    TEST_ASSERT(param_len == 0);
}

static void
ble_gap_test_misc_verify_tx_add_wl(struct ble_gap_white_entry *entry)
{
    uint8_t param_len;
    uint8_t *param;
    int i;

    TEST_ASSERT_FATAL(ble_hs_test_util_prev_hci_tx != NULL);

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_ADD_WHITE_LIST,
                                           &param_len);
    TEST_ASSERT(param_len == 7);
    TEST_ASSERT(param[0] == entry->addr_type);
    for (i = 0; i < 6; i++) {
        TEST_ASSERT(param[1 + i] == entry->addr[i]);
    }
}

static void
ble_gap_test_misc_verify_tx_create_conn(void)
{
    uint8_t param_len;

    TEST_ASSERT_FATAL(ble_hs_test_util_prev_hci_tx != NULL);

    ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_CREATE_CONN,
                                   &param_len);
    TEST_ASSERT(param_len == BLE_HCI_CREATE_CONN_LEN);
}

static void
ble_gap_test_misc_conn_auto_good_once(struct ble_gap_white_entry *white_list,
                                      int white_list_count, int dev_idx)
{
    struct hci_le_conn_complete evt;
    int rc;
    int i;

    ble_gap_test_misc_init();

    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    rc = ble_gap_conn_auto_connect(white_list, white_list_count);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(ble_gap_conn_master_in_progress());

    /* Verify tx of clear white list command. */
    ble_hci_sched_wakeup();
    ble_gap_test_misc_verify_tx_clear_wl();
    ble_hs_test_util_rx_le_ack(BLE_HCI_OCF_LE_CLEAR_WHITE_LIST, 0);

    /* Verify tx of add white list commands. */
    for (i = 0; i < white_list_count; i++) {
        ble_hci_sched_wakeup();
        ble_gap_test_misc_verify_tx_add_wl(white_list + i);
        ble_hs_test_util_rx_le_ack(BLE_HCI_OCF_LE_ADD_WHITE_LIST, 0);
    }

    /* Verify tx of create connection command. */
    ble_hci_sched_wakeup();
    ble_gap_test_misc_verify_tx_create_conn();
    ble_hs_test_util_rx_le_ack(BLE_HCI_OCF_LE_CREATE_CONN, 0);

    TEST_ASSERT(ble_gap_conn_master_in_progress());
    TEST_ASSERT(ble_hs_conn_find(2) == NULL);

    /* Receive connection complete event. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    memcpy(evt.peer_addr, white_list[dev_idx].addr, 6);
    rc = ble_gap_conn_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    TEST_ASSERT(ble_gap_test_conn_event.type ==
                BLE_GAP_CONN_EVENT_TYPE_CONNECT);
    TEST_ASSERT(ble_gap_test_conn_event.conn.status == 0);
    TEST_ASSERT(ble_gap_test_conn_event.conn.handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_event.conn.peer_addr,
                       white_list[dev_idx].addr, 6) == 0);

    TEST_ASSERT(ble_hs_conn_find(2) != NULL);
}

static void
ble_gap_test_misc_conn_auto_bad_addr(struct ble_gap_white_entry *white_list,
                                     int white_list_count)
{
    struct hci_le_conn_complete evt;
    int rc;
    int i;

    ble_gap_test_misc_init();

    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    rc = ble_gap_conn_auto_connect(white_list, white_list_count);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(ble_gap_conn_master_in_progress());

    /* Verify tx of clear white list command. */
    ble_hci_sched_wakeup();
    ble_gap_test_misc_verify_tx_clear_wl();
    ble_hs_test_util_rx_le_ack(BLE_HCI_OCF_LE_CLEAR_WHITE_LIST, 0);

    /* Verify tx of add white list commands. */
    for (i = 0; i < white_list_count; i++) {
        ble_hci_sched_wakeup();
        ble_gap_test_misc_verify_tx_add_wl(white_list + i);
        ble_hs_test_util_rx_le_ack(BLE_HCI_OCF_LE_ADD_WHITE_LIST, 0);
    }

    /* Verify tx of create connection command. */
    ble_hci_sched_wakeup();
    ble_gap_test_misc_verify_tx_create_conn();
    ble_hs_test_util_rx_le_ack(BLE_HCI_OCF_LE_CREATE_CONN, 0);

    TEST_ASSERT(ble_gap_conn_master_in_progress());
    TEST_ASSERT(ble_hs_conn_find(2) == NULL);

    /* Receive connection complete event with unknown address. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    memcpy(evt.peer_addr, ((uint8_t[]){ 1,1,1,1,1,1 }), 6);
    rc = ble_gap_conn_rx_conn_complete(&evt);
    TEST_ASSERT(rc != 0);

    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    TEST_ASSERT(ble_gap_test_conn_event.type ==
                BLE_GAP_CONN_EVENT_TYPE_CONNECT);
    TEST_ASSERT(ble_gap_test_conn_event.conn.status == BLE_HS_ECONTROLLER);

    TEST_ASSERT(ble_hs_conn_find(2) == NULL);
}

TEST_CASE(ble_gap_test_case_conn_auto_good)
{
    struct ble_gap_white_entry white_list[] = { {
        BLE_ADDR_TYPE_PUBLIC, { 1, 2, 3, 4, 5, 6 }
    }, {
        BLE_ADDR_TYPE_PUBLIC, { 2, 3, 4, 5, 6, 7 }
    }, {
        BLE_ADDR_TYPE_PUBLIC, { 3, 4, 5, 6, 7, 8 }
    }, {
        BLE_ADDR_TYPE_PUBLIC, { 4, 5, 6, 7, 8, 9 }
    } };
    int white_list_count = sizeof white_list / sizeof white_list[0];

    int i;

    for (i = 0; i < white_list_count; i++) {
        ble_gap_test_misc_conn_auto_good_once(white_list, white_list_count, i);
    }
}
TEST_CASE(ble_gap_test_case_conn_auto_bad_args)
{
    int rc;

    ble_gap_test_misc_init();

    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    /*** 0 white list entries. */
    rc = ble_gap_conn_auto_connect(NULL, 0);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    /*** Invalid address type. */
    rc = ble_gap_conn_auto_connect(
        ((struct ble_gap_white_entry[]) { {
            5, { 1, 2, 3, 4, 5, 6 }
        }, }), 1);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    /*** Connection already in progress. */
    rc = ble_gap_conn_auto_connect(
        ((struct ble_gap_white_entry[]) { {
            BLE_ADDR_TYPE_PUBLIC, { 1, 2, 3, 4, 5, 6 }
        }, }), 1);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_conn_master_in_progress());

    rc = ble_gap_conn_auto_connect(
        ((struct ble_gap_white_entry[]) { {
            BLE_ADDR_TYPE_PUBLIC, { 2, 3, 4, 5, 6, 7 }
        }, }), 1);
    TEST_ASSERT(rc == BLE_HS_EALREADY);
}

TEST_CASE(ble_gap_test_case_conn_auto_bad_addr)
{
    ble_gap_test_misc_conn_auto_bad_addr(
        ((struct ble_gap_white_entry[]) { {
            BLE_ADDR_TYPE_PUBLIC, { 1, 2, 3, 4, 5, 6 }
        }, {
            BLE_ADDR_TYPE_PUBLIC, { 2, 3, 4, 5, 6, 7 }
        }, {
            BLE_ADDR_TYPE_PUBLIC, { 3, 4, 5, 6, 7, 8 }
        }, {
            BLE_ADDR_TYPE_PUBLIC, { 4, 5, 6, 7, 8, 9 }
        } }), 4);
}

TEST_SUITE(ble_gap_suite)
{
    ble_gap_test_case_conn_auto_good();
    ble_gap_test_case_conn_auto_bad_args();
    ble_gap_test_case_conn_auto_bad_addr();
}

int
ble_gap_test_all(void)
{
    ble_gap_suite();

    return tu_any_failed;
}
