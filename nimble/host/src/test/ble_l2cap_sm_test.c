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

int ble_l2cap_sm_test_gap_event;
int ble_l2cap_sm_test_gap_status;
struct ble_gap_sec_state ble_l2cap_sm_test_sec_state;
struct ble_gap_ltk_params ble_l2cap_sm_test_ltk_params;

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

    ble_l2cap_sm_test_gap_event = -1;
    ble_l2cap_sm_test_gap_status = -1;
    memset(&ble_l2cap_sm_test_sec_state, 0xff,
           sizeof ble_l2cap_sm_test_sec_state);
}

struct ble_l2cap_sm_test_ltk_info {
    uint8_t ltk[16];
    unsigned authenticated:1;
};

static int
ble_l2cap_sm_test_util_conn_cb(int event, int status,
                               struct ble_gap_conn_ctxt *ctxt, void *arg)
{
    struct ble_l2cap_sm_test_ltk_info *ltk_info;
    int rc;

    switch (event) {
    case BLE_GAP_EVENT_SECURITY:
        ble_l2cap_sm_test_sec_state = ctxt->desc->sec_state;
        rc = 0;
        break;

    case BLE_GAP_EVENT_LTK_REQUEST:
        ltk_info = arg;
        if (ltk_info == NULL) {
            memset(ctxt->ltk_params->ltk, 0, sizeof ctxt->ltk_params->ltk);
            ctxt->ltk_params->authenticated = 0;
        } else {
            memcpy(ctxt->ltk_params->ltk, ltk_info->ltk,
                   sizeof ctxt->ltk_params->ltk);
            ctxt->ltk_params->authenticated = ltk_info->authenticated;
        }

        ble_l2cap_sm_test_ltk_params = *ctxt->ltk_params;
        if (ltk_info == NULL) {
            rc = -1;
        } else {
            rc = 0;
        }
        break;

    default:
        return 0;
    }

    ble_l2cap_sm_test_gap_event = event;
    ble_l2cap_sm_test_gap_status = status;

    return rc;
}

static void
ble_l2cap_sm_test_util_rx_pair_cmd(struct ble_hs_conn *conn, uint8_t op,
                                   struct ble_l2cap_sm_pair_cmd *cmd,
                                   int rx_status)
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

    v = os_mbuf_extend(om, payload_len);
    TEST_ASSERT_FATAL(v != NULL);

    ble_l2cap_sm_pair_cmd_write(v, payload_len, op == BLE_L2CAP_SM_OP_PAIR_REQ,
                                cmd);

    rc = ble_hs_test_util_l2cap_rx_first_frag(conn, BLE_L2CAP_CID_SM, &hci_hdr,
                                              om);
    TEST_ASSERT(rc == rx_status);
}

static void
ble_l2cap_sm_test_util_rx_pair_req(struct ble_hs_conn *conn,
                                   struct ble_l2cap_sm_pair_cmd *req,
                                   int rx_status)
{
    ble_l2cap_sm_test_util_rx_pair_cmd(conn, BLE_L2CAP_SM_OP_PAIR_REQ, req,
                                       rx_status);
}

static void
ble_l2cap_sm_test_util_rx_pair_rsp(struct ble_hs_conn *conn,
                                   struct ble_l2cap_sm_pair_cmd *rsp,
                                   int rx_status)
{
    ble_l2cap_sm_test_util_rx_pair_cmd(conn, BLE_L2CAP_SM_OP_PAIR_RSP, rsp,
                                       rx_status);
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

    v = os_mbuf_extend(om, payload_len);
    TEST_ASSERT_FATAL(v != NULL);

    ble_l2cap_sm_pair_confirm_write(v, payload_len, cmd);

    rc = ble_hs_test_util_l2cap_rx_first_frag(conn, BLE_L2CAP_CID_SM, &hci_hdr,
                                              om);
    TEST_ASSERT_FATAL(rc == 0);
}

