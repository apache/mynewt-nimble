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
#include "ble_hs_priv.h"

#if NIMBLE_OPT(SM)

static int
ble_l2cap_sm_tx(uint16_t conn_handle, struct os_mbuf *txom)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    BLE_HS_DBG_ASSERT(ble_hs_thread_safe());

    STATS_INC(ble_l2cap_stats, sm_tx);

    rc = ble_hs_misc_conn_chan_find_reqd(conn_handle, BLE_L2CAP_CID_SM,
                                         &conn, &chan);
    if (rc == 0) {
        BLE_HS_LOG(DEBUG, "ble_l2cap_sm_tx(): ");
        ble_hs_misc_log_mbuf(txom);
        BLE_HS_LOG(DEBUG, "\n");
        rc = ble_l2cap_tx(conn, chan, txom);
    }

    return rc;
}

static int
ble_l2cap_sm_init_req(uint16_t initial_sz, struct os_mbuf **out_txom)
{
    void *buf;
    int rc;

    *out_txom = ble_hs_misc_pkthdr();
    if (*out_txom == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    buf = os_mbuf_extend(*out_txom, BLE_L2CAP_SM_HDR_SZ + initial_sz);
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
ble_l2cap_sm_pair_cmd_parse(void *payload, int len,
                            struct ble_l2cap_sm_pair_cmd *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_L2CAP_SM_PAIR_CMD_SZ);

    u8ptr = payload;
    cmd->io_cap = u8ptr[0];
    cmd->oob_data_flag = u8ptr[1];
    cmd->authreq = u8ptr[2];
    cmd->max_enc_key_size = u8ptr[3];
    cmd->init_key_dist = u8ptr[4];
    cmd->resp_key_dist = u8ptr[5];
}

int
ble_l2cap_sm_pair_cmd_is_valid(struct ble_l2cap_sm_pair_cmd *cmd)
{
    if (cmd->io_cap >= BLE_L2CAP_SM_IO_CAP_RESERVED) {
        return 0;
    }

    if (cmd->oob_data_flag >= BLE_L2CAP_SM_PAIR_OOB_RESERVED) {
        return 0;
    }

    if (cmd->authreq & BLE_L2CAP_SM_PAIR_AUTHREQ_RESERVED) {
        return 0;
    }

    if (cmd->max_enc_key_size < BLE_L2CAP_SM_PAIR_KEY_SZ_MIN ||
        cmd->max_enc_key_size > BLE_L2CAP_SM_PAIR_KEY_SZ_MAX) {

        return 0;
    }

    if (cmd->init_key_dist & BLE_L2CAP_SM_PAIR_KEY_DIST_RESERVED) {
        return 0;
    }

    if (cmd->resp_key_dist & BLE_L2CAP_SM_PAIR_KEY_DIST_RESERVED) {
        return 0;
    }

    return 1;
}

void
ble_l2cap_sm_pair_cmd_write(void *payload, int len, int is_req,
                            struct ble_l2cap_sm_pair_cmd *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_CMD_SZ);

    u8ptr = payload;
    u8ptr[0] = is_req ? BLE_L2CAP_SM_OP_PAIR_REQ : BLE_L2CAP_SM_OP_PAIR_RSP;
    u8ptr[1] = cmd->io_cap;
    u8ptr[2] = cmd->oob_data_flag;
    u8ptr[3] = cmd->authreq;
    u8ptr[4] = cmd->max_enc_key_size;
    u8ptr[5] = cmd->init_key_dist;
    u8ptr[6] = cmd->resp_key_dist;
}

int
ble_l2cap_sm_pair_cmd_tx(uint16_t conn_handle, int is_req,
                         struct ble_l2cap_sm_pair_cmd *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_l2cap_sm_init_req(BLE_L2CAP_SM_PAIR_CMD_SZ, &txom);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    ble_l2cap_sm_pair_cmd_write(txom->om_data, txom->om_len, is_req, cmd);
    BLE_HS_DBG_ASSERT(ble_l2cap_sm_pair_cmd_is_valid(cmd));

    rc = ble_l2cap_sm_tx(conn_handle, txom);
    txom = NULL;

done:
    os_mbuf_free_chain(txom);
    return rc;
}

void
ble_l2cap_sm_pair_confirm_parse(void *payload, int len,
                                struct ble_l2cap_sm_pair_confirm *cmd)
{
    BLE_HS_DBG_ASSERT(len >= BLE_L2CAP_SM_PAIR_CONFIRM_SZ);
    memcpy(cmd->value, payload, sizeof cmd->value);
}

