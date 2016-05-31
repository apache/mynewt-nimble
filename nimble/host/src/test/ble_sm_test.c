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
#include "host/ble_sm.h"
#include "host/ble_hs_test.h"
#include "ble_hs_test_util.h"

#if NIMBLE_OPT(SM)

int ble_sm_test_gap_event;
int ble_sm_test_gap_status;
struct ble_gap_sec_state ble_sm_test_sec_state;

int ble_sm_test_store_obj_type;
union ble_store_key ble_sm_test_store_key;
union ble_store_value ble_sm_test_store_value;

static ble_store_read_fn ble_sm_test_util_store_read;
static ble_store_write_fn ble_sm_test_util_store_write;


/*****************************************************************************
 * $util                                                                     *
 *****************************************************************************/

struct ble_sm_test_lgcy_params {
    uint8_t init_addr[6];
    uint8_t rsp_addr[6];
    struct ble_sm_sec_req sec_req;
    struct ble_sm_pair_cmd pair_req;
    struct ble_sm_pair_cmd pair_rsp;
    struct ble_sm_pair_confirm confirm_req;
    struct ble_sm_pair_confirm confirm_rsp;
    struct ble_sm_pair_random random_req;
    struct ble_sm_pair_random random_rsp;
    struct ble_sm_enc_info enc_info_req;
    struct ble_sm_master_id master_id_req;
    struct ble_sm_enc_info enc_info_rsp;
    struct ble_sm_master_id master_id_rsp;
    int pair_alg;
    unsigned authenticated:1;
    uint8_t tk[16];
    uint8_t stk[16];
    uint64_t r;
    uint16_t ediv;

    struct ble_sm_passkey passkey;
    struct ble_sm_pair_fail pair_fail;

    unsigned has_sec_req:1;
    unsigned has_enc_info_req:1;
    unsigned has_enc_info_rsp:1;
    unsigned has_master_id_req:1;
    unsigned has_master_id_rsp:1;
};

struct ble_sm_test_sc_params {
    uint8_t init_addr[6];
    uint8_t rsp_addr[6];
    struct ble_sm_sec_req sec_req;
    struct ble_sm_pair_cmd pair_req;
    struct ble_sm_pair_cmd pair_rsp;
    struct ble_sm_pair_confirm confirm_req;
    struct ble_sm_pair_confirm confirm_rsp;
    struct ble_sm_pair_random random_req;
    struct ble_sm_pair_random random_rsp;
    struct ble_sm_public_key public_key_req;
    struct ble_sm_public_key public_key_rsp;
    struct ble_sm_dhkey_check dhkey_check_req;
    struct ble_sm_dhkey_check dhkey_check_rsp;
    int pair_alg;
    unsigned authenticated:1;
    uint8_t ltk[16];

    struct ble_sm_pair_fail pair_fail;

    unsigned has_sec_req:1;
    unsigned has_confirm_req:1;
    unsigned has_enc_info_req:1;
    unsigned has_enc_info_rsp:1;
    unsigned has_master_id_req:1;
    unsigned has_master_id_rsp:1;
};

#define BLE_SM_TEST_UTIL_HCI_HDR(handle, pb, len) \
    ((struct hci_data_hdr) {                            \
        .hdh_handle_pb_bc = ((handle)  << 0) |          \
                            ((pb)      << 12),          \
        .hdh_len = (len)                                \
    })

static int
ble_sm_test_util_store_read(int obj_type, union ble_store_key *key,
                                  union ble_store_value *val)
{
    ble_sm_test_store_obj_type = obj_type;
    ble_sm_test_store_key = *key;

    return ble_hs_test_util_store_read(obj_type, key, val);
}

static int
ble_sm_test_util_store_write(int obj_type, union ble_store_value *val)
{
    ble_sm_test_store_obj_type = obj_type;
    ble_sm_test_store_value = *val;

    return ble_hs_test_util_store_write(obj_type, val);
}

static void
ble_sm_test_util_init(void)
{
    ble_hs_test_util_init();
    ble_hs_test_util_store_init(10, 10, 10);
    ble_hs_cfg.store_read_cb = ble_sm_test_util_store_read;
    ble_hs_cfg.store_write_cb = ble_sm_test_util_store_write;

    ble_sm_test_store_obj_type = -1;
    ble_sm_test_gap_event = -1;
    ble_sm_test_gap_status = -1;

    memset(&ble_sm_test_sec_state, 0xff,
           sizeof ble_sm_test_sec_state);
}

struct ble_sm_test_ltk_info {
    uint8_t ltk[16];
    unsigned authenticated:1;
};

static int
ble_sm_test_util_conn_cb(int event, struct ble_gap_conn_ctxt *ctxt,
                               void *arg)
{
    struct ble_sm_passkey *passkey;
    int rc;

    switch (event) {
    case BLE_GAP_EVENT_ENC_CHANGE:
        ble_sm_test_gap_status = ctxt->enc_change.status;
        ble_sm_test_sec_state = ctxt->desc->sec_state;
        rc = 0;
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        passkey = arg;
        TEST_ASSERT_FATAL(passkey != NULL);

        rc = ble_sm_set_tk(ctxt->desc->conn_handle, passkey);
        break;

    default:
        return 0;
    }

    ble_sm_test_gap_event = event;

    return rc;
}

static void
ble_sm_test_util_rx_pair_cmd(uint16_t conn_handle, uint8_t op,
                                   struct ble_sm_pair_cmd *cmd,
                                   int rx_status)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int payload_len;
    int rc;

    hci_hdr = BLE_SM_TEST_UTIL_HCI_HDR(
        2, BLE_HCI_PB_FIRST_FLUSH,
        BLE_L2CAP_HDR_SZ + BLE_SM_HDR_SZ + BLE_SM_PAIR_CMD_SZ);

    om = ble_hs_misc_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    payload_len = BLE_SM_HDR_SZ + BLE_SM_PAIR_CMD_SZ;

    v = os_mbuf_extend(om, payload_len);
    TEST_ASSERT_FATAL(v != NULL);

    ble_sm_pair_cmd_write(v, payload_len, op == BLE_SM_OP_PAIR_REQ,
                                cmd);

    rc = ble_hs_test_util_l2cap_rx_first_frag(conn_handle, BLE_L2CAP_CID_SM,
                                              &hci_hdr, om);
    TEST_ASSERT(rc == rx_status);
}

static void
ble_sm_test_util_rx_pair_req(uint16_t conn_handle,
                                   struct ble_sm_pair_cmd *req,
                                   int rx_status)
{
    ble_sm_test_util_rx_pair_cmd(conn_handle, BLE_SM_OP_PAIR_REQ,
                                       req, rx_status);
}

static void
ble_sm_test_util_rx_pair_rsp(uint16_t conn_handle,
                                   struct ble_sm_pair_cmd *rsp,
                                   int rx_status)
{
    ble_sm_test_util_rx_pair_cmd(conn_handle, BLE_SM_OP_PAIR_RSP,
                                       rsp, rx_status);
}

static void
ble_sm_test_util_rx_confirm(uint16_t conn_handle,
                                  struct ble_sm_pair_confirm *cmd)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int payload_len;
    int rc;

    hci_hdr = BLE_SM_TEST_UTIL_HCI_HDR(
        2, BLE_HCI_PB_FIRST_FLUSH,
        BLE_L2CAP_HDR_SZ + BLE_SM_HDR_SZ + BLE_SM_PAIR_CONFIRM_SZ);

    om = ble_hs_misc_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    payload_len = BLE_SM_HDR_SZ + BLE_SM_PAIR_CONFIRM_SZ;

    v = os_mbuf_extend(om, payload_len);
    TEST_ASSERT_FATAL(v != NULL);

    ble_sm_pair_confirm_write(v, payload_len, cmd);

    rc = ble_hs_test_util_l2cap_rx_first_frag(conn_handle, BLE_L2CAP_CID_SM,
                                              &hci_hdr, om);
    TEST_ASSERT_FATAL(rc == 0);
}

static void
ble_sm_test_util_rx_random(uint16_t conn_handle,
                                 struct ble_sm_pair_random *cmd,
                                 int exp_status)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int payload_len;
    int rc;

    hci_hdr = BLE_SM_TEST_UTIL_HCI_HDR(
        2, BLE_HCI_PB_FIRST_FLUSH,
        BLE_L2CAP_HDR_SZ + BLE_SM_HDR_SZ + BLE_SM_PAIR_RANDOM_SZ);

    om = ble_hs_misc_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    payload_len = BLE_SM_HDR_SZ + BLE_SM_PAIR_RANDOM_SZ;

    v = os_mbuf_extend(om, payload_len);
    TEST_ASSERT_FATAL(v != NULL);

    ble_sm_pair_random_write(v, payload_len, cmd);

    rc = ble_hs_test_util_l2cap_rx_first_frag(conn_handle, BLE_L2CAP_CID_SM,
                                              &hci_hdr, om);
    TEST_ASSERT_FATAL(rc == exp_status);
}

