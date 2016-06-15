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
#include "ble_sm_test_util.h"

int ble_sm_test_gap_event;
int ble_sm_test_gap_status;
struct ble_gap_sec_state ble_sm_test_sec_state;

int ble_sm_test_store_obj_type;
union ble_store_key ble_sm_test_store_key;
union ble_store_value ble_sm_test_store_value;

static ble_store_read_fn ble_sm_test_util_store_read;
static ble_store_write_fn ble_sm_test_util_store_write;

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

void
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

int
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

void
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

static void
ble_sm_test_util_rx_id_info(uint16_t conn_handle,
                            struct ble_sm_id_info *cmd,
                            int exp_status)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int payload_len;
    int rc;

    hci_hdr = BLE_SM_TEST_UTIL_HCI_HDR(
        2, BLE_HCI_PB_FIRST_FLUSH,
        BLE_L2CAP_HDR_SZ + BLE_SM_HDR_SZ + BLE_SM_ID_INFO_SZ);

    om = ble_hs_misc_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    payload_len = BLE_SM_HDR_SZ + BLE_SM_ID_INFO_SZ;

    v = os_mbuf_extend(om, payload_len);
    TEST_ASSERT_FATAL(v != NULL);

    ble_sm_id_info_write(v, payload_len, cmd);

    rc = ble_hs_test_util_l2cap_rx_first_frag(conn_handle, BLE_L2CAP_CID_SM,
                                              &hci_hdr, om);
    TEST_ASSERT_FATAL(rc == exp_status);
}

static void
ble_sm_test_util_rx_id_addr_info(uint16_t conn_handle,
                                 struct ble_sm_id_addr_info *cmd,
                                 int exp_status)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int payload_len;
    int rc;

    hci_hdr = BLE_SM_TEST_UTIL_HCI_HDR(
        2, BLE_HCI_PB_FIRST_FLUSH,
        BLE_L2CAP_HDR_SZ + BLE_SM_HDR_SZ + BLE_SM_ID_ADDR_INFO_SZ);

    om = ble_hs_misc_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    payload_len = BLE_SM_HDR_SZ + BLE_SM_ID_ADDR_INFO_SZ;

    v = os_mbuf_extend(om, payload_len);
    TEST_ASSERT_FATAL(v != NULL);

    ble_sm_id_addr_info_write(v, payload_len, cmd);

    rc = ble_hs_test_util_l2cap_rx_first_frag(conn_handle, BLE_L2CAP_CID_SM,
                                              &hci_hdr, om);
    TEST_ASSERT_FATAL(rc == exp_status);
}

static void
ble_sm_test_util_rx_sign_info(uint16_t conn_handle,
                              struct ble_sm_sign_info *cmd,
                              int exp_status)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int payload_len;
    int rc;

    hci_hdr = BLE_SM_TEST_UTIL_HCI_HDR(
        2, BLE_HCI_PB_FIRST_FLUSH,
        BLE_L2CAP_HDR_SZ + BLE_SM_HDR_SZ + BLE_SM_SIGN_INFO_SZ);

    om = ble_hs_misc_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    payload_len = BLE_SM_HDR_SZ + BLE_SM_SIGN_INFO_SZ;

    v = os_mbuf_extend(om, payload_len);
    TEST_ASSERT_FATAL(v != NULL);

    ble_sm_sign_info_write(v, payload_len, cmd);

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
ble_sm_test_util_verify_tx_id_info(struct ble_sm_id_info *exp_cmd)
{
    struct ble_sm_id_info cmd;
    struct os_mbuf *om;
    uint8_t irk[16];

    ble_hs_test_util_tx_all();
    om = ble_sm_test_util_verify_tx_hdr(BLE_SM_OP_IDENTITY_INFO,
                                        BLE_SM_ID_INFO_SZ);
    ble_sm_id_info_parse(om->om_data, om->om_len, &cmd);

    TEST_ASSERT(memcmp(cmd.irk, exp_cmd->irk, 16) == 0);

    /* Ensure IRK is sent in big endian. */
    swap_buf(irk, om->om_data, 16);
    TEST_ASSERT(memcmp(irk, cmd.irk, 16) == 0);
}

static void
ble_sm_test_util_verify_tx_id_addr_info(struct ble_sm_id_addr_info *exp_cmd)
{
    struct ble_sm_id_addr_info cmd;
    struct os_mbuf *om;
    uint8_t *our_id_addr;
    uint8_t our_id_addr_type;

    our_id_addr = bls_hs_priv_get_local_identity_addr(&our_id_addr_type);

    ble_hs_test_util_tx_all();
    om = ble_sm_test_util_verify_tx_hdr(BLE_SM_OP_IDENTITY_ADDR_INFO,
                                        BLE_SM_ID_ADDR_INFO_SZ);
    ble_sm_id_addr_info_parse(om->om_data, om->om_len, &cmd);

    TEST_ASSERT(cmd.addr_type == exp_cmd->addr_type);
    TEST_ASSERT(memcmp(cmd.bd_addr, exp_cmd->bd_addr, 6) == 0);

    TEST_ASSERT(cmd.addr_type == our_id_addr_type);
    TEST_ASSERT(memcmp(cmd.bd_addr, our_id_addr, 6) == 0);
}

