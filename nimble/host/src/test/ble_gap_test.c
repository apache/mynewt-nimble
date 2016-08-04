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
#include <errno.h>
#include "testutil/testutil.h"
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_test.h"
#include "ble_hs_test_util.h"

static int ble_gap_test_conn_event_type;
static int ble_gap_test_conn_status;
static struct ble_gap_conn_desc ble_gap_test_conn_desc;
static void *ble_gap_test_conn_arg;
static struct ble_gap_upd_params ble_gap_test_conn_peer_params;
static struct ble_gap_upd_params ble_gap_test_conn_self_params;

static int ble_gap_test_disc_event_type;
static struct ble_gap_disc_desc ble_gap_test_disc_desc;
static void *ble_gap_test_disc_arg;

/*****************************************************************************
 * $misc                                                                     *
 *****************************************************************************/

static int
ble_gap_test_util_update_in_progress(uint16_t conn_handle)
{
    ble_hs_conn_flags_t conn_flags;
    int rc;

    rc = ble_hs_atomic_conn_flags(conn_handle, &conn_flags);
    return rc == 0 && conn_flags & BLE_HS_CONN_F_UPDATE;
}

static void
ble_gap_test_util_reset_cb_info(void)
{
    ble_gap_test_conn_event_type = -1;
    ble_gap_test_conn_status = -1;
    memset(&ble_gap_test_conn_desc, 0xff, sizeof ble_gap_test_conn_desc);
    ble_gap_test_conn_arg = (void *)-1;

    ble_gap_test_disc_event_type = -1;
    memset(&ble_gap_test_disc_desc, 0xff, sizeof ble_gap_test_disc_desc);
    ble_gap_test_disc_arg = (void *)-1;
}

static void
ble_gap_test_util_init(void)
{
    ble_hs_test_util_init();
    ble_hs_test_util_set_static_rnd_addr();
    ble_gap_test_util_reset_cb_info();
}

static int
ble_gap_test_util_disc_cb(struct ble_gap_event *event, void *arg)
{
    ble_gap_test_disc_event_type = event->type;
    ble_gap_test_disc_arg = arg;

    if (event->type == BLE_GAP_EVENT_DISC) {
        ble_gap_test_disc_desc = event->disc;
    }

    return 0;
}

static int
ble_gap_test_util_connect_cb(struct ble_gap_event *event, void *arg)
{
    int *fail_reason;

    ble_gap_test_conn_event_type = event->type;
    ble_gap_test_conn_arg = arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ble_gap_test_conn_status = event->connect.status;
        ble_gap_conn_find(event->connect.conn_handle, &ble_gap_test_conn_desc);
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ble_gap_test_conn_status = event->disconnect.reason;
        ble_gap_test_conn_desc = event->disconnect.conn;
        break;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ble_gap_test_conn_status = event->conn_update.status;
        ble_gap_conn_find(event->conn_update.conn_handle,
                          &ble_gap_test_conn_desc);
        break;

    case BLE_GAP_EVENT_CONN_CANCEL:
        break;

    case BLE_GAP_EVENT_TERM_FAILURE:
        ble_gap_test_conn_status = event->term_failure.status;
        ble_gap_conn_find(event->term_failure.conn_handle,
                          &ble_gap_test_conn_desc);
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ble_gap_test_conn_arg = arg;
        break;

    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        ble_gap_test_conn_peer_params = *event->conn_update_req.peer_params;
        *event->conn_update_req.self_params = ble_gap_test_conn_self_params;
        ble_gap_conn_find(event->conn_update_req.conn_handle,
                          &ble_gap_test_conn_desc);

        fail_reason = arg;
        if (fail_reason == NULL) {
            return 0;
        } else {
            return *fail_reason;
        }
        break;

    default:
        TEST_ASSERT_FATAL(0);
        break;
    }

    return 0;
}

static void
ble_gap_test_util_verify_tx_clear_wl(void)
{
    uint8_t param_len;

    ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                   BLE_HCI_OCF_LE_CLEAR_WHITE_LIST,
                                   &param_len);
    TEST_ASSERT(param_len == 0);
}

static void
ble_gap_test_util_verify_tx_add_wl(struct ble_gap_white_entry *entry)
{
    uint8_t param_len;
    uint8_t *param;
    int i;

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
ble_gap_test_util_verify_tx_set_scan_params(uint8_t own_addr_type,
                                            uint8_t scan_type,
                                            uint16_t itvl,
                                            uint16_t scan_window,
                                            uint8_t filter_policy)
{
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_SET_SCAN_PARAMS,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_SET_SCAN_PARAM_LEN);
    TEST_ASSERT(param[0] == scan_type);
    TEST_ASSERT(le16toh(param + 1) == itvl);
    TEST_ASSERT(le16toh(param + 3) == scan_window);
    TEST_ASSERT(param[5] == own_addr_type);
    TEST_ASSERT(param[6] == filter_policy);
}

static void
ble_gap_test_util_verify_tx_scan_enable(uint8_t enable,
                                        uint8_t filter_duplicates)
{
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_SET_SCAN_ENABLE,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_SET_SCAN_ENABLE_LEN);
    TEST_ASSERT(param[0] == enable);
    TEST_ASSERT(param[1] == filter_duplicates);
}

static void
ble_hs_test_util_verify_tx_create_conn_cancel(void)
{
    uint8_t param_len;

    ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                   BLE_HCI_OCF_LE_CREATE_CONN_CANCEL,
                                   &param_len);
    TEST_ASSERT(param_len == 0);
}

static void
ble_gap_test_util_verify_tx_disconnect(void)
{
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LINK_CTRL,
                                           BLE_HCI_OCF_DISCONNECT_CMD,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_DISCONNECT_CMD_LEN);
    TEST_ASSERT(le16toh(param + 0) == 2);
    TEST_ASSERT(param[2] == BLE_ERR_REM_USER_CONN_TERM);
}

static void
ble_gap_test_util_verify_tx_adv_params(void)
{
    uint8_t param_len;

    ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                   BLE_HCI_OCF_LE_SET_ADV_PARAMS,
                                   &param_len);
    TEST_ASSERT(param_len == BLE_HCI_SET_ADV_PARAM_LEN);

    /* Note: Content of message verified in ble_hs_adv_test.c. */
}

static void
ble_gap_test_util_verify_tx_adv_data(void)
{
    uint8_t param_len;

    ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                   BLE_HCI_OCF_LE_SET_ADV_DATA,
                                   &param_len);
    /* Note: Content of message verified in ble_hs_adv_test.c. */
}

static void
ble_gap_test_util_verify_tx_rsp_data(void)
{
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_SET_SCAN_RSP_DATA,
                                           &param_len);
    (void)param; /* XXX: Verify other fields. */
}

static void
ble_gap_test_util_verify_tx_adv_enable(int enabled)
{
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_SET_ADV_ENABLE,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_SET_ADV_ENABLE_LEN);
    TEST_ASSERT(param[0] == !!enabled);
}

static void
ble_gap_test_util_verify_tx_update_conn(struct ble_gap_upd_params *params)
{
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_CONN_UPDATE,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_CONN_UPDATE_LEN);
    TEST_ASSERT(le16toh(param + 0) == 2);
    TEST_ASSERT(le16toh(param + 2) == params->itvl_min);
    TEST_ASSERT(le16toh(param + 4) == params->itvl_max);
    TEST_ASSERT(le16toh(param + 6) == params->latency);
    TEST_ASSERT(le16toh(param + 8) == params->supervision_timeout);
    TEST_ASSERT(le16toh(param + 10) == params->min_ce_len);
    TEST_ASSERT(le16toh(param + 12) == params->max_ce_len);
}

static void
ble_gap_test_util_verify_tx_params_reply_pos(void)
{
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_REM_CONN_PARAM_RR,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_CONN_PARAM_REPLY_LEN);
    TEST_ASSERT(le16toh(param + 0) == 2);
    TEST_ASSERT(le16toh(param + 2) == ble_gap_test_conn_self_params.itvl_min);
    TEST_ASSERT(le16toh(param + 4) == ble_gap_test_conn_self_params.itvl_max);
    TEST_ASSERT(le16toh(param + 6) == ble_gap_test_conn_self_params.latency);
    TEST_ASSERT(le16toh(param + 8) ==
                ble_gap_test_conn_self_params.supervision_timeout);
    TEST_ASSERT(le16toh(param + 10) ==
                ble_gap_test_conn_self_params.min_ce_len);
    TEST_ASSERT(le16toh(param + 12) ==
                ble_gap_test_conn_self_params.max_ce_len);
}

static void
ble_gap_test_util_verify_tx_params_reply_neg(uint8_t reason)
{
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_REM_CONN_PARAM_NRR,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_CONN_PARAM_NEG_REPLY_LEN);
    TEST_ASSERT(le16toh(param + 0) == 2);
    TEST_ASSERT(param[2] == reason);
}

static void
ble_gap_test_util_rx_update_complete(
    uint8_t status,
    struct ble_gap_upd_params *params)
{
    struct hci_le_conn_upd_complete evt;

    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_UPD_COMPLETE;
    evt.status = status;
    evt.connection_handle = 2;
    evt.conn_itvl = params->itvl_max;
    evt.conn_latency = params->latency;
    evt.supervision_timeout = params->supervision_timeout;

    ble_gap_rx_update_complete(&evt);
}

static int
ble_gap_test_util_rx_param_req(struct ble_gap_upd_params *params, int pos,
                               int *cmd_idx, int cmd_fail_idx,
                               uint8_t fail_status)
{
    struct hci_le_conn_param_req evt;
    uint16_t opcode;
    uint8_t hci_status;

    evt.subevent_code = BLE_HCI_LE_SUBEV_REM_CONN_PARM_REQ;
    evt.connection_handle = 2;
    evt.itvl_min = params->itvl_min;
    evt.itvl_max = params->itvl_max;
    evt.latency = params->latency;
    evt.timeout = params->supervision_timeout;

    if (pos) {
        opcode = ble_hs_hci_util_opcode_join(
            BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_REM_CONN_PARAM_RR);
    } else {
        opcode = ble_hs_hci_util_opcode_join(
            BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_REM_CONN_PARAM_NRR);
    }
    if (*cmd_idx == cmd_fail_idx) {
        hci_status = fail_status;
    } else {
        hci_status = 0;
    }
    (*cmd_idx)++;

    ble_hs_test_util_set_ack(opcode, hci_status);
    ble_gap_rx_param_req(&evt);

    return hci_status;
}