static void
ble_l2cap_sm_test_util_rx_random(struct ble_hs_conn *conn,
                                 struct ble_l2cap_sm_pair_random *cmd,
                                 int exp_status)
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

    v = os_mbuf_extend(om, payload_len);
    TEST_ASSERT_FATAL(v != NULL);

    ble_l2cap_sm_pair_random_write(v, payload_len, cmd);

    rc = ble_hs_test_util_l2cap_rx_first_frag(conn, BLE_L2CAP_CID_SM, &hci_hdr,
                                              om);
    TEST_ASSERT_FATAL(rc == exp_status);
}

static struct os_mbuf *
ble_l2cap_sm_test_util_verify_tx_hdr(uint8_t sm_op, uint16_t payload_len)
{
    struct os_mbuf *om;

    om = ble_hs_test_util_prev_tx_dequeue();
    TEST_ASSERT_FATAL(om != NULL);

    TEST_ASSERT(OS_MBUF_PKTLEN(om) == BLE_L2CAP_SM_HDR_SZ + payload_len);
    TEST_ASSERT_FATAL(om->om_data[0] == sm_op);

    om->om_data += BLE_L2CAP_SM_HDR_SZ;
    om->om_len -= BLE_L2CAP_SM_HDR_SZ;

    return om;
}

static void
ble_l2cap_sm_test_util_verify_tx_pair_cmd(
    uint8_t op,
    struct ble_l2cap_sm_pair_cmd *exp_cmd)
{
    struct ble_l2cap_sm_pair_cmd cmd;
    struct os_mbuf *om;

    om = ble_l2cap_sm_test_util_verify_tx_hdr(op, BLE_L2CAP_SM_PAIR_CMD_SZ);
    ble_l2cap_sm_pair_cmd_parse(om->om_data, om->om_len, &cmd);

    TEST_ASSERT(cmd.io_cap == exp_cmd->io_cap);
    TEST_ASSERT(cmd.oob_data_flag == exp_cmd->oob_data_flag);
    TEST_ASSERT(cmd.authreq == exp_cmd->authreq);
    TEST_ASSERT(cmd.max_enc_key_size == exp_cmd->max_enc_key_size);
    TEST_ASSERT(cmd.init_key_dist == exp_cmd->init_key_dist);
    TEST_ASSERT(cmd.resp_key_dist == exp_cmd->resp_key_dist);
}

static void
ble_l2cap_sm_test_util_verify_tx_pair_req(
    struct ble_l2cap_sm_pair_cmd *exp_req)
{
    ble_l2cap_sm_test_util_verify_tx_pair_cmd(BLE_L2CAP_SM_OP_PAIR_REQ,
                                              exp_req);
}

static void
ble_l2cap_sm_test_util_verify_tx_pair_rsp(
    struct ble_l2cap_sm_pair_cmd *exp_rsp)
{
    ble_l2cap_sm_test_util_verify_tx_pair_cmd(BLE_L2CAP_SM_OP_PAIR_RSP,
                                              exp_rsp);
}

static void
ble_l2cap_sm_test_util_verify_tx_pair_confirm(
    struct ble_l2cap_sm_pair_confirm *exp_cmd)
{
    struct ble_l2cap_sm_pair_confirm cmd;
    struct os_mbuf *om;

    om = ble_l2cap_sm_test_util_verify_tx_hdr(BLE_L2CAP_SM_OP_PAIR_CONFIRM,
                                              BLE_L2CAP_SM_PAIR_CONFIRM_SZ);
    ble_l2cap_sm_pair_confirm_parse(om->om_data, om->om_len, &cmd);

    TEST_ASSERT(memcmp(cmd.value, exp_cmd->value, 16) == 0);
}

static void
ble_l2cap_sm_test_util_verify_tx_pair_random(
    struct ble_l2cap_sm_pair_random *exp_cmd)
{
    struct ble_l2cap_sm_pair_random cmd;
    struct os_mbuf *om;

    om = ble_l2cap_sm_test_util_verify_tx_hdr(BLE_L2CAP_SM_OP_PAIR_RANDOM,
                                              BLE_L2CAP_SM_PAIR_RANDOM_SZ);
    ble_l2cap_sm_pair_random_parse(om->om_data, om->om_len, &cmd);

