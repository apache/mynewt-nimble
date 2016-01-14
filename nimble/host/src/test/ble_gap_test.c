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
#include "host/ble_hs_test.h"
#include "ble_hs_priv.h"
#include "ble_hs_conn.h"
#include "ble_hci_sched.h"
#include "ble_gatt_priv.h"
#include "ble_gap_priv.h"
#include "ble_hs_test_util.h"

static int ble_gap_test_conn_event;
static int ble_gap_test_conn_status;
static struct ble_gap_conn_desc ble_gap_test_conn_desc;
static void *ble_gap_test_conn_arg;

static int ble_gap_test_wl_status;
static void *ble_gap_test_wl_arg;

static void
ble_gap_test_misc_init(void)
{
    ble_hs_test_util_init();

    ble_gap_test_conn_event = -1;
    memset(&ble_gap_test_conn_desc, 0xff, sizeof ble_gap_test_conn_desc);
    ble_gap_test_conn_arg = (void *)-1;

    ble_gap_test_wl_status = -1;
    ble_gap_test_wl_arg = (void *)-1;
}

static void
ble_gap_test_misc_connect_cb(int event, int status,
                             struct ble_gap_conn_desc *desc, void *arg)
{
    ble_gap_test_conn_event = event;
    ble_gap_test_conn_status = status;
    ble_gap_test_conn_desc = *desc;
    ble_gap_test_conn_arg = arg;
}

static void
ble_gap_test_misc_wl_cb(int status, void *arg)
{
    ble_gap_test_wl_status = status;
    ble_gap_test_wl_arg = arg;
}

static int
ble_gap_test_misc_rx_hci_ack(int *cmd_idx, int cmd_fail_idx,
                             uint8_t ogf, uint16_t ocf, uint8_t fail_status)
{
    uint16_t opcode;
    int cur_idx;

    opcode = (ogf << 10) | ocf;

    cur_idx = *cmd_idx;
    (*cmd_idx)++;

    if (cur_idx == cmd_fail_idx) {
        ble_hs_test_util_rx_ack(opcode, fail_status);
        return 1;
    } else {
        ble_hs_test_util_rx_ack(opcode, 0);
        return 0;
    }
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
ble_gap_test_misc_verify_tx_create_conn(uint8_t filter_policy)
{
    uint8_t param_len;
    uint8_t *param;

    TEST_ASSERT_FATAL(ble_hs_test_util_prev_hci_tx != NULL);

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_CREATE_CONN,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_CREATE_CONN_LEN);

    TEST_ASSERT(param[4] == filter_policy);

    /* XXX: Verify other fields. */
}

static void
ble_gap_test_misc_verify_tx_create_conn_cancel(void)
{
    uint8_t param_len;

    TEST_ASSERT_FATAL(ble_hs_test_util_prev_hci_tx != NULL);

    ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                   BLE_HCI_OCF_LE_CREATE_CONN_CANCEL,
                                   &param_len);
    TEST_ASSERT(param_len == 0);
}

static void
ble_gap_test_misc_verify_tx_disconnect(void)
{
    uint8_t param_len;
    uint8_t *param;

    TEST_ASSERT_FATAL(ble_hs_test_util_prev_hci_tx != NULL);

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LINK_CTRL,
                                           BLE_HCI_OCF_DISCONNECT_CMD,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_DISCONNECT_CMD_LEN);
    TEST_ASSERT(le16toh(param + 0) == 2);
    TEST_ASSERT(param[2] == BLE_ERR_REM_USER_CONN_TERM);
}