/*****************************************************************************
 * $white list                                                               *
 *****************************************************************************/

static void
ble_gap_test_util_wl_set(struct ble_gap_white_entry *white_list,
                         int white_list_count, int cmd_fail_idx,
                         uint8_t fail_status)
{
    int cmd_idx;
    int rc;
    int i;

    ble_gap_test_util_init();
    cmd_idx = 0;

    rc = ble_hs_test_util_wl_set(white_list, white_list_count, cmd_fail_idx,
                                 fail_status);
    TEST_ASSERT(rc == BLE_HS_HCI_ERR(fail_status));

    /* Verify tx of clear white list command. */
    ble_gap_test_util_verify_tx_clear_wl();
    if (cmd_idx >= cmd_fail_idx) {
        return;
    }
    cmd_idx++;

    /* Verify tx of add white list commands. */
    for (i = 0; i < white_list_count; i++) {
        ble_gap_test_util_verify_tx_add_wl(white_list + i);
        if (cmd_idx >= cmd_fail_idx) {
            return;
        }
        cmd_idx++;
    }
}

TEST_CASE(ble_gap_test_case_wl_bad_args)
{
    int rc;

    ble_gap_test_util_init();

    /*** 0 white list entries. */
    rc = ble_hs_test_util_wl_set(NULL, 0, 0, 0);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    /*** Invalid address type. */
    rc = ble_hs_test_util_wl_set(
        ((struct ble_gap_white_entry[]) { {
            5, { 1, 2, 3, 4, 5, 6 }
        }, }),
        1, 0, 0);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    /*** White-list-using connection in progress. */
    rc = ble_hs_test_util_connect(BLE_ADDR_TYPE_PUBLIC,
                                  BLE_GAP_ADDR_TYPE_WL, NULL, 0, NULL,
                                  ble_gap_test_util_connect_cb, NULL, 0);
    TEST_ASSERT(rc == 0);

    rc = ble_hs_test_util_wl_set(
        ((struct ble_gap_white_entry[]) { {
            BLE_ADDR_TYPE_PUBLIC, { 1, 2, 3, 4, 5, 6 }
        }, }),
        1, 0, 0);
    TEST_ASSERT(rc == BLE_HS_EBUSY);
}

TEST_CASE(ble_gap_test_case_wl_ctlr_fail)
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
        ble_gap_test_util_wl_set(white_list, white_list_count, i,
                                 BLE_ERR_UNSPECIFIED);
    }
}

TEST_CASE(ble_gap_test_case_wl_good)
{
    struct ble_gap_white_entry white_list[] = {
        { BLE_ADDR_TYPE_PUBLIC, { 1, 2, 3, 4, 5, 6 } },
        { BLE_ADDR_TYPE_PUBLIC, { 2, 3, 4, 5, 6, 7 } },
        { BLE_ADDR_TYPE_PUBLIC, { 3, 4, 5, 6, 7, 8 } },
        { BLE_ADDR_TYPE_PUBLIC, { 4, 5, 6, 7, 8, 9 } },
    };
    int white_list_count = sizeof white_list / sizeof white_list[0];

    ble_gap_test_util_wl_set(white_list, white_list_count, 0, 0);
}

TEST_SUITE(ble_gap_test_suite_wl)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_gap_test_case_wl_good();
    ble_gap_test_case_wl_bad_args();
    ble_gap_test_case_wl_ctlr_fail();
}

/*****************************************************************************
 * $discovery                                                                *
 *****************************************************************************/

static int
ble_gap_test_util_disc(uint8_t own_addr_type,
                       const struct ble_gap_disc_params *disc_params,
                       struct ble_gap_disc_desc *desc, int cmd_fail_idx,
                       uint8_t fail_status)
{
    int rc;

    ble_gap_test_util_init();

    TEST_ASSERT(!ble_gap_disc_active());

    /* Begin the discovery procedure. */
    rc = ble_hs_test_util_disc(own_addr_type, BLE_HS_FOREVER, disc_params,
                               ble_gap_test_util_disc_cb, NULL, cmd_fail_idx,
                               fail_status);
    TEST_ASSERT(rc == BLE_HS_HCI_ERR(fail_status));
    if (rc == 0) {
        TEST_ASSERT(ble_gap_master_in_progress());
        ble_gap_rx_adv_report(desc);
    } else {
        TEST_ASSERT(ble_gap_test_disc_event_type == -1);
    }

    if (cmd_fail_idx > 0) {
        /* Verify tx of set scan parameters command. */
        ble_gap_test_util_verify_tx_set_scan_params(
            own_addr_type,
            disc_params->passive ?
                BLE_HCI_SCAN_TYPE_PASSIVE :
                BLE_HCI_SCAN_TYPE_ACTIVE,
            disc_params->itvl,
            disc_params->window,
            disc_params->filter_policy);
    }

    if (cmd_fail_idx > 1) {
        /* Verify tx of scan enable command. */
        ble_gap_test_util_verify_tx_scan_enable(
            1, disc_params->filter_duplicates);
    }

    if (rc == 0) {
        TEST_ASSERT(ble_gap_disc_active());
    }

    return rc;
}

TEST_CASE(ble_gap_test_case_disc_bad_args)
{
    struct ble_gap_disc_params params;
    int rc;

    params.itvl = 0;
    params.window = 0;
    params.filter_policy = BLE_HCI_SCAN_FILT_NO_WL;
    params.limited = 0;
    params.passive = 0;
    params.filter_duplicates = 0;

    ble_gap_test_util_init();

    /*** Invalid filter policy. */
    params.filter_policy = 6;
    rc = ble_gap_disc(BLE_ADDR_TYPE_PUBLIC, 0, &params,
                      ble_gap_test_util_disc_cb, NULL);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    /*** Master operation already in progress. */
    params.filter_policy = BLE_HCI_SCAN_FILT_NO_WL;
    rc = ble_hs_test_util_connect(BLE_ADDR_TYPE_PUBLIC,
                                  BLE_GAP_ADDR_TYPE_WL, NULL, 0, NULL,
                                  ble_gap_test_util_connect_cb, NULL, 0);
    rc = ble_gap_disc(BLE_ADDR_TYPE_PUBLIC, 0, &params,
                      ble_gap_test_util_disc_cb, NULL);
    TEST_ASSERT(rc == BLE_HS_EALREADY);
}

TEST_CASE(ble_gap_test_case_disc_good)
{
    uint8_t adv_data[32];
    uint8_t flags;
    uint8_t own_addr_type;
    int passive;
    int limited;
    int rc;

    struct ble_gap_disc_desc desc = {
        .event_type = BLE_HCI_ADV_TYPE_ADV_IND,
        .addr_type = BLE_ADDR_TYPE_PUBLIC,
        .length_data = 0,
        .rssi = 0,
        .addr = { 1, 2, 3, 4, 5, 6 },
        .data = adv_data,
    };
    struct ble_gap_disc_params disc_params = {
        .itvl = BLE_GAP_SCAN_SLOW_INTERVAL1,
        .window = BLE_GAP_SCAN_SLOW_WINDOW1,
        .filter_policy = BLE_HCI_CONN_FILT_NO_WL,
        .limited = 0,
        .passive = 0,
        .filter_duplicates = 0,
    };

    flags = BLE_HS_ADV_F_DISC_LTD;
    rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_FLAGS, 1, &flags,
                             desc.data, &desc.length_data,
                             sizeof adv_data);
    TEST_ASSERT_FATAL(rc == 0);

    for (own_addr_type = 0;
         own_addr_type <= BLE_ADDR_TYPE_RPA_RND_DEFAULT;
         own_addr_type++)
    for (passive = 0; passive <= 1; passive++)
    for (limited = 0; limited <= 1; limited++) {
        disc_params.passive = passive;
        disc_params.limited = limited;
        ble_gap_test_util_disc(own_addr_type, &disc_params, &desc, -1, 0);

        TEST_ASSERT(ble_gap_master_in_progress());
        TEST_ASSERT(ble_gap_test_disc_event_type == BLE_GAP_EVENT_DISC);
        TEST_ASSERT(ble_gap_test_disc_desc.event_type ==
                    BLE_HCI_ADV_TYPE_ADV_IND);
        TEST_ASSERT(ble_gap_test_disc_desc.addr_type ==
                    BLE_ADDR_TYPE_PUBLIC);
        TEST_ASSERT(ble_gap_test_disc_desc.length_data == 3);
        TEST_ASSERT(ble_gap_test_disc_desc.rssi == 0);
        TEST_ASSERT(memcmp(ble_gap_test_disc_desc.addr, desc.addr, 6) == 0);
        TEST_ASSERT(ble_gap_test_disc_arg == NULL);

    }
}

TEST_CASE(ble_gap_test_case_disc_ltd_mismatch)
{
    int rc;
    struct ble_gap_disc_desc desc = {
        .event_type = BLE_HCI_ADV_TYPE_ADV_IND,
        .addr_type = BLE_ADDR_TYPE_PUBLIC,
        .length_data = 0,
        .rssi = 0,
        .addr = { 1, 2, 3, 4, 5, 6 },
        .data = (uint8_t[BLE_HCI_MAX_ADV_DATA_LEN]){
            2, 
            BLE_HS_ADV_TYPE_FLAGS,
            BLE_HS_ADV_F_DISC_GEN,
        },
    };
    struct ble_gap_disc_params disc_params = {
        .itvl = BLE_GAP_SCAN_SLOW_INTERVAL1,
        .window = BLE_GAP_SCAN_SLOW_WINDOW1,
        .filter_policy = BLE_HCI_CONN_FILT_NO_WL,
        .limited = 1,
        .passive = 0,
        .filter_duplicates = 0,
    };

    rc = ble_gap_test_util_disc(BLE_ADDR_TYPE_PUBLIC, &disc_params, &desc,
                                -1, 0);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_master_in_progress());

    /* Verify that the report was ignored because of a mismatched LTD flag. */
    TEST_ASSERT(ble_gap_test_disc_event_type == -1);

    /* Stop the scan and swap the flags. */
    rc = ble_hs_test_util_disc_cancel(0);
    TEST_ASSERT(rc == 0);

    desc.data[2] = BLE_HS_ADV_F_DISC_LTD;
    disc_params.limited = 0;
    rc = ble_gap_test_util_disc(BLE_ADDR_TYPE_PUBLIC, &disc_params, &desc,
                                -1, 0);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_master_in_progress());

    /* This time we should have reported the advertisement; general discovery
     * hears everything.
     */
    TEST_ASSERT(ble_gap_test_disc_event_type == BLE_GAP_EVENT_DISC);

}

