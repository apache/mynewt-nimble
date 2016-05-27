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

#include "ble_hs_priv.h"
#include "ble_l2cap_sm_priv.h"

static int
ble_sm_sc_initiator_txes_confirm(struct ble_l2cap_sm_proc *proc)
{
    BLE_HS_DBG_ASSERT(proc->flags & BLE_L2CAP_SM_PROC_F_SC);

    /* Initiator does not send a confirm when pairing algorithm is any of:
     *     o just works
     *     o numeric comparison
     * (vol. 3, part H, 2.3.5.6.2)
     */
    return proc->pair_alg != BLE_L2CAP_SM_PAIR_ALG_JW &&
           proc->pair_alg != BLE_L2CAP_SM_PAIR_ALG_NUM_CMP;
}

static int
ble_sm_sc_responder_verifies_random(struct ble_l2cap_sm_proc *proc)
{
    BLE_HS_DBG_ASSERT(proc->flags & BLE_L2CAP_SM_PROC_F_SC);

    /* Responder does not verify the initiator's random number when pairing
     * algorithm is any of:
     *     o just works
     *     o numeric comparison
     * (vol. 3, part H, 2.3.5.6.2)
     */
    return proc->pair_alg != BLE_L2CAP_SM_PAIR_ALG_JW &&
           proc->pair_alg != BLE_L2CAP_SM_PAIR_ALG_NUM_CMP;
}

int
ble_sm_sc_passkey_action(struct ble_l2cap_sm_proc *proc)
{
    return 0;
}

void
ble_sm_sc_confirm_go(struct ble_l2cap_sm_proc *proc,
                     struct ble_l2cap_sm_result *res)
{
    struct ble_l2cap_sm_pair_confirm cmd;
    int rc;

    rc = ble_l2cap_sm_alg_f4(proc->pub_key_our.x, proc->pub_key_their.x,
                             ble_l2cap_sm_our_pair_rand(proc), 0,
                             cmd.value);
    if (rc != 0) {
        res->app_status = rc;
        res->do_cb = 1;
        res->sm_err = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        return;
    }

    rc = ble_l2cap_sm_pair_confirm_tx(proc->conn_handle, &cmd);
    if (rc != 0) {
        res->app_status = rc;
        res->do_cb = 1;
        res->sm_err = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        return;
    }

    if (!(proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR)) {
        proc->state = BLE_L2CAP_SM_PROC_STATE_RANDOM;
    }
}

static void
ble_sm_sc_random_verify(struct ble_l2cap_sm_proc *proc,
                        struct ble_l2cap_sm_result *res)
{
    uint8_t confirm_val[16];
    int rc;

    rc = ble_l2cap_sm_alg_f4(proc->pub_key_our.x,
                             proc->pub_key_their.x,
                             ble_l2cap_sm_their_pair_rand(proc), 0,
                             confirm_val);
    if (rc != 0) {
        res->app_status = rc;
        res->sm_err = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        return;
    }

    if (memcmp(proc->confirm_their, confirm_val, 16) != 0) {
        /* Random number mismatch. */
        res->app_status = BLE_HS_SM_US_ERR(BLE_L2CAP_SM_ERR_CONFIRM_MISMATCH);
        res->sm_err = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        return;
    }
}

void
ble_sm_sc_random_go(struct ble_l2cap_sm_proc *proc,
                    struct ble_l2cap_sm_result *res)
{
    struct ble_l2cap_sm_pair_random cmd;
    int rc;

    memcpy(cmd.value, ble_l2cap_sm_our_pair_rand(proc), 16);

    rc = ble_l2cap_sm_pair_random_tx(proc->conn_handle, &cmd);
    if (rc != 0) {
        res->app_status = rc;
        res->do_cb = 1;
        res->sm_err = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        return;
    }

    if (!(proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR)) {
        proc->state = BLE_L2CAP_SM_PROC_STATE_DHKEY_CHECK;
    }
}

void
ble_sm_sc_random_handle(struct ble_l2cap_sm_proc *proc,
                        struct ble_l2cap_sm_result *res)
{
    int rc;

    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR ||
        ble_sm_sc_responder_verifies_random(proc)) {

        ble_sm_sc_random_verify(proc, res);
        if (res->app_status != 0) {
            return;
        }
    }

    /* Calculate the mac key and ltk. */
    rc = ble_l2cap_sm_alg_f5(NULL, NULL, NULL, 0, NULL, 0, NULL,
                             proc->mackey, proc->ltk);
    if (rc != 0) {
        res->app_status = rc;
        res->sm_err = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        res->do_cb = 1;
        return;
    }

    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        proc->state = BLE_L2CAP_SM_PROC_STATE_DHKEY_CHECK;
    }

    ble_l2cap_sm_go(proc, res, NULL);
}