void
ble_l2cap_sm_pair_confirm_write(void *payload, int len,
                                struct ble_l2cap_sm_pair_confirm *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >=
                      BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_CONFIRM_SZ);

    u8ptr = payload;

    u8ptr[0] = BLE_L2CAP_SM_OP_PAIR_CONFIRM;
    memcpy(u8ptr + BLE_L2CAP_SM_HDR_SZ, cmd->value, sizeof cmd->value);
}

int
ble_l2cap_sm_pair_confirm_tx(uint16_t conn_handle,
                             struct ble_l2cap_sm_pair_confirm *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_l2cap_sm_init_req(BLE_L2CAP_SM_PAIR_CONFIRM_SZ, &txom);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    ble_l2cap_sm_pair_confirm_write(txom->om_data, txom->om_len, cmd);

    BLE_HS_LOG(DEBUG, "ble_l2cap_sm_pair_confirm_tx(): ");
    ble_hs_misc_log_mbuf(txom);
    BLE_HS_LOG(DEBUG, "\n");
    rc = ble_l2cap_sm_tx(conn_handle, txom);
    txom = NULL;

done:
    os_mbuf_free_chain(txom);
    return rc;
}

void
ble_l2cap_sm_pair_random_parse(void *payload, int len,
                               struct ble_l2cap_sm_pair_random *cmd)
{
    BLE_HS_DBG_ASSERT(len >= BLE_L2CAP_SM_PAIR_RANDOM_SZ);
    memcpy(cmd->value, payload, sizeof cmd->value);
}

void
ble_l2cap_sm_pair_random_write(void *payload, int len,
                               struct ble_l2cap_sm_pair_random *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >=
                      BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_RANDOM_SZ);

    u8ptr = payload;

    u8ptr[0] = BLE_L2CAP_SM_OP_PAIR_RANDOM;
    memcpy(u8ptr + BLE_L2CAP_SM_HDR_SZ, cmd->value, sizeof cmd->value);
}

int
ble_l2cap_sm_pair_random_tx(uint16_t conn_handle,
                            struct ble_l2cap_sm_pair_random *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_l2cap_sm_init_req(BLE_L2CAP_SM_PAIR_RANDOM_SZ, &txom);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    ble_l2cap_sm_pair_random_write(txom->om_data, txom->om_len, cmd);

    rc = ble_l2cap_sm_tx(conn_handle, txom);
    txom = NULL;

done:
    os_mbuf_free_chain(txom);
    return rc;
}

void
ble_l2cap_sm_pair_fail_parse(void *payload, int len,
                             struct ble_l2cap_sm_pair_fail *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_L2CAP_SM_PAIR_FAIL_SZ);

    u8ptr = payload;
    cmd->reason = u8ptr[0];
}

void
ble_l2cap_sm_pair_fail_write(void *payload, int len,
                             struct ble_l2cap_sm_pair_fail *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_FAIL_SZ);

    u8ptr = payload;

    u8ptr[0] = BLE_L2CAP_SM_OP_PAIR_FAIL;
    u8ptr[1] = cmd->reason;
}

int
ble_l2cap_sm_pair_fail_tx(uint16_t conn_handle, uint8_t reason)
{
    struct ble_l2cap_sm_pair_fail cmd;
    struct os_mbuf *txom;
    int rc;

    BLE_HS_DBG_ASSERT(reason > 0 && reason < BLE_L2CAP_SM_ERR_MAX_PLUS_1);

    rc = ble_l2cap_sm_init_req(BLE_L2CAP_SM_PAIR_FAIL_SZ, &txom);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    cmd.reason = reason;
    ble_l2cap_sm_pair_fail_write(txom->om_data, txom->om_len, &cmd);

    rc = ble_l2cap_sm_tx(conn_handle, txom);
    txom = NULL;

done:
    os_mbuf_free_chain(txom);
    return rc;
}

void
ble_l2cap_sm_enc_info_parse(void *payload, int len,
                            struct ble_l2cap_sm_enc_info *cmd)
{
    uint8_t *u8ptr = payload;
    memcpy(cmd->ltk_le, u8ptr, 16);
}

int
ble_l2cap_sm_enc_info_tx(uint16_t conn_handle,
                         struct ble_l2cap_sm_enc_info *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_l2cap_sm_init_req(BLE_L2CAP_SM_ENC_INFO_SZ, &txom);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    txom->om_data[0] = BLE_L2CAP_SM_OP_ENC_INFO;
    memcpy(txom->om_data + 1, cmd->ltk_le, sizeof cmd->ltk_le);
    
    rc = ble_l2cap_sm_tx(conn_handle, txom);
    txom = NULL;

done:
    os_mbuf_free_chain(txom);
    return rc;
}

