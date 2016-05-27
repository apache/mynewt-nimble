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

/**
 * L2CAP Security Manager (channel ID = 6).
 *
 * Design overview:
 *
 * L2CAP sm procedures are initiated by the application via function calls.
 * Such functions return when either of the following happens:
 *
 * (1) The procedure completes (success or failure).
 * (2) The procedure cannot proceed until a BLE peer responds.
 *
 * For (1), the result of the procedure if fully indicated by the function
 * return code.
 * For (2), the procedure result is indicated by an application-configured
 * callback.  The callback is executed when the procedure completes.
 *
 * Notes on thread-safety:
 * 1. The ble_hs mutex must never be locked when an application callback is
 *    executed.  A callback is free to initiate additional host procedures.
 * 2. Keep the host mutex locked whenever:
 *      o A proc entry is read from or written to.
 *      o The proc list is read or modified.
 */

#include <string.h>
#include <errno.h>
#include "console/console.h"
#include "nimble/ble.h"
#include "nimble/nimble_opt.h"
#include "ble_hs_priv.h"

#if NIMBLE_OPT(SM)

/** Procedure timeout; 30 seconds. */
#define BLE_L2CAP_SM_TIMEOUT_OS_TICKS           (30 * OS_TICKS_PER_SEC)

STAILQ_HEAD(ble_l2cap_sm_proc_list, ble_l2cap_sm_proc);

typedef int ble_l2cap_sm_rx_fn(uint16_t conn_handle, uint8_t op,
                               struct os_mbuf **om);

static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_noop;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_pair_req;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_pair_rsp;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_pair_confirm;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_pair_random;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_pair_fail;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_key_exchange;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_sec_req;

static ble_l2cap_sm_rx_fn * const ble_l2cap_sm_dispatch[] = {
   [BLE_L2CAP_SM_OP_PAIR_REQ] = ble_l2cap_sm_rx_pair_req,
   [BLE_L2CAP_SM_OP_PAIR_RSP] = ble_l2cap_sm_rx_pair_rsp,
   [BLE_L2CAP_SM_OP_PAIR_CONFIRM] = ble_l2cap_sm_rx_pair_confirm,
   [BLE_L2CAP_SM_OP_PAIR_RANDOM] = ble_l2cap_sm_rx_pair_random,
   [BLE_L2CAP_SM_OP_PAIR_FAIL] = ble_l2cap_sm_rx_pair_fail,
   [BLE_L2CAP_SM_OP_ENC_INFO] = ble_l2cap_sm_rx_key_exchange,
   [BLE_L2CAP_SM_OP_MASTER_ID] = ble_l2cap_sm_rx_key_exchange,
   [BLE_L2CAP_SM_OP_IDENTITY_INFO] = ble_l2cap_sm_rx_key_exchange,
   [BLE_L2CAP_SM_OP_IDENTITY_ADDR_INFO] = ble_l2cap_sm_rx_key_exchange,
   [BLE_L2CAP_SM_OP_SIGN_INFO] = ble_l2cap_sm_rx_key_exchange,
   [BLE_L2CAP_SM_OP_SEC_REQ] = ble_l2cap_sm_rx_sec_req,
   [BLE_L2CAP_SM_OP_PAIR_KEYPRESS_NOTIFY] = ble_l2cap_sm_rx_noop,
#if NIMBLE_OPT_SM_SC
   [BLE_L2CAP_SM_OP_PAIR_PUBLIC_KEY] = ble_sm_sc_rx_public_key,
   [BLE_L2CAP_SM_OP_PAIR_DHKEY_CHECK] = ble_sm_sc_rx_dhkey_check,
#else
   [BLE_L2CAP_SM_OP_PAIR_PUBLIC_KEY] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_PAIR_DHKEY_CHECK] = ble_l2cap_sm_rx_noop,
#endif
};

typedef void ble_l2cap_sm_state_fn(struct ble_l2cap_sm_proc *proc,
                                   struct ble_l2cap_sm_result *res,
                                   void *arg);

static ble_l2cap_sm_state_fn ble_l2cap_sm_pair_go;
static ble_l2cap_sm_state_fn ble_l2cap_sm_confirm_go;
static ble_l2cap_sm_state_fn ble_l2cap_sm_random_go;
static ble_l2cap_sm_state_fn ble_l2cap_sm_ltk_start_go;
static ble_l2cap_sm_state_fn ble_l2cap_sm_ltk_restore_go;
static ble_l2cap_sm_state_fn ble_l2cap_sm_enc_start_go;
static ble_l2cap_sm_state_fn ble_l2cap_sm_enc_restore_go;
static ble_l2cap_sm_state_fn ble_l2cap_sm_key_exch_go;
static ble_l2cap_sm_state_fn ble_l2cap_sm_sec_req_go;

static ble_l2cap_sm_state_fn * const
ble_l2cap_sm_state_dispatch[BLE_L2CAP_SM_PROC_STATE_CNT] = {
    [BLE_L2CAP_SM_PROC_STATE_PAIR]          = ble_l2cap_sm_pair_go,
    [BLE_L2CAP_SM_PROC_STATE_CONFIRM]       = ble_l2cap_sm_confirm_go,
    [BLE_L2CAP_SM_PROC_STATE_RANDOM]        = ble_l2cap_sm_random_go,
    [BLE_L2CAP_SM_PROC_STATE_LTK_START]     = ble_l2cap_sm_ltk_start_go,
    [BLE_L2CAP_SM_PROC_STATE_LTK_RESTORE]   = ble_l2cap_sm_ltk_restore_go,
    [BLE_L2CAP_SM_PROC_STATE_ENC_START]     = ble_l2cap_sm_enc_start_go,
    [BLE_L2CAP_SM_PROC_STATE_ENC_RESTORE]   = ble_l2cap_sm_enc_restore_go,
    [BLE_L2CAP_SM_PROC_STATE_KEY_EXCH]      = ble_l2cap_sm_key_exch_go,
    [BLE_L2CAP_SM_PROC_STATE_SEC_REQ]       = ble_l2cap_sm_sec_req_go,
#if NIMBLE_OPT_SM_SC
    [BLE_L2CAP_SM_PROC_STATE_PUBLIC_KEY]    = ble_sm_sc_public_key_go,
    [BLE_L2CAP_SM_PROC_STATE_DHKEY_CHECK]   = ble_sm_sc_dhkey_check_go,
#else
    [BLE_L2CAP_SM_PROC_STATE_PUBLIC_KEY]    = NULL,
    [BLE_L2CAP_SM_PROC_STATE_DHKEY_CHECK]   = NULL,
#endif
};

static void *ble_l2cap_sm_proc_mem;
static struct os_mempool ble_l2cap_sm_proc_pool;

static void *ble_l2cap_sm_key_mem;
static struct os_mempool ble_l2cap_sm_key_pool;

/* Maintains the list of active security manager procedures. */
static struct ble_l2cap_sm_proc_list ble_l2cap_sm_procs;

static void ble_l2cap_sm_check_key_exchange(struct ble_l2cap_sm_proc *proc);


/*****************************************************************************
 * $debug                                                                    *
 *****************************************************************************/

#ifdef BLE_HS_DEBUG

static uint8_t ble_l2cap_sm_dbg_next_pair_rand[16];
static uint8_t ble_l2cap_sm_dbg_next_pair_rand_set;
static uint16_t ble_l2cap_sm_dbg_next_ediv;
static uint8_t ble_l2cap_sm_dbg_next_ediv_set;
static uint64_t ble_l2cap_sm_dbg_next_start_rand;
static uint8_t ble_l2cap_sm_dbg_next_start_rand_set;
static uint8_t ble_l2cap_sm_dbg_next_ltk[16];
static uint8_t ble_l2cap_sm_dbg_next_ltk_set;
static uint8_t ble_l2cap_sm_dbg_next_irk[16];
static uint8_t ble_l2cap_sm_dbg_next_irk_set;
static uint8_t ble_l2cap_sm_dbg_next_csrk[16];
static uint8_t ble_l2cap_sm_dbg_next_csrk_set;
static uint8_t ble_l2cap_sm_dbg_sc_pub_key[64];
static uint8_t ble_l2cap_sm_dbg_sc_priv_key[32];
static uint8_t ble_l2cap_sm_dbg_sc_keys_set;

void
ble_l2cap_sm_dbg_set_next_pair_rand(uint8_t *next_pair_rand)
{
    memcpy(ble_l2cap_sm_dbg_next_pair_rand, next_pair_rand,
           sizeof ble_l2cap_sm_dbg_next_pair_rand);
    ble_l2cap_sm_dbg_next_pair_rand_set = 1;
}

void
ble_l2cap_sm_dbg_set_next_ediv(uint16_t next_ediv)
{
    ble_l2cap_sm_dbg_next_ediv = next_ediv;
    ble_l2cap_sm_dbg_next_ediv_set = 1;
}

void
ble_l2cap_sm_dbg_set_next_start_rand(uint64_t next_start_rand)
{
    ble_l2cap_sm_dbg_next_start_rand = next_start_rand;
    ble_l2cap_sm_dbg_next_start_rand_set = 1;
}

void
ble_l2cap_sm_dbg_set_next_ltk(uint8_t *next_ltk)
{
    memcpy(ble_l2cap_sm_dbg_next_ltk, next_ltk,
           sizeof ble_l2cap_sm_dbg_next_ltk);
    ble_l2cap_sm_dbg_next_ltk_set = 1;
}

void
ble_l2cap_sm_dbg_set_next_irk(uint8_t *next_irk)
{
    memcpy(ble_l2cap_sm_dbg_next_irk, next_irk,
           sizeof ble_l2cap_sm_dbg_next_irk);
    ble_l2cap_sm_dbg_next_irk_set = 1;
}

void
ble_l2cap_sm_dbg_set_next_csrk(uint8_t *next_csrk)
{
    memcpy(ble_l2cap_sm_dbg_next_csrk, next_csrk,
           sizeof ble_l2cap_sm_dbg_next_csrk);
    ble_l2cap_sm_dbg_next_csrk_set = 1;
}