    TEST_ASSERT(memcmp(cmd.value, exp_cmd->value, 16) == 0);
}

static void
ble_l2cap_sm_test_util_verify_tx_pair_fail(
    struct ble_l2cap_sm_pair_fail *exp_cmd)
{
    struct ble_l2cap_sm_pair_fail cmd;
    struct os_mbuf *om;

    om = ble_l2cap_sm_test_util_verify_tx_hdr(BLE_L2CAP_SM_OP_PAIR_FAIL,
                                              BLE_L2CAP_SM_PAIR_FAIL_SZ);
    ble_l2cap_sm_pair_fail_parse(om->om_data, om->om_len, &cmd);

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

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_LT_KEY_REQ_REPLY,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_LT_KEY_REQ_REPLY_LEN);
    TEST_ASSERT(le16toh(param + 0) == conn_handle);
    TEST_ASSERT(memcmp(param + 2, stk, 16) == 0);
}

static void
ble_l2cap_sm_test_util_verify_tx_lt_key_req_neg_reply(uint16_t conn_handle)
{
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_LT_KEY_REQ_NEG_REPLY,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_LT_KEY_REQ_NEG_REPLY_LEN);
    TEST_ASSERT(le16toh(param + 0) == conn_handle);
}

static void
ble_l2cap_sm_test_util_set_lt_key_req_reply_ack(uint8_t status,
                                                uint16_t conn_handle)
{
    static uint8_t params[BLE_HCI_LT_KEY_REQ_REPLY_ACK_PARAM_LEN];

    htole16(params, conn_handle);
    ble_hs_test_util_set_ack_params(
        host_hci_opcode_join(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_LT_KEY_REQ_REPLY),
        status, params, sizeof params);
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

static void
ble_l2cap_sm_test_util_verify_tx_start_enc(uint16_t conn_handle,
                                           uint64_t random_number,
                                           uint16_t ediv,
                                           uint8_t *ltk)
{
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_START_ENCRYPT,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_LE_START_ENCRYPT_LEN);
    TEST_ASSERT(le16toh(param + 0) == conn_handle);
    TEST_ASSERT(le64toh(param + 2) == random_number);
    TEST_ASSERT(le16toh(param + 10) == ediv);
    TEST_ASSERT(memcmp(param + 12, ltk, 16) == 0);
}

/*****************************************************************************
 * $peer                                                                     *
 *****************************************************************************/

static void
ble_l2cap_sm_test_util_peer_fail_inval(
    int we_are_master,
    uint8_t *init_addr,
    uint8_t *rsp_addr,
    struct ble_l2cap_sm_pair_cmd *pair_req,
    struct ble_l2cap_sm_pair_fail *pair_fail)
{
    struct ble_hs_conn *conn;

    ble_l2cap_sm_test_util_init();
    ble_hs_test_util_set_public_addr(rsp_addr);

    conn = ble_hs_test_util_create_conn(2, init_addr,
                                        ble_l2cap_sm_test_util_conn_cb,
                                        NULL);

    if (!we_are_master) {
        conn->bhc_flags &= ~BLE_HS_CONN_F_MASTER;
    }

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Receive a pair request from the peer. */
    ble_l2cap_sm_test_util_rx_pair_req(conn, pair_req,
                                       BLE_HS_SM_US_ERR(pair_fail->reason));
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Ensure we sent the expected pair fail. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_fail(pair_fail);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Verify that security callback was not executed. */
    TEST_ASSERT(ble_l2cap_sm_test_gap_event == -1);
    TEST_ASSERT(ble_l2cap_sm_test_gap_status == -1);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(!conn->bhc_sec_state.authenticated);
}

