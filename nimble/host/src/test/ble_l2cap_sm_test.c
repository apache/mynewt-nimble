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
#include <string.h>
#include <errno.h>
#include "testutil/testutil.h"
#include "nimble/hci_common.h"
#include "nimble/nimble_opt.h"
#include "host/host_hci.h"
#include "host/ble_hs_test.h"
#include "ble_hs_test_util.h"

#if NIMBLE_OPT_SM

/*****************************************************************************
 * $util                                                                     *
 *****************************************************************************/

#define BLE_L2CAP_SM_TEST_UTIL_HCI_HDR(handle, pb, len) \
    ((struct hci_data_hdr) {                            \
        .hdh_handle_pb_bc = ((handle)  << 0) |          \
                            ((pb)      << 12),          \
        .hdh_len = (len)                                \
    })

static void
ble_l2cap_sm_test_util_init(void)
{
    ble_hs_test_util_init();
    //ble_l2cap_test_update_status = -1;
    //ble_l2cap_test_update_arg = (void *)(uintptr_t)-1;
}

static int
ble_l2cap_sm_test_util_conn_cb(int event, int status,
                               struct ble_gap_conn_ctxt *ctxt, void *arg)
{
    int *accept;

    switch (event) {
    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
        accept = arg;
        return !*accept;

    default:
        return 0;
    }
}

static void
ble_l2cap_sm_test_util_rx_pair_req(struct ble_hs_conn *conn,
                                   struct ble_l2cap_sm_pair_cmd *req)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int payload_len;
    int rc;

    hci_hdr = BLE_L2CAP_SM_TEST_UTIL_HCI_HDR(
        2, BLE_HCI_PB_FIRST_FLUSH,
        BLE_L2CAP_HDR_SZ + BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_CMD_SZ);

    om = ble_hs_misc_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    payload_len = BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_CMD_SZ;

    om = ble_l2cap_prepend_hdr(om, BLE_L2CAP_CID_SM, payload_len);
    TEST_ASSERT_FATAL(om != NULL);

    v = os_mbuf_extend(om, payload_len);
    TEST_ASSERT_FATAL(v != NULL);

    rc = ble_l2cap_sm_pair_cmd_write(v, payload_len, 1, req);
    TEST_ASSERT_FATAL(rc == 0);

    rc = ble_hs_test_util_l2cap_rx(conn, &hci_hdr, om);
    TEST_ASSERT_FATAL(rc == 0);
}

static void
ble_l2cap_sm_test_util_rx_confirm(struct ble_hs_conn *conn,
                                  struct ble_l2cap_sm_pair_confirm *cmd)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int payload_len;
    int rc;

    hci_hdr = BLE_L2CAP_SM_TEST_UTIL_HCI_HDR(
        2, BLE_HCI_PB_FIRST_FLUSH,
        BLE_L2CAP_HDR_SZ + BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_CONFIRM_SZ);

    om = ble_hs_misc_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    payload_len = BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_CONFIRM_SZ;

    om = ble_l2cap_prepend_hdr(om, BLE_L2CAP_CID_SM, payload_len);
    TEST_ASSERT_FATAL(om != NULL);

    v = os_mbuf_extend(om, payload_len);
    TEST_ASSERT_FATAL(v != NULL);

    rc = ble_l2cap_sm_pair_confirm_write(v, payload_len, cmd);
    TEST_ASSERT_FATAL(rc == 0);

    rc = ble_hs_test_util_l2cap_rx(conn, &hci_hdr, om);
    TEST_ASSERT_FATAL(rc == 0);
}

static void
ble_l2cap_sm_test_util_rx_random(struct ble_hs_conn *conn,
                                 struct ble_l2cap_sm_pair_random *cmd)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int payload_len;
    int rc;

    hci_hdr = BLE_L2CAP_SM_TEST_UTIL_HCI_HDR(
        2, BLE_HCI_PB_FIRST_FLUSH,
        BLE_L2CAP_HDR_SZ + BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_RANDOM_SZ);

    om = ble_hs_misc_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    payload_len = BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_RANDOM_SZ;

    om = ble_l2cap_prepend_hdr(om, BLE_L2CAP_CID_SM, payload_len);
    TEST_ASSERT_FATAL(om != NULL);

    v = os_mbuf_extend(om, payload_len);
    TEST_ASSERT_FATAL(v != NULL);

    rc = ble_l2cap_sm_pair_random_write(v, payload_len, cmd);
    TEST_ASSERT_FATAL(rc == 0);

    rc = ble_hs_test_util_l2cap_rx(conn, &hci_hdr, om);
    TEST_ASSERT_FATAL(rc == 0);
}

