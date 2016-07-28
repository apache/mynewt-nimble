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
#include "console/console.h"
#include "nimble/ble.h"
#include "nimble/nimble_opt.h"
#include "host/ble_sm.h"
#include "ble_hs_priv.h"

#if NIMBLE_OPT(SM)

static int
ble_sm_tx(uint16_t conn_handle, struct os_mbuf *txom)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());

    STATS_INC(ble_l2cap_stats, sm_tx);

    rc = ble_hs_misc_conn_chan_find_reqd(conn_handle, BLE_L2CAP_CID_SM,
                                         &conn, &chan);
    if (rc != 0) {
        os_mbuf_free_chain(txom);
        return rc;
    }

    rc = ble_l2cap_tx(conn, chan, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_sm_init_req(uint16_t initial_sz, struct os_mbuf **out_txom)
{
    void *buf;
    int rc;

    *out_txom = ble_hs_mbuf_l2cap_pkt();
    if (*out_txom == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    buf = os_mbuf_extend(*out_txom, BLE_SM_HDR_SZ + initial_sz);
    if (buf == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(*out_txom);
    *out_txom = NULL;
    return rc;
}

void
ble_sm_pair_cmd_parse(void *payload, int len, struct ble_sm_pair_cmd *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_SM_PAIR_CMD_SZ);

    u8ptr = payload;
    cmd->io_cap = u8ptr[0];
    cmd->oob_data_flag = u8ptr[1];
    cmd->authreq = u8ptr[2];
    cmd->max_enc_key_size = u8ptr[3];
    cmd->init_key_dist = u8ptr[4];
    cmd->resp_key_dist = u8ptr[5];
}

int
ble_sm_pair_cmd_is_valid(struct ble_sm_pair_cmd *cmd)
{
    if (cmd->io_cap >= BLE_SM_IO_CAP_RESERVED) {
        return 0;
    }

    if (cmd->oob_data_flag >= BLE_SM_PAIR_OOB_RESERVED) {
        return 0;
    }

    if (cmd->authreq & BLE_SM_PAIR_AUTHREQ_RESERVED) {
        return 0;
    }

    if (cmd->max_enc_key_size < BLE_SM_PAIR_KEY_SZ_MIN ||
        cmd->max_enc_key_size > BLE_SM_PAIR_KEY_SZ_MAX) {

        return 0;
    }

    if (cmd->init_key_dist & BLE_SM_PAIR_KEY_DIST_RESERVED) {
        return 0;
    }

    if (cmd->resp_key_dist & BLE_SM_PAIR_KEY_DIST_RESERVED) {
        return 0;
    }

    return 1;
}

void
ble_sm_pair_cmd_write(void *payload, int len, int is_req,
                      struct ble_sm_pair_cmd *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_SM_HDR_SZ + BLE_SM_PAIR_CMD_SZ);

    u8ptr = payload;
    u8ptr[0] = is_req ? BLE_SM_OP_PAIR_REQ : BLE_SM_OP_PAIR_RSP;
    u8ptr[1] = cmd->io_cap;
    u8ptr[2] = cmd->oob_data_flag;
    u8ptr[3] = cmd->authreq;
    u8ptr[4] = cmd->max_enc_key_size;
    u8ptr[5] = cmd->init_key_dist;
    u8ptr[6] = cmd->resp_key_dist;
}

int
ble_sm_pair_cmd_tx(uint16_t conn_handle, int is_req,
                   struct ble_sm_pair_cmd *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_sm_init_req(BLE_SM_PAIR_CMD_SZ, &txom);
    if (rc != 0) {
        return BLE_HS_ENOMEM;
    }

    ble_sm_pair_cmd_write(txom->om_data, txom->om_len, is_req, cmd);
    BLE_SM_LOG_CMD(1, is_req ? "pair req" : "pair rsp", conn_handle,
                   ble_sm_pair_cmd_log, cmd);
    BLE_HS_DBG_ASSERT(ble_sm_pair_cmd_is_valid(cmd));

    rc = ble_sm_tx(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_sm_pair_cmd_log(struct ble_sm_pair_cmd *cmd)
{
    BLE_HS_LOG(DEBUG, "io_cap=%d oob_data_flag=%d authreq=0x%02x "
                      "mac_enc_key_size=%d init_key_dist=%d "
                      "resp_key_dist=%d",
               cmd->io_cap, cmd->oob_data_flag, cmd->authreq,
               cmd->max_enc_key_size, cmd->init_key_dist,
               cmd->resp_key_dist);
}

void
ble_sm_pair_confirm_parse(void *payload, int len,
                          struct ble_sm_pair_confirm *cmd)
{
    BLE_HS_DBG_ASSERT(len >= BLE_SM_PAIR_CONFIRM_SZ);
    memcpy(cmd->value, payload, sizeof cmd->value);
}

void
ble_sm_pair_confirm_write(void *payload, int len,
                          struct ble_sm_pair_confirm *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_SM_HDR_SZ + BLE_SM_PAIR_CONFIRM_SZ);

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_PAIR_CONFIRM;
    memcpy(u8ptr + BLE_SM_HDR_SZ, cmd->value, sizeof cmd->value);
}

int
ble_sm_pair_confirm_tx(uint16_t conn_handle, struct ble_sm_pair_confirm *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_sm_init_req(BLE_SM_PAIR_CONFIRM_SZ, &txom);
    if (rc != 0) {
        return BLE_HS_ENOMEM;
    }

    ble_sm_pair_confirm_write(txom->om_data, txom->om_len, cmd);
    BLE_SM_LOG_CMD(1, "confirm", conn_handle, ble_sm_pair_confirm_log, cmd);

    rc = ble_sm_tx(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_sm_pair_confirm_log(struct ble_sm_pair_confirm *cmd)
{
    BLE_HS_LOG(DEBUG, "value=");
    ble_hs_log_flat_buf(cmd->value, sizeof cmd->value);
}

void
ble_sm_pair_random_parse(void *payload, int len,
                         struct ble_sm_pair_random *cmd)
{
    BLE_HS_DBG_ASSERT(len >= BLE_SM_PAIR_RANDOM_SZ);
    memcpy(cmd->value, payload, sizeof cmd->value);
}

void
ble_sm_pair_random_write(void *payload, int len,
                         struct ble_sm_pair_random *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >=
                      BLE_SM_HDR_SZ + BLE_SM_PAIR_RANDOM_SZ);

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_PAIR_RANDOM;
    memcpy(u8ptr + BLE_SM_HDR_SZ, cmd->value, sizeof cmd->value);
}

int
ble_sm_pair_random_tx(uint16_t conn_handle, struct ble_sm_pair_random *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_sm_init_req(BLE_SM_PAIR_RANDOM_SZ, &txom);
    if (rc != 0) {
        return BLE_HS_ENOMEM;
    }

    ble_sm_pair_random_write(txom->om_data, txom->om_len, cmd);
    BLE_SM_LOG_CMD(1, "random", conn_handle, ble_sm_pair_random_log, cmd);

    rc = ble_sm_tx(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_sm_pair_random_log(struct ble_sm_pair_random *cmd)
{
    BLE_HS_LOG(DEBUG, "value=");
    ble_hs_log_flat_buf(cmd->value, sizeof cmd->value);
}

void
ble_sm_pair_fail_parse(void *payload, int len, struct ble_sm_pair_fail *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_SM_PAIR_FAIL_SZ);

    u8ptr = payload;
    cmd->reason = u8ptr[0];
}

void
ble_sm_pair_fail_write(void *payload, int len, struct ble_sm_pair_fail *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_SM_HDR_SZ + BLE_SM_PAIR_FAIL_SZ);

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_PAIR_FAIL;
    u8ptr[1] = cmd->reason;
}

/* XXX: Should not require locked. */
int
ble_sm_pair_fail_tx(uint16_t conn_handle, uint8_t reason)
{
    struct ble_sm_pair_fail cmd;
    struct os_mbuf *txom;
    int rc;

    BLE_HS_DBG_ASSERT(reason > 0 && reason < BLE_SM_ERR_MAX_PLUS_1);

    rc = ble_sm_init_req(BLE_SM_PAIR_FAIL_SZ, &txom);
    if (rc != 0) {
        return BLE_HS_ENOMEM;
    }

    cmd.reason = reason;

    ble_sm_pair_fail_write(txom->om_data, txom->om_len, &cmd);
    BLE_SM_LOG_CMD(1, "fail", conn_handle, ble_sm_pair_fail_log, &cmd);

    rc = ble_sm_tx(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_sm_pair_fail_log(struct ble_sm_pair_fail *cmd)
{
    BLE_HS_LOG(DEBUG, "reason=%d", cmd->reason);
}

void
ble_sm_enc_info_parse(void *payload, int len, struct ble_sm_enc_info *cmd)
{
    memcpy(cmd->ltk, payload, sizeof cmd->ltk);
}

void
ble_sm_enc_info_write(void *payload, int len, struct ble_sm_enc_info *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_SM_HDR_SZ + BLE_SM_ENC_INFO_SZ);

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_ENC_INFO;
    memcpy(u8ptr + 1, cmd->ltk, sizeof cmd->ltk);
}

int
ble_sm_enc_info_tx(uint16_t conn_handle, struct ble_sm_enc_info *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_sm_init_req(BLE_SM_ENC_INFO_SZ, &txom);
    if (rc != 0) {
        return BLE_HS_ENOMEM;
    }

    ble_sm_enc_info_write(txom->om_data, txom->om_len, cmd);

    BLE_SM_LOG_CMD(1, "enc info", conn_handle, ble_sm_enc_info_log, cmd);
    
    rc = ble_sm_tx(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_sm_enc_info_log(struct ble_sm_enc_info *cmd)
{
    BLE_HS_LOG(DEBUG, "ltk=");
    ble_hs_log_flat_buf(cmd->ltk, sizeof cmd->ltk);
}

void
ble_sm_master_id_parse(void *payload, int len, struct ble_sm_master_id *cmd)
{
    uint8_t *u8ptr;

    u8ptr = payload;

    cmd->ediv = le16toh(u8ptr);
    cmd->rand_val = le64toh(u8ptr + 2);
}

void
ble_sm_master_id_write(void *payload, int len, struct ble_sm_master_id *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_SM_HDR_SZ + BLE_SM_MASTER_ID_SZ);

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_MASTER_ID;
    htole16(u8ptr + 1, cmd->ediv);
    htole64(u8ptr + 3, cmd->rand_val);
}

int
ble_sm_master_id_tx(uint16_t conn_handle, struct ble_sm_master_id *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_sm_init_req(BLE_SM_MASTER_ID_SZ, &txom);
    if (rc != 0) {
        return BLE_HS_ENOMEM;
    }

    txom->om_data[0] = BLE_SM_OP_MASTER_ID;
    htole16(txom->om_data + 1, cmd->ediv);
    htole64(txom->om_data + 3, cmd->rand_val);

    BLE_SM_LOG_CMD(1, "master id", conn_handle, ble_sm_master_id_log, cmd);

    rc = ble_sm_tx(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_sm_master_id_log(struct ble_sm_master_id *cmd)
{
    /* These get logged separately to accommodate a bug in the va_args
     * implementation related to 64-bit integers.
     */
    BLE_HS_LOG(DEBUG, "ediv=0x%04x ", cmd->ediv);
    BLE_HS_LOG(DEBUG, "rand=0x%016llx", cmd->rand_val);
}

void
ble_sm_id_info_parse(void *payload, int len, struct ble_sm_id_info *cmd)
{
    memcpy(cmd->irk, payload, 16);
}

void
ble_sm_id_info_write(void *payload, int len, struct ble_sm_id_info *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_SM_HDR_SZ + BLE_SM_ID_INFO_SZ);

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_IDENTITY_INFO;
    memcpy(u8ptr + BLE_SM_HDR_SZ, cmd->irk, sizeof cmd->irk);
}

int
ble_sm_id_info_tx(uint16_t conn_handle, struct ble_sm_id_info *cmd)
{
    struct os_mbuf *txom;
    int rc;

    BLE_SM_LOG_CMD(1, "id info", conn_handle, ble_sm_id_info_log, cmd);

    rc = ble_sm_init_req(BLE_SM_ID_INFO_SZ, &txom);
    if (rc != 0) {
        return BLE_HS_ENOMEM;
    }

    ble_sm_id_info_write(txom->om_data, txom->om_len, cmd);

    rc = ble_sm_tx(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_sm_id_info_log(struct ble_sm_id_info *cmd)
{
    BLE_HS_LOG(DEBUG, "irk=");
    ble_hs_log_flat_buf(cmd->irk, sizeof cmd->irk);
}

void
ble_sm_id_addr_info_parse(void *payload, int len,
                          struct ble_sm_id_addr_info *cmd)
{
    uint8_t *u8ptr = payload;
    cmd->addr_type = *u8ptr;
    memcpy(cmd->bd_addr, u8ptr + 1, 6);
}

void
ble_sm_id_addr_info_write(void *payload, int len,
                          struct ble_sm_id_addr_info *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_SM_HDR_SZ + BLE_SM_ID_ADDR_INFO_SZ);

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_IDENTITY_ADDR_INFO;
    u8ptr[1] = cmd->addr_type;
    memcpy(u8ptr + 2, cmd->bd_addr, sizeof cmd->bd_addr);
}

int
ble_sm_id_addr_info_tx(uint16_t conn_handle, struct ble_sm_id_addr_info *cmd)
{
    struct os_mbuf *txom;
    int rc;

    BLE_SM_LOG_CMD(1, "id addr info", conn_handle, ble_sm_id_addr_info_log,
                   cmd);

    rc = ble_sm_init_req(BLE_SM_ID_ADDR_INFO_SZ, &txom);
    if (rc != 0) {
        return BLE_HS_ENOMEM;
    }

    ble_sm_id_addr_info_write(txom->om_data, txom->om_len, cmd);

    rc = ble_sm_tx(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_sm_id_addr_info_log(struct ble_sm_id_addr_info *cmd)
{
    BLE_HS_LOG(DEBUG, "addr_type=%d addr=", cmd->addr_type);
    BLE_HS_LOG_ADDR(DEBUG, cmd->bd_addr);
}

void
ble_sm_sign_info_parse(void *payload, int len, struct ble_sm_sign_info *cmd)
{
    memcpy(cmd->sig_key, payload, 16);
}

void
ble_sm_sign_info_write(void *payload, int len, struct ble_sm_sign_info *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_SM_HDR_SZ + BLE_SM_SIGN_INFO_SZ);

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_SIGN_INFO;
    memcpy(u8ptr + BLE_SM_HDR_SZ, cmd->sig_key, sizeof cmd->sig_key);
}

int
ble_sm_sign_info_tx(uint16_t conn_handle, struct ble_sm_sign_info *cmd)
{
    struct os_mbuf *txom;
    int rc;

    BLE_SM_LOG_CMD(1, "sign info", conn_handle, ble_sm_sign_info_log, cmd);

    rc = ble_sm_init_req(BLE_SM_SIGN_INFO_SZ, &txom);
    if (rc != 0) {
        return BLE_HS_ENOMEM;
    }

    ble_sm_sign_info_write(txom->om_data, txom->om_len, cmd);

    rc = ble_sm_tx(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_sm_sign_info_log(struct ble_sm_sign_info *cmd)
{
    BLE_HS_LOG(DEBUG, "sig_key=");
    ble_hs_log_flat_buf(cmd->sig_key, sizeof cmd->sig_key);
}

void
ble_sm_sec_req_parse(void *payload, int len, struct ble_sm_sec_req *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_SM_SEC_REQ_SZ);

    u8ptr = payload;
    cmd->authreq = *u8ptr;
}

void
ble_sm_sec_req_write(void *payload, int len, struct ble_sm_sec_req *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_SM_HDR_SZ + BLE_SM_SEC_REQ_SZ);

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_SEC_REQ;
    u8ptr[1] = cmd->authreq;
}

int
ble_sm_sec_req_tx(uint16_t conn_handle, struct ble_sm_sec_req *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_sm_init_req(BLE_SM_SEC_REQ_SZ, &txom);
    if (rc != 0) {
        return BLE_HS_ENOMEM;
    }

    ble_sm_sec_req_write(txom->om_data, txom->om_len, cmd);

    BLE_SM_LOG_CMD(1, "sec req", conn_handle, ble_sm_sec_req_log, cmd);

    rc = ble_sm_tx(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_sm_sec_req_log(struct ble_sm_sec_req *cmd)
{
    BLE_HS_LOG(DEBUG, "authreq=0x%02x", cmd->authreq);
}

void
ble_sm_public_key_parse(void *payload, int len, struct ble_sm_public_key *cmd)
{
    uint8_t *u8ptr;

    u8ptr = payload;

    memcpy(cmd->x, u8ptr, sizeof cmd->x);
    u8ptr += sizeof cmd->x;

    memcpy(cmd->y, u8ptr, sizeof cmd->y);
    u8ptr += sizeof cmd->y;
}

int
ble_sm_public_key_write(void *payload, int len, struct ble_sm_public_key *cmd)
{
    uint8_t *u8ptr;

    if (len < BLE_SM_HDR_SZ + BLE_SM_PUBLIC_KEY_SZ) {
        return BLE_HS_EMSGSIZE;
    }

    u8ptr = payload;

    *u8ptr = BLE_SM_OP_PAIR_PUBLIC_KEY;
    u8ptr++;

    memcpy(u8ptr, cmd->x, sizeof cmd->x);
    memcpy(u8ptr + 32, cmd->y, sizeof cmd->y);

    return 0;
}

int
ble_sm_public_key_tx(uint16_t conn_handle, struct ble_sm_public_key *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_sm_init_req(BLE_SM_PUBLIC_KEY_SZ, &txom);
    if (rc != 0) {
        return BLE_HS_ENOMEM;
    }

    rc = ble_sm_public_key_write(txom->om_data, txom->om_len, cmd);
    assert(rc == 0);

    BLE_SM_LOG_CMD(1, "public key", conn_handle, ble_sm_public_key_log, cmd);

    rc = ble_sm_tx(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_sm_public_key_log(struct ble_sm_public_key *cmd)
{
    BLE_HS_LOG(DEBUG, "x=");
    ble_hs_log_flat_buf(cmd->x, sizeof cmd->x);
    BLE_HS_LOG(DEBUG, "y=");
    ble_hs_log_flat_buf(cmd->y, sizeof cmd->y);
}

void
ble_sm_dhkey_check_parse(void *payload, int len,
                         struct ble_sm_dhkey_check *cmd)
{
    memcpy(cmd->value, payload, sizeof cmd->value);
}

int
ble_sm_dhkey_check_write(void *payload, int len,
                         struct ble_sm_dhkey_check *cmd)
{
    uint8_t *u8ptr;

    if (len < BLE_SM_HDR_SZ + BLE_SM_DHKEY_CHECK_SZ) {
        return BLE_HS_EMSGSIZE;
    }

    u8ptr = payload;

    *u8ptr = BLE_SM_OP_PAIR_DHKEY_CHECK;
    u8ptr++;

    memcpy(u8ptr, cmd->value, sizeof cmd->value);

    return 0;
}

int
ble_sm_dhkey_check_tx(uint16_t conn_handle, struct ble_sm_dhkey_check *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_sm_init_req(BLE_SM_DHKEY_CHECK_SZ, &txom);
    if (rc != 0) {
        return BLE_HS_ENOMEM;
    }

    rc = ble_sm_dhkey_check_write(txom->om_data, txom->om_len, cmd);
    assert(rc == 0);

    BLE_SM_LOG_CMD(1, "dhkey check", conn_handle, ble_sm_dhkey_check_log, cmd);

    rc = ble_sm_tx(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_sm_dhkey_check_log(struct ble_sm_dhkey_check *cmd)
{
    BLE_HS_LOG(DEBUG, "value=");
    ble_hs_log_flat_buf(cmd->value, sizeof cmd->value);
}

#endif