TEST_CASE(ble_l2cap_sm_test_case_peer_fail_inval)
{
    /* Invalid role detected before other arguments. */
    ble_l2cap_sm_test_util_peer_fail_inval(
        1,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x14,
            .oob_data_flag = 0,
            .authreq = 0x12,
            .max_enc_key_size = 20,
            .init_key_dist = 0x0b,
            .resp_key_dist = 0x11,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_CMD_NOT_SUPP,
        } })
    );

    /* Invalid IO capabiltiies. */
    ble_l2cap_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x14,
            .oob_data_flag = 0,
            .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );

    /* Invalid OOB flag. */
    ble_l2cap_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 2,
            .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );

    /* Invalid authreq - reserved bonding flag. */
    ble_l2cap_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x2,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );

    /* Invalid authreq - reserved other flag. */
    ble_l2cap_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x20,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );

    /* Invalid key size - too small. */
    ble_l2cap_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x5,
            .max_enc_key_size = 6,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );

    /* Invalid key size - too large. */
    ble_l2cap_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x5,
            .max_enc_key_size = 17,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );

    /* Invalid init key dist. */
    ble_l2cap_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x5,
            .max_enc_key_size = 16,
            .init_key_dist = 0x10,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );

    /* Invalid resp key dist. */
    ble_l2cap_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x5,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x10,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );
}

static void
ble_l2cap_sm_test_util_peer_lgcy_fail_confirm(
    uint8_t *init_addr,
    uint8_t *rsp_addr,
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
    ble_hs_test_util_set_public_addr(rsp_addr);
    ble_l2cap_sm_dbg_set_next_pair_rand(random_rsp->value);

    conn = ble_hs_test_util_create_conn(2, init_addr,
                                        ble_l2cap_sm_test_util_conn_cb,
                                        NULL);

    /* Peer is the initiator so we must be the slave. */
    conn->bhc_flags &= ~BLE_HS_CONN_F_MASTER;

    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Receive a pair request from the peer. */
    ble_l2cap_sm_test_util_rx_pair_req(conn, pair_req, 0);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair response. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_rsp(pair_rsp);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive a pair confirm from the peer. */
    ble_l2cap_sm_test_util_rx_confirm(conn, confirm_req);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair confirm. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_confirm(confirm_rsp);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive a pair random from the peer. */
    ble_l2cap_sm_test_util_rx_random(
        conn, random_req, BLE_HS_SM_US_ERR(BLE_L2CAP_SM_ERR_CONFIRM_MISMATCH));

    /* Ensure we sent the expected pair fail. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_fail(fail_rsp);

    /* The proc should now be freed. */
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Verify that security callback was executed. */
    TEST_ASSERT(ble_l2cap_sm_test_gap_event == BLE_GAP_EVENT_SECURITY);
    TEST_ASSERT(ble_l2cap_sm_test_gap_status ==
                BLE_HS_SM_US_ERR(BLE_L2CAP_SM_ERR_CONFIRM_MISMATCH));
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.pair_alg ==
                BLE_L2CAP_SM_PAIR_ALG_JW);
    TEST_ASSERT(!ble_l2cap_sm_test_sec_state.enc_enabled);
    TEST_ASSERT(!ble_l2cap_sm_test_sec_state.authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.pair_alg ==
                conn->bhc_sec_state.pair_alg);
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.enc_enabled ==
                conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.authenticated ==
                conn->bhc_sec_state.authenticated);
}

