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
#include "host/ble_hs_test.h"
#include "ble_hs_test_util.h"

static int ble_gap_test_conn_event;
static int ble_gap_test_conn_status;
static struct ble_gap_conn_desc ble_gap_test_conn_desc;
static void *ble_gap_test_conn_arg;
static struct ble_gap_upd_params ble_gap_test_conn_peer_params;
static struct ble_gap_upd_params ble_gap_test_conn_self_params;

static int ble_gap_test_disc_event;
static int ble_gap_test_disc_status;
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
    ble_gap_test_conn_event = -1;
    ble_gap_test_conn_status = -1;
    memset(&ble_gap_test_conn_desc, 0xff, sizeof ble_gap_test_conn_desc);
    ble_gap_test_conn_arg = (void *)-1;

    ble_gap_test_disc_event = -1;
    ble_gap_test_disc_status = -1;
    memset(&ble_gap_test_disc_desc, 0xff, sizeof ble_gap_test_disc_desc);
    ble_gap_test_disc_arg = (void *)-1;
}

static void
ble_gap_test_util_init(void)
{
    ble_hs_test_util_init();
    ble_gap_test_util_reset_cb_info();
}

static void
ble_gap_test_util_disc_cb(int event, int status,
                          struct ble_gap_disc_desc *desc, void *arg)
{
    ble_gap_test_disc_event = event;
    ble_gap_test_disc_status = status;
    ble_gap_test_disc_desc = *desc;
    ble_gap_test_disc_arg = arg;
}

static int
ble_gap_test_util_connect_cb(int event, struct ble_gap_conn_ctxt *ctxt,
                             void *arg)
{
    int *fail_reason;

    ble_gap_test_conn_event = event;
    ble_gap_test_conn_desc = *ctxt->desc;
    ble_gap_test_conn_arg = arg;

    switch (event) {
    case BLE_GAP_EVENT_CONNECT:
        ble_gap_test_conn_status = ctxt->connect.status;
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ble_gap_test_conn_status = ctxt->disconnect.reason;
        break;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ble_gap_test_conn_status = ctxt->conn_update.status;
        break;

    case BLE_GAP_EVENT_CONN_CANCEL:
        break;

    case BLE_GAP_EVENT_TERM_FAILURE:
        ble_gap_test_conn_status = ctxt->term_failure.status;
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ble_gap_test_conn_arg = arg;
        break;

    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        ble_gap_test_conn_peer_params = *ctxt->conn_update_req.peer_params;
        *ctxt->conn_update_req.self_params = ble_gap_test_conn_self_params;

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
ble_gap_test_util_verify_tx_set_scan_params(uint16_t itvl,
                                            uint16_t scan_window,
                                            uint8_t filter_policy)
{
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_SET_SCAN_PARAMS,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_SET_SCAN_PARAM_LEN);
    TEST_ASSERT(param[0] == BLE_HCI_SCAN_TYPE_ACTIVE);
    TEST_ASSERT(le16toh(param + 1) == itvl);
    TEST_ASSERT(le16toh(param + 3) == scan_window);
    TEST_ASSERT(param[5] == BLE_HCI_ADV_OWN_ADDR_PUBLIC);
    TEST_ASSERT(param[6] == filter_policy);
}

static void
ble_gap_test_util_verify_tx_scan_enable(uint8_t enable)
{
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_SET_SCAN_ENABLE,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_SET_SCAN_ENABLE_LEN);
    TEST_ASSERT(param[0] == enable);
}

static void
ble_gap_test_util_verify_tx_create_conn(uint8_t filter_policy)
{
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_CREATE_CONN,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_CREATE_CONN_LEN);

    TEST_ASSERT(param[4] == filter_policy);

    /* XXX: Verify other fields. */
}