static void
ble_sm_test_util_rx_sec_req(uint16_t conn_handle,
                            struct ble_sm_sec_req *cmd,
                            int exp_status)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int payload_len;
    int rc;

    hci_hdr = BLE_SM_TEST_UTIL_HCI_HDR(
        2, BLE_HCI_PB_FIRST_FLUSH,
        BLE_L2CAP_HDR_SZ + BLE_SM_HDR_SZ + BLE_SM_SEC_REQ_SZ);

    om = ble_hs_misc_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    payload_len = BLE_SM_HDR_SZ + BLE_SM_SEC_REQ_SZ;

    v = os_mbuf_extend(om, payload_len);
    TEST_ASSERT_FATAL(v != NULL);

    ble_sm_sec_req_write(v, payload_len, cmd);

    rc = ble_hs_test_util_l2cap_rx_first_frag(conn_handle, BLE_L2CAP_CID_SM,
                                              &hci_hdr, om);
    TEST_ASSERT_FATAL(rc == exp_status);
}

static void
ble_sm_test_util_rx_public_key(uint16_t conn_handle,
                                     struct ble_sm_public_key *cmd)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int payload_len;
    int rc;

    hci_hdr = BLE_SM_TEST_UTIL_HCI_HDR(
        2, BLE_HCI_PB_FIRST_FLUSH,
        BLE_L2CAP_HDR_SZ + BLE_SM_HDR_SZ + BLE_SM_PUBLIC_KEY_SZ);

    om = ble_hs_misc_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    payload_len = BLE_SM_HDR_SZ + BLE_SM_PUBLIC_KEY_SZ;

    v = os_mbuf_extend(om, payload_len);
    TEST_ASSERT_FATAL(v != NULL);

    ble_sm_public_key_write(v, payload_len, cmd);

    rc = ble_hs_test_util_l2cap_rx_first_frag(conn_handle, BLE_L2CAP_CID_SM,
                                              &hci_hdr, om);
    TEST_ASSERT_FATAL(rc == 0);
}

static void
ble_sm_test_util_rx_dhkey_check(uint16_t conn_handle,
                                      struct ble_sm_dhkey_check *cmd,
                                      int exp_status)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int payload_len;
    int rc;

    hci_hdr = BLE_SM_TEST_UTIL_HCI_HDR(
        2, BLE_HCI_PB_FIRST_FLUSH,
        BLE_L2CAP_HDR_SZ + BLE_SM_HDR_SZ + BLE_SM_DHKEY_CHECK_SZ);

    om = ble_hs_misc_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    payload_len = BLE_SM_HDR_SZ + BLE_SM_DHKEY_CHECK_SZ;

    v = os_mbuf_extend(om, payload_len);
    TEST_ASSERT_FATAL(v != NULL);

    ble_sm_dhkey_check_write(v, payload_len, cmd);

    rc = ble_hs_test_util_l2cap_rx_first_frag(conn_handle, BLE_L2CAP_CID_SM,
                                              &hci_hdr, om);
    TEST_ASSERT_FATAL(rc == exp_status);
}

static struct os_mbuf *
ble_sm_test_util_verify_tx_hdr(uint8_t sm_op, uint16_t payload_len)
{
    struct os_mbuf *om;

    om = ble_hs_test_util_prev_tx_dequeue();
    TEST_ASSERT_FATAL(om != NULL);

    TEST_ASSERT(OS_MBUF_PKTLEN(om) == BLE_SM_HDR_SZ + payload_len);
    TEST_ASSERT_FATAL(om->om_data[0] == sm_op);

    om->om_data += BLE_SM_HDR_SZ;
    om->om_len -= BLE_SM_HDR_SZ;

    return om;
}

static void
ble_sm_test_util_verify_tx_pair_cmd(
    uint8_t op,
    struct ble_sm_pair_cmd *exp_cmd)
{
    struct ble_sm_pair_cmd cmd;
    struct os_mbuf *om;

    om = ble_sm_test_util_verify_tx_hdr(op, BLE_SM_PAIR_CMD_SZ);
    ble_sm_pair_cmd_parse(om->om_data, om->om_len, &cmd);

    TEST_ASSERT(cmd.io_cap == exp_cmd->io_cap);
    TEST_ASSERT(cmd.oob_data_flag == exp_cmd->oob_data_flag);
    TEST_ASSERT(cmd.authreq == exp_cmd->authreq);
    TEST_ASSERT(cmd.max_enc_key_size == exp_cmd->max_enc_key_size);
    TEST_ASSERT(cmd.init_key_dist == exp_cmd->init_key_dist);
    TEST_ASSERT(cmd.resp_key_dist == exp_cmd->resp_key_dist);
}

static void
ble_sm_test_util_verify_tx_pair_req(
    struct ble_sm_pair_cmd *exp_req)
{
    ble_sm_test_util_verify_tx_pair_cmd(BLE_SM_OP_PAIR_REQ,
                                              exp_req);
}

static void
ble_sm_test_util_verify_tx_pair_rsp(
    struct ble_sm_pair_cmd *exp_rsp)
{
    ble_sm_test_util_verify_tx_pair_cmd(BLE_SM_OP_PAIR_RSP,
                                              exp_rsp);
}

static void
ble_sm_test_util_verify_tx_pair_confirm(
    struct ble_sm_pair_confirm *exp_cmd)
{
    struct ble_sm_pair_confirm cmd;
    struct os_mbuf *om;

    om = ble_sm_test_util_verify_tx_hdr(BLE_SM_OP_PAIR_CONFIRM,
                                              BLE_SM_PAIR_CONFIRM_SZ);
    ble_sm_pair_confirm_parse(om->om_data, om->om_len, &cmd);

    TEST_ASSERT(memcmp(cmd.value, exp_cmd->value, 16) == 0);
}

static void
ble_sm_test_util_verify_tx_pair_random(
    struct ble_sm_pair_random *exp_cmd)
{
    struct ble_sm_pair_random cmd;
    struct os_mbuf *om;

    om = ble_sm_test_util_verify_tx_hdr(BLE_SM_OP_PAIR_RANDOM,
                                              BLE_SM_PAIR_RANDOM_SZ);
    ble_sm_pair_random_parse(om->om_data, om->om_len, &cmd);

    TEST_ASSERT(memcmp(cmd.value, exp_cmd->value, 16) == 0);
}

static void
ble_sm_test_util_verify_tx_public_key(
    struct ble_sm_public_key *exp_cmd)
{
    struct ble_sm_public_key cmd;
    struct os_mbuf *om;

    ble_hs_test_util_tx_all();

    om = ble_sm_test_util_verify_tx_hdr(BLE_SM_OP_PAIR_PUBLIC_KEY,
                                              BLE_SM_PUBLIC_KEY_SZ);
    ble_sm_public_key_parse(om->om_data, om->om_len, &cmd);

    TEST_ASSERT(memcmp(cmd.x, exp_cmd->x, sizeof cmd.x) == 0);
    TEST_ASSERT(memcmp(cmd.y, exp_cmd->y, sizeof cmd.y) == 0);
}

static void
ble_sm_test_util_verify_tx_dhkey_check(
    struct ble_sm_dhkey_check *exp_cmd)
{
    struct ble_sm_dhkey_check cmd;
    struct os_mbuf *om;

    om = ble_sm_test_util_verify_tx_hdr(BLE_SM_OP_PAIR_DHKEY_CHECK,
                                              BLE_SM_DHKEY_CHECK_SZ);
    ble_sm_dhkey_check_parse(om->om_data, om->om_len, &cmd);

    TEST_ASSERT(memcmp(cmd.value, exp_cmd->value, 16) == 0);
}

static void
ble_sm_test_util_verify_tx_enc_info(
    struct ble_sm_enc_info *exp_cmd)
{
    struct ble_sm_enc_info cmd;
    struct os_mbuf *om;

    om = ble_sm_test_util_verify_tx_hdr(BLE_SM_OP_ENC_INFO,
                                              BLE_SM_ENC_INFO_SZ);
    ble_sm_enc_info_parse(om->om_data, om->om_len, &cmd);

    TEST_ASSERT(memcmp(cmd.ltk, exp_cmd->ltk, sizeof cmd.ltk) == 0);
}

static void
ble_sm_test_util_verify_tx_sec_req(struct ble_sm_sec_req *exp_cmd)
{
    struct ble_sm_sec_req cmd;
    struct os_mbuf *om;

    ble_hs_test_util_tx_all();

    om = ble_sm_test_util_verify_tx_hdr(BLE_SM_OP_SEC_REQ,
                                              BLE_SM_SEC_REQ_SZ);
    ble_sm_sec_req_parse(om->om_data, om->om_len, &cmd);

    TEST_ASSERT(cmd.authreq == exp_cmd->authreq);
}