static void
ble_sm_test_util_verify_tx_sign_info(struct ble_sm_sign_info *exp_cmd)
{
    struct ble_sm_sign_info cmd;
    struct os_mbuf *om;
    uint8_t csrk[16];

    ble_hs_test_util_tx_all();
    om = ble_sm_test_util_verify_tx_hdr(BLE_SM_OP_SIGN_INFO,
                                        BLE_SM_ID_INFO_SZ);
    ble_sm_sign_info_parse(om->om_data, om->om_len, &cmd);

    TEST_ASSERT(memcmp(cmd.sig_key, exp_cmd->sig_key, 16) == 0);

    /* Ensure CSRK is sent in big endian. */
    swap_buf(csrk, om->om_data, 16);
    TEST_ASSERT(memcmp(csrk, cmd.sig_key, 16) == 0);
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

void
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
ble_sm_test_util_rx_lt_key_req(uint16_t conn_handle, uint64_t r, uint16_t ediv)
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
ble_sm_test_util_verify_tx_lt_key_req_reply(uint16_t conn_handle, uint8_t *stk)
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
ble_sm_test_util_verify_tx_add_resolve_list(uint8_t peer_id_addr_type,
                                            uint8_t *peer_id_addr,
                                            uint8_t *peer_irk,
                                            uint8_t *our_irk)
{
    uint8_t buf[16];
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_ADD_RESOLV_LIST,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_ADD_TO_RESOLV_LIST_LEN);
    TEST_ASSERT(param[0] == peer_id_addr_type);
    TEST_ASSERT(memcmp(param + 1, peer_id_addr, 6) == 0);

    /* Ensure IRKs are sent in little endian. */
    memcpy(buf, peer_irk, 16);
    TEST_ASSERT(memcmp(param + 7, buf, 16) == 0);
    memcpy(buf, our_irk, 16);
    TEST_ASSERT(memcmp(param + 23, buf, 16) == 0);
}

void
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

void
ble_sm_test_util_io_inject_bad(uint16_t conn_handle, uint8_t correct_io_act)
{
    struct ble_sm_proc *proc;
    struct ble_sm_io io;
    uint8_t io_sm_state;
    int already_injected;
    int rc;
    int i;

    /* Lock mutex to prevent thread-safety assert from failing. */
    ble_hs_lock();
    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_NONE, -1, NULL);
    ble_hs_unlock();

    TEST_ASSERT_FATAL(proc != NULL);

    io_sm_state = ble_sm_ioact_state(correct_io_act);

    for (i = 1; i < BLE_SM_IOACT_MAX_PLUS_ONE; i++) {
        if (io_sm_state != proc->state  ||
            i != correct_io_act         ||
            proc->flags & BLE_SM_PROC_F_IO_INJECTED) {

            already_injected = proc->flags & BLE_SM_PROC_F_IO_INJECTED;

            io.action = i;
            rc = ble_sm_inject_io(conn_handle, &io);

            if (already_injected) {
                TEST_ASSERT(rc == BLE_HS_EALREADY);
            } else {
                TEST_ASSERT(rc == BLE_HS_EINVAL);
            }
        }
    }
}

void
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

void
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
        rc = ble_store_read_our_sec(&key_sec, &value_sec);
        TEST_ASSERT(rc == BLE_HS_ENOENT);
    } else {
        rc = ble_store_read_peer_sec(&key_sec, &value_sec);
        TEST_ASSERT_FATAL(rc == 0);
        TEST_ASSERT(value_sec.peer_addr_type == 0);
        TEST_ASSERT(memcmp(value_sec.peer_addr, params->init_id_addr, 6) == 0);
        TEST_ASSERT(value_sec.ediv == params->ediv);
        TEST_ASSERT(value_sec.rand_num == params->r);
        TEST_ASSERT(value_sec.authenticated == params->authenticated);
        TEST_ASSERT(value_sec.ltk_present == 1);
        TEST_ASSERT(memcmp(value_sec.ltk, params->enc_info_req.ltk, 16) == 0);
        TEST_ASSERT(value_sec.irk_present == 0);
        TEST_ASSERT(value_sec.csrk_present == 0);

        /* Verify no other keys were persisted. */
        key_sec.idx++;
        rc = ble_store_read_peer_sec(&key_sec, &value_sec);
        TEST_ASSERT_FATAL(rc == BLE_HS_ENOENT);
    }

    memset(&key_sec, 0, sizeof key_sec);
    key_sec.peer_addr_type = BLE_STORE_ADDR_TYPE_NONE;

    if (params->pair_rsp.resp_key_dist == 0) {
        rc = ble_store_read_our_sec(&key_sec, &value_sec);
        TEST_ASSERT(rc == BLE_HS_ENOENT);
    } else {
        rc = ble_store_read_our_sec(&key_sec, &value_sec);
        TEST_ASSERT_FATAL(rc == 0);
        TEST_ASSERT(value_sec.peer_addr_type == 0);
        TEST_ASSERT(memcmp(value_sec.peer_addr, params->init_id_addr, 6) == 0);
        TEST_ASSERT(value_sec.ediv == params->ediv);
        TEST_ASSERT(value_sec.rand_num == params->r);
        TEST_ASSERT(value_sec.authenticated == params->authenticated);
        TEST_ASSERT(value_sec.ltk_present == 1);
        TEST_ASSERT(memcmp(value_sec.ltk, params->enc_info_req.ltk, 16) == 0);
        TEST_ASSERT(value_sec.irk_present == 0);
        TEST_ASSERT(value_sec.csrk_present == 0);

        /* Verify no other keys were persisted. */
        key_sec.idx++;
        rc = ble_store_read_our_sec(&key_sec, &value_sec);
        TEST_ASSERT_FATAL(rc == BLE_HS_ENOENT);
    }
}