void
ble_sm_sc_public_key_go(struct ble_l2cap_sm_proc *proc,
                        struct ble_l2cap_sm_result *res,
                        void *arg)
{
    int initiator_txes;
    int is_initiator;
    int rc;

    rc = ble_l2cap_sm_gen_pub_priv(proc, proc->pub_key_our.x,
                                   proc->priv_key_our);
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_sm_public_key_tx(proc->conn_handle, &proc->pub_key_our);
    if (rc != 0) {
        goto err;
    }

    initiator_txes = ble_sm_sc_initiator_txes_confirm(proc);
    is_initiator = proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR;
    if ((initiator_txes  && is_initiator) ||
        (!initiator_txes && !is_initiator)) {

        proc->state = BLE_L2CAP_SM_PROC_STATE_CONFIRM;
        res->do_tx = 1;
    }

    return;

err:
    res->app_status = rc;
    res->do_cb = 1;
    res->sm_err = BLE_L2CAP_SM_ERR_UNSPECIFIED;
}

void
ble_sm_sc_public_key_handle(struct ble_l2cap_sm_proc *proc,
                            struct ble_l2cap_sm_public_key *cmd,
                            struct ble_l2cap_sm_result *res)
{
    proc->pub_key_their = *cmd;

    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        proc->state = BLE_L2CAP_SM_PROC_STATE_CONFIRM;

        if (ble_sm_sc_initiator_txes_confirm(proc)) {
            ble_l2cap_sm_go(proc, res, NULL);
        }
    } else {
        ble_l2cap_sm_go(proc, res, NULL);
    }
}

int
ble_sm_sc_rx_public_key(uint16_t conn_handle, uint8_t op, struct os_mbuf **om)
{
    struct ble_l2cap_sm_public_key cmd;
    struct ble_l2cap_sm_result res;
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;
    int rc;

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SM_PUBLIC_KEY_SZ);
    if (rc != 0) {
        return rc;
    }

    ble_l2cap_sm_public_key_parse((*om)->om_data, (*om)->om_len, &cmd);

    BLE_HS_LOG(DEBUG, "rxed sm public key cmd\n");

    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(conn_handle,
                                  BLE_L2CAP_SM_PROC_STATE_PUBLIC_KEY,
                                  -1, &prev);
    if (proc != NULL) {
        memset(&res, 0, sizeof res);
        ble_sm_sc_public_key_handle(proc, &cmd, &res);
    }
    ble_hs_unlock();

    if (proc == NULL) {
        return BLE_HS_ENOENT;
    }

    ble_l2cap_sm_process_result(conn_handle, &res);

    return rc;
}

/*****************************************************************************
 * $dhkey check                                                              *
 *****************************************************************************/

void
ble_sm_sc_dhkey_check_go(struct ble_l2cap_sm_proc *proc,
                            struct ble_l2cap_sm_result *res, void *arg)
{
    struct ble_l2cap_sm_dhkey_check cmd;
    int rc;

    rc = ble_l2cap_sm_alg_f6(NULL, NULL, NULL, NULL, NULL, 0, NULL, 0, NULL,
                             cmd.value);
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_sm_dhkey_check_tx(proc->conn_handle, &cmd);
    if (rc != 0) {
        goto err;
    }

    if (!(proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR)) {
        proc->state = BLE_L2CAP_SM_PROC_STATE_LTK_START;
    }

    return;

err:
    res->app_status = rc;
    res->do_cb = 1;
    res->sm_err = BLE_L2CAP_SM_ERR_UNSPECIFIED;
}

static void
ble_l2cap_sm_dhkey_check_handle(struct ble_l2cap_sm_proc *proc,
                                struct ble_l2cap_sm_dhkey_check *cmd,
                                struct ble_l2cap_sm_result *res)
{
    uint8_t exp_value[16];

    res->app_status = ble_l2cap_sm_alg_f6(NULL, NULL, NULL, NULL, NULL, 0,
                                          NULL, 0, NULL, exp_value);
    if (res->app_status != 0) {
        return;
    }
    if (memcmp(cmd->value, exp_value, 16) != 0) {
        /* Random number mismatch. */
        res->sm_err = BLE_L2CAP_SM_ERR_DHKEY;
        res->app_status = BLE_HS_SM_US_ERR(BLE_L2CAP_SM_ERR_DHKEY);
        return;
    }

    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        proc->state = BLE_L2CAP_SM_PROC_STATE_ENC_START;
    }

    ble_l2cap_sm_go(proc, res, NULL);
}

int
ble_sm_sc_rx_dhkey_check(uint16_t conn_handle, uint8_t op, struct os_mbuf **om)
{
    struct ble_l2cap_sm_dhkey_check cmd;
    struct ble_l2cap_sm_result res;
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;
    int rc;

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SM_DHKEY_CHECK_SZ);
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_sm_dhkey_check_parse((*om)->om_data, (*om)->om_len, &cmd);
    if (rc != 0) {
        return rc;
    }

    BLE_HS_LOG(DEBUG, "rxed sm dhkey check cmd\n");

    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(conn_handle,
                                  BLE_L2CAP_SM_PROC_STATE_DHKEY_CHECK,
                                  -1, &prev);
    if (proc != NULL) {
        memset(&res, 0, sizeof res);
        ble_l2cap_sm_dhkey_check_handle(proc, &cmd, &res);
    }
    ble_hs_unlock();

    if (proc == NULL) {
        return BLE_HS_ENOENT;
    }

    ble_l2cap_sm_process_result(conn_handle, &res);

    return res.app_status;
}