void
ble_l2cap_sm_dbg_set_sc_keys(uint8_t *pubkey, uint8_t *privkey)
{
    memcpy(ble_l2cap_sm_dbg_sc_pub_key, pubkey,
           sizeof ble_l2cap_sm_dbg_sc_pub_key);
    memcpy(ble_l2cap_sm_dbg_sc_priv_key, privkey,
           sizeof ble_l2cap_sm_dbg_sc_priv_key);
    ble_l2cap_sm_dbg_sc_keys_set = 1;
}

int
ble_l2cap_sm_dbg_num_procs(void)
{
    struct ble_l2cap_sm_proc *proc;
    int cnt;

    cnt = 0;
    STAILQ_FOREACH(proc, &ble_l2cap_sm_procs, next) {
        BLE_HS_DBG_ASSERT(cnt < ble_hs_cfg.max_l2cap_sm_procs);
        cnt++;
    }

    return cnt;
}

#endif

static void
ble_l2cap_sm_dbg_assert_no_cycles(void)
{
#if BLE_HS_DEBUG
    ble_l2cap_sm_dbg_num_procs();
#endif
}

static void
ble_l2cap_sm_dbg_assert_not_inserted(struct ble_l2cap_sm_proc *proc)
{
#if BLE_HS_DEBUG
    struct ble_l2cap_sm_proc *cur;

    STAILQ_FOREACH(cur, &ble_l2cap_sm_procs, next) {
        BLE_HS_DBG_ASSERT(cur != proc);
    }
#endif
}

/*****************************************************************************
 * $misc                                                                     *
 *****************************************************************************/

