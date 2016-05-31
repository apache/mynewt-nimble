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

#include "host/ble_sm.h"
#include "ble_hs_priv.h"
#include "ble_sm_priv.h"

/**
 * Create some shortened names for the passkey actions so that the table is
 * easier to read.
 */
#define PKACT_NONE      BLE_SM_PKACT_NONE
#define PKACT_OOB       BLE_SM_PKACT_OOB
#define PKACT_INPUT     BLE_SM_PKACT_INPUT
#define PKACT_DISP      BLE_SM_PKACT_DISP
#define PKACT_NUMCMP    BLE_SM_PKACT_NUMCMP

/* This is the initiator passkey action action dpeneding on the io
 * capabilties of both parties
 */
static const uint8_t ble_sm_lgcy_init_pka[5 /*resp*/ ][5 /*init */] =
{
    {PKACT_NONE,    PKACT_NONE,   PKACT_INPUT, PKACT_NONE, PKACT_INPUT},
    {PKACT_NONE,    PKACT_NUMCMP, PKACT_INPUT, PKACT_NONE, PKACT_INPUT},
    {PKACT_DISP,    PKACT_DISP,   PKACT_INPUT, PKACT_NONE, PKACT_DISP},
    {PKACT_NONE,    PKACT_NONE,   PKACT_NONE,  PKACT_NONE, PKACT_NONE},
    {PKACT_DISP,    PKACT_NUMCMP, PKACT_INPUT, PKACT_NONE, PKACT_NUMCMP},
};

/* This is the responder passkey action action depending on the io
 * capabilities of both parties
 */
static const uint8_t ble_sm_lgcy_resp_pka[5 /*init*/ ][5 /*resp */] =
{
    {PKACT_NONE,    PKACT_NONE,   PKACT_DISP,  PKACT_NONE, PKACT_DISP},
    {PKACT_NONE,    PKACT_NUMCMP, PKACT_DISP,  PKACT_NONE, PKACT_NUMCMP},
    {PKACT_INPUT,   PKACT_INPUT,  PKACT_INPUT, PKACT_NONE, PKACT_INPUT},
    {PKACT_NONE,    PKACT_NONE,   PKACT_NONE,  PKACT_NONE, PKACT_NONE},
    {PKACT_INPUT,   PKACT_NUMCMP, PKACT_DISP,  PKACT_NONE, PKACT_NUMCMP},
};

static uint8_t ble_sm_sc_pub_key[64];
static uint8_t ble_sm_sc_priv_key[32];
static uint8_t ble_sm_sc_keys_generated;

static int
ble_sm_sc_ensure_keys_generated(void)
{
    int rc;

    if (!ble_sm_sc_keys_generated) {
        rc = ble_sm_gen_pub_priv(ble_sm_sc_pub_key, ble_sm_sc_priv_key);
        if (rc != 0) {
            return rc;
        }

        ble_sm_sc_keys_generated = 1;
    }

    return 0;
}

static int
ble_sm_sc_initiator_txes_confirm(struct ble_sm_proc *proc)
{
    BLE_HS_DBG_ASSERT(proc->flags & BLE_SM_PROC_F_SC);

    /* Initiator does not send a confirm when pairing algorithm is any of:
     *     o just works
     *     o numeric comparison
     * (vol. 3, part H, 2.3.5.6.2)
     */
    return proc->pair_alg != BLE_SM_PAIR_ALG_JW &&
           proc->pair_alg != BLE_SM_PAIR_ALG_NUM_CMP;
}

static int
ble_sm_sc_responder_verifies_random(struct ble_sm_proc *proc)
{
    BLE_HS_DBG_ASSERT(proc->flags & BLE_SM_PROC_F_SC);

    /* Responder does not verify the initiator's random number when pairing
     * algorithm is any of:
     *     o just works
     *     o numeric comparison
     * (vol. 3, part H, 2.3.5.6.2)
     */
    return proc->pair_alg != BLE_SM_PAIR_ALG_JW &&
           proc->pair_alg != BLE_SM_PAIR_ALG_NUM_CMP;
}

int
ble_sm_sc_passkey_action(struct ble_sm_proc *proc)
{
    return 0;
}

void
ble_sm_sc_confirm_go(struct ble_sm_proc *proc, struct ble_sm_result *res)
{
    struct ble_sm_pair_confirm cmd;
    int rc;

    rc = ble_sm_alg_f4(ble_sm_sc_pub_key, proc->pub_key_their.x,
                       ble_sm_our_pair_rand(proc), 0, cmd.value);
    if (rc != 0) {
        res->app_status = rc;
        res->enc_cb = 1;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        return;
    }

    rc = ble_sm_pair_confirm_tx(proc->conn_handle, &cmd);
    if (rc != 0) {
        res->app_status = rc;
        res->enc_cb = 1;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        return;
    }

    if (!(proc->flags & BLE_SM_PROC_F_INITIATOR)) {
        proc->state = BLE_SM_PROC_STATE_RANDOM;
    }
}