static void
ble_gap_test_misc_wl_set(struct ble_gap_white_entry *white_list,
                         int white_list_count, int cmd_fail_idx,
                         uint8_t hci_status)
{
    int cmd_idx;
    int rc;
    int i;

    ble_gap_test_misc_init();
    cmd_idx = 0;

    TEST_ASSERT(!ble_gap_conn_wl_busy());

    rc = ble_gap_conn_wl_set(white_list, white_list_count,
                             ble_gap_test_misc_wl_cb, NULL);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_conn_wl_busy());

    /* Verify tx of clear white list command. */
    ble_hci_sched_wakeup();
    ble_gap_test_misc_verify_tx_clear_wl();
    rc = ble_gap_test_misc_rx_hci_ack(&cmd_idx, cmd_fail_idx, BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_CLEAR_WHITE_LIST,
                                      hci_status);
    if (rc != 0) {
        return;
    }

    /* Verify tx of add white list commands. */
    for (i = 0; i < white_list_count; i++) {
        TEST_ASSERT(ble_gap_conn_wl_busy());
        ble_hci_sched_wakeup();
        ble_gap_test_misc_verify_tx_add_wl(white_list + i);
        rc = ble_gap_test_misc_rx_hci_ack(&cmd_idx, cmd_fail_idx,
                                          BLE_HCI_OGF_LE,
                                          BLE_HCI_OCF_LE_ADD_WHITE_LIST,
                                          hci_status);
        if (rc != 0) {
            return;
        }
    }

    TEST_ASSERT_FATAL(cmd_fail_idx < 0);
    TEST_ASSERT(!ble_gap_conn_wl_busy());
}

static void
ble_gap_test_misc_terminate(uint8_t *peer_addr, int cmd_fail_idx,
                            uint8_t hci_status)
{
    struct hci_disconn_complete evt;
    int cmd_idx;
    int rc;

    ble_gap_test_misc_init();
    cmd_idx = 0;

    /* Create a connection. */
    ble_hs_test_util_create_conn(2, peer_addr, ble_gap_test_misc_connect_cb,
                                 NULL);

    /* Terminate the connection. */
    rc = ble_gap_conn_terminate(2);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    /* Verify tx of disconnect command. */
    ble_hci_sched_wakeup();
    ble_gap_test_misc_verify_tx_disconnect();
    rc = ble_gap_test_misc_rx_hci_ack(&cmd_idx, cmd_fail_idx,
                                      BLE_HCI_OGF_LINK_CTRL,
                                      BLE_HCI_OCF_DISCONNECT_CMD,
                                      hci_status);
    if (rc != 0) {
        return;
    }

    /* Receive disconnection complete event. */
    evt.connection_handle = 2;
    evt.status = 0;
    evt.reason = BLE_ERR_CONN_TERM_LOCAL;
    ble_gap_conn_rx_disconn_complete(&evt);
}

TEST_CASE(ble_gap_test_case_conn_wl_good)
{
    struct ble_gap_white_entry white_list[] = {
        { BLE_ADDR_TYPE_PUBLIC, { 1, 2, 3, 4, 5, 6 } },
        { BLE_ADDR_TYPE_PUBLIC, { 2, 3, 4, 5, 6, 7 } },
        { BLE_ADDR_TYPE_PUBLIC, { 3, 4, 5, 6, 7, 8 } },
        { BLE_ADDR_TYPE_PUBLIC, { 4, 5, 6, 7, 8, 9 } },
    };
    int white_list_count = sizeof white_list / sizeof white_list[0];

    ble_gap_test_misc_wl_set(white_list, white_list_count, -1, 0);
    TEST_ASSERT(ble_gap_test_wl_status == 0);
    TEST_ASSERT(ble_gap_test_wl_arg == NULL);
}

TEST_CASE(ble_gap_test_case_conn_wl_bad_args)
{
    int rc;

    ble_gap_test_misc_init();

    TEST_ASSERT(!ble_gap_conn_wl_busy());

    /*** 0 white list entries. */
    rc = ble_gap_conn_wl_set(NULL, 0, ble_gap_test_misc_wl_cb, NULL);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
    TEST_ASSERT(!ble_gap_conn_wl_busy());

    /*** Invalid address type. */
    rc = ble_gap_conn_wl_set(
        ((struct ble_gap_white_entry[]) { {
            5, { 1, 2, 3, 4, 5, 6 }
        }, }),
        1, ble_gap_test_misc_wl_cb, NULL);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
    TEST_ASSERT(!ble_gap_conn_wl_busy());

    /*** White-list-using connection in progress. */
    rc = ble_gap_conn_direct_connect(BLE_GAP_ADDR_TYPE_WL, NULL,
                                     ble_gap_test_misc_connect_cb, NULL);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_conn_wl_busy());

    rc = ble_gap_conn_wl_set(
        ((struct ble_gap_white_entry[]) { {
            BLE_ADDR_TYPE_PUBLIC, { 1, 2, 3, 4, 5, 6 }
        }, }),
        1, ble_gap_test_misc_wl_cb, NULL);
    TEST_ASSERT(rc == BLE_HS_EBUSY);
    TEST_ASSERT(ble_gap_conn_wl_busy());
}

