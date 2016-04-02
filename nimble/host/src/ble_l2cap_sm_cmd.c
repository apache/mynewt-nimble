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

#if NIMBLE_OPT_SM

static int
ble_l2cap_sm_tx(uint16_t conn_handle, struct os_mbuf *txom)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    STATS_INC(ble_l2cap_stats, sm_tx);

    ble_hs_conn_lock();

    rc = ble_hs_misc_conn_chan_find_reqd(conn_handle, BLE_L2CAP_CID_SM,
                                         &conn, &chan);
    if (rc == 0) {
        rc = ble_l2cap_tx(conn, chan, txom);
    }

    ble_hs_conn_unlock();

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
ble_l2cap_sm_pair_fail_tx(uint16_t conn_handle,
                          struct ble_l2cap_sm_pair_fail *cmd)
{
    struct os_mbuf *txom;
    int rc;

    rc = ble_l2cap_sm_init_req(BLE_L2CAP_SM_PAIR_FAIL_SZ, &txom);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    ble_l2cap_sm_pair_fail_write(txom->om_data, txom->om_len, cmd);

    rc = ble_l2cap_sm_tx(conn_handle, txom);
    txom = NULL;

done:
    os_mbuf_free_chain(txom);
    return rc;
}

#endif