static int
ble_l2cap_sm_gen_pair_rand(uint8_t *pair_rand)
{
    int rc;

#ifdef BLE_HS_DEBUG
    if (ble_l2cap_sm_dbg_next_pair_rand_set) {
        ble_l2cap_sm_dbg_next_pair_rand_set = 0;
        memcpy(pair_rand, ble_l2cap_sm_dbg_next_pair_rand,
               sizeof ble_l2cap_sm_dbg_next_pair_rand);
        return 0;
    }
#endif

    rc = ble_hci_util_rand(pair_rand, 16);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_l2cap_sm_gen_ediv(uint16_t *ediv)
{
    int rc;

#ifdef BLE_HS_DEBUG
    if (ble_l2cap_sm_dbg_next_ediv_set) {
        ble_l2cap_sm_dbg_next_ediv_set = 0;
        *ediv = ble_l2cap_sm_dbg_next_ediv;
        return 0;
    }
#endif

    rc = ble_hci_util_rand(ediv, sizeof *ediv);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_l2cap_sm_gen_start_rand(uint64_t *start_rand)
{
    int rc;

#ifdef BLE_HS_DEBUG
    if (ble_l2cap_sm_dbg_next_start_rand_set) {
        ble_l2cap_sm_dbg_next_start_rand_set = 0;
        *start_rand = ble_l2cap_sm_dbg_next_start_rand;
        return 0;
    }
#endif

    rc = ble_hci_util_rand(start_rand, sizeof *start_rand);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_l2cap_sm_gen_ltk(struct ble_l2cap_sm_proc *proc, uint8_t *ltk)
{
    int rc;

#ifdef BLE_HS_DEBUG
    if (ble_l2cap_sm_dbg_next_ltk_set) {
        ble_l2cap_sm_dbg_next_ltk_set = 0;
        memcpy(ltk, ble_l2cap_sm_dbg_next_ltk,
               sizeof ble_l2cap_sm_dbg_next_ltk);
        return 0;
    }
#endif

    rc = ble_hci_util_rand(ltk, 16);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_l2cap_sm_gen_csrk(struct ble_l2cap_sm_proc *proc, uint8_t *csrk)
{
    int rc;

#ifdef BLE_HS_DEBUG
    if (ble_l2cap_sm_dbg_next_csrk_set) {
        ble_l2cap_sm_dbg_next_csrk_set = 0;
        memcpy(csrk, ble_l2cap_sm_dbg_next_csrk,
               sizeof ble_l2cap_sm_dbg_next_csrk);
        return 0;
    }
#endif

    rc = ble_hci_util_rand(csrk, 16);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_l2cap_sm_gen_pub_priv(struct ble_l2cap_sm_proc *proc,
                          uint8_t *pub, uint8_t *priv)
{
    int rc;

#ifdef BLE_HS_DEBUG
    if (ble_l2cap_sm_dbg_sc_keys_set) {
        ble_l2cap_sm_dbg_sc_keys_set = 0;
        memcpy(pub, ble_l2cap_sm_dbg_sc_pub_key,
               sizeof ble_l2cap_sm_dbg_sc_pub_key);
        memcpy(priv, ble_l2cap_sm_dbg_sc_priv_key,
               sizeof ble_l2cap_sm_dbg_sc_priv_key);
        return 0;
    }
#endif

    rc = ble_l2cap_sm_alg_gen_key_pair(pub, priv);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
ble_l2cap_sm_proc_set_timer(struct ble_l2cap_sm_proc *proc)
{
    /* Set a timeout of 30 seconds. */
    proc->exp_os_ticks = os_time_get() + BLE_L2CAP_SM_TIMEOUT_OS_TICKS;
}

static ble_l2cap_sm_rx_fn *
ble_l2cap_sm_dispatch_get(uint8_t op)
{
    if (op > sizeof ble_l2cap_sm_dispatch / sizeof ble_l2cap_sm_dispatch[0]) {
        return NULL;
    }

    return ble_l2cap_sm_dispatch[op];
}

/**
 * Allocates a key entry.
 *
 * @return                      An entry on success; null on failure.
 */
static struct ble_l2cap_sm_keys *
ble_l2cap_sm_keys_alloc(void)
{
    struct ble_l2cap_sm_keys *keys;

    keys = os_memblock_get(&ble_l2cap_sm_key_pool);
    if (keys != NULL) {
        memset(keys, 0, sizeof *keys);
    }

    return keys;
}

/**
 * Frees the specified keys entry.  No-state if passed a null pointer.
 */
static void
ble_l2cap_sm_keys_free(struct ble_l2cap_sm_keys *keys)
{
    int rc;

    if (keys != NULL) {
        rc = os_memblock_put(&ble_l2cap_sm_key_pool, keys);
        BLE_HS_DBG_ASSERT_EVAL(rc == 0);
    }
}

/**
 * Allocates a proc entry.
 *
 * @return                      An entry on success; null on failure.
 */
static struct ble_l2cap_sm_proc *
ble_l2cap_sm_proc_alloc(void)
{
    struct ble_l2cap_sm_proc *proc;

    proc = os_memblock_get(&ble_l2cap_sm_proc_pool);
    if (proc != NULL) {
        memset(proc, 0, sizeof *proc);
    }

    return proc;
}

/**
 * Frees the specified proc entry.  No-state if passed a null pointer.
 */
static void
ble_l2cap_sm_proc_free(struct ble_l2cap_sm_proc *proc)
{
    int rc;

    if (proc != NULL) {
        ble_l2cap_sm_dbg_assert_not_inserted(proc);

        ble_l2cap_sm_keys_free(proc->our_keys);
        ble_l2cap_sm_keys_free(proc->peer_keys);

        rc = os_memblock_put(&ble_l2cap_sm_proc_pool, proc);
        BLE_HS_DBG_ASSERT_EVAL(rc == 0);
    }
}

static void
ble_l2cap_sm_proc_remove(struct ble_l2cap_sm_proc *proc,
                         struct ble_l2cap_sm_proc *prev)
{
    if (prev == NULL) {
        BLE_HS_DBG_ASSERT(STAILQ_FIRST(&ble_l2cap_sm_procs) == proc);
        STAILQ_REMOVE_HEAD(&ble_l2cap_sm_procs, next);
    } else {
        BLE_HS_DBG_ASSERT(STAILQ_NEXT(prev, next) == proc);
        STAILQ_REMOVE_AFTER(&ble_l2cap_sm_procs, prev, next);
    }

    ble_l2cap_sm_dbg_assert_no_cycles();
}

static void
ble_l2cap_sm_sec_state(struct ble_l2cap_sm_proc *proc,
                       struct ble_gap_sec_state *out_sec_state,
                       int enc_enabled)
{
    out_sec_state->pair_alg = proc->pair_alg;
    out_sec_state->enc_enabled = enc_enabled;
    out_sec_state->authenticated =
            (proc->flags & BLE_L2CAP_SM_PROC_F_AUTHENTICATED) ? 1 : 0;
    out_sec_state->bonded = (proc->flags & BLE_L2CAP_SM_PROC_F_BONDED) ? 1 : 0;
}

static void
ble_l2cap_sm_fill_store_value(uint8_t peer_addr_type, uint8_t *peer_addr,
                              int authenticated,
                              struct ble_l2cap_sm_keys *keys,
                              struct ble_store_value_sec *value_sec)
{
    memset(value_sec, 0, sizeof *value_sec);

    if (keys->ediv_rand_valid && keys->ltk_valid) {
        value_sec->peer_addr_type = peer_addr_type;
        memcpy(value_sec->peer_addr, peer_addr, sizeof value_sec->peer_addr);
        value_sec->ediv = keys->ediv;
        value_sec->rand_num = keys->rand_val;

        memcpy(value_sec->ltk, keys->ltk, sizeof value_sec->ltk);
        value_sec->ltk_present = 1;

        value_sec->authenticated = authenticated;
        value_sec->sc = 0;
    }

    if (keys->irk_valid) {
        memcpy(value_sec->irk, keys->irk, sizeof value_sec->irk);
        value_sec->irk_present = 1;
    }

    if (keys->csrk_valid) {
        memcpy(value_sec->csrk, keys->csrk, sizeof value_sec->csrk);
        value_sec->csrk_present = 1;
    }
}

static void
ble_l2cap_sm_key_exchange_events(uint16_t conn_handle,
                                 struct ble_l2cap_sm_keys *our_keys,
                                 struct ble_l2cap_sm_keys *peer_keys,
                                 int authenticated)
{
    struct ble_store_value_sec value_sec;
    struct ble_hs_conn *conn;
    uint8_t peer_addr[8];
    uint8_t peer_addr_type;

    ble_hs_lock();

    conn = ble_hs_conn_find(conn_handle);
    BLE_HS_DBG_ASSERT(conn != NULL);

    /* if we got an identity address, use that for key storage */
    if(proc->peer_keys.addr_valid) {
        peer_addr_type = proc->peer_keys.addr_type;
        memcpy(peer_addr, proc->peer_keys.addr, sizeof peer_addr);
    } else {
        peer_addr_type = conn->bhc_addr_type;
        memcpy(peer_addr, conn->bhc_addr, sizeof peer_addr);
    }
    ble_hs_unlock();

    ble_l2cap_sm_fill_store_value(peer_addr_type, peer_addr, authenticated,
                                  our_keys, &value_sec);
    ble_store_write_slv_sec(&value_sec);

    ble_l2cap_sm_fill_store_value(peer_addr_type, peer_addr, authenticated,
                                  peer_keys, &value_sec);
    ble_store_write_mst_sec(&value_sec);
}

static void
ble_l2cap_sm_gap_event(struct ble_l2cap_sm_proc *proc, int status,
                       int enc_enabled)
{
    struct ble_gap_sec_state sec_state;

    ble_l2cap_sm_sec_state(proc, &sec_state, enc_enabled);
    ble_gap_enc_changed(proc->conn_handle, status, &sec_state);
}

static int
ble_l2cap_sm_proc_matches(struct ble_l2cap_sm_proc *proc, uint16_t conn_handle,
                          uint8_t state, int is_initiator)
{
    int proc_is_initiator;

    if (conn_handle != proc->conn_handle) {
        return 0;
    }

    if (state != BLE_L2CAP_SM_PROC_STATE_NONE && state != proc->state) {
        return 0;
    }

    proc_is_initiator = !!(proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR);
    if (is_initiator != -1 && is_initiator != proc_is_initiator) {
        return 0;
    }

    return 1;
}

/**
 * Searches the main proc list for an entry whose connection handle and state
 * code match those specified.
 *
 * @param conn_handle           The connection handle to match against.
 * @param state                 The state code to match against.
 * @param is_initiator          Matches on the proc's initiator flag:
 *                                   0=non-initiator only
 *                                   1=initiator only
 *                                  -1=don't care
 * @param out_prev              On success, the entry previous to the result is
 *                                  written here.
 *
 * @return                      The matching proc entry on success;
 *                                  null on failure.
 */
struct ble_l2cap_sm_proc *
ble_l2cap_sm_proc_find(uint16_t conn_handle, uint8_t state, int is_initiator,
                       struct ble_l2cap_sm_proc **out_prev)
{
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;

    BLE_HS_DBG_ASSERT(ble_hs_thread_safe());

    prev = NULL;
    STAILQ_FOREACH(proc, &ble_l2cap_sm_procs, next) {
        if (ble_l2cap_sm_proc_matches(proc, conn_handle, state,
                                      is_initiator)) {
            if (out_prev != NULL) {
                *out_prev = prev;
            }
            break;
        }

        prev = proc;
    }

    return proc;
}

static void
ble_l2cap_sm_insert(struct ble_l2cap_sm_proc *proc)
{
#ifdef BLE_HS_DEBUG
    struct ble_l2cap_sm_proc *cur;

    STAILQ_FOREACH(cur, &ble_l2cap_sm_procs, next) {
        BLE_HS_DBG_ASSERT(cur != proc);
    }
#endif

    STAILQ_INSERT_HEAD(&ble_l2cap_sm_procs, proc, next);
}

static void
ble_l2cap_sm_extract_expired(struct ble_l2cap_sm_proc_list *dst_list)
{
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;
    struct ble_l2cap_sm_proc *next;
    uint32_t now;
    int32_t time_diff;

    now = os_time_get();
    STAILQ_INIT(dst_list);

    ble_hs_lock();

    prev = NULL;
    proc = STAILQ_FIRST(&ble_l2cap_sm_procs);
    while (proc != NULL) {
        next = STAILQ_NEXT(proc, next);

        time_diff = now - proc->exp_os_ticks;
        if (time_diff >= 0) {
            if (prev == NULL) {
                STAILQ_REMOVE_HEAD(&ble_l2cap_sm_procs, next);
            } else {
                STAILQ_REMOVE_AFTER(&ble_l2cap_sm_procs, prev, next);
            }
            STAILQ_INSERT_HEAD(dst_list, proc, next);
        }

        prev = proc;
        proc = next;
    }

    ble_l2cap_sm_dbg_assert_no_cycles();

    ble_hs_unlock();
}

static int
ble_l2cap_sm_rx_noop(uint16_t conn_handle, uint8_t op, struct os_mbuf **om)
{
    return BLE_HS_ENOTSUP;
}

uint8_t
ble_l2cap_sm_build_authreq(void)
{
    return ble_hs_cfg.sm_bonding << 0  |
           ble_hs_cfg.sm_mitm << 2     |
           ble_hs_cfg.sm_sc << 3       |
           ble_hs_cfg.sm_keypress << 4;
}

static int
ble_l2cap_sm_passkey_action(struct ble_l2cap_sm_proc *proc)
{
    if (proc->flags & BLE_L2CAP_SM_PROC_F_SC) {
        return ble_sm_sc_passkey_action(proc);
    } else {
        return ble_sm_lgcy_passkey_action(proc);
    }
}

void
ble_l2cap_sm_go(struct ble_l2cap_sm_proc *proc,
                struct ble_l2cap_sm_result *res,
                void *arg)
{
    ble_l2cap_sm_state_fn *cb;

    while (1) {
        BLE_HS_DBG_ASSERT(proc->state < BLE_L2CAP_SM_PROC_STATE_CNT);
        cb = ble_l2cap_sm_state_dispatch[proc->state];
        BLE_HS_DBG_ASSERT(cb != NULL);

        memset(res, 0, sizeof *res);

        cb(proc, res, arg);
        if (res->app_status != 0 ||
            proc->state == BLE_L2CAP_SM_PROC_STATE_NONE) {

            break;
        }
        if (!res->do_tx) {
            break;
        }

        arg = NULL;
    }

    if (res->app_status == 0) {
        ble_l2cap_sm_proc_set_timer(proc);
    }
}

void
ble_l2cap_sm_process_result(uint16_t conn_handle,
                            struct ble_l2cap_sm_result *res)
{
    struct ble_l2cap_sm_proc *prev;
    struct ble_l2cap_sm_proc *proc;
    int rm;

    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_NONE,
                                  -1, &prev);

    if (proc != NULL) {
        rm = res->app_status != 0 ||
             proc->state == BLE_L2CAP_SM_PROC_STATE_NONE;

        if (rm) {
            ble_l2cap_sm_proc_remove(proc, prev);
        }

        if (res->app_status != 0 && res->sm_err != 0) {
            ble_l2cap_sm_pair_fail_tx(conn_handle, res->sm_err);
        }
    }

    ble_hs_unlock();

    if (proc == NULL) {
        return;
    }

    if (res->do_cb) {
        BLE_HS_DBG_ASSERT(rm);
        ble_l2cap_sm_gap_event(proc, res->app_status,
                               res->app_status == 0);
    }

    if (rm) {
        ble_l2cap_sm_proc_free(proc);
    }
}

/*****************************************************************************
 * $hci                                                                      *
 *****************************************************************************/

static int
ble_l2cap_sm_start_encrypt_tx(struct hci_start_encrypt *cmd)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_LE_START_ENCRYPT_LEN];
    int rc;

    host_hci_cmd_build_le_start_encrypt(cmd, buf, sizeof buf);
    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
ble_l2cap_sm_enc_start_go(struct ble_l2cap_sm_proc *proc,
                          struct ble_l2cap_sm_result *res, void *arg)
{
    struct hci_start_encrypt *cmd;
    int rc;

    BLE_HS_DBG_ASSERT(proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR);

    cmd = arg;
    BLE_HS_DBG_ASSERT(cmd != NULL);

    rc = ble_l2cap_sm_start_encrypt_tx(cmd);
    if (rc != 0) {
        res->sm_err = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        res->app_status = rc;
        res->do_cb = 1;
    }
}

static void
ble_l2cap_sm_enc_restore_go(struct ble_l2cap_sm_proc *proc,
                            struct ble_l2cap_sm_result *res, void *arg)
{
    struct hci_start_encrypt *cmd;

    BLE_HS_DBG_ASSERT(proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR);

    cmd = arg;
    BLE_HS_DBG_ASSERT(cmd != NULL);

    res->app_status = ble_l2cap_sm_start_encrypt_tx(cmd);
}

static int
ble_l2cap_sm_lt_key_req_reply_tx(uint16_t conn_handle, uint8_t *ltk)
{
    struct hci_lt_key_req_reply cmd;
    uint16_t ack_conn_handle;
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_LT_KEY_REQ_REPLY_LEN];
    uint8_t ack_params_len;
    int rc;

    cmd.conn_handle = conn_handle;
    memcpy(cmd.long_term_key, ltk, 16);

    host_hci_cmd_build_le_lt_key_req_reply(&cmd, buf, sizeof buf);
    rc = ble_hci_cmd_tx(buf, &ack_conn_handle, sizeof ack_conn_handle,
                        &ack_params_len);
    if (rc != 0) {
        return rc;
    }
    if (ack_params_len != BLE_HCI_LT_KEY_REQ_REPLY_ACK_PARAM_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    ack_conn_handle = TOFROMLE16(ack_conn_handle);
    if (ack_conn_handle != conn_handle) {
        return BLE_HS_ECONTROLLER;
    }

    return 0;
}

static int
ble_l2cap_sm_lt_key_req_neg_reply_tx(uint16_t conn_handle)
{
    uint16_t ack_conn_handle;
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_LT_KEY_REQ_NEG_REPLY_LEN];
    uint8_t ack_params_len;
    int rc;

    host_hci_cmd_build_le_lt_key_req_neg_reply(conn_handle, buf, sizeof buf);
    rc = ble_hci_cmd_tx(buf, &ack_conn_handle, sizeof ack_conn_handle,
                        &ack_params_len);
    if (rc != 0) {
        return rc;
    }
    if (ack_params_len != BLE_HCI_LT_KEY_REQ_NEG_REPLY_ACK_PARAM_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    ack_conn_handle = TOFROMLE16(ack_conn_handle);
    if (ack_conn_handle != conn_handle) {
        return BLE_HS_ECONTROLLER;
    }

    return 0;
}

static void
ble_l2cap_sm_ltk_start_go(struct ble_l2cap_sm_proc *proc,
                          struct ble_l2cap_sm_result *res, void *arg)
{
    BLE_HS_DBG_ASSERT(!(proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR));

    res->app_status = ble_l2cap_sm_lt_key_req_reply_tx(proc->conn_handle,
                                                       proc->ltk);
    if (res->app_status == 0) {
        proc->state = BLE_L2CAP_SM_PROC_STATE_ENC_START;
    } else {
        res->do_cb = 1;
    }
}

static void
ble_l2cap_sm_ltk_restore_go(struct ble_l2cap_sm_proc *proc,
                            struct ble_l2cap_sm_result *res,
                            void *arg)
{
    struct ble_store_value_sec *value_sec;

    BLE_HS_DBG_ASSERT(!(proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR));

    value_sec = arg;

    if (value_sec != NULL) {
        /* Store provided a key; send it to the controller. */
        res->app_status = ble_l2cap_sm_lt_key_req_reply_tx(
            proc->conn_handle, value_sec->ltk);

        if (res->app_status == 0) {
            if (value_sec->authenticated) {
                proc->flags |= BLE_L2CAP_SM_PROC_F_AUTHENTICATED;
            }
        } else {
            /* Notify the app if it provided a key and the procedure failed. */
            res->do_cb = 1;
        }
    } else {
        /* Application does not have the requested key in its database.  Send a
         * negative reply to the controller.
         */
        ble_l2cap_sm_lt_key_req_neg_reply_tx(proc->conn_handle);
        res->app_status = BLE_HS_ENOENT;
    }

    
    if (res->app_status == 0) {
        proc->state = BLE_L2CAP_SM_PROC_STATE_ENC_RESTORE;
    }
}

/*****************************************************************************
 * $random                                                                   *
 *****************************************************************************/

uint8_t *
ble_l2cap_sm_our_pair_rand(struct ble_l2cap_sm_proc *proc)
{
    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        return proc->randm;
    } else {
        return proc->rands;
    }
}

uint8_t *
ble_l2cap_sm_their_pair_rand(struct ble_l2cap_sm_proc *proc)
{
    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        return proc->rands;
    } else {
        return proc->randm;
    }
}

static void
ble_l2cap_sm_random_go(struct ble_l2cap_sm_proc *proc,
                       struct ble_l2cap_sm_result *res, void *arg)
{
    if (proc->flags & BLE_L2CAP_SM_PROC_F_SC) {
        ble_sm_sc_random_go(proc, res);
    } else {
        ble_sm_lgcy_random_go(proc, res);
    }
}

static void
ble_l2cap_sm_random_handle(struct ble_l2cap_sm_proc *proc,
                           struct ble_l2cap_sm_pair_random *cmd,
                           struct ble_l2cap_sm_result *res)
{
    memcpy(ble_l2cap_sm_their_pair_rand(proc), cmd->value, 16);

    if (proc->flags & BLE_L2CAP_SM_PROC_F_SC) {
        ble_sm_sc_random_handle(proc, res);
    } else {
        ble_sm_lgcy_random_handle(proc, res);
    }
}

/*****************************************************************************
 * $confirm                                                                  *
 *****************************************************************************/

static int
ble_l2cap_sm_confirm_prepare_args_lgcy(struct ble_l2cap_sm_proc *proc,
                                  uint8_t *k, uint8_t *preq, uint8_t *pres,
                                  uint8_t *iat, uint8_t *rat,
                                  uint8_t *ia, uint8_t *ra)
{
    struct ble_hs_conn *conn;
    uint8_t our_addr[6];
    uint8_t our_addr_type;
    uint8_t peer_addr[6];
    uint8_t peer_addr_type;

    BLE_HS_DBG_ASSERT(ble_hs_thread_safe());

    conn = ble_hs_conn_find(proc->conn_handle);
    if (conn != NULL) {

        /* this is tricky because the address we use here for our sec
         * calcs depends on the type used in the connection . NOTE
         * The security algorithm uses 1-bit iat and rat */
        our_addr_type = conn->our_addr_type;
        switch(conn->our_addr_type) {
            case BLE_ADDR_TYPE_PUBLIC:
                bls_hs_priv_copy_local_identity_addr(our_addr, NULL);
                break;
            case BLE_ADDR_TYPE_RANDOM:
                ble_hs_priv_get_nrpa(our_addr);
                break;
            case BLE_ADDR_TYPE_RPA_PUB_DEFAULT:
                bls_hs_priv_copy_local_identity_addr(our_addr, NULL);
                if(memcmp(our_addr, conn->our_rpa_addr, 6) == 0) {
                    our_addr_type = 0;
                } else {
                    memcpy(our_addr, conn->our_rpa_addr, sizeof(our_addr));
                    our_addr_type = 1;
                }
                break;
            case BLE_ADDR_TYPE_RPA_RND_DEFAULT:
                ble_hs_priv_get_nrpa(our_addr);
                if(memcmp(our_addr, conn->our_rpa_addr, 6) == 0) {
                    our_addr_type = 1;
                } else {
                    memcpy(our_addr, conn->our_rpa_addr, sizeof(our_addr));
                    our_addr_type = 1;
                }
                break;
        }

        peer_addr_type = (conn->bhc_addr_type ? 1 : 0);
        switch(conn->bhc_addr_type) {
            case BLE_ADDR_TYPE_PUBLIC:
                memcpy(peer_addr, conn->bhc_addr, 6);
                break;
            case BLE_ADDR_TYPE_RANDOM:
                memcpy(peer_addr, conn->bhc_addr, 6);
                break;
            case BLE_ADDR_TYPE_RPA_RND_DEFAULT:
            case BLE_ADDR_TYPE_RPA_PUB_DEFAULT:
                memcpy(peer_addr, conn->peer_rpa_addr, 6);
                break;

        }

        if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
            memcpy(ia, our_addr, 6);
            *iat = our_addr_type;
            *rat = peer_addr_type;
            memcpy(ra, peer_addr, 6);
        } else {
            memcpy(ra, our_addr, 6);
            *rat = our_addr_type;
            *iat = peer_addr_type;
            memcpy(ia, peer_addr, 6);
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

static void
ble_l2cap_sm_confirm_go(struct ble_l2cap_sm_proc *proc,
                        struct ble_l2cap_sm_result *res, void *arg)
{
    if (!(proc->flags & BLE_L2CAP_SM_PROC_F_SC)) {
        ble_sm_lgcy_confirm_go(proc, res);
    } else {
        ble_sm_sc_confirm_go(proc, res);
    }
}

static void
ble_l2cap_sm_confirm_handle(struct ble_l2cap_sm_proc *proc,
                            struct ble_l2cap_sm_pair_confirm *cmd,
                            struct ble_l2cap_sm_result *res)
{
    memcpy(proc->confirm_their, cmd->value, 16);

    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        proc->state = BLE_L2CAP_SM_PROC_STATE_RANDOM;
        ble_l2cap_sm_go(proc, res, NULL);
        if (res->app_status != 0) {
            return;
        }
    } else {
        proc->flags |= BLE_L2CAP_SM_PROC_F_RX_CONFIRM;

        if (ble_l2cap_sm_passkey_action(proc) == BLE_GAP_PKACT_NONE ||
            proc->flags & BLE_L2CAP_SM_PROC_F_TK_VALID) {

            ble_l2cap_sm_go(proc, res, NULL);
            if (res->app_status != 0) {
                return;
            }
        }
    }
}

/*****************************************************************************
 * $pair                                                                     *
 *****************************************************************************/

static uint8_t
ble_l2cap_sm_state_after_pair(struct ble_l2cap_sm_proc *proc)
{
    if (proc->flags & BLE_L2CAP_SM_PROC_F_SC) {
        return BLE_L2CAP_SM_PROC_STATE_PUBLIC_KEY;
    } else {
        return BLE_L2CAP_SM_PROC_STATE_CONFIRM;
    }
}

static void
ble_l2cap_sm_pair_go(struct ble_l2cap_sm_proc *proc,
                     struct ble_l2cap_sm_result *res, void *arg)
{
    struct ble_l2cap_sm_pair_cmd cmd;
    int is_req;
    int rc;

    is_req = proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR;

    cmd.io_cap = ble_hs_cfg.sm_io_cap;
    cmd.oob_data_flag = ble_hs_cfg.sm_oob_data_flag;
    cmd.authreq = ble_l2cap_sm_build_authreq();
    cmd.max_enc_key_size = 16;

    if (is_req) {
        cmd.init_key_dist = ble_hs_cfg.sm_our_key_dist;
        cmd.resp_key_dist = ble_hs_cfg.sm_their_key_dist;
    } else {
        /* The response's key distribution flags field is the intersection of
         * the peer's preferences and our capabilities.
         */
        cmd.init_key_dist = proc->pair_req.init_key_dist &
                            ble_hs_cfg.sm_their_key_dist;
        cmd.resp_key_dist = proc->pair_req.resp_key_dist &
                            ble_hs_cfg.sm_our_key_dist;
    }

    rc = ble_l2cap_sm_pair_cmd_tx(proc->conn_handle, is_req, &cmd);
    if (rc != 0) {
        goto err;
    }

    if (is_req) {
        proc->pair_req = cmd;
    } else {
        proc->pair_rsp = cmd;
    }

    rc = ble_l2cap_sm_gen_pair_rand(ble_l2cap_sm_our_pair_rand(proc));
    if (rc != 0) {
        goto err;
    }

    return;

err:
    res->app_status = rc;

    if (is_req) {
        res->sm_err = BLE_L2CAP_SM_ERR_UNSPECIFIED;
    }
}

static void
ble_l2cap_sm_pair_req_handle(struct ble_l2cap_sm_proc *proc,
                             struct ble_l2cap_sm_pair_cmd *req,
                             struct ble_l2cap_sm_result *res)
{
    struct ble_hs_conn *conn;

    proc->pair_req = *req;

    conn = ble_hs_conn_find(proc->conn_handle);
    if (conn == NULL) {
        res->sm_err = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        res->app_status = BLE_HS_ENOTCONN;
        return;
    }
    if (conn->bhc_flags & BLE_HS_CONN_F_MASTER) {
        res->sm_err = BLE_L2CAP_SM_ERR_CMD_NOT_SUPP;
        res->app_status = BLE_HS_SM_US_ERR(BLE_L2CAP_SM_ERR_CMD_NOT_SUPP);
        return;
    }

    if (!ble_l2cap_sm_pair_cmd_is_valid(req)) {
        res->sm_err = BLE_L2CAP_SM_ERR_INVAL;
        res->app_status =  BLE_HS_SM_US_ERR(BLE_L2CAP_SM_ERR_INVAL);
        return;
    }

    ble_l2cap_sm_go(proc, res, NULL);
    if (res->app_status != 0) {
        return;
    }

    ble_l2cap_sm_check_key_exchange(proc);
    proc->state = ble_l2cap_sm_state_after_pair(proc);
}

static void
ble_l2cap_sm_pair_rsp_handle(struct ble_l2cap_sm_proc *proc,
                             struct ble_l2cap_sm_pair_cmd *rsp,
                             uint8_t passkey_action,
                             struct ble_l2cap_sm_result *res)
{
    proc->pair_rsp = *rsp;

    if (!ble_l2cap_sm_pair_cmd_is_valid(rsp)) {
        res->sm_err = BLE_L2CAP_SM_ERR_INVAL;
        res->app_status = BLE_HS_SM_US_ERR(BLE_L2CAP_SM_ERR_INVAL);
        return;
    }

    ble_l2cap_sm_check_key_exchange(proc);

    /* If there is no passkey action to take, proceed to next state. */
    if (passkey_action == BLE_GAP_PKACT_NONE) {
        proc->state = ble_l2cap_sm_state_after_pair(proc);
        ble_l2cap_sm_go(proc, res, NULL);
    }
}

/*****************************************************************************
 * $security request                                                         *
 *****************************************************************************/

static void
ble_l2cap_sm_sec_req_go(struct ble_l2cap_sm_proc *proc,
                        struct ble_l2cap_sm_result *res, void *arg)
{
    struct ble_l2cap_sm_sec_req cmd;
    int rc;

    cmd.authreq = ble_l2cap_sm_build_authreq();
    rc = ble_l2cap_sm_sec_req_tx(proc->conn_handle, &cmd);
    if (rc != 0) {
        res->app_status = rc;
        return;
    }
}

/*****************************************************************************
 * $key exchange                                                             *
 *****************************************************************************/

static void
ble_l2cap_sm_check_key_exchange(struct ble_l2cap_sm_proc *proc)
{
    uint8_t rx_key_dist;

    if (proc->pair_req.authreq & BLE_L2CAP_SM_PAIR_AUTHREQ_BOND &&
        proc->pair_rsp.authreq & BLE_L2CAP_SM_PAIR_AUTHREQ_BOND &&
        proc->pair_rsp.init_key_dist                            &&
        proc->pair_rsp.resp_key_dist) {

        proc->flags |= BLE_L2CAP_SM_PROC_F_KEY_EXCHANGE;
    }

    if (proc->pair_req.authreq & BLE_L2CAP_SM_PAIR_AUTHREQ_SC &&
        proc->pair_rsp.authreq & BLE_L2CAP_SM_PAIR_AUTHREQ_SC) {

        proc->flags |= BLE_L2CAP_SM_PROC_F_SC;
    }

    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        rx_key_dist = proc->pair_rsp.resp_key_dist;
    } else {
        rx_key_dist = proc->pair_rsp.init_key_dist;
    }

    proc->rx_key_flags = 0;
    if (rx_key_dist & BLE_L2CAP_SM_PAIR_KEY_DIST_ENC) {
        proc->rx_key_flags |= BLE_L2CAP_SM_KE_F_ENC_INFO |
                              BLE_L2CAP_SM_KE_F_MASTER_IDEN;
    }
    if (rx_key_dist & BLE_L2CAP_SM_PAIR_KEY_DIST_ID) {
        proc->rx_key_flags |= BLE_L2CAP_SM_KE_F_IDEN_INFO |
                              BLE_L2CAP_SM_KE_F_ADDR_INFO;
    }
    if (rx_key_dist & BLE_L2CAP_SM_PAIR_KEY_DIST_SIGN) {
        proc->rx_key_flags |= BLE_L2CAP_SM_KE_F_SIGN_INFO;
    }
}

static void
ble_l2cap_sm_enc_info_handle(struct ble_l2cap_sm_proc *proc,
                             struct ble_l2cap_sm_enc_info *info)
{
    proc->rx_key_flags &= ~BLE_L2CAP_SM_KE_F_ENC_INFO;

    /* Save until completion. */
    proc->peer_keys->ltk_valid = 1;
    memcpy(proc->peer_keys->ltk, info->ltk_le, 16);
}

static void
ble_l2cap_sm_master_iden_handle(struct ble_l2cap_sm_proc *proc,
                             struct ble_l2cap_sm_master_iden *info)
{
    proc->rx_key_flags &= ~BLE_L2CAP_SM_KE_F_MASTER_IDEN;
    /* Save until completion. */
    proc->peer_keys->ediv_rand_valid = 1;
    proc->peer_keys->ediv = info->ediv;
    proc->peer_keys->rand_val = info->rand_val;
}

static void
ble_l2cap_sm_iden_info_handle(struct ble_l2cap_sm_proc *proc,
                              struct ble_l2cap_sm_iden_info *info)
{
    proc->rx_key_flags &= ~BLE_L2CAP_SM_KE_F_IDEN_INFO;

    /* Save until completion. */
    proc->peer_keys->irk_valid = 1;
    memcpy(proc->peer_keys->irk, info->irk_le, 16);
}

static void
ble_l2cap_sm_iden_addr_handle(struct ble_l2cap_sm_proc *proc,
                              struct ble_l2cap_sm_iden_addr_info *info)
{
    proc->rx_key_flags &= ~BLE_L2CAP_SM_KE_F_ADDR_INFO;

    /* Save until completion. */
    proc->peer_keys->addr_valid = 1;
    proc->peer_keys->addr_type = info->addr_type;
    memcpy(proc->peer_keys->addr, info->bd_addr_le, 6);
}

static void
ble_l2cap_sm_signing_info_handle(struct ble_l2cap_sm_proc *proc,
                                 struct ble_l2cap_sm_signing_info *info)
{
    proc->rx_key_flags &= ~BLE_L2CAP_SM_KE_F_SIGN_INFO;

    /* save until completion */
    proc->peer_keys->csrk_valid = 1;
    memcpy(proc->peer_keys->csrk, info->sig_key_le, 16);
}

static void
ble_l2cap_sm_key_exch_go(struct ble_l2cap_sm_proc *proc,
                         struct ble_l2cap_sm_result *res, void *arg)
{
    struct ble_l2cap_sm_signing_info sign_info;
    struct ble_l2cap_sm_master_iden master_iden;
    struct ble_l2cap_sm_iden_info iden_info;
    struct ble_l2cap_sm_enc_info enc_info;
    struct ble_l2cap_sm_iden_addr_info iden_addr_info;
    uint8_t our_key_dist;
    int rc;

    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        our_key_dist = proc->pair_rsp.init_key_dist;
    } else {
        our_key_dist = proc->pair_rsp.resp_key_dist;
    }

    if (our_key_dist == 0) {
        return;
    }

    BLE_HS_DBG_ASSERT(proc->our_keys == NULL);
    proc->our_keys = ble_l2cap_sm_keys_alloc();
    BLE_HS_DBG_ASSERT(proc->our_keys != NULL);

    if (our_key_dist & BLE_L2CAP_SM_PAIR_KEY_DIST_ENC) {
        /* Send encryption information. */
        rc = ble_l2cap_sm_gen_ltk(proc, enc_info.ltk_le);
        if (rc != 0) {
            goto err;
        }
        rc = ble_l2cap_sm_enc_info_tx(proc->conn_handle, &enc_info);
        if (rc != 0) {
            goto err;
        }
        proc->our_keys->ltk_valid = 1;
        memcpy(proc->our_keys->ltk, enc_info.ltk_le, 16);

        /* Send master identification. */
        rc = ble_l2cap_sm_gen_ediv(&master_iden.ediv);
        if (rc != 0) {
            goto err;
        }
        rc = ble_l2cap_sm_gen_start_rand(&master_iden.rand_val);
        if (rc != 0) {
            goto err;
        }
        rc = ble_l2cap_sm_master_iden_tx(proc->conn_handle, &master_iden);
        if (rc != 0) {
            goto err;
        }
        proc->our_keys->ediv_rand_valid = 1;
        proc->our_keys->rand_val = master_iden.rand_val;
        proc->our_keys->ediv = master_iden.ediv;
    }

    if (our_key_dist & BLE_L2CAP_SM_PAIR_KEY_DIST_ID) {
        /* send IRK and BD ADDR */
        uint8_t *irk = ble_hs_priv_get_local_irk();

        memcpy(iden_info.irk_le, irk, 16);

        rc = ble_l2cap_sm_iden_info_tx(proc->conn_handle, &iden_info);
        if (rc != 0) {
            goto err;
        }
        proc->our_keys->irk_valid = 1;
        memcpy(proc->our_keys->irk, iden_info.irk_le, 16);

        bls_hs_priv_copy_local_identity_addr(iden_addr_info.bd_addr_le,
                                            &iden_addr_info.addr_type);
        rc = ble_l2cap_sm_iden_addr_tx(proc->conn_handle, &iden_addr_info);
        if (rc != 0) {
            goto err;
        }

        /* copy data to pass to application */
        proc->our_keys.irk_valid = 1;
        proc->our_keys.addr_valid = 1;
        memcpy(proc->our_keys.irk, irk,16);
        proc->our_keys.addr_type = iden_addr_info.addr_type;
        memcpy(proc->our_keys.addr, iden_addr_info.bd_addr_le, 6);
        memcpy(proc->our_keys.ltk, enc_info.ltk_le, 16);
    }

    if (our_key_dist & BLE_L2CAP_SM_PAIR_KEY_DIST_SIGN) {
        /* Send signing information. */
        rc = ble_l2cap_sm_gen_csrk(proc, sign_info.sig_key_le);
        if (rc != 0) {
            goto err;
        }
        rc = ble_l2cap_sm_signing_info_tx(proc->conn_handle, &sign_info);
        if (rc != 0) {
            goto err;
        }
        proc->our_keys->csrk_valid = 1;
        memcpy(proc->our_keys->csrk, sign_info.sig_key_le, 16);
    }

    return;

err:
    res->app_status = rc;
    res->sm_err = BLE_L2CAP_SM_ERR_UNSPECIFIED;
    res->do_cb = 1;
}

static int
ble_l2cap_sm_rx_key_exchange(uint16_t conn_handle, uint8_t op,
                             struct os_mbuf **om)
{
    union {
        struct ble_l2cap_sm_enc_info enc_info;
        struct ble_l2cap_sm_master_iden master_iden;
        struct ble_l2cap_sm_iden_info iden_info;
        struct ble_l2cap_sm_iden_addr_info iden_addr;
        struct ble_l2cap_sm_signing_info signing_info;
    } u;
    int rc;
    int base_len;
    struct ble_l2cap_sm_result res;
    struct ble_l2cap_sm_keys *peer_keys;
    struct ble_l2cap_sm_keys *our_keys;
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;
    int authenticated;

    switch (op)  {
    case BLE_L2CAP_SM_OP_ENC_INFO:
        base_len = BLE_L2CAP_SM_ENC_INFO_SZ;
        break;

    case BLE_L2CAP_SM_OP_MASTER_ID:
        base_len = BLE_L2CAP_SM_MASTER_IDEN_SZ;
        break;

    case BLE_L2CAP_SM_OP_IDENTITY_INFO:
        base_len = BLE_L2CAP_SM_IDEN_INFO_SZ;
        break;

    case BLE_L2CAP_SM_OP_IDENTITY_ADDR_INFO:
        base_len = BLE_L2CAP_SM_IDEN_ADDR_INFO_SZ;
        break;

    case BLE_L2CAP_SM_OP_SIGN_INFO:
        base_len = BLE_L2CAP_SM_SIGNING_INFO_SZ;
        break;

    default:
        /* TODO error code */
        return BLE_HS_ENOTSUP;
    }

    rc = ble_hs_misc_pullup_base(om, base_len);
    if (rc != 0) {
        return rc;
    }

    switch (op) {
    case BLE_L2CAP_SM_OP_ENC_INFO:
        ble_l2cap_sm_enc_info_parse((*om)->om_data, (*om)->om_len,
                                    &u.enc_info);
        break;
    case BLE_L2CAP_SM_OP_MASTER_ID:
        ble_l2cap_sm_master_iden_parse((*om)->om_data, (*om)->om_len,
                                       &u.master_iden);
        break;
    case BLE_L2CAP_SM_OP_IDENTITY_INFO:
        ble_l2cap_sm_iden_info_parse((*om)->om_data, (*om)->om_len,
                                     &u.iden_info);
        break;
    case BLE_L2CAP_SM_OP_IDENTITY_ADDR_INFO:
        ble_l2cap_sm_iden_addr_parse((*om)->om_data, (*om)->om_len,
                                     &u.iden_addr);
        break;
    case BLE_L2CAP_SM_OP_SIGN_INFO:
        ble_l2cap_sm_signing_info_parse((*om)->om_data, (*om)->om_len,
                                        &u.signing_info);
        break;
    }

    ble_hs_lock();

    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_KEY_EXCH,
                                  -1, &prev);
    if (proc != NULL) {
        if (proc->peer_keys == NULL) {
            proc->peer_keys = ble_l2cap_sm_keys_alloc();
            BLE_HS_DBG_ASSERT(proc->peer_keys != NULL);
        }

        switch (op) {
        case BLE_L2CAP_SM_OP_ENC_INFO:
            ble_l2cap_sm_enc_info_handle(proc, &u.enc_info);
            break;

        case BLE_L2CAP_SM_OP_MASTER_ID:
            ble_l2cap_sm_master_iden_handle(proc, &u.master_iden);
            break;

        case BLE_L2CAP_SM_OP_IDENTITY_INFO:
            ble_l2cap_sm_iden_info_handle(proc, &u.iden_info);
            break;

        case BLE_L2CAP_SM_OP_IDENTITY_ADDR_INFO:
            ble_l2cap_sm_iden_addr_handle(proc, &u.iden_addr);
            break;

        case BLE_L2CAP_SM_OP_SIGN_INFO:
            ble_l2cap_sm_signing_info_handle(proc, &u.signing_info);
            break;
        }

        BLE_HS_LOG(DEBUG, "op=%d rx_key_flags=0x%02x\n",
                   op, proc->rx_key_flags);

        if (!proc->rx_key_flags) {
            if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
                ble_l2cap_sm_go(proc, &res, NULL);
            }

            /* The SM procedure is complete. */
            proc->state = BLE_L2CAP_SM_PROC_STATE_NONE;

            if (res.app_status == 0) {
                proc->flags |= BLE_L2CAP_SM_PROC_F_BONDED;
            }
        }
    } else {
        res.app_status = BLE_HS_ENOENT;
    }

    if (proc->flags & BLE_L2CAP_SM_PROC_F_BONDED) {
        our_keys = proc->our_keys;
        peer_keys = proc->peer_keys;
        authenticated = proc->flags & BLE_L2CAP_SM_PROC_F_AUTHENTICATED;

        proc->our_keys = NULL;
        proc->peer_keys = NULL;
    } else {
        our_keys = NULL;
        peer_keys = NULL;
    }

    ble_hs_unlock();

    if (our_keys != NULL && peer_keys != NULL) {
        ble_l2cap_sm_key_exchange_events(conn_handle, our_keys, peer_keys,
                                         authenticated);
    }

    ble_l2cap_sm_process_result(conn_handle, &res);

    return res.app_status;
}