TEST_CASE(ble_gap_test_case_conn_wl_ctlr_fail)
{
    int i;

    struct ble_gap_white_entry white_list[] = {
        { BLE_ADDR_TYPE_PUBLIC, { 1, 2, 3, 4, 5, 6 } },
        { BLE_ADDR_TYPE_PUBLIC, { 2, 3, 4, 5, 6, 7 } },
        { BLE_ADDR_TYPE_PUBLIC, { 3, 4, 5, 6, 7, 8 } },
        { BLE_ADDR_TYPE_PUBLIC, { 4, 5, 6, 7, 8, 9 } },
    };
    int white_list_count = sizeof white_list / sizeof white_list[0];

    for (i = 0; i < 5; i++) {
        ble_gap_test_misc_wl_set(white_list, white_list_count, i,
                                 BLE_ERR_UNSPECIFIED);
        TEST_ASSERT(ble_gap_test_wl_status != 0);
        TEST_ASSERT(ble_gap_test_wl_arg == NULL);
    }
}

TEST_CASE(ble_gap_test_case_conn_dir_good)
{
    struct hci_le_conn_complete evt;
    int rc;

    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_misc_init();

    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    rc = ble_gap_conn_direct_connect(BLE_ADDR_TYPE_PUBLIC, peer_addr,
                                     ble_gap_test_misc_connect_cb, NULL);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(ble_gap_conn_master_in_progress());

    /* Verify tx of create connection command. */
    ble_hci_sched_wakeup();
    ble_gap_test_misc_verify_tx_create_conn(BLE_HCI_CONN_FILT_NO_WL);
    ble_hs_test_util_rx_le_ack(BLE_HCI_OCF_LE_CREATE_CONN, 0);

    TEST_ASSERT(ble_gap_conn_master_in_progress());
    TEST_ASSERT(ble_hs_conn_find(2) == NULL);

    /* Receive connection complete event. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    memcpy(evt.peer_addr, peer_addr, 6);
    rc = ble_gap_conn_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CONN);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_addr,
                       peer_addr, 6) == 0);

    TEST_ASSERT(ble_hs_conn_find(2) != NULL);
}

TEST_CASE(ble_gap_test_case_conn_dir_bad_args)
{
    int rc;

    ble_gap_test_misc_init();

    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    /*** Invalid address type. */
    rc = ble_gap_conn_direct_connect(5, ((uint8_t[]){ 1, 2, 3, 4, 5, 6 }),
                                     ble_gap_test_misc_connect_cb, NULL);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    /*** Connection already in progress. */
    rc = ble_gap_conn_direct_connect(BLE_ADDR_TYPE_PUBLIC,
                                     ((uint8_t[]){ 1, 2, 3, 4, 5, 6 }),
                                     ble_gap_test_misc_connect_cb, NULL);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_conn_master_in_progress());

    rc = ble_gap_conn_direct_connect(BLE_ADDR_TYPE_PUBLIC,
                                     ((uint8_t[]){ 2, 3, 4, 5, 6, 7 }),
                                     ble_gap_test_misc_connect_cb, NULL);
    TEST_ASSERT(rc == BLE_HS_EALREADY);
}