static void
ble_gap_test_util_verify_tx_create_conn_cancel(void)
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
ble_gap_test_util_verify_tx_rd_pwr(void)
{
    uint8_t param_len;

    ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                   BLE_HCI_OCF_LE_RD_ADV_CHAN_TXPWR,
                                   &param_len);
    TEST_ASSERT(param_len == 0);
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
        opcode = host_hci_opcode_join(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_REM_CONN_PARAM_RR);
    } else {
        opcode = host_hci_opcode_join(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_REM_CONN_PARAM_NRR);
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

TEST_CASE(ble_gap_test_case_conn_wl_bad_args)
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
    rc = ble_hs_test_util_conn_initiate(BLE_GAP_ADDR_TYPE_WL, NULL, NULL,
                                        ble_gap_test_util_connect_cb, NULL, 0);
    TEST_ASSERT(rc == 0);

    rc = ble_hs_test_util_wl_set(
        ((struct ble_gap_white_entry[]) { {
            BLE_ADDR_TYPE_PUBLIC, { 1, 2, 3, 4, 5, 6 }
        }, }),
        1, 0, 0);
    TEST_ASSERT(rc == BLE_HS_EBUSY);
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
        ble_gap_test_util_wl_set(white_list, white_list_count, i,
                                 BLE_ERR_UNSPECIFIED);
    }
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

    ble_gap_test_util_wl_set(white_list, white_list_count, 0, 0);
}

TEST_SUITE(ble_gap_test_suite_conn_wl)
{
    ble_gap_test_case_conn_wl_good();
    ble_gap_test_case_conn_wl_bad_args();
    ble_gap_test_case_conn_wl_ctlr_fail();
}

/*****************************************************************************
 * $discovery                                                                *
 *****************************************************************************/

static int
ble_gap_test_util_disc(uint8_t disc_mode, uint8_t *peer_addr,
                       struct ble_hs_adv *adv, int cmd_fail_idx,
                       uint8_t fail_status)
{
    int rc;

    ble_gap_test_util_init();

    /* Begin the discovery procedure. */
    rc = ble_hs_test_util_disc(0, disc_mode, BLE_HCI_SCAN_TYPE_ACTIVE,
                               BLE_HCI_SCAN_FILT_NO_WL,
                               ble_gap_test_util_disc_cb, NULL, cmd_fail_idx,
                               fail_status);
    TEST_ASSERT(rc == BLE_HS_HCI_ERR(fail_status));
    if (rc == 0) {
        TEST_ASSERT(ble_gap_master_in_progress());
        ble_gap_rx_adv_report(adv);
    } else {
        TEST_ASSERT(ble_gap_test_disc_status == -1);
    }

    if (cmd_fail_idx > 0) {
        /* Verify tx of set scan parameters command. */
        ble_gap_test_util_verify_tx_set_scan_params(
            30 * 1000 / BLE_HCI_ADV_ITVL,
            30 * 1000 / BLE_HCI_SCAN_ITVL,
            BLE_HCI_SCAN_FILT_NO_WL);
    }

    if (cmd_fail_idx > 1) {
        /* Verify tx of scan enable command. */
        ble_gap_test_util_verify_tx_scan_enable(1);
    }

    return rc;
}

TEST_CASE(ble_gap_test_case_conn_disc_bad_args)
{
    int rc;

    ble_gap_test_util_init();

    /*** Invalid discovery mode. */
    rc = ble_gap_disc(0, BLE_GAP_DISC_MODE_NON, BLE_HCI_SCAN_TYPE_ACTIVE,
                      BLE_HCI_SCAN_FILT_NO_WL, BLE_ADDR_TYPE_PUBLIC, ble_gap_test_util_disc_cb,
                      NULL);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    /*** Master operation already in progress. */
    rc = ble_hs_test_util_conn_initiate(BLE_GAP_ADDR_TYPE_WL, NULL, NULL,
                                        ble_gap_test_util_connect_cb, NULL, 0);
    rc = ble_gap_disc(0, BLE_GAP_DISC_MODE_GEN, BLE_HCI_SCAN_TYPE_ACTIVE,
                      BLE_HCI_SCAN_FILT_NO_WL, BLE_ADDR_TYPE_PUBLIC, ble_gap_test_util_disc_cb,
                      NULL);
    TEST_ASSERT(rc == BLE_HS_EALREADY);
}