/*****************************************************************************
 * $rx                                                                       *
 *****************************************************************************/

static int
ble_l2cap_sm_rx_pair_req(uint16_t conn_handle, uint8_t op,
                         struct os_mbuf **om)
{
    struct ble_l2cap_sm_pair_cmd req;
    struct ble_l2cap_sm_result res;
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;
    uint8_t passkey_action = BLE_GAP_PKACT_NONE;

    int rc;

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SM_PAIR_CMD_SZ);
    if (rc != 0) {
        return rc;
    }

    ble_l2cap_sm_pair_cmd_parse((*om)->om_data, (*om)->om_len, &req);

    BLE_HS_LOG(DEBUG, "rxed sm pair req; io_cap=0x%02x oob_data_flag=%d "
                      "authreq=0x%02x max_enc_key_size=%d "
                      "init_key_dist=0x%02x resp_key_dist=0x%02x\n",
               req.io_cap, req.oob_data_flag, req.authreq,
               req.max_enc_key_size, req.init_key_dist, req.resp_key_dist);

    ble_hs_lock();

    /* XXX: Check connection state; reject if not appropriate. */
    /* XXX: Ensure enough time has passed since the previous failed pairing
     * attempt.
     */
    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_NONE,
                                  -1, &prev);
    if (proc != NULL) {
        /* Pairing already in progress; abort old procedure and start new. */
        /* XXX: Check the spec on this. */
        ble_l2cap_sm_proc_remove(proc, prev);
        ble_l2cap_sm_proc_free(proc);
    }

    memset(&res, 0, sizeof res);

    proc = ble_l2cap_sm_proc_alloc();
    if (proc != NULL) {
        proc->conn_handle = conn_handle;
        proc->state = BLE_L2CAP_SM_PROC_STATE_PAIR;
        ble_l2cap_sm_insert(proc);

        ble_l2cap_sm_pair_req_handle(proc, &req, &res);
        if (res.app_status == 0) {
            passkey_action = ble_l2cap_sm_passkey_action(proc);
        }
    }

    ble_hs_unlock();

    if (res.app_status == 0 && passkey_action != BLE_GAP_PKACT_NONE) {
        ble_gap_passkey_event(conn_handle, passkey_action);
    }

    ble_l2cap_sm_process_result(conn_handle, &res);

    return res.app_status;
}