TEST_CASE(ble_gap_test_case_conn_dir_bad_addr)
{
    struct hci_le_conn_complete evt;
    int rc;

    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_misc_init();

    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    rc = ble_gap_conn_direct_connect(BLE_ADDR_TYPE_PUBLIC, peer_addr,
                                     ble_gap_test_misc_connect_cb, NULL);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(ble_gap_conn_master_in_progress());

    /* Verify tx of create connection command. */
    ble_hci_sched_wakeup();
    ble_gap_test_misc_verify_tx_create_conn(BLE_HCI_CONN_FILT_NO_WL);
    ble_hs_test_util_rx_le_ack(BLE_HCI_OCF_LE_CREATE_CONN, 0);

    TEST_ASSERT(ble_gap_conn_master_in_progress());
    TEST_ASSERT(ble_hs_conn_find(2) == NULL);

    /* Receive connection complete event. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    memcpy(evt.peer_addr, ((uint8_t[]){1,1,1,1,1,1}), 6);
    rc = ble_gap_conn_rx_conn_complete(&evt);
    TEST_ASSERT(rc == BLE_HS_ECONTROLLER);

    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CONN);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == BLE_HS_CONN_HANDLE_NONE);

    TEST_ASSERT(ble_hs_conn_find(2) == NULL);
}

static void
ble_gap_test_misc_conn_cancel(uint8_t *peer_addr, int cmd_fail_idx,
                              uint8_t hci_status)
{
    struct hci_le_conn_complete evt;
    int cmd_idx;
    int rc;

    ble_gap_test_misc_init();
    cmd_idx = 0;

    /* Begin creating a connection. */
    rc = ble_gap_conn_direct_connect(BLE_ADDR_TYPE_PUBLIC, peer_addr,
                                     ble_gap_test_misc_connect_cb, NULL);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_conn_master_in_progress());

    ble_hci_sched_wakeup();
    ble_gap_test_misc_verify_tx_create_conn(BLE_HCI_CONN_FILT_NO_WL);
    ble_hs_test_util_rx_le_ack(BLE_HCI_OCF_LE_CREATE_CONN, 0);

    /* Initiate cancel procedure. */
    rc = ble_gap_conn_cancel();
    TEST_ASSERT(rc == 0);

    /* Verify tx of cancel create connection command. */
    ble_hci_sched_wakeup();
    ble_gap_test_misc_verify_tx_create_conn_cancel();
    rc = ble_gap_test_misc_rx_hci_ack(&cmd_idx, cmd_fail_idx, BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_CREATE_CONN_CANCEL,
                                      hci_status);
    if (rc != 0) {
        return;
    }
    TEST_ASSERT(ble_gap_conn_master_in_progress());

    TEST_ASSERT_FATAL(cmd_fail_idx == -1);

    /* Receive connection complete event. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_UNK_CONN_ID;
    evt.connection_handle = 2;
    memcpy(evt.peer_addr, peer_addr, 6);
    rc = ble_gap_conn_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(!ble_gap_conn_master_in_progress());
    TEST_ASSERT(ble_hs_conn_find(2) == NULL);
}

TEST_CASE(ble_gap_test_case_conn_cancel_bad_args)
{
    int rc;

    ble_gap_test_misc_init();

    /* Initiate cancel procedure with no connection in progress. */
    TEST_ASSERT(!ble_gap_conn_master_in_progress());
    rc = ble_gap_conn_cancel();
    TEST_ASSERT(rc == BLE_HS_ENOENT);
}

TEST_CASE(ble_gap_test_case_conn_cancel_good)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_misc_conn_cancel(peer_addr, -1, 0);

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CONN);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == BLE_HS_CONN_HANDLE_NONE);
}

TEST_CASE(ble_gap_test_case_conn_cancel_ctlr_fail)
{
    struct hci_le_conn_complete evt;
    int rc;

    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_misc_conn_cancel(peer_addr, 0, BLE_ERR_REPEATED_ATTEMPTS);

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CANCEL_FAILURE);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == BLE_HS_CONN_HANDLE_NONE);

    /* Allow connection complete to succeed. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    memcpy(evt.peer_addr, peer_addr, 6);
    rc = ble_gap_conn_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CONN);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_addr,
                       peer_addr, 6) == 0);

    TEST_ASSERT(ble_hs_conn_find(2) != NULL);
}

TEST_CASE(ble_gap_test_case_conn_terminate_bad_args)
{
    int rc;

    ble_gap_test_misc_init();

    /*** Nonexistent connection. */
    rc = ble_gap_conn_terminate(2);
    TEST_ASSERT(rc == BLE_HS_ENOENT);
}