void
ble_sm_sc_random_go(struct ble_sm_proc *proc,
                    struct ble_sm_result *res)
{
    struct ble_sm_pair_random cmd;
    int rc;

    memcpy(cmd.value, ble_sm_our_pair_rand(proc), 16);

    rc = ble_sm_pair_random_tx(proc->conn_handle, &cmd);
    if (rc != 0) {
        res->app_status = rc;
        res->enc_cb = 1;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        return;
    }

    if (!(proc->flags & BLE_SM_PROC_F_INITIATOR)) {
        proc->state = BLE_SM_PROC_STATE_DHKEY_CHECK;
    }
}

void
ble_sm_sc_rx_pair_random(struct ble_sm_proc *proc, struct ble_sm_result *res)
{
    uint8_t confirm_val[16];
    uint8_t *ia;
    uint8_t *ra;
    uint8_t iat;
    uint8_t rat;
    int rc;

    if (proc->flags & BLE_SM_PROC_F_INITIATOR ||
        ble_sm_sc_responder_verifies_random(proc)) {

        rc = ble_sm_alg_f4(ble_sm_sc_pub_key, proc->pub_key_their.x,
                           ble_sm_their_pair_rand(proc), 0, confirm_val);
        if (rc != 0) {
            res->app_status = rc;
            res->sm_err = BLE_SM_ERR_UNSPECIFIED;
            res->enc_cb = 1;
            return;
        }

        if (memcmp(proc->confirm_their, confirm_val, 16) != 0) {
            /* Random number mismatch. */
            res->app_status = BLE_HS_SM_US_ERR(BLE_SM_ERR_CONFIRM_MISMATCH);
            res->sm_err = BLE_SM_ERR_UNSPECIFIED;
            res->enc_cb = 1;
            return;
        }
    }

    /* Calculate the mac key and ltk. */
    rc = ble_sm_addrs(proc, &iat, &ia, &rat, &ra);
    if (rc != 0) {
        res->app_status = rc;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    rc = ble_sm_alg_f5(proc->dhkey, proc->randm, proc->rands,
                       iat, ia, rat, ra, proc->mackey, proc->ltk);
    if (rc != 0) {
        res->app_status = rc;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
        proc->state = BLE_SM_PROC_STATE_DHKEY_CHECK;
    }

    res->execute = 1;
}

void
ble_sm_sc_public_key_go(struct ble_sm_proc *proc,
                        struct ble_sm_result *res,
                        void *arg)
{
    struct ble_sm_public_key cmd;
    int initiator_txes;
    int is_initiator;

    res->app_status = ble_sm_sc_ensure_keys_generated();
    if (res->app_status != 0) {
        res->enc_cb = 1;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        return;
    }

    memcpy(cmd.x, ble_sm_sc_pub_key + 0, 32);
    memcpy(cmd.y, ble_sm_sc_pub_key + 32, 32);
    res->app_status = ble_sm_public_key_tx(proc->conn_handle, &cmd);
    if (res->app_status != 0) {
        res->enc_cb = 1;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        return;
    }

    initiator_txes = ble_sm_sc_initiator_txes_confirm(proc);
    is_initiator = proc->flags & BLE_SM_PROC_F_INITIATOR;
    if ((initiator_txes  && is_initiator) ||
        (!initiator_txes && !is_initiator)) {

        proc->state = BLE_SM_PROC_STATE_CONFIRM;
        res->execute = 1;
    }
}

void
ble_sm_sc_rx_public_key(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
                        struct ble_sm_result *res)
{
    struct ble_sm_public_key cmd;
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;
    int rc;

    res->app_status = ble_hs_misc_pullup_base(om, BLE_SM_PUBLIC_KEY_SZ);
    if (res->app_status != 0) {
        res->enc_cb = 1;
        return;
    }

    res->app_status = ble_sm_sc_ensure_keys_generated();
    if (res->app_status != 0) {
        res->enc_cb = 1;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        return;
    }

    ble_sm_public_key_parse((*om)->om_data, (*om)->om_len, &cmd);

    BLE_HS_LOG(DEBUG, "rxed sm public key cmd\n");

    ble_hs_lock();
    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_PUBLIC_KEY, -1,
                            &prev);
    if (proc == NULL) {
        res->app_status = BLE_HS_ENOENT;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
    } else {
        proc->pub_key_their = cmd;
        rc = ble_sm_alg_gen_dhkey(proc->pub_key_their.x,
                                  proc->pub_key_their.y,
                                  ble_sm_sc_priv_key, proc->dhkey);
        if (rc != 0) {
            res->app_status = BLE_HS_SM_US_ERR(BLE_SM_ERR_DHKEY);
            res->sm_err = BLE_SM_ERR_DHKEY;
            res->enc_cb = 1;
        } else {
            if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
                proc->state = BLE_SM_PROC_STATE_CONFIRM;

                if (ble_sm_sc_initiator_txes_confirm(proc)) {
                    res->execute = 1;
                }
            } else {
                res->execute = 1;
            }
        }
    }
    ble_hs_unlock();
}