TEST_CASE(ble_l2cap_sm_test_case_peer_lgcy_fail_confirm)
{
    ble_l2cap_sm_test_util_peer_lgcy_fail_confirm(
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
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

static void
ble_l2cap_sm_test_util_peer_lgcy_good(
    uint8_t *init_addr,
    uint8_t *rsp_addr,
    struct ble_l2cap_sm_pair_cmd *pair_req,
    struct ble_l2cap_sm_pair_cmd *pair_rsp,
    struct ble_l2cap_sm_pair_confirm *confirm_req,
    struct ble_l2cap_sm_pair_confirm *confirm_rsp,
    struct ble_l2cap_sm_pair_random *random_req,
    struct ble_l2cap_sm_pair_random *random_rsp,
    int pair_alg,
    uint8_t *tk,
    uint8_t *stk,
    uint64_t r,
    uint16_t ediv)
{
    struct ble_hs_conn *conn;

    ble_l2cap_sm_test_util_init();
    ble_hs_test_util_set_public_addr(rsp_addr);
    ble_l2cap_sm_dbg_set_next_pair_rand(random_rsp->value);

    conn = ble_hs_test_util_create_conn(2, init_addr,
                                        ble_l2cap_sm_test_util_conn_cb,
                                        NULL);

    /* Peer is the initiator so we must be the slave. */
    conn->bhc_flags &= ~BLE_HS_CONN_F_MASTER;

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Receive a pair request from the peer. */
    ble_l2cap_sm_test_util_rx_pair_req(conn, pair_req, 0);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair response. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_rsp(pair_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive a pair confirm from the peer. */
    ble_l2cap_sm_test_util_rx_confirm(conn, confirm_req);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair confirm. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_confirm(confirm_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive a pair random from the peer. */
    ble_l2cap_sm_test_util_rx_random(conn, random_req, 0);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair random. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_random(random_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive a long term key request from the controller. */
    ble_l2cap_sm_test_util_set_lt_key_req_reply_ack(0, 2);
    ble_l2cap_sm_test_util_rx_lt_key_req(2, r, ediv);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected long term key request reply command. */
    ble_l2cap_sm_test_util_verify_tx_lt_key_req_reply(2, stk);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive an encryption changed event. */
    ble_l2cap_sm_test_util_rx_enc_change(2, 0, 1);

    /* Pairing should now be complete. */
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Verify that security callback was executed. */
    TEST_ASSERT(ble_l2cap_sm_test_gap_event == BLE_GAP_EVENT_SECURITY);
    TEST_ASSERT(ble_l2cap_sm_test_gap_status == 0);
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.pair_alg == pair_alg);
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.enc_enabled);
    TEST_ASSERT(!ble_l2cap_sm_test_sec_state.authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.pair_alg ==
                conn->bhc_sec_state.pair_alg);
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.enc_enabled ==
                conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.authenticated ==
                conn->bhc_sec_state.authenticated);
}

TEST_CASE(ble_l2cap_sm_test_case_peer_lgcy_jw_good)
{
    ble_l2cap_sm_test_util_peer_lgcy_good(
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
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
        BLE_L2CAP_SM_PAIR_ALG_JW,
        NULL,
        ((uint8_t[16]) {
            0xa4, 0x8e, 0x51, 0x0d, 0x33, 0xe7, 0x8f, 0x38,
            0x45, 0xf0, 0x67, 0xc3, 0xd4, 0x05, 0xb3, 0xe6,
        }),
        0,
        0
    );
}

static void
ble_l2cap_sm_test_util_peer_bonding_good(uint8_t *ltk, int authenticated,
                                         uint16_t ediv, uint64_t rand_num)
{
    struct ble_l2cap_sm_test_ltk_info ltk_info;
    struct ble_hs_conn *conn;

    ble_l2cap_sm_test_util_init();

    memcpy(ltk_info.ltk, ltk, sizeof ltk_info.ltk);
    ltk_info.authenticated = authenticated;

    conn = ble_hs_test_util_create_conn(2, ((uint8_t[6]){1,2,3,4,5,6}),
                                        ble_l2cap_sm_test_util_conn_cb,
                                        &ltk_info);

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Receive a long term key request from the controller. */
    ble_l2cap_sm_test_util_set_lt_key_req_reply_ack(0, 2);
    ble_l2cap_sm_test_util_rx_lt_key_req(2, rand_num, ediv);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure the LTK request event got sent to the application. */
    TEST_ASSERT(ble_l2cap_sm_test_gap_event == BLE_GAP_EVENT_LTK_REQUEST);
    TEST_ASSERT(ble_l2cap_sm_test_gap_status == 0);
    TEST_ASSERT(ble_l2cap_sm_test_ltk_params.ediv == ediv);
    TEST_ASSERT(ble_l2cap_sm_test_ltk_params.rand_num == rand_num);
    TEST_ASSERT(memcmp(ble_l2cap_sm_test_ltk_params.ltk, ltk_info.ltk,
                       16) == 0);
    TEST_ASSERT(ble_l2cap_sm_test_ltk_params.authenticated ==
                ltk_info.authenticated);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected long term key request reply command. */
    ble_l2cap_sm_test_util_verify_tx_lt_key_req_reply(2, ltk_info.ltk);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive an encryption changed event. */
    ble_l2cap_sm_test_util_rx_enc_change(2, 0, 1);

    /* Pairing should now be complete. */
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Verify that security callback was executed. */
    TEST_ASSERT(ble_l2cap_sm_test_gap_event == BLE_GAP_EVENT_SECURITY);
    TEST_ASSERT(ble_l2cap_sm_test_gap_status == 0);
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.authenticated ==
                ltk_info.authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.authenticated ==
                ltk_info.authenticated);
}

static void
ble_l2cap_sm_test_util_peer_bonding_bad(uint16_t ediv, uint64_t rand_num)
{
    struct ble_hs_conn *conn;

    ble_l2cap_sm_test_util_init();

    conn = ble_hs_test_util_create_conn(2, ((uint8_t[6]){1,2,3,4,5,6}),
                                        ble_l2cap_sm_test_util_conn_cb,
                                        NULL);

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Receive a long term key request from the controller. */
    ble_l2cap_sm_test_util_set_lt_key_req_reply_ack(0, 2);
    ble_l2cap_sm_test_util_rx_lt_key_req(2, rand_num, ediv);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);

    /* Ensure the LTK request event got sent to the application. */
    TEST_ASSERT(ble_l2cap_sm_test_gap_event == BLE_GAP_EVENT_LTK_REQUEST);
    TEST_ASSERT(ble_l2cap_sm_test_gap_status == 0);
    TEST_ASSERT(ble_l2cap_sm_test_ltk_params.ediv == ediv);
    TEST_ASSERT(ble_l2cap_sm_test_ltk_params.rand_num == rand_num);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);

    /* Ensure we sent the expected long term key request neg reply command. */
    ble_l2cap_sm_test_util_verify_tx_lt_key_req_neg_reply(2);

    /* Ensure the security procedure was aborted. */
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(!conn->bhc_sec_state.authenticated);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);
}

