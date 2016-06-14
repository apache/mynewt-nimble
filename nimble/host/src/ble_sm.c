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
#include "host/ble_sm.h"
#include "ble_hs_priv.h"

#if NIMBLE_OPT(SM)

/** Procedure timeout; 30 seconds. */
#define BLE_SM_TIMEOUT_OS_TICKS             (30 * OS_TICKS_PER_SEC)

STAILQ_HEAD(ble_sm_proc_list, ble_sm_proc);

typedef void ble_sm_rx_fn(uint16_t conn_handle, uint8_t op,
                          struct os_mbuf **om, struct ble_sm_result *res);

static ble_sm_rx_fn ble_sm_rx_noop;
static ble_sm_rx_fn ble_sm_pair_req_rx;
static ble_sm_rx_fn ble_sm_pair_rsp_rx;
static ble_sm_rx_fn ble_sm_confirm_rx;
static ble_sm_rx_fn ble_sm_random_rx;
static ble_sm_rx_fn ble_sm_fail_rx;
static ble_sm_rx_fn ble_sm_enc_info_rx;
static ble_sm_rx_fn ble_sm_master_id_rx;
static ble_sm_rx_fn ble_sm_id_info_rx;
static ble_sm_rx_fn ble_sm_id_addr_info_rx;
static ble_sm_rx_fn ble_sm_sign_info_rx;
static ble_sm_rx_fn ble_sm_sec_req_rx;

static ble_sm_rx_fn * const ble_sm_dispatch[] = {
   [BLE_SM_OP_PAIR_REQ] = ble_sm_pair_req_rx,
   [BLE_SM_OP_PAIR_RSP] = ble_sm_pair_rsp_rx,
   [BLE_SM_OP_PAIR_CONFIRM] = ble_sm_confirm_rx,
   [BLE_SM_OP_PAIR_RANDOM] = ble_sm_random_rx,
   [BLE_SM_OP_PAIR_FAIL] = ble_sm_fail_rx,
   [BLE_SM_OP_ENC_INFO] = ble_sm_enc_info_rx,
   [BLE_SM_OP_MASTER_ID] = ble_sm_master_id_rx,
   [BLE_SM_OP_IDENTITY_INFO] = ble_sm_id_info_rx,
   [BLE_SM_OP_IDENTITY_ADDR_INFO] = ble_sm_id_addr_info_rx,
   [BLE_SM_OP_SIGN_INFO] = ble_sm_sign_info_rx,
   [BLE_SM_OP_SEC_REQ] = ble_sm_sec_req_rx,
   [BLE_SM_OP_PAIR_KEYPRESS_NOTIFY] = ble_sm_rx_noop,
#if NIMBLE_OPT_SM_SC
   [BLE_SM_OP_PAIR_PUBLIC_KEY] = ble_sm_sc_public_key_rx,
   [BLE_SM_OP_PAIR_DHKEY_CHECK] = ble_sm_sc_dhkey_check_rx,
#else
   [BLE_SM_OP_PAIR_PUBLIC_KEY] = ble_sm_rx_noop,
   [BLE_SM_OP_PAIR_DHKEY_CHECK] = ble_sm_rx_noop,
#endif
};

typedef void ble_sm_state_fn(struct ble_sm_proc *proc,
                             struct ble_sm_result *res, void *arg);

static ble_sm_state_fn ble_sm_pair_exec;
static ble_sm_state_fn ble_sm_confirm_exec;
static ble_sm_state_fn ble_sm_random_exec;
static ble_sm_state_fn ble_sm_ltk_start_exec;
static ble_sm_state_fn ble_sm_ltk_restore_exec;
static ble_sm_state_fn ble_sm_enc_start_exec;
static ble_sm_state_fn ble_sm_enc_restore_exec;
static ble_sm_state_fn ble_sm_key_exch_exec;
static ble_sm_state_fn ble_sm_sec_req_exec;

static ble_sm_state_fn * const
ble_sm_state_dispatch[BLE_SM_PROC_STATE_CNT] = {
    [BLE_SM_PROC_STATE_PAIR]          = ble_sm_pair_exec,
    [BLE_SM_PROC_STATE_CONFIRM]       = ble_sm_confirm_exec,
    [BLE_SM_PROC_STATE_RANDOM]        = ble_sm_random_exec,
    [BLE_SM_PROC_STATE_LTK_START]     = ble_sm_ltk_start_exec,
    [BLE_SM_PROC_STATE_LTK_RESTORE]   = ble_sm_ltk_restore_exec,
    [BLE_SM_PROC_STATE_ENC_START]     = ble_sm_enc_start_exec,
    [BLE_SM_PROC_STATE_ENC_RESTORE]   = ble_sm_enc_restore_exec,
    [BLE_SM_PROC_STATE_KEY_EXCH]      = ble_sm_key_exch_exec,
    [BLE_SM_PROC_STATE_SEC_REQ]       = ble_sm_sec_req_exec,
#if NIMBLE_OPT_SM_SC
    [BLE_SM_PROC_STATE_PUBLIC_KEY]    = ble_sm_sc_public_key_exec,
    [BLE_SM_PROC_STATE_DHKEY_CHECK]   = ble_sm_sc_dhkey_check_exec,
#else
    [BLE_SM_PROC_STATE_PUBLIC_KEY]    = NULL,
    [BLE_SM_PROC_STATE_DHKEY_CHECK]   = NULL,
#endif
};

static void *ble_sm_proc_mem;
static struct os_mempool ble_sm_proc_pool;

/* Maintains the list of active security manager procedures. */
static struct ble_sm_proc_list ble_sm_procs;

static void ble_sm_pair_cfg(struct ble_sm_proc *proc);


/*****************************************************************************
 * $debug                                                                    *
 *****************************************************************************/

#ifdef BLE_HS_DEBUG

static uint8_t ble_sm_dbg_next_pair_rand[16];
static uint8_t ble_sm_dbg_next_pair_rand_set;
static uint16_t ble_sm_dbg_next_ediv;
static uint8_t ble_sm_dbg_next_ediv_set;
static uint64_t ble_sm_dbg_next_start_rand;
static uint8_t ble_sm_dbg_next_start_rand_set;
static uint8_t ble_sm_dbg_next_ltk[16];
static uint8_t ble_sm_dbg_next_ltk_set;
static uint8_t ble_sm_dbg_next_csrk[16];
static uint8_t ble_sm_dbg_next_csrk_set;
static uint8_t ble_sm_dbg_sc_pub_key[64];
static uint8_t ble_sm_dbg_sc_priv_key[32];
static uint8_t ble_sm_dbg_sc_keys_set;

void
ble_sm_dbg_set_next_pair_rand(uint8_t *next_pair_rand)
{
    memcpy(ble_sm_dbg_next_pair_rand, next_pair_rand,
           sizeof ble_sm_dbg_next_pair_rand);
    ble_sm_dbg_next_pair_rand_set = 1;
}

void
ble_sm_dbg_set_next_ediv(uint16_t next_ediv)
{
    ble_sm_dbg_next_ediv = next_ediv;
    ble_sm_dbg_next_ediv_set = 1;
}

void
ble_sm_dbg_set_next_start_rand(uint64_t next_start_rand)
{
    ble_sm_dbg_next_start_rand = next_start_rand;
    ble_sm_dbg_next_start_rand_set = 1;
}

void
ble_sm_dbg_set_next_ltk(uint8_t *next_ltk)
{
    memcpy(ble_sm_dbg_next_ltk, next_ltk,
           sizeof ble_sm_dbg_next_ltk);
    ble_sm_dbg_next_ltk_set = 1;
}

void
ble_sm_dbg_set_next_csrk(uint8_t *next_csrk)
{
    memcpy(ble_sm_dbg_next_csrk, next_csrk,
           sizeof ble_sm_dbg_next_csrk);
    ble_sm_dbg_next_csrk_set = 1;
}

void
ble_sm_dbg_set_sc_keys(uint8_t *pubkey, uint8_t *privkey)
{
    memcpy(ble_sm_dbg_sc_pub_key, pubkey,
           sizeof ble_sm_dbg_sc_pub_key);
    memcpy(ble_sm_dbg_sc_priv_key, privkey,
           sizeof ble_sm_dbg_sc_priv_key);
    ble_sm_dbg_sc_keys_set = 1;
}

int
ble_sm_dbg_num_procs(void)
{
    struct ble_sm_proc *proc;
    int cnt;

    cnt = 0;
    STAILQ_FOREACH(proc, &ble_sm_procs, next) {
        BLE_HS_DBG_ASSERT(cnt < ble_hs_cfg.max_l2cap_sm_procs);
        cnt++;
    }

    return cnt;
}

#endif