static void
ble_sm_test_util_verify_tx_pair_fail(
    struct ble_sm_pair_fail *exp_cmd)
{
    struct ble_sm_pair_fail cmd;
    struct os_mbuf *om;

    om = ble_sm_test_util_verify_tx_hdr(BLE_SM_OP_PAIR_FAIL,
                                              BLE_SM_PAIR_FAIL_SZ);
    ble_sm_pair_fail_parse(om->om_data, om->om_len, &cmd);

    TEST_ASSERT(cmd.reason == exp_cmd->reason);
}

static void
ble_sm_test_util_rx_lt_key_req(uint16_t conn_handle, uint64_t r,
                                     uint16_t ediv)
{
    struct hci_le_lt_key_req evt;
    int rc;

    evt.subevent_code = BLE_HCI_LE_SUBEV_LT_KEY_REQ;
    evt.connection_handle = conn_handle;
    evt.random_number = r;
    evt.encrypted_diversifier = ediv;

    rc = ble_sm_rx_lt_key_req(&evt);
    TEST_ASSERT_FATAL(rc == 0);
}

static void
ble_sm_test_util_verify_tx_lt_key_req_reply(uint16_t conn_handle,
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
ble_sm_test_util_verify_tx_lt_key_req_neg_reply(uint16_t conn_handle)
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
ble_sm_test_util_set_lt_key_req_reply_ack(uint8_t status,
                                                uint16_t conn_handle)
{
    static uint8_t params[BLE_HCI_LT_KEY_REQ_REPLY_ACK_PARAM_LEN];

    htole16(params, conn_handle);
    ble_hs_test_util_set_ack_params(
        host_hci_opcode_join(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_LT_KEY_REQ_REPLY),
        status, params, sizeof params);
}

static void
ble_sm_test_util_rx_enc_change(uint16_t conn_handle, uint8_t status,
                                     uint8_t encryption_enabled)
{
    struct hci_encrypt_change evt;

    evt.status = status;
    evt.encryption_enabled = encryption_enabled;
    evt.connection_handle = conn_handle;

    ble_sm_rx_encryption_change(&evt);
}

static void
ble_sm_test_util_verify_tx_start_enc(uint16_t conn_handle,
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
ble_sm_test_util_peer_fail_inval(
    int we_are_master,
    uint8_t *init_addr,
    uint8_t *rsp_addr,
    struct ble_sm_pair_cmd *pair_req,
    struct ble_sm_pair_fail *pair_fail)
{
    struct ble_hs_conn *conn;

    ble_sm_test_util_init();
    ble_hs_test_util_set_public_addr(rsp_addr);

    ble_hs_test_util_create_conn(2, init_addr, ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object without locking
     * the host mutex.  It is not OK for real code to do this, but this test
     * can assume the connection list is unchanging.
     */
    ble_hs_lock();
    conn = ble_hs_conn_find(2);
    TEST_ASSERT_FATAL(conn != NULL);
    ble_hs_unlock();

    if (!we_are_master) {
        conn->bhc_flags &= ~BLE_HS_CONN_F_MASTER;
    }

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Receive a pair request from the peer. */
    ble_sm_test_util_rx_pair_req(2, pair_req,
                                 BLE_HS_SM_US_ERR(pair_fail->reason));
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Ensure we sent the expected pair fail. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_fail(pair_fail);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Verify that security callback was not executed. */
    TEST_ASSERT(ble_sm_test_gap_event == -1);
    TEST_ASSERT(ble_sm_test_gap_status == -1);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(!conn->bhc_sec_state.authenticated);
}

TEST_CASE(ble_sm_test_case_peer_fail_inval)
{
    /* Invalid role detected before other arguments. */
    ble_sm_test_util_peer_fail_inval(
        1,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_sm_pair_cmd[1]) { {
            .io_cap = 0x14,
            .oob_data_flag = 0,
            .authreq = 0x12,
            .max_enc_key_size = 20,
            .init_key_dist = 0x0b,
            .resp_key_dist = 0x11,
        } }),
        ((struct ble_sm_pair_fail[1]) { {
            .reason = BLE_SM_ERR_CMD_NOT_SUPP,
        } })
    );

    /* Invalid IO capabiltiies. */
    ble_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_sm_pair_cmd[1]) { {
            .io_cap = 0x14,
            .oob_data_flag = 0,
            .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_sm_pair_fail[1]) { {
            .reason = BLE_SM_ERR_INVAL,
        } })
    );

    /* Invalid OOB flag. */
    ble_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 2,
            .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_sm_pair_fail[1]) { {
            .reason = BLE_SM_ERR_INVAL,
        } })
    );

    /* Invalid authreq - reserved bonding flag. */
    ble_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x2,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_sm_pair_fail[1]) { {
            .reason = BLE_SM_ERR_INVAL,
        } })
    );

    /* Invalid authreq - reserved other flag. */
    ble_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x20,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_sm_pair_fail[1]) { {
            .reason = BLE_SM_ERR_INVAL,
        } })
    );

    /* Invalid key size - too small. */
    ble_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x5,
            .max_enc_key_size = 6,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_sm_pair_fail[1]) { {
            .reason = BLE_SM_ERR_INVAL,
        } })
    );

    /* Invalid key size - too large. */
    ble_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x5,
            .max_enc_key_size = 17,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_sm_pair_fail[1]) { {
            .reason = BLE_SM_ERR_INVAL,
        } })
    );

    /* Invalid init key dist. */
    ble_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x5,
            .max_enc_key_size = 16,
            .init_key_dist = 0x10,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_sm_pair_fail[1]) { {
            .reason = BLE_SM_ERR_INVAL,
        } })
    );

    /* Invalid resp key dist. */
    ble_sm_test_util_peer_fail_inval(
        0,
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x5,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x10,
        } }),
        ((struct ble_sm_pair_fail[1]) { {
            .reason = BLE_SM_ERR_INVAL,
        } })
    );
}

static void
ble_sm_test_util_peer_lgcy_fail_confirm(
    uint8_t *init_addr,
    uint8_t *rsp_addr,
    struct ble_sm_pair_cmd *pair_req,
    struct ble_sm_pair_cmd *pair_rsp,
    struct ble_sm_pair_confirm *confirm_req,
    struct ble_sm_pair_confirm *confirm_rsp,
    struct ble_sm_pair_random *random_req,
    struct ble_sm_pair_random *random_rsp,
    struct ble_sm_pair_fail *fail_rsp)
{
    struct ble_hs_conn *conn;

    ble_sm_test_util_init();
    ble_hs_test_util_set_public_addr(rsp_addr);
    ble_sm_dbg_set_next_pair_rand(random_rsp->value);

    ble_hs_test_util_create_conn(2, init_addr, ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object without locking
     * the host mutex.  It is not OK for real code to do this, but this test
     * can assume the connection list is unchanging.
     */
    ble_hs_lock();
    conn = ble_hs_conn_find(2);
    TEST_ASSERT_FATAL(conn != NULL);
    ble_hs_unlock();

    /* Peer is the initiator so we must be the slave. */
    conn->bhc_flags &= ~BLE_HS_CONN_F_MASTER;

    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Receive a pair request from the peer. */
    ble_sm_test_util_rx_pair_req(2, pair_req, 0);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair response. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_rsp(pair_rsp);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive a pair confirm from the peer. */
    ble_sm_test_util_rx_confirm(2, confirm_req);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair confirm. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_confirm(confirm_rsp);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive a pair random from the peer. */
    ble_sm_test_util_rx_random(
        2, random_req, BLE_HS_SM_US_ERR(BLE_SM_ERR_CONFIRM_MISMATCH));

    /* Ensure we sent the expected pair fail. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_fail(fail_rsp);

    /* The proc should now be freed. */
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Verify that security callback was executed. */
    TEST_ASSERT(ble_sm_test_gap_event == BLE_GAP_EVENT_ENC_CHANGE);
    TEST_ASSERT(ble_sm_test_gap_status ==
                BLE_HS_SM_US_ERR(BLE_SM_ERR_CONFIRM_MISMATCH));
    TEST_ASSERT(ble_sm_test_sec_state.pair_alg ==
                BLE_SM_PAIR_ALG_JW);
    TEST_ASSERT(!ble_sm_test_sec_state.enc_enabled);
    TEST_ASSERT(!ble_sm_test_sec_state.authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_sm_test_sec_state.pair_alg ==
                conn->bhc_sec_state.pair_alg);
    TEST_ASSERT(ble_sm_test_sec_state.enc_enabled ==
                conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                conn->bhc_sec_state.authenticated);
}

TEST_CASE(ble_sm_test_case_peer_lgcy_fail_confirm)
{
    ble_sm_test_util_peer_lgcy_fail_confirm(
        ((uint8_t[]){0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c}),
        ((uint8_t[]){0x03, 0x02, 0x01, 0x50, 0x13, 0x00}),
        ((struct ble_sm_pair_cmd[1]) { {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        } }),
        ((struct ble_sm_pair_cmd[1]) { {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        } }),
        ((struct ble_sm_pair_confirm[1]) { {
            .value = {
                0x0a, 0xac, 0xa2, 0xae, 0xa6, 0x98, 0xdc, 0x6d,
                0x65, 0x84, 0x11, 0x69, 0x47, 0x36, 0x8d, 0xa0,
            },
        } }),
        ((struct ble_sm_pair_confirm[1]) { {
            .value = {
                0x45, 0xd2, 0x2c, 0x38, 0xd8, 0x91, 0x4f, 0x19,
                0xa2, 0xd4, 0xfc, 0x7d, 0xad, 0x37, 0x79, 0xe0
            },
        } }),
        ((struct ble_sm_pair_random[1]) { {
            .value = {
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            },
        } }),
        ((struct ble_sm_pair_random[1]) { {
            .value = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            },
        } }),
        ((struct ble_sm_pair_fail[1]) { {
            .reason = BLE_SM_ERR_CONFIRM_MISMATCH,
        } })
    );
}