TEST_CASE(ble_gap_test_case_disc_hci_fail)
{
    int fail_idx;
    int limited;
    int rc;

    struct ble_gap_disc_desc desc = {
        .event_type = BLE_HCI_ADV_TYPE_ADV_IND,
        .addr_type = BLE_ADDR_TYPE_PUBLIC,
        .length_data = 0,
        .rssi = 0,
        .addr = { 1, 2, 3, 4, 5, 6 },
        .data = NULL,
    };
    struct ble_gap_disc_params disc_params = {
        .itvl = BLE_GAP_SCAN_SLOW_INTERVAL1,
        .window = BLE_GAP_SCAN_SLOW_WINDOW1,
        .filter_policy = BLE_HCI_CONN_FILT_NO_WL,
        .limited = 0,
        .passive = 0,
        .filter_duplicates = 0,
    };

    for (limited = 0; limited <= 1; limited++) {
        disc_params.limited = limited;

        for (fail_idx = 0; fail_idx < 2; fail_idx++) {
            rc = ble_gap_test_util_disc(BLE_ADDR_TYPE_PUBLIC, &disc_params,
                                        &desc, fail_idx, BLE_ERR_UNSUPPORTED);
            TEST_ASSERT(rc == BLE_HS_HCI_ERR(BLE_ERR_UNSUPPORTED));
            TEST_ASSERT(!ble_gap_master_in_progress());
        }
    }
}

static void
ble_gap_test_util_disc_dflts_once(int limited)
{
    struct ble_gap_disc_params params;
    uint16_t exp_window;
    uint16_t exp_itvl;
    int rc;

    ble_gap_test_util_init();

    memset(&params, 0, sizeof params);
    params.limited = limited;

    rc = ble_hs_test_util_disc(BLE_ADDR_TYPE_PUBLIC, 0, &params,
                               ble_gap_test_util_disc_cb, NULL, -1, 0);
    TEST_ASSERT_FATAL(rc == 0);

    if (limited) {
        exp_itvl = BLE_GAP_LIM_DISC_SCAN_INT;
        exp_window = BLE_GAP_LIM_DISC_SCAN_WINDOW;
    } else {
        exp_itvl = BLE_GAP_SCAN_FAST_INTERVAL_MIN;
        exp_window = BLE_GAP_SCAN_FAST_WINDOW;
    }
    ble_gap_test_util_verify_tx_set_scan_params(
        BLE_ADDR_TYPE_PUBLIC,
        BLE_HCI_SCAN_TYPE_ACTIVE,
        exp_itvl,
        exp_window,
        BLE_HCI_SCAN_FILT_NO_WL);

    ble_gap_test_util_verify_tx_scan_enable(1, 0);
}

TEST_CASE(ble_gap_test_case_disc_dflts)
{
    ble_gap_test_util_disc_dflts_once(0);
    ble_gap_test_util_disc_dflts_once(1);
}

TEST_SUITE(ble_gap_test_suite_disc)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_gap_test_case_disc_bad_args();
    ble_gap_test_case_disc_good();
    ble_gap_test_case_disc_ltd_mismatch();
    ble_gap_test_case_disc_hci_fail();
    ble_gap_test_case_disc_dflts();
}

/*****************************************************************************
 * $direct connect                                                           *
 *****************************************************************************/

TEST_CASE(ble_gap_test_case_conn_dir_good)
{
    struct hci_le_conn_complete evt;
    struct ble_gap_conn_params params;
    int rc;

    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_util_init();

    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(!ble_gap_conn_active());

    params.scan_itvl = 0x12;
    params.scan_window = 0x11;
    params.itvl_min = 25;
    params.itvl_max = 26;
    params.latency = 1;
    params.supervision_timeout = 20;
    params.min_ce_len = 3;
    params.max_ce_len = 4;

    rc = ble_hs_test_util_connect(BLE_ADDR_TYPE_PUBLIC,
                                  BLE_ADDR_TYPE_PUBLIC, peer_addr, 0, &params,
                                  ble_gap_test_util_connect_cb, NULL, 0);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(ble_gap_master_in_progress());
    TEST_ASSERT(ble_gap_conn_active());

    TEST_ASSERT(ble_gap_master_in_progress());
    TEST_ASSERT(ble_hs_atomic_conn_flags(2, NULL) == BLE_HS_ENOTCONN);

    /* Receive connection complete event. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    evt.role = BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER;
    memcpy(evt.peer_addr, peer_addr, 6);
    rc = ble_gap_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(!ble_gap_master_in_progress());

    TEST_ASSERT(ble_gap_test_conn_event_type == BLE_GAP_EVENT_CONNECT);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr,
                       peer_addr, 6) == 0);

    TEST_ASSERT(ble_hs_atomic_conn_flags(2, NULL) == 0);
}

TEST_CASE(ble_gap_test_case_conn_dir_bad_args)
{
    int rc;

    ble_gap_test_util_init();

    TEST_ASSERT(!ble_gap_master_in_progress());

    /*** Invalid address type. */
    rc = ble_gap_connect(BLE_ADDR_TYPE_PUBLIC, 5,
                         ((uint8_t[]){ 1, 2, 3, 4, 5, 6 }), 0, NULL,
                         ble_gap_test_util_connect_cb, NULL);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
    TEST_ASSERT(!ble_gap_master_in_progress());

    /*** Connection already in progress. */
    rc = ble_hs_test_util_connect(BLE_ADDR_TYPE_PUBLIC,
                                  BLE_ADDR_TYPE_PUBLIC,
                                  ((uint8_t[]){ 1, 2, 3, 4, 5, 6 }), 0,
                                  NULL, ble_gap_test_util_connect_cb,
                                  NULL, 0);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_master_in_progress());

    rc = ble_gap_connect(BLE_ADDR_TYPE_PUBLIC, BLE_ADDR_TYPE_PUBLIC,
                         ((uint8_t[]){ 1, 2, 3, 4, 5, 6 }), 0, NULL,
                         ble_gap_test_util_connect_cb, NULL);
    TEST_ASSERT(rc == BLE_HS_EALREADY);
}

TEST_CASE(ble_gap_test_case_conn_dir_dflt_params)
{
    static const uint8_t peer_addr[6] = { 2, 3, 8, 6, 6, 1 };
    int rc;

    ble_gap_test_util_init();

    rc = ble_hs_test_util_connect(BLE_ADDR_TYPE_PUBLIC,
                                  BLE_ADDR_TYPE_PUBLIC, peer_addr, 0, NULL,
                                  ble_gap_test_util_connect_cb, NULL, 0);
    TEST_ASSERT(rc == 0);
}

TEST_SUITE(ble_gap_test_suite_conn_dir)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_gap_test_case_conn_dir_good();
    ble_gap_test_case_conn_dir_bad_args();
    ble_gap_test_case_conn_dir_dflt_params();
}

/*****************************************************************************
 * $cancel                                                                   *
 *****************************************************************************/

static void
ble_gap_test_util_conn_cancel(uint8_t hci_status)
{
    struct hci_le_conn_complete evt;
    int rc;

    /* Initiate cancel procedure. */
    rc = ble_hs_test_util_conn_cancel(hci_status);
    TEST_ASSERT(rc == BLE_HS_HCI_ERR(hci_status));

    /* Verify tx of cancel create connection command. */
    ble_hs_test_util_verify_tx_create_conn_cancel();
    if (rc != 0) {
        return;
    }
    TEST_ASSERT(ble_gap_master_in_progress());

    /* Receive connection complete event. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_UNK_CONN_ID;
    rc = ble_gap_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(!ble_gap_master_in_progress());

    TEST_ASSERT(ble_gap_test_conn_event_type == BLE_GAP_EVENT_CONN_CANCEL);
}

static void
ble_gap_test_util_conn_and_cancel(uint8_t *peer_addr, uint8_t hci_status)
{
    int rc;

    ble_gap_test_util_init();

    /* Begin creating a connection. */
    rc = ble_hs_test_util_connect(BLE_ADDR_TYPE_PUBLIC,
                                  BLE_ADDR_TYPE_PUBLIC, peer_addr, 0, NULL,
                                  ble_gap_test_util_connect_cb, NULL, 0);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_master_in_progress());

    /* Initiate cancel procedure. */
    ble_gap_test_util_conn_cancel(hci_status);
    TEST_ASSERT(ble_hs_atomic_conn_flags(2, NULL) == BLE_HS_ENOTCONN);
}

TEST_CASE(ble_gap_test_case_conn_cancel_bad_args)
{
    int rc;

    ble_gap_test_util_init();

    /* Initiate cancel procedure with no connection in progress. */
    TEST_ASSERT(!ble_gap_master_in_progress());
    rc = ble_hs_test_util_conn_cancel(0);
    TEST_ASSERT(rc == BLE_HS_EALREADY);
}

TEST_CASE(ble_gap_test_case_conn_cancel_good)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_util_conn_and_cancel(peer_addr, 0);

    TEST_ASSERT(ble_gap_test_conn_event_type == BLE_GAP_EVENT_CONN_CANCEL);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == BLE_HS_CONN_HANDLE_NONE);
}