static void
ble_sm_test_util_verify_sc_persist(struct ble_sm_test_sc_params *params,
                                   int we_are_initiator)
{
    struct ble_store_value_sec value_sec;
    struct ble_store_key_sec key_sec;
    uint8_t *peer_id_addr;
    uint8_t *peer_csrk;
    uint8_t *our_csrk;
    uint8_t *peer_irk;
    uint8_t *our_irk;
    uint8_t peer_id_addr_type;
    uint8_t peer_addr_type;
    uint8_t peer_key_dist;
    uint8_t our_key_dist;
    int csrk_expected;
    int peer_irk_expected;
    int our_irk_expected;
    int bonding;
    int rc;

    if (we_are_initiator) {
        our_key_dist = params->pair_rsp.init_key_dist;
        peer_key_dist = params->pair_rsp.resp_key_dist;

        peer_addr_type = params->resp_addr_type;
        peer_id_addr = params->resp_id_addr;

        peer_irk = params->id_info_req.irk;
        peer_csrk = params->sign_info_req.sig_key;
        our_irk = params->id_info_rsp.irk;
        our_csrk = params->sign_info_rsp.sig_key;
    } else {
        our_key_dist = params->pair_rsp.resp_key_dist;
        peer_key_dist = params->pair_rsp.init_key_dist;

        peer_addr_type = params->init_addr_type;
        peer_id_addr = params->init_id_addr;

        peer_irk = params->id_info_rsp.irk;
        peer_csrk = params->sign_info_rsp.sig_key;
        our_irk = params->id_info_req.irk;
        our_csrk = params->sign_info_req.sig_key;
    }
    peer_id_addr_type = ble_hs_misc_addr_type_to_id(peer_addr_type);

    memset(&key_sec, 0, sizeof key_sec);
    key_sec.peer_addr_type = BLE_STORE_ADDR_TYPE_NONE;

    bonding = params->pair_req.authreq & BLE_SM_PAIR_AUTHREQ_BOND &&
              params->pair_rsp.authreq & BLE_SM_PAIR_AUTHREQ_BOND;

    rc = ble_store_read_peer_sec(&key_sec, &value_sec);
    if (!bonding) {
        TEST_ASSERT(rc == BLE_HS_ENOENT);
        peer_irk_expected = 0;
    } else {
        TEST_ASSERT_FATAL(rc == 0);

        peer_irk_expected = !!(peer_key_dist & BLE_SM_PAIR_KEY_DIST_ID);
        csrk_expected = !!(peer_key_dist & BLE_SM_PAIR_KEY_DIST_SIGN);

        TEST_ASSERT(value_sec.peer_addr_type == peer_id_addr_type);
        TEST_ASSERT(memcmp(value_sec.peer_addr, peer_id_addr, 6) == 0);
        TEST_ASSERT(value_sec.ediv == 0);
        TEST_ASSERT(value_sec.rand_num == 0);
        TEST_ASSERT(value_sec.authenticated == params->authenticated);

        /*** All keys get persisted in big endian. */

        TEST_ASSERT(value_sec.ltk_present == 1);
        TEST_ASSERT(memcmp(value_sec.ltk, params->ltk, 16) == 0);

        TEST_ASSERT(value_sec.irk_present == peer_irk_expected);
        if (peer_irk_expected) {
            TEST_ASSERT(memcmp(value_sec.irk, peer_irk, 16) == 0);
        }

        TEST_ASSERT(value_sec.csrk_present == csrk_expected);
        if (csrk_expected) {
            TEST_ASSERT(memcmp(value_sec.csrk, peer_csrk, 16) == 0);
        }
    }

    rc = ble_store_read_our_sec(&key_sec, &value_sec);
    if (!bonding) {
        TEST_ASSERT(rc == BLE_HS_ENOENT);
    } else {
        TEST_ASSERT_FATAL(rc == 0);

        our_irk_expected = !!(our_key_dist & BLE_SM_PAIR_KEY_DIST_ID);
        csrk_expected = !!(our_key_dist & BLE_SM_PAIR_KEY_DIST_SIGN);

        TEST_ASSERT(value_sec.peer_addr_type == peer_id_addr_type);
        TEST_ASSERT(memcmp(value_sec.peer_addr, peer_id_addr, 6) == 0);
        TEST_ASSERT(value_sec.ediv == 0);
        TEST_ASSERT(value_sec.rand_num == 0);
        TEST_ASSERT(value_sec.authenticated == params->authenticated);

        TEST_ASSERT(value_sec.ltk_present == 1);
        TEST_ASSERT(memcmp(value_sec.ltk, params->ltk, 16) == 0);

        TEST_ASSERT(value_sec.irk_present == our_irk_expected);
        if (our_irk_expected) {
            TEST_ASSERT(memcmp(value_sec.irk, our_irk, 16) == 0);
        }

        TEST_ASSERT(value_sec.csrk_present == csrk_expected);
        if (csrk_expected) {
            TEST_ASSERT(memcmp(value_sec.csrk, our_csrk, 16) == 0);
        }
    }

    /* Verify no other keys were persisted. */
    key_sec.idx++;
    rc = ble_store_read_our_sec(&key_sec, &value_sec);
    TEST_ASSERT_FATAL(rc == BLE_HS_ENOENT);
    rc = ble_store_read_peer_sec(&key_sec, &value_sec);
    TEST_ASSERT_FATAL(rc == BLE_HS_ENOENT);

    /* Verify we sent the peer's IRK to the controller. */
    if (peer_irk_expected) {
        ble_sm_test_util_verify_tx_add_resolve_list(peer_id_addr_type,
                                                    peer_id_addr,
                                                    peer_irk, our_irk);
    }
}