static void
ble_sm_test_util_peer_lgcy_good(
    struct ble_sm_test_lgcy_params *params)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_sm_test_util_init();

    ble_hs_cfg.sm_io_cap = params->pair_rsp.io_cap;
    ble_hs_cfg.sm_oob_data_flag = params->pair_rsp.oob_data_flag;
    ble_hs_cfg.sm_bonding = !!(params->pair_rsp.authreq &
                               BLE_SM_PAIR_AUTHREQ_BOND);
    ble_hs_cfg.sm_mitm = !!(params->pair_rsp.authreq &
                            BLE_SM_PAIR_AUTHREQ_MITM);
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_keypress = !!(params->pair_rsp.authreq &
                                BLE_SM_PAIR_AUTHREQ_KEYPRESS);
    ble_hs_cfg.sm_our_key_dist = params->pair_rsp.resp_key_dist;
    ble_hs_cfg.sm_their_key_dist = params->pair_rsp.init_key_dist;

    ble_hs_test_util_set_public_addr(params->rsp_addr);
    ble_sm_dbg_set_next_pair_rand(params->random_rsp.value);
    ble_sm_dbg_set_next_ediv(params->ediv);
    ble_sm_dbg_set_next_start_rand(params->r);

    if (params->has_enc_info_req) {
        ble_sm_dbg_set_next_ltk(params->enc_info_req.ltk);
    }

    ble_hs_test_util_create_conn(2, params->init_addr,
                                 ble_sm_test_util_conn_cb,
                                 &params->passkey);

    /* This test inspects and modifies the connection object without locking
     * the host mutex.  It is not OK for real code to do this, but this test
     * can assume the connection list is unchanging.
     */
    ble_hs_lock();
    conn = ble_hs_conn_find(2);
    TEST_ASSERT_FATAL(conn != NULL);
    ble_hs_unlock();

    /* Peer is the initiator so we must be the slave. */
    conn->bhc_flags &= ~BLE_HS_CONN_F_MASTER;

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    if (params->has_sec_req) {
        rc = ble_sm_slave_initiate(2);
        TEST_ASSERT(rc == 0);

        /* Ensure we sent the expected security request. */
        ble_sm_test_util_verify_tx_sec_req(&params->sec_req);
    }

    /* Receive a pair request from the peer. */
    ble_sm_test_util_rx_pair_req(2, &params->pair_req, 0);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair response. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_rsp(&params->pair_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive a pair confirm from the peer. */
    ble_sm_test_util_rx_confirm(2, &params->confirm_req);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair confirm. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_confirm(&params->confirm_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive a pair random from the peer. */
    ble_sm_test_util_rx_random(2, &params->random_req, 0);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair random. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_random(&params->random_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive a long term key request from the controller. */
    ble_sm_test_util_set_lt_key_req_reply_ack(0, 2);
    ble_sm_test_util_rx_lt_key_req(2, params->r, params->ediv);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected long term key request reply command. */
    ble_sm_test_util_verify_tx_lt_key_req_reply(2, params->stk);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive an encryption changed event. */
    ble_sm_test_util_rx_enc_change(2, 0, 1);

    if (params->has_enc_info_req) {
        return; // XXX
    }

    /* Pairing should now be complete. */
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Verify that security callback was executed. */
    TEST_ASSERT(ble_sm_test_gap_event == BLE_GAP_EVENT_ENC_CHANGE);
    TEST_ASSERT(ble_sm_test_gap_status == 0);
    TEST_ASSERT(ble_sm_test_sec_state.pair_alg == params->pair_alg);
    TEST_ASSERT(ble_sm_test_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                params->authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_sm_test_sec_state.pair_alg ==
                conn->bhc_sec_state.pair_alg);
    TEST_ASSERT(ble_sm_test_sec_state.enc_enabled ==
                conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                conn->bhc_sec_state.authenticated);
}

TEST_CASE(ble_sm_test_case_peer_lgcy_jw_good)
{
    struct ble_sm_test_lgcy_params params;

    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c},
        .rsp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        },
        .confirm_req = (struct ble_sm_pair_confirm) {
            .value = {
                0x0a, 0xac, 0xa2, 0xae, 0xa6, 0x98, 0xdc, 0x6d,
                0x65, 0x84, 0x11, 0x69, 0x47, 0x36, 0x8d, 0xa0,
            },
        },
        .confirm_rsp = (struct ble_sm_pair_confirm) {
            .value = {
                0x45, 0xd2, 0x2c, 0x38, 0xd8, 0x91, 0x4f, 0x19,
                0xa2, 0xd4, 0xfc, 0x7d, 0xad, 0x37, 0x79, 0xe0
            },
        },
        .random_req = (struct ble_sm_pair_random) {
            .value = {
                0x2b, 0x3b, 0x69, 0xe4, 0xef, 0xab, 0xcc, 0x48,
                0x78, 0x20, 0x1a, 0x54, 0x7a, 0x91, 0x5d, 0xfb,
            },
        },
        .random_rsp = (struct ble_sm_pair_random) {
            .value = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            },
        },
        .pair_alg = BLE_SM_PAIR_ALG_JW,
        .authenticated = 0,
        .tk = { 0 },
        .stk = {
            0xa4, 0x8e, 0x51, 0x0d, 0x33, 0xe7, 0x8f, 0x38,
            0x45, 0xf0, 0x67, 0xc3, 0xd4, 0x05, 0xb3, 0xe6,
        },
        .r = 0,
        .ediv = 0,
    };
    ble_sm_test_util_peer_lgcy_good(&params);
}

TEST_CASE(ble_sm_test_case_peer_lgcy_passkey_good)
{
    struct ble_sm_test_lgcy_params params;

    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c},
        .rsp_addr = {0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07},
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 0x04,
            .oob_data_flag = 0, .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x02,
            .oob_data_flag = 0,
            .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x01,
            .resp_key_dist = 0x01,
        },
        .confirm_req = (struct ble_sm_pair_confirm) {
            .value = {
                0x54, 0xed, 0x7c, 0x65, 0xc5, 0x3a, 0xee, 0x87,
                0x8e, 0xf8, 0x04, 0xd8, 0x93, 0xb0, 0xfa, 0xa4,
            },
        },
        .confirm_rsp = (struct ble_sm_pair_confirm) {
            .value = {
                0xdf, 0x96, 0x88, 0x73, 0x49, 0x24, 0x3f, 0xe8,
                0xb0, 0xaf, 0xb3, 0xf6, 0xc8, 0xf4, 0xe2, 0x36,
            },
        },
        .random_req = (struct ble_sm_pair_random) {
            .value = {
                0x4d, 0x2c, 0xf2, 0xb7, 0x11, 0x56, 0xbd, 0x4f,
                0xfc, 0xde, 0xa9, 0x86, 0x4d, 0xfd, 0x77, 0x03,
            },
        },
        .random_rsp = {
            .value = {
                0x12, 0x45, 0x65, 0x2c, 0x85, 0x56, 0x32, 0x8f,
                0xf4, 0x7f, 0x44, 0xd0, 0x17, 0x35, 0x41, 0xed
            },
        },
        .pair_alg = BLE_SM_PAIR_ALG_PASSKEY,
        .authenticated = 1,
        .tk = {
            0x5a, 0x7f, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        },
        .stk = {
            0x2b, 0x9c, 0x1e, 0x42, 0xa8, 0xcb, 0xab, 0xd1,
            0x4b, 0xde, 0x50, 0x05, 0x50, 0xd9, 0x95, 0xc6
        },
        .r = 4107344270811490869,
        .ediv = 61621,

        .passkey = {
            .action = BLE_SM_PKACT_INPUT,
            .passkey = 884570,
        },
        .enc_info_req = {
            .ltk = {
                0x2b, 0x9c, 0x1e, 0x42, 0xa8, 0xcb, 0xab, 0xd1,
                0x4b, 0xde, 0x50, 0x05, 0x50, 0xd9, 0x95, 0xc6
            },
        },
        .has_enc_info_req = 1,

        .master_id_req = {
            .ediv = 61621,
            .rand_val = 4107344270811490869,
        },
        .has_master_id_req = 1,

        .enc_info_rsp = {
            .ltk = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 }
        },
        .has_enc_info_rsp = 1,

        .master_id_rsp = {
            .ediv = 61621,
            .rand_val = 4107344270811490869,
        },
        .has_master_id_rsp = 1,
    };
    ble_sm_test_util_peer_lgcy_good(&params);
}