TEST_CASE(ble_gap_test_case_conn_cancel_ctlr_fail)
{
    struct hci_le_conn_complete evt;
    int rc;

    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_util_conn_and_cancel(peer_addr, BLE_ERR_REPEATED_ATTEMPTS);

    /* Make sure the host didn't invoke the application callback.  The cancel
     * failure was indicated via the return code from the gap call.
     */
    TEST_ASSERT(ble_gap_test_conn_event_type == -1);

    /* Allow connection complete to succeed. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    evt.role = BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER;
    memcpy(evt.peer_addr, peer_addr, 6);
    rc = ble_gap_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(!ble_gap_master_in_progress());

    TEST_ASSERT(ble_gap_test_conn_event_type == BLE_GAP_EVENT_CONNECT);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr,
                       peer_addr, 6) == 0);

    TEST_ASSERT(ble_hs_atomic_conn_flags(2, NULL) == 0);
}

TEST_SUITE(ble_gap_test_suite_conn_cancel)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_gap_test_case_conn_cancel_good();
    ble_gap_test_case_conn_cancel_bad_args();
    ble_gap_test_case_conn_cancel_ctlr_fail();
}

/*****************************************************************************
 * $terminate                                                                *
 *****************************************************************************/

static void
ble_gap_test_util_terminate(uint8_t *peer_addr, uint8_t hci_status)
{
    struct hci_disconn_complete evt;
    int rc;

    ble_gap_test_util_init();

    /* Create a connection. */
    ble_hs_test_util_create_conn(2, peer_addr, ble_gap_test_util_connect_cb,
                                 NULL);

    /* Reset the callback event code; we don't care about the successful
     * connection in this test.
     */
    ble_gap_test_conn_event_type = -1;

    /* Terminate the connection. */
    rc = ble_hs_test_util_conn_terminate(2, hci_status);
    TEST_ASSERT(rc == BLE_HS_HCI_ERR(hci_status));
    TEST_ASSERT(!ble_gap_master_in_progress());

    /* Verify tx of disconnect command. */
    ble_gap_test_util_verify_tx_disconnect();

    if (hci_status == 0) {
        /* Receive disconnection complete event. */
        evt.connection_handle = 2;
        evt.status = 0;
        evt.reason = BLE_ERR_CONN_TERM_LOCAL;
        ble_gap_rx_disconn_complete(&evt);
    }
}

TEST_CASE(ble_gap_test_case_conn_terminate_bad_args)
{
    int rc;

    ble_gap_test_util_init();

    /*** Nonexistent connection. */
    rc = ble_hs_test_util_conn_terminate(2, 0);
    TEST_ASSERT(rc == BLE_HS_ENOTCONN);
}

TEST_CASE(ble_gap_test_case_conn_terminate_good)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_util_terminate(peer_addr, 0);

    TEST_ASSERT(ble_gap_test_conn_event_type == BLE_GAP_EVENT_DISCONNECT);
    TEST_ASSERT(ble_gap_test_conn_status ==
                BLE_HS_HCI_ERR(BLE_ERR_CONN_TERM_LOCAL));
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(ble_gap_test_conn_desc.peer_id_addr_type ==
                BLE_ADDR_TYPE_PUBLIC);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr,
                       peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_arg == NULL);

    TEST_ASSERT(ble_hs_atomic_conn_flags(2, NULL) == BLE_HS_ENOTCONN);
    TEST_ASSERT(!ble_gap_master_in_progress());
}

TEST_CASE(ble_gap_test_case_conn_terminate_ctlr_fail)
{
    struct hci_disconn_complete evt;
    int rc;

    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_util_init();

    /* Create a connection. */
    ble_hs_test_util_create_conn(2, peer_addr, ble_gap_test_util_connect_cb,
                                 NULL);

    /* Terminate the connection. */
    rc = ble_hs_test_util_conn_terminate(2, 0);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(!ble_gap_master_in_progress());

    /* Verify tx of disconnect command. */
    ble_gap_test_util_verify_tx_disconnect();

    /* Receive failed disconnection complete event. */
    evt.connection_handle = 2;
    evt.status = BLE_ERR_UNSUPPORTED;
    evt.reason = 0;
    ble_gap_rx_disconn_complete(&evt);

    TEST_ASSERT(ble_gap_test_conn_event_type == BLE_GAP_EVENT_TERM_FAILURE);
    TEST_ASSERT(ble_gap_test_conn_status ==
                BLE_HS_HCI_ERR(BLE_ERR_UNSUPPORTED));
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(ble_gap_test_conn_desc.peer_id_addr_type ==
                BLE_ADDR_TYPE_PUBLIC);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr,
                       peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_arg == NULL);

    TEST_ASSERT(ble_hs_atomic_conn_flags(2, NULL) == 0);
    TEST_ASSERT(!ble_gap_master_in_progress());
}

TEST_CASE(ble_gap_test_case_conn_terminate_hci_fail)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_util_terminate(peer_addr, BLE_ERR_REPEATED_ATTEMPTS);

    TEST_ASSERT(ble_gap_test_conn_event_type == -1);
    TEST_ASSERT(ble_hs_atomic_conn_flags(2, NULL) == 0);
    TEST_ASSERT(!ble_gap_master_in_progress());
}

TEST_SUITE(ble_gap_test_suite_conn_terminate)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_gap_test_case_conn_terminate_bad_args();
    ble_gap_test_case_conn_terminate_good();
    ble_gap_test_case_conn_terminate_ctlr_fail();
    ble_gap_test_case_conn_terminate_hci_fail();
}

/*****************************************************************************
 * $conn find                                                                *
 *****************************************************************************/

TEST_CASE(ble_gap_test_case_conn_find)
{

    struct ble_gap_conn_desc desc;
    struct ble_hs_conn *conn;
    uint8_t pub_addr[6];
    int rc;

    /*** We are master; public addresses. */
    ble_gap_test_util_init();

    ble_hs_test_util_create_rpa_conn(8,
                                     BLE_ADDR_TYPE_PUBLIC,
                                     ((uint8_t[6]){0,0,0,0,0,0}),
                                     BLE_ADDR_TYPE_PUBLIC,
                                     ((uint8_t[6]){2,3,4,5,6,7}),
                                     ((uint8_t[6]){0,0,0,0,0,0}),
                                     ble_gap_test_util_connect_cb,
                                     NULL);


    rc = ble_hs_id_copy_addr(BLE_ADDR_TYPE_PUBLIC, pub_addr, NULL);
    TEST_ASSERT_FATAL(rc == 0);

    rc = ble_gap_conn_find(8, &desc);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(desc.conn_handle == 8);
    TEST_ASSERT(desc.our_id_addr_type == BLE_ADDR_TYPE_PUBLIC);
    TEST_ASSERT(desc.our_ota_addr_type == BLE_ADDR_TYPE_PUBLIC);
    TEST_ASSERT(desc.peer_ota_addr_type == BLE_ADDR_TYPE_PUBLIC);
    TEST_ASSERT(desc.role == BLE_GAP_ROLE_MASTER);
    TEST_ASSERT(memcmp(desc.our_ota_addr, pub_addr, 6) == 0);
    TEST_ASSERT(memcmp(desc.our_id_addr, pub_addr, 6) == 0);
    TEST_ASSERT(memcmp(desc.peer_ota_addr,
                       ((uint8_t[6]){2,3,4,5,6,7}), 6) == 0);
    TEST_ASSERT(memcmp(desc.peer_id_addr,
                       ((uint8_t[6]){2,3,4,5,6,7}), 6) == 0);
    TEST_ASSERT(desc.conn_itvl == BLE_GAP_INITIAL_CONN_ITVL_MAX);
    TEST_ASSERT(desc.conn_latency == BLE_GAP_INITIAL_CONN_LATENCY);
    TEST_ASSERT(desc.supervision_timeout ==
                BLE_GAP_INITIAL_SUPERVISION_TIMEOUT);
    TEST_ASSERT(desc.master_clock_accuracy == 0);
    TEST_ASSERT(!desc.sec_state.encrypted);
    TEST_ASSERT(!desc.sec_state.authenticated);
    TEST_ASSERT(!desc.sec_state.bonded);

    /*** Swap roles. */
    ble_hs_lock();
    conn = ble_hs_conn_find(8);
    conn->bhc_flags &= ~BLE_HS_CONN_F_MASTER;
    ble_hs_unlock();

    rc = ble_gap_conn_find(8, &desc);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(desc.role == BLE_GAP_ROLE_SLAVE);

    /*** We are master; RPAs. */
    ble_gap_test_util_init();

    ble_hs_test_util_create_rpa_conn(54,
                                     BLE_ADDR_TYPE_RPA_PUB_DEFAULT,
                                     ((uint8_t[6]){0x40,1,2,3,4,5}),
                                     BLE_ADDR_TYPE_RPA_RND_DEFAULT,
                                     ((uint8_t[6]){3,4,5,6,7,8}),
                                     ((uint8_t[6]){0x50,1,2,3,4,5}),
                                     ble_gap_test_util_connect_cb,
                                     NULL);

    rc = ble_gap_conn_find(54, &desc);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(desc.conn_handle == 54);
    TEST_ASSERT(desc.our_id_addr_type == BLE_ADDR_TYPE_PUBLIC);
    TEST_ASSERT(desc.our_ota_addr_type == BLE_ADDR_TYPE_RPA_PUB_DEFAULT);
    TEST_ASSERT(desc.peer_ota_addr_type == BLE_ADDR_TYPE_RPA_RND_DEFAULT);
    TEST_ASSERT(desc.role == BLE_GAP_ROLE_MASTER);
    TEST_ASSERT(memcmp(desc.our_ota_addr,
                       ((uint8_t[6]){0x40,1,2,3,4,5}), 6) == 0);
    TEST_ASSERT(memcmp(desc.our_id_addr, pub_addr, 6) == 0);
    TEST_ASSERT(memcmp(desc.peer_ota_addr,
                       ((uint8_t[6]){0x50,1,2,3,4,5}), 6) == 0);
    TEST_ASSERT(memcmp(desc.peer_id_addr,
                       ((uint8_t[6]){3,4,5,6,7,8}), 6) == 0);
    TEST_ASSERT(desc.conn_itvl == BLE_GAP_INITIAL_CONN_ITVL_MAX);
    TEST_ASSERT(desc.conn_latency == BLE_GAP_INITIAL_CONN_LATENCY);
    TEST_ASSERT(desc.supervision_timeout ==
                BLE_GAP_INITIAL_SUPERVISION_TIMEOUT);
    TEST_ASSERT(desc.master_clock_accuracy == 0);
    TEST_ASSERT(!desc.sec_state.encrypted);
    TEST_ASSERT(!desc.sec_state.authenticated);
    TEST_ASSERT(!desc.sec_state.bonded);

    /*** Swap roles. */
    ble_hs_lock();
    conn = ble_hs_conn_find(54);
    conn->bhc_flags &= ~BLE_HS_CONN_F_MASTER;
    ble_hs_unlock();

    rc = ble_gap_conn_find(54, &desc);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(desc.role == BLE_GAP_ROLE_SLAVE);
}

TEST_SUITE(ble_gap_test_suite_conn_find)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_gap_test_case_conn_find();
}

/*****************************************************************************
 * $advertise                                                                *
 *****************************************************************************/

