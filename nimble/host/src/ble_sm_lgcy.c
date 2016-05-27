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

static int
ble_sm_lgcy_confirm_prepare_args(struct ble_l2cap_sm_proc *proc,
                                 uint8_t *k, uint8_t *preq, uint8_t *pres,
                                 uint8_t *iat, uint8_t *rat,
                                 uint8_t *ia, uint8_t *ra)
{
    struct ble_hs_conn *conn;

    BLE_HS_DBG_ASSERT(ble_hs_thread_safe());

    conn = ble_hs_conn_find(proc->conn_handle);
    if (conn != NULL) {
        if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
            *iat = BLE_ADDR_TYPE_PUBLIC; /* XXX: Support random addresses. */
            memcpy(ia, ble_hs_our_dev.public_addr, 6);

            *rat = conn->bhc_addr_type;
            memcpy(ra, conn->bhc_addr, 6);
        } else {
            *rat = BLE_ADDR_TYPE_PUBLIC; /* XXX: Support random addresses. */
            memcpy(ra, ble_hs_our_dev.public_addr, 6);

            *iat = conn->bhc_addr_type;
            memcpy(ia, conn->bhc_addr, 6);
        }
    }

    if (conn == NULL) {
        return BLE_HS_ENOTCONN;
    }

    memcpy(k, proc->tk, sizeof proc->tk);

    ble_l2cap_sm_pair_cmd_write(
        preq, BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_CMD_SZ, 1,
        &proc->pair_req);

    ble_l2cap_sm_pair_cmd_write(
        pres, BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_CMD_SZ, 0,
        &proc->pair_rsp);

    return 0;
}

int
ble_sm_lgcy_confirm_go(struct ble_l2cap_sm_proc *proc)
{
    struct ble_l2cap_sm_pair_confirm cmd;
    uint8_t preq[BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_CMD_SZ];
    uint8_t pres[BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_CMD_SZ];
    uint8_t k[16];
    uint8_t ia[6];
    uint8_t ra[6];
    uint8_t iat;
    uint8_t rat;
    int rc;

    rc = ble_sm_lgcy_confirm_prepare_args(proc, k, preq, pres,
                                          &iat, &rat, ia, ra);
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_sm_alg_c1(k, ble_l2cap_sm_our_pair_rand(proc),
                             preq, pres, iat, rat, ia, ra, cmd.value);
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_sm_pair_confirm_tx(proc->conn_handle, &cmd);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_l2cap_sm_gen_stk(struct ble_l2cap_sm_proc *proc)
{
    uint8_t key[16];
    int rc;

    rc = ble_l2cap_sm_alg_s1(proc->tk, proc->rands, proc->randm, key);
    if (rc != 0) {
        return rc;
    }

    memcpy(proc->ltk, key, sizeof key);

    return 0;
}

int
ble_sm_lgcy_random_handle(struct ble_l2cap_sm_proc *proc,
                          struct ble_l2cap_sm_pair_random *cmd,
                          uint8_t *out_sm_status)
{
    uint8_t preq[BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_CMD_SZ];
    uint8_t pres[BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_CMD_SZ];
    uint8_t confirm_val[16];
    uint8_t k[16];
    uint8_t ia[6];
    uint8_t ra[6];
    uint8_t iat;
    uint8_t rat;
    int rc;

    rc = ble_sm_lgcy_confirm_prepare_args(proc, k, preq, pres,
                                          &iat, &rat, ia, ra);
    if (rc != 0) {
        *out_sm_status = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        return rc;
    }

    rc = ble_l2cap_sm_alg_c1(k, cmd->value, preq, pres, iat, rat,
                             ia, ra, confirm_val);
    if (rc != 0) {
        *out_sm_status = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        return rc;
    }

    if (memcmp(proc->confirm_their, confirm_val, 16) != 0) {
        /* Random number mismatch. */
        *out_sm_status = BLE_L2CAP_SM_ERR_CONFIRM_MISMATCH;
        return BLE_HS_SM_US_ERR(BLE_L2CAP_SM_ERR_CONFIRM_MISMATCH);
    }

    memcpy(ble_l2cap_sm_their_pair_rand(proc), cmd->value, 16);

    /* Generate the key. */
    rc = ble_l2cap_sm_gen_stk(proc);
    if (rc != 0) {
        *out_sm_status = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        return rc;
    }

    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        /* Send the start-encrypt HCI command to the controller.   For
         * short-term key generation, we always set ediv and rand to 0.
         * (Vol. 3, part H, 2.4.4.1).
         */
        proc->state = BLE_L2CAP_SM_PROC_STATE_ENC_CHANGE;
    }

    *out_sm_status = 0;
    return 0;
}