static int
ble_l2cap_sm_rx_pair_rsp(uint16_t conn_handle, uint8_t op,
                         struct os_mbuf **om)
{
    struct ble_l2cap_sm_pair_cmd rsp;
    struct ble_l2cap_sm_result res;
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;
    int rc;
    uint8_t passkey_action = BLE_GAP_PKACT_NONE;

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SM_PAIR_CMD_SZ);
    if (rc != 0) {
        return rc;
    }

    ble_l2cap_sm_pair_cmd_parse((*om)->om_data, (*om)->om_len, &rsp);

    BLE_HS_LOG(DEBUG, "rxed sm pair rsp; io_cap=0x%02x oob_data_flag=%d "
                      "authreq=0x%02x max_enc_key_size=%d "
                      "init_key_dist=0x%02x resp_key_dist=0x%02x\n",
               rsp.io_cap, rsp.oob_data_flag, rsp.authreq,
               rsp.max_enc_key_size, rsp.init_key_dist, rsp.resp_key_dist);

    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_PAIR, 1,
                                  &prev);
    if (proc != NULL) {
        passkey_action = ble_l2cap_sm_passkey_action(proc);

        memset(&res, 0, sizeof res);
        ble_l2cap_sm_pair_rsp_handle(proc, &rsp, passkey_action, &res);
    }

    ble_hs_unlock();

    if (proc == NULL) {
        return BLE_HS_ENOENT;
    }

    if (passkey_action != BLE_GAP_PKACT_NONE) {
        ble_gap_passkey_event(conn_handle, passkey_action);
    }

    ble_l2cap_sm_process_result(conn_handle, &res);

    return res.app_status;
}