static void
ble_gap_test_util_adv(uint8_t own_addr_type, uint8_t peer_addr_type,
                      const uint8_t *peer_addr, uint8_t conn_mode,
                      uint8_t disc_mode, int connect_status,
                      int cmd_fail_idx, uint8_t fail_status)
{
    struct hci_le_conn_complete evt;
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields adv_fields;
    uint8_t hci_status;
    int cmd_idx;
    int rc;

    ble_gap_test_util_init();

    adv_params = ble_hs_test_util_adv_params;
    adv_params.conn_mode = conn_mode;
    adv_params.disc_mode = disc_mode;

    TEST_ASSERT(!ble_gap_adv_active());

    cmd_idx = 0;

    if (conn_mode != BLE_GAP_CONN_MODE_DIR) {
        memset(&adv_fields, 0, sizeof adv_fields);
        adv_fields.tx_pwr_lvl_is_present = 1;
        adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

        hci_status = ble_hs_test_util_exp_hci_status(cmd_idx, cmd_fail_idx,
                                                     fail_status);
        rc = ble_hs_test_util_adv_set_fields(&adv_fields, hci_status);

        if (adv_fields.tx_pwr_lvl_is_present &&
            adv_fields.tx_pwr_lvl == BLE_HS_ADV_TX_PWR_LVL_AUTO) {

            TEST_ASSERT_FATAL(rc == BLE_HS_HCI_ERR(hci_status));
            cmd_idx++;
        }
    }

    if (fail_status == 0 || cmd_fail_idx >= cmd_idx) {
        rc = ble_hs_test_util_adv_start(own_addr_type, peer_addr_type,
                                        peer_addr, &adv_params,
                                        ble_gap_test_util_connect_cb, NULL,
                                        cmd_fail_idx - cmd_idx, fail_status);

        TEST_ASSERT(rc == BLE_HS_HCI_ERR(fail_status));
    }

    if (fail_status == 0 || cmd_fail_idx >= cmd_idx) {
        /* Verify tx of set advertising params command. */
        ble_gap_test_util_verify_tx_adv_params();
    }
    cmd_idx++;

    if (conn_mode != BLE_GAP_CONN_MODE_DIR) {
        if (fail_status == 0 || cmd_fail_idx >= cmd_idx) {
            /* Verify tx of set advertise data command. */
            ble_gap_test_util_verify_tx_adv_data();
        }
        cmd_idx++;

        if (fail_status == 0 || cmd_fail_idx >= cmd_idx) {
            /* Verify tx of set scan response data command. */
            ble_gap_test_util_verify_tx_rsp_data();
        }
        cmd_idx++;
    }

    if (fail_status == 0 || cmd_fail_idx >= cmd_idx) {
        /* Verify tx of set advertise enable command. */
        ble_gap_test_util_verify_tx_adv_enable(1);
    }
    cmd_idx++;

    if (connect_status != -1 &&
        (fail_status == 0 || cmd_fail_idx >= cmd_idx)) {

        TEST_ASSERT(ble_gap_adv_active());

        /* Receive a connection complete event. */
        if (conn_mode != BLE_GAP_CONN_MODE_NON) {
            memset(&evt, 0, sizeof evt);
            evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
            evt.status = connect_status;
            evt.connection_handle = 2;
            evt.role = BLE_HCI_LE_CONN_COMPLETE_ROLE_SLAVE;
            memcpy(evt.peer_addr, peer_addr, 6);
            rc = ble_gap_rx_conn_complete(&evt);
            TEST_ASSERT(rc == 0);

            if (connect_status == 0 ||
                connect_status == BLE_ERR_DIR_ADV_TMO) {

                TEST_ASSERT(!ble_gap_adv_active());
            } else {
                TEST_ASSERT(ble_gap_adv_active());
            }
        }
    }
}

TEST_CASE(ble_gap_test_case_adv_bad_args)
{
    struct ble_gap_adv_params adv_params;
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };
    int rc;

    TEST_ASSERT(!ble_gap_adv_active());

    /*** Invalid discoverable mode. */
    adv_params = ble_hs_test_util_adv_params;
    adv_params.disc_mode = 43;
    rc = ble_hs_test_util_adv_start(BLE_ADDR_TYPE_PUBLIC, BLE_ADDR_TYPE_PUBLIC,
                                    peer_addr, &adv_params,
                                    ble_gap_test_util_connect_cb, NULL, 0, 0);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
    TEST_ASSERT(!ble_gap_adv_active());

    /*** Invalid connectable mode. */
    adv_params = ble_hs_test_util_adv_params;
    adv_params.conn_mode = 27;
    rc = ble_hs_test_util_adv_start(BLE_ADDR_TYPE_PUBLIC, BLE_ADDR_TYPE_PUBLIC,
                                    peer_addr, &adv_params,
                                    ble_gap_test_util_connect_cb, NULL, 0, 0);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
    TEST_ASSERT(!ble_gap_adv_active());

    /*** Invalid peer address type with directed advertisable mode. */
    adv_params = ble_hs_test_util_adv_params;
    adv_params.conn_mode = BLE_GAP_CONN_MODE_DIR;
    rc = ble_hs_test_util_adv_start(BLE_ADDR_TYPE_PUBLIC, 12,
                                    peer_addr, &adv_params,
                                    ble_gap_test_util_connect_cb, NULL, 0, 0);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
    TEST_ASSERT(!ble_gap_adv_active());

    /*** Advertising already in progress. */
    adv_params = ble_hs_test_util_adv_params;
    rc = ble_hs_test_util_adv_start(BLE_ADDR_TYPE_PUBLIC, BLE_ADDR_TYPE_PUBLIC,
                                    peer_addr, &adv_params,
                                    ble_gap_test_util_connect_cb, NULL, 0, 0);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_adv_active());

    rc = ble_hs_test_util_adv_start(BLE_ADDR_TYPE_PUBLIC, BLE_ADDR_TYPE_PUBLIC,
                                    peer_addr, &adv_params,
                                    ble_gap_test_util_connect_cb, NULL, 0, 0);
    TEST_ASSERT(rc == BLE_HS_EALREADY);
    TEST_ASSERT(ble_gap_adv_active());
}

static void
ble_gap_test_util_adv_verify_dflt_params(uint8_t own_addr_type,
                                         uint8_t peer_addr_type,
                                         const uint8_t *peer_addr,
                                         uint8_t conn_mode,
                                         uint8_t disc_mode)
{
    struct ble_gap_adv_params adv_params;
    struct hci_adv_params hci_cmd;
    uint8_t *hci_buf;
    uint8_t hci_param_len;
    int rc;

    ble_gap_test_util_init();

    TEST_ASSERT(!ble_gap_adv_active());

    adv_params = ble_hs_test_util_adv_params;
    adv_params.conn_mode = conn_mode;
    adv_params.disc_mode = disc_mode;

    /* Let stack calculate all default parameters. */
    adv_params.itvl_min = 0;
    adv_params.itvl_max = 0;
    adv_params.channel_map = 0;
    adv_params.filter_policy = 0;
    adv_params.high_duty_cycle = 0;

    rc = ble_hs_test_util_adv_start(BLE_ADDR_TYPE_PUBLIC, BLE_ADDR_TYPE_PUBLIC,
                                    peer_addr, &adv_params,
                                    ble_gap_test_util_connect_cb, NULL, 0, 0);
    TEST_ASSERT_FATAL(rc == 0);

    /* Ensure default parameters properly filled in. */
    hci_buf = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                             BLE_HCI_OCF_LE_SET_ADV_PARAMS,
                                             &hci_param_len);
    TEST_ASSERT_FATAL(hci_buf != NULL);
    TEST_ASSERT_FATAL(hci_param_len == BLE_HCI_SET_ADV_PARAM_LEN);

    hci_cmd.adv_itvl_min = le16toh(hci_buf + 0);
    hci_cmd.adv_itvl_max = le16toh(hci_buf + 2);
    hci_cmd.adv_type = hci_buf[4];
    hci_cmd.own_addr_type = hci_buf[5];
    hci_cmd.peer_addr_type = hci_buf[6];
    memcpy(hci_cmd.peer_addr, hci_buf + 7, 6);
    hci_cmd.adv_channel_map = hci_buf[13];
    hci_cmd.adv_filter_policy = hci_buf[14];

    if (conn_mode == BLE_GAP_CONN_MODE_NON) {
        TEST_ASSERT(hci_cmd.adv_itvl_min == BLE_GAP_ADV_FAST_INTERVAL2_MIN);
        TEST_ASSERT(hci_cmd.adv_itvl_max == BLE_GAP_ADV_FAST_INTERVAL2_MAX);
    } else {
        TEST_ASSERT(hci_cmd.adv_itvl_min == BLE_GAP_ADV_FAST_INTERVAL1_MIN);
        TEST_ASSERT(hci_cmd.adv_itvl_max == BLE_GAP_ADV_FAST_INTERVAL1_MAX);
    }

    if (conn_mode == BLE_GAP_CONN_MODE_NON) {
        if (disc_mode == BLE_GAP_DISC_MODE_NON) {
            TEST_ASSERT(hci_cmd.adv_type == BLE_HCI_ADV_TYPE_ADV_NONCONN_IND);
        } else {
            TEST_ASSERT(hci_cmd.adv_type == BLE_HCI_ADV_TYPE_ADV_SCAN_IND);
        }
    } else if (conn_mode == BLE_GAP_CONN_MODE_UND) {
        TEST_ASSERT(hci_cmd.adv_type == BLE_HCI_ADV_TYPE_ADV_IND);
    } else {
        TEST_ASSERT(hci_cmd.adv_type == BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_LD);
    }
}

TEST_CASE(ble_gap_test_case_adv_dflt_params)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    int d;
    int c;

    for (c = BLE_GAP_CONN_MODE_NON; c < BLE_GAP_CONN_MODE_MAX; c++) {
        for (d = BLE_GAP_DISC_MODE_NON; d < BLE_GAP_DISC_MODE_MAX; d++) {
            ble_gap_test_util_adv_verify_dflt_params(
                BLE_ADDR_TYPE_PUBLIC, BLE_ADDR_TYPE_PUBLIC, peer_addr, c, d);
        }
    }
}

TEST_CASE(ble_gap_test_case_adv_good)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };
    int d;
    int c;

    for (c = BLE_GAP_CONN_MODE_NON; c < BLE_GAP_CONN_MODE_MAX; c++) {
        for (d = BLE_GAP_DISC_MODE_NON; d < BLE_GAP_DISC_MODE_MAX; d++) {
            ble_gap_test_util_adv(BLE_ADDR_TYPE_PUBLIC, BLE_ADDR_TYPE_PUBLIC,
                                  peer_addr, c, d, BLE_ERR_SUCCESS, -1, 0);

            if (c != BLE_GAP_CONN_MODE_NON) {
                TEST_ASSERT(!ble_gap_adv_active());
                TEST_ASSERT(ble_gap_test_conn_event_type ==
                                BLE_GAP_EVENT_CONNECT);
                TEST_ASSERT(ble_gap_test_conn_status == 0);
                TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
                TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr,
                                   peer_addr, 6) == 0);
                TEST_ASSERT(ble_gap_test_conn_arg == NULL);
            }
        }
    }
}