TEST_CASE(ble_gap_test_case_conn_disc_good)
{
    uint8_t adv_data[32];
    uint8_t flags;
    int rc;
    int d;

    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };
    struct ble_hs_adv adv = {
        .event_type = BLE_HCI_ADV_TYPE_ADV_IND,
        .addr_type = BLE_ADDR_TYPE_PUBLIC,
        .length_data = 0,
        .rssi = 0,
        .addr = { 1, 2, 3, 4, 5, 6 },
        .data = adv_data,
    };

    flags = BLE_HS_ADV_F_DISC_LTD;
    rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_FLAGS, 1, &flags,
                             adv.data, &adv.length_data,
                             sizeof adv_data);
    TEST_ASSERT_FATAL(rc == 0);

    for (d = BLE_GAP_DISC_MODE_LTD; d < BLE_GAP_DISC_MODE_MAX; d++) {
        ble_gap_test_util_disc(d, peer_addr, &adv, -1, 0);

        TEST_ASSERT(ble_gap_master_in_progress());
        TEST_ASSERT(ble_gap_test_disc_event == BLE_GAP_EVENT_DISC_SUCCESS);
        TEST_ASSERT(ble_gap_test_disc_status == 0);
        TEST_ASSERT(ble_gap_test_disc_desc.event_type ==
                    BLE_HCI_ADV_TYPE_ADV_IND);
        TEST_ASSERT(ble_gap_test_disc_desc.addr_type == BLE_ADDR_TYPE_PUBLIC);
        TEST_ASSERT(ble_gap_test_disc_desc.length_data == 3);
        TEST_ASSERT(ble_gap_test_disc_desc.rssi == 0);
        TEST_ASSERT(memcmp(ble_gap_test_disc_desc.addr, adv.addr, 6) == 0);
        TEST_ASSERT(ble_gap_test_disc_arg == NULL);
    }
}

TEST_CASE(ble_gap_test_case_conn_disc_bad_flags)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };
    struct ble_hs_adv adv = {
        .event_type = BLE_HCI_ADV_TYPE_ADV_IND,
        .addr_type = BLE_ADDR_TYPE_PUBLIC,
        .length_data = 0,
        .rssi = 0,
        .addr = { 1, 2, 3, 4, 5, 6 },
        .data = NULL,
    };

    ble_gap_test_util_disc(BLE_GAP_DISC_MODE_LTD, peer_addr, &adv, -1, 0);
    TEST_ASSERT(ble_gap_master_in_progress());

    /* Verify that the report was ignored becuase of a mismatched LTD flag. */
    TEST_ASSERT(ble_gap_test_disc_event == -1);
}

TEST_CASE(ble_gap_test_case_conn_disc_hci_fail)
{
    int fail_idx;
    int rc;
    int d;

    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };
    struct ble_hs_adv adv = {
        .event_type = BLE_HCI_ADV_TYPE_ADV_IND,
        .addr_type = BLE_ADDR_TYPE_PUBLIC,
        .length_data = 0,
        .rssi = 0,
        .addr = { 1, 2, 3, 4, 5, 6 },
        .data = NULL,
    };

    for (d = BLE_GAP_DISC_MODE_LTD; d < BLE_GAP_DISC_MODE_MAX; d++) {
        for (fail_idx = 0; fail_idx < 2; fail_idx++) {
            rc = ble_gap_test_util_disc(d, peer_addr, &adv, fail_idx,
                                        BLE_ERR_UNSUPPORTED);
            TEST_ASSERT(rc == BLE_HS_HCI_ERR(BLE_ERR_UNSUPPORTED));
            TEST_ASSERT(!ble_gap_master_in_progress());
        }
    }
}