void
ble_l2cap_sm_master_iden_parse(void *payload, int len,
                               struct ble_l2cap_sm_master_iden *cmd)
{
    uint8_t *u8ptr = payload;
    cmd->ediv = le16toh(u8ptr);
    cmd->rand_val = le64toh(u8ptr + 2);
}

int
ble_l2cap_sm_master_iden_tx(uint16_t conn_handle,
                            struct ble_l2cap_sm_master_iden *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_l2cap_sm_init_req(BLE_L2CAP_SM_MASTER_IDEN_SZ, &txom);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    txom->om_data[0] = BLE_L2CAP_SM_OP_MASTER_ID;
    htole16(txom->om_data + 1, cmd->ediv);
    htole64(txom->om_data + 3, cmd->rand_val);
    rc = ble_l2cap_sm_tx(conn_handle, txom);
    txom = NULL;

done:
    os_mbuf_free_chain(txom);
    return rc;
}

void
ble_l2cap_sm_iden_info_parse(void *payload, int len,
                             struct ble_l2cap_sm_iden_info *cmd)
{
    uint8_t *u8ptr = payload;
    memcpy(cmd->irk_le, u8ptr, 16);
}

int
ble_l2cap_sm_iden_info_tx(uint16_t conn_handle,
                          struct ble_l2cap_sm_iden_info *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_l2cap_sm_init_req(BLE_L2CAP_SM_IDEN_INFO_SZ, &txom);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    txom->om_data[0] = BLE_L2CAP_SM_OP_IDENTITY_INFO;
    memcpy(txom->om_data + 1, cmd->irk_le, sizeof cmd->irk_le);
    rc = ble_l2cap_sm_tx(conn_handle, txom);
    txom = NULL;

done:
    os_mbuf_free_chain(txom);
    return rc;
}

void
ble_l2cap_sm_iden_addr_parse(void *payload, int len,
                             struct ble_l2cap_sm_iden_addr_info *cmd) {
    uint8_t *u8ptr = payload;
    cmd->addr_type = *u8ptr;
    memcpy(cmd->bd_addr_le, u8ptr + 1, 6);
}

int
ble_l2cap_sm_iden_addr_tx(uint16_t conn_handle,
                          struct ble_l2cap_sm_iden_addr_info *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_l2cap_sm_init_req(BLE_L2CAP_SM_IDEN_ADDR_INFO_SZ, &txom);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    txom->om_data[0] = BLE_L2CAP_SM_OP_IDENTITY_ADDR_INFO;
    txom->om_data[1] = cmd->addr_type;
    memcpy(txom->om_data + 2, cmd->bd_addr_le, sizeof cmd->bd_addr_le);
    rc = ble_l2cap_sm_tx(conn_handle, txom);
    txom = NULL;

done:
    os_mbuf_free_chain(txom);
    return rc;
}

void
ble_l2cap_sm_signing_info_parse(void *payload, int len,
                               struct ble_l2cap_sm_signing_info *cmd) {
    uint8_t *u8ptr = payload;
    memcpy(cmd->sig_key_le, u8ptr, 16);
}

int
ble_l2cap_sm_signing_info_tx(uint16_t conn_handle,
                             struct ble_l2cap_sm_signing_info *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_l2cap_sm_init_req(BLE_L2CAP_SM_SIGNING_INFO_SZ, &txom);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    txom->om_data[0] = BLE_L2CAP_SM_OP_SIGN_INFO;
    memcpy(txom->om_data + 1, cmd->sig_key_le, sizeof cmd->sig_key_le);
    rc = ble_l2cap_sm_tx(conn_handle, txom);
    txom = NULL;

done:
    os_mbuf_free_chain(txom);
    return rc;
}

void
ble_l2cap_sm_sec_req_parse(void *payload, int len,
                           struct ble_l2cap_sm_sec_req *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_L2CAP_SM_SEC_REQ_SZ);

    u8ptr = payload;
    cmd->authreq = *u8ptr;
}

void
ble_l2cap_sm_sec_req_write(void *payload, int len,
                           struct ble_l2cap_sm_sec_req *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_SEC_REQ_SZ);

    u8ptr = payload;

    u8ptr[0] = BLE_L2CAP_SM_OP_SEC_REQ;
    u8ptr[1] = cmd->authreq;
}

int
ble_l2cap_sm_sec_req_tx(uint16_t conn_handle, struct ble_l2cap_sm_sec_req *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_l2cap_sm_init_req(BLE_L2CAP_SM_SEC_REQ_SZ, &txom);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    ble_l2cap_sm_sec_req_write(txom->om_data, txom->om_len, cmd);
    rc = ble_l2cap_sm_tx(conn_handle, txom);
    txom = NULL;

done:
    os_mbuf_free_chain(txom);
    return rc;
}
#endif