TEST_CASE(ble_gap_test_case_adv_ctlr_fail)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };
    int d;
    int c;

    for (c = BLE_GAP_CONN_MODE_NON + 1; c < BLE_GAP_CONN_MODE_MAX; c++) {
        for (d = BLE_GAP_DISC_MODE_NON; d < BLE_GAP_DISC_MODE_MAX; d++) {
            ble_gap_test_util_adv(BLE_ADDR_TYPE_PUBLIC, BLE_ADDR_TYPE_PUBLIC,
                                  peer_addr, c, d, BLE_ERR_DIR_ADV_TMO, -1, 0);

            TEST_ASSERT(!ble_gap_adv_active());
            TEST_ASSERT(ble_gap_test_conn_event_type ==
                        BLE_GAP_EVENT_ADV_COMPLETE);
            TEST_ASSERT(ble_gap_test_conn_desc.conn_handle ==
                        BLE_HS_CONN_HANDLE_NONE);
            TEST_ASSERT(ble_gap_test_conn_arg == NULL);
        }
    }
}

TEST_CASE(ble_gap_test_case_adv_hci_fail)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };
    int num_hci_cmds;
    int fail_idx;
    int d;
    int c;

    for (c = BLE_GAP_CONN_MODE_NON; c < BLE_GAP_CONN_MODE_MAX; c++) {
        if (c == BLE_GAP_CONN_MODE_DIR) {
            num_hci_cmds = 2;
        } else {
            num_hci_cmds = 5;
        }

        for (d = BLE_GAP_DISC_MODE_NON; d < BLE_GAP_DISC_MODE_MAX; d++) {
            for (fail_idx = 0; fail_idx < num_hci_cmds; fail_idx++) {
                ble_gap_test_util_adv(BLE_ADDR_TYPE_PUBLIC,
                                      BLE_ADDR_TYPE_PUBLIC, peer_addr,
                                      c, d, 0, fail_idx, BLE_ERR_UNSUPPORTED);

                TEST_ASSERT(!ble_gap_adv_active());
                TEST_ASSERT(ble_gap_test_conn_event_type == -1);
            }
        }
    }
}

TEST_SUITE(ble_gap_test_suite_adv)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_gap_test_case_adv_bad_args();
    ble_gap_test_case_adv_dflt_params();
    ble_gap_test_case_adv_good();
    ble_gap_test_case_adv_ctlr_fail();
    ble_gap_test_case_adv_hci_fail();
}

/*****************************************************************************
 * $stop advertise                                                           *
 *****************************************************************************/

static void
ble_gap_test_util_stop_adv(uint8_t peer_addr_type, const uint8_t *peer_addr,
                           uint8_t conn_mode, uint8_t disc_mode,
                           int cmd_fail_idx, uint8_t fail_status)
{
    uint8_t hci_status;
    int rc;

    ble_gap_test_util_init();

    /* Start advertising; don't rx a successful connection event. */
    ble_gap_test_util_adv(BLE_ADDR_TYPE_PUBLIC, peer_addr_type, peer_addr,
                          conn_mode, disc_mode, -1, -1, 0);

    TEST_ASSERT(ble_gap_adv_active());

    /* Stop advertising. */
    hci_status = cmd_fail_idx == 0 ? fail_status : 0;

    rc = ble_hs_test_util_adv_stop(hci_status);
    TEST_ASSERT(rc == BLE_HS_HCI_ERR(hci_status));

    /* Verify tx of advertising enable command. */
    ble_gap_test_util_verify_tx_adv_enable(0);
}

TEST_CASE(ble_gap_test_case_stop_adv_good)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };
    int d;
    int c;

    for (c = BLE_GAP_CONN_MODE_NON; c < BLE_GAP_CONN_MODE_MAX; c++) {
        for (d = BLE_GAP_DISC_MODE_NON; d < BLE_GAP_DISC_MODE_MAX; d++) {
            ble_gap_test_util_stop_adv(BLE_ADDR_TYPE_PUBLIC, peer_addr, c, d,
                                       -1, 0);
            TEST_ASSERT(!ble_gap_adv_active());
            TEST_ASSERT(ble_gap_test_conn_event_type == -1);
            TEST_ASSERT(ble_gap_test_conn_status == -1);
            TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == (uint16_t)-1);
            TEST_ASSERT(ble_gap_test_conn_arg == (void *)-1);
        }
    }
}

TEST_CASE(ble_gap_test_case_stop_adv_hci_fail)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };
    int d;
    int c;

    for (c = BLE_GAP_CONN_MODE_NON; c < BLE_GAP_CONN_MODE_MAX; c++) {
        for (d = BLE_GAP_DISC_MODE_NON; d < BLE_GAP_DISC_MODE_MAX; d++) {
            ble_gap_test_util_stop_adv(BLE_ADDR_TYPE_PUBLIC, peer_addr, c, d,
                                       0, BLE_ERR_UNSUPPORTED);
            TEST_ASSERT(ble_gap_adv_active());
            TEST_ASSERT(ble_gap_test_conn_event_type == -1);
            TEST_ASSERT(ble_gap_test_conn_status == -1);
            TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == (uint16_t)-1);
            TEST_ASSERT(ble_gap_test_conn_arg == (void *)-1);
        }
    }
}

TEST_SUITE(ble_gap_test_suite_stop_adv)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_gap_test_case_stop_adv_good();
    ble_gap_test_case_stop_adv_hci_fail();
}

/*****************************************************************************
 * $update connection                                                        *
 *****************************************************************************/

static void
ble_gap_test_util_update(struct ble_gap_upd_params *params,
                         int cmd_fail_idx, uint8_t hci_status,
                         uint8_t event_status)
{
    int status;
    int rc;

    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_util_init();

    ble_hs_test_util_create_conn(2, peer_addr, ble_gap_test_util_connect_cb,
                                 NULL);

    TEST_ASSERT(!ble_gap_master_in_progress());

    rc = ble_hs_test_util_conn_update(2, params, hci_status);
    TEST_ASSERT(rc == BLE_HS_HCI_ERR(hci_status));
    TEST_ASSERT(!ble_gap_master_in_progress());

    /* Verify tx of connection update command. */
    ble_gap_test_util_verify_tx_update_conn(params);

    if (rc == 0) {
        TEST_ASSERT(ble_gap_test_util_update_in_progress(2));
    } else {
        TEST_ASSERT(!ble_gap_test_util_update_in_progress(2));
        return;
    }

    /* Receive connection update complete event. */
    ble_gap_test_util_rx_update_complete(event_status, params);

    if (event_status != 0) {
        status = BLE_HS_HCI_ERR(event_status);
        goto fail;
    }

    TEST_ASSERT(!ble_gap_master_in_progress());

    TEST_ASSERT(ble_gap_test_conn_event_type == BLE_GAP_EVENT_CONN_UPDATE);
    TEST_ASSERT(ble_gap_test_conn_status == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr,
                       peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_itvl == params->itvl_max);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_latency == params->latency);
    TEST_ASSERT(ble_gap_test_conn_desc.supervision_timeout ==
        params->supervision_timeout);

    TEST_ASSERT(!ble_gap_test_util_update_in_progress(2));

    return;

fail:
    TEST_ASSERT(ble_gap_test_conn_event_type == BLE_GAP_EVENT_CONN_UPDATE);
    TEST_ASSERT(ble_gap_test_conn_status == status);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr,
                       peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_itvl ==
                BLE_GAP_INITIAL_CONN_ITVL_MAX);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_latency ==
                BLE_GAP_INITIAL_CONN_LATENCY);
    TEST_ASSERT(ble_gap_test_conn_desc.supervision_timeout ==
                BLE_GAP_INITIAL_SUPERVISION_TIMEOUT);
    TEST_ASSERT(!ble_gap_test_util_update_in_progress(2));
}

static void
ble_gap_test_util_update_peer(uint8_t status,
                              struct ble_gap_upd_params *params)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_util_init();

    ble_hs_test_util_create_conn(2, peer_addr, ble_gap_test_util_connect_cb,
                                 NULL);

    TEST_ASSERT(!ble_gap_master_in_progress());

    /* Receive connection update complete event. */
    ble_gap_test_util_rx_update_complete(status, params);

    TEST_ASSERT(!ble_gap_master_in_progress());

    TEST_ASSERT(ble_gap_test_conn_event_type == BLE_GAP_EVENT_CONN_UPDATE);
    TEST_ASSERT(ble_gap_test_conn_status == BLE_HS_HCI_ERR(status));
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr,
                       peer_addr, 6) == 0);

    if (status == 0) {
        TEST_ASSERT(ble_gap_test_conn_desc.conn_itvl == params->itvl_max);
        TEST_ASSERT(ble_gap_test_conn_desc.conn_latency == params->latency);
        TEST_ASSERT(ble_gap_test_conn_desc.supervision_timeout ==
                    params->supervision_timeout);
    }

    TEST_ASSERT(!ble_gap_test_util_update_in_progress(2));
}

static void
ble_gap_test_util_update_req_pos(struct ble_gap_upd_params *peer_params,
                                 struct ble_gap_upd_params *self_params,
                                 int cmd_fail_idx, uint8_t hci_status)
{
    int cmd_idx;
    int rc;

    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_util_init();
    cmd_idx = 0;

    ble_hs_test_util_create_conn(2, peer_addr, ble_gap_test_util_connect_cb,
                                 NULL);

    TEST_ASSERT(!ble_gap_master_in_progress());

    ble_gap_test_conn_self_params = *self_params;
    rc = ble_gap_test_util_rx_param_req(peer_params, 1, &cmd_idx, cmd_fail_idx,
                                        hci_status);
    if (rc != 0) {
        goto hci_fail;
    }
    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(ble_gap_test_util_update_in_progress(2));

    /* Verify tx of connection parameters reply command. */
    ble_gap_test_util_verify_tx_params_reply_pos();

    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(ble_gap_test_util_update_in_progress(2));

    /* Receive connection update complete event. */
    ble_gap_test_util_rx_update_complete(0, self_params);

    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(!ble_gap_test_util_update_in_progress(2));

    TEST_ASSERT(ble_gap_test_conn_event_type == BLE_GAP_EVENT_CONN_UPDATE);
    TEST_ASSERT(ble_gap_test_conn_status == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr,
                       peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_itvl == self_params->itvl_max);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_latency == self_params->latency);
    TEST_ASSERT(ble_gap_test_conn_desc.supervision_timeout ==
                self_params->supervision_timeout);

    return;