void
ble_sm_sc_dhkey_check_go(struct ble_sm_proc *proc, struct ble_sm_result *res,
                         void *arg)
{
    struct ble_sm_dhkey_check cmd;
    uint8_t iocap[3];
    uint8_t *peer_addr;
    uint8_t peer_addr_type;
    int rc;

    uint8_t zeros[16] = { 0 };

    if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
        iocap[0] = proc->pair_req.io_cap;
        iocap[1] = proc->pair_req.oob_data_flag;
        iocap[2] = proc->pair_req.authreq;
    } else {
        iocap[0] = proc->pair_rsp.io_cap;
        iocap[1] = proc->pair_rsp.oob_data_flag;
        iocap[2] = proc->pair_rsp.authreq;
    }

    rc = ble_sm_peer_addr(proc, &peer_addr_type, &peer_addr);
    if (rc != 0) {
        goto err;
    }

    rc = ble_sm_alg_f6(proc->mackey, ble_sm_our_pair_rand(proc),
                       ble_sm_their_pair_rand(proc), zeros, iocap,
                       0, ble_hs_our_dev.public_addr,
                       peer_addr_type, peer_addr,
                       cmd.value);
    if (rc != 0) {
        goto err;
    }

    rc = ble_sm_dhkey_check_tx(proc->conn_handle, &cmd);
    if (rc != 0) {
        goto err;
    }

    if (!(proc->flags & BLE_SM_PROC_F_INITIATOR)) {
        proc->state = BLE_SM_PROC_STATE_LTK_START;
    }

    return;

err:
    res->app_status = rc;
    res->enc_cb = 1;
    res->sm_err = BLE_SM_ERR_UNSPECIFIED;
}

static void
ble_sm_dhkey_check_handle(struct ble_sm_proc *proc,
                          struct ble_sm_dhkey_check *cmd,
                          struct ble_sm_result *res)
{
    uint8_t exp_value[16];
    uint8_t iocap[3];
    uint8_t *peer_addr;
    uint8_t peer_addr_type;

    uint8_t zeros[16] = { 0 };

    if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
        iocap[0] = proc->pair_rsp.io_cap;
        iocap[1] = proc->pair_rsp.oob_data_flag;
        iocap[2] = proc->pair_rsp.authreq;
    } else {
        iocap[0] = proc->pair_req.io_cap;
        iocap[1] = proc->pair_req.oob_data_flag;
        iocap[2] = proc->pair_req.authreq;
    }

    res->app_status = ble_sm_peer_addr(proc, &peer_addr_type, &peer_addr);
    if (res->app_status != 0) {
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    res->app_status = ble_sm_alg_f6(proc->mackey,
                                    ble_sm_their_pair_rand(proc),
                                    ble_sm_our_pair_rand(proc), zeros, iocap,
                                    peer_addr_type, peer_addr,
                                    0, ble_hs_our_dev.public_addr,
                                    exp_value);
    if (res->app_status != 0) {
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    if (memcmp(cmd->value, exp_value, 16) != 0) {
        /* Random number mismatch. */
        res->sm_err = BLE_SM_ERR_DHKEY;
        res->app_status = BLE_HS_SM_US_ERR(BLE_SM_ERR_DHKEY);
        res->enc_cb = 1;
        return;
    }

    if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
        proc->state = BLE_SM_PROC_STATE_ENC_START;
    }

    res->execute = 1;
}

void
ble_sm_sc_rx_dhkey_check(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
                         struct ble_sm_result *res)
{
    struct ble_sm_dhkey_check cmd;
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;

    res->app_status = ble_hs_misc_pullup_base(om, BLE_SM_DHKEY_CHECK_SZ);
    if (res->app_status != 0) {
        res->enc_cb = 1;
        return;
    }

    res->app_status = ble_sm_dhkey_check_parse((*om)->om_data, (*om)->om_len,
                                               &cmd);
    if (res->app_status != 0) {
        res->enc_cb = 1;
        return;
    }

    BLE_HS_LOG(DEBUG, "rxed sm dhkey check cmd\n");

    ble_hs_lock();
    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_DHKEY_CHECK, -1,
                            &prev);
    if (proc == NULL) {
        res->app_status = BLE_HS_ENOENT;
    } else {
        ble_sm_dhkey_check_handle(proc, &cmd, res);
    }
    ble_hs_unlock();
}

void
ble_sm_sc_init(void)
{
    ble_sm_sc_keys_generated = 0;
}
