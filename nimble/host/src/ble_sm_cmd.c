/*
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

void *
ble_sm_cmd_get(uint8_t opcode, size_t len, struct os_mbuf **txom)
{
    struct ble_sm_hdr *hdr;

    *txom = ble_hs_mbuf_l2cap_pkt();
    if (*txom == NULL) {
        return NULL;
    }

    if (os_mbuf_extend(*txom, sizeof(*hdr) + len) == NULL) {
        os_mbuf_free_chain(*txom);
        return NULL;
    }

    hdr = (struct ble_sm_hdr *)(*txom)->om_data;

    hdr->opcode = opcode;

    return hdr->data;
}

/* this function consumes tx os_mbuf */
int
ble_sm_tx(uint16_t conn_handle, struct os_mbuf *txom)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;

    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());

    STATS_INC(ble_l2cap_stats, sm_tx);

    ble_hs_misc_conn_chan_find_reqd(conn_handle, BLE_L2CAP_CID_SM,
                                    &conn, &chan);
    return ble_l2cap_tx(conn, chan, txom);
}

#if NIMBLE_BLE_SM
void
ble_sm_pair_cmd_parse(void *payload, int len, struct ble_sm_pair_cmd *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= sizeof(struct ble_sm_pair_cmd));

    u8ptr = payload;
    cmd->io_cap = u8ptr[0];
    cmd->oob_data_flag = u8ptr[1];
    cmd->authreq = u8ptr[2];
    cmd->max_enc_key_size = u8ptr[3];
    cmd->init_key_dist = u8ptr[4];
    cmd->resp_key_dist = u8ptr[5];
}

void
ble_sm_pair_cmd_write(void *payload, int len, int is_req,
                      struct ble_sm_pair_cmd *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= sizeof(struct ble_sm_hdr) + sizeof(struct ble_sm_pair_cmd));

    u8ptr = payload;
    u8ptr[0] = is_req ? BLE_SM_OP_PAIR_REQ : BLE_SM_OP_PAIR_RSP;
    u8ptr[1] = cmd->io_cap;
    u8ptr[2] = cmd->oob_data_flag;
    u8ptr[3] = cmd->authreq;
    u8ptr[4] = cmd->max_enc_key_size;
    u8ptr[5] = cmd->init_key_dist;
    u8ptr[6] = cmd->resp_key_dist;
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
    BLE_HS_DBG_ASSERT(len >= sizeof(struct ble_sm_pair_confirm));
    memcpy(cmd->value, payload, sizeof cmd->value);
}

void
ble_sm_pair_confirm_write(void *payload, int len,
                          struct ble_sm_pair_confirm *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= sizeof(struct ble_sm_hdr) + sizeof(struct ble_sm_pair_confirm));

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_PAIR_CONFIRM;
    memcpy(u8ptr + sizeof(struct ble_sm_hdr), cmd->value, sizeof cmd->value);
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
    BLE_HS_DBG_ASSERT(len >= sizeof(struct ble_sm_pair_random));
    memcpy(cmd->value, payload, sizeof cmd->value);
}

void
ble_sm_pair_random_write(void *payload, int len,
                         struct ble_sm_pair_random *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >=
                      sizeof(struct ble_sm_hdr) + sizeof(struct ble_sm_pair_random));

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_PAIR_RANDOM;
    memcpy(u8ptr + sizeof(struct ble_sm_hdr), cmd->value, sizeof cmd->value);
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

    BLE_HS_DBG_ASSERT(len >= sizeof(struct ble_sm_pair_fail));

    u8ptr = payload;
    cmd->reason = u8ptr[0];
}

void
ble_sm_pair_fail_write(void *payload, int len, struct ble_sm_pair_fail *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= sizeof(struct ble_sm_hdr) + sizeof(struct ble_sm_pair_fail));

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_PAIR_FAIL;
    u8ptr[1] = cmd->reason;
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

    BLE_HS_DBG_ASSERT(len >= sizeof(struct ble_sm_hdr) + sizeof(struct ble_sm_enc_info));

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_ENC_INFO;
    memcpy(u8ptr + 1, cmd->ltk, sizeof cmd->ltk);
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

    cmd->ediv = get_le16(u8ptr);
    cmd->rand_val = get_le64(u8ptr + 2);
}

void
ble_sm_master_id_write(void *payload, int len, struct ble_sm_master_id *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= sizeof(struct ble_sm_hdr) + sizeof(struct ble_sm_master_id));

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_MASTER_ID;
    put_le16(u8ptr + 1, cmd->ediv);
    put_le64(u8ptr + 3, cmd->rand_val);
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

    BLE_HS_DBG_ASSERT(len >= sizeof(struct ble_sm_hdr) + sizeof(struct ble_sm_id_info));

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_IDENTITY_INFO;
    memcpy(u8ptr + sizeof(struct ble_sm_hdr), cmd->irk, sizeof cmd->irk);
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

    BLE_HS_DBG_ASSERT(len >= sizeof(struct ble_sm_hdr) + sizeof(struct ble_sm_id_addr_info));

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_IDENTITY_ADDR_INFO;
    u8ptr[1] = cmd->addr_type;
    memcpy(u8ptr + 2, cmd->bd_addr, sizeof cmd->bd_addr);
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

    BLE_HS_DBG_ASSERT(len >= sizeof(struct ble_sm_hdr) + sizeof(struct ble_sm_sign_info));

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_SIGN_INFO;
    memcpy(u8ptr + sizeof(struct ble_sm_hdr), cmd->sig_key, sizeof cmd->sig_key);
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

    BLE_HS_DBG_ASSERT(len >= sizeof(struct ble_sm_sec_req));

    u8ptr = payload;
    cmd->authreq = *u8ptr;
}

void
ble_sm_sec_req_write(void *payload, int len, struct ble_sm_sec_req *cmd)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= sizeof(struct ble_sm_hdr) + sizeof(struct ble_sm_sec_req));

    u8ptr = payload;

    u8ptr[0] = BLE_SM_OP_SEC_REQ;
    u8ptr[1] = cmd->authreq;
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

    if (len < sizeof(struct ble_sm_hdr) + sizeof(struct ble_sm_public_key)) {
        return BLE_HS_EMSGSIZE;
    }

    u8ptr = payload;

    *u8ptr = BLE_SM_OP_PAIR_PUBLIC_KEY;
    u8ptr++;

    memcpy(u8ptr, cmd->x, sizeof cmd->x);
    memcpy(u8ptr + 32, cmd->y, sizeof cmd->y);

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

    if (len < sizeof(struct ble_sm_hdr) + sizeof(struct ble_sm_dhkey_check)) {
        return BLE_HS_EMSGSIZE;
    }

    u8ptr = payload;

    *u8ptr = BLE_SM_OP_PAIR_DHKEY_CHECK;
    u8ptr++;

    memcpy(u8ptr, cmd->value, sizeof cmd->value);

    return 0;
}

void
ble_sm_dhkey_check_log(struct ble_sm_dhkey_check *cmd)
{
    BLE_HS_LOG(DEBUG, "value=");
    ble_hs_log_flat_buf(cmd->value, sizeof cmd->value);
}

#endif