hci_fail:
    TEST_ASSERT(ble_gap_test_conn_event_type == BLE_GAP_EVENT_CONN_UPDATE);
    TEST_ASSERT(ble_gap_test_conn_status == BLE_HS_HCI_ERR(hci_status));
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr,
                       peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_itvl ==
                BLE_GAP_INITIAL_CONN_ITVL_MAX);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_latency ==
                BLE_GAP_INITIAL_CONN_LATENCY);
    TEST_ASSERT(ble_gap_test_conn_desc.supervision_timeout ==
                BLE_GAP_INITIAL_SUPERVISION_TIMEOUT);
}

static void
ble_gap_test_util_update_req_neg(struct ble_gap_upd_params *peer_params,
                                 int cmd_fail_idx, uint8_t hci_status)
{
    int cmd_idx;
    int reason;
    int rc;

    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_util_init();
    cmd_idx = 0;

    reason = BLE_ERR_UNSPECIFIED;
    ble_hs_test_util_create_conn(2, peer_addr, ble_gap_test_util_connect_cb,
                                 &reason);

    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(!ble_gap_test_util_update_in_progress(2));

    rc = ble_gap_test_util_rx_param_req(peer_params, 0, &cmd_idx, cmd_fail_idx,
                                        hci_status);
    if (rc != 0) {
        goto hci_fail;
    }
    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(!ble_gap_test_util_update_in_progress(2));

    /* Verify tx of connection parameters negative reply command. */
    ble_gap_test_util_verify_tx_params_reply_neg(reason);

    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(!ble_gap_test_util_update_in_progress(2));

    return;

hci_fail:
    TEST_ASSERT(ble_gap_test_conn_event_type == BLE_GAP_EVENT_CONN_UPDATE);
    TEST_ASSERT(ble_gap_test_conn_status == BLE_HS_HCI_ERR(hci_status));
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr,
                       peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_itvl ==
                BLE_GAP_INITIAL_CONN_ITVL_MAX);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_latency ==
                BLE_GAP_INITIAL_CONN_LATENCY);
    TEST_ASSERT(ble_gap_test_conn_desc.supervision_timeout ==
                BLE_GAP_INITIAL_SUPERVISION_TIMEOUT);
}

static void
ble_gap_test_util_update_req_concurrent(
    struct ble_gap_upd_params *init_params,
    struct ble_gap_upd_params *peer_params,
    struct ble_gap_upd_params *self_params,
    int cmd_fail_idx,
    uint8_t fail_status)
{
    uint8_t hci_status;
    int cmd_idx;
    int rc;

    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_util_init();

    ble_hs_test_util_create_conn(2, peer_addr, ble_gap_test_util_connect_cb,
                                 NULL);

    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(!ble_gap_test_util_update_in_progress(2));

    hci_status = cmd_fail_idx == 0 ? fail_status : 0;
    rc = ble_hs_test_util_conn_update(2, init_params, hci_status);
    TEST_ASSERT(rc == BLE_HS_HCI_ERR(hci_status));

    TEST_ASSERT(!ble_gap_master_in_progress());

    /* Verify tx of connection update command. */
    ble_gap_test_util_verify_tx_update_conn(init_params);

    if (rc == 0) {
        TEST_ASSERT(ble_gap_test_util_update_in_progress(2));
    } else {
        TEST_ASSERT(!ble_gap_test_util_update_in_progress(2));
        return;
    }

    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(ble_gap_test_util_update_in_progress(2));

    /* Receive connection parameter update request from peer. */
    ble_gap_test_conn_self_params = *self_params;
    rc = ble_gap_test_util_rx_param_req(peer_params, 1, &cmd_idx, cmd_fail_idx,
                                        hci_status);
    if (rc != 0) {
        goto hci_fail;
    }
    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(ble_gap_test_util_update_in_progress(2));

    /* Verify tx of connection parameters reply command. */
    ble_gap_test_util_verify_tx_params_reply_pos();

    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(ble_gap_test_util_update_in_progress(2));

    /* Receive connection update complete event. */
    ble_gap_test_util_rx_update_complete(0, self_params);

    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(!ble_gap_test_util_update_in_progress(2));

    TEST_ASSERT(ble_gap_test_conn_event_type == BLE_GAP_EVENT_CONN_UPDATE);
    TEST_ASSERT(ble_gap_test_conn_status == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr,
                       peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_itvl == self_params->itvl_max);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_latency == self_params->latency);
    TEST_ASSERT(ble_gap_test_conn_desc.supervision_timeout ==
                self_params->supervision_timeout);

    return;

hci_fail:
    TEST_ASSERT(ble_gap_test_conn_event_type == BLE_GAP_EVENT_CONN_UPDATE);
    TEST_ASSERT(ble_gap_test_conn_status == BLE_HS_HCI_ERR(fail_status));
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr,
                       peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_itvl ==
                BLE_GAP_INITIAL_CONN_ITVL_MAX);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_latency ==
                BLE_GAP_INITIAL_CONN_LATENCY);
    TEST_ASSERT(ble_gap_test_conn_desc.supervision_timeout ==
                BLE_GAP_INITIAL_SUPERVISION_TIMEOUT);
}

TEST_CASE(ble_gap_test_case_update_conn_good)
{
    ble_gap_test_util_update(
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 10,
            .itvl_max = 100,
            .supervision_timeout = 0,
            .min_ce_len = 123,
            .max_ce_len = 456,
        }}),
        -1, 0, 0);

    ble_gap_test_util_update(
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 100,
            .itvl_max = 100,
            .supervision_timeout = 100,
            .min_ce_len = 554,
            .max_ce_len = 554,
        }}),
        -1, 0, 0);
}

TEST_CASE(ble_gap_test_case_update_conn_bad)
{
    ble_gap_test_util_update(
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 10,
            .itvl_max = 100,
            .supervision_timeout = 0,
            .min_ce_len = 123,
            .max_ce_len = 456,
        }}),
        -1, 0, BLE_ERR_LMP_COLLISION);
}

TEST_CASE(ble_gap_test_case_update_conn_hci_fail)
{
    ble_gap_test_util_update(
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 10,
            .itvl_max = 100,
            .supervision_timeout = 0,
            .min_ce_len = 123,
            .max_ce_len = 456,
        }}),
        0, BLE_ERR_UNSUPPORTED, 0);
}

TEST_CASE(ble_gap_test_case_update_peer_good)
{
    ble_gap_test_util_update_peer(0,
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 10,
            .itvl_max = 100,
            .supervision_timeout = 0,
            .min_ce_len = 123,
            .max_ce_len = 456,
        }}));

    ble_gap_test_util_update_peer(0,
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 100,
            .itvl_max = 100,
            .supervision_timeout = 100,
            .min_ce_len = 554,
            .max_ce_len = 554,
        }}));
}

TEST_CASE(ble_gap_test_case_update_req_good)
{
    ble_gap_test_util_update_req_pos(
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 50,
            .itvl_max = 500,
            .supervision_timeout = 20,
            .min_ce_len = 555,
            .max_ce_len = 888,
        }}),
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 10,
            .itvl_max = 100,
            .supervision_timeout = 0,
            .min_ce_len = 123,
            .max_ce_len = 456,
        }}),
        -1, 0);

    ble_gap_test_util_update_req_pos(
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 50,
            .itvl_max = 500,
            .supervision_timeout = 20,
            .min_ce_len = 555,
            .max_ce_len = 888,
        }}),
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 100,
            .itvl_max = 100,
            .supervision_timeout = 100,
            .min_ce_len = 554,
            .max_ce_len = 554,
        }}),
        -1, 0);

}

TEST_CASE(ble_gap_test_case_update_req_hci_fail)
{
    ble_gap_test_util_update_req_pos(
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 50,
            .itvl_max = 500,
            .supervision_timeout = 20,
            .min_ce_len = 555,
            .max_ce_len = 888,
        }}),
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 10,
            .itvl_max = 100,
            .supervision_timeout = 0,
            .min_ce_len = 123,
            .max_ce_len = 456,
        }}),
        0, BLE_ERR_UNSUPPORTED);
}

TEST_CASE(ble_gap_test_case_update_req_reject)
{
    ble_gap_test_util_update_req_neg(
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 50,
            .itvl_max = 500,
            .supervision_timeout = 20,
            .min_ce_len = 555,
            .max_ce_len = 888,
        }}),
        -1, 0);

    ble_gap_test_util_update_req_neg(
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 50,
            .itvl_max = 500,
            .supervision_timeout = 20,
            .min_ce_len = 555,
            .max_ce_len = 888,
        }}),
        -1, 0);
}

TEST_CASE(ble_gap_test_case_update_concurrent_good)
{
    ble_gap_test_util_update_req_concurrent(
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 10,
            .itvl_max = 100,
            .supervision_timeout = 0,
            .min_ce_len = 123,
            .max_ce_len = 456,
        }}),
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 50,
            .itvl_max = 500,
            .supervision_timeout = 20,
            .min_ce_len = 555,
            .max_ce_len = 888,
        }}),
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 10,
            .itvl_max = 100,
            .supervision_timeout = 0,
            .min_ce_len = 123,
            .max_ce_len = 456,
        }}),
        -1, 0);

    ble_gap_test_util_update_req_concurrent(
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 10,
            .itvl_max = 100,
            .supervision_timeout = 0,
            .min_ce_len = 123,
            .max_ce_len = 456,
        }}),
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 50,
            .itvl_max = 500,
            .supervision_timeout = 20,
            .min_ce_len = 555,
            .max_ce_len = 888,
        }}),
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 20,
            .itvl_max = 200,
            .supervision_timeout = 2,
            .min_ce_len = 111,
            .max_ce_len = 222,
        }}),
        -1, 0);
}