static void
ble_sm_dbg_assert_no_cycles(void)
{
#if BLE_HS_DEBUG
    ble_sm_dbg_num_procs();
#endif
}

static void
ble_sm_dbg_assert_not_inserted(struct ble_sm_proc *proc)
{
#if BLE_HS_DEBUG
    struct ble_sm_proc *cur;

    STAILQ_FOREACH(cur, &ble_sm_procs, next) {
        BLE_HS_DBG_ASSERT(cur != proc);
    }
#endif
}

/*****************************************************************************
 * $misc                                                                     *
 *****************************************************************************/

int
ble_sm_gen_pair_rand(uint8_t *pair_rand)
{
    int rc;

#ifdef BLE_HS_DEBUG
    if (ble_sm_dbg_next_pair_rand_set) {
        ble_sm_dbg_next_pair_rand_set = 0;
        memcpy(pair_rand, ble_sm_dbg_next_pair_rand,
               sizeof ble_sm_dbg_next_pair_rand);
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
ble_sm_gen_ediv(uint16_t *ediv)
{
    int rc;

#ifdef BLE_HS_DEBUG
    if (ble_sm_dbg_next_ediv_set) {
        ble_sm_dbg_next_ediv_set = 0;
        *ediv = ble_sm_dbg_next_ediv;
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
ble_sm_gen_start_rand(uint64_t *start_rand)
{
    int rc;

#ifdef BLE_HS_DEBUG
    if (ble_sm_dbg_next_start_rand_set) {
        ble_sm_dbg_next_start_rand_set = 0;
        *start_rand = ble_sm_dbg_next_start_rand;
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
ble_sm_gen_ltk(struct ble_sm_proc *proc, uint8_t *ltk)
{
    int rc;

#ifdef BLE_HS_DEBUG
    if (ble_sm_dbg_next_ltk_set) {
        ble_sm_dbg_next_ltk_set = 0;
        memcpy(ltk, ble_sm_dbg_next_ltk,
               sizeof ble_sm_dbg_next_ltk);
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
ble_sm_gen_csrk(struct ble_sm_proc *proc, uint8_t *csrk)
{
    int rc;

#ifdef BLE_HS_DEBUG
    if (ble_sm_dbg_next_csrk_set) {
        ble_sm_dbg_next_csrk_set = 0;
        memcpy(csrk, ble_sm_dbg_next_csrk,
               sizeof ble_sm_dbg_next_csrk);
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
ble_sm_gen_pub_priv(void *pub, uint32_t *priv)
{
    int rc;

#ifdef BLE_HS_DEBUG
    if (ble_sm_dbg_sc_keys_set) {
        ble_sm_dbg_sc_keys_set = 0;
        memcpy(pub, ble_sm_dbg_sc_pub_key, sizeof ble_sm_dbg_sc_pub_key);
        memcpy(priv, ble_sm_dbg_sc_priv_key, sizeof ble_sm_dbg_sc_priv_key);
        return 0;
    }
#endif

    rc = ble_sm_alg_gen_key_pair(pub, priv);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
ble_sm_proc_set_timer(struct ble_sm_proc *proc)
{
    /* Set a timeout of 30 seconds. */
    proc->exp_os_ticks = os_time_get() + BLE_SM_TIMEOUT_OS_TICKS;
}

static ble_sm_rx_fn *
ble_sm_dispatch_get(uint8_t op)
{
    if (op >= sizeof ble_sm_dispatch / sizeof ble_sm_dispatch[0]) {
        return NULL;
    }

    return ble_sm_dispatch[op];
}

/**
 * Allocates a proc entry.
 *
 * @return                      An entry on success; null on failure.
 */
static struct ble_sm_proc *
ble_sm_proc_alloc(void)
{
    struct ble_sm_proc *proc;

    proc = os_memblock_get(&ble_sm_proc_pool);
    if (proc != NULL) {
        memset(proc, 0, sizeof *proc);
    }

    return proc;
}

/**
 * Frees the specified proc entry.  No-state if passed a null pointer.
 */
static void
ble_sm_proc_free(struct ble_sm_proc *proc)
{
    int rc;

    if (proc != NULL) {
        ble_sm_dbg_assert_not_inserted(proc);

        rc = os_memblock_put(&ble_sm_proc_pool, proc);
        BLE_HS_DBG_ASSERT_EVAL(rc == 0);
    }
}

static void
ble_sm_proc_remove(struct ble_sm_proc *proc,
                         struct ble_sm_proc *prev)
{
    if (prev == NULL) {
        BLE_HS_DBG_ASSERT(STAILQ_FIRST(&ble_sm_procs) == proc);
        STAILQ_REMOVE_HEAD(&ble_sm_procs, next);
    } else {
        BLE_HS_DBG_ASSERT(STAILQ_NEXT(prev, next) == proc);
        STAILQ_REMOVE_AFTER(&ble_sm_procs, prev, next);
    }

    ble_sm_dbg_assert_no_cycles();
}

static void
ble_sm_update_sec_state(uint16_t conn_handle, int encrypted,
                        int authenticated, int bonded)
{
    struct ble_hs_conn *conn;

    conn = ble_hs_conn_find(conn_handle);
    if (conn != NULL) {
        conn->bhc_sec_state.encrypted = encrypted;

        /* Authentication and bonding are never revoked from a secure link */
        if (authenticated) {
            conn->bhc_sec_state.authenticated = 1;
        }
        if (bonded) {
            conn->bhc_sec_state.bonded = 1;
        }
    }
}

static void
ble_sm_fill_store_value(uint8_t peer_addr_type, uint8_t *peer_addr,
                        int authenticated,
                        struct ble_sm_keys *keys,
                        struct ble_store_value_sec *value_sec)
{
    memset(value_sec, 0, sizeof *value_sec);

    value_sec->peer_addr_type = peer_addr_type;
    memcpy(value_sec->peer_addr, peer_addr, sizeof value_sec->peer_addr);

    if (keys->ediv_rand_valid && keys->ltk_valid) {
        value_sec->ediv = keys->ediv;
        value_sec->rand_num = keys->rand_val;

        memcpy(value_sec->ltk, keys->ltk, sizeof value_sec->ltk);
        value_sec->ltk_present = 1;

        value_sec->authenticated = !!authenticated;
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

int
ble_sm_peer_addr(struct ble_sm_proc *proc,
                 uint8_t *out_type, uint8_t **out_addr)
{
    struct ble_hs_conn *conn;

    conn = ble_hs_conn_find(proc->conn_handle);
    if (conn == NULL) {
        return BLE_HS_ENOTCONN;
    }

    *out_type = conn->bhc_addr_type;
    *out_addr = conn->bhc_addr;

    return 0;
}

int
ble_sm_ia_ra(struct ble_sm_proc *proc,
             uint8_t *out_iat, uint8_t *out_ia,
             uint8_t *out_rat, uint8_t *out_ra)
{
    struct ble_hs_conn_addrs addrs;
    struct ble_hs_conn *conn;

    conn = ble_hs_conn_find(proc->conn_handle);
    if (conn == NULL) {
        return BLE_HS_ENOTCONN;
    }

    ble_hs_conn_addrs(conn, &addrs);

    if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
        *out_iat = addrs.our_id_addr_type;
        memcpy(out_ia, addrs.our_ota_addr, 6);

        *out_rat = addrs.peer_id_addr_type;
        memcpy(out_ra, addrs.peer_ota_addr, 6);
    } else {
        *out_iat = addrs.peer_id_addr_type;
        memcpy(out_ia, addrs.peer_ota_addr, 6);

        *out_rat = addrs.our_id_addr_type;
        memcpy(out_ra, addrs.our_ota_addr, 6);
    }

    return 0;
}

static void
ble_sm_persist_keys(struct ble_sm_proc *proc)
{
    struct ble_store_value_sec value_sec;
    struct ble_hs_conn *conn;
    uint8_t peer_addr[8];
    uint8_t peer_addr_type;
    int authenticated;

    ble_hs_lock();

    conn = ble_hs_conn_find(proc->conn_handle);
    BLE_HS_DBG_ASSERT(conn != NULL);

    /* If we got an identity address, use that for key storage. */
    if (proc->peer_keys.addr_valid) {
        peer_addr_type = proc->peer_keys.addr_type;
        memcpy(peer_addr, proc->peer_keys.addr, sizeof peer_addr);
    } else {
        peer_addr_type = ble_hs_misc_addr_type_to_ident(conn->bhc_addr_type);
        memcpy(peer_addr, conn->bhc_addr, sizeof peer_addr);
    }

    ble_hs_unlock();

    authenticated = proc->flags & BLE_SM_PROC_F_AUTHENTICATED;

    ble_sm_fill_store_value(peer_addr_type, peer_addr, authenticated,
                            &proc->our_keys, &value_sec);
    ble_store_write_our_sec(&value_sec);

    ble_sm_fill_store_value(peer_addr_type, peer_addr, authenticated,
                            &proc->peer_keys, &value_sec);
    ble_store_write_peer_sec(&value_sec);
}

static int
ble_sm_proc_matches(struct ble_sm_proc *proc, uint16_t conn_handle,
                    uint8_t state, int is_initiator)
{
    int proc_is_initiator;

    if (conn_handle != proc->conn_handle) {
        return 0;
    }

    if (state != BLE_SM_PROC_STATE_NONE && state != proc->state) {
        return 0;
    }

    proc_is_initiator = !!(proc->flags & BLE_SM_PROC_F_INITIATOR);
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
struct ble_sm_proc *
ble_sm_proc_find(uint16_t conn_handle, uint8_t state, int is_initiator,
                 struct ble_sm_proc **out_prev)
{
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;

    BLE_HS_DBG_ASSERT(ble_hs_thread_safe());

    prev = NULL;
    STAILQ_FOREACH(proc, &ble_sm_procs, next) {
        if (ble_sm_proc_matches(proc, conn_handle, state, is_initiator)) {
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
ble_sm_insert(struct ble_sm_proc *proc)
{
#ifdef BLE_HS_DEBUG
    struct ble_sm_proc *cur;

    STAILQ_FOREACH(cur, &ble_sm_procs, next) {
        BLE_HS_DBG_ASSERT(cur != proc);
    }
#endif

    STAILQ_INSERT_HEAD(&ble_sm_procs, proc, next);
}

static void
ble_sm_extract_expired(struct ble_sm_proc_list *dst_list)
{
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;
    struct ble_sm_proc *next;
    uint32_t now;
    int32_t time_diff;

    now = os_time_get();
    STAILQ_INIT(dst_list);

    ble_hs_lock();

    prev = NULL;
    proc = STAILQ_FIRST(&ble_sm_procs);
    while (proc != NULL) {
        next = STAILQ_NEXT(proc, next);

        time_diff = now - proc->exp_os_ticks;
        if (time_diff >= 0) {
            if (prev == NULL) {
                STAILQ_REMOVE_HEAD(&ble_sm_procs, next);
            } else {
                STAILQ_REMOVE_AFTER(&ble_sm_procs, prev, next);
            }
            STAILQ_INSERT_HEAD(dst_list, proc, next);
        }

        prev = proc;
        proc = next;
    }

    ble_sm_dbg_assert_no_cycles();

    ble_hs_unlock();
}

static void
ble_sm_rx_noop(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
               struct ble_sm_result *res)
{
    res->app_status = BLE_HS_SM_US_ERR(BLE_SM_ERR_CMD_NOT_SUPP);
    res->sm_err = BLE_SM_ERR_CMD_NOT_SUPP;
}

uint8_t
ble_sm_build_authreq(void)
{
    return ble_hs_cfg.sm_bonding << 0  |
           ble_hs_cfg.sm_mitm << 2     |
           ble_hs_cfg.sm_sc << 3       |
           ble_hs_cfg.sm_keypress << 4;
}

static int
ble_sm_io_action(struct ble_sm_proc *proc)
{
    if (proc->flags & BLE_SM_PROC_F_SC) {
        return ble_sm_sc_io_action(proc);
    } else {
        return ble_sm_lgcy_io_action(proc);
    }
}

int
ble_sm_ioact_state(uint8_t action)
{
    switch (action) {
    case BLE_SM_IOACT_NONE:
        return BLE_SM_PROC_STATE_NONE;

    case BLE_SM_IOACT_NUMCMP:
        return BLE_SM_PROC_STATE_DHKEY_CHECK;

    case BLE_SM_IOACT_OOB:
    case BLE_SM_IOACT_INPUT:
    case BLE_SM_IOACT_DISP:
        return BLE_SM_PROC_STATE_CONFIRM;

    default:
        BLE_HS_DBG_ASSERT(0);
        return BLE_SM_PROC_STATE_NONE;
    }
}

int
ble_sm_proc_can_advance(struct ble_sm_proc *proc)
{
    uint8_t ioact;

    ioact = ble_sm_sc_io_action(proc);
    if (ble_sm_ioact_state(ioact) != proc->state) {
        return 1;
    }

    if (proc->flags & BLE_SM_PROC_F_IO_INJECTED &&
        proc->flags & BLE_SM_PROC_F_ADVANCE_ON_IO) {

        return 1;
    }

    return 0;
}

static void
ble_sm_exec(struct ble_sm_proc *proc, struct ble_sm_result *res, void *arg)
{
    ble_sm_state_fn *cb;

    BLE_HS_DBG_ASSERT(proc->state < BLE_SM_PROC_STATE_CNT);
    cb = ble_sm_state_dispatch[proc->state];
    BLE_HS_DBG_ASSERT(cb != NULL);

    memset(res, 0, sizeof *res);

    cb(proc, res, arg);
}

void
ble_sm_process_result(uint16_t conn_handle, struct ble_sm_result *res)
{
    struct ble_sm_proc *prev;
    struct ble_sm_proc *proc;
    int rm;

    rm = 0;

    while (1) {
        ble_hs_lock();
        proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_NONE, -1,
                                &prev);

        if (proc != NULL) {
            if (res->execute) {
                ble_sm_exec(proc, res, res->state_arg);
            }

            if (res->app_status != 0) {
                rm = 1;
            }

            if (proc->state == BLE_SM_PROC_STATE_NONE) {
                rm = 1;
            }

            if (rm) {
                ble_sm_proc_remove(proc, prev);
            } else {
                ble_sm_proc_set_timer(proc);
            }
        }

        if (res->sm_err != 0) {
            ble_sm_pair_fail_tx(conn_handle, res->sm_err);
        }

        ble_hs_unlock();

        if (proc == NULL) {
            break;
        }

        if (res->enc_cb) {
            BLE_HS_DBG_ASSERT(proc == NULL || rm);
            ble_gap_enc_event(conn_handle, res->app_status, res->restore);
        }

        if (res->app_status == 0 &&
            res->passkey_action.action != BLE_SM_IOACT_NONE) {

            ble_gap_passkey_event(conn_handle, &res->passkey_action);
        }

        /* Persist keys if bonding has successfully completed. */
        if (res->app_status == 0    &&
            rm                      &&
            proc->flags & BLE_SM_PROC_F_BONDING) {

            ble_sm_persist_keys(proc);
        }

        if (rm) {
            ble_sm_proc_free(proc);
            break;
        }

        if (!res->execute) {
            break;
        }

        memset(res, 0, sizeof *res);
        res->execute = 1;
    }
}

static void
ble_sm_key_dist(struct ble_sm_proc *proc,
                uint8_t *out_init_key_dist, uint8_t *out_resp_key_dist)
{
    *out_init_key_dist = proc->pair_rsp.init_key_dist;
    *out_resp_key_dist = proc->pair_rsp.resp_key_dist;

    /* Encryption info and master ID are only sent in legacy pairing. */
    if (proc->flags & BLE_SM_PROC_F_SC) {
        *out_init_key_dist &= ~BLE_SM_PAIR_KEY_DIST_ENC;
        *out_resp_key_dist &= ~BLE_SM_PAIR_KEY_DIST_ENC;
    }
}

/*****************************************************************************
 * $enc                                                                      *
 *****************************************************************************/

static int
ble_sm_start_encrypt_tx(struct hci_start_encrypt *cmd)
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
ble_sm_enc_start_exec(struct ble_sm_proc *proc, struct ble_sm_result *res,
                      void *arg)
{
    struct hci_start_encrypt cmd;
    int rc;

    BLE_HS_DBG_ASSERT(proc->flags & BLE_SM_PROC_F_INITIATOR);

    cmd.connection_handle = proc->conn_handle;
    cmd.encrypted_diversifier = 0;
    cmd.random_number = 0;
    memcpy(cmd.long_term_key, proc->ltk, sizeof cmd.long_term_key);

    rc = ble_sm_start_encrypt_tx(&cmd);
    if (rc != 0) {
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->app_status = rc;
        res->enc_cb = 1;
    }
}

static void
ble_sm_enc_restore_exec(struct ble_sm_proc *proc, struct ble_sm_result *res,
                        void *arg)
{
    struct hci_start_encrypt *cmd;

    BLE_HS_DBG_ASSERT(proc->flags & BLE_SM_PROC_F_INITIATOR);

    cmd = arg;
    BLE_HS_DBG_ASSERT(cmd != NULL);

    res->app_status = ble_sm_start_encrypt_tx(cmd);
}

static void
ble_sm_enc_event_rx(uint16_t conn_handle, uint8_t evt_status, int encrypted)
{
    struct ble_sm_result res;
    struct ble_sm_proc *proc;
    int authenticated;
    int bonded;

    memset(&res, 0, sizeof res);

    /* Assume no change in authenticated and bonded statuses. */
    authenticated = 0;
    bonded = 0;

    ble_hs_lock();

    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_NONE, -1, NULL);
    if (proc != NULL) {
        switch (proc->state) {
        case BLE_SM_PROC_STATE_ENC_START:
            /* We are completing a pairing procedure; keys may need to be
             * exchanged.
             */
            if (evt_status == 0) {
                /* If the responder has any keys to send, it sends them
                 * first.
                 */
                proc->state = BLE_SM_PROC_STATE_KEY_EXCH;
                if (!(proc->flags & BLE_SM_PROC_F_INITIATOR) ||
                    proc->rx_key_flags == 0) {

                    res.execute = 1;
                }
            } else {
                /* Failure or no keys to exchange; procedure is complete. */
                proc->state = BLE_SM_PROC_STATE_NONE;
            }
            if (proc->flags & BLE_SM_PROC_F_AUTHENTICATED) {
                authenticated = 1;
            }
            break;

        case BLE_SM_PROC_STATE_ENC_RESTORE:
            /* A secure link is being restored via the encryption
             * procedure.  Keys were exchanged during pairing; they don't
             * get exchanged again now.  Procedure is complete.
             */
            BLE_HS_DBG_ASSERT(proc->rx_key_flags == 0);
            proc->state = BLE_SM_PROC_STATE_NONE;
            if (proc->flags & BLE_SM_PROC_F_AUTHENTICATED) {
                authenticated = 1;
            }
            bonded = 1;
            res.restore = 1;
            break;

        default:
            /* The encryption change event is unexpected.  We take the
             * controller at its word that the state has changed and we
             * terminate the procedure.
             */
            proc->state = BLE_SM_PROC_STATE_NONE;
            res.sm_err = BLE_SM_ERR_UNSPECIFIED;
            break;
        }
    }

    if (evt_status == 0) {
        /* Set the encrypted state of the connection as indicated in the
         * event.
         */
        ble_sm_update_sec_state(conn_handle, encrypted, authenticated, bonded);
    }

    /* Unless keys need to be exchanged, notify the application of the security
     * change.  If key exchange is pending, the application callback is
     * triggered after exchange completes.
     */
    if (proc == NULL || proc->state == BLE_SM_PROC_STATE_NONE) {
        res.enc_cb = 1;
        res.app_status = BLE_HS_HCI_ERR(evt_status);
    }

    ble_hs_unlock();

    ble_sm_process_result(conn_handle, &res);
}

void
ble_sm_enc_change_rx(struct hci_encrypt_change *evt)
{
    /* For encrypted state: read LE-encryption bit; ignore BR/EDR and reserved
     * bits.
     */
    ble_sm_enc_event_rx(evt->connection_handle, evt->status,
                        evt->encryption_enabled & 0x01);
}

void
ble_sm_enc_key_refresh_rx(struct hci_encrypt_key_refresh *evt)
{
    ble_sm_enc_event_rx(evt->connection_handle, evt->status, 1);
}

/*****************************************************************************
 * $ltk                                                                      *
 *****************************************************************************/

static int
ble_sm_retrieve_ltk(struct hci_le_lt_key_req *evt, uint8_t peer_addr_type,
                    uint8_t *peer_addr, struct ble_store_value_sec *value_sec)
{
    struct ble_store_key_sec key_sec;
    int rc;

    /* Tell applicaiton to look up LTK by peer address and ediv/rand pair. */
    memset(&key_sec, 0, sizeof key_sec);
    key_sec.peer_addr_type = peer_addr_type;
    memcpy(key_sec.peer_addr, peer_addr, 6);
    key_sec.ediv = evt->encrypted_diversifier;
    key_sec.rand_num = evt->random_number;
    key_sec.ediv_rand_present = 1;

    rc = ble_store_read_our_sec(&key_sec, value_sec);
    return rc;
}

static int
ble_sm_ltk_req_reply_tx(uint16_t conn_handle, uint8_t *ltk)
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
ble_sm_ltk_req_neg_reply_tx(uint16_t conn_handle)
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
ble_sm_ltk_start_exec(struct ble_sm_proc *proc, struct ble_sm_result *res,
                      void *arg)
{
    BLE_HS_DBG_ASSERT(!(proc->flags & BLE_SM_PROC_F_INITIATOR));

    res->app_status = ble_sm_ltk_req_reply_tx(proc->conn_handle, proc->ltk);
    if (res->app_status == 0) {
        proc->state = BLE_SM_PROC_STATE_ENC_START;
    } else {
        res->enc_cb = 1;
    }
}

static void
ble_sm_ltk_restore_exec(struct ble_sm_proc *proc, struct ble_sm_result *res,
                        void *arg)
{
    struct ble_store_value_sec *value_sec;

    BLE_HS_DBG_ASSERT(!(proc->flags & BLE_SM_PROC_F_INITIATOR));

    value_sec = arg;

    if (value_sec != NULL) {
        /* Store provided a key; send it to the controller. */
        res->app_status = ble_sm_ltk_req_reply_tx(
            proc->conn_handle, value_sec->ltk);

        if (res->app_status == 0) {
            if (value_sec->authenticated) {
                proc->flags |= BLE_SM_PROC_F_AUTHENTICATED;
            }
        } else {
            /* Notify the app if it provided a key and the procedure failed. */
            res->enc_cb = 1;
        }
    } else {
        /* Application does not have the requested key in its database.  Send a
         * negative reply to the controller.
         */
        ble_sm_ltk_req_neg_reply_tx(proc->conn_handle);
        res->app_status = BLE_HS_ENOENT;
    }

    if (res->app_status == 0) {
        proc->state = BLE_SM_PROC_STATE_ENC_RESTORE;
    }
}

int
ble_sm_ltk_req_rx(struct hci_le_lt_key_req *evt)
{
    struct ble_store_value_sec value_sec;
    struct ble_hs_conn_addrs addrs;
    struct ble_sm_result res;
    struct ble_sm_proc *proc;
    struct ble_hs_conn *conn;
    uint8_t peer_id_addr[6];
    int store_rc;
    int restore;

    memset(&res, 0, sizeof res);

    ble_hs_lock();
    proc = ble_sm_proc_find(evt->connection_handle, BLE_SM_PROC_STATE_NONE,
                            0, NULL);
    if (proc == NULL) {
        /* The peer is attempting to restore a encrypted connection via the
         * encryption procedure.  Create a proc entry to indicate that security
         * establishment is in progress and execute the procedure after the
         * mutex gets unlocked.
         */
        restore = 1;
        proc = ble_sm_proc_alloc();
        if (proc == NULL) {
            res.app_status = BLE_HS_ENOMEM;
        } else {
            proc->conn_handle = evt->connection_handle;
            proc->state = BLE_SM_PROC_STATE_LTK_RESTORE;
            ble_sm_insert(proc);

            res.execute = 1;
        }
    } else if (proc->state == BLE_SM_PROC_STATE_SEC_REQ) {
        /* Same as above, except we solicited the encryption procedure by
         * sending a security request.
         */
        restore = 1;
        proc->state = BLE_SM_PROC_STATE_LTK_RESTORE;
        res.execute = 1;
    } else if (proc->state == BLE_SM_PROC_STATE_LTK_START) {
        /* Legacy pairing just completed.  Send the short term key to the
         * controller.
         */
        restore = 0;
        res.execute = 1;
    } else {
        /* The request is unexpected; nack and forget. */
        restore = 0;
        ble_sm_ltk_req_neg_reply_tx(evt->connection_handle);
        proc = NULL;
    }

    if (restore) {
        conn = ble_hs_conn_find(evt->connection_handle);
        if (conn == NULL) {
            res.app_status = BLE_HS_ENOTCONN;
        } else {
            ble_hs_conn_addrs(conn, &addrs);
            memcpy(peer_id_addr, addrs.peer_id_addr, 6);
        }
    }

    ble_hs_unlock();

    if (proc == NULL) {
        return res.app_status;
    }

    if (res.app_status == 0) {
        if (restore) {
            store_rc = ble_sm_retrieve_ltk(evt, addrs.peer_id_addr_type,
                                           peer_id_addr, &value_sec);
            if (store_rc == 0) {
                /* Send the key to the controller. */
                res.state_arg = &value_sec;
            } else {
                /* Send a nack to the controller. */
                res.state_arg = NULL;
            }
        }
    }

    ble_sm_process_result(evt->connection_handle, &res);

    return 0;
}

/*****************************************************************************
 * $random                                                                   *
 *****************************************************************************/

uint8_t *
ble_sm_our_pair_rand(struct ble_sm_proc *proc)
{
    if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
        return proc->randm;
    } else {
        return proc->rands;
    }
}

uint8_t *
ble_sm_peer_pair_rand(struct ble_sm_proc *proc)
{
    if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
        return proc->rands;
    } else {
        return proc->randm;
    }
}

static void
ble_sm_random_exec(struct ble_sm_proc *proc, struct ble_sm_result *res,
                   void *arg)
{
    if (proc->flags & BLE_SM_PROC_F_SC) {
        ble_sm_sc_random_exec(proc, res);
    } else {
        ble_sm_lgcy_random_exec(proc, res);
    }
}

static void
ble_sm_random_rx(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
                 struct ble_sm_result *res)
{
    struct ble_sm_pair_random cmd;
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;

    res->app_status = ble_hs_misc_pullup_base(om, BLE_SM_PAIR_RANDOM_SZ);
    if (res->app_status != 0) {
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    ble_sm_pair_random_parse((*om)->om_data, (*om)->om_len, &cmd);
    BLE_SM_LOG_CMD(0, "random", conn_handle, ble_sm_pair_random_log, &cmd);

    ble_hs_lock();
    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_RANDOM, -1, &prev);
    if (proc == NULL) {
        res->app_status = BLE_HS_ENOENT;
    } else {
        memcpy(ble_sm_peer_pair_rand(proc), cmd.value, 16);

        if (proc->flags & BLE_SM_PROC_F_SC) {
            ble_sm_sc_random_rx(proc, res);
        } else {
            ble_sm_lgcy_random_rx(proc, res);
        }
    }
    ble_hs_unlock();
}

/*****************************************************************************
 * $confirm                                                                  *
 *****************************************************************************/

static void
ble_sm_confirm_exec(struct ble_sm_proc *proc, struct ble_sm_result *res,
                    void *arg)
{
    if (!(proc->flags & BLE_SM_PROC_F_SC)) {
        ble_sm_lgcy_confirm_exec(proc, res);
    } else {
        ble_sm_sc_confirm_exec(proc, res);
    }
}

static void
ble_sm_confirm_rx(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
                  struct ble_sm_result *res)
{
    struct ble_sm_pair_confirm cmd;
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;
    uint8_t ioact;

    res->app_status = ble_hs_misc_pullup_base(om, BLE_SM_PAIR_CONFIRM_SZ);
    if (res->app_status != 0) {
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    ble_sm_pair_confirm_parse((*om)->om_data, (*om)->om_len, &cmd);
    BLE_SM_LOG_CMD(0, "confirm", conn_handle, ble_sm_pair_confirm_log, &cmd);

    ble_hs_lock();
    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_CONFIRM, -1, &prev);
    if (proc == NULL) {
        res->app_status = BLE_HS_ENOENT;
    } else {
        memcpy(proc->confirm_peer, cmd.value, 16);

        if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
            proc->state = BLE_SM_PROC_STATE_RANDOM;
            res->execute = 1;
        } else {
            ioact = ble_sm_io_action(proc);
            if (ble_sm_ioact_state(ioact) == proc->state) {
                proc->flags |= BLE_SM_PROC_F_ADVANCE_ON_IO;
            }
            if (ble_sm_proc_can_advance(proc)) {
                res->execute = 1;
            }
        }
    }
    ble_hs_unlock();
}

/*****************************************************************************
 * $pair                                                                     *
 *****************************************************************************/

static uint8_t
ble_sm_state_after_pair(struct ble_sm_proc *proc)
{
    if (proc->flags & BLE_SM_PROC_F_SC) {
        return BLE_SM_PROC_STATE_PUBLIC_KEY;
    } else {
        return BLE_SM_PROC_STATE_CONFIRM;
    }
}

static void
ble_sm_pair_cfg(struct ble_sm_proc *proc)
{
    uint8_t init_key_dist;
    uint8_t resp_key_dist;
    uint8_t rx_key_dist;

    if (proc->pair_req.authreq & BLE_SM_PAIR_AUTHREQ_SC &&
        proc->pair_rsp.authreq & BLE_SM_PAIR_AUTHREQ_SC) {

        proc->flags |= BLE_SM_PROC_F_SC;
    }

    if (proc->pair_req.authreq & BLE_SM_PAIR_AUTHREQ_BOND &&
        proc->pair_rsp.authreq & BLE_SM_PAIR_AUTHREQ_BOND) {

        proc->flags |= BLE_SM_PROC_F_BONDING;
    }

    ble_sm_key_dist(proc, &init_key_dist, &resp_key_dist);
    if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
        rx_key_dist = resp_key_dist;
    } else {
        rx_key_dist = init_key_dist;
    }

    proc->rx_key_flags = 0;
    if (rx_key_dist & BLE_SM_PAIR_KEY_DIST_ENC) {
        proc->rx_key_flags |= BLE_SM_KE_F_ENC_INFO |
                              BLE_SM_KE_F_MASTER_ID;
    }
    if (rx_key_dist & BLE_SM_PAIR_KEY_DIST_ID) {
        proc->rx_key_flags |= BLE_SM_KE_F_ID_INFO |
                              BLE_SM_KE_F_ADDR_INFO;
    }
    if (rx_key_dist & BLE_SM_PAIR_KEY_DIST_SIGN) {
        proc->rx_key_flags |= BLE_SM_KE_F_SIGN_INFO;
    }
}

static void
ble_sm_pair_exec(struct ble_sm_proc *proc, struct ble_sm_result *res,
                 void *arg)
{
    struct ble_sm_pair_cmd cmd;
    uint8_t ioact;
    int is_req;
    int rc;

    is_req = proc->flags & BLE_SM_PROC_F_INITIATOR;

    cmd.io_cap = ble_hs_cfg.sm_io_cap;
    cmd.oob_data_flag = ble_hs_cfg.sm_oob_data_flag;
    cmd.authreq = ble_sm_build_authreq();
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

    rc = ble_sm_pair_cmd_tx(proc->conn_handle, is_req, &cmd);
    if (rc != 0) {
        goto err;
    }

    if (is_req) {
        proc->pair_req = cmd;
    } else {
        proc->pair_rsp = cmd;

        ble_sm_pair_cfg(proc);
        proc->state = ble_sm_state_after_pair(proc);

        ioact = ble_sm_io_action(proc);
        if (ble_sm_ioact_state(ioact) == proc->state) {
            res->passkey_action.action = ioact;
        }
    }

    res->app_status = ble_sm_gen_pair_rand(ble_sm_our_pair_rand(proc));
    if (res->app_status != 0) {
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    return;

err:
    res->app_status = rc;

    if (!is_req) {
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
    }
}

static void
ble_sm_pair_req_rx(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
                   struct ble_sm_result *res)
{
    struct ble_sm_pair_cmd req;
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;
    struct ble_hs_conn *conn;

    res->app_status = ble_hs_misc_pullup_base(om, BLE_SM_PAIR_CMD_SZ);
    if (res->app_status != 0) {
        return;
    }

    ble_sm_pair_cmd_parse((*om)->om_data, (*om)->om_len, &req);
    BLE_SM_LOG_CMD(0, "pair req", conn_handle, ble_sm_pair_cmd_log, &req);

    ble_hs_lock();

    /* XXX: Check connection state; reject if not appropriate. */
    /* XXX: Ensure enough time has passed since the previous failed pairing
     * attempt.
     */
    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_NONE, -1, &prev);
    if (proc != NULL) {
        /* Pairing already in progress; abort old procedure and start new. */
        /* XXX: Check the spec on this. */
        ble_sm_proc_remove(proc, prev);
        ble_sm_proc_free(proc);
    }

    proc = ble_sm_proc_alloc();
    if (proc != NULL) {
        proc->conn_handle = conn_handle;
        proc->state = BLE_SM_PROC_STATE_PAIR;
        ble_sm_insert(proc);

        proc->pair_req = req;

        conn = ble_hs_conn_find(proc->conn_handle);
        if (conn == NULL) {
            res->sm_err = BLE_SM_ERR_UNSPECIFIED;
            res->app_status = BLE_HS_ENOTCONN;
        } else if (conn->bhc_flags & BLE_HS_CONN_F_MASTER) {
            res->sm_err = BLE_SM_ERR_CMD_NOT_SUPP;
            res->app_status = BLE_HS_SM_US_ERR(BLE_SM_ERR_CMD_NOT_SUPP);
        } else if (!ble_sm_pair_cmd_is_valid(&req)) {
            res->sm_err = BLE_SM_ERR_INVAL;
            res->app_status =  BLE_HS_SM_US_ERR(BLE_SM_ERR_INVAL);
        } else {
            res->execute = 1;
        }
    }

    ble_hs_unlock();
}

static void
ble_sm_pair_rsp_rx(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
                   struct ble_sm_result *res)
{
    struct ble_sm_pair_cmd rsp;
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;
    uint8_t ioact;

    res->app_status = ble_hs_misc_pullup_base(om, BLE_SM_PAIR_CMD_SZ);
    if (res->app_status != 0) {
        res->enc_cb = 1;
        return;
    }

    ble_sm_pair_cmd_parse((*om)->om_data, (*om)->om_len, &rsp);
    BLE_SM_LOG_CMD(0, "pair rsp", conn_handle, ble_sm_pair_cmd_log, &rsp);

    ble_hs_lock();
    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_PAIR, 1, &prev);
    if (proc != NULL) {
        proc->pair_rsp = rsp;
        if (!ble_sm_pair_cmd_is_valid(&rsp)) {
            res->sm_err = BLE_SM_ERR_INVAL;
            res->app_status = BLE_HS_SM_US_ERR(BLE_SM_ERR_INVAL);
        } else {
            ble_sm_pair_cfg(proc);

            ioact = ble_sm_io_action(proc);
            if (ble_sm_ioact_state(ioact) == proc->state) {
                res->passkey_action.action = ioact;
            } else {
                proc->state = ble_sm_state_after_pair(proc);
                res->execute = 1;
            }
        }
    }

    ble_hs_unlock();
}

/*****************************************************************************
 * $security request                                                         *
 *****************************************************************************/

static void
ble_sm_sec_req_exec(struct ble_sm_proc *proc, struct ble_sm_result *res,
                    void *arg)
{
    struct ble_sm_sec_req cmd;
    int rc;

    cmd.authreq = ble_sm_build_authreq();
    rc = ble_sm_sec_req_tx(proc->conn_handle, &cmd);
    if (rc != 0) {
        res->app_status = rc;
        return;
    }
}

static void
ble_sm_sec_req_rx(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
                  struct ble_sm_result *res)
{
    struct ble_store_value_sec value_sec;
    struct ble_store_key_sec key_sec;
    struct ble_hs_conn_addrs addrs;
    struct ble_sm_sec_req cmd;
    struct ble_hs_conn *conn;
    int authreq_mitm;

    res->app_status = ble_hs_misc_pullup_base(om, BLE_SM_SEC_REQ_SZ);
    if (res->app_status != 0) {
        return;
    }

    ble_sm_sec_req_parse((*om)->om_data, (*om)->om_len, &cmd);
    BLE_SM_LOG_CMD(0, "sec req", conn_handle, ble_sm_sec_req_log, &cmd);

    /* XXX: Reject if:
     *     o authreq-bonded flag not set?
     *     o authreq-reserved flags set?
     */

    ble_hs_lock();

    conn = ble_hs_conn_find(conn_handle);
    if (conn == NULL) {
        res->app_status = BLE_HS_ENOTCONN;
    } else if (!(conn->bhc_flags & BLE_HS_CONN_F_MASTER)) {
        res->app_status = BLE_HS_SM_US_ERR(BLE_SM_ERR_CMD_NOT_SUPP);
        res->sm_err = BLE_SM_ERR_CMD_NOT_SUPP;
    } else {
        /* We will be querying the SM database for a key corresponding to the
         * sender; remember the sender's address while the connection list is
         * locked.
         */
        ble_hs_conn_addrs(conn, &addrs);
        memset(&key_sec, 0, sizeof key_sec);
        key_sec.peer_addr_type = addrs.peer_id_addr_type;
        memcpy(key_sec.peer_addr, addrs.peer_id_addr, 6);
    }

    ble_hs_unlock();

    if (res->app_status == 0) {
        /* Query database for an LTK corresponding to the sender.  We are the
         * master, so retrieve a master key.
         */
        res->app_status = ble_store_read_peer_sec(&key_sec, &value_sec);
        if (res->app_status == 0) {
            /* Found a key corresponding to this peer.  Make sure it meets the
             * requested minimum authreq.
             */
            authreq_mitm = cmd.authreq & BLE_SM_PAIR_AUTHREQ_MITM;
            if ((!authreq_mitm && value_sec.authenticated) ||
                (authreq_mitm && !value_sec.authenticated)) {

                res->app_status = BLE_HS_EREJECT;
            }
        }

        if (res->app_status == 0) {
            res->app_status = ble_sm_enc_initiate(conn_handle, value_sec.ltk,
                                                  value_sec.ediv,
                                                  value_sec.rand_num,
                                                  value_sec.authenticated);
        } else {
            res->app_status = ble_sm_pair_initiate(conn_handle);
        }
    }
}

/*****************************************************************************
 * $key exchange                                                             *
 *****************************************************************************/

static void
ble_sm_key_exch_success(struct ble_sm_proc *proc, struct ble_sm_result *res)
{
    /* The procedure is now complete.  Update connection bonded state and
     * terminate procedure.
     */
    ble_sm_update_sec_state(proc->conn_handle, 1, 0, 1);
    proc->state = BLE_SM_PROC_STATE_NONE;

    res->app_status = 0;
    res->enc_cb = 1;
}

static void
ble_sm_key_exch_exec(struct ble_sm_proc *proc, struct ble_sm_result *res,
                     void *arg)
{
    struct ble_sm_id_addr_info addr_info;
    struct ble_sm_sign_info sign_info;
    struct ble_sm_master_id master_iden;
    struct ble_sm_id_info iden_info;
    struct ble_sm_enc_info enc_info;
    uint8_t init_key_dist;
    uint8_t resp_key_dist;
    uint8_t our_key_dist;
    uint8_t *irk;
    int rc;

    ble_sm_key_dist(proc, &init_key_dist, &resp_key_dist);
    if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
        our_key_dist = init_key_dist;
    } else {
        our_key_dist = resp_key_dist;
    }

    if (our_key_dist & BLE_SM_PAIR_KEY_DIST_ENC) {
        /* Send encryption information. */
        rc = ble_sm_gen_ltk(proc, enc_info.ltk);
        if (rc != 0) {
            goto err;
        }
        rc = ble_sm_enc_info_tx(proc->conn_handle, &enc_info);
        if (rc != 0) {
            goto err;
        }
        proc->our_keys.ltk_valid = 1;
        memcpy(proc->our_keys.ltk, enc_info.ltk, 16);

        /* Send master identification. */
        rc = ble_sm_gen_ediv(&master_iden.ediv);
        if (rc != 0) {
            goto err;
        }
        rc = ble_sm_gen_start_rand(&master_iden.rand_val);
        if (rc != 0) {
            goto err;
        }
        rc = ble_sm_master_id_tx(proc->conn_handle, &master_iden);
        if (rc != 0) {
            goto err;
        }
        proc->our_keys.ediv_rand_valid = 1;
        proc->our_keys.rand_val = master_iden.rand_val;
        proc->our_keys.ediv = master_iden.ediv;
    }

    if (our_key_dist & BLE_SM_PAIR_KEY_DIST_ID) {
        /* Send identity information. */
        irk = ble_hs_priv_get_local_irk();

        memcpy(iden_info.irk, irk, 16);

        rc = ble_sm_id_info_tx(proc->conn_handle, &iden_info);
        if (rc != 0) {
            goto err;
        }
        proc->our_keys.irk_valid = 1;

        /* Send identity address information. */
        bls_hs_priv_copy_local_identity_addr(addr_info.bd_addr,
                                             &addr_info.addr_type);
        rc = ble_sm_id_addr_info_tx(proc->conn_handle, &addr_info);
        if (rc != 0) {
            goto err;
        }

        proc->our_keys.addr_valid = 1;
        memcpy(proc->our_keys.irk, irk, 16);
        proc->our_keys.addr_type = addr_info.addr_type;
        memcpy(proc->our_keys.addr, addr_info.bd_addr, 6);
    }

    if (our_key_dist & BLE_SM_PAIR_KEY_DIST_SIGN) {
        /* Send signing information. */
        rc = ble_sm_gen_csrk(proc, sign_info.sig_key);
        if (rc != 0) {
            goto err;
        }
        rc = ble_sm_sign_info_tx(proc->conn_handle, &sign_info);
        if (rc != 0) {
            goto err;
        }
        proc->our_keys.csrk_valid = 1;
        memcpy(proc->our_keys.csrk, sign_info.sig_key, 16);
    }

    if (proc->flags & BLE_SM_PROC_F_INITIATOR || proc->rx_key_flags == 0) {
        /* The procedure is now complete. */
        ble_sm_key_exch_success(proc, res);
    }

    return;

err:
    res->app_status = rc;
    res->sm_err = BLE_SM_ERR_UNSPECIFIED;
    res->enc_cb = 1;
}

static void
ble_sm_key_rxed(struct ble_sm_proc *proc, struct ble_sm_result *res)
{
    BLE_HS_LOG(DEBUG, "rx_key_flags=0x%02x\n", proc->rx_key_flags);

    if (proc->rx_key_flags == 0) {
        /* The peer is done sending keys.  If we are the initiator, we need to
         * send ours.  If we are the responder, the procedure is complete.
         */
        if (proc->flags & BLE_SM_PROC_F_INITIATOR) {
            res->execute = 1;
        } else {
            ble_sm_key_exch_success(proc, res);
        }
    }
}

static void
ble_sm_enc_info_rx(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
                   struct ble_sm_result *res)
{
    struct ble_sm_enc_info cmd;
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;

    res->app_status = ble_hs_misc_pullup_base(om, BLE_SM_ENC_INFO_SZ);
    if (res->app_status != 0) {
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    ble_sm_enc_info_parse((*om)->om_data, (*om)->om_len, &cmd);
    BLE_SM_LOG_CMD(0, "enc info", conn_handle, ble_sm_enc_info_log, &cmd);

    ble_hs_lock();

    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_KEY_EXCH, -1,
                            &prev);
    if (proc == NULL) {
        res->app_status = BLE_HS_ENOENT;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
    } else {
        proc->rx_key_flags &= ~BLE_SM_KE_F_ENC_INFO;
        proc->peer_keys.ltk_valid = 1;
        memcpy(proc->peer_keys.ltk, cmd.ltk, 16);

        ble_sm_key_rxed(proc, res);
    }

    ble_hs_unlock();
}

static void
ble_sm_master_id_rx(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
                    struct ble_sm_result *res)
{
    struct ble_sm_master_id cmd;
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;

    res->app_status = ble_hs_misc_pullup_base(om, BLE_SM_MASTER_ID_SZ);
    if (res->app_status != 0) {
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    ble_sm_master_id_parse((*om)->om_data, (*om)->om_len, &cmd);
    BLE_SM_LOG_CMD(0, "master id", conn_handle, ble_sm_master_id_log, &cmd);

    ble_hs_lock();

    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_KEY_EXCH, -1,
                            &prev);
    if (proc == NULL) {
        res->app_status = BLE_HS_ENOENT;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
    } else {
        proc->rx_key_flags &= ~BLE_SM_KE_F_MASTER_ID;
        proc->peer_keys.ediv_rand_valid = 1;
        proc->peer_keys.ediv = cmd.ediv;
        proc->peer_keys.rand_val = cmd.rand_val;

        ble_sm_key_rxed(proc, res);
    }

    ble_hs_unlock();
}

static void
ble_sm_id_info_rx(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
                  struct ble_sm_result *res)
{
    struct ble_sm_id_info cmd;
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;

    res->app_status = ble_hs_misc_pullup_base(om, BLE_SM_ID_INFO_SZ);
    if (res->app_status != 0) {
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    ble_sm_id_info_parse((*om)->om_data, (*om)->om_len, &cmd);
    BLE_SM_LOG_CMD(0, "id info", conn_handle, ble_sm_id_info_log, &cmd);

    ble_hs_lock();

    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_KEY_EXCH, -1,
                            &prev);
    if (proc == NULL) {
        res->app_status = BLE_HS_ENOENT;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
    } else {
        proc->rx_key_flags &= ~BLE_SM_KE_F_ID_INFO;
        proc->peer_keys.irk_valid = 1;

        /* Store IRK in big endian. */
        memcpy(proc->peer_keys.irk, cmd.irk, 16);

        ble_sm_key_rxed(proc, res);
    }

    ble_hs_unlock();
}

static void
ble_sm_id_addr_info_rx(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
                       struct ble_sm_result *res)
{
    struct ble_sm_id_addr_info cmd;
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;

    res->app_status = ble_hs_misc_pullup_base(om, BLE_SM_ID_ADDR_INFO_SZ);
    if (res->app_status != 0) {
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    ble_sm_id_addr_info_parse((*om)->om_data, (*om)->om_len, &cmd);
    BLE_SM_LOG_CMD(0, "id addr info", conn_handle, ble_sm_id_addr_info_log,
                   &cmd);

    ble_hs_lock();

    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_KEY_EXCH, -1,
                            &prev);
    if (proc == NULL) {
        res->app_status = BLE_HS_ENOENT;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
    } else {
        proc->rx_key_flags &= ~BLE_SM_KE_F_ADDR_INFO;
        proc->peer_keys.addr_valid = 1;
        proc->peer_keys.addr_type = cmd.addr_type;
        memcpy(proc->peer_keys.addr, cmd.bd_addr, 6);

        ble_sm_key_rxed(proc, res);
    }

    ble_hs_unlock();
}

static void
ble_sm_sign_info_rx(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
                    struct ble_sm_result *res)
{
    struct ble_sm_sign_info cmd;
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;

    res->app_status = ble_hs_misc_pullup_base(om, BLE_SM_SIGN_INFO_SZ);
    if (res->app_status != 0) {
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
        res->enc_cb = 1;
        return;
    }

    ble_sm_sign_info_parse((*om)->om_data, (*om)->om_len, &cmd);
    BLE_SM_LOG_CMD(0, "sign info", conn_handle, ble_sm_sign_info_log, &cmd);

    ble_hs_lock();

    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_KEY_EXCH, -1,
                            &prev);
    if (proc == NULL) {
        res->app_status = BLE_HS_ENOENT;
        res->sm_err = BLE_SM_ERR_UNSPECIFIED;
    } else {
        proc->rx_key_flags &= ~BLE_SM_KE_F_SIGN_INFO;
        proc->peer_keys.csrk_valid = 1;
        memcpy(proc->peer_keys.csrk, cmd.sig_key, 16);

        ble_sm_key_rxed(proc, res);
    }

    ble_hs_unlock();
}

/*****************************************************************************
 * $fail                                                                     *
 *****************************************************************************/

static void
ble_sm_fail_rx(uint16_t conn_handle, uint8_t op, struct os_mbuf **om,
               struct ble_sm_result *res)
{
    struct ble_sm_pair_fail cmd;

    res->enc_cb = 1;

    res->app_status = ble_hs_misc_pullup_base(om, BLE_SM_PAIR_FAIL_SZ);
    if (res->app_status == 0) {
        ble_sm_pair_fail_parse((*om)->om_data, (*om)->om_len, &cmd);
        BLE_SM_LOG_CMD(0, "fail", conn_handle, ble_sm_pair_fail_log, &cmd);

        res->app_status = BLE_HS_SM_THEM_ERR(cmd.reason);
    }
}

/*****************************************************************************
 * $api                                                                      *
 *****************************************************************************/

void
ble_sm_heartbeat(void)
{
    struct ble_sm_proc_list exp_list;
    struct ble_sm_proc *proc;

    /* Remove all timed out procedures and insert them into a temporary
     * list.
     */
    ble_sm_extract_expired(&exp_list);

    /* Notify application of each failure and free the corresponding procedure
     * object.
     * XXX: Mark connection as tainted; don't allow any subsequent SMP
     * procedures without reconnect.
     */
    while ((proc = STAILQ_FIRST(&exp_list)) != NULL) {
        ble_gap_enc_event(proc->conn_handle, BLE_HS_ETIMEOUT, 0);

        STAILQ_REMOVE_HEAD(&exp_list, next);
        ble_sm_proc_free(proc);
    }
}

/**
 * Initiates the pairing procedure for the specified connection.
 */
int
ble_sm_pair_initiate(uint16_t conn_handle)
{
    struct ble_sm_result res;
    struct ble_sm_proc *proc;

    /* Make sure a procedure isn't already in progress for this connection. */
    ble_hs_lock();
    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_NONE,
                                  -1, NULL);
    if (proc != NULL) {
        res.app_status = BLE_HS_EALREADY;

        /* Set pointer to null so that existing entry doesn't get freed. */
        proc = NULL;
    } else {
        proc = ble_sm_proc_alloc();
        if (proc == NULL) {
            res.app_status = BLE_HS_ENOMEM;
        } else {
            proc->conn_handle = conn_handle;
            proc->state = BLE_SM_PROC_STATE_PAIR;
            proc->flags |= BLE_SM_PROC_F_INITIATOR;
            ble_sm_insert(proc);

            res.execute = 1;
        }
    }

    ble_hs_unlock();

    if (proc != NULL) {
        ble_sm_process_result(conn_handle, &res);
    }

    return res.app_status;
}

int
ble_sm_slave_initiate(uint16_t conn_handle)
{
    struct ble_sm_result res;
    struct ble_sm_proc *proc;

    ble_hs_lock();

    /* Make sure a procedure isn't already in progress for this connection. */
    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_NONE,
                                  -1, NULL);
    if (proc != NULL) {
        res.app_status = BLE_HS_EALREADY;

        /* Set pointer to null so that existing entry doesn't get freed. */
        proc = NULL;
    } else {
        proc = ble_sm_proc_alloc();
        if (proc == NULL) {
            res.app_status = BLE_HS_ENOMEM;
        } else {
            proc->conn_handle = conn_handle;
            proc->state = BLE_SM_PROC_STATE_SEC_REQ;
            ble_sm_insert(proc);

            res.execute = 1;
        }
    }

    ble_hs_unlock();

    if (proc != NULL) {
        ble_sm_process_result(conn_handle, &res);
    }

    return res.app_status;
}

/**
 * Initiates the encryption procedure for the specified connection.
 */
int
ble_sm_enc_initiate(uint16_t conn_handle, uint8_t *ltk, uint16_t ediv,
                    uint64_t rand_val, int auth)
{
    struct ble_sm_result res;
    struct ble_sm_proc *proc;
    struct hci_start_encrypt cmd;

    memset(&res, 0, sizeof res);

    /* Make sure a procedure isn't already in progress for this connection. */
    ble_hs_lock();
    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_NONE,
                            -1, NULL);
    if (proc != NULL) {
        res.app_status = BLE_HS_EALREADY;

        /* Set pointer to null so that existing entry doesn't get freed. */
        proc = NULL;
    } else {
        proc = ble_sm_proc_alloc();
        if (proc == NULL) {
            res.app_status = BLE_HS_ENOMEM;
        } else {
            proc->conn_handle = conn_handle;
            proc->state = BLE_SM_PROC_STATE_ENC_RESTORE;
            proc->flags |= BLE_SM_PROC_F_INITIATOR;
            if (auth) {
                proc->flags |= BLE_SM_PROC_F_AUTHENTICATED;
            }
            ble_sm_insert(proc);

            cmd.connection_handle = conn_handle;
            cmd.encrypted_diversifier = ediv;
            cmd.random_number = rand_val;
            memcpy(cmd.long_term_key, ltk, sizeof cmd.long_term_key);

            res.execute = 1;
            res.state_arg = &cmd;
        }
    }

    ble_hs_unlock();

    ble_sm_process_result(conn_handle, &res);

    return res.app_status;
}

static int
ble_sm_rx(uint16_t conn_handle, struct os_mbuf **om)
{
    struct ble_sm_result res;
    ble_sm_rx_fn *rx_cb;
    uint8_t op;
    int rc;

    STATS_INC(ble_l2cap_stats, sm_rx);

    rc = os_mbuf_copydata(*om, 0, 1, &op);
    if (rc != 0) {
        return BLE_HS_EBADDATA;
    }

    /* Strip L2CAP SM header from the front of the mbuf. */
    os_mbuf_adj(*om, 1);

    rx_cb = ble_sm_dispatch_get(op);
    if (rx_cb != NULL) {
        memset(&res, 0, sizeof res);

        rx_cb(conn_handle, op, om, &res);
        ble_sm_process_result(conn_handle, &res);
        rc = res.app_status;
    } else {
        rc = BLE_HS_ENOTSUP;
    }

    return rc;
}

struct ble_l2cap_chan *
ble_sm_create_chan(void)
{
    struct ble_l2cap_chan *chan;

    chan = ble_l2cap_chan_alloc();
    if (chan == NULL) {
        return NULL;
    }

    chan->blc_cid = BLE_L2CAP_CID_SM;
    chan->blc_my_mtu = BLE_SM_MTU;
    chan->blc_default_mtu = BLE_SM_MTU;
    chan->blc_rx_fn = ble_sm_rx;

    return chan;
}

int
ble_sm_inject_io(uint16_t conn_handle, struct ble_sm_io *pkey)
{
    struct ble_sm_result res;
    struct ble_sm_proc *proc;
    struct ble_sm_proc *prev;
    int rc;

    memset(&res, 0, sizeof res);

    ble_hs_lock();

    proc = ble_sm_proc_find(conn_handle, BLE_SM_PROC_STATE_NONE, -1, &prev);

    if (proc == NULL) {
        rc = BLE_HS_ENOENT;
    } else if (proc->flags & BLE_SM_PROC_F_IO_INJECTED) {
        rc = BLE_HS_EALREADY;
    } else if (pkey->action != ble_sm_io_action(proc)) {
        /* Application provided incorrect IO type. */
        rc = BLE_HS_EINVAL;
    } else if (ble_sm_ioact_state(pkey->action) != proc->state) {
        /* Procedure is not ready for user input. */
        rc = BLE_HS_EINVAL;
    } else {
        /* Assume valid input. */
        rc = 0;

        switch (pkey->action) {
        case BLE_SM_IOACT_OOB:
            if (pkey->oob == NULL) {
                res.app_status = BLE_HS_SM_US_ERR(BLE_SM_ERR_OOB);
                res.sm_err = BLE_SM_ERR_OOB;
            } else {
                proc->flags |= BLE_SM_PROC_F_IO_INJECTED;
                memcpy(proc->tk, pkey->oob, 16);
                if ((proc->flags & BLE_SM_PROC_F_INITIATOR) ||
                    (proc->flags & BLE_SM_PROC_F_ADVANCE_ON_IO)) {

                    res.execute = 1;
                }
            }
            break;

        case BLE_SM_IOACT_INPUT:
        case BLE_SM_IOACT_DISP:
            if (pkey->passkey > 999999) {
                rc = BLE_HS_EINVAL;
            } else {
                proc->flags |= BLE_SM_PROC_F_IO_INJECTED;
                memset(proc->tk, 0, 16);
                proc->tk[0] = (pkey->passkey >> 0) & 0xff;
                proc->tk[1] = (pkey->passkey >> 8) & 0xff;
                proc->tk[2] = (pkey->passkey >> 16) & 0xff;
                proc->tk[3] = (pkey->passkey >> 24) & 0xff;
                if ((proc->flags & BLE_SM_PROC_F_INITIATOR) ||
                    (proc->flags & BLE_SM_PROC_F_ADVANCE_ON_IO)) {

                    res.execute = 1;
                }
            }
            break;

        case BLE_SM_IOACT_NUMCMP:
            if (!pkey->numcmp_accept) {
                res.app_status = BLE_HS_SM_US_ERR(BLE_SM_ERR_NUMCMP);
                res.sm_err = BLE_SM_ERR_NUMCMP;
            } else {
                proc->flags |= BLE_SM_PROC_F_IO_INJECTED;
                if (proc->flags & BLE_SM_PROC_F_INITIATOR ||
                    proc->flags & BLE_SM_PROC_F_ADVANCE_ON_IO) {

                    res.execute = 1;
                }
            }
            break;

        default:
            BLE_HS_DBG_ASSERT(0);
            rc = BLE_HS_EINVAL;
            break;
        }
    }

    ble_hs_unlock();

    /* If application provided invalid input, return error without modifying
     * SMP state.
     */
    if (rc != 0) {
        return rc;
    }

    ble_sm_process_result(conn_handle, &res);
    return res.app_status;
}

void
ble_sm_connection_broken(uint16_t conn_handle)
{
    struct ble_sm_result res;

    memset(&res, 0, sizeof res);
    res.app_status = BLE_HS_ENOTCONN;
    res.enc_cb = 1;

    ble_sm_process_result(conn_handle, &res);
}

static void
ble_sm_free_mem(void)
{
    free(ble_sm_proc_mem);
}

int
ble_sm_init(void)
{
    int rc;

    ble_sm_free_mem();

    STAILQ_INIT(&ble_sm_procs);

    if (ble_hs_cfg.max_l2cap_sm_procs > 0) {
        ble_sm_proc_mem = malloc(
            OS_MEMPOOL_BYTES(ble_hs_cfg.max_l2cap_sm_procs,
                             sizeof (struct ble_sm_proc)));
        if (ble_sm_proc_mem == NULL) {
            rc = BLE_HS_ENOMEM;
            goto err;
        }
        rc = os_mempool_init(&ble_sm_proc_pool,
                             ble_hs_cfg.max_l2cap_sm_procs,
                             sizeof (struct ble_sm_proc),
                             ble_sm_proc_mem,
                             "ble_sm_proc_pool");
        if (rc != 0) {
            goto err;
        }
    }

    ble_sm_sc_init();

    return 0;

err:
    ble_sm_free_mem();
    return rc;
}

#endif