TEST_CASE(ble_l2cap_sm_test_case_peer_bonding_good)
{
    /* Unauthenticated. */
    ble_l2cap_sm_test_util_peer_bonding_good(
        ((uint8_t[16]){ 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 }),
        0,
        0x1234,
        0x5678);

    /* Authenticated. */
    ble_l2cap_sm_test_util_peer_bonding_good(
        ((uint8_t[16]){ 2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17 }),
        1,
        0x4325,
        0x543892375);
}

TEST_CASE(ble_l2cap_sm_test_case_peer_bonding_bad)
{
    ble_l2cap_sm_test_util_peer_bonding_bad(0x5684, 32);
    ble_l2cap_sm_test_util_peer_bonding_bad(54325, 65437);
}

/*****************************************************************************
 * $us                                                                       *
 *****************************************************************************/

static void
ble_l2cap_sm_test_util_us_fail_inval(
    uint8_t *init_addr,
    uint8_t *rsp_addr,
    struct ble_l2cap_sm_pair_cmd *pair_req,
    struct ble_l2cap_sm_pair_cmd *pair_rsp,
    struct ble_l2cap_sm_pair_fail *pair_fail)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_l2cap_sm_test_util_init();
    ble_hs_test_util_set_public_addr(rsp_addr);

    conn = ble_hs_test_util_create_conn(2, init_addr,
                                        ble_l2cap_sm_test_util_conn_cb,
                                        NULL);

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Initiate the pairing procedure. */
    rc = ble_hs_test_util_security_initiate(2, 0);
    TEST_ASSERT_FATAL(rc == 0);

    /* Ensure we sent the expected pair request. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_req(pair_req);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive a pair response from the peer. */
    ble_l2cap_sm_test_util_rx_pair_rsp(
        conn, pair_rsp, BLE_HS_SM_US_ERR(BLE_L2CAP_SM_ERR_INVAL));
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Ensure we sent the expected pair fail. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_fail(pair_fail);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Verify that security callback was not executed. */
    //TEST_ASSERT(ble_l2cap_sm_test_gap_event == -1);
    //TEST_ASSERT(ble_l2cap_sm_test_gap_status == -1);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(!conn->bhc_sec_state.authenticated);
}