static void
ble_l2cap_sm_test_util_verify_tx_hdr(uint8_t sm_op, uint16_t payload_len)
{
    TEST_ASSERT_FATAL(ble_hs_test_util_prev_tx != NULL);
    TEST_ASSERT(OS_MBUF_PKTLEN(ble_hs_test_util_prev_tx) ==
                BLE_L2CAP_SM_HDR_SZ + payload_len);

    TEST_ASSERT_FATAL(ble_hs_test_util_prev_tx->om_data[0] == sm_op);

    ble_hs_test_util_prev_tx->om_data += BLE_L2CAP_SM_HDR_SZ;
    ble_hs_test_util_prev_tx->om_len -= BLE_L2CAP_SM_HDR_SZ;
}

static void
ble_l2cap_sm_test_util_verify_tx_pair_rsp(
    struct ble_l2cap_sm_pair_cmd *exp_rsp)
{
    struct ble_l2cap_sm_pair_cmd rsp;
    int rc;

    ble_l2cap_sm_test_util_verify_tx_hdr(BLE_L2CAP_SM_OP_PAIR_RSP,
                                         BLE_L2CAP_SM_PAIR_CMD_SZ);

    rc = ble_l2cap_sm_pair_cmd_parse(ble_hs_test_util_prev_tx->om_data,
                                     ble_hs_test_util_prev_tx->om_len,
                                     &rsp);
    TEST_ASSERT_FATAL(rc == 0);

    TEST_ASSERT(rsp.io_cap == exp_rsp->io_cap);
    TEST_ASSERT(rsp.oob_data_flag == exp_rsp->oob_data_flag);
    TEST_ASSERT(rsp.authreq == exp_rsp->authreq);
    TEST_ASSERT(rsp.max_enc_key_size == exp_rsp->max_enc_key_size);
    TEST_ASSERT(rsp.init_key_dist == exp_rsp->init_key_dist);
    TEST_ASSERT(rsp.resp_key_dist == exp_rsp->resp_key_dist);
}

static void
ble_l2cap_sm_test_util_verify_tx_pair_confirm(
    struct ble_l2cap_sm_pair_confirm *exp_cmd)
{
    struct ble_l2cap_sm_pair_confirm cmd;
    int rc;

    ble_l2cap_sm_test_util_verify_tx_hdr(BLE_L2CAP_SM_OP_PAIR_CONFIRM,
                                         BLE_L2CAP_SM_PAIR_CONFIRM_SZ);

    rc = ble_l2cap_sm_pair_confirm_parse(ble_hs_test_util_prev_tx->om_data,
                                         ble_hs_test_util_prev_tx->om_len,
                                         &cmd);
    TEST_ASSERT_FATAL(rc == 0);

    TEST_ASSERT(memcmp(cmd.value, exp_cmd->value, 16) == 0);
}

static void
ble_l2cap_sm_test_util_verify_tx_pair_random(
    struct ble_l2cap_sm_pair_random *exp_cmd)
{
    struct ble_l2cap_sm_pair_random cmd;
    int rc;

    ble_l2cap_sm_test_util_verify_tx_hdr(BLE_L2CAP_SM_OP_PAIR_RANDOM,
                                         BLE_L2CAP_SM_PAIR_RANDOM_SZ);

    rc = ble_l2cap_sm_pair_random_parse(ble_hs_test_util_prev_tx->om_data,
                                         ble_hs_test_util_prev_tx->om_len,
                                         &cmd);
    TEST_ASSERT_FATAL(rc == 0);

    TEST_ASSERT(memcmp(cmd.value, exp_cmd->value, 16) == 0);
}

static void
ble_l2cap_sm_test_util_verify_tx_pair_fail(
    struct ble_l2cap_sm_pair_fail *exp_cmd)
{
    struct ble_l2cap_sm_pair_fail cmd;
    int rc;

    ble_l2cap_sm_test_util_verify_tx_hdr(BLE_L2CAP_SM_OP_PAIR_FAIL,
                                         BLE_L2CAP_SM_PAIR_FAIL_SZ);

    rc = ble_l2cap_sm_pair_fail_parse(ble_hs_test_util_prev_tx->om_data,
                                      ble_hs_test_util_prev_tx->om_len,
                                      &cmd);
    TEST_ASSERT_FATAL(rc == 0);

    TEST_ASSERT(cmd.reason == exp_cmd->reason);
}