TEST_SUITE(ble_gap_test_suite_conn_disc)
{
    ble_gap_test_case_conn_disc_bad_args();
    ble_gap_test_case_conn_disc_good();
    ble_gap_test_case_conn_disc_bad_flags();
    ble_gap_test_case_conn_disc_hci_fail();
}

/*****************************************************************************
 * $direct connect                                                           *
 *****************************************************************************/

TEST_CASE(ble_gap_test_case_conn_dir_good)
{
    struct hci_le_conn_complete evt;
    int rc;

    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_util_init();

    TEST_ASSERT(!ble_gap_master_in_progress());

    rc = ble_hs_test_util_conn_initiate(BLE_ADDR_TYPE_PUBLIC, peer_addr, NULL,
                                        ble_gap_test_util_connect_cb, NULL, 0);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(ble_gap_master_in_progress());

    /* Verify tx of create connection command. */
    ble_gap_test_util_verify_tx_create_conn(BLE_HCI_CONN_FILT_NO_WL);
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

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CONNECT);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr, peer_addr, 6) == 0);

    TEST_ASSERT(ble_hs_atomic_conn_flags(2, NULL) == 0);
}

TEST_CASE(ble_gap_test_case_conn_dir_bad_args)
{
    int rc;

    ble_gap_test_util_init();

    TEST_ASSERT(!ble_gap_master_in_progress());

    /*** Invalid address type. */
    rc = ble_gap_conn_initiate(5, ((uint8_t[]){ 1, 2, 3, 4, 5, 6 }), NULL,
                               ble_gap_test_util_connect_cb, NULL);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
    TEST_ASSERT(!ble_gap_master_in_progress());

    /*** Connection already in progress. */
    rc = ble_hs_test_util_conn_initiate(BLE_ADDR_TYPE_PUBLIC,
                                        ((uint8_t[]){ 1, 2, 3, 4, 5, 6 }),
                                        NULL, ble_gap_test_util_connect_cb,
                                        NULL, 0);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_master_in_progress());

    rc = ble_gap_conn_initiate(BLE_ADDR_TYPE_PUBLIC,
                               ((uint8_t[]){ 2, 3, 4, 5, 6, 7 }), NULL,
                               ble_gap_test_util_connect_cb, NULL);
    TEST_ASSERT(rc == BLE_HS_EALREADY);
}

TEST_SUITE(ble_gap_test_suite_conn_dir)
{
    ble_gap_test_case_conn_dir_good();
    ble_gap_test_case_conn_dir_bad_args();
}

/*****************************************************************************
 * $cancel                                                                   *
 *****************************************************************************/

static void
ble_gap_test_util_conn_cancel(uint8_t *peer_addr, uint8_t hci_status)
{
    struct hci_le_conn_complete evt;
    int rc;

    ble_gap_test_util_init();

    /* Begin creating a connection. */
    rc = ble_hs_test_util_conn_initiate(BLE_ADDR_TYPE_PUBLIC, peer_addr, NULL,
                                        ble_gap_test_util_connect_cb, NULL, 0);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_master_in_progress());
    ble_gap_test_util_verify_tx_create_conn(BLE_HCI_CONN_FILT_NO_WL);

    /* Initiate cancel procedure. */
    rc = ble_hs_test_util_conn_cancel(hci_status);
    TEST_ASSERT(rc == BLE_HS_HCI_ERR(hci_status));

    /* Verify tx of cancel create connection command. */
    ble_gap_test_util_verify_tx_create_conn_cancel();
    if (rc != 0) {
        return;
    }
    TEST_ASSERT(ble_gap_master_in_progress());

    /* Receive connection complete event. */
    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_UNK_CONN_ID;
    evt.connection_handle = 2;
    evt.role = BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER;
    memcpy(evt.peer_addr, peer_addr, 6);
    rc = ble_gap_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(!ble_gap_master_in_progress());
    TEST_ASSERT(ble_hs_atomic_conn_flags(2, NULL) == BLE_HS_ENOTCONN);
}

