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

struct ble_sm_test_passkey_info {
    struct ble_sm_io passkey;
    uint32_t exp_numcmp;
    unsigned io_before_rx:1;
};

struct ble_sm_test_lgcy_params {
    uint8_t init_addr[6];
    uint8_t resp_addr[6];
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

    struct ble_sm_test_passkey_info passkey_info;
    struct ble_sm_pair_fail pair_fail;

    unsigned has_sec_req:1;
    unsigned has_enc_info_req:1;
    unsigned has_enc_info_rsp:1;
    unsigned has_master_id_req:1;
    unsigned has_master_id_rsp:1;
};

struct ble_sm_test_sc_params {
    uint8_t init_addr[6];
    uint8_t resp_addr[6];
    struct ble_sm_sec_req sec_req;
    struct ble_sm_pair_cmd pair_req;
    struct ble_sm_pair_cmd pair_rsp;
    struct ble_sm_pair_confirm confirm_req[20];
    struct ble_sm_pair_confirm confirm_rsp[20];
    struct ble_sm_pair_random random_req[20];
    struct ble_sm_pair_random random_rsp[20];
    struct ble_sm_public_key public_key_req;
    struct ble_sm_public_key public_key_rsp;
    struct ble_sm_dhkey_check dhkey_check_req;
    struct ble_sm_dhkey_check dhkey_check_rsp;
    int pair_alg;
    unsigned authenticated:1;
    uint8_t ltk[16];
    uint8_t our_priv_key[32];

    struct ble_sm_test_passkey_info passkey_info;
    struct ble_sm_pair_fail pair_fail;

    unsigned has_sec_req:1;
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

    memset(&ble_sm_test_sec_state, 0xff, sizeof ble_sm_test_sec_state);
}

struct ble_sm_test_ltk_info {
    uint8_t ltk[16];
    unsigned authenticated:1;
};

struct ble_gap_passkey_action ble_sm_test_ioact;