TEST_CASE(ble_l2cap_sm_test_case_us_fail_inval)
{
    /* Invalid IO capabiltiies. */
    ble_l2cap_sm_test_util_us_fail_inval(
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        } }),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x14,
            .oob_data_flag = 0,
            .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );

    /* Invalid OOB flag. */
    ble_l2cap_sm_test_util_us_fail_inval(
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        } }),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 2,
            .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );

    /* Invalid authreq - reserved bonding flag. */
    ble_l2cap_sm_test_util_us_fail_inval(
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        } }),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x2,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );

    /* Invalid authreq - reserved other flag. */
    ble_l2cap_sm_test_util_us_fail_inval(
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        } }),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x20,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );

    /* Invalid key size - too small. */
    ble_l2cap_sm_test_util_us_fail_inval(
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        } }),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x5,
            .max_enc_key_size = 6,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );

    /* Invalid key size - too large. */
    ble_l2cap_sm_test_util_us_fail_inval(
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        } }),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x5,
            .max_enc_key_size = 17,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );

    /* Invalid init key dist. */
    ble_l2cap_sm_test_util_us_fail_inval(
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        } }),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x5,
            .max_enc_key_size = 16,
            .init_key_dist = 0x10,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );

    /* Invalid resp key dist. */
    ble_l2cap_sm_test_util_us_fail_inval(
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        } }),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x5,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x10,
        } }),
        ((struct ble_l2cap_sm_pair_fail[1]) { {
            .reason = BLE_L2CAP_SM_ERR_INVAL,
        } })
    );
}

static void
ble_l2cap_sm_test_util_us_lgcy_good(
    uint8_t *init_addr,
    uint8_t *rsp_addr,
    struct ble_l2cap_sm_pair_cmd *pair_req,
    struct ble_l2cap_sm_pair_cmd *pair_rsp,
    struct ble_l2cap_sm_pair_confirm *confirm_req,
    struct ble_l2cap_sm_pair_confirm *confirm_rsp,
    struct ble_l2cap_sm_pair_random *random_req,
    struct ble_l2cap_sm_pair_random *random_rsp,
    int pair_alg,
    uint8_t *tk,
    uint8_t *stk,
    uint64_t r,
    uint16_t ediv)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_l2cap_sm_test_util_init();
    ble_hs_test_util_set_public_addr(init_addr);
    ble_l2cap_sm_dbg_set_next_pair_rand(random_req->value);
    ble_l2cap_sm_dbg_set_next_ediv(ediv);
    ble_l2cap_sm_dbg_set_next_start_rand(r);

    conn = ble_hs_test_util_create_conn(2, rsp_addr,
                                        ble_l2cap_sm_test_util_conn_cb,
                                        NULL);

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Initiate the pairing procedure. */
    rc = ble_hs_test_util_security_initiate(2, 0);
    TEST_ASSERT_FATAL(rc == 0);

    /* Ensure we sent the expected pair request. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_req(pair_req);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive a pair response from the peer. */
    ble_l2cap_sm_test_util_rx_pair_rsp(conn, pair_rsp, 0);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair confirm. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_confirm(confirm_req);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive a pair confirm from the peer. */
    ble_l2cap_sm_test_util_rx_confirm(conn, confirm_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair random. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_pair_random(random_req);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive a pair random from the peer. */
    ble_l2cap_sm_test_util_rx_random(conn, random_rsp, 0);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Ensure keys are distributed, if necessary. */

    /* Ensure we sent the expected start encryption command. */
    ble_hs_test_util_tx_all();
    ble_l2cap_sm_test_util_verify_tx_start_enc(2, r, ediv, stk);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Receive an encryption changed event. */
    ble_l2cap_sm_test_util_rx_enc_change(2, 0, 1);

    /* Pairing should now be complete. */
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 0);

    /* Verify that security callback was executed. */
    TEST_ASSERT(ble_l2cap_sm_test_gap_event == BLE_GAP_EVENT_SECURITY);
    TEST_ASSERT(ble_l2cap_sm_test_gap_status == 0);
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.pair_alg == pair_alg);
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.enc_enabled);
    TEST_ASSERT(!ble_l2cap_sm_test_sec_state.authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.pair_alg ==
                conn->bhc_sec_state.pair_alg);
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.enc_enabled ==
                conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_l2cap_sm_test_sec_state.authenticated ==
                conn->bhc_sec_state.authenticated);
}