void
ble_sm_test_util_us_lgcy_good(
    struct ble_sm_test_lgcy_params *params)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_sm_test_util_init();
    ble_hs_test_util_set_public_addr(params->init_id_addr);
    ble_sm_dbg_set_next_pair_rand(params->random_req.value);
    ble_sm_dbg_set_next_ediv(params->ediv);
    ble_sm_dbg_set_next_start_rand(params->r);

    if (params->has_enc_info_req) {
        ble_sm_dbg_set_next_ltk(params->enc_info_req.ltk);
    }

    ble_hs_test_util_create_conn(2, params->resp_id_addr,
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

    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
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
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Receive a pair response from the peer. */
    ble_sm_test_util_rx_pair_rsp(2, &params->pair_rsp, 0);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Ensure we sent the expected pair confirm. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_confirm(&params->confirm_req);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Receive a pair confirm from the peer. */
    ble_sm_test_util_rx_confirm(2, &params->confirm_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Ensure we sent the expected pair random. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_random(&params->random_req);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Receive a pair random from the peer. */
    ble_sm_test_util_rx_random(2, &params->random_rsp, 0);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Ensure keys are distributed, if necessary. */
    if (params->has_enc_info_req) {
        ble_sm_test_util_verify_tx_enc_info(&params->enc_info_req);
    }

    /* Ensure we sent the expected start encryption command. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_start_enc(2, params->r, params->ediv,
                                         params->stk);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Receive an encryption changed event. */
    ble_sm_test_util_rx_enc_change(2, 0, 1);

    /* Pairing should now be complete. */
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Verify that security callback was executed. */
    TEST_ASSERT(ble_sm_test_gap_event == BLE_GAP_EVENT_ENC_CHANGE);
    TEST_ASSERT(ble_sm_test_gap_status == 0);
    TEST_ASSERT(ble_sm_test_sec_state.encrypted);
    TEST_ASSERT(!ble_sm_test_sec_state.authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_sm_test_sec_state.encrypted ==
                conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                conn->bhc_sec_state.authenticated);

    /* Verify the appropriate security material was persisted. */
    ble_sm_test_util_verify_lgcy_persist(params);
}

void
ble_sm_test_util_peer_fail_inval(
    int we_are_master,
    uint8_t *init_id_addr,
    uint8_t *resp_addr,
    struct ble_sm_pair_cmd *pair_req,
    struct ble_sm_pair_fail *pair_fail)
{
    struct ble_hs_conn *conn;

    ble_sm_test_util_init();
    ble_hs_test_util_set_public_addr(resp_addr);

    ble_hs_test_util_create_conn(2, init_id_addr, ble_sm_test_util_conn_cb,
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

    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Receive a pair request from the peer. */
    ble_sm_test_util_rx_pair_req(2, pair_req,
                                 BLE_HS_SM_US_ERR(pair_fail->reason));
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Ensure we sent the expected pair fail. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_fail(pair_fail);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Verify that security callback was not executed. */
    TEST_ASSERT(ble_sm_test_gap_event == -1);
    TEST_ASSERT(ble_sm_test_gap_status == -1);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(!conn->bhc_sec_state.authenticated);
}

void
ble_sm_test_util_peer_lgcy_fail_confirm(
    uint8_t *init_id_addr,
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

    ble_hs_test_util_create_conn(2, init_id_addr, ble_sm_test_util_conn_cb,
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
    ble_sm_test_util_io_inject_bad(2, BLE_SM_IOACT_NONE);

    /* Ensure we sent the expected pair response. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_rsp(pair_rsp);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, BLE_SM_IOACT_NONE);

    /* Receive a pair confirm from the peer. */
    ble_sm_test_util_rx_confirm(2, confirm_req);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, BLE_SM_IOACT_NONE);

    /* Ensure we sent the expected pair confirm. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_confirm(confirm_rsp);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, BLE_SM_IOACT_NONE);

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
    TEST_ASSERT(!ble_sm_test_sec_state.encrypted);
    TEST_ASSERT(!ble_sm_test_sec_state.authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_sm_test_sec_state.encrypted ==
                conn->bhc_sec_state.encrypted);
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

    ble_hs_test_util_set_public_addr(params->resp_id_addr);
    ble_sm_dbg_set_next_pair_rand(params->random_rsp.value);
    ble_sm_dbg_set_next_ediv(params->ediv);
    ble_sm_dbg_set_next_start_rand(params->r);

    if (params->has_enc_info_req) {
        ble_sm_dbg_set_next_ltk(params->enc_info_req.ltk);
    }

    ble_hs_test_util_create_conn(2, params->init_id_addr,
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

    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    if (params->has_sec_req) {
        rc = ble_sm_slave_initiate(2);
        TEST_ASSERT(rc == 0);

        /* Ensure we sent the expected security request. */
        ble_sm_test_util_verify_tx_sec_req(&params->sec_req);
    }

    /* Receive a pair request from the peer. */
    ble_sm_test_util_rx_pair_req(2, &params->pair_req, 0);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Ensure we sent the expected pair response. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_rsp(&params->pair_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    ble_sm_test_util_io_check_pre(&params->passkey_info,
                                  BLE_SM_PROC_STATE_CONFIRM);

    /* Receive a pair confirm from the peer. */
    ble_sm_test_util_rx_confirm(2, &params->confirm_req);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    ble_sm_test_util_io_check_post(&params->passkey_info,
                                   BLE_SM_PROC_STATE_CONFIRM);

    /* Ensure we sent the expected pair confirm. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_confirm(&params->confirm_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Receive a pair random from the peer. */
    ble_sm_test_util_rx_random(2, &params->random_req, 0);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Ensure we sent the expected pair random. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_random(&params->random_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Receive a long term key request from the controller. */
    ble_sm_test_util_set_lt_key_req_reply_ack(0, 2);
    ble_sm_test_util_rx_lt_key_req(2, params->r, params->ediv);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Ensure we sent the expected long term key request reply command. */
    ble_sm_test_util_verify_tx_lt_key_req_reply(2, params->stk);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

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
    TEST_ASSERT(ble_sm_test_sec_state.encrypted);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                params->authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_sm_test_sec_state.encrypted ==
                conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                conn->bhc_sec_state.authenticated);

    /* Verify the appropriate security material was persisted. */
    ble_sm_test_util_verify_lgcy_persist(params);
}

void
ble_sm_test_util_peer_lgcy_good(struct ble_sm_test_lgcy_params *params)
{
    params->passkey_info.io_before_rx = 0;
    ble_sm_test_util_peer_lgcy_good_once(params);

    params->passkey_info.io_before_rx = 1;
    ble_sm_test_util_peer_lgcy_good_once(params);
}

static void
ble_sm_test_util_peer_bonding_good(int send_enc_req,
                                   uint8_t peer_addr_type,
                                   uint8_t *peer_addr,
                                   uint8_t *ltk, int authenticated,
                                   uint16_t ediv, uint64_t rand_num)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_test_util_create_conn(2, peer_addr,
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

    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    if (send_enc_req) {
        rc = ble_sm_slave_initiate(2);
        TEST_ASSERT(rc == 0);
    }

    /* Receive a long term key request from the controller. */
    ble_sm_test_util_set_lt_key_req_reply_ack(0, 2);
    ble_sm_test_util_rx_lt_key_req(2, rand_num, ediv);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);

    /* Ensure the LTK request event got sent to the application. */
    TEST_ASSERT(ble_sm_test_store_obj_type ==
                BLE_STORE_OBJ_TYPE_OUR_SEC);
    TEST_ASSERT(ble_sm_test_store_key.sec.peer_addr_type ==
                BLE_ADDR_TYPE_PUBLIC);
    TEST_ASSERT(ble_sm_test_store_key.sec.ediv_rand_present);
    TEST_ASSERT(ble_sm_test_store_key.sec.ediv == ediv);
    TEST_ASSERT(ble_sm_test_store_key.sec.rand_num == rand_num);

    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, BLE_SM_IOACT_NONE);

    /* Ensure we sent the expected long term key request reply command. */
    ble_sm_test_util_verify_tx_lt_key_req_reply(2, ltk);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, BLE_SM_IOACT_NONE);

    /* Receive an encryption changed event. */
    ble_sm_test_util_rx_enc_change(2, 0, 1);

    /* Pairing should now be complete. */
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Verify that security callback was executed. */
    TEST_ASSERT(ble_sm_test_gap_event == BLE_GAP_EVENT_ENC_CHANGE);
    TEST_ASSERT(ble_sm_test_gap_status == 0);
    TEST_ASSERT(ble_sm_test_sec_state.encrypted);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_sm_test_sec_state.encrypted);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                authenticated);

    ble_hs_test_util_conn_disconnect(2);
}