TEST_CASE(ble_gap_test_case_conn_cancel_bad_args)
{
    int rc;

    ble_gap_test_util_init();

    /* Initiate cancel procedure with no connection in progress. */
    TEST_ASSERT(!ble_gap_master_in_progress());
    rc = ble_hs_test_util_conn_cancel(0);
    TEST_ASSERT(rc == BLE_HS_ENOENT);
}

TEST_CASE(ble_gap_test_case_conn_cancel_good)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_util_conn_cancel(peer_addr, 0);

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CONN_CANCEL);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == BLE_HS_CONN_HANDLE_NONE);
}

TEST_CASE(ble_gap_test_case_conn_cancel_ctlr_fail)
{
    struct hci_le_conn_complete evt;
    int rc;

    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_util_conn_cancel(peer_addr, BLE_ERR_REPEATED_ATTEMPTS);

    /* Make sure the host didn't invoke the application callback.  The cancel
     * failure was indicated via the return code from the gap call.
     */
    TEST_ASSERT(ble_gap_test_conn_event == -1);

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

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CONNECT);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr,
                       peer_addr, 6) == 0);

    TEST_ASSERT(ble_hs_atomic_conn_flags(2, NULL) == 0);
}

TEST_SUITE(ble_gap_test_suite_conn_cancel)
{
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
    ble_gap_test_conn_event = -1;

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

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_DISCONNECT);
    TEST_ASSERT(ble_gap_test_conn_status == BLE_HS_ENOTCONN);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(ble_gap_test_conn_desc.peer_id_addr_type == BLE_ADDR_TYPE_PUBLIC);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr, peer_addr, 6) == 0);
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

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_TERM_FAILURE);
    TEST_ASSERT(ble_gap_test_conn_status ==
                BLE_HS_HCI_ERR(BLE_ERR_UNSUPPORTED));
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(ble_gap_test_conn_desc.peer_id_addr_type == BLE_ADDR_TYPE_PUBLIC);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr, peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_arg == NULL);

    TEST_ASSERT(ble_hs_atomic_conn_flags(2, NULL) == 0);
    TEST_ASSERT(!ble_gap_master_in_progress());
}

TEST_CASE(ble_gap_test_case_conn_terminate_hci_fail)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

    ble_gap_test_util_terminate(peer_addr, BLE_ERR_REPEATED_ATTEMPTS);

    TEST_ASSERT(ble_gap_test_conn_event == -1);
    TEST_ASSERT(ble_hs_atomic_conn_flags(2, NULL) == 0);
    TEST_ASSERT(!ble_gap_master_in_progress());
}

TEST_SUITE(ble_gap_test_suite_conn_terminate)
{
    ble_gap_test_case_conn_terminate_bad_args();
    ble_gap_test_case_conn_terminate_good();
    ble_gap_test_case_conn_terminate_ctlr_fail();
    ble_gap_test_case_conn_terminate_hci_fail();
}

/*****************************************************************************
 * $advertise                                                                *
 *****************************************************************************/