static void
ble_l2cap_sm_test_util_rx_lt_key_req(uint16_t conn_handle, uint64_t r,
                                     uint16_t ediv)
{
    struct hci_le_lt_key_req evt;
    int rc;

    evt.subevent_code = BLE_HCI_LE_SUBEV_LT_KEY_REQ;
    evt.connection_handle = conn_handle;
    evt.random_number = r;
    evt.encrypted_diversifier = ediv;

    rc = ble_l2cap_sm_rx_lt_key_req(&evt);
    TEST_ASSERT_FATAL(rc == 0);
}

static void
ble_l2cap_sm_test_util_verify_tx_lt_key_req_reply(uint16_t conn_handle,
                                                  uint8_t *stk)
{
    uint8_t param_len;
    uint8_t *param;

    TEST_ASSERT_FATAL(ble_hs_test_util_prev_hci_tx != NULL);

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_LT_KEY_REQ_REPLY,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_LT_KEY_REQ_REPLY_LEN);
    TEST_ASSERT(le16toh(param + 0) == conn_handle);
    TEST_ASSERT(memcmp(param + 2, stk, 16) == 0);
}

static void
ble_l2cap_sm_test_util_rx_lt_key_req_reply_ack(uint8_t status,
                                               uint16_t conn_handle)
{
    uint8_t params[BLE_HCI_LT_KEY_REQ_REPLY_ACK_PARAM_LEN - 1];

    htole16(params, conn_handle);
    ble_hs_test_util_rx_le_ack_param(BLE_HCI_OCF_LE_LT_KEY_REQ_REPLY, status,
                                     params, sizeof params);
}

static void
ble_l2cap_sm_test_util_rx_enc_change(uint16_t conn_handle, uint8_t status,
                                     uint8_t encryption_enabled)
{
    struct hci_encrypt_change evt;

    evt.status = status;
    evt.encryption_enabled = encryption_enabled;
    evt.connection_handle = conn_handle;

    ble_l2cap_sm_rx_encryption_change(&evt);
}

/*****************************************************************************
 * $peer                                                                     *
 *****************************************************************************/