void
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

    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Receive a long term key request from the controller. */
    ble_sm_test_util_set_lt_key_req_reply_ack(0, 2);
    ble_sm_test_util_rx_lt_key_req(2, rand_num, ediv);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);

    /* Ensure the LTK request event got sent to the application. */
    TEST_ASSERT(ble_sm_test_store_obj_type ==
                BLE_STORE_OBJ_TYPE_OUR_SEC);
    TEST_ASSERT(ble_sm_test_store_key.sec.ediv_rand_present);
    TEST_ASSERT(ble_sm_test_store_key.sec.ediv == ediv);
    TEST_ASSERT(ble_sm_test_store_key.sec.rand_num == rand_num);

    TEST_ASSERT(!conn->bhc_sec_state.encrypted);

    /* Ensure we sent the expected long term key request neg reply command. */
    ble_sm_test_util_verify_tx_lt_key_req_neg_reply(2);

    /* Ensure the security procedure was aborted. */
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(!conn->bhc_sec_state.authenticated);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);
}

/**
 * @param send_enc_req          Whether this procedure is initiated by a slave
 *                                  security request;
 *                                  1: Peer sends a security request at start.
 *                                  0: No security request; we initiate.
 */
static void
ble_sm_test_util_us_bonding_good(int send_enc_req,
                                 uint8_t peer_addr_type, uint8_t *peer_addr,
                                 uint8_t *ltk, int authenticated,
                                 uint16_t ediv, uint64_t rand_num)
{
    struct ble_sm_sec_req sec_req;
    struct ble_hs_conn *conn;

    ble_hs_test_util_create_conn(2, peer_addr,
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

    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    ble_hs_test_util_set_ack(
        host_hci_opcode_join(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_START_ENCRYPT),
        0);

    if (send_enc_req) {
        sec_req.authreq = 0;
        sec_req.authreq |= BLE_SM_PAIR_AUTHREQ_BOND;
        if (authenticated) {
            sec_req.authreq |= BLE_SM_PAIR_AUTHREQ_MITM;
        }
        ble_sm_test_util_rx_sec_req(2, &sec_req, 0);
    } else {
        ble_gap_security_initiate(2);
    }

    /* Ensure we sent the expected start encryption command. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_start_enc(2, rand_num, ediv, ltk);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, BLE_SM_IOACT_NONE);

    /* Receive an encryption changed event. */
    ble_sm_test_util_rx_enc_change(2, 0, 1);

    /* Pairing should now be complete. */
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Verify that security callback was executed. */
    TEST_ASSERT(ble_sm_test_gap_event == BLE_GAP_EVENT_ENC_CHANGE);
    TEST_ASSERT(ble_sm_test_gap_status == 0);
    TEST_ASSERT(ble_sm_test_sec_state.encrypted);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_sm_test_sec_state.encrypted);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                authenticated);

    ble_hs_test_util_conn_disconnect(2);
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

    ble_hs_test_util_set_public_addr(params->resp_id_addr);
    ble_sm_dbg_set_next_pair_rand(params->random_rsp[0].value);

    ble_sm_dbg_set_sc_keys(params->public_key_rsp.x, params->our_priv_key);

    ble_hs_priv_update_irk(params->id_info_req.irk);
    if (params->pair_rsp.resp_key_dist & BLE_SM_PAIR_KEY_DIST_SIGN) {
        ble_sm_dbg_set_next_csrk(params->sign_info_req.sig_key);
    }

    ble_hs_test_util_create_rpa_conn(2, params->resp_rpa,
                                     params->init_addr_type,
                                     params->init_id_addr,
                                     params->init_rpa,
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

    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    if (params->has_sec_req) {
        rc = ble_sm_slave_initiate(2);
        TEST_ASSERT(rc == 0);

        /* Ensure we sent the expected security request. */
        ble_sm_test_util_verify_tx_sec_req(&params->sec_req);
    }

    /* Receive a pair request from the peer. */
    ble_sm_test_util_rx_pair_req(2, &params->pair_req, 0);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Ensure we sent the expected pair response. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_rsp(&params->pair_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Receive a public key from the peer. */
    ble_sm_test_util_rx_public_key(2, &params->public_key_req);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Ensure we sent the expected public key. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_public_key(&params->public_key_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

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
            TEST_ASSERT(!conn->bhc_sec_state.encrypted);
            TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
            ble_sm_test_util_io_inject_bad(
                2, params->passkey_info.passkey.action);

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
        TEST_ASSERT(!conn->bhc_sec_state.encrypted);
        TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
        ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

        /* Receive a pair random from the peer. */
        ble_sm_test_util_rx_random(2, params->random_req + i, 0);
        TEST_ASSERT(!conn->bhc_sec_state.encrypted);
        TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
        ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

        /* Ensure we sent the expected pair random. */
        ble_hs_test_util_tx_all();
        ble_sm_test_util_verify_tx_pair_random(params->random_rsp + i);
        TEST_ASSERT(!conn->bhc_sec_state.encrypted);
        TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
        ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    }

    ble_sm_test_util_io_check_pre(&params->passkey_info,
                                  BLE_SM_PROC_STATE_DHKEY_CHECK);

    /* Receive a dhkey check from the peer. */
    ble_sm_test_util_rx_dhkey_check(2, &params->dhkey_check_req, 0);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    ble_sm_test_util_io_check_post(&params->passkey_info,
                                   BLE_SM_PROC_STATE_DHKEY_CHECK);

    /* Ensure we sent the expected dhkey check. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_dhkey_check(&params->dhkey_check_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Receive a long term key request from the controller. */
    ble_sm_test_util_set_lt_key_req_reply_ack(0, 2);
    ble_sm_test_util_rx_lt_key_req(2, 0, 0);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Ensure we sent the expected long term key request reply command. */
    ble_sm_test_util_verify_tx_lt_key_req_reply(2, params->ltk);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Receive an encryption changed event. */
    ble_sm_test_util_rx_enc_change(2, 0, 1);

    /* Verify key material gets sent to peer. */
    if (params->pair_rsp.resp_key_dist & BLE_SM_PAIR_KEY_DIST_ID) {
        ble_sm_test_util_verify_tx_id_info(&params->id_info_req);
        ble_sm_test_util_verify_tx_id_addr_info(&params->id_addr_info_req);
    }
    if (params->pair_rsp.resp_key_dist & BLE_SM_PAIR_KEY_DIST_SIGN) {
        ble_sm_test_util_verify_tx_sign_info(&params->sign_info_req);
    }

    /* Receive key material from peer. */
    if (params->pair_rsp.init_key_dist & BLE_SM_PAIR_KEY_DIST_ID) {
        ble_hs_test_util_set_ack(
            host_hci_opcode_join(BLE_HCI_OGF_LE,
                                 BLE_HCI_OCF_LE_ADD_RESOLV_LIST), 0);
        ble_sm_test_util_rx_id_info(2, &params->id_info_rsp, 0);
        ble_sm_test_util_rx_id_addr_info(2, &params->id_addr_info_rsp, 0);
    }
    if (params->pair_rsp.init_key_dist & BLE_SM_PAIR_KEY_DIST_SIGN) {
        ble_sm_test_util_rx_sign_info(2, &params->sign_info_rsp, 0);
    }

    /* Pairing should now be complete. */
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Verify that security callback was executed. */
    TEST_ASSERT(ble_sm_test_gap_event == BLE_GAP_EVENT_ENC_CHANGE);
    TEST_ASSERT(ble_sm_test_gap_status == 0);
    TEST_ASSERT(ble_sm_test_sec_state.encrypted);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                params->authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_sm_test_sec_state.encrypted ==
                conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                conn->bhc_sec_state.authenticated);

    /* Verify the appropriate security material was persisted. */
    ble_sm_test_util_verify_sc_persist(params, 0);

    ble_hs_test_util_conn_disconnect(2);
}