/**
 * @param send_enc_req          Whether this procedure is initiated by a slave
 *                                  security request;
 *                                  1: We send a security request at start.
 *                                  0: No security request; peer initiates.
 */
static void
ble_sm_test_util_peer_bonding_good(int send_enc_req, uint8_t *ltk,
                                         int authenticated,
                                         uint16_t ediv, uint64_t rand_num)
{
    struct ble_store_value_sec value_sec;
    struct ble_hs_conn *conn;
    int rc;

    ble_sm_test_util_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[6]){1,2,3,4,5,6}),
                                 ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object without locking
     * the host mutex.  It is not OK for real code to do this, but this test
     * can assume the connection list is unchanging.
     */
    ble_hs_lock();
    conn = ble_hs_conn_find(2);
    TEST_ASSERT_FATAL(conn != NULL);
    conn->bhc_flags &= ~BLE_HS_CONN_F_MASTER;
    ble_hs_unlock();

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Populate the SM database with an LTK for this peer. */
    value_sec.peer_addr_type = conn->bhc_addr_type;
    memcpy(value_sec.peer_addr, conn->bhc_addr, sizeof value_sec.peer_addr);
    value_sec.ediv = ediv;
    value_sec.rand_num = rand_num;
    memcpy(value_sec.ltk, ltk, sizeof value_sec.ltk);
    value_sec.authenticated = authenticated;
    value_sec.sc = 0;

    rc = ble_store_write_slv_sec(&value_sec);
    TEST_ASSERT_FATAL(rc == 0);

    if (send_enc_req) {
        rc = ble_sm_slave_initiate(2);
        TEST_ASSERT(rc == 0);
    }

    /* Receive a long term key request from the controller. */
    ble_sm_test_util_set_lt_key_req_reply_ack(0, 2);
    ble_sm_test_util_rx_lt_key_req(2, rand_num, ediv);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);

    /* Ensure the LTK request event got sent to the application. */
    TEST_ASSERT(ble_sm_test_store_obj_type ==
                BLE_STORE_OBJ_TYPE_SLV_SEC);
    TEST_ASSERT(ble_sm_test_store_key.sec.peer_addr_type ==
                BLE_STORE_ADDR_TYPE_NONE);
    TEST_ASSERT(ble_sm_test_store_key.sec.ediv_rand_present);
    TEST_ASSERT(ble_sm_test_store_key.sec.ediv == ediv);
    TEST_ASSERT(ble_sm_test_store_key.sec.rand_num == rand_num);

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected long term key request reply command. */
    ble_sm_test_util_verify_tx_lt_key_req_reply(2, value_sec.ltk);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive an encryption changed event. */
    ble_sm_test_util_rx_enc_change(2, 0, 1);

    /* Pairing should now be complete. */
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Verify that security callback was executed. */
    TEST_ASSERT(ble_sm_test_gap_event == BLE_GAP_EVENT_ENC_CHANGE);
    TEST_ASSERT(ble_sm_test_gap_status == 0);
    TEST_ASSERT(ble_sm_test_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_sm_test_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                authenticated);
}

static void
ble_sm_test_util_peer_bonding_bad(uint16_t ediv, uint64_t rand_num)
{
    struct ble_hs_conn *conn;

    ble_sm_test_util_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[6]){1,2,3,4,5,6}),
                                 ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object without locking
     * the host mutex.  It is not OK for real code to do this, but this test
     * can assume the connection list is unchanging.
     */
    ble_hs_lock();
    conn = ble_hs_conn_find(2);
    TEST_ASSERT_FATAL(conn != NULL);
    conn->bhc_flags &= ~BLE_HS_CONN_F_MASTER;
    ble_hs_unlock();

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Receive a long term key request from the controller. */
    ble_sm_test_util_set_lt_key_req_reply_ack(0, 2);
    ble_sm_test_util_rx_lt_key_req(2, rand_num, ediv);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);

    /* Ensure the LTK request event got sent to the application. */
    TEST_ASSERT(ble_sm_test_store_obj_type ==
                BLE_STORE_OBJ_TYPE_SLV_SEC);
    TEST_ASSERT(ble_sm_test_store_key.sec.ediv_rand_present);
    TEST_ASSERT(ble_sm_test_store_key.sec.ediv == ediv);
    TEST_ASSERT(ble_sm_test_store_key.sec.rand_num == rand_num);

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);

    /* Ensure we sent the expected long term key request neg reply command. */
    ble_sm_test_util_verify_tx_lt_key_req_neg_reply(2);

    /* Ensure the security procedure was aborted. */
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(!conn->bhc_sec_state.authenticated);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);
}

TEST_CASE(ble_sm_test_case_peer_bonding_good)
{
    /* Unauthenticated. */
    ble_sm_test_util_peer_bonding_good(
        0,
        ((uint8_t[16]){ 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 }),
        0,
        0x1234,
        0x5678);

    /* Authenticated. */
    ble_sm_test_util_peer_bonding_good(
        0,
        ((uint8_t[16]){ 2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17 }),
        1,
        0x4325,
        0x543892375);
}

TEST_CASE(ble_sm_test_case_peer_bonding_bad)
{
    ble_sm_test_util_peer_bonding_bad(0x5684, 32);
    ble_sm_test_util_peer_bonding_bad(54325, 65437);
}

/*****************************************************************************
 * $us                                                                       *
 *****************************************************************************/

static void
ble_sm_test_util_us_fail_inval(
    struct ble_sm_test_lgcy_params *params)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_sm_test_util_init();
    ble_hs_test_util_set_public_addr(params->rsp_addr);

    ble_sm_dbg_set_next_pair_rand(((uint8_t[16]){0}));

    ble_hs_test_util_create_conn(2, params->init_addr,
                                 ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object without locking
     * the host mutex.  It is not OK for real code to do this, but this test
     * can assume the connection list is unchanging.
     */
    ble_hs_lock();
    conn = ble_hs_conn_find(2);
    TEST_ASSERT_FATAL(conn != NULL);
    ble_hs_unlock();

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Initiate the pairing procedure. */
    rc = ble_hs_test_util_security_initiate(2, 0);
    TEST_ASSERT_FATAL(rc == 0);

    /* Ensure we sent the expected pair request. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_req(&params->pair_req);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive a pair response from the peer. */
    ble_sm_test_util_rx_pair_rsp(
        2, &params->pair_rsp, BLE_HS_SM_US_ERR(BLE_SM_ERR_INVAL));
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Ensure we sent the expected pair fail. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_fail(&params->pair_fail);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Verify that security callback was not executed. */
    //TEST_ASSERT(ble_sm_test_gap_event == -1);
    //TEST_ASSERT(ble_sm_test_gap_status == -1);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(!conn->bhc_sec_state.authenticated);
}

TEST_CASE(ble_sm_test_case_us_fail_inval)
{
    struct ble_sm_test_lgcy_params params;

    /* Invalid IO capabiltiies. */
    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c},
        .rsp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x14,
            .oob_data_flag = 0,
            .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        },
        .pair_fail = (struct ble_sm_pair_fail) {
            .reason = BLE_SM_ERR_INVAL,
        },
    };
    ble_sm_test_util_us_fail_inval(&params);

    /* Invalid OOB flag. */
    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c},
        .rsp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x14,
            .oob_data_flag = 2,
            .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        },
        .pair_fail = (struct ble_sm_pair_fail) {
            .reason = BLE_SM_ERR_INVAL,
        },
    };
    ble_sm_test_util_us_fail_inval(&params);

    /* Invalid authreq - reserved bonding flag. */
    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c},
        .rsp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x02,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        },
        .pair_fail = (struct ble_sm_pair_fail) {
            .reason = BLE_SM_ERR_INVAL,
        },
    };
    ble_sm_test_util_us_fail_inval(&params);

    /* Invalid authreq - reserved other flag. */
    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c},
        .rsp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x20,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        },
        .pair_fail = (struct ble_sm_pair_fail) {
            .reason = BLE_SM_ERR_INVAL,
        },
    };
    ble_sm_test_util_us_fail_inval(&params);

    /* Invalid key size - too small. */
    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c},
        .rsp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x05,
            .max_enc_key_size = 6,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        },
        .pair_fail = (struct ble_sm_pair_fail) {
            .reason = BLE_SM_ERR_INVAL,
        },
    };
    ble_sm_test_util_us_fail_inval(&params);

    /* Invalid key size - too large. */
    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c},
        .rsp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x05,
            .max_enc_key_size = 17,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        },
        .pair_fail = (struct ble_sm_pair_fail) {
            .reason = BLE_SM_ERR_INVAL,
        },
    };
    ble_sm_test_util_us_fail_inval(&params);

    /* Invalid init key dist. */
    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c},
        .rsp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x05,
            .max_enc_key_size = 17,
            .init_key_dist = 0x10,
            .resp_key_dist = 0x07,
        },
        .pair_fail = (struct ble_sm_pair_fail) {
            .reason = BLE_SM_ERR_INVAL,
        },
    };
    ble_sm_test_util_us_fail_inval(&params);

    /* Invalid resp key dist. */
    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c},
        .rsp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x05,
            .max_enc_key_size = 17,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x10,
        },
        .pair_fail = (struct ble_sm_pair_fail) {
            .reason = BLE_SM_ERR_INVAL,
        },
    };
    ble_sm_test_util_us_fail_inval(&params);
}