TEST_CASE(ble_gap_test_case_update_concurrent_hci_fail)
{
    ble_gap_test_util_update_req_concurrent(
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 10,
            .itvl_max = 100,
            .supervision_timeout = 0,
            .min_ce_len = 123,
            .max_ce_len = 456,
        }}),
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 50,
            .itvl_max = 500,
            .supervision_timeout = 20,
            .min_ce_len = 555,
            .max_ce_len = 888,
        }}),
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 20,
            .itvl_max = 200,
            .supervision_timeout = 2,
            .min_ce_len = 111,
            .max_ce_len = 222,
        }}),
        0, BLE_ERR_UNSUPPORTED);

    ble_gap_test_util_update_req_concurrent(
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 10,
            .itvl_max = 100,
            .supervision_timeout = 0,
            .min_ce_len = 123,
            .max_ce_len = 456,
        }}),
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 50,
            .itvl_max = 500,
            .supervision_timeout = 20,
            .min_ce_len = 555,
            .max_ce_len = 888,
        }}),
        ((struct ble_gap_upd_params[]) { {
            .itvl_min = 20,
            .itvl_max = 200,
            .supervision_timeout = 2,
            .min_ce_len = 111,
            .max_ce_len = 222,
        }}),
        1, BLE_ERR_UNSUPPORTED);
}

TEST_SUITE(ble_gap_test_suite_update_conn)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_gap_test_case_update_conn_good();
    ble_gap_test_case_update_conn_bad();
    ble_gap_test_case_update_conn_hci_fail();
    ble_gap_test_case_update_peer_good();
    ble_gap_test_case_update_req_good();
    ble_gap_test_case_update_req_hci_fail();
    ble_gap_test_case_update_req_reject();
    ble_gap_test_case_update_concurrent_good();
    ble_gap_test_case_update_concurrent_hci_fail();
}

/*****************************************************************************
 * $timeout                                                                  *
 *****************************************************************************/

static void
ble_gap_test_util_conn_forever(void)
{
    int32_t ticks_from_now;

    /* Initiate a connect procedure with no timeout. */
    ble_hs_test_util_connect(BLE_ADDR_TYPE_PUBLIC,
                             BLE_ADDR_TYPE_PUBLIC,
                             ((uint8_t[]){ 1, 2, 3, 4, 5, 6 }), BLE_HS_FOREVER,
                             NULL, ble_gap_test_util_connect_cb,
                             NULL, 0);

    /* Ensure no pending GAP event. */
    ticks_from_now = ble_gap_heartbeat();
    TEST_ASSERT(ticks_from_now == BLE_HS_FOREVER);

    /* Advance 100 seconds; ensure no timeout reported. */
    os_time_advance(100 * OS_TICKS_PER_SEC);
    TEST_ASSERT(ble_gap_test_conn_event_type == -1);
    TEST_ASSERT(ble_gap_conn_active());
}

static void
ble_gap_test_util_conn_timeout(int32_t duration_ms)
{
    struct hci_le_conn_complete evt;
    uint32_t duration_ticks;
    int32_t ticks_from_now;
    int rc;

    TEST_ASSERT_FATAL(duration_ms != BLE_HS_FOREVER);

    /* Initiate a connect procedure with the specified timeout. */
    ble_hs_test_util_connect(BLE_ADDR_TYPE_PUBLIC,
                             BLE_ADDR_TYPE_PUBLIC,
                             ((uint8_t[]){ 1, 2, 3, 4, 5, 6 }), duration_ms,
                             NULL, ble_gap_test_util_connect_cb,
                             NULL, 0);

    /* Ensure next GAP event is at the expected time. */
    rc = os_time_ms_to_ticks(duration_ms, &duration_ticks);
    TEST_ASSERT_FATAL(rc == 0);
    ticks_from_now = ble_gap_heartbeat();
    TEST_ASSERT(ticks_from_now == duration_ticks);

    /* Advance duration ms; ensure timeout event does not get reported before
     * connection complete event rxed.
     */
    os_time_advance(duration_ms);

    ble_hs_test_util_set_ack(
        ble_hs_hci_util_opcode_join(BLE_HCI_OGF_LE,
                                    BLE_HCI_OCF_LE_CREATE_CONN_CANCEL),
        0);

    TEST_ASSERT(ble_gap_test_conn_event_type == -1);

    ticks_from_now = ble_gap_heartbeat();
    TEST_ASSERT(ticks_from_now == BLE_HS_FOREVER);

    /* Ensure cancel create connection command was sent. */
    ble_hs_test_util_verify_tx_create_conn_cancel();

    /* Ensure timer has been stopped. */
    ticks_from_now = ble_gap_heartbeat();
    TEST_ASSERT(ticks_from_now == BLE_HS_FOREVER);

    /* Receive the connection complete event indicating a successful cancel. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_UNK_CONN_ID;
    rc = ble_gap_rx_conn_complete(&evt);
    TEST_ASSERT_FATAL(rc == 0);

    /* Ensure the GAP event was triggered. */
    TEST_ASSERT(ble_gap_test_conn_event_type == BLE_GAP_EVENT_CONNECT);
    TEST_ASSERT(ble_gap_test_conn_status == BLE_HS_ETIMEOUT);

    /* Clear GAP event for remainder of test. */
    ble_gap_test_util_reset_cb_info();
}

static void
ble_gap_test_util_disc_forever(void)
{
    struct ble_gap_disc_params params;
    int32_t ticks_from_now;

    memset(&params, 0, sizeof params);

    /* Initiate a discovery procedure with no timeout. */
    ble_hs_test_util_disc(BLE_ADDR_TYPE_PUBLIC,
                          BLE_HS_FOREVER, &params, ble_gap_test_util_disc_cb,
                          NULL, -1, 0);

    /* Ensure no pending GAP event. */
    ticks_from_now = ble_gap_heartbeat();
    TEST_ASSERT(ticks_from_now == BLE_HS_FOREVER);

    /* Advance 100 seconds; ensure no timeout reported. */
    os_time_advance(100 * OS_TICKS_PER_SEC);
    TEST_ASSERT(ble_gap_test_disc_event_type == -1);
    TEST_ASSERT(ble_gap_disc_active());
}

static void
ble_gap_test_util_disc_timeout(int32_t duration_ms)
{
    struct ble_gap_disc_params params;
    uint32_t duration_ticks;
    int32_t ticks_from_now;
    int rc;

    TEST_ASSERT_FATAL(duration_ms != BLE_HS_FOREVER);

    memset(&params, 0, sizeof params);

    /* Initiate a discovery procedure with the specified timeout. */
    ble_hs_test_util_disc(BLE_ADDR_TYPE_PUBLIC,
                          duration_ms, &params, ble_gap_test_util_disc_cb,
                          NULL, -1, 0);

    /* Ensure next GAP event is at the expected time. */
    rc = os_time_ms_to_ticks(duration_ms, &duration_ticks);
    TEST_ASSERT_FATAL(rc == 0);
    ticks_from_now = ble_gap_heartbeat();
    TEST_ASSERT(ticks_from_now == duration_ticks);

    /* Advance duration ms; ensure timeout event was reported. */
    os_time_advance(duration_ms);

    ble_hs_test_util_set_ack(
        ble_hs_hci_util_opcode_join(BLE_HCI_OGF_LE,
                                    BLE_HCI_OCF_LE_SET_SCAN_ENABLE),
        0);
    ticks_from_now = ble_gap_heartbeat();
    TEST_ASSERT(ticks_from_now == BLE_HS_FOREVER);

    TEST_ASSERT(ble_gap_test_disc_event_type == BLE_GAP_EVENT_DISC_COMPLETE);

    /* Clear GAP event for remainder of test. */
    ble_gap_test_util_reset_cb_info();
}

TEST_CASE(ble_gap_test_case_conn_timeout_conn_forever)
{
    ble_gap_test_util_init();

    /* 3 ms. */
    ble_gap_test_util_conn_timeout(3);

    /* No timeout. */
    ble_gap_test_util_conn_forever();

}

TEST_CASE(ble_gap_test_case_conn_timeout_conn_timeout)
{
    ble_gap_test_util_init();

    /* 30 ms. */
    ble_gap_test_util_conn_timeout(30);

    /* 5 ms. */
    ble_gap_test_util_conn_timeout(5);

}

TEST_CASE(ble_gap_test_case_conn_forever_conn_timeout)
{
    ble_gap_test_util_init();

    /* No timeout. */
    ble_gap_test_util_conn_forever();

    /* Cancel connect procedure manually. */
    ble_gap_test_util_conn_cancel(0);

    /* Clear GAP event for remainder of test. */
    ble_gap_test_util_reset_cb_info();

    /* 3 ms. */
    ble_gap_test_util_conn_timeout(3);
}

TEST_CASE(ble_gap_test_case_disc_timeout_disc_forever)
{
    ble_gap_test_util_init();

    /* 3 ms. */
    ble_gap_test_util_disc_timeout(3);

    /* No timeout. */
    ble_gap_test_util_disc_forever();

}

TEST_CASE(ble_gap_test_case_disc_timeout_disc_timeout)
{
    ble_gap_test_util_init();

    /* 30 ms. */
    ble_gap_test_util_disc_timeout(30);

    /* 5 ms. */
    ble_gap_test_util_disc_timeout(5);

}

TEST_CASE(ble_gap_test_case_disc_forever_disc_timeout)
{
    ble_gap_test_util_init();

    /* No timeout. */
    ble_gap_test_util_disc_forever();

    /* Cancel discovery procedure manually. */
    ble_hs_test_util_disc_cancel(0);

    /* 3 ms. */
    ble_gap_test_util_disc_timeout(3);
}

TEST_CASE(ble_gap_test_case_conn_timeout_disc_timeout)
{
    ble_gap_test_util_init();

    /* 15 seconds. */
    ble_gap_test_util_conn_timeout(15000);

    /* 1285 ms. */
    ble_gap_test_util_disc_timeout(1285);
}

TEST_SUITE(ble_gap_test_suite_timeout)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_gap_test_case_conn_timeout_conn_forever();
    ble_gap_test_case_conn_timeout_conn_timeout();
    ble_gap_test_case_conn_forever_conn_timeout();

    ble_gap_test_case_disc_timeout_disc_forever();
    ble_gap_test_case_disc_timeout_disc_timeout();
    ble_gap_test_case_disc_forever_disc_timeout();

    ble_gap_test_case_conn_timeout_disc_timeout();
}

/*****************************************************************************
 * $all                                                                      *
 *****************************************************************************/

int
ble_gap_test_all(void)
{
    ble_gap_test_suite_wl();
    ble_gap_test_suite_disc();
    ble_gap_test_suite_conn_dir();
    ble_gap_test_suite_conn_cancel();
    ble_gap_test_suite_conn_terminate();
    ble_gap_test_suite_conn_find();
    ble_gap_test_suite_adv();
    ble_gap_test_suite_stop_adv();
    ble_gap_test_suite_update_conn();
    ble_gap_test_suite_timeout();

    return tu_any_failed;
}