void
ble_sm_test_util_peer_sc_good(struct ble_sm_test_sc_params *params)
{
    /*** Peer is master; peer initiates pairing. */

    /* Peer performs IO first. */
    params->passkey_info.io_before_rx = 0;
    ble_sm_test_util_peer_sc_good_once(params);

    /* We perform IO first. */
    params->passkey_info.io_before_rx = 1;
    ble_sm_test_util_peer_sc_good_once(params);

    /*** Verify link can be restored via the encryption procedure. */

    /* Peer is master; peer initiates procedure. */
    ble_sm_test_util_peer_bonding_good(0, 0, params->init_id_addr,
                                       params->ltk, params->authenticated,
                                       0, 0);

    /* Peer is master; we initiate procedure via security request. */
    ble_sm_test_util_peer_bonding_good(1, 0, params->init_id_addr,
                                       params->ltk, params->authenticated,
                                       0, 0);

    /* We are master; we initiate procedure. */
    ble_sm_test_util_us_bonding_good(0, 0, params->init_id_addr,
                                     params->ltk, params->authenticated,
                                     0, 0);

    /* We are master; peer initiates procedure via security request. */
    ble_sm_test_util_us_bonding_good(1, 0, params->init_id_addr,
                                     params->ltk, params->authenticated,
                                     0, 0);
}