static void
ble_l2cap_sm_test_util_peer_lgcy_good(
    uint8_t *our_addr,
    uint8_t *their_addr,
    struct ble_l2cap_sm_pair_cmd *pair_req,
    struct ble_l2cap_sm_pair_cmd *pair_rsp,
    struct ble_l2cap_sm_pair_confirm *confirm_req,
    struct ble_l2cap_sm_pair_confirm *confirm_rsp,
    struct ble_l2cap_sm_pair_random *random_req,
    struct ble_l2cap_sm_pair_random *random_rsp,
    uint8_t *stk,
    uint64_t r,
    uint16_t ediv)
{
    struct ble_hs_conn *conn;

    ble_l2cap_sm_test_util_init();
    ble_hs_test_util_set_public_addr(our_addr);

    conn = ble_hs_test_util_create_conn(2, their_addr,
                                        ble_l2cap_sm_test_util_conn_cb,
                                        NULL);

    TEST_ASSERT(!conn->bhc_sec_params.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Receive a pair request from the peer. */
    ble_l2cap_sm_test_util_rx_pair_req(conn, pair_req);
    TEST_ASSERT(!conn->bhc_sec_params.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair response. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_rsp(pair_rsp);
    TEST_ASSERT(!conn->bhc_sec_params.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    ble_l2cap_sm_dbg_set_next_rand(random_rsp->value);

    /* Receive a pair confirm from the peer. */
    ble_l2cap_sm_test_util_rx_confirm(conn, confirm_req);
    TEST_ASSERT(!conn->bhc_sec_params.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair confirm. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_confirm(confirm_rsp);
    TEST_ASSERT(!conn->bhc_sec_params.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive a pair random from the peer. */
    ble_l2cap_sm_test_util_rx_random(conn, random_req);
    TEST_ASSERT(!conn->bhc_sec_params.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair random. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_random(random_rsp);
    TEST_ASSERT(!conn->bhc_sec_params.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive a long term key request from the controller. */
    ble_l2cap_sm_test_util_rx_lt_key_req(2, r, ediv);
    TEST_ASSERT(!conn->bhc_sec_params.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected long term key request reply command. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_lt_key_req_reply(2, stk);
    TEST_ASSERT(!conn->bhc_sec_params.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive a command complete event. */
    ble_l2cap_sm_test_util_rx_lt_key_req_reply_ack(0, 2);
    TEST_ASSERT(!conn->bhc_sec_params.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive an encryption changed event. */
    ble_l2cap_sm_test_util_rx_enc_change(2, 0, 1);

    /* Pairing should now be complete. */
    TEST_ASSERT(conn->bhc_sec_params.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);
}

static void
ble_l2cap_sm_test_util_peer_lgcy_fail(
    uint8_t *our_addr,
    uint8_t *their_addr,
    struct ble_l2cap_sm_pair_cmd *pair_req,
    struct ble_l2cap_sm_pair_cmd *pair_rsp,
    struct ble_l2cap_sm_pair_confirm *confirm_req,
    struct ble_l2cap_sm_pair_confirm *confirm_rsp,
    struct ble_l2cap_sm_pair_random *random_req,
    struct ble_l2cap_sm_pair_random *random_rsp,
    struct ble_l2cap_sm_pair_fail *fail_rsp)
{
    struct ble_hs_conn *conn;

    ble_l2cap_sm_test_util_init();
    ble_hs_test_util_set_public_addr(our_addr);

    conn = ble_hs_test_util_create_conn(2, their_addr,
                                        ble_l2cap_sm_test_util_conn_cb,
                                        NULL);

    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Receive a pair request from the peer. */
    ble_l2cap_sm_test_util_rx_pair_req(conn, pair_req);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair response. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_rsp(pair_rsp);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    ble_l2cap_sm_dbg_set_next_rand(random_rsp->value);

    /* Receive a pair confirm from the peer. */
    ble_l2cap_sm_test_util_rx_confirm(conn, confirm_req);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair confirm. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_confirm(confirm_rsp);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive a pair random from the peer. */
    ble_l2cap_sm_test_util_rx_random(conn, random_req);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair fail. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_fail(fail_rsp);

    /* The proc should now be freed. */
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);
}

TEST_CASE(ble_l2cap_sm_test_case_peer_lgcy_good)
{
    ble_l2cap_sm_test_util_peer_lgcy_good(
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        } }),
        ((struct ble_l2cap_sm_pair_confirm[1]) { {
            .value = {
                0x0a, 0xac, 0xa2, 0xae, 0xa6, 0x98, 0xdc, 0x6d,
                0x65, 0x84, 0x11, 0x69, 0x47, 0x36, 0x8d, 0xa0,
            },
        } }),
        ((struct ble_l2cap_sm_pair_confirm[1]) { {
            .value = {
                0x45, 0xd2, 0x2c, 0x38, 0xd8, 0x91, 0x4f, 0x19,
                0xa2, 0xd4, 0xfc, 0x7d, 0xad, 0x37, 0x79, 0xe0
            },
        } }),
        ((struct ble_l2cap_sm_pair_random[1]) { {
            .value = {
                0x2b, 0x3b, 0x69, 0xe4, 0xef, 0xab, 0xcc, 0x48,
                0x78, 0x20, 0x1a, 0x54, 0x7a, 0x91, 0x5d, 0xfb,
            },
        } }),
        ((struct ble_l2cap_sm_pair_random[1]) { {
            .value = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            },
        } }),
        ((uint8_t[16]) {
            0xe6, 0xb3, 0x05, 0xd4, 0xc3, 0x67, 0xf0, 0x45,
            0x38, 0x8f, 0xe7, 0x33, 0x0d, 0x51, 0x8e, 0xa4,
        }),
        0,
        0
    );
}

TEST_CASE(ble_l2cap_sm_test_case_peer_lgcy_fail)
{
    ble_l2cap_sm_test_util_peer_lgcy_fail(
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        } }),
        ((struct ble_l2cap_sm_pair_confirm[1]) { {
            .value = {
                0x0a, 0xac, 0xa2, 0xae, 0xa6, 0x98, 0xdc, 0x6d,
                0x65, 0x84, 0x11, 0x69, 0x47, 0x36, 0x8d, 0xa0,
            },
        } }),
        ((struct ble_l2cap_sm_pair_confirm[1]) { {
            .value = {
                0x45, 0xd2, 0x2c, 0x38, 0xd8, 0x91, 0x4f, 0x19,
                0xa2, 0xd4, 0xfc, 0x7d, 0xad, 0x37, 0x79, 0xe0
            },
        } }),
        ((struct ble_l2cap_sm_pair_random[1]) { {
            .value = {
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            },
        } }),
        ((struct ble_l2cap_sm_pair_random[1]) { {
            .value = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            },
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_CONFIRM_MISMATCH,
        } })
    );
}

TEST_SUITE(ble_l2cap_sm_test_suite)
{
    ble_l2cap_sm_test_case_peer_lgcy_good();
    ble_l2cap_sm_test_case_peer_lgcy_fail();
}
#endif

int
ble_l2cap_sm_test_all(void)
{
#if !NIMBLE_OPT_SM
    return 0;
#else
    ble_l2cap_sm_test_suite();

    return tu_any_failed;
#endif
}