static void
ble_gap_test_util_adv(uint8_t disc_mode, uint8_t conn_mode,
                      uint8_t *peer_addr, uint8_t peer_addr_type,
                      int connect_status,
                      int cmd_fail_idx, uint8_t fail_status)
{
    struct hci_le_conn_complete evt;
    struct ble_hs_adv_fields adv_fields;
    int cmd_idx;
    int rc;

    ble_gap_test_util_init();

    TEST_ASSERT(!ble_gap_slave_in_progress());

    if (conn_mode != BLE_GAP_CONN_MODE_DIR) {
        memset(&adv_fields, 0, sizeof adv_fields);
        adv_fields.tx_pwr_lvl_is_present = 1;
        rc = ble_gap_adv_set_fields(&adv_fields);
        TEST_ASSERT_FATAL(rc == 0);
    }

    rc = ble_hs_test_util_adv_start(disc_mode, conn_mode, peer_addr,
                                    peer_addr_type, NULL,
                                    ble_gap_test_util_connect_cb, NULL,
                                    cmd_fail_idx, fail_status);
    TEST_ASSERT(rc == BLE_HS_HCI_ERR(fail_status));

    cmd_idx = 0;

    if (fail_status == 0 || cmd_fail_idx >= cmd_idx) {
        /* Verify tx of set advertising params command. */
        ble_gap_test_util_verify_tx_adv_params();
    }
    cmd_idx++;

    if (conn_mode != BLE_GAP_CONN_MODE_DIR) {
        if (fail_status == 0 || cmd_fail_idx >= cmd_idx) {
            /* Verify tx of read tx power command. */
            ble_gap_test_util_verify_tx_rd_pwr();
        }
        cmd_idx++;

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

    if (fail_status == 0 || cmd_fail_idx >= cmd_idx) {
        TEST_ASSERT(ble_gap_slave_in_progress());

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

                TEST_ASSERT(!ble_gap_slave_in_progress());
            } else {
                TEST_ASSERT(ble_gap_slave_in_progress());
            }
        }
    }
}

TEST_CASE(ble_gap_test_case_conn_adv_bad_args)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };
    int rc;

    TEST_ASSERT(!ble_gap_slave_in_progress());

    /*** Invalid discoverable mode. */
    rc = ble_hs_test_util_adv_start(-1, BLE_GAP_CONN_MODE_DIR, peer_addr,
                                    BLE_ADDR_TYPE_PUBLIC, NULL,
                                    ble_gap_test_util_connect_cb, NULL, 0, 0);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
    TEST_ASSERT(!ble_gap_slave_in_progress());

    /*** Invalid connectable mode. */
    rc = ble_hs_test_util_adv_start(BLE_GAP_DISC_MODE_GEN, -1, peer_addr,
                                    BLE_ADDR_TYPE_PUBLIC, NULL,
                                    ble_gap_test_util_connect_cb, NULL, 0, 0);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
    TEST_ASSERT(!ble_gap_slave_in_progress());

    /*** Invalid peer address type with directed advertisable mode. */
    rc = ble_hs_test_util_adv_start(BLE_GAP_DISC_MODE_GEN,
                                    BLE_GAP_CONN_MODE_DIR, peer_addr, -1,
                                    NULL, ble_gap_test_util_connect_cb, NULL,
                                    0, 0);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
    TEST_ASSERT(!ble_gap_slave_in_progress());

    /*** Advertising already in progress. */
    rc = ble_hs_test_util_adv_start(BLE_GAP_DISC_MODE_GEN,
                                    BLE_GAP_CONN_MODE_DIR,
                                    peer_addr, BLE_ADDR_TYPE_PUBLIC, NULL,
                                    ble_gap_test_util_connect_cb, NULL, 0, 0);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ble_gap_slave_in_progress());

    rc = ble_hs_test_util_adv_start(BLE_GAP_DISC_MODE_GEN,
                                    BLE_GAP_CONN_MODE_DIR,
                                    peer_addr, BLE_ADDR_TYPE_PUBLIC, NULL,
                                    ble_gap_test_util_connect_cb, NULL, 0, 0);
    TEST_ASSERT(rc == BLE_HS_EALREADY);
    TEST_ASSERT(ble_gap_slave_in_progress());
}