void
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

    ble_hs_test_util_set_public_addr(params->init_id_addr);
    ble_sm_dbg_set_next_pair_rand(params->random_req[0].value);

    ble_sm_dbg_set_sc_keys(params->public_key_req.x, params->our_priv_key);

    ble_hs_priv_update_irk(params->id_info_rsp.irk);
    if (params->pair_rsp.init_key_dist & BLE_SM_PAIR_KEY_DIST_SIGN) {
        ble_sm_dbg_set_next_csrk(params->sign_info_rsp.sig_key);
    }

    ble_hs_test_util_create_rpa_conn(2, params->init_rpa,
                                     params->resp_addr_type,
                                     params->resp_id_addr,
                                     params->resp_rpa,
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

    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
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
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Receive a pair response from the peer. */
    ble_sm_test_util_rx_pair_rsp(2, &params->pair_rsp, 0);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Ensure we sent the expected public key. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_public_key(&params->public_key_req);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Receive a public key from the peer. */
    ble_sm_test_util_rx_public_key(2, &params->public_key_rsp);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

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
            TEST_ASSERT(!conn->bhc_sec_state.encrypted);
            TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
            ble_sm_test_util_io_inject_bad(
                2, params->passkey_info.passkey.action);
        }

        /* Receive a pair confirm from the peer. */
        ble_sm_test_util_rx_confirm(2, params->confirm_rsp + i);
        TEST_ASSERT(!conn->bhc_sec_state.encrypted);
        TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
        ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

        /* Ensure we sent the expected pair random. */
        ble_hs_test_util_tx_all();
        ble_sm_test_util_verify_tx_pair_random(params->random_req + i);
        TEST_ASSERT(!conn->bhc_sec_state.encrypted);
        TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
        ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

        /* Receive a pair random from the peer. */
        ble_sm_test_util_rx_random(2, params->random_rsp + i, 0);
        TEST_ASSERT(!conn->bhc_sec_state.encrypted);
        TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
        ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);
    }

    ble_sm_test_util_io_inject(&params->passkey_info,
                               BLE_SM_PROC_STATE_DHKEY_CHECK);

    /* Ensure we sent the expected dhkey check. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_dhkey_check(&params->dhkey_check_req);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Receive a dhkey check from the peer. */
    ble_sm_test_util_rx_dhkey_check(2, &params->dhkey_check_rsp, 0);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Ensure we sent the expected start encryption command. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_start_enc(2, 0, 0, params->ltk);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Receive an encryption changed event. */
    ble_sm_test_util_rx_enc_change(2, 0, 1);

    /* Receive key material from peer. */
    if (params->pair_rsp.resp_key_dist & BLE_SM_PAIR_KEY_DIST_ID) {
        ble_sm_test_util_rx_id_info(2, &params->id_info_req, 0);
        ble_sm_test_util_rx_id_addr_info(2, &params->id_addr_info_req, 0);
    }
    if (params->pair_rsp.resp_key_dist & BLE_SM_PAIR_KEY_DIST_SIGN) {
        ble_sm_test_util_rx_sign_info(2, &params->sign_info_req, 0);
    }

    /* Verify key material gets sent to peer. */
    if (params->pair_rsp.init_key_dist & BLE_SM_PAIR_KEY_DIST_ID) {
        ble_sm_test_util_verify_tx_id_info(&params->id_info_rsp);
        ble_sm_test_util_verify_tx_id_addr_info(&params->id_addr_info_rsp);
    }
    if (params->pair_rsp.init_key_dist & BLE_SM_PAIR_KEY_DIST_SIGN) {
        ble_sm_test_util_verify_tx_sign_info(&params->sign_info_rsp);
    }

    /* Pairing should now be complete. */
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Verify that security callback was executed. */
    TEST_ASSERT(ble_sm_test_gap_event == BLE_GAP_EVENT_ENC_CHANGE);
    TEST_ASSERT(ble_sm_test_gap_status == 0);
    TEST_ASSERT(ble_sm_test_sec_state.encrypted);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                params->authenticated);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(ble_sm_test_sec_state.encrypted ==
                conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_test_sec_state.authenticated ==
                conn->bhc_sec_state.authenticated);

    /* Verify the appropriate security material was persisted. */
    ble_sm_test_util_verify_sc_persist(params, 1);
}