static void
ble_sm_test_util_us_lgcy_good(
    struct ble_sm_test_lgcy_params *params)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_sm_test_util_init();
    ble_hs_test_util_set_public_addr(params->init_addr);
    ble_sm_dbg_set_next_pair_rand(params->random_req.value);
    ble_sm_dbg_set_next_ediv(params->ediv);
    ble_sm_dbg_set_next_start_rand(params->r);

    if (params->has_enc_info_req) {
        ble_sm_dbg_set_next_ltk(params->enc_info_req.ltk);
    }

    ble_hs_test_util_create_conn(2, params->rsp_addr,
                                 ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object without locking
     * the host mutex.  It is not OK for real code to do this, but this test
     * can assume the connection list is unchanging.
     */
    ble_hs_lock();
    conn = ble_hs_conn_find(2);
    TEST_ASSERT_FATAL(conn != NULL);
    ble_hs_unlock();

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    ble_hs_test_util_set_ack(
        host_hci_opcode_join(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_START_ENCRYPT), 0);
    if (params->has_sec_req) {
        ble_sm_test_util_rx_sec_req(2, &params->sec_req, 0);
    } else {
        /* Initiate the pairing procedure. */
        rc = ble_gap_security_initiate(2);
        TEST_ASSERT_FATAL(rc == 0);
    }

    /* Ensure we sent the expected pair request. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_req(&params->pair_req);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive a pair response from the peer. */
    ble_sm_test_util_rx_pair_rsp(2, &params->pair_rsp, 0);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair confirm. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_confirm(&params->confirm_req);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive a pair confirm from the peer. */
    ble_sm_test_util_rx_confirm(2, &params->confirm_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair random. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_random(&params->random_req);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive a pair random from the peer. */
    ble_sm_test_util_rx_random(2, &params->random_rsp, 0);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure keys are distributed, if necessary. */
    if (params->has_enc_info_req) {
        ble_sm_test_util_verify_tx_enc_info(&params->enc_info_req);
    }

    /* Ensure we sent the expected start encryption command. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_start_enc(2, params->r, params->ediv,
                                               params->stk);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive an encryption changed event. */
    ble_sm_test_util_rx_enc_change(2, 0, 1);

    /* Pairing should now be complete. */
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Verify that security callback was executed. */
    TEST_ASSERT(ble_sm_test_gap_event == BLE_GAP_EVENT_ENC_CHANGE);
    TEST_ASSERT(ble_sm_test_gap_status == 0);
    TEST_ASSERT(ble_sm_test_sec_state.pair_alg == params->pair_alg);
    TEST_ASSERT(ble_sm_test_sec_state.enc_enabled);
    TEST_ASSERT(!ble_sm_test_sec_state.authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_sm_test_sec_state.pair_alg ==
                conn->bhc_sec_state.pair_alg);
    TEST_ASSERT(ble_sm_test_sec_state.enc_enabled ==
                conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                conn->bhc_sec_state.authenticated);
}

TEST_CASE(ble_sm_test_case_us_lgcy_jw_good)
{
    struct ble_sm_test_lgcy_params params;

    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0x06, 0x05, 0x04, 0x03, 0x02, 0x01},
        .rsp_addr = {0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a},
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        },
        .confirm_req = (struct ble_sm_pair_confirm) {
            .value = {
                0x04, 0x4e, 0xaf, 0xce, 0x30, 0x79, 0x2c, 0x9e,
                0xa2, 0xeb, 0x53, 0x6a, 0xdf, 0xf7, 0x99, 0xb2,
            },
        },
        .confirm_rsp = (struct ble_sm_pair_confirm) {
            .value = {
                0x04, 0x4e, 0xaf, 0xce, 0x30, 0x79, 0x2c, 0x9e,
                0xa2, 0xeb, 0x53, 0x6a, 0xdf, 0xf7, 0x99, 0xb2,
            },
        },
        .random_req = (struct ble_sm_pair_random) {
            .value = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            },
        },
        .random_rsp = (struct ble_sm_pair_random) {
            .value = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            },
        },
        .pair_alg = BLE_SM_PAIR_ALG_JW,
        .tk = { 0 },
        .stk = {
            0x2e, 0x2b, 0x34, 0xca, 0x59, 0xfa, 0x4c, 0x88,
            0x3b, 0x2c, 0x8a, 0xef, 0xd4, 0x4b, 0xe9, 0x66,
        },
        .r = 0,
        .ediv = 0,
    };

    ble_sm_test_util_us_lgcy_good(&params);
}

TEST_CASE(ble_sm_test_case_conn_broken)
{
    struct hci_disconn_complete disconn_evt;
    int rc;

    ble_sm_test_util_init();

    ble_sm_dbg_set_next_pair_rand(((uint8_t[16]){0}));

    ble_hs_test_util_create_conn(2, ((uint8_t[6]){1,2,3,5,6,7}),
                                 ble_sm_test_util_conn_cb, NULL);

    /* Initiate the pairing procedure. */
    rc = ble_hs_test_util_security_initiate(2, 0);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Terminate the connection. */
    disconn_evt.connection_handle = 2;
    disconn_evt.status = 0;
    disconn_evt.reason = BLE_ERR_REM_USER_CONN_TERM;
    ble_gap_rx_disconn_complete(&disconn_evt);

    /* Verify security callback got called. */
    TEST_ASSERT(ble_sm_test_gap_status == BLE_HS_ENOTCONN);
    TEST_ASSERT(!ble_sm_test_sec_state.enc_enabled);
    TEST_ASSERT(!ble_sm_test_sec_state.authenticated);
}

TEST_CASE(ble_sm_test_case_peer_sec_req_inval)
{
    struct ble_sm_pair_fail fail;
    struct ble_sm_sec_req sec_req;
    int rc;

    ble_sm_test_util_init();

    ble_sm_dbg_set_next_pair_rand(((uint8_t[16]){0}));

    ble_hs_test_util_create_conn(2, ((uint8_t[6]){1,2,3,5,6,7}),
                                 ble_sm_test_util_conn_cb,
                                 NULL);

    /*** We are the slave; reject the security request. */
    ble_hs_atomic_conn_set_flags(2, BLE_HS_CONN_F_MASTER, 0);

    sec_req.authreq = 0;
    ble_sm_test_util_rx_sec_req(
        2, &sec_req, BLE_HS_SM_US_ERR(BLE_SM_ERR_CMD_NOT_SUPP));

    ble_hs_test_util_tx_all();

    fail.reason = BLE_SM_ERR_CMD_NOT_SUPP;
    ble_sm_test_util_verify_tx_pair_fail(&fail);

    /*** Pairing already in progress; ignore security request. */
    ble_hs_atomic_conn_set_flags(2, BLE_HS_CONN_F_MASTER, 1);
    rc = ble_sm_pair_initiate(2);
    TEST_ASSERT_FATAL(rc == 0);
    ble_hs_test_util_tx_all();
    ble_hs_test_util_prev_tx_queue_clear();

    ble_sm_test_util_rx_sec_req(2, &sec_req, BLE_HS_EALREADY);
    ble_hs_test_util_tx_all();
    TEST_ASSERT(ble_hs_test_util_prev_tx_queue_sz() == 0);
}

/**
 * Master: us.
 * Peer sends a security request.
 * We respond by initiating the pairing procedure.
 */