TEST_CASE(ble_gap_test_case_conn_adv_good)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };
    int d;
    int c;

    for (c = BLE_GAP_CONN_MODE_NON; c < BLE_GAP_CONN_MODE_MAX; c++) {
        for (d = BLE_GAP_DISC_MODE_NON; d < BLE_GAP_DISC_MODE_MAX; d++) {
            ble_gap_test_util_adv(d, c, peer_addr, BLE_ADDR_TYPE_PUBLIC,
                                  BLE_ERR_SUCCESS, -1, 0);

            if (c != BLE_GAP_CONN_MODE_NON) {
                TEST_ASSERT(!ble_gap_slave_in_progress());
                TEST_ASSERT(ble_gap_test_conn_event ==
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

TEST_CASE(ble_gap_test_case_conn_adv_ctlr_fail)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };
    int d;
    int c;

    for (c = BLE_GAP_CONN_MODE_DIR; c < BLE_GAP_CONN_MODE_MAX; c++) {
        for (d = BLE_GAP_DISC_MODE_NON; d < BLE_GAP_DISC_MODE_MAX; d++) {
            ble_gap_test_util_adv(d, c, peer_addr, BLE_ADDR_TYPE_PUBLIC,
                                  BLE_ERR_DIR_ADV_TMO, -1, 0);

            TEST_ASSERT(!ble_gap_slave_in_progress());
            TEST_ASSERT(ble_gap_test_conn_event ==
                        BLE_GAP_EVENT_ADV_COMPLETE);
            TEST_ASSERT(ble_gap_test_conn_desc.conn_handle ==
                        BLE_HS_CONN_HANDLE_NONE);
            TEST_ASSERT(ble_gap_test_conn_arg == NULL);
        }
    }
}

TEST_CASE(ble_gap_test_case_conn_adv_hci_fail)
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
                ble_gap_test_util_adv(d, c, peer_addr, BLE_ADDR_TYPE_PUBLIC,
                                      0, fail_idx, BLE_ERR_UNSUPPORTED);
                TEST_ASSERT(!ble_gap_slave_in_progress());
                TEST_ASSERT(ble_gap_test_conn_event == -1);
            }
        }
    }
}

TEST_SUITE(ble_gap_test_suite_conn_adv)
{
    ble_gap_test_case_conn_adv_bad_args();
    ble_gap_test_case_conn_adv_good();
    ble_gap_test_case_conn_adv_ctlr_fail();
    ble_gap_test_case_conn_adv_hci_fail();
}

/*****************************************************************************
 * $stop advertise                                                           *
 *****************************************************************************/

static void
ble_gap_test_util_stop_adv(uint8_t disc_mode, uint8_t conn_mode,
                           uint8_t *peer_addr, uint8_t peer_addr_type,
                           int cmd_fail_idx, uint8_t fail_status)
{
    uint8_t hci_status;
    int rc;

    ble_gap_test_util_init();

    /* Start advertising; don't rx a successful connection event. */
    ble_gap_test_util_adv(disc_mode, conn_mode, peer_addr,
                          BLE_ADDR_TYPE_PUBLIC, BLE_ERR_UNSUPPORTED, 0, 0);

    TEST_ASSERT(ble_gap_slave_in_progress());

    /* Clear the advertising event that the host sent; we only care about what
     * happens after advertising is stopped.
     */
    ble_gap_test_util_reset_cb_info();

    /* Stop advertising. */
    hci_status = cmd_fail_idx == 0 ? fail_status : 0;

    rc = ble_hs_test_util_adv_stop(hci_status);
    TEST_ASSERT(rc == BLE_HS_HCI_ERR(hci_status));

    /* Verify tx of advertising enable command. */
    ble_gap_test_util_verify_tx_adv_enable(0);
}

TEST_CASE(ble_gap_test_case_conn_stop_adv_good)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };
    int d;
    int c;

    for (c = BLE_GAP_CONN_MODE_NON; c < BLE_GAP_CONN_MODE_MAX; c++) {
        for (d = BLE_GAP_DISC_MODE_NON; d < BLE_GAP_DISC_MODE_MAX; d++) {
            ble_gap_test_util_stop_adv(d, c, peer_addr, BLE_ADDR_TYPE_PUBLIC,
                                       -1, 0);
            TEST_ASSERT(!ble_gap_slave_in_progress());
            TEST_ASSERT(ble_gap_test_conn_event == -1);
            TEST_ASSERT(ble_gap_test_conn_status == -1);
            TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == (uint16_t)-1);
            TEST_ASSERT(ble_gap_test_conn_arg == (void *)-1);
        }
    }
}