TEST_CASE(ble_gap_test_case_conn_terminate_good)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_misc_terminate(peer_addr, -1, 0);

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_TERM);
    TEST_ASSERT(ble_gap_test_conn_status == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(ble_gap_test_conn_desc.peer_addr_type == BLE_ADDR_TYPE_PUBLIC);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_addr, peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_arg == NULL);

    TEST_ASSERT(ble_hs_conn_find(2) == NULL);
    TEST_ASSERT(!ble_gap_conn_master_in_progress());
}

TEST_CASE(ble_gap_test_case_conn_terminate_ctlr_fail)
{
    struct hci_disconn_complete evt;
    int rc;

    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_misc_init();

    /* Create a connection. */
    ble_hs_test_util_create_conn(2, peer_addr, ble_gap_test_misc_connect_cb,
                                 NULL);

    /* Terminate the connection. */
    rc = ble_gap_conn_terminate(2);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(!ble_gap_conn_master_in_progress());

    /* Verify tx of disconnect command. */
    ble_hci_sched_wakeup();
    ble_gap_test_misc_verify_tx_disconnect();
    ble_hs_test_util_rx_ack((BLE_HCI_OGF_LINK_CTRL << 10) |
                            BLE_HCI_OCF_DISCONNECT_CMD, 0);

    /* Receive failed disconnection complete event. */
    evt.connection_handle = 2;
    evt.status = BLE_ERR_UNSUPPORTED;
    evt.reason = 0;
    ble_gap_conn_rx_disconn_complete(&evt);

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_TERM);
    TEST_ASSERT(ble_gap_test_conn_status ==
                BLE_HS_HCI_ERR(BLE_ERR_UNSUPPORTED));
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(ble_gap_test_conn_desc.peer_addr_type == BLE_ADDR_TYPE_PUBLIC);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_addr, peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_arg == NULL);

    TEST_ASSERT(ble_hs_conn_find(2) != NULL);
    TEST_ASSERT(!ble_gap_conn_master_in_progress());
}

TEST_CASE(ble_gap_test_case_conn_terminate_hci_fail)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_misc_terminate(peer_addr, 0, BLE_ERR_REPEATED_ATTEMPTS);

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_TERM);
    TEST_ASSERT(ble_gap_test_conn_status ==
                BLE_HS_HCI_ERR(BLE_ERR_REPEATED_ATTEMPTS));
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(ble_gap_test_conn_desc.peer_addr_type == BLE_ADDR_TYPE_PUBLIC);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_addr, peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_arg == NULL);

    TEST_ASSERT(ble_hs_conn_find(2) != NULL);
    TEST_ASSERT(!ble_gap_conn_master_in_progress());
}

TEST_SUITE(ble_gap_suite_conn_wl)
{
    ble_gap_test_case_conn_wl_good();
    ble_gap_test_case_conn_wl_bad_args();
    ble_gap_test_case_conn_wl_ctlr_fail();
}

TEST_SUITE(ble_gap_suite_conn_dir)
{
    ble_gap_test_case_conn_dir_good();
    ble_gap_test_case_conn_dir_bad_args();
    ble_gap_test_case_conn_dir_bad_addr();
}

TEST_SUITE(ble_gap_suite_conn_cancel)
{
    ble_gap_test_case_conn_cancel_good();
    ble_gap_test_case_conn_cancel_bad_args();
    ble_gap_test_case_conn_cancel_ctlr_fail();
}

TEST_SUITE(ble_gap_suite_conn_terminate)
{
    ble_gap_test_case_conn_terminate_bad_args();
    ble_gap_test_case_conn_terminate_good();
    ble_gap_test_case_conn_terminate_ctlr_fail();
    ble_gap_test_case_conn_terminate_hci_fail();
}

int
ble_gap_test_all(void)
{
    ble_gap_suite_conn_wl();
    ble_gap_suite_conn_dir();
    ble_gap_suite_conn_cancel();
    ble_gap_suite_conn_terminate();

    return tu_any_failed;
}