static int
ble_sm_test_util_conn_cb(int event, struct ble_gap_conn_ctxt *ctxt, void *arg)
{
    int rc;

    switch (event) {
    case BLE_GAP_EVENT_ENC_CHANGE:
        ble_sm_test_gap_status = ctxt->enc_change.status;
        ble_sm_test_sec_state = ctxt->desc->sec_state;
        rc = 0;
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ble_sm_test_ioact = ctxt->passkey_action;
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
ble_sm_test_util_rx_pair_rsp(uint16_t conn_handle, struct ble_sm_pair_cmd *rsp,
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
ble_sm_test_util_rx_sec_req(uint16_t conn_handle, struct ble_sm_sec_req *cmd,
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

    rc = ble_sm_ltk_req_rx(&evt);
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

    ble_sm_enc_change_rx(&evt);
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

static void
ble_sm_test_util_io_inject(struct ble_sm_test_passkey_info *passkey_info,
                           uint8_t cur_sm_state)
{
    uint8_t io_sm_state;
    int rc;

    io_sm_state = ble_sm_ioact_state(passkey_info->passkey.action);
    if (io_sm_state != cur_sm_state) {
        return;
    }

    if (passkey_info->passkey.action == BLE_SM_IOACT_NUMCMP) {
        TEST_ASSERT(ble_sm_test_ioact.numcmp == passkey_info->exp_numcmp);
    }

    rc = ble_sm_inject_io(2, &passkey_info->passkey);
    TEST_ASSERT(rc == 0);
}

static void
ble_sm_test_util_io_check_pre(struct ble_sm_test_passkey_info *passkey_info,
                              uint8_t cur_sm_state)
{
    uint8_t io_sm_state;
    int rc;

    io_sm_state = ble_sm_ioact_state(passkey_info->passkey.action);
    if (io_sm_state != cur_sm_state) {
        return;
    }

    if (!passkey_info->io_before_rx) {
        return;
    }

    if (passkey_info->passkey.action == BLE_SM_IOACT_NUMCMP) {
        TEST_ASSERT(ble_sm_test_ioact.numcmp == passkey_info->exp_numcmp);
    }

    rc = ble_sm_inject_io(2, &passkey_info->passkey);
    TEST_ASSERT(rc == 0);
}

static void
ble_sm_test_util_io_check_post(struct ble_sm_test_passkey_info *passkey_info,
                               uint8_t cur_sm_state)
{
    uint8_t io_sm_state;
    int rc;

    io_sm_state = ble_sm_ioact_state(passkey_info->passkey.action);
    if (io_sm_state != cur_sm_state) {
        return;
    }

    if (passkey_info->io_before_rx) {
        return;
    }

    if (passkey_info->passkey.action == BLE_SM_IOACT_NUMCMP) {
        TEST_ASSERT(ble_sm_test_ioact.numcmp == passkey_info->exp_numcmp);
    }

    /* Ensure response not sent until user performs IO. */
    ble_hs_test_util_tx_all();
    TEST_ASSERT(ble_hs_test_util_prev_tx_queue_sz() == 0);

    rc = ble_sm_inject_io(2, &passkey_info->passkey);
    TEST_ASSERT_FATAL(rc == 0);
}

static void
ble_sm_test_util_verify_lgcy_persist(struct ble_sm_test_lgcy_params *params)
{
    struct ble_store_value_sec value_sec;
    struct ble_store_key_sec key_sec;
    int rc;

    memset(&key_sec, 0, sizeof key_sec);
    key_sec.peer_addr_type = BLE_STORE_ADDR_TYPE_NONE;

    if (params->pair_rsp.init_key_dist == 0) {
        rc = ble_store_read_slv_sec(&key_sec, &value_sec);
        TEST_ASSERT(rc == BLE_HS_ENOENT);
    } else {
        rc = ble_store_read_mst_sec(&key_sec, &value_sec);
        TEST_ASSERT_FATAL(rc == 0);
        TEST_ASSERT(value_sec.peer_addr_type == 0);
        TEST_ASSERT(memcmp(value_sec.peer_addr, params->init_addr, 6) == 0);
        TEST_ASSERT(value_sec.ediv == params->ediv);
        TEST_ASSERT(value_sec.rand_num == params->r);
        TEST_ASSERT(value_sec.authenticated == params->authenticated);
        TEST_ASSERT(value_sec.ltk_present == 1);
        TEST_ASSERT(memcmp(value_sec.ltk, params->enc_info_req.ltk, 16) == 0);
        TEST_ASSERT(value_sec.irk_present == 0);
        TEST_ASSERT(value_sec.csrk_present == 0);

        /* Verify no other keys were persisted. */
        key_sec.idx++;
        rc = ble_store_read_mst_sec(&key_sec, &value_sec);
        TEST_ASSERT_FATAL(rc == BLE_HS_ENOENT);
    }

    memset(&key_sec, 0, sizeof key_sec);
    key_sec.peer_addr_type = BLE_STORE_ADDR_TYPE_NONE;

    if (params->pair_rsp.resp_key_dist == 0) {
        rc = ble_store_read_slv_sec(&key_sec, &value_sec);
        TEST_ASSERT(rc == BLE_HS_ENOENT);
    } else {
        rc = ble_store_read_slv_sec(&key_sec, &value_sec);
        TEST_ASSERT_FATAL(rc == 0);
        TEST_ASSERT(value_sec.peer_addr_type == 0);
        TEST_ASSERT(memcmp(value_sec.peer_addr, params->init_addr, 6) == 0);
        TEST_ASSERT(value_sec.ediv == params->ediv);
        TEST_ASSERT(value_sec.rand_num == params->r);
        TEST_ASSERT(value_sec.authenticated == params->authenticated);
        TEST_ASSERT(value_sec.ltk_present == 1);
        TEST_ASSERT(memcmp(value_sec.ltk, params->enc_info_req.ltk, 16) == 0);
        TEST_ASSERT(value_sec.irk_present == 0);
        TEST_ASSERT(value_sec.csrk_present == 0);

        /* Verify no other keys were persisted. */
        key_sec.idx++;
        rc = ble_store_read_slv_sec(&key_sec, &value_sec);
        TEST_ASSERT_FATAL(rc == BLE_HS_ENOENT);
    }
}

static void
ble_sm_test_util_verify_sc_persist(struct ble_sm_test_sc_params *params,
                                   int we_are_initiator)
{
    struct ble_store_value_sec value_sec;
    struct ble_store_key_sec key_sec;
    uint8_t *peer_addr;
    uint8_t peer_addr_type;
    int rc;

    if (we_are_initiator) {
        peer_addr_type = 0;
        peer_addr = params->resp_addr;
    } else {
        peer_addr_type = 0;
        peer_addr = params->init_addr;
    }

    memset(&key_sec, 0, sizeof key_sec);
    key_sec.peer_addr_type = BLE_STORE_ADDR_TYPE_NONE;

    rc = ble_store_read_mst_sec(&key_sec, &value_sec);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(value_sec.peer_addr_type == peer_addr_type);
    TEST_ASSERT(memcmp(value_sec.peer_addr, peer_addr, 6) == 0);
    TEST_ASSERT(value_sec.ediv == 0);
    TEST_ASSERT(value_sec.rand_num == 0);
    TEST_ASSERT(value_sec.authenticated == params->authenticated);
    TEST_ASSERT(value_sec.ltk_present == 1);
    TEST_ASSERT(memcmp(value_sec.ltk, params->ltk, 16) == 0);
    TEST_ASSERT(value_sec.irk_present == 0);
    TEST_ASSERT(value_sec.csrk_present == 0);

    rc = ble_store_read_slv_sec(&key_sec, &value_sec);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(value_sec.peer_addr_type == peer_addr_type);
    TEST_ASSERT(memcmp(value_sec.peer_addr, peer_addr, 6) == 0);
    TEST_ASSERT(value_sec.ediv == 0);
    TEST_ASSERT(value_sec.rand_num == 0);
    TEST_ASSERT(value_sec.authenticated == params->authenticated);
    TEST_ASSERT(value_sec.ltk_present == 1);
    TEST_ASSERT(memcmp(value_sec.ltk, params->ltk, 16) == 0);
    TEST_ASSERT(value_sec.irk_present == 0);
    TEST_ASSERT(value_sec.csrk_present == 0);

    /* Verify no other keys were persisted. */
    key_sec.idx++;
    rc = ble_store_read_slv_sec(&key_sec, &value_sec);
    TEST_ASSERT_FATAL(rc == BLE_HS_ENOENT);
    rc = ble_store_read_mst_sec(&key_sec, &value_sec);
    TEST_ASSERT_FATAL(rc == BLE_HS_ENOENT);
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

    ble_hs_test_util_create_conn(2, params->resp_addr,
                                 ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object after unlocking
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

    /* Verify the appropriate security material was persisted. */
    ble_sm_test_util_verify_lgcy_persist(params);
}

static void
ble_sm_test_util_peer_fail_inval(
    int we_are_master,
    uint8_t *init_addr,
    uint8_t *resp_addr,
    struct ble_sm_pair_cmd *pair_req,
    struct ble_sm_pair_fail *pair_fail)
{
    struct ble_hs_conn *conn;

    ble_sm_test_util_init();
    ble_hs_test_util_set_public_addr(resp_addr);

    ble_hs_test_util_create_conn(2, init_addr, ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object after unlocking
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

static void
ble_sm_test_util_peer_lgcy_fail_confirm(
    uint8_t *init_addr,
    uint8_t *resp_addr,
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
    ble_hs_test_util_set_public_addr(resp_addr);
    ble_sm_dbg_set_next_pair_rand(random_rsp->value);

    ble_hs_test_util_create_conn(2, init_addr, ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object after unlocking
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

static void
ble_sm_test_util_peer_lgcy_good_once(struct ble_sm_test_lgcy_params *params)
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

    ble_hs_test_util_set_public_addr(params->resp_addr);
    ble_sm_dbg_set_next_pair_rand(params->random_rsp.value);
    ble_sm_dbg_set_next_ediv(params->ediv);
    ble_sm_dbg_set_next_start_rand(params->r);

    if (params->has_enc_info_req) {
        ble_sm_dbg_set_next_ltk(params->enc_info_req.ltk);
    }

    ble_hs_test_util_create_conn(2, params->init_addr,
                                 ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object after unlocking
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

    ble_sm_test_util_io_check_pre(&params->passkey_info,
                                  BLE_SM_PROC_STATE_CONFIRM);

    /* Receive a pair confirm from the peer. */
    ble_sm_test_util_rx_confirm(2, &params->confirm_req);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    ble_sm_test_util_io_check_post(&params->passkey_info,
                                   BLE_SM_PROC_STATE_CONFIRM);

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

    /* Verify the appropriate security material was persisted. */
    ble_sm_test_util_verify_lgcy_persist(params);
}

static void
ble_sm_test_util_peer_lgcy_good(struct ble_sm_test_lgcy_params *params)
{
    params->passkey_info.io_before_rx = 0;
    ble_sm_test_util_peer_lgcy_good_once(params);

    params->passkey_info.io_before_rx = 1;
    ble_sm_test_util_peer_lgcy_good_once(params);
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

    /* This test inspects and modifies the connection object after unlocking
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
    value_sec.ltk_present = 1;
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
                BLE_ADDR_TYPE_PUBLIC);
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

    /* This test inspects and modifies the connection object after unlocking
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

static void
ble_sm_test_util_peer_sc_good_once(struct ble_sm_test_sc_params *params)
{
    struct ble_hs_conn *conn;
    int num_iters;
    int rc;
    int i;

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

    ble_hs_test_util_set_public_addr(params->resp_addr);
    ble_sm_dbg_set_next_pair_rand(params->random_rsp[0].value);

    ble_sm_dbg_set_sc_keys(params->public_key_rsp.x, params->our_priv_key);

    ble_hs_test_util_create_conn(2, params->init_addr,
                                 ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object after unlocking
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

    switch (params->pair_alg) {
    case BLE_SM_PAIR_ALG_PASSKEY:
        num_iters = 20;
        break;

    default:
        num_iters = 1;
        break;
    }

    ble_sm_test_util_io_check_pre(&params->passkey_info,
                                  BLE_SM_PROC_STATE_CONFIRM);

    for (i = 0; i < num_iters; i++) {
        if (params->pair_alg != BLE_SM_PAIR_ALG_JW      &&
            params->pair_alg != BLE_SM_PAIR_ALG_NUMCMP) {

            /* Receive a pair confirm from the peer. */
            ble_sm_test_util_rx_confirm(2, params->confirm_req + i);
            TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
            TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

            if (i < num_iters - 1) {
                ble_sm_dbg_set_next_pair_rand(
                    params->random_rsp[i + 1].value);
            }
        }

        if (i == 0) {
            ble_sm_test_util_io_check_post(&params->passkey_info,
                                           BLE_SM_PROC_STATE_CONFIRM);
        }

        /* Ensure we sent the expected pair confirm. */
        ble_hs_test_util_tx_all();
        ble_sm_test_util_verify_tx_pair_confirm(params->confirm_rsp + i);
        TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
        TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

        /* Receive a pair random from the peer. */
        ble_sm_test_util_rx_random(2, params->random_req + i, 0);
        TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
        TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

        /* Ensure we sent the expected pair random. */
        ble_hs_test_util_tx_all();
        ble_sm_test_util_verify_tx_pair_random(params->random_rsp + i);
        TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
        TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    }

    ble_sm_test_util_io_check_pre(&params->passkey_info,
                                  BLE_SM_PROC_STATE_DHKEY_CHECK);

    /* Receive a dhkey check from the peer. */
    ble_sm_test_util_rx_dhkey_check(2, &params->dhkey_check_req, 0);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    ble_sm_test_util_io_check_post(&params->passkey_info,
                                   BLE_SM_PROC_STATE_DHKEY_CHECK);

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

    /* Verify the appropriate security material was persisted. */
    ble_sm_test_util_verify_sc_persist(params, 0);
}

static void
ble_sm_test_util_peer_sc_good(struct ble_sm_test_sc_params *params)
{
    params->passkey_info.io_before_rx = 0;
    ble_sm_test_util_peer_sc_good_once(params);

    params->passkey_info.io_before_rx = 1;
    ble_sm_test_util_peer_sc_good_once(params);

}

static void
ble_sm_test_util_us_sc_good(struct ble_sm_test_sc_params *params)
{
    struct ble_hs_conn *conn;
    int num_iters;
    int rc;
    int i;

    ble_sm_test_util_init();

    ble_hs_cfg.sm_io_cap = params->pair_req.io_cap;
    ble_hs_cfg.sm_oob_data_flag = params->pair_req.oob_data_flag;
    ble_hs_cfg.sm_bonding = !!(params->pair_req.authreq &
                               BLE_SM_PAIR_AUTHREQ_BOND);
    ble_hs_cfg.sm_mitm = !!(params->pair_req.authreq &
                            BLE_SM_PAIR_AUTHREQ_MITM);
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_keypress = !!(params->pair_req.authreq &
                                BLE_SM_PAIR_AUTHREQ_KEYPRESS);
    ble_hs_cfg.sm_our_key_dist = params->pair_req.init_key_dist;
    ble_hs_cfg.sm_their_key_dist = params->pair_req.resp_key_dist;

    ble_hs_test_util_set_public_addr(params->init_addr);
    ble_sm_dbg_set_next_pair_rand(params->random_req[0].value);

    ble_sm_dbg_set_sc_keys(params->public_key_req.x, params->our_priv_key);

    ble_hs_test_util_create_conn(2, params->resp_addr,
                                 ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object after unlocking
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

    /* Ensure we sent the expected public key. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_public_key(&params->public_key_req);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive a public key from the peer. */
    ble_sm_test_util_rx_public_key(2, &params->public_key_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    switch (params->pair_alg) {
    case BLE_SM_PAIR_ALG_PASSKEY:
        num_iters = 20;
        break;

    default:
        num_iters = 1;
        break;
    }

    ble_sm_test_util_io_inject(&params->passkey_info,
                               BLE_SM_PROC_STATE_CONFIRM);

    for (i = 0; i < num_iters; i++) {
        if (params->pair_alg != BLE_SM_PAIR_ALG_JW      &&
            params->pair_alg != BLE_SM_PAIR_ALG_NUMCMP) {

            if (i < num_iters - 1) {
                ble_sm_dbg_set_next_pair_rand(
                    params->random_req[i + 1].value);
            }

            /* Ensure we sent the expected pair confirm. */
            ble_hs_test_util_tx_all();
            ble_sm_test_util_verify_tx_pair_confirm(params->confirm_req + i);
            TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
            TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
        }

        /* Receive a pair confirm from the peer. */
        ble_sm_test_util_rx_confirm(2, params->confirm_rsp + i);
        TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
        TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

        /* Ensure we sent the expected pair random. */
        ble_hs_test_util_tx_all();
        ble_sm_test_util_verify_tx_pair_random(params->random_req + i);
        TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
        TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

        /* Receive a pair random from the peer. */
        ble_sm_test_util_rx_random(2, params->random_rsp + i, 0);
        TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
        TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    }

    ble_sm_test_util_io_inject(&params->passkey_info,
                               BLE_SM_PROC_STATE_DHKEY_CHECK);

    /* Ensure we sent the expected dhkey check. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_dhkey_check(&params->dhkey_check_req);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Receive a dhkey check from the peer. */
    ble_sm_test_util_rx_dhkey_check(2, &params->dhkey_check_rsp, 0);
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);

    /* Ensure we sent the expected start encryption command. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_start_enc(2, 0, 0, params->ltk);
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

    /* Verify the appropriate security material was persisted. */
    ble_sm_test_util_verify_sc_persist(params, 1);
}

static void
ble_sm_test_util_us_fail_inval(
    struct ble_sm_test_lgcy_params *params)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_sm_test_util_init();
    ble_hs_test_util_set_public_addr(params->resp_addr);

    ble_sm_dbg_set_next_pair_rand(((uint8_t[16]){0}));

    ble_hs_test_util_create_conn(2, params->init_addr,
                                 ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object after unlocking
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
    TEST_ASSERT(ble_sm_test_gap_event == -1);
    TEST_ASSERT(ble_sm_test_gap_status == -1);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(!conn->bhc_sec_state.enc_enabled);
    TEST_ASSERT(!conn->bhc_sec_state.authenticated);
}

/**
 * @param send_enc_req          Whether this procedure is initiated by a slave
 *                                  security request;
 *                                  1: Peer sends a security request at start.
 *                                  0: No security request; we initiate.
 */
static void
ble_sm_test_util_us_bonding_good(int send_enc_req, uint8_t *ltk,
                                 int authenticated, uint16_t ediv,
                                 uint64_t rand_num)
{
    struct ble_sm_sec_req sec_req;
    struct ble_store_value_sec value_sec;
    struct ble_hs_conn *conn;
    int rc;

    ble_sm_test_util_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[6]){1,2,3,4,5,6}),
                                 ble_sm_test_util_conn_cb,
                                 NULL);

    /* This test inspects and modifies the connection object after unlocking
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
    value_sec.ltk_present = 1;
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

/*****************************************************************************
 * $misc                                                                     *
 *****************************************************************************/

TEST_CASE(ble_sm_test_case_f4)
{
	uint8_t u[32] = { 0xe6, 0x9d, 0x35, 0x0e, 0x48, 0x01, 0x03, 0xcc,
			  0xdb, 0xfd, 0xf4, 0xac, 0x11, 0x91, 0xf4, 0xef,
			  0xb9, 0xa5, 0xf9, 0xe9, 0xa7, 0x83, 0x2c, 0x5e,
			  0x2c, 0xbe, 0x97, 0xf2, 0xd2, 0x03, 0xb0, 0x20 };
	uint8_t v[32] = { 0xfd, 0xc5, 0x7f, 0xf4, 0x49, 0xdd, 0x4f, 0x6b,
			  0xfb, 0x7c, 0x9d, 0xf1, 0xc2, 0x9a, 0xcb, 0x59,
			  0x2a, 0xe7, 0xd4, 0xee, 0xfb, 0xfc, 0x0a, 0x90,
			  0x9a, 0xbb, 0xf6, 0x32, 0x3d, 0x8b, 0x18, 0x55 };
	uint8_t x[16] = { 0xab, 0xae, 0x2b, 0x71, 0xec, 0xb2, 0xff, 0xff,
			  0x3e, 0x73, 0x77, 0xd1, 0x54, 0x84, 0xcb, 0xd5 };
	uint8_t z = 0x00;
	uint8_t exp[16] = { 0x2d, 0x87, 0x74, 0xa9, 0xbe, 0xa1, 0xed, 0xf1,
			    0x1c, 0xbd, 0xa9, 0x07, 0xf1, 0x16, 0xc9, 0xf2 };
	uint8_t res[16];
	int err;

	err = ble_sm_alg_f4(u, v, x, z, res);
	TEST_ASSERT_FATAL(err == 0);
    TEST_ASSERT(memcmp(res, exp, 16) == 0);
}

TEST_CASE(ble_sm_test_case_f5)
{
	uint8_t w[32] = { 0x98, 0xa6, 0xbf, 0x73, 0xf3, 0x34, 0x8d, 0x86,
			  0xf1, 0x66, 0xf8, 0xb4, 0x13, 0x6b, 0x79, 0x99,
			  0x9b, 0x7d, 0x39, 0x0a, 0xa6, 0x10, 0x10, 0x34,
			  0x05, 0xad, 0xc8, 0x57, 0xa3, 0x34, 0x02, 0xec };
	uint8_t n1[16] = { 0xab, 0xae, 0x2b, 0x71, 0xec, 0xb2, 0xff, 0xff,
			   0x3e, 0x73, 0x77, 0xd1, 0x54, 0x84, 0xcb, 0xd5 };
	uint8_t n2[16] = { 0xcf, 0xc4, 0x3d, 0xff, 0xf7, 0x83, 0x65, 0x21,
			   0x6e, 0x5f, 0xa7, 0x25, 0xcc, 0xe7, 0xe8, 0xa6 };
    uint8_t a1t = 0x00;
	uint8_t a1[6] = { 0xce, 0xbf, 0x37, 0x37, 0x12, 0x56 };
    uint8_t a2t = 0x00;
    uint8_t a2[6] = { 0xc1, 0xcf, 0x2d, 0x70, 0x13, 0xa7 };
	uint8_t exp_ltk[16] = { 0x38, 0x0a, 0x75, 0x94, 0xb5, 0x22, 0x05,
				0x98, 0x23, 0xcd, 0xd7, 0x69, 0x11, 0x79,
				0x86, 0x69 };
	uint8_t exp_mackey[16] = { 0x20, 0x6e, 0x63, 0xce, 0x20, 0x6a, 0x3f,
				   0xfd, 0x02, 0x4a, 0x08, 0xa1, 0x76, 0xf1,
				   0x65, 0x29 };
	uint8_t mackey[16], ltk[16];
	int err;

	err = ble_sm_alg_f5(w, n1, n2, a1t, a1, a2t, a2, mackey, ltk);
	TEST_ASSERT_FATAL(err == 0);
    TEST_ASSERT(memcmp(mackey, exp_mackey, 16) == 0);
    TEST_ASSERT(memcmp(ltk, exp_ltk, 16) == 0);
}

TEST_CASE(ble_sm_test_case_f6)
{
	uint8_t w[16] = { 0x20, 0x6e, 0x63, 0xce, 0x20, 0x6a, 0x3f, 0xfd,
			  0x02, 0x4a, 0x08, 0xa1, 0x76, 0xf1, 0x65, 0x29 };
	uint8_t n1[16] = { 0xab, 0xae, 0x2b, 0x71, 0xec, 0xb2, 0xff, 0xff,
			   0x3e, 0x73, 0x77, 0xd1, 0x54, 0x84, 0xcb, 0xd5 };
	uint8_t n2[16] = { 0xcf, 0xc4, 0x3d, 0xff, 0xf7, 0x83, 0x65, 0x21,
			   0x6e, 0x5f, 0xa7, 0x25, 0xcc, 0xe7, 0xe8, 0xa6 };
	uint8_t r[16] = { 0xc8, 0x0f, 0x2d, 0x0c, 0xd2, 0x42, 0xda, 0x08,
			  0x54, 0xbb, 0x53, 0xb4, 0x3b, 0x34, 0xa3, 0x12 };
	uint8_t io_cap[3] = { 0x02, 0x01, 0x01 };
    uint8_t a1t = 0x00;
	uint8_t a1[6] = { 0xce, 0xbf, 0x37, 0x37, 0x12, 0x56 };
    uint8_t a2t = 0x00;
    uint8_t a2[6] = { 0xc1, 0xcf, 0x2d, 0x70, 0x13, 0xa7 };
	uint8_t exp[16] = { 0x61, 0x8f, 0x95, 0xda, 0x09, 0x0b, 0x6c, 0xd2,
			    0xc5, 0xe8, 0xd0, 0x9c, 0x98, 0x73, 0xc4, 0xe3 };
	uint8_t res[16];
	int err;

	err = ble_sm_alg_f6(w, n1, n2, r, io_cap, a1t, a1, a2t, a2, res);
	TEST_ASSERT_FATAL(err == 0);
    TEST_ASSERT(memcmp(res, exp, 16) == 0);
}

TEST_CASE(ble_sm_test_case_g2)
{
	uint8_t u[32] = { 0xe6, 0x9d, 0x35, 0x0e, 0x48, 0x01, 0x03, 0xcc,
			  0xdb, 0xfd, 0xf4, 0xac, 0x11, 0x91, 0xf4, 0xef,
			  0xb9, 0xa5, 0xf9, 0xe9, 0xa7, 0x83, 0x2c, 0x5e,
			  0x2c, 0xbe, 0x97, 0xf2, 0xd2, 0x03, 0xb0, 0x20 };
	uint8_t v[32] = { 0xfd, 0xc5, 0x7f, 0xf4, 0x49, 0xdd, 0x4f, 0x6b,
			  0xfb, 0x7c, 0x9d, 0xf1, 0xc2, 0x9a, 0xcb, 0x59,
			  0x2a, 0xe7, 0xd4, 0xee, 0xfb, 0xfc, 0x0a, 0x90,
			  0x9a, 0xbb, 0xf6, 0x32, 0x3d, 0x8b, 0x18, 0x55 };
	uint8_t x[16] = { 0xab, 0xae, 0x2b, 0x71, 0xec, 0xb2, 0xff, 0xff,
			  0x3e, 0x73, 0x77, 0xd1, 0x54, 0x84, 0xcb, 0xd5 };
	uint8_t y[16] = { 0xcf, 0xc4, 0x3d, 0xff, 0xf7, 0x83, 0x65, 0x21,
			  0x6e, 0x5f, 0xa7, 0x25, 0xcc, 0xe7, 0xe8, 0xa6 };
	uint32_t exp_val = 0x2f9ed5ba % 1000000;
	uint32_t val;
	int err;

	err = ble_sm_alg_g2(u, v, x, y, &val);
	TEST_ASSERT_FATAL(err == 0);
	TEST_ASSERT(val == exp_val);
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

/*****************************************************************************
 * $peer                                                                     *
 *****************************************************************************/

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

TEST_CASE(ble_sm_test_case_peer_lgcy_jw_good)
{
    struct ble_sm_test_lgcy_params params;

    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c},
        .resp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
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
        .resp_addr = {0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07},
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

        .passkey_info = {
            .passkey = {
                .action = BLE_SM_IOACT_INPUT,
                .passkey = 884570,
            },
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
        .resp_addr = {0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a},
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

TEST_CASE(ble_sm_test_case_peer_sc_jw_good)
{
    struct ble_sm_test_sc_params params;

    params = (struct ble_sm_test_sc_params) {
        .init_addr = { 0xca, 0x61, 0xa0, 0x67, 0x94, 0xe0 },
        .resp_addr = { 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07 },
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 0x03,
            .oob_data_flag = 0x00,
            .authreq = 0x09,
            .max_enc_key_size = 16,
            .init_key_dist = 0x0d,
            .resp_key_dist = 0x0f,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x03,
            .oob_data_flag = 0x00,
            .authreq = 0x09,
            .max_enc_key_size = 16,
            .init_key_dist = 0x01,
            .resp_key_dist = 0x01,
        },
        .public_key_req = (struct ble_sm_public_key) {
            .x = {
                0x8b, 0xc6, 0x16, 0xc4, 0xcb, 0xd4, 0xb6, 0xad,
                0x51, 0x8a, 0x21, 0x9a, 0x17, 0xf0, 0x3d, 0xab,
                0xdb, 0x4c, 0xfb, 0x70, 0xac, 0x6c, 0xa1, 0x33,
                0x6c, 0xa5, 0xa0, 0x56, 0x9b, 0x5f, 0xa7, 0xaa,
            },
            .y = {
                0xd1, 0x8c, 0xd1, 0x86, 0x79, 0x46, 0x6b, 0x42,
                0x28, 0x94, 0xf3, 0x80, 0x60, 0x43, 0xeb, 0x02,
                0x3f, 0xb9, 0xa3, 0xb4, 0x8a, 0x60, 0x98, 0x17,
                0xb5, 0x77, 0xc5, 0xc9, 0xf7, 0xae, 0x8d, 0xfc,
            },
        },
        .public_key_rsp = (struct ble_sm_public_key) {
            .x = {
                0x48, 0x26, 0x4c, 0xff, 0xe7, 0xa9, 0xa1, 0x82,
                0x4c, 0xc1, 0xdd, 0x34, 0x4a, 0x90, 0x9a, 0x7e,
                0xda, 0x1f, 0x8a, 0x61, 0x30, 0xb7, 0xe5, 0xf1,
                0x3c, 0x38, 0xc2, 0x61, 0xf7, 0x5f, 0x51, 0xdf,
            },
            .y = {
                0xd4, 0xf5, 0xd9, 0x45, 0x92, 0x54, 0x53, 0xec,
                0xe3, 0x08, 0x38, 0x3a, 0x33, 0x68, 0xab, 0x6b,
                0x59, 0xb0, 0x79, 0xec, 0x6f, 0x8e, 0xc7, 0x1a,
                0xe4, 0x72, 0x5d, 0x8c, 0x7a, 0xb9, 0x06, 0x2a,
            },
        },
        .confirm_rsp[0] = {
            .value = {
                0x03, 0x8d, 0xc2, 0x76, 0xe8, 0xcb, 0xef, 0x88,
                0x7d, 0x1f, 0xde, 0xf9, 0x8d, 0x1c, 0x7d, 0xc3,
            },
        },
        .random_req[0] = {
            .value = {
                0xb9, 0x6c, 0xe1, 0xbb, 0x14, 0xec, 0x6f, 0x18,
                0x2a, 0x5e, 0xae, 0xe3, 0x23, 0x91, 0x83, 0x3b,
            },
        },
        .random_rsp[0] = {
            .value = {
                0x73, 0x7c, 0xc6, 0xc8, 0xbd, 0x64, 0xa5, 0xab,
                0xf2, 0xd6, 0xbb, 0x82, 0x68, 0x8e, 0x1b, 0xc2,
            },
        },
        .dhkey_check_req = (struct ble_sm_dhkey_check) {
            .value = {
                0x99, 0x75, 0xfb, 0x6f, 0xf0, 0xcd, 0x27, 0x83,
                0x83, 0xf0, 0x0d, 0x6f, 0x54, 0xa1, 0x8a, 0xf0,
            }
        },
        .dhkey_check_rsp = (struct ble_sm_dhkey_check) {
            .value = {
                0x4e, 0x78, 0x60, 0xcd, 0x2b, 0xdd, 0xa3, 0x03,
                0x02, 0x3f, 0x9a, 0x05, 0x1d, 0xcb, 0x93, 0xd9,
            }
        },
        .pair_alg = BLE_SM_PAIR_ALG_JW,
        .authenticated = 0,
        .ltk = {
            0xdd, 0x81, 0x53, 0x79, 0x75, 0x63, 0x50, 0x79,
            0x72, 0xa7, 0x2e, 0x96, 0xd6, 0x7b, 0x2b, 0xdd,
        },
        .our_priv_key = {
            0x25, 0xd6, 0x7f, 0x0d, 0xf2, 0x89, 0x04, 0x05,
            0xe2, 0xd3, 0x62, 0xb1, 0x64, 0xee, 0x01, 0xad,
            0x1c, 0xa2, 0x54, 0xae, 0x43, 0x4e, 0xa9, 0x09,
            0x67, 0xdc, 0xc1, 0x7c, 0x98, 0x63, 0x80, 0xd2
        }
    };
    ble_sm_test_util_peer_sc_good(&params);
}

TEST_CASE(ble_sm_test_case_peer_sc_numcmp_good)
{
    struct ble_sm_test_sc_params params;

    params = (struct ble_sm_test_sc_params) {
        .init_addr = { 0xca, 0x61, 0xa0, 0x67, 0x94, 0xe0 },
        .resp_addr = { 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07 },
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 0x01,
            .oob_data_flag = 0x00,
            .authreq = 0x0d,
            .max_enc_key_size = 16,
            .init_key_dist = 0x0d,
            .resp_key_dist = 0x0f,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x01,
            .oob_data_flag = 0x00,
            .authreq = 0x0d,
            .max_enc_key_size = 16,
            .init_key_dist = 0x01,
            .resp_key_dist = 0x01,
        },
        .public_key_req = (struct ble_sm_public_key) {
            .x = {
                0xbf, 0x85, 0xf4, 0x0b, 0xf2, 0x93, 0x05, 0x38,
                0xfd, 0x09, 0xf3, 0x80, 0xc1, 0x9c, 0x07, 0xee,
                0x4b, 0x48, 0x28, 0x17, 0x18, 0x03, 0xc8, 0x8f,
                0x5d, 0xdc, 0x7e, 0xa1, 0xf5, 0x06, 0x17, 0x12,
            },

            .y = {
                0xee, 0x63, 0x19, 0x9e, 0x9e, 0x66, 0xd2, 0xee,
                0x8a, 0xf9, 0xc7, 0xf6, 0x2b, 0x56, 0xec, 0xda,
                0x8a, 0x18, 0xdb, 0x49, 0x55, 0x8d, 0x05, 0x08,
                0x9a, 0x9c, 0xe3, 0xf4, 0xe8, 0x31, 0x4c, 0x1d,
            },
        },
        .public_key_rsp = (struct ble_sm_public_key) {
            .x = {
                0x15, 0x88, 0xed, 0xe4, 0xa4, 0x81, 0xa7, 0xed,
                0x1a, 0xff, 0x69, 0x66, 0x3d, 0x4d, 0xf1, 0x27,
                0xc2, 0xb9, 0x03, 0xaf, 0x65, 0x9e, 0x45, 0x86,
                0x80, 0xba, 0xc8, 0x4a, 0x7d, 0xbc, 0x17, 0x6e,
            },

            .y = {
                0xb3, 0x34, 0x36, 0x36, 0x77, 0x5c, 0x28, 0xaf,
                0x73, 0x25, 0x0e, 0xff, 0x29, 0xc4, 0xe8, 0x23,
                0xeb, 0x35, 0xa7, 0x47, 0x29, 0x6e, 0xbd, 0x29,
                0x93, 0x26, 0x07, 0xfd, 0x9c, 0x93, 0xf3, 0xd6,
            },
        },
        .confirm_rsp[0] = (struct ble_sm_pair_confirm) {
            .value = {
                0x7c, 0x45, 0xb0, 0x55, 0xce, 0x22, 0x61, 0x57,
                0x68, 0x2f, 0x2d, 0x3a, 0xce, 0xf5, 0x80, 0xba,
            },
        },
        .random_req[0] = (struct ble_sm_pair_random) {
            .value = {
                0x4e, 0xfb, 0x89, 0x84, 0xfd, 0xa1, 0xed, 0x65,
                0x0e, 0x57, 0x11, 0xe6, 0x94, 0xd5, 0x18, 0x78,
            },
        },
        .random_rsp[0] = (struct ble_sm_pair_random) {
            .value = {
                0x29, 0x06, 0xbc, 0x65, 0x1d, 0xe0, 0x95, 0xde,
                0x79, 0xee, 0xd9, 0x41, 0x86, 0x6f, 0x35, 0x75,
            },
        },
        .dhkey_check_req = (struct ble_sm_dhkey_check) {
            .value = {
                0xbc, 0x85, 0xc2, 0x2e, 0xe5, 0x19, 0xb0, 0xdd,
                0xf7, 0xed, 0x5d, 0xdd, 0xa7, 0xa7, 0xc0, 0x54,
            }
        },
        .dhkey_check_rsp = (struct ble_sm_dhkey_check) {
            .value = {
                0x65, 0x82, 0x74, 0xd0, 0x29, 0xcb, 0xe9, 0x9a,
                0xed, 0x9c, 0xa4, 0xbb, 0x39, 0x5c, 0xef, 0xfd,
            }
        },
        .pair_alg = BLE_SM_PAIR_ALG_NUMCMP,
        .authenticated = 1,
        .ltk = {
            0xb0, 0x43, 0x9c, 0xed, 0x93, 0x73, 0x5c, 0xfb,
            0x7f, 0xfd, 0xd9, 0x06, 0xad, 0xbc, 0x7c, 0xd0,
        },
        .our_priv_key = {
            0x8c, 0x7a, 0x1a, 0x8e, 0x8e, 0xfa, 0x2d, 0x2e,
            0xa6, 0xd5, 0xa2, 0x51, 0x86, 0xe4, 0x31, 0x1c,
            0x1e, 0xf8, 0x13, 0x33, 0x08, 0x76, 0x38, 0x6e,
            0xa0, 0x06, 0x88, 0x6d, 0x9d, 0x96, 0x43, 0x4e,
        },

        .passkey_info = {
            .passkey = {
                .action = BLE_SM_IOACT_NUMCMP,
                .numcmp_accept = 1,
            },

            .exp_numcmp = 476091,
            .io_before_rx = 0,
        },
    };
    ble_sm_test_util_peer_sc_good(&params);
}

TEST_CASE(ble_sm_test_case_peer_sc_passkey_good)
{
    struct ble_sm_test_sc_params params;

    params = (struct ble_sm_test_sc_params) {
        .init_addr = { 0xca, 0x61, 0xa0, 0x67, 0x94, 0xe0 },
        .resp_addr = { 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07 },
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 0x04,
            .oob_data_flag = 0x00,
            .authreq = 0x0d,
            .max_enc_key_size = 16,
            .init_key_dist = 0x0d,
            .resp_key_dist = 0x0f,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x02,
            .oob_data_flag = 0x00,
            .authreq = 0x0d,
            .max_enc_key_size = 16,
            .init_key_dist = 0x01,
            .resp_key_dist = 0x01,
        },
        .public_key_req = (struct ble_sm_public_key) {
            .x = {
                0x82,0xcf, 0x99, 0xe1, 0x56, 0xe5, 0xa3, 0xbc,
                0x1d,0xd5, 0xab, 0x32, 0x90, 0x65, 0x9a, 0x6c,
                0x15,0x34, 0xcf, 0xa3, 0xb1, 0xa3, 0x88, 0x9f,
                0xb0,0x86, 0xc7, 0x17, 0xd4, 0x1a, 0x88, 0x8d,
            },
            .y = {
                0x09,0xfb, 0x98, 0xd2, 0xf8, 0x94, 0x73, 0xed,
                0x81,0x09, 0x08, 0x00, 0x3f, 0x14, 0x87, 0x6c,
                0xc6,0x83, 0x36, 0xc1, 0x9d, 0x8e, 0x39, 0xea,
                0x3d,0x79, 0x9d, 0x8c, 0x7f, 0xcf, 0x00, 0xbd,
            },
        },
        .public_key_rsp = (struct ble_sm_public_key) {
            .x = {
                0x9e,0x05, 0x7b, 0x5b, 0x61, 0xf2, 0x57, 0xbe,
                0xb7,0x73, 0x3f, 0x72, 0x03, 0xc9, 0x51, 0x44,
                0x9e,0xe1, 0xef, 0xe8, 0x28, 0x67, 0xcc, 0xbe,
                0x20,0x13, 0xb5, 0xcc, 0x71, 0x39, 0x85, 0xaf,
            },
            .y = {
                0xff,0xac, 0x2c, 0x48, 0x57, 0xd9, 0x49, 0x9d,
                0x27,0x8d, 0x99, 0x30, 0x01, 0xe3, 0x30, 0x72,
                0xd4,0x7b, 0x5c, 0x64, 0x7e, 0x80, 0xd5, 0x09,
                0x3a,0x70, 0x44, 0x61, 0xc9, 0x36, 0x50, 0x6b,
            },
        },
        .confirm_req[0] = {
            .value = {
                0x2f,0xa2, 0x10, 0xff, 0xd8, 0x1c, 0x5c, 0x93,
                0xd9,0x96, 0xf2, 0x8d, 0x7a, 0x34, 0xef, 0xbc,
            },
        },
        .confirm_rsp[0] = {
            .value = {
                0xe7,0xd6, 0x11, 0xd9, 0xb1, 0x69, 0xe1, 0x0b,
                0xbb,0x07, 0x63, 0x38, 0x62, 0x75, 0x5f, 0x2b,
            },
        },
        .random_req[0] = {
            .value = {
                0xfa,0x4c, 0xa5, 0x7e, 0xfd, 0x11, 0x68, 0xeb,
                0xf2,0x6a, 0xcd, 0x50, 0x7a, 0x92, 0x36, 0xf1,
            },
        },
        .random_rsp[0] = {
            .value = {
                0xa5,0xec, 0xc8, 0x7f, 0xe2, 0x19, 0x05, 0x5e,
                0x2e,0x0d, 0x12, 0x65, 0xd5, 0x11, 0x98, 0x7e,
            },
        },
        .confirm_req[1] = {
            .value = {
                0x5b,0xff, 0xad, 0x40, 0x98, 0xb8, 0xc8, 0xd9,
                0x1e,0xde, 0x11, 0x17, 0xcc, 0x16, 0x09, 0x08,
            },
        },
        .confirm_rsp[1] = {
            .value = {
                0xe1,0xd7, 0xb3, 0x1b, 0x2b, 0xa8, 0x38, 0x29,
                0xa4,0xd7, 0x62, 0x4b, 0xf2, 0x59, 0x51, 0xdc,
            },
        },
        .random_req[1] = {
            .value = {
                0xac,0x6b, 0xc5, 0x83, 0x9f, 0x08, 0x05, 0xed,
                0x0e,0x76, 0x14, 0xce, 0x57, 0x07, 0x6e, 0x0b,
            },
        },
        .random_rsp[1] = {
            .value = {
                0x6d,0x7d, 0x10, 0x78, 0x76, 0x09, 0x34, 0xca,
                0xa9,0x70, 0x5b, 0x59, 0xfb, 0xfd, 0x15, 0x17,
            },
        },
        .confirm_req[2] = {
            .value = {
                0x5c,0xb7, 0x96, 0x9a, 0x5b, 0x8a, 0x8f, 0x00,
                0xae,0x84, 0xa1, 0x72, 0xc8, 0x4d, 0xd0, 0x85,
            },
        },
        .confirm_rsp[2] = {
            .value = {
                0xe7,0x0e, 0x80, 0x89, 0xf7, 0x04, 0x48, 0x02,
                0x06,0xd3, 0xd2, 0x09, 0x88, 0x73, 0x44, 0x3c,
            },
        },
        .random_req[2] = {
            .value = {
                0xc7,0x83, 0xec, 0x81, 0xf4, 0xe0, 0xdd, 0x41,
                0xaf,0x28, 0x6f, 0xbe, 0xb4, 0x92, 0x30, 0xf7,
            },
        },
        .random_rsp[2] = {
            .value = {
                0x38,0x95, 0xbb, 0x5f, 0x8a, 0x67, 0xb7, 0x8f,
                0x95,0x2a, 0x7e, 0x3b, 0xac, 0x27, 0xf7, 0x86,
            },
        },
        .confirm_req[3] = {
            .value = {
                0x01,0x3d, 0xee, 0x42, 0xb7, 0xfa, 0xc1, 0x82,
                0x51,0x02, 0x42, 0x51, 0xa2, 0xbb, 0x20, 0x0e,
            },
        },
        .confirm_rsp[3] = {
            .value = {
                0x99,0x01, 0x88, 0x1c, 0x45, 0x2c, 0xa6, 0x6f,
                0x6b,0xca, 0x9d, 0x8d, 0x32, 0xfe, 0x2f, 0x02,
            },
        },
        .random_req[3] = {
            .value = {
                0x5e,0x39, 0x5f, 0x91, 0x9d, 0xed, 0xf9, 0x93,
                0xec,0xd5, 0x6b, 0x39, 0xb6, 0x77, 0x3a, 0x94,
            },
        },
        .random_rsp[3] = {
            .value = {
                0xe1,0x06, 0x02, 0xcc, 0x80, 0x9d, 0xd1, 0x47,
                0x42,0xa6, 0xca, 0x52, 0x6a, 0x1b, 0x9e, 0x7a,
            },
        },
        .confirm_req[4] = {
            .value = {
                0x2e,0xcd, 0x64, 0x01, 0x39, 0x74, 0x59, 0xfc,
                0x78,0xeb, 0x48, 0x4f, 0xa3, 0x8a, 0x6e, 0x2f,
            },
        },
        .confirm_rsp[4] = {
            .value = {
                0x9e,0x27, 0x31, 0x14, 0x35, 0x95, 0x0a, 0x3d,
                0x0b,0x60, 0xd9, 0x78, 0xd9, 0xfc, 0x5d, 0xc4,
            },
        },
        .random_req[4] = {
            .value = {
                0x3f,0x35, 0xa0, 0x90, 0xbf, 0x5a, 0x16, 0xcd,
                0x32,0x0c, 0x88, 0x3a, 0x3c, 0x28, 0x89, 0x29,
            },
        },
        .random_rsp[4] = {
            .value = {
                0xde,0xb9, 0x95, 0x46, 0x55, 0xed, 0xaa, 0x76,
                0xb3,0xae, 0x59, 0x92, 0xec, 0x3e, 0xe6, 0x17,
            },
        },
        .confirm_req[5] = {
            .value = {
                0x61,0x65, 0x44, 0xa6, 0x35, 0x88, 0x34, 0x80,
                0xeb,0xce, 0xc4, 0xc3, 0xa0, 0xf4, 0xc4, 0xdb,
            },
        },
        .confirm_rsp[5] = {
            .value = {
                0xc5,0xdd, 0x46, 0x1c, 0x42, 0x8a, 0xe9, 0x5c,
                0x71,0xb3, 0x55, 0x5f, 0x35, 0xdd, 0xb3, 0x79,
            },
        },
        .random_req[5] = {
            .value = {
                0xfa,0xae, 0x45, 0x2a, 0x17, 0xff, 0xfe, 0xe1,
                0x05,0x58, 0xa4, 0xbd, 0x40, 0x9e, 0x05, 0x0c,
            },
        },
        .random_rsp[5] = {
            .value = {
                0x7b,0xef, 0xca, 0x4e, 0x87, 0x80, 0xef, 0xd9,
                0x8f,0xa5, 0x61, 0x8d, 0x73, 0xb2, 0x38, 0xcd,
            },
        },
        .confirm_req[6] = {
            .value = {
                0x38,0xfb, 0x19, 0x1e, 0xc7, 0xec, 0x11, 0xed,
                0xaa,0xc0, 0xa5, 0xaf, 0x79, 0x8f, 0x96, 0x82,
            },
        },
        .confirm_rsp[6] = {
            .value = {
                0x9d,0x0f, 0x44, 0x66, 0xd7, 0x05, 0x24, 0x81,
                0xff,0x32, 0x5c, 0xda, 0x25, 0x2c, 0x92, 0xec,
            },
        },
        .random_req[6] = {
            .value = {
                0x77,0xc3, 0x35, 0x7b, 0x78, 0xbc, 0xe4, 0xc1,
                0x9c,0x13, 0xbd, 0xd2, 0xd2, 0xc6, 0x1e, 0x50,
            },
        },
        .random_rsp[6] = {
            .value = {
                0x5d,0xd5, 0x12, 0x06, 0x66, 0xd1, 0x74, 0x3a,
                0xb7,0x5e, 0x54, 0x0d, 0xa2, 0x0f, 0xa4, 0x7c,
            },
        },
        .confirm_req[7] = {
            .value = {
                0x32,0xe8, 0xf4, 0xc9, 0xed, 0xa7, 0xd3, 0x95,
                0x94,0x0f, 0x08, 0xfb, 0xaa, 0xce, 0x84, 0x06,
            },
        },
        .confirm_rsp[7] = {
            .value = {
                0x5a,0x59, 0xb8, 0xbe, 0x70, 0x33, 0x99, 0xf9,
                0x07,0xff, 0x70, 0xe2, 0x10, 0x53, 0xf6, 0xf1,
            },
        },
        .random_req[7] = {
            .value = {
                0x2f,0x07, 0x42, 0x95, 0xb2, 0x97, 0xcf, 0xa4,
                0xde,0x2e, 0x88, 0xa1, 0x2a, 0x3c, 0x89, 0x6e,
            },
        },
        .random_rsp[7] = {
            .value = {
                0x23,0x21, 0x9f, 0x2f, 0xfc, 0xb1, 0x80, 0xce,
                0x0f,0x3d, 0xe6, 0xee, 0xbe, 0xab, 0x28, 0xe2,
            },
        },
        .confirm_req[8] = {
            .value = {
                0x20,0xc3, 0x49, 0x90, 0x13, 0xad, 0x99, 0x69,
                0xf1,0xd3, 0x71, 0x95, 0xb0, 0x54, 0xc4, 0xce,
            },
        },
        .confirm_rsp[8] = {
            .value = {
                0x02,0x50, 0x29, 0x57, 0xc5, 0x26, 0x1f, 0x90,
                0x05,0x8b, 0x8c, 0x20, 0xe9, 0xf7, 0xdd, 0x03,
            },
        },
        .random_req[8] = {
            .value = {
                0xfd,0x09, 0x39, 0xc8, 0xc6, 0x57, 0xcf, 0x09,
                0xc7,0x89, 0xab, 0xcc, 0x05, 0xda, 0xb7, 0x05,
            },
        },
        .random_rsp[8] = {
            .value = {
                0x0d,0xed, 0xfd, 0x38, 0x76, 0x56, 0xa0, 0xcc,
                0xf4,0x66, 0x09, 0xcb, 0x0f, 0x8c, 0xa5, 0x6a,
            },
        },
        .confirm_req[9] = {
            .value = {
                0xce,0x50, 0x75, 0xda, 0x6a, 0x88, 0x98, 0x00,
                0xe5,0x4d, 0x7d, 0xcf, 0xde, 0x00, 0xc5, 0x18,
            },
        },
        .confirm_rsp[9] = {
            .value = {
                0x0b,0xd4, 0x0c, 0xba, 0xfe, 0x9a, 0xf0, 0x31,
                0x13,0x21, 0x70, 0x11, 0x7c, 0xde, 0x26, 0x75,
            },
        },
        .random_req[9] = {
            .value = {
                0x24,0x85, 0x91, 0xef, 0x4b, 0x8e, 0x2b, 0x67,
                0x39,0x93, 0x7c, 0x7e, 0x4b, 0x76, 0x10, 0x44,
            },
        },
        .random_rsp[9] = {
            .value = {
                0x2d,0xa3, 0x52, 0xc7, 0x09, 0x0b, 0xda, 0x81,
                0xbd,0x10, 0x30, 0x57, 0x20, 0x2c, 0x5b, 0x90,
            },
        },
        .confirm_req[10] = {
            .value = {
                0x7a,0x3e, 0x5b, 0x55, 0x08, 0x63, 0x32, 0x65,
                0xaa,0xf8, 0xc1, 0xd2, 0xd6, 0x01, 0x1a, 0x7e,
            },
        },
        .confirm_rsp[10] = {
            .value = {
                0x46,0xdd, 0x55, 0x46, 0x90, 0x55, 0xc8, 0x0b,
                0x9e,0x7b, 0x81, 0xbb, 0x71, 0x11, 0x7f, 0x58,
            },
        },
        .random_req[10] = {
            .value = {
                0xc0,0xe0, 0xa0, 0x10, 0xfc, 0xbc, 0xd9, 0x54,
                0x48,0xdc, 0xcf, 0x8f, 0xda, 0xe6, 0x6a, 0x5f,
            },
        },
        .random_rsp[10] = {
            .value = {
                0x22,0x3c, 0xd5, 0x79, 0x3e, 0xb3, 0x64, 0x3c,
                0x46,0xba, 0xbc, 0x74, 0x4a, 0xc0, 0x5c, 0x18,
            },
        },
        .confirm_req[11] = {
            .value = {
                0x29,0x6e, 0x96, 0xb8, 0xc1, 0x74, 0x27, 0x4c,
                0x56,0xe0, 0x3e, 0x33, 0x3a, 0xca, 0xf9, 0x68,
            },
        },
        .confirm_rsp[11] = {
            .value = {
                0xf0,0xe5, 0x3e, 0x5b, 0x68, 0x82, 0x40, 0x53,
                0x98,0x1b, 0x57, 0xdd, 0x4f, 0x40, 0x23, 0xa2,
            },
        },
        .random_req[11] = {
            .value = {
                0xb8,0xd9, 0x75, 0x53, 0x65, 0x42, 0x5a, 0x0c,
                0x82,0x74, 0x55, 0x2c, 0x40, 0x5b, 0x02, 0xed,
            },
        },
        .random_rsp[11] = {
            .value = {
                0x3f,0x88, 0x97, 0x63, 0x72, 0xe7, 0x84, 0x29,
                0x2d,0xcc, 0xd3, 0xc6, 0xbb, 0xa8, 0x6a, 0xb8,
            },
        },
        .confirm_req[12] = {
            .value = {
                0x6c,0x1f, 0x97, 0xda, 0x08, 0x2c, 0x9b, 0x8b,
                0xf2,0xb0, 0x7a, 0x3e, 0xdf, 0xbf, 0x1e, 0x70,
            },
        },
        .confirm_rsp[12] = {
            .value = {
                0x18,0xfe, 0x73, 0x49, 0xe0, 0x7a, 0x75, 0xd3,
                0x68,0x52, 0x2d, 0xae, 0x76, 0xf5, 0xc1, 0x4f,
            },
        },
        .random_req[12] = {
            .value = {
                0xb3,0xf1, 0xd0, 0xa9, 0x5a, 0x69, 0xa7, 0x33,
                0xb9,0xdd, 0x04, 0x7d, 0x19, 0xc9, 0x0e, 0xdc,
            },
        },
        .random_rsp[12] = {
            .value = {
                0x62,0x10, 0x99, 0xae, 0xc0, 0x5c, 0x00, 0x59,
                0xda,0x14, 0x38, 0x41, 0x8e, 0x70, 0xd9, 0x51,
            },
        },
        .confirm_req[13] = {
            .value = {
                0x3f,0x62, 0xd8, 0x3c, 0x29, 0x25, 0xf7, 0x96,
                0x1c,0x01, 0x4f, 0x27, 0x19, 0x89, 0x51, 0xaf,
            },
        },
        .confirm_rsp[13] = {
            .value = {
                0x4c,0x9f, 0xff, 0xef, 0x9c, 0x04, 0x4f, 0xa7,
                0xb9,0x8f, 0x9b, 0x4c, 0xbb, 0xaa, 0x8c, 0x44,
            },
        },
        .random_req[13] = {
            .value = {
                0x73,0x5e, 0xef, 0x3b, 0x9b, 0x45, 0xf1, 0x31,
                0xcd,0x0c, 0x26, 0x7c, 0xe9, 0xfc, 0x04, 0x5d,
            },
        },
        .random_rsp[13] = {
            .value = {
                0x38,0x53, 0x68, 0xaa, 0xdf, 0x20, 0xd4, 0xef,
                0x11,0x2b, 0xac, 0xe2, 0x7c, 0x11, 0x00, 0x10,
            },
        },
        .confirm_req[14] = {
            .value = {
                0x13,0x1d, 0xc2, 0x1a, 0x39, 0x01, 0xbf, 0xf9,
                0x87,0xdd, 0xb4, 0xd3, 0xe5, 0xe8, 0x8c, 0x42,
            },
        },
        .confirm_rsp[14] = {
            .value = {
                0x34,0xc5, 0xda, 0xc1, 0x4c, 0xa8, 0x47, 0x31,
                0x76,0x5e, 0x4f, 0xcd, 0x37, 0x77, 0x04, 0x10,
            },
        },
        .random_req[14] = {
            .value = {
                0x59,0x02, 0xa1, 0x35, 0xd7, 0x12, 0x9a, 0x51,
                0xf4,0xad, 0x32, 0xdf, 0x3b, 0xa4, 0x3a, 0xae,
            },
        },
        .random_rsp[14] = {
            .value = {
                0x47,0x9e, 0x47, 0x0f, 0x53, 0xad, 0x74, 0x57,
                0x1a,0xdc, 0x1b, 0x5f, 0xc0, 0xf0, 0x00, 0x1a,
            },
        },
        .confirm_req[15] = {
            .value = {
                0x4c,0xa7, 0x4f, 0x64, 0xc1, 0xb7, 0xfe, 0xa9,
                0xcb,0x94, 0x5f, 0x39, 0x9a, 0x1e, 0xbf, 0xb4,
            },
        },
        .confirm_rsp[15] = {
            .value = {
                0xed,0x61, 0x96, 0xaa, 0xd5, 0x27, 0x23, 0x32,
                0xc4,0x80, 0x52, 0x3c, 0x81, 0x45, 0x37, 0xc3,
            },
        },
        .random_req[15] = {
            .value = {
                0xe4,0x2e, 0x04, 0xce, 0x7d, 0xbe, 0xb5, 0x5b,
                0xb5,0xd5, 0xba, 0x60, 0x3f, 0x88, 0x05, 0xf1,
            },
        },
        .random_rsp[15] = {
            .value = {
                0x64,0xa0, 0x95, 0x1f, 0x01, 0x7e, 0x00, 0x8d,
                0x00,0x40, 0xa1, 0x2f, 0x90, 0x42, 0xcb, 0x15,
            },
        },
        .confirm_req[16] = {
            .value = {
                0xce,0x32, 0x34, 0x33, 0x44, 0xbc, 0xb2, 0x8e,
                0x64,0xfd, 0x8a, 0xeb, 0x41, 0xf9, 0x4d, 0xd7,
            },
        },
        .confirm_rsp[16] = {
            .value = {
                0xb4,0x69, 0x44, 0xa5, 0x0a, 0x88, 0xfe, 0x39,
                0x0c,0x7f, 0xc9, 0x8b, 0x7f, 0x7e, 0x6e, 0x45,
            },
        },
        .random_req[16] = {
            .value = {
                0xff,0x56, 0x8f, 0x97, 0x85, 0x6e, 0xac, 0xd1,
                0x00,0x0e, 0x8d, 0x2f, 0xf1, 0x68, 0xf9, 0xf2,
            },
        },
        .random_rsp[16] = {
            .value = {
                0xc0,0x84, 0x5c, 0x92, 0x0d, 0x71, 0xdd, 0x88,
                0xab,0x13, 0xa4, 0xf4, 0x06, 0x54, 0x4f, 0x1b,
            },
        },
        .confirm_req[17] = {
            .value = {
                0x59,0xe5, 0x79, 0xdd, 0x1d, 0xd3, 0xa5, 0xf8,
                0xba,0x7f, 0xf6, 0xc9, 0xaf, 0xca, 0xe8, 0xbc,
            },
        },
        .confirm_rsp[17] = {
            .value = {
                0x39,0xd4, 0xc3, 0x39, 0xd9, 0x0b, 0x07, 0xe1,
                0x6c,0x68, 0x6b, 0xef, 0xc8, 0x87, 0x93, 0x4f,
            },
        },
        .random_req[17] = {
            .value = {
                0xaa,0xf9, 0xa4, 0xc9, 0xe0, 0x6f, 0xfc, 0x7b,
                0x22,0x5a, 0xb6, 0x3d, 0x50, 0x3e, 0xd1, 0x02,
            },
        },
        .random_rsp[17] = {
            .value = {
                0x22,0x90, 0x0e, 0xd4, 0x92, 0xb7, 0xa9, 0x16,
                0x39,0x2a, 0x1c, 0xd2, 0x8b, 0xbf, 0xf7, 0x43,
            },
        },
        .confirm_req[18] = {
            .value = {
                0x69,0xb9, 0x75, 0x4b, 0x80, 0xe8, 0x93, 0xbb,
                0x57,0xe8, 0x37, 0x3c, 0x69, 0x47, 0x40, 0xb7,
            },
        },
        .confirm_rsp[18] = {
            .value = {
                0xd8,0x69, 0x55, 0xf4, 0x84, 0x54, 0x95, 0x98,
                0xdf,0x13, 0x69, 0x62, 0xbc, 0xea, 0x46, 0x16,
            },
        },
        .random_req[18] = {
            .value = {
                0xf6,0x26, 0xc3, 0xec, 0xdc, 0xc8, 0x85, 0x78,
                0xd4,0xe4, 0x0e, 0x1b, 0x05, 0x6d, 0x33, 0x20,
            },
        },
        .random_rsp[18] = {
            .value = {
                0xf7,0xb6, 0x8a, 0xdc, 0xd5, 0xdd, 0x0f, 0x2f,
                0x6a,0xab, 0x94, 0xcd, 0x68, 0xf4, 0x2d, 0xda,
            },
        },
        .confirm_req[19] = {
            .value = {
                0x07,0xea, 0xe0, 0x61, 0x90, 0x23, 0x52, 0xd8,
                0x90,0x50, 0xe1, 0x7c, 0x10, 0x1e, 0x41, 0xf0,
            },
        },
        .confirm_rsp[19] = {
            .value = {
                0xed,0xd9, 0x02, 0xb7, 0x03, 0xd9, 0xe8, 0x87,
                0x4a,0x48, 0x43, 0x96, 0xa0, 0x59, 0xb1, 0xa8,
            },
        },
        .random_req[19] = {
            .value = {
                0x88,0x00, 0xe6, 0xfc, 0x1f, 0x6f, 0x92, 0x0e,
                0x33,0xeb, 0x18, 0xc3, 0x5d, 0x7e, 0x37, 0x0e,
            },
        },
        .random_rsp[19] = {
            .value = {
                0xfb,0xcf, 0x24, 0x02, 0x4a, 0xe4, 0x92, 0x47,
                0xa9,0x67, 0x43, 0x33, 0x22, 0x95, 0x0a, 0xd3,
            },
        },
        .dhkey_check_req = (struct ble_sm_dhkey_check) {
            .value = {
                0xcb, 0xb5, 0x5f, 0x62, 0xde, 0x14, 0xc7, 0x30,
                0xaa, 0x58, 0x23, 0x3c, 0xdb, 0x39, 0xc1, 0xb9,
            }
        },
        .dhkey_check_rsp = (struct ble_sm_dhkey_check) {
            .value = {
                0x52, 0x00, 0xeb, 0xe5, 0xd7, 0xa6, 0x5c, 0xc4,
                0xf6, 0x6d, 0xa5, 0x7c, 0xff, 0xa8, 0x7c, 0x3f,
            }
        },
        .pair_alg = BLE_SM_PAIR_ALG_PASSKEY,
        .authenticated = 1,
        .ltk = {
            0x45, 0x8a, 0x1b, 0x92, 0x1c, 0x2b, 0xdf, 0x3e,
            0xda, 0xf9, 0x5c, 0xdf, 0x96, 0x59, 0xb7, 0x13,
        },
        .our_priv_key = {
            0x20, 0x62, 0xba, 0x77, 0xfe, 0xdd, 0x34, 0xf7,
            0x30, 0xcc, 0x35, 0x3f, 0xa3, 0x35, 0x02, 0x95,
            0x2d, 0x03, 0x78, 0x4e, 0xe8, 0x80, 0xaa, 0x4d,
            0x8c, 0xec, 0x93, 0x02, 0x28, 0x55, 0x2f, 0xf8,
        },

        .passkey_info = {
            .passkey = {
                .action = BLE_SM_IOACT_INPUT,
                .passkey = 873559,
            },

            .io_before_rx = 0,
        },
    };
    ble_sm_test_util_peer_sc_good(&params);
}

/*****************************************************************************
 * $us                                                                       *
 *****************************************************************************/

TEST_CASE(ble_sm_test_case_us_fail_inval)
{
    struct ble_sm_test_lgcy_params params;

    /* Invalid IO capabiltiies. */
    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c},
        .resp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
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
        .resp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
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
        .resp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
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
        .resp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
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
        .resp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
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
        .resp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
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
        .resp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
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
        .resp_addr = {0x03, 0x02, 0x01, 0x50, 0x13, 0x00},
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

TEST_CASE(ble_sm_test_case_us_lgcy_jw_good)
{
    struct ble_sm_test_lgcy_params params;

    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0x06, 0x05, 0x04, 0x03, 0x02, 0x01},
        .resp_addr = {0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a},
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
 * Master: peer.
 * We send a security request.
 * We accept pairing request sent in response.
 */
TEST_CASE(ble_sm_test_case_us_sec_req_pair)
{
    struct ble_sm_test_lgcy_params params;

    params = (struct ble_sm_test_lgcy_params) {
        .init_addr = {0xe1, 0xfc, 0xda, 0xf4, 0xb7, 0x6c},
        .resp_addr = {0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07},
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

        .passkey_info = {
            .passkey = {
                .action = BLE_SM_IOACT_INPUT,
                .passkey = 884570,
            },
            .io_before_rx = 1,
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
 * Master: us
 * Secure connections
 * Pair algorithm: just works
 */
TEST_CASE(ble_sm_test_case_us_sc_jw_good)
{
    struct ble_sm_test_sc_params params;

    params = (struct ble_sm_test_sc_params) {
        .init_addr = { 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 },
        .resp_addr = { 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07 },
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 0x03,
            .oob_data_flag = 0x00,
            .authreq = 0x09,
            .max_enc_key_size = 16,
            .init_key_dist = 0x01,
            .resp_key_dist = 0x01,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x03,
            .oob_data_flag = 0x00,
            .authreq = 0x09,
            .max_enc_key_size = 16,
            .init_key_dist = 0x01,
            .resp_key_dist = 0x01,
        },
        .public_key_req = (struct ble_sm_public_key) {
            .x = {
                0x3b, 0x55, 0x78, 0xad, 0xa1, 0xda, 0xec, 0x54,
                0x10, 0x57, 0x35, 0x5f, 0x63, 0xb0, 0xec, 0xdb,
                0xdf, 0xb2, 0x31, 0x0d, 0xca, 0xf6, 0x3a, 0xa8,
                0x82, 0xbd, 0xe3, 0x27, 0x12, 0xa5, 0xbe, 0x81
            },
            .y = {
                0x07, 0x32, 0x82, 0x00, 0xbe, 0xc1, 0x39, 0xb5,
                0x16, 0x7f, 0xbb, 0x7a, 0x79, 0xb8, 0xba, 0xf4,
                0x45, 0x02, 0xa7, 0xe6, 0xeb, 0x58, 0xa4, 0x4e,
                0xff, 0xd3, 0x55, 0x55, 0xd4, 0xb5, 0x21, 0x01
            }
        },
        .public_key_rsp = (struct ble_sm_public_key) {
            .x = {
                0x05, 0xc5, 0x2e, 0x5e, 0x47, 0xcf, 0xd1, 0x1c,
                0x88, 0x47, 0xbb, 0x96, 0x8f, 0x3a, 0xd9, 0xf3,
                0xce, 0x48, 0x49, 0xa4, 0xef, 0x08, 0x95, 0xb3,
                0xcc, 0x52, 0x75, 0xad, 0x3e, 0xb3, 0xce, 0x9d
            },
            .y = {
                0x11, 0x68, 0x8e, 0x4e, 0x97, 0xae, 0x1b, 0x7e,
                0x20, 0x57, 0x46, 0x0c, 0xc5, 0x9f, 0xf7, 0x1a,
                0x9b, 0x73, 0x23, 0x1e, 0x56, 0x45, 0x2f, 0x6a,
                0xb5, 0xf4, 0x62, 0x0e, 0xca, 0xcd, 0x57, 0x23
            },
        },
        .confirm_rsp[0] = {
            .value = {
                0x7a, 0x1b, 0xd1, 0x71, 0x29, 0x20, 0x31, 0x47,
                0x67, 0x3c, 0x72, 0x4a, 0xa6, 0xad, 0x8a, 0x16
            },
        },
        .random_req[0] = {
            .value = {
                0xa1, 0x7a, 0x73, 0x52, 0xc0, 0x5f, 0x97, 0xd5,
                0x9b, 0x37, 0xcb, 0x50, 0x6b, 0x2e, 0x4f, 0xe8
            },
        },
        .random_rsp[0] = {
            .value = {
                0x58, 0xec, 0x42, 0xef, 0xe5, 0x19, 0x54, 0xa5,
                0x7a, 0xa2, 0x7f, 0xae, 0x45, 0x49, 0x08, 0x44
            },
        },
        .dhkey_check_req = (struct ble_sm_dhkey_check) {
            .value = {
                0xfe, 0x60, 0x72, 0x4f, 0x17, 0x5b, 0x45, 0xde,
                0xdc, 0x3d, 0x79, 0x71, 0x35, 0xc7, 0x14, 0x65
            }
        },
        .dhkey_check_rsp = (struct ble_sm_dhkey_check) {
            .value = {
                0x63, 0x11, 0xd1, 0xbf, 0xff, 0x86, 0x6d, 0xbf,
                0xad, 0xf0, 0x08, 0x8c, 0x4d, 0x6e, 0x27, 0xa8
            }
        },
        .pair_alg = BLE_SM_PAIR_ALG_JW,
        .authenticated = 0,
        .ltk = {
            0xb2, 0x62, 0xbf, 0xf5, 0x57, 0x6b, 0x78, 0x09,
            0x0c, 0xab, 0xde, 0x8a, 0x62, 0xae, 0xc1, 0xc3
        },
        .our_priv_key = {
            0x87, 0x5d, 0x24, 0x79, 0x43, 0x29, 0x26, 0x84,
            0x73, 0x64, 0xb1, 0x32, 0x30, 0x42, 0xab, 0x79,
            0x55, 0x7b, 0x6d, 0xaa, 0xc2, 0x1e, 0x3e, 0xa6,
            0xbe, 0x5b, 0x9e, 0x61, 0xb, 0x90, 0x3a, 0x95
        },
    };
    ble_sm_test_util_us_sc_good(&params);
}

/**
 * Master: us
 * Secure connections
 * Pair algorithm: numeric comparison
 */
TEST_CASE(ble_sm_test_case_us_sc_numcmp_good)
{
    struct ble_sm_test_sc_params params;

    params = (struct ble_sm_test_sc_params) {
        .init_addr = { 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 },
        .resp_addr = { 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07 },
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 0x01,
            .oob_data_flag = 0x00,
            .authreq = 0x0d,
            .max_enc_key_size = 16,
            .init_key_dist = 0x01,
            .resp_key_dist = 0x01,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x01,
            .oob_data_flag = 0x00,
            .authreq = 0x0d,
            .max_enc_key_size = 16,
            .init_key_dist = 0x01,
            .resp_key_dist = 0x01,
        },
        .public_key_req = (struct ble_sm_public_key) {
            .x = {
                0xb5, 0x48, 0x49, 0x35, 0xe6, 0xc4, 0x91, 0x6c,
                0x65, 0x39, 0xd3, 0x51, 0x1d, 0x8c, 0xc5, 0x62,
                0x87, 0xc9, 0x1b, 0x06, 0x96, 0x78, 0x4a, 0xaa,
                0x18, 0x70, 0x85, 0x99, 0xfc, 0x01, 0xf4, 0xba
            },
            .y= {
                0xe0, 0x9f, 0x1b, 0x49, 0x31, 0x43, 0xf8, 0xae,
                0xff, 0xd1, 0x8f, 0x19, 0xc0, 0x1a, 0x44, 0x8f,
                0x0c, 0xc6, 0xc4, 0x7f, 0x2e, 0xf0, 0xb4, 0x31,
                0x1b, 0x75, 0xa9, 0x0a, 0xa1, 0x28, 0xb1, 0xbc
            },
        },
        .public_key_rsp = (struct ble_sm_public_key) {
            .x = {
                0x04, 0xf7, 0x4b, 0x20, 0x94, 0x55, 0xd4, 0x95,
                0xff, 0x04, 0x71, 0xa9, 0xe6, 0x36, 0x09, 0x9a,
                0xd8, 0x55, 0xb3, 0x56, 0xcc, 0x59, 0x1d, 0xab,
                0x7d, 0x0a, 0x21, 0x95, 0xc4, 0xdb, 0x8a, 0x8a
            },
            .y = {
                0x08, 0xc9, 0xe7, 0x6b, 0x50, 0x3d, 0x25, 0x44,
                0x32, 0xce, 0xa6, 0x07, 0xfc, 0x76, 0x78, 0xb7,
                0xa4, 0x8a, 0xdc, 0xf8, 0x9c, 0x85, 0x61, 0x7f,
                0x7f, 0x4f, 0x9e, 0xcd, 0xe3, 0xac, 0x78, 0x65
            },

        },
        .confirm_rsp[0] = {
            .value = {
                0x36, 0xb4, 0x46, 0x1f, 0xc0, 0x6f, 0xd3, 0x5b,
                0x18, 0x81, 0xbd, 0xe7, 0x05, 0xa2, 0x72, 0x75
            },
        },
        .random_req[0] = {
            .value = {
                0x1e, 0xc6, 0x28, 0xd7, 0x7e, 0xb7, 0x4c, 0xe7,
                0xbc, 0xac, 0x50, 0x67, 0xd1, 0x1c, 0xd9, 0xd8
            },
        },
        .random_rsp[0] = {
            .value = {
                0x6b, 0x06, 0x4c, 0xdc, 0x02, 0x05, 0xcd, 0x34,
                0xde, 0x0b, 0x4d, 0x37, 0x7f, 0x2c, 0x26, 0xef
            },
        },
        .dhkey_check_req = (struct ble_sm_dhkey_check) {
            .value = {
                0xd5, 0x2d, 0x3e, 0x0b, 0x63, 0x4d, 0xea, 0xe8,
                0x77, 0x10, 0x9c, 0x9a, 0xc5, 0x61, 0x89, 0x24
            }
        },
        .dhkey_check_rsp = (struct ble_sm_dhkey_check) {
            .value = {
                0x2d, 0x1f, 0x24, 0x2e, 0x80, 0x0e, 0xc3, 0x74,
                0xe3, 0x3f, 0xdb, 0xb6, 0x3e, 0x3d, 0x24, 0x08
            }
        },
        .pair_alg = BLE_SM_PAIR_ALG_NUMCMP,
        .authenticated = 1,
        .ltk = {
            0x38, 0xfd, 0x2a, 0x71, 0xfd, 0xd0, 0x03, 0xc0,
            0x2e, 0xb0, 0xa3, 0xc3, 0x01, 0xf7, 0x4e, 0x41
        },
        .our_priv_key = {
            0x90, 0x79, 0x54, 0xed, 0xed, 0x5f, 0x0c, 0x86,
            0x98, 0x41, 0x6d, 0xec, 0xde, 0xd0, 0x4b, 0x5a,
            0x52, 0xca, 0x04, 0x04, 0x76, 0x73, 0x0f, 0x79,
            0xd0, 0xf8, 0xff, 0x33, 0xd2, 0x36, 0x77, 0x1e
        },

        .passkey_info = {
            .passkey = {
                .action = BLE_SM_IOACT_NUMCMP,
                .numcmp_accept = 1,
            },

            .exp_numcmp = 9737,
            .io_before_rx = 0,
        },
    };
    ble_sm_test_util_us_sc_good(&params);
}

/**
 * Master: us
 * Secure connections
 * Pair algorithm: passkey
 */
TEST_CASE(ble_sm_test_case_us_sc_passkey_good)
{
    struct ble_sm_test_sc_params params;

    params = (struct ble_sm_test_sc_params) {
        .init_addr = { 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 },
        .resp_addr = { 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07 },
        .pair_req = (struct ble_sm_pair_cmd) {
            .io_cap = 0x04,
            .oob_data_flag = 0x00,
            .authreq = 0x0d,
            .max_enc_key_size = 16,
            .init_key_dist = 0x01,
            .resp_key_dist = 0x01,
        },
        .pair_rsp = (struct ble_sm_pair_cmd) {
            .io_cap = 0x02,
            .oob_data_flag = 0x00,
            .authreq = 0x0d,
            .max_enc_key_size = 16,
            .init_key_dist = 0x01,
            .resp_key_dist = 0x01,
        },
        .public_key_req = (struct ble_sm_public_key) {
            .x = {
                0x68, 0x71, 0x95, 0xf3, 0xca, 0xbe, 0x0d, 0x5c,
                0xc2, 0x04, 0x7c, 0xb3, 0x2f, 0x57, 0xa5, 0x0a,
                0xc0, 0x48, 0xbc, 0xd6, 0xe0, 0x86, 0x42, 0x6f,
                0x0e, 0x71, 0x39, 0xe1, 0xed, 0x75, 0xa3, 0x0c
            },
            .y = {
                0xee, 0x6a, 0x72, 0xb0, 0xb0, 0xa7, 0xb4, 0xc3,
                0x64, 0x74, 0xb8, 0xef, 0x7e, 0x78, 0x1c, 0x00,
                0x8f, 0x87, 0x34, 0x0c, 0x8d, 0xf6, 0x1b, 0x4f,
                0xde, 0x35, 0xf3, 0x38, 0x9b, 0x42, 0xcd, 0x90
            },
        },
        .public_key_rsp = (struct ble_sm_public_key) {
            .x = {
                0x73, 0xc8, 0x44, 0x6e, 0x5b, 0x5e, 0xd4, 0xcf,
                0x78, 0x2e, 0xba, 0xb8, 0x32, 0xc5, 0x07, 0xc8,
                0xec, 0xf9, 0xcf, 0x05, 0xde, 0xa3, 0xe3, 0x32,
                0x57, 0x5e, 0xb8, 0x69, 0x8a, 0x49, 0xe3, 0x0b
            },
            .y = {
                0x71, 0x54, 0xa0, 0x0c, 0x18, 0x03, 0x4b, 0x11,
                0x0b, 0xdc, 0xdf, 0x89, 0x9a, 0x24, 0x9e, 0x75,
                0xbf, 0xff, 0xd7, 0xf8, 0x45, 0xcb, 0x3c, 0xdc,
                0xfc, 0x2d, 0x3c, 0x54, 0x36, 0x29, 0x04, 0xc4
            },
        },

        .confirm_req[0] = {
            .value = {
                0x5f, 0x5b, 0x6a, 0x61, 0x93, 0xc3, 0x28, 0x09,
                0x82, 0x4d, 0xb8, 0x43, 0x23, 0x1a, 0x06, 0x5d,
            },
        },
        .confirm_rsp[0] = {
            .value = {
                0xcc, 0xc0, 0xe7, 0x5b, 0x67, 0x4b, 0xca, 0x19,
                0xa6, 0xa2, 0x88, 0x8b, 0xc0, 0x16, 0x28, 0x6b,
            },
        },
        .random_req[0] = {
            .value = {
                0xf6, 0x6c, 0xba, 0x43, 0x1e, 0xfd, 0xed, 0x6d,
                0x11, 0xa3, 0xd5, 0x94, 0xeb, 0x69, 0xe6, 0x4a,
            },
        },
        .random_rsp[0] = {
            .value = {
                0x57, 0xe2, 0x16, 0x9d, 0x9a, 0xec, 0x96, 0xa4,
                0xfc, 0xd0, 0x5b, 0xc1, 0x3d, 0xc8, 0x69, 0x08,
            },
        },
        .confirm_req[1] = {
            .value = {
                0xb9, 0x58, 0x47, 0xc2, 0xf9, 0xf4, 0x1a, 0xb3,
                0x4d, 0xd9, 0x0d, 0x98, 0x26, 0x51, 0xa6, 0x08,
            },
        },
        .confirm_rsp[1] = {
            .value = {
                0xbb, 0x93, 0x6e, 0xbb, 0x82, 0xb2, 0x9f, 0x20,
                0x56, 0x66, 0xd5, 0x12, 0xa2, 0x4c, 0x14, 0x4f,
            },
        },
        .random_req[1] = {
            .value = {
                0x8e, 0xeb, 0x53, 0xf5, 0x7b, 0x80, 0x4e, 0x54,
                0xbe, 0x54, 0xa1, 0xca, 0x3a, 0x3b, 0xec, 0xdb,
            },
        },
        .random_rsp[1] = {
            .value = {
                0xc5, 0xb3, 0xbf, 0x5d, 0x5a, 0xd0, 0xb9, 0xe3,
                0x3d, 0xc4, 0x20, 0x9b, 0x51, 0x2a, 0x66, 0xcc,
            },
        },
        .confirm_req[2] = {
            .value = {
                0x8c, 0xf2, 0x30, 0xb1, 0x36, 0x09, 0xa6, 0x97,
                0xea, 0xd1, 0x62, 0x26, 0x6c, 0xce, 0x11, 0xee,
            },
        },
        .confirm_rsp[2] = {
            .value = {
                0x23, 0x4b, 0xb6, 0x4a, 0x96, 0xea, 0x19, 0xdd,
                0xff, 0xe2, 0xad, 0x19, 0x39, 0x64, 0x4c, 0xe8,
            },
        },
        .random_req[2] = {
            .value = {
                0x27, 0x7e, 0xe8, 0x09, 0x66, 0x0a, 0xef, 0xf0,
                0x46, 0x24, 0x81, 0xd4, 0x03, 0x9d, 0x31, 0x92,
            },
        },
        .random_rsp[2] = {
            .value = {
                0x56, 0xa8, 0x6c, 0x25, 0xfb, 0xab, 0xc4, 0x14,
                0xdb, 0xd6, 0x85, 0x41, 0x73, 0x49, 0xc7, 0x0c,
            },
        },
        .confirm_req[3] = {
            .value = {
                0x3d, 0xda, 0xaa, 0xed, 0x76, 0xaf, 0xb2, 0xbd,
                0x58, 0x6b, 0x1a, 0xe7, 0x3a, 0x3d, 0xc1, 0x74,
            },
        },
        .confirm_rsp[3] = {
            .value = {
                0x99, 0xcd, 0xfe, 0xb6, 0x8f, 0xc5, 0x94, 0x6d,
                0x94, 0xdb, 0x69, 0x2a, 0x2b, 0xa5, 0x31, 0xea,
            },
        },
        .random_req[3] = {
            .value = {
                0xa9, 0xf6, 0x19, 0xe0, 0xbc, 0xbd, 0xf7, 0x74,
                0x2c, 0x37, 0x69, 0x43, 0x20, 0x5e, 0xe7, 0xd1,
            },
        },
        .random_rsp[3] = {
            .value = {
                0x2a, 0xd0, 0x4c, 0x99, 0xf8, 0xa7, 0x0a, 0x57,
                0xa2, 0xb6, 0x6f, 0x48, 0x59, 0xc1, 0x8f, 0xec,
            },
        },
        .confirm_req[4] = {
            .value = {
                0xd4, 0x5d, 0xfa, 0xda, 0xc6, 0x48, 0x43, 0x54,
                0x0f, 0x7b, 0xfa, 0x83, 0x56, 0x8d, 0x92, 0xff,
            },
        },
        .confirm_rsp[4] = {
            .value = {
                0xb4, 0xc1, 0x32, 0x08, 0x28, 0xd4, 0x39, 0xef,
                0x83, 0x20, 0x7c, 0xa6, 0x9d, 0x2a, 0x40, 0x98,
            },
        },
        .random_req[4] = {
            .value = {
                0x7b, 0x5d, 0x1b, 0x08, 0x3b, 0xef, 0x26, 0x05,
                0x74, 0x86, 0x83, 0x71, 0x2e, 0x09, 0x70, 0x15,
            },
        },
        .random_rsp[4] = {
            .value = {
                0x14, 0x63, 0x16, 0xbb, 0x62, 0x0d, 0x34, 0xaa,
                0xed, 0xe2, 0xd3, 0x05, 0x07, 0xda, 0xfd, 0x9e,
            },
        },
        .confirm_req[5] = {
            .value = {
                0x78, 0x4c, 0x8d, 0x96, 0xba, 0x57, 0x18, 0xd7,
                0x1e, 0x9c, 0xaa, 0x70, 0x79, 0xb8, 0x73, 0xb2,
            },
        },
        .confirm_rsp[5] = {
            .value = {
                0xab, 0x28, 0xe8, 0x23, 0x55, 0x1a, 0x69, 0x93,
                0x53, 0x24, 0xd8, 0xf7, 0x48, 0x3f, 0x1b, 0x6f,
            },
        },
        .random_req[5] = {
            .value = {
                0x01, 0xc2, 0xec, 0xef, 0xdf, 0xc4, 0x32, 0xa1,
                0xc7, 0x51, 0x8d, 0x99, 0x1a, 0xb9, 0xbb, 0x18,
            },
        },
        .random_rsp[5] = {
            .value = {
                0x9e, 0xa7, 0x10, 0x2b, 0x95, 0xef, 0x9f, 0xd1,
                0x23, 0xa7, 0xb3, 0x49, 0x5b, 0x2e, 0x6f, 0xed,
            },
        },
        .confirm_req[6] = {
            .value = {
                0xcc, 0x58, 0x50, 0xc7, 0x4b, 0x76, 0x29, 0xd0,
                0x3c, 0xa7, 0x16, 0xee, 0x4a, 0x39, 0x4a, 0xd3,
            },
        },
        .confirm_rsp[6] = {
            .value = {
                0xcd, 0x55, 0xfb, 0xbe, 0x03, 0x70, 0xbf, 0xa4,
                0xda, 0xfc, 0x16, 0x75, 0x67, 0x96, 0xfe, 0x0c,
            },
        },
        .random_req[6] = {
            .value = {
                0xf9, 0x12, 0x58, 0xbe, 0xc4, 0xa2, 0xc0, 0xad,
                0x00, 0x99, 0xb4, 0x73, 0x5c, 0x2c, 0xad, 0x39,
            },
        },
        .random_rsp[6] = {
            .value = {
                0x15, 0x03, 0xbb, 0xa5, 0x6e, 0x98, 0x12, 0x7d,
                0x8a, 0x0f, 0xcb, 0x9a, 0x80, 0xdc, 0x79, 0x5f,
            },
        },
        .confirm_req[7] = {
            .value = {
                0xe6, 0xe0, 0xb5, 0x02, 0x8a, 0xda, 0x3f, 0x09,
                0x20, 0xad, 0xe9, 0x36, 0x70, 0x50, 0xeb, 0xc1,
            },
        },
        .confirm_rsp[7] = {
            .value = {
                0xc3, 0xc8, 0x94, 0xce, 0x58, 0x47, 0x68, 0x0e,
                0x47, 0x89, 0x97, 0xd1, 0x6c, 0xed, 0x89, 0x6b,
            },
        },
        .random_req[7] = {
            .value = {
                0xb6, 0x76, 0xaa, 0xd2, 0x2f, 0x29, 0x0f, 0x04,
                0x24, 0xc8, 0x8d, 0x99, 0x00, 0xd7, 0xc0, 0xd4,
            },
        },
        .random_rsp[7] = {
            .value = {
                0xa0, 0xa5, 0xda, 0xf1, 0x32, 0x6b, 0x17, 0xc6,
                0xc2, 0x23, 0x3b, 0x2f, 0x88, 0xbd, 0x97, 0x7e,
            },
        },
        .confirm_req[8] = {
            .value = {
                0xb9, 0x8b, 0x7c, 0x6e, 0x00, 0xaa, 0x42, 0xc7,
                0x89, 0x2a, 0x21, 0x2b, 0xa0, 0xde, 0xb2, 0xe5,
            },
        },
        .confirm_rsp[8] = {
            .value = {
                0x3d, 0x7e, 0xc4, 0x2f, 0x66, 0xa7, 0x1c, 0x9f,
                0xbe, 0x93, 0x1f, 0x95, 0xdd, 0xd4, 0x1a, 0xf7,
            },
        },
        .random_req[8] = {
            .value = {
                0x88, 0xf8, 0xe2, 0xf9, 0xc3, 0x91, 0x07, 0x33,
                0x17, 0x24, 0xb5, 0x80, 0x40, 0x66, 0xce, 0x15,
            },
        },
        .random_rsp[8] = {
            .value = {
                0x5e, 0x41, 0x2a, 0xa2, 0xb1, 0xa4, 0x0c, 0xf1,
                0x0b, 0xb3, 0x5c, 0xb3, 0x65, 0x32, 0xbd, 0x1c,
            },
        },
        .confirm_req[9] = {
            .value = {
                0x50, 0x69, 0xb9, 0x89, 0x46, 0xf5, 0x6e, 0x44,
                0x78, 0x71, 0x45, 0x5b, 0xb1, 0x11, 0x45, 0xad,
            },
        },
        .confirm_rsp[9] = {
            .value = {
                0x4b, 0x9a, 0x95, 0x0b, 0x61, 0x4c, 0xa3, 0x01,
                0x6b, 0x4c, 0xc9, 0x68, 0x1a, 0x29, 0xb8, 0xe9,
            },
        },
        .random_req[9] = {
            .value = {
                0x25, 0x30, 0xf4, 0x21, 0x14, 0x98, 0x83, 0xa5,
                0x2c, 0x4e, 0x48, 0xfd, 0x62, 0xa6, 0x75, 0xd0,
            },
        },
        .random_rsp[9] = {
            .value = {
                0xe6, 0x72, 0xf3, 0xc2, 0xcd, 0xf0, 0x42, 0x68,
                0x47, 0x5a, 0x99, 0x79, 0xa6, 0x37, 0x62, 0xa6,
            },
        },
        .confirm_req[10] = {
            .value = {
                0x1f, 0x38, 0x3a, 0xc0, 0x07, 0x06, 0x0f, 0xfa,
                0xe5, 0x7f, 0x9c, 0xe9, 0xbf, 0xdf, 0xc7, 0x36,
            },
        },
        .confirm_rsp[10] = {
            .value = {
                0xb0, 0x1c, 0x9d, 0xb6, 0x50, 0xc1, 0x2a, 0x5c,
                0x76, 0xfe, 0x9f, 0xf3, 0x7f, 0x8a, 0x6a, 0x13,
            },
        },
        .random_req[10] = {
            .value = {
                0x7d, 0xea, 0x3e, 0xae, 0xaf, 0x72, 0x9b, 0x85,
                0x69, 0x1e, 0x7a, 0x86, 0xff, 0xb8, 0x2f, 0x9a,
            },
        },
        .random_rsp[10] = {
            .value = {
                0x6a, 0x01, 0x4c, 0x46, 0x16, 0x42, 0x9c, 0x71,
                0x8b, 0x45, 0xbb, 0x9a, 0xec, 0xdd, 0xca, 0xb2,
            },
        },
        .confirm_req[11] = {
            .value = {
                0x51, 0x5b, 0x3a, 0xf0, 0xcb, 0x38, 0xfb, 0xde,
                0x3e, 0xe4, 0x18, 0x1f, 0x08, 0x40, 0x34, 0xb4,
            },
        },
        .confirm_rsp[11] = {
            .value = {
                0xbc, 0x84, 0x6d, 0x2b, 0xe6, 0x1c, 0xd5, 0xe4,
                0x11, 0x2a, 0x13, 0xa4, 0x84, 0x37, 0x3b, 0xff,
            },
        },
        .random_req[11] = {
            .value = {
                0x6d, 0x00, 0x4b, 0x6c, 0x06, 0xda, 0xc6, 0x8a,
                0xa7, 0xff, 0xd3, 0x51, 0x29, 0x10, 0x1a, 0x32,
            },
        },
        .random_rsp[11] = {
            .value = {
                0x76, 0x1f, 0x19, 0xa7, 0xb6, 0xdd, 0x34, 0x65,
                0xb9, 0xf3, 0xfc, 0x4a, 0x91, 0x9b, 0x00, 0xf7,
            },
        },
        .confirm_req[12] = {
            .value = {
                0x7e, 0x53, 0x9a, 0x81, 0xd8, 0x10, 0xc4, 0xa6,
                0xa3, 0x94, 0x3d, 0xeb, 0x4c, 0x60, 0x4a, 0xda,
            },
        },
        .confirm_rsp[12] = {
            .value = {
                0xfa, 0x43, 0x65, 0xc1, 0x7a, 0xea, 0xa1, 0x40,
                0x71, 0xca, 0x1c, 0x3a, 0xd7, 0xbb, 0x18, 0x23,
            },
        },
        .random_req[12] = {
            .value = {
                0x85, 0x3c, 0x99, 0x0a, 0xc1, 0x4e, 0x7e, 0xb8,
                0x30, 0x32, 0x7c, 0x93, 0xba, 0x13, 0x44, 0xec,
            },
        },
        .random_rsp[12] = {
            .value = {
                0x77, 0x3c, 0x53, 0xa8, 0xf7, 0xb4, 0x3c, 0x63,
                0x35, 0x85, 0xd4, 0x24, 0x95, 0x22, 0x78, 0x9f,
            },
        },
        .confirm_req[13] = {
            .value = {
                0x75, 0x9b, 0x2b, 0x67, 0x89, 0x19, 0xba, 0xcb,
                0xac, 0x72, 0x5e, 0xa9, 0x42, 0xf7, 0x46, 0x8b,
            },
        },
        .confirm_rsp[13] = {
            .value = {
                0x48, 0x14, 0xc4, 0x1a, 0x51, 0xc5, 0x7f, 0x43,
                0xbb, 0x18, 0x6f, 0xc4, 0x3e, 0xab, 0x0e, 0x28,
            },
        },
        .random_req[13] = {
            .value = {
                0xdb, 0x60, 0xf2, 0xa5, 0x9d, 0x17, 0x63, 0xd5,
                0x0d, 0x64, 0x36, 0xf5, 0x77, 0x01, 0xf9, 0xd4,
            },
        },
        .random_rsp[13] = {
            .value = {
                0x6e, 0xce, 0x46, 0x71, 0xf9, 0x8b, 0x6e, 0xdc,
                0xe2, 0x17, 0xb1, 0xf6, 0x64, 0x10, 0xd8, 0x41,
            },
        },
        .confirm_req[14] = {
            .value = {
                0xc9, 0x19, 0xa7, 0xb6, 0x3e, 0xb8, 0x9d, 0xb1,
                0x25, 0x41, 0x3c, 0xf6, 0x2c, 0x37, 0x14, 0x56,
            },
        },
        .confirm_rsp[14] = {
            .value = {
                0x69, 0x35, 0xad, 0x96, 0xdd, 0x9a, 0x9a, 0x27,
                0x55, 0x93, 0xb2, 0x55, 0x5d, 0xce, 0xce, 0xce,
            },
        },
        .random_req[14] = {
            .value = {
                0x25, 0x2b, 0x05, 0xc1, 0x25, 0xab, 0x2e, 0x97,
                0x97, 0x99, 0x45, 0xe4, 0x2a, 0x2a, 0x03, 0xb8,
            },
        },
        .random_rsp[14] = {
            .value = {
                0x2d, 0x8f, 0xc5, 0x4d, 0x13, 0x9a, 0xe8, 0x4b,
                0xa7, 0xac, 0xa7, 0x75, 0xef, 0x29, 0x60, 0x58,
            },
        },
        .confirm_req[15] = {
            .value = {
                0xea, 0x55, 0x35, 0x28, 0x08, 0x73, 0x07, 0xaf,
                0x70, 0xff, 0x84, 0xd7, 0xef, 0x94, 0x11, 0x6c,
            },
        },
        .confirm_rsp[15] = {
            .value = {
                0x25, 0xb6, 0x64, 0xa0, 0xa3, 0x5f, 0x33, 0xe0,
                0x40, 0xc2, 0x3a, 0x03, 0x77, 0x90, 0x72, 0xe6,
            },
        },
        .random_req[15] = {
            .value = {
                0x60, 0xef, 0xa8, 0xd6, 0xb8, 0xa4, 0xff, 0x66,
                0x29, 0x50, 0x9c, 0xcb, 0x9f, 0x9c, 0x4e, 0x59,
            },
        },
        .random_rsp[15] = {
            .value = {
                0xa3, 0xe1, 0xab, 0x87, 0x18, 0xda, 0x8a, 0xdd,
                0x89, 0xaa, 0xcd, 0x92, 0xa7, 0x32, 0x5f, 0xab,
            },
        },
        .confirm_req[16] = {
            .value = {
                0x09, 0x06, 0x3f, 0xd7, 0xfa, 0x26, 0xc6, 0xc2,
                0x80, 0x6e, 0xd8, 0x9d, 0xb5, 0x58, 0x53, 0x48,
            },
        },
        .confirm_rsp[16] = {
            .value = {
                0xb9, 0x6c, 0x12, 0xdf, 0x8e, 0xd9, 0x2f, 0x89,
                0x3c, 0x02, 0xd5, 0x7c, 0xe3, 0xad, 0x3c, 0xdd,
            },
        },
        .random_req[16] = {
            .value = {
                0x05, 0x9f, 0x60, 0x0c, 0x29, 0x75, 0x30, 0x4e,
                0xfe, 0xf1, 0x71, 0x04, 0x96, 0x17, 0xf2, 0x3d,
            },
        },
        .random_rsp[16] = {
            .value = {
                0x25, 0x40, 0x4c, 0x13, 0x69, 0x9b, 0x2f, 0xe6,
                0x12, 0x6b, 0x2e, 0xc0, 0x38, 0xdf, 0x22, 0x6f,
            },
        },
        .confirm_req[17] = {
            .value = {
                0x07, 0x72, 0xf6, 0x08, 0xbf, 0xd8, 0x86, 0x4d,
                0x42, 0x17, 0xea, 0xa6, 0x91, 0xad, 0x8c, 0x84,
            },
        },
        .confirm_rsp[17] = {
            .value = {
                0xb1, 0xea, 0x74, 0x47, 0xae, 0xbc, 0xed, 0x0e,
                0x64, 0xc7, 0xe1, 0x48, 0x81, 0x56, 0x53, 0xff,
            },
        },
        .random_req[17] = {
            .value = {
                0x58, 0x03, 0xa1, 0x58, 0xca, 0x27, 0x89, 0x21,
                0x06, 0x77, 0x7d, 0x0d, 0xbd, 0x7e, 0x73, 0x17,
            },
        },
        .random_rsp[17] = {
            .value = {
                0x02, 0x0e, 0x6c, 0xd6, 0x26, 0x70, 0xf1, 0xb4,
                0xa6, 0x48, 0xe3, 0x62, 0xcf, 0x10, 0xc2, 0x00,
            },
        },
        .confirm_req[18] = {
            .value = {
                0xe8, 0x39, 0x1b, 0xcb, 0x7c, 0x3d, 0x2d, 0xfd,
                0x25, 0x54, 0xf6, 0x70, 0x26, 0xf8, 0x35, 0x74,
            },
        },
        .confirm_rsp[18] = {
            .value = {
                0xba, 0x0d, 0x4e, 0x42, 0x4b, 0x9f, 0xf5, 0xc9,
                0x08, 0x46, 0x3b, 0xf2, 0xb3, 0x7d, 0xf4, 0x19,
            },
        },
        .random_req[18] = {
            .value = {
                0xe2, 0x5b, 0x52, 0xba, 0x74, 0x3e, 0x0b, 0xe3,
                0x6d, 0xa0, 0x51, 0x33, 0x00, 0x17, 0x70, 0x79,
            },
        },
        .random_rsp[18] = {
            .value = {
                0x4a, 0xe9, 0x10, 0xbb, 0x3d, 0x18, 0xe5, 0xd0,
                0xfd, 0x5a, 0x35, 0x05, 0x18, 0x7f, 0x4a, 0xd9,
            },
        },
        .confirm_req[19] = {
            .value = {
                0x5f, 0x56, 0xdd, 0xbf, 0x7b, 0x1f, 0xd3, 0x94,
                0xc6, 0xa8, 0xb8, 0x43, 0x86, 0xd6, 0x5f, 0x40,
            },
        },
        .confirm_rsp[19] = {
            .value = {
                0x44, 0xfa, 0x55, 0x12, 0xf3, 0x55, 0xb6, 0xed,
                0xae, 0x68, 0x41, 0x98, 0x4e, 0xa0, 0x20, 0x4f,
            },
        },
        .random_req[19] = {
            .value = {
                0x0c, 0x89, 0xcd, 0x07, 0xcf, 0x2b, 0x3d, 0x56,
                0x0c, 0xba, 0x81, 0x01, 0xaa, 0x10, 0x2b, 0x9e,
            },
        },
        .random_rsp[19] = {
            .value = {
                0x71, 0x53, 0x1d, 0xbc, 0x1e, 0x93, 0x78, 0x9d,
                0xb9, 0x1a, 0xa3, 0xf8, 0xd0, 0x97, 0x9b, 0x71,
            },
        },
        .dhkey_check_req = (struct ble_sm_dhkey_check) {
            .value = {
                0xb7, 0xaa, 0x3d, 0x50, 0x5b, 0x25, 0x7b, 0xb8,
                0xb1, 0x1b, 0x06, 0xa2, 0x74, 0x15, 0x56, 0xa2
            }
        },
        .dhkey_check_rsp = (struct ble_sm_dhkey_check) {
            .value = {
                0x7b, 0x92, 0xa0, 0xfb, 0x87, 0xaa, 0x30, 0xf5,
                0x0c, 0xf2, 0x07, 0x73, 0xee, 0x28, 0x39, 0xe0
            }
        },
        .pair_alg = BLE_SM_PAIR_ALG_PASSKEY,
        .authenticated = 1,
        .ltk = {
            0x03, 0xb8, 0x78, 0x47, 0x7f, 0x05, 0x95, 0x52,
            0x59, 0x3f, 0xcc, 0x9c, 0x32, 0x95, 0x53, 0xd5
        },
        .our_priv_key = {
            0x77, 0xd4, 0x0, 0x51, 0xcf, 0x83, 0xbf, 0xc6,
            0x82, 0x3b, 0x63, 0x13, 0x75, 0x21, 0x9, 0x7,
            0x73, 0x8a, 0x9c, 0x4, 0x5f, 0x14, 0xee, 0x52,
            0x19, 0x2f, 0xb0, 0x9b, 0xc7, 0x26, 0xed, 0xb4
        },

        .passkey_info = {
            .passkey = {
                .action = BLE_SM_IOACT_DISP,
                .passkey = 85212,
            },
        },
    };
    ble_sm_test_util_us_sc_good(&params);
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

TEST_SUITE(ble_sm_test_suite)
{
    ble_sm_test_case_f4();
    ble_sm_test_case_f5();
    ble_sm_test_case_f6();
    ble_sm_test_case_g2();

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
    ble_sm_test_case_peer_sc_numcmp_good();
    ble_sm_test_case_peer_sc_passkey_good();
    ble_sm_test_case_us_sc_jw_good();
    ble_sm_test_case_us_sc_numcmp_good();
    ble_sm_test_case_us_sc_passkey_good();
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