TEST_CASE(ble_sm_test_case_peer_sec_req_pair)
{
    struct ble_sm_test_lgcy_params params;

    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0x06, 0x05, 0x04, 0x03, 0x02, 0x01},
        .rsp_addr = {0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a},
        .sec_req = (struct ble_sm_sec_req) {
            .authreq = 0,
        },
        .has_sec_req = 1,
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 3,
            .oob_data_flag = 0,
            .authreq = 0,
            .max_enc_key_size = 16,
            .init_key_dist = 0,
            .resp_key_dist = 0,
        },
        .confirm_req = (struct ble_sm_pair_confirm) {
            .value = {
                0x04, 0x4e, 0xaf, 0xce, 0x30, 0x79, 0x2c, 0x9e,
                0xa2, 0xeb, 0x53, 0x6a, 0xdf, 0xf7, 0x99, 0xb2,
            },
        },
        .confirm_rsp = (struct ble_sm_pair_confirm) {
            .value = {
                0x04, 0x4e, 0xaf, 0xce, 0x30, 0x79, 0x2c, 0x9e,
                0xa2, 0xeb, 0x53, 0x6a, 0xdf, 0xf7, 0x99, 0xb2,
            },
        },
        .random_req = (struct ble_sm_pair_random) {
            .value = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            },
        },
        .random_rsp = (struct ble_sm_pair_random) {
            .value = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            },
        },
        .pair_alg = BLE_SM_PAIR_ALG_JW,
        .tk = { 0 },
        .stk = {
            0x2e, 0x2b, 0x34, 0xca, 0x59, 0xfa, 0x4c, 0x88,
            0x3b, 0x2c, 0x8a, 0xef, 0xd4, 0x4b, 0xe9, 0x66,
        },
        .r = 0,
        .ediv = 0,
    };

    ble_sm_test_util_us_lgcy_good(&params);
}

/**
 * @param send_enc_req          Whether this procedure is initiated by a slave
 *                                  security request;
 *                                  1: Peer sends a security request at start.
 *                                  0: No security request; we initiate.
 */
static void
ble_sm_test_util_us_bonding_good(int send_enc_req, uint8_t *ltk,
                                       int authenticated,
                                       uint16_t ediv, uint64_t rand_num)
{
    struct ble_sm_sec_req sec_req;
    struct ble_store_value_sec value_sec;
    struct ble_hs_conn *conn;
    int rc;

    ble_sm_test_util_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[6]){1,2,3,4,5,6}),
                                 ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object without locking
     * the host mutex.  It is not OK for real code to do this, but this test
     * can assume the connection list is unchanging.
     */
    ble_hs_lock();
    conn = ble_hs_conn_find(2);
    TEST_ASSERT_FATAL(conn != NULL);
    ble_hs_unlock();

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Populate the SM database with an LTK for this peer. */
    value_sec.peer_addr_type = conn->bhc_addr_type;
    memcpy(value_sec.peer_addr, conn->bhc_addr, sizeof value_sec.peer_addr);
    value_sec.ediv = ediv;
    value_sec.rand_num = rand_num;
    memcpy(value_sec.ltk, ltk, sizeof value_sec.ltk);
    value_sec.authenticated = authenticated;
    value_sec.sc = 0;

    rc = ble_store_write_mst_sec(&value_sec);
    TEST_ASSERT_FATAL(rc == 0);

    if (send_enc_req) {
        sec_req.authreq = 0;
        sec_req.authreq |= BLE_SM_PAIR_AUTHREQ_BOND;
        if (authenticated) {
            sec_req.authreq |= BLE_SM_PAIR_AUTHREQ_MITM;
        }
        ble_hs_test_util_set_ack(
            host_hci_opcode_join(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_START_ENCRYPT),
            0);
        ble_sm_test_util_rx_sec_req(2, &sec_req, 0);
    }

    /* Ensure we sent the expected start encryption command. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_start_enc(2, rand_num, ediv, ltk);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive an encryption changed event. */
    ble_sm_test_util_rx_enc_change(2, 0, 1);

    /* Pairing should now be complete. */
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Verify that security callback was executed. */
    TEST_ASSERT(ble_sm_test_gap_event == BLE_GAP_EVENT_ENC_CHANGE);
    TEST_ASSERT(ble_sm_test_gap_status == 0);
    TEST_ASSERT(ble_sm_test_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_sm_test_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                authenticated);
}

/**
 * Master: us.
 * Peer sends a security request.
 * We respond by initiating the encryption procedure.
 */
TEST_CASE(ble_sm_test_case_peer_sec_req_enc)
{
    /* Unauthenticated. */
    ble_sm_test_util_us_bonding_good(
        1,
        ((uint8_t[16]){ 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 }),
        0,
        0x1234,
        0x5678);
}

/**
 * Master: peer.
 * We send a security request.
 * We accept pairing request sent in response.
 */
TEST_CASE(ble_sm_test_case_us_sec_req_pair)
{
    struct ble_sm_test_lgcy_params params;

    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c},
        .rsp_addr = {0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07},
        .sec_req = (struct ble_sm_sec_req) {
            .authreq = 0x05,
        },
        .has_sec_req = 1,
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 0x04,
            .oob_data_flag = 0,
            .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x07,
            .resp_key_dist = 0x07,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x02,
            .oob_data_flag = 0,
            .authreq = 0x05,
            .max_enc_key_size = 16,
            .init_key_dist = 0x01,
            .resp_key_dist = 0x01,
        },
        .confirm_req = (struct ble_sm_pair_confirm) {
            .value = {
                0x54, 0xed, 0x7c, 0x65, 0xc5, 0x3a, 0xee, 0x87,
                0x8e, 0xf8, 0x04, 0xd8, 0x93, 0xb0, 0xfa, 0xa4,
            },
        },
        .confirm_rsp = (struct ble_sm_pair_confirm) {
            .value = {
                0xdf, 0x96, 0x88, 0x73, 0x49, 0x24, 0x3f, 0xe8,
                0xb0, 0xaf, 0xb3, 0xf6, 0xc8, 0xf4, 0xe2, 0x36,
            },
        },
        .random_req = (struct ble_sm_pair_random) {
            .value = {
                0x4d, 0x2c, 0xf2, 0xb7, 0x11, 0x56, 0xbd, 0x4f,
                0xfc, 0xde, 0xa9, 0x86, 0x4d, 0xfd, 0x77, 0x03,
            },
        },
        .random_rsp = {
            .value = {
                0x12, 0x45, 0x65, 0x2c, 0x85, 0x56, 0x32, 0x8f,
                0xf4, 0x7f, 0x44, 0xd0, 0x17, 0x35, 0x41, 0xed
            },
        },
        .pair_alg = BLE_SM_PAIR_ALG_PASSKEY,
        .authenticated = 1,
        .tk = {
            0x5a, 0x7f, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        },
        .stk = {
            0x2b, 0x9c, 0x1e, 0x42, 0xa8, 0xcb, 0xab, 0xd1,
            0x4b, 0xde, 0x50, 0x05, 0x50, 0xd9, 0x95, 0xc6
        },
        .r = 4107344270811490869,
        .ediv = 61621,

        .passkey = {
            .action = BLE_SM_PKACT_INPUT,
            .passkey = 884570,
        },

        .enc_info_req = {
            .ltk = {
                0x2b, 0x9c, 0x1e, 0x42, 0xa8, 0xcb, 0xab, 0xd1,
                0x4b, 0xde, 0x50, 0x05, 0x50, 0xd9, 0x95, 0xc6
            },
        },
        .has_enc_info_req = 1,

        .master_id_req = {
            .ediv = 61621,
            .rand_val = 4107344270811490869,
        },
        .has_master_id_req = 1,

        .enc_info_rsp = {
            .ltk = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 }
        },
        .has_enc_info_rsp = 1,

        .master_id_rsp = {
            .ediv = 61621,
            .rand_val = 4107344270811490869,
        },
        .has_master_id_rsp = 1,
    };
    ble_sm_test_util_peer_lgcy_good(&params);
}

/**
 * Master: peer.
 * We send a security request.
 * We accept an encryption-changed event in response.
 */
TEST_CASE(ble_sm_test_case_us_sec_req_enc)
{
    ble_sm_test_util_peer_bonding_good(
        1,
        ((uint8_t[16]){ 2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17 }),
        1,
        0x4325,
        0x543892375);
}