static int
ble_l2cap_sm_rx_pair_confirm(uint16_t conn_handle, uint8_t op,
                             struct os_mbuf **om)
{
    struct ble_l2cap_sm_pair_confirm cmd;
    struct ble_l2cap_sm_result res;
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;

    res.app_status = ble_hs_misc_pullup_base(om, BLE_L2CAP_SM_PAIR_CONFIRM_SZ);
    if (res.app_status != 0) {
        return res.app_status;
    }

    ble_l2cap_sm_pair_confirm_parse((*om)->om_data, (*om)->om_len, &cmd);

    BLE_HS_LOG(DEBUG, "rxed sm confirm cmd\n");

    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_CONFIRM,
                                  -1, &prev);
    if (proc != NULL) {
        memset(&res, 0, sizeof res);
        ble_l2cap_sm_confirm_handle(proc, &cmd, &res);
    }
    ble_hs_unlock();

    if (proc == NULL) {
        return BLE_HS_ENOENT;
    }

    ble_l2cap_sm_process_result(conn_handle, &res);

    return res.app_status;
}

static int
ble_l2cap_sm_rx_pair_random(uint16_t conn_handle, uint8_t op,
                            struct os_mbuf **om)
{
    struct ble_l2cap_sm_pair_random cmd;
    struct ble_l2cap_sm_result res;
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;
    int rc;

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SM_PAIR_RANDOM_SZ);
    if (rc != 0) {
        return rc;
    }

    ble_l2cap_sm_pair_random_parse((*om)->om_data, (*om)->om_len, &cmd);

    BLE_HS_LOG(DEBUG, "rxed sm random cmd\n");

    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_RANDOM,
                                  -1, &prev);
    if (proc != NULL) {
        memset(&res, 0, sizeof res);
        ble_l2cap_sm_random_handle(proc, &cmd, &res);
    }
    ble_hs_unlock();

    if (proc == NULL) {
        return BLE_HS_ENOENT;
    }

    ble_l2cap_sm_process_result(conn_handle, &res);

    return res.app_status;
}