TEST_CASE(ble_gap_test_case_conn_stop_adv_hci_fail)
{
    uint8_t peer_addr[6] = { 1, 2, 3, 4, 5, 6 };
    int d;
    int c;

    for (c = BLE_GAP_CONN_MODE_NON; c < BLE_GAP_CONN_MODE_MAX; c++) {
        for (d = BLE_GAP_DISC_MODE_NON; d < BLE_GAP_DISC_MODE_MAX; d++) {
            ble_gap_test_util_stop_adv(d, c, peer_addr, BLE_ADDR_TYPE_PUBLIC,
                                       0, BLE_ERR_UNSUPPORTED);
            TEST_ASSERT(ble_gap_slave_in_progress());
            TEST_ASSERT(ble_gap_test_conn_event == -1);
            TEST_ASSERT(ble_gap_test_conn_status == -1);
            TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == (uint16_t)-1);
            TEST_ASSERT(ble_gap_test_conn_arg == (void *)-1);
        }
    }
}

TEST_SUITE(ble_gap_test_suite_conn_stop_adv)
{
    ble_gap_test_case_conn_stop_adv_good();
    ble_gap_test_case_conn_stop_adv_hci_fail();
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

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CONN_UPDATE);
    TEST_ASSERT(ble_gap_test_conn_status == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr, peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_itvl == params->itvl_max);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_latency == params->latency);
    TEST_ASSERT(ble_gap_test_conn_desc.supervision_timeout ==
        params->supervision_timeout);

    TEST_ASSERT(!ble_gap_test_util_update_in_progress(2));

    return;

fail:
    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CONN_UPDATE);
    TEST_ASSERT(ble_gap_test_conn_status == status);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr, peer_addr, 6) == 0);
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

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CONN_UPDATE);
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

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CONN_UPDATE);
    TEST_ASSERT(ble_gap_test_conn_status == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr, peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_itvl == self_params->itvl_max);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_latency == self_params->latency);
    TEST_ASSERT(ble_gap_test_conn_desc.supervision_timeout ==
                self_params->supervision_timeout);

    return;

hci_fail:
    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CONN_UPDATE);
    TEST_ASSERT(ble_gap_test_conn_status == BLE_HS_HCI_ERR(hci_status));
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr, peer_addr, 6) == 0);
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
    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CONN_UPDATE);
    TEST_ASSERT(ble_gap_test_conn_status == BLE_HS_HCI_ERR(hci_status));
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr, peer_addr, 6) == 0);
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

    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CONN_UPDATE);
    TEST_ASSERT(ble_gap_test_conn_status == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr, peer_addr, 6) == 0);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_itvl == self_params->itvl_max);
    TEST_ASSERT(ble_gap_test_conn_desc.conn_latency == self_params->latency);
    TEST_ASSERT(ble_gap_test_conn_desc.supervision_timeout ==
                self_params->supervision_timeout);

    return;

hci_fail:
    TEST_ASSERT(ble_gap_test_conn_event == BLE_GAP_EVENT_CONN_UPDATE);
    TEST_ASSERT(ble_gap_test_conn_status == BLE_HS_HCI_ERR(fail_status));
    TEST_ASSERT(ble_gap_test_conn_desc.conn_handle == 2);
    TEST_ASSERT(memcmp(ble_gap_test_conn_desc.peer_id_addr, peer_addr, 6) == 0);
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
 * $all                                                                      *
 *****************************************************************************/

int
ble_gap_test_all(void)
{
    ble_gap_test_suite_conn_wl();
    ble_gap_test_suite_conn_disc();
    ble_gap_test_suite_conn_dir();
    ble_gap_test_suite_conn_cancel();
    ble_gap_test_suite_conn_terminate();
    ble_gap_test_suite_conn_adv();
    ble_gap_test_suite_conn_stop_adv();
    ble_gap_test_suite_update_conn();

    return tu_any_failed;
}