static void
ble_sm_test_util_peer_sc_good(struct ble_sm_test_sc_params *params)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_sm_test_util_init();

    ble_hs_cfg.sm_io_cap = params->pair_rsp.io_cap;
    ble_hs_cfg.sm_oob_data_flag = params->pair_rsp.oob_data_flag;
    ble_hs_cfg.sm_bonding = !!(params->pair_rsp.authreq &
                               BLE_SM_PAIR_AUTHREQ_BOND);
    ble_hs_cfg.sm_mitm = !!(params->pair_rsp.authreq &
                            BLE_SM_PAIR_AUTHREQ_MITM);
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_keypress = !!(params->pair_rsp.authreq &
                                BLE_SM_PAIR_AUTHREQ_KEYPRESS);
    ble_hs_cfg.sm_our_key_dist = params->pair_rsp.resp_key_dist;
    ble_hs_cfg.sm_their_key_dist = params->pair_rsp.init_key_dist;

    ble_hs_test_util_set_public_addr(params->rsp_addr);
    ble_sm_dbg_set_next_pair_rand(params->random_rsp.value);

    ble_hs_test_util_create_conn(2, params->init_addr,
                                 ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object without locking
     * the host mutex.  It is not OK for real code to do this, but this test
     * can assume the connection list is unchanging.
     */
    ble_hs_lock();
    conn = ble_hs_conn_find(2);
    TEST_ASSERT_FATAL(conn != NULL);
    ble_hs_unlock();

    /* Peer is the initiator so we must be the slave. */
    conn->bhc_flags &= ~BLE_HS_CONN_F_MASTER;

    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    if (params->has_sec_req) {
        rc = ble_sm_slave_initiate(2);
        TEST_ASSERT(rc == 0);

        /* Ensure we sent the expected security request. */
        ble_sm_test_util_verify_tx_sec_req(&params->sec_req);
    }

    /* Receive a pair request from the peer. */
    ble_sm_test_util_rx_pair_req(2, &params->pair_req, 0);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair response. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_rsp(&params->pair_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive a public key from the peer. */
    ble_sm_test_util_rx_public_key(2, &params->public_key_req);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected public key. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_public_key(&params->public_key_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    if (params->has_confirm_req) {
        /* Receive a pair confirm from the peer. */
        ble_sm_test_util_rx_confirm(2, &params->confirm_req);
        TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
        TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    }

    /* Ensure we sent the expected pair confirm. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_confirm(&params->confirm_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive a pair random from the peer. */
    ble_sm_test_util_rx_random(2, &params->random_req, 0);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected pair random. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_random(&params->random_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive a dhkey check from the peer. */
    ble_sm_test_util_rx_dhkey_check(2, &params->dhkey_check_req, 0);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected dhkey check. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_dhkey_check(&params->dhkey_check_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive a long term key request from the controller. */
    ble_sm_test_util_set_lt_key_req_reply_ack(0, 2);
    ble_sm_test_util_rx_lt_key_req(2, 0, 0);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected long term key request reply command. */
    ble_sm_test_util_verify_tx_lt_key_req_reply(2, params->ltk);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive an encryption changed event. */
    ble_sm_test_util_rx_enc_change(2, 0, 1);

    /* Pairing should now be complete. */
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Verify that security callback was executed. */
    TEST_ASSERT(ble_sm_test_gap_event == BLE_GAP_EVENT_ENC_CHANGE);
    TEST_ASSERT(ble_sm_test_gap_status == 0);
    TEST_ASSERT(ble_sm_test_sec_state.pair_alg == params->pair_alg);
    TEST_ASSERT(ble_sm_test_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                params->authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_sm_test_sec_state.pair_alg ==
                conn->bhc_sec_state.pair_alg);
    TEST_ASSERT(ble_sm_test_sec_state.enc_enabled ==
                conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                conn->bhc_sec_state.authenticated);
}

TEST_CASE(ble_sm_test_case_peer_sc_jw_good)
{
    struct ble_sm_test_sc_params params;

    params = (struct ble_sm_test_sc_params) {
        .init_addr = {0xec, 0xfb, 0x73, 0x73, 0x21, 0x65},
        .rsp_addr = {0x1c, 0xfc, 0xd2, 0x07, 0x31, 0x7a},
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 0x00,
            .oob_data_flag = 0x00,
            .authreq = 0x08,
            .max_enc_key_size = 16,
            .init_key_dist = 0x00,
            .resp_key_dist = 0x00,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x00,
            .oob_data_flag = 0x00,
            .authreq = 0x08,
            .max_enc_key_size = 16,
            .init_key_dist = 0x00,
            .resp_key_dist = 0x00,
        },
        .public_key_req = (struct ble_sm_public_key) {
            .x = {
                0xe6, 0x9d, 0x35, 0x0e, 0x48, 0x01, 0x03, 0xcc,
                0xdb, 0xfd, 0xf4, 0xac, 0x11, 0x91, 0xf4, 0xef,
                0xb9, 0xa5, 0xf9, 0xe9, 0xa7, 0x83, 0x2c, 0x5e,
                0x2c, 0xbe, 0x97, 0xf2, 0xd2, 0x03, 0xb0, 0x20,
            },
            .y = {
                0x8b, 0xd2, 0x89, 0x15, 0xd0, 0x8e, 0x1c, 0x74,
                0x24, 0x30, 0xed, 0x8f, 0xc2, 0x45, 0x63, 0x76,
                0x5c, 0x15, 0x52, 0x5a, 0xbf, 0x9a, 0x32, 0x63,
                0x6d, 0xeb, 0x2a, 0x65, 0x49, 0x9c, 0x80, 0xdc,
            }
        },
        .public_key_rsp = (struct ble_sm_public_key) {
            .x = {
                0xe6, 0x9d, 0x35, 0x0e, 0x48, 0x01, 0x03, 0xcc,
                0xdb, 0xfd, 0xf4, 0xac, 0x11, 0x91, 0xf4, 0xef,
                0xb9, 0xa5, 0xf9, 0xe9, 0xa7, 0x83, 0x2c, 0x5e,
                0x2c, 0xbe, 0x97, 0xf2, 0xd2, 0x03, 0xb0, 0x20,
            },
            .y = {
                0x8b, 0xd2, 0x89, 0x15, 0xd0, 0x8e, 0x1c, 0x74,
                0x24, 0x30, 0xed, 0x8f, 0xc2, 0x45, 0x63, 0x76,
                0x5c, 0x15, 0x52, 0x5a, 0xbf, 0x9a, 0x32, 0x63,
                0x6d, 0xeb, 0x2a, 0x65, 0x49, 0x9c, 0x80, 0xdc,
            }
        },
        .confirm_req = (struct ble_sm_pair_confirm) {
            .value = {
                0x2d, 0x87, 0x74, 0xa9, 0xbe, 0xa1, 0xed, 0xf1,
                0x1c, 0xbd, 0xa9, 0x07, 0xf1, 0x16, 0xc9, 0xf2,
            },
        },
        .confirm_rsp = (struct ble_sm_pair_confirm) {
            .value = {
                0x2d, 0x87, 0x74, 0xa9, 0xbe, 0xa1, 0xed, 0xf1,
                0x1c, 0xbd, 0xa9, 0x07, 0xf1, 0x16, 0xc9, 0xf2,
            },
        },
        .random_req = (struct ble_sm_pair_random) {
            .value = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            },
        },
        .random_rsp = (struct ble_sm_pair_random) {
            .value = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            },
        },
        .dhkey_check_req = (struct ble_sm_dhkey_check) {
            .value = {
                0x61, 0x8f, 0x95, 0xda, 0x09, 0x0b, 0x6c, 0xd2,
                0xc5, 0xe8, 0xd0, 0x9c, 0x98, 0x73, 0xc4, 0xe3,
            }
        },
        .dhkey_check_rsp = (struct ble_sm_dhkey_check) {
            .value = {
                0x61, 0x8f, 0x95, 0xda, 0x09, 0x0b, 0x6c, 0xd2,
                0xc5, 0xe8, 0xd0, 0x9c, 0x98, 0x73, 0xc4, 0xe3,
            }
        },
        .pair_alg = BLE_SM_PAIR_ALG_JW,
        .authenticated = 0,
        .ltk = {
            0x20, 0x6e, 0x63, 0xce, 0x20, 0x6a, 0x3f, 0xfd,
            0x02, 0x4a, 0x08, 0xa1, 0x76, 0xf1, 0x65, 0x29,
        },
    };
    ble_sm_test_util_peer_sc_good(&params);
}

TEST_SUITE(ble_sm_test_suite)
{
    ble_sm_test_case_peer_fail_inval();
    ble_sm_test_case_peer_lgcy_fail_confirm();
    ble_sm_test_case_peer_lgcy_jw_good();
    ble_sm_test_case_peer_lgcy_passkey_good();
    ble_sm_test_case_us_fail_inval();
    ble_sm_test_case_us_lgcy_jw_good();
    ble_sm_test_case_peer_bonding_good();
    ble_sm_test_case_peer_bonding_bad();
    ble_sm_test_case_conn_broken();
    ble_sm_test_case_peer_sec_req_inval();
    ble_sm_test_case_peer_sec_req_pair();
    ble_sm_test_case_peer_sec_req_enc();
    ble_sm_test_case_us_sec_req_pair();
    ble_sm_test_case_us_sec_req_enc();
    ble_sm_test_case_peer_sc_jw_good();
}
#endif

int
ble_sm_test_all(void)
{
#if !NIMBLE_OPT(SM)
    return 0;
#else
    ble_sm_test_suite();

    return tu_any_failed;
#endif
}