TEST_CASE(ble_l2cap_sm_test_case_us_lgcy_jw_good)
{
    ble_l2cap_sm_test_util_us_lgcy_good(
        ((uint8_t[]){0x06, 0x05, 0x04, 0x03, 0x02, 0x01}),
        ((uint8_t[]){0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a}),
        ((struct ble_l2cap_sm_pair_cmd[1]) { {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
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
                0x04, 0x4e, 0xaf, 0xce, 0x30, 0x79, 0x2c, 0x9e,
                0xa2, 0xeb, 0x53, 0x6a, 0xdf, 0xf7, 0x99, 0xb2,
            },
        } }),
        ((struct ble_l2cap_sm_pair_confirm[1]) { {
            .value = {
                0x04, 0x4e, 0xaf, 0xce, 0x30, 0x79, 0x2c, 0x9e,
                0xa2, 0xeb, 0x53, 0x6a, 0xdf, 0xf7, 0x99, 0xb2,
            },
        } }),
        ((struct ble_l2cap_sm_pair_random[1]) { {
            .value = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            },
        } }),
        ((struct ble_l2cap_sm_pair_random[1]) { {
            .value = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            },
        } }),
        BLE_L2CAP_SM_PAIR_ALG_JW,
        NULL,
        ((uint8_t[16]) {
            0x2e, 0x2b, 0x34, 0xca, 0x59, 0xfa, 0x4c, 0x88,
            0x3b, 0x2c, 0x8a, 0xef, 0xd4, 0x4b, 0xe9, 0x66,
        }),
        0,
        0
    );
}

TEST_CASE(ble_l2cap_sm_test_case_conn_broken)
{
    struct hci_disconn_complete disconn_evt;
    int rc;

    ble_l2cap_sm_test_util_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[6]){1,2,3,5,6,7}),
                                 ble_l2cap_sm_test_util_conn_cb, NULL);

    /* Initiate the pairing procedure. */
    rc = ble_hs_test_util_security_initiate(2, 0);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(ble_l2cap_sm_dbg_num_procs() == 1);

    /* Terminate the connection. */
    disconn_evt.connection_handle = 2;
    disconn_evt.status = 0;
    disconn_evt.reason = BLE_ERR_REM_USER_CONN_TERM;
    ble_gap_rx_disconn_complete(&disconn_evt);

    /* Verify security callback got called. */
    TEST_ASSERT(ble_l2cap_sm_test_gap_status == BLE_HS_ENOTCONN);
    TEST_ASSERT(!ble_l2cap_sm_test_sec_state.enc_enabled);
    TEST_ASSERT(!ble_l2cap_sm_test_sec_state.authenticated);
}

TEST_SUITE(ble_l2cap_sm_test_suite)
{
    ble_l2cap_sm_test_case_peer_fail_inval();
    ble_l2cap_sm_test_case_peer_lgcy_fail_confirm();
    ble_l2cap_sm_test_case_peer_lgcy_jw_good();
    ble_l2cap_sm_test_case_us_fail_inval();
    ble_l2cap_sm_test_case_us_lgcy_jw_good();
    ble_l2cap_sm_test_case_peer_bonding_good();
    ble_l2cap_sm_test_case_peer_bonding_bad();
    ble_l2cap_sm_test_case_conn_broken();
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