static int
ble_l2cap_sm_rx_pair_fail(uint16_t conn_handle, uint8_t op,
                          struct os_mbuf **om)
{
    struct ble_l2cap_sm_pair_fail cmd;
    struct ble_l2cap_sm_result res;
    int rc;

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SM_PAIR_FAIL_SZ);
    if (rc != 0) {
        return rc;
    }

    ble_l2cap_sm_pair_fail_parse((*om)->om_data, (*om)->om_len, &cmd);

    BLE_HS_LOG(DEBUG, "rxed sm fail cmd; reason=%d\n", cmd.reason);

    memset(&res, 0, sizeof res);
    res.app_status = BLE_HS_SM_THEM_ERR(cmd.reason),
    ble_l2cap_sm_process_result(conn_handle, &res);

    return 0;
}

static int
ble_l2cap_sm_retrieve_ltk(struct hci_le_lt_key_req *evt,
                          struct ble_store_value_sec *value_sec)
{
    struct ble_store_key_sec key_sec;
    int rc;

    /* Tell application to look up LTK by ediv/rand pair. */
    /* XXX: Also filter by peer address? */
    memset(&key_sec, 0, sizeof key_sec);
    key_sec.peer_addr_type = BLE_STORE_ADDR_TYPE_NONE;
    key_sec.ediv = evt->encrypted_diversifier;
    key_sec.rand_num = evt->random_number;
    key_sec.ediv_rand_present = 1;

    rc = ble_store_read_slv_sec(&key_sec, value_sec);
    return rc;
}

int
ble_l2cap_sm_rx_lt_key_req(struct hci_le_lt_key_req *evt)
{
    struct ble_store_value_sec value_sec;
    struct ble_l2cap_sm_result res;
    struct ble_l2cap_sm_proc *proc;
    void *arg;
    int store_rc;
    int bonding;

    memset(&res, 0, sizeof res);
    arg = NULL;

    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(evt->connection_handle,
                                  BLE_L2CAP_SM_PROC_STATE_NONE, 0, NULL);
    if (proc == NULL) {
        /* The peer is attempting to restore a encrypted connection via the
         * encryption procedure (bonding).  Create a proc entry to indicate
         * that security establishment is in progress and execute the procedure
         * after the mutex gets unlocked.
         */
        /* XXX: Ensure we are the master. */
        bonding = 1;
        proc = ble_l2cap_sm_proc_alloc();
        if (proc == NULL) {
            res.app_status = BLE_HS_ENOMEM;
        } else {
            proc->conn_handle = evt->connection_handle;
            proc->state = BLE_L2CAP_SM_PROC_STATE_LTK_RESTORE;
            proc->flags |= BLE_L2CAP_SM_PROC_F_BONDED;
            ble_l2cap_sm_insert(proc);
        }
    } else if (proc->state == BLE_L2CAP_SM_PROC_STATE_SEC_REQ) {
        /* Same as above, except we solicited the encryption procedure by
         * sending a security request.
         */
        bonding = 1;
        proc->state = BLE_L2CAP_SM_PROC_STATE_LTK_RESTORE;
        proc->flags |= BLE_L2CAP_SM_PROC_F_BONDED;
    } else if (proc->state == BLE_L2CAP_SM_PROC_STATE_LTK_START) {
        /* Short-term key pairing just completed.  Send the short term key to
         * the controller.
         */
        bonding = 0;
    } else {
        /* The request is unexpected.  Quietly ignore it. */
        proc = NULL;
    }

    ble_hs_unlock();

    if (proc == NULL) {
        return res.app_status;
    }

    if (bonding) {
        store_rc = ble_l2cap_sm_retrieve_ltk(evt, &value_sec);
        if (store_rc == 0) {
            arg = &value_sec;
        }
    }

    if (res.app_status == 0) {
        ble_hs_lock();

        proc = ble_l2cap_sm_proc_find(evt->connection_handle,
                                      BLE_L2CAP_SM_PROC_STATE_NONE, 0, NULL);
        if (proc != NULL) {
            ble_l2cap_sm_go(proc, &res, arg);
        }

        ble_hs_unlock();
    }

    ble_l2cap_sm_process_result(evt->connection_handle, &res);

    return 0;
}

void
ble_l2cap_sm_rx_encryption_change(struct hci_encrypt_change *evt)
{
    struct ble_l2cap_sm_result res;
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;
    int enc_enabled = 0;
    int do_key_exchange = 0;

    memset(&res, 0, sizeof res);

    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(evt->connection_handle,
                                  BLE_L2CAP_SM_PROC_STATE_NONE, -1,
                                  &prev);
    if (proc == NULL) {
        res.app_status = BLE_HS_ENOENT;
    } else if (proc->state == BLE_L2CAP_SM_PROC_STATE_ENC_START) {
        enc_enabled = evt->encryption_enabled & 0x01; /* LE bit. */
        do_key_exchange = proc->flags & BLE_L2CAP_SM_PROC_F_KEY_EXCHANGE;
        res.app_status = 0;
    } else if (proc->state == BLE_L2CAP_SM_PROC_STATE_ENC_RESTORE) {
        enc_enabled = evt->encryption_enabled & 0x01; /* LE bit. */
        do_key_exchange = 0;
        res.app_status = 0;
    } else {
        proc = NULL;
        res.app_status = BLE_HS_ENOENT;
    }

    if (proc != NULL) {
        if (do_key_exchange && enc_enabled) {
            proc->state = BLE_L2CAP_SM_PROC_STATE_KEY_EXCH;

            /* The responder sends its keys first. */
            if (!(proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR)) {
                ble_l2cap_sm_go(proc, &res, NULL);
            }
        } else {
            proc->state = BLE_L2CAP_SM_PROC_STATE_NONE;
            res.do_cb = 1;
        }
    }

    ble_hs_unlock();

    ble_l2cap_sm_process_result(evt->connection_handle, &res);
}

static int
ble_l2cap_sm_rx_sec_req(uint16_t conn_handle, uint8_t op, struct os_mbuf **om)
{
    struct ble_l2cap_sm_sec_req cmd;
    struct ble_store_value_sec value_sec;
    struct ble_store_key_sec key_sec;
    struct ble_hs_conn *conn;
    int authreq_mitm;
    int rc;

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SM_SEC_REQ_SZ);
    if (rc != 0) {
        return rc;
    }

    ble_l2cap_sm_sec_req_parse((*om)->om_data, (*om)->om_len, &cmd);

    /* XXX: Reject if:
     *     o authreq-bonded flag not set?
     *     o authreq-reserved flags set?
     */

    BLE_HS_LOG(DEBUG, "rxed sm sec req; authreq=%d\n", cmd.authreq);

    ble_hs_lock();

    conn = ble_hs_conn_find(conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else if (!(conn->bhc_flags & BLE_HS_CONN_F_MASTER)) {
        rc = BLE_HS_SM_US_ERR(BLE_L2CAP_SM_ERR_CMD_NOT_SUPP);
        ble_l2cap_sm_pair_fail_tx(conn_handle, BLE_L2CAP_SM_ERR_CMD_NOT_SUPP);
    } else {
        rc = 0;

        /* We will be querying the SM database for a key corresponding to the
         * sender; remember the sender's address while the connection list is
         * locked.
         */
        memset(&key_sec, 0, sizeof key_sec);
        key_sec.peer_addr_type = conn->bhc_addr_type;
        memcpy(key_sec.peer_addr, conn->bhc_addr, 6);
    }

    ble_hs_unlock();

    if (rc == 0) {
        /* Query database for an LTK corresponding to the sender.  We are the
         * master, so retrieve a master key.
         */
        rc = ble_store_read_mst_sec(&key_sec, &value_sec);
        if (rc == 0) {
            /* Found a key corresponding to this peer.  Make sure it meets the
             * requested minimum authreq.
             */
            authreq_mitm = cmd.authreq & BLE_L2CAP_SM_PAIR_AUTHREQ_MITM;
            if ((!authreq_mitm && value_sec.authenticated) ||
                (authreq_mitm && !value_sec.authenticated)) {

                rc = BLE_HS_EREJECT;
            }
        }

        if (rc == 0) {
            rc = ble_l2cap_sm_enc_initiate(conn_handle, value_sec.ltk,
                                           value_sec.ediv, value_sec.rand_num,
                                           value_sec.authenticated);
        } else {
            rc = ble_l2cap_sm_pair_initiate(conn_handle);
        }
    }

    return rc;
}

static int
ble_l2cap_sm_rx(uint16_t conn_handle, struct os_mbuf **om)
{
    ble_l2cap_sm_rx_fn *rx_cb;
    uint8_t op;
    int rc;

    STATS_INC(ble_l2cap_stats, sm_rx);
    BLE_HS_LOG(DEBUG, "L2CAP - rxed security manager msg: ");
    ble_hs_misc_log_mbuf(*om);
    BLE_HS_LOG(DEBUG, "\n");

    rc = os_mbuf_copydata(*om, 0, 1, &op);
    if (rc != 0) {
        return BLE_HS_EBADDATA;
    }

    /* Strip L2CAP SM header from the front of the mbuf. */
    os_mbuf_adj(*om, 1);

    rx_cb = ble_l2cap_sm_dispatch_get(op);
    if (rx_cb != NULL) {
        rc = rx_cb(conn_handle, op, om);
    } else {
        rc = BLE_HS_ENOTSUP;
    }

    return rc;
}