void
ble_sm_test_util_us_fail_inval(
    struct ble_sm_test_lgcy_params *params)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_sm_test_util_init();
    ble_hs_test_util_set_public_addr(params->resp_id_addr);

    ble_sm_dbg_set_next_pair_rand(((uint8_t[16]){0}));

    ble_hs_test_util_create_conn(2, params->init_id_addr,
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

    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Initiate the pairing procedure. */
    rc = ble_hs_test_util_security_initiate(2, 0);
    TEST_ASSERT_FATAL(rc == 0);

    /* Ensure we sent the expected pair request. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_req(&params->pair_req);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 1);
    ble_sm_test_util_io_inject_bad(2, params->passkey_info.passkey.action);

    /* Receive a pair response from the peer. */
    ble_sm_test_util_rx_pair_rsp(
        2, &params->pair_rsp, BLE_HS_SM_US_ERR(BLE_SM_ERR_INVAL));
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Ensure we sent the expected pair fail. */
    ble_hs_test_util_tx_all();
    ble_sm_test_util_verify_tx_pair_fail(&params->pair_fail);
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(ble_sm_dbg_num_procs() == 0);

    /* Verify that security callback was not executed. */
    TEST_ASSERT(ble_sm_test_gap_event == -1);
    TEST_ASSERT(ble_sm_test_gap_status == -1);

    /* Verify that connection has correct security state. */
    TEST_ASSERT(!conn->bhc_sec_state.encrypted);
    TEST_ASSERT(!conn->bhc_sec_state.authenticated);
}