/*****************************************************************************
 * $api                                                                      *
 *****************************************************************************/

void
ble_l2cap_sm_heartbeat(void)
{
    struct ble_l2cap_sm_proc_list exp_list;
    struct ble_l2cap_sm_proc *proc;

    /* Remove all timed out procedures and insert them into a temporary
     * list.
     */
    ble_l2cap_sm_extract_expired(&exp_list);

    /* Notify application of each failure and free the corresponding procedure
     * object.
     */
    while ((proc = STAILQ_FIRST(&exp_list)) != NULL) {
        ble_l2cap_sm_gap_event(proc, BLE_HS_ETIMEOUT, 0);

        STAILQ_REMOVE_HEAD(&exp_list, next);
        ble_l2cap_sm_proc_free(proc);
    }
}

/**
 * Initiates the pairing procedure for the specified connection.
 */
int
ble_l2cap_sm_pair_initiate(uint16_t conn_handle)
{
    struct ble_l2cap_sm_result res;
    struct ble_l2cap_sm_proc *proc;

    /* Make sure a procedure isn't already in progress for this connection. */
    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_NONE,
                                  -1, NULL);
    if (proc != NULL) {
        res.app_status = BLE_HS_EALREADY;

        /* Set pointer to null so that existing entry doesn't get freed. */
        proc = NULL;
    } else {
        proc = ble_l2cap_sm_proc_alloc();
        if (proc == NULL) {
            res.app_status = BLE_HS_ENOMEM;
        } else {
            proc->conn_handle = conn_handle;
            proc->state = BLE_L2CAP_SM_PROC_STATE_PAIR;
            proc->flags |= BLE_L2CAP_SM_PROC_F_INITIATOR;
            ble_l2cap_sm_insert(proc);

            ble_l2cap_sm_go(proc, &res, NULL);
        }
    }

    ble_hs_unlock();

    if (proc != NULL) {
        ble_l2cap_sm_process_result(conn_handle, &res);
    }

    return res.app_status;
}

int
ble_l2cap_sm_slave_initiate(uint16_t conn_handle)
{
    struct ble_l2cap_sm_result res;
    struct ble_l2cap_sm_proc *proc;

    ble_hs_lock();

    /* Make sure a procedure isn't already in progress for this connection. */
    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_NONE,
                                  -1, NULL);
    if (proc != NULL) {
        res.app_status = BLE_HS_EALREADY;

        /* Set pointer to null so that existing entry doesn't get freed. */
        proc = NULL;
    } else {
        proc = ble_l2cap_sm_proc_alloc();
        if (proc == NULL) {
            res.app_status = BLE_HS_ENOMEM;
        } else {
            proc->conn_handle = conn_handle;
            proc->state = BLE_L2CAP_SM_PROC_STATE_SEC_REQ;
            ble_l2cap_sm_insert(proc);

            ble_l2cap_sm_go(proc, &res, NULL);
        }
    }

    ble_hs_unlock();

    if (proc != NULL) {
        ble_l2cap_sm_process_result(conn_handle, &res);
    }

    return res.app_status;
}

/**
 * Initiates the encryption procedure for the specified connection.
 */
int
ble_l2cap_sm_enc_initiate(uint16_t conn_handle,
                          uint8_t *ltk,
                          uint16_t ediv,
                          uint64_t rand_val,
                          int auth)
{
    struct ble_l2cap_sm_result res;
    struct ble_l2cap_sm_proc *proc;
    struct hci_start_encrypt cmd;

    memset(&res, 0, sizeof res);

    /* Make sure a procedure isn't already in progress for this connection. */
    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_NONE,
                                  -1, NULL);
    if (proc != NULL) {
        res.app_status = BLE_HS_EALREADY;

        /* Set pointer to null so that existing entry doesn't get freed. */
        proc = NULL;
    } else {
        proc = ble_l2cap_sm_proc_alloc();
        if (proc == NULL) {
            res.app_status = BLE_HS_ENOMEM;
        } else {
            proc->conn_handle = conn_handle;
            proc->state = BLE_L2CAP_SM_PROC_STATE_ENC_RESTORE;
            proc->flags |= BLE_L2CAP_SM_PROC_F_INITIATOR;
            if (auth) {
                proc->flags |= BLE_L2CAP_SM_PROC_F_AUTHENTICATED;
            }
            ble_l2cap_sm_insert(proc);

            cmd.connection_handle = conn_handle;
            cmd.encrypted_diversifier = ediv;
            cmd.random_number = rand_val;
            memcpy(cmd.long_term_key, ltk, sizeof cmd.long_term_key);
            ble_l2cap_sm_go(proc, &res, &cmd);
        }
    }
    
    ble_hs_unlock();

    ble_l2cap_sm_process_result(conn_handle, &res);

    return res.app_status;
}

struct ble_l2cap_chan *
ble_l2cap_sm_create_chan(void)
{
    struct ble_l2cap_chan *chan;

    chan = ble_l2cap_chan_alloc();
    if (chan == NULL) {
        return NULL;
    }

    chan->blc_cid = BLE_L2CAP_CID_SM;
    chan->blc_my_mtu = BLE_L2CAP_SM_MTU;
    chan->blc_default_mtu = BLE_L2CAP_SM_MTU;
    chan->blc_rx_fn = ble_l2cap_sm_rx;

    return chan;
}

int
ble_l2cap_sm_set_tk(uint16_t conn_handle, struct ble_l2cap_sm_passkey *pkey)
{
    struct ble_l2cap_sm_result res;
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;

    memset(&res, 0, sizeof res);

    ble_hs_lock();

    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_CONFIRM,
                                  -1, &prev);

    if (proc == NULL) {
        res.app_status = BLE_HS_ENOENT;
        goto set_tk_return;
    }

    /* Do we already have a valid TK? */
    if (proc->flags & BLE_L2CAP_SM_PROC_F_TK_VALID) {
        res.app_status = BLE_HS_EALREADY;
        goto set_tk_return;
    }

    /* Is the response of the right type?  Must match what we asked for. */
    if (pkey->action != ble_l2cap_sm_passkey_action(proc)) {
        res.app_status = BLE_HS_EINVAL;
        goto set_tk_return;
    }

    /* Add the passkey range. */
    switch (pkey->action) {
    case BLE_GAP_PKACT_OOB:
        memcpy(proc->tk, pkey->oob, 16);
        res.sm_err = BLE_L2CAP_SM_ERR_OOB;
        break;

    case BLE_GAP_PKACT_INPUT:
    case BLE_GAP_PKACT_DISP:
        res.sm_err = BLE_L2CAP_SM_ERR_PASSKEY;
        if (pkey->passkey > 999999) {
            res.app_status = BLE_HS_EINVAL;
            goto set_tk_return;
        }
        memset(proc->tk, 0, 16);
        proc->tk[0] = (pkey->passkey >> 0) & 0xff;
        proc->tk[1] = (pkey->passkey >> 8) & 0xff;
        proc->tk[2] = (pkey->passkey >> 16) & 0xff;
        proc->tk[3] = (pkey->passkey >> 24) & 0xff;
        break;

    default:
        res.sm_err = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        res.app_status = BLE_HS_EINVAL;
        goto set_tk_return;
    }

    proc->flags |= BLE_L2CAP_SM_PROC_F_TK_VALID;

    /* If we are the initiator, its time to send the confirm. If we are the
     * responder, we check whether or not we received the confirm yet.
     */
    if (proc->flags & (BLE_L2CAP_SM_PROC_F_INITIATOR |
                       BLE_L2CAP_SM_PROC_F_RX_CONFIRM)) {

        ble_l2cap_sm_go(proc, &res, NULL);
        if (res.app_status != 0) {
            goto set_tk_return;
        }
    }

set_tk_return:
    ble_hs_unlock();

    ble_l2cap_sm_process_result(conn_handle, &res);
    return res.app_status;
}

void
ble_l2cap_sm_connection_broken(uint16_t conn_handle)
{
    struct ble_l2cap_sm_result res;

    memset(&res, 0, sizeof res);
    res.app_status = BLE_HS_ENOTCONN;
    res.do_cb = 1;

    ble_l2cap_sm_process_result(conn_handle, &res);
}

static void
ble_l2cap_sm_free_mem(void)
{
    free(ble_l2cap_sm_proc_mem);
    free(ble_l2cap_sm_key_mem);
}

int
ble_l2cap_sm_init(void)
{
    int rc;

    ble_l2cap_sm_free_mem();

    STAILQ_INIT(&ble_l2cap_sm_procs);

    if (ble_hs_cfg.max_l2cap_sm_procs > 0) {
        ble_l2cap_sm_proc_mem = malloc(
            OS_MEMPOOL_BYTES(ble_hs_cfg.max_l2cap_sm_procs,
                             sizeof (struct ble_l2cap_sm_proc)));
        if (ble_l2cap_sm_proc_mem == NULL) {
            rc = BLE_HS_ENOMEM;
            goto err;
        }
        rc = os_mempool_init(&ble_l2cap_sm_proc_pool,
                             ble_hs_cfg.max_l2cap_sm_procs,
                             sizeof (struct ble_l2cap_sm_proc),
                             ble_l2cap_sm_proc_mem,
                             "ble_l2cap_sm_proc_pool");
        if (rc != 0) {
            goto err;
        }

        ble_l2cap_sm_key_mem = malloc(
            OS_MEMPOOL_BYTES(ble_hs_cfg.max_l2cap_sm_procs * 2,
                             sizeof (struct ble_l2cap_sm_keys)));
        if (ble_l2cap_sm_key_mem == NULL) {
            rc = BLE_HS_ENOMEM;
            goto err;
        }
        rc = os_mempool_init(&ble_l2cap_sm_key_pool,
                             ble_hs_cfg.max_l2cap_sm_procs * 2,
                             sizeof (struct ble_l2cap_sm_keys),
                             ble_l2cap_sm_key_mem,
                             "ble_l2cap_sm_key_pool");
        if (rc != 0) {
            goto err;
        }
    }

    return 0;

err:
    ble_l2cap_sm_free_mem();
    return rc;
}

#endif
