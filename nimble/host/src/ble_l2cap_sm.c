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

#if NIMBLE_OPT_SM

#define BLE_L2CAP_SM_PROC_STATE_NONE            ((uint8_t)-1)

#define BLE_L2CAP_SM_PROC_STATE_PAIR            0
#define BLE_L2CAP_SM_PROC_STATE_CONFIRM         1
#define BLE_L2CAP_SM_PROC_STATE_RANDOM          2
#define BLE_L2CAP_SM_PROC_STATE_LTK             3
#define BLE_L2CAP_SM_PROC_STATE_ENC_CHANGE      4
#define BLE_L2CAP_SM_PROC_STATE_CNT             5

#define BLE_L2CAP_SM_PROC_F_INITIATOR           0x01

/** Procedure timeout; 30 seconds. */
#define BLE_L2CAP_SM_TIMEOUT_OS_TICKS           (30 * OS_TICKS_PER_SEC)

typedef uint16_t ble_l2cap_sm_proc_flags;

struct ble_l2cap_sm_proc {
    STAILQ_ENTRY(ble_l2cap_sm_proc) next;

    uint32_t exp_os_ticks;
    ble_l2cap_sm_proc_flags flags;
    uint16_t conn_handle;
    uint8_t pair_alg;
    uint8_t state;

    /* XXX: Minimum security requirements. */

    struct ble_l2cap_sm_pair_cmd pair_req;
    struct ble_l2cap_sm_pair_cmd pair_rsp;
    uint8_t tk[16];
    uint8_t confirm_their[16];
    uint8_t randm[16];
    uint8_t rands[16];
    uint8_t ltk[16];
};

STAILQ_HEAD(ble_l2cap_sm_proc_list, ble_l2cap_sm_proc);

typedef int ble_l2cap_sm_rx_fn(uint16_t conn_handle, uint8_t state,
                               struct os_mbuf **om);

static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_noop;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_pair_req;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_pair_rsp;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_pair_confirm;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_pair_random;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_pair_fail;

static ble_l2cap_sm_rx_fn * const ble_l2cap_sm_dispatch[] = {
   [BLE_L2CAP_SM_OP_PAIR_REQ] = ble_l2cap_sm_rx_pair_req,
   [BLE_L2CAP_SM_OP_PAIR_RSP] = ble_l2cap_sm_rx_pair_rsp,
   [BLE_L2CAP_SM_OP_PAIR_CONFIRM] = ble_l2cap_sm_rx_pair_confirm,
   [BLE_L2CAP_SM_OP_PAIR_RANDOM] = ble_l2cap_sm_rx_pair_random,
   [BLE_L2CAP_SM_OP_PAIR_FAIL] = ble_l2cap_sm_rx_pair_fail,
   [BLE_L2CAP_SM_OP_ENC_INFO] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_MASTER_ID] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_IDENTITY_INFO] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_IDENTITY_ADDR_INFO] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_SIGN_INFO] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_SEC_REQ] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_PAIR_PUBLIC_KEY] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_PAIR_DHKEY_CHECK] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_PAIR_KEYPRESS_NOTIFY] = ble_l2cap_sm_rx_noop,
};

static void *ble_l2cap_sm_proc_mem;
static struct os_mempool ble_l2cap_sm_proc_pool;

/* Maintains the list of active security manager procedures. */
static struct ble_l2cap_sm_proc_list ble_l2cap_sm_procs;

static int ble_l2cap_sm_confirm_prepare_args(struct ble_l2cap_sm_proc *proc,
                                             uint8_t *k, uint8_t *preq,
                                             uint8_t *pres, uint8_t *iat,
                                             uint8_t *rat, uint8_t *ia,
                                             uint8_t *ra);

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

int
ble_l2cap_sm_dbg_num_procs(void)
{
    struct ble_l2cap_sm_proc *proc;
    int cnt;

    cnt = 0;
    STAILQ_FOREACH(proc, &ble_l2cap_sm_procs, next) {
        cnt++;
    }

    return cnt;
}

#endif

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
ble_l2cap_sm_gen_key(struct ble_l2cap_sm_proc *proc)
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

static void
ble_l2cap_sm_proc_set_timer(struct ble_l2cap_sm_proc *proc)
{
    /* Set a timeout of 30 seconds. */
    proc->exp_os_ticks = os_time_get() + BLE_L2CAP_SM_TIMEOUT_OS_TICKS;
}

static ble_l2cap_sm_rx_fn *
ble_l2cap_sm_dispatch_get(uint8_t state)
{
    if (state > sizeof ble_l2cap_sm_dispatch / sizeof ble_l2cap_sm_dispatch[0]) {
        return NULL;
    }

    return ble_l2cap_sm_dispatch[state];
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
}

static void
ble_l2cap_sm_sec_params(struct ble_l2cap_sm_proc *proc,
                        struct ble_gap_sec_params *out_sec_params,
                        int enc_enabled)
{
    out_sec_params->pair_alg = proc->pair_alg;
    out_sec_params->enc_enabled = enc_enabled;
    out_sec_params->auth_enabled = 0; // XXX
}

static void
ble_l2cap_sm_gap_event(struct ble_l2cap_sm_proc *proc, int status,
                       int enc_enabled)
{
    struct ble_gap_sec_params sec_params;

    ble_l2cap_sm_sec_params(proc, &sec_params, enc_enabled);
    ble_gap_security_event(proc->conn_handle, status, &sec_params);
}

static int
ble_l2cap_sm_process_status(struct ble_l2cap_sm_proc *proc, int status,
                            uint8_t sm_status, int call_cb, int tx_fail)
{
    if (proc == NULL) {
        status = BLE_HS_ENOENT;
    } else if (status != 0) {
        if (tx_fail) {
            ble_l2cap_sm_pair_fail_tx(proc->conn_handle, sm_status);
        }
        if (call_cb) {
            ble_l2cap_sm_gap_event(proc, status, 0);
        }
        ble_l2cap_sm_proc_free(proc);
    }

    return status;
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
static struct ble_l2cap_sm_proc *
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
            STAILQ_INSERT_TAIL(dst_list, proc, next);
        }

        prev = proc;
        proc = next;
    }

    ble_hs_unlock();
}

static int
ble_l2cap_sm_rx_noop(uint16_t conn_handle, uint8_t state, struct os_mbuf **om)
{
    return BLE_HS_ENOTSUP;
}

/*****************************************************************************
 * $hci                                                                      *
 *****************************************************************************/

static int
ble_l2cap_sm_start_encrypt_tx(uint16_t conn_handle, uint8_t *ltk)
{
    struct hci_start_encrypt cmd;
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_LE_START_ENCRYPT_LEN];
    int rc;

    rc = ble_l2cap_sm_gen_ediv(&cmd.encrypted_diversifier);
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_sm_gen_start_rand(&cmd.random_number);
    if (rc != 0) {
        return rc;
    }

    cmd.connection_handle = conn_handle;
    memcpy(cmd.long_term_key, ltk, sizeof cmd.long_term_key);

    host_hci_cmd_build_le_start_encrypt(&cmd, buf, sizeof buf);
    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
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
    if (ack_params_len != BLE_HCI_LT_KEY_REQ_REPLY_ACK_PARAM_LEN - 1) {
        return BLE_HS_ECONTROLLER;
    }

    ack_conn_handle = TOFROMLE16(ack_conn_handle);
    if (ack_conn_handle != conn_handle) {
        return BLE_HS_ECONTROLLER;
    }

    return 0;
}

static int
ble_l2cap_sm_lt_key_req_handle(struct ble_l2cap_sm_proc *proc,
                               struct hci_le_lt_key_req *evt,
                               uint8_t *out_sm_status)
{
    int rc;

    rc = ble_l2cap_sm_lt_key_req_reply_tx(proc->conn_handle, proc->ltk);
    if (rc != 0) {
        *out_sm_status = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        return rc;
    }

    proc->state = BLE_L2CAP_SM_PROC_STATE_ENC_CHANGE;

    return 0;
}

/*****************************************************************************
 * $random                                                                   *
 *****************************************************************************/

static uint8_t *
ble_l2cap_sm_our_pair_rand(struct ble_l2cap_sm_proc *proc)
{
    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        return proc->randm;
    } else {
        return proc->rands;
    }
}

static uint8_t *
ble_l2cap_sm_their_pair_rand(struct ble_l2cap_sm_proc *proc)
{
    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        return proc->rands;
    } else {
        return proc->randm;
    }
}

static int
ble_l2cap_sm_random_go(struct ble_l2cap_sm_proc *proc)
{
    struct ble_l2cap_sm_pair_random cmd;
    int rc;

    memcpy(cmd.value, ble_l2cap_sm_our_pair_rand(proc), 16);
    rc = ble_l2cap_sm_pair_random_tx(proc->conn_handle, &cmd);
    if (rc != 0) {
        return rc;
    }

    ble_l2cap_sm_proc_set_timer(proc);

    return 0;
}

static int
ble_l2cap_sm_random_handle(struct ble_l2cap_sm_proc *proc,
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

    /* Verify peer's random value. */
    rc = ble_l2cap_sm_confirm_prepare_args(proc, k, preq, pres, &iat, &rat,
                                           ia, ra);
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
        rc = BLE_HS_SM_US_ERR(BLE_L2CAP_SM_ERR_CONFIRM_MISMATCH);
        *out_sm_status = BLE_L2CAP_SM_ERR_CONFIRM_MISMATCH;
        return rc;
    }

    memcpy(ble_l2cap_sm_their_pair_rand(proc), cmd->value, 16);

    /* Generate the key. */
    rc = ble_l2cap_sm_gen_key(proc);
    if (rc != 0) {
        *out_sm_status = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        return rc;
    }

    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        /* Send the start-encrypt HCI command to the controller. */
        rc = ble_l2cap_sm_start_encrypt_tx(proc->conn_handle, proc->ltk);
        if (rc != 0) {
            *out_sm_status = BLE_L2CAP_SM_ERR_UNSPECIFIED;
            return rc;
        }
        proc->state = BLE_L2CAP_SM_PROC_STATE_ENC_CHANGE;
    } else {
        rc = ble_l2cap_sm_random_go(proc);
        if (rc != 0) {
            *out_sm_status = BLE_L2CAP_SM_ERR_UNSPECIFIED;
            return rc;
        }
        proc->state = BLE_L2CAP_SM_PROC_STATE_LTK;
    }

    *out_sm_status = 0;
    return 0;
}

/*****************************************************************************
 * $confirm                                                                  *
 *****************************************************************************/

static int
ble_l2cap_sm_confirm_prepare_args(struct ble_l2cap_sm_proc *proc,
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

static int
ble_l2cap_sm_confirm_go(struct ble_l2cap_sm_proc *proc)
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

    rc = ble_l2cap_sm_confirm_prepare_args(proc, k, preq, pres, &iat, &rat,
                                           ia, ra);
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_sm_alg_c1(k, ble_l2cap_sm_our_pair_rand(proc), preq, pres,
                             iat, rat, ia, ra, cmd.value);
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_sm_pair_confirm_tx(proc->conn_handle, &cmd);
    if (rc != 0) {
        return rc;
    }

    ble_l2cap_sm_proc_set_timer(proc);

    return 0;
}

static int
ble_l2cap_sm_confirm_handle(struct ble_l2cap_sm_proc *proc,
                            struct ble_l2cap_sm_pair_confirm *cmd,
                            uint8_t *out_sm_status)
{
    int rc;

    memcpy(proc->confirm_their, cmd->value, 16);

    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        rc = ble_l2cap_sm_random_go(proc);
        if (rc != 0) {
            *out_sm_status = BLE_L2CAP_SM_ERR_UNSPECIFIED;
            return rc;
        }
    } else {
        /* XXX: If MITM is used, request TK from application. */

        rc = ble_l2cap_sm_confirm_go(proc);
        if (rc != 0) {
            *out_sm_status = BLE_L2CAP_SM_ERR_UNSPECIFIED;
            return rc;
        }
    }

    proc->state = BLE_L2CAP_SM_PROC_STATE_RANDOM;

    return 0;
}

/*****************************************************************************
 * $pair                                                                     *
 *****************************************************************************/

static int
ble_l2cap_sm_pair_go(struct ble_l2cap_sm_proc *proc)
{
    struct ble_l2cap_sm_pair_cmd cmd;
    int is_req;
    int rc;

    is_req = proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR;

    cmd.io_cap = ble_hs_cfg.sm_io_cap;
    cmd.oob_data_flag = ble_hs_cfg.sm_oob_data_flag;
    cmd.authreq = (ble_hs_cfg.sm_bonding << 0)  |
                  (ble_hs_cfg.sm_mitm << 2)     |
                  (ble_hs_cfg.sm_sc << 3)       |
                  (ble_hs_cfg.sm_keypress << 4);
    cmd.max_enc_key_size = 16;

    if (is_req) {
        cmd.init_key_dist = ble_hs_cfg.sm_our_key_dist;
        cmd.resp_key_dist = ble_hs_cfg.sm_their_key_dist;
    } else {
        cmd.init_key_dist = ble_hs_cfg.sm_their_key_dist;
        cmd.resp_key_dist = ble_hs_cfg.sm_our_key_dist;
    }

    rc = ble_l2cap_sm_pair_cmd_tx(proc->conn_handle, is_req, &cmd);
    if (rc != 0) {
        return rc;
    }

    if (is_req) {
        proc->pair_req = cmd;
    } else {
        proc->pair_alg = BLE_L2CAP_SM_PAIR_ALG_JW;
        proc->pair_rsp = cmd;
    }

    rc = ble_l2cap_sm_gen_pair_rand(ble_l2cap_sm_our_pair_rand(proc));
    if (rc != 0) {
        return rc;
    }

    ble_l2cap_sm_proc_set_timer(proc);

    return 0;
}

static int
ble_l2cap_sm_pair_req_handle(struct ble_l2cap_sm_proc *proc,
                             struct ble_l2cap_sm_pair_cmd *req,
                             uint8_t *out_sm_status)
{
    int rc;

    proc->pair_req = *req;

    rc = ble_l2cap_sm_pair_go(proc);
    if (rc != 0) {
        *out_sm_status = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        return rc;
    }

    proc->state = BLE_L2CAP_SM_PROC_STATE_CONFIRM;

    return 0;
}

static int
ble_l2cap_sm_pair_rsp_handle(struct ble_l2cap_sm_proc *proc,
                             struct ble_l2cap_sm_pair_cmd *rsp,
                             uint8_t *out_sm_status)
{
    int rc;

    proc->pair_rsp = *rsp;

    /* XXX: Assume legacy "Just Works" for now. */
    proc->pair_alg = BLE_L2CAP_SM_PAIR_ALG_JW;

    /* XXX: If MITM is used, request TK from application. */

    proc->state = BLE_L2CAP_SM_PROC_STATE_CONFIRM;
    rc = ble_l2cap_sm_confirm_go(proc);
    if (rc != 0) {
        *out_sm_status = BLE_L2CAP_SM_ERR_UNSPECIFIED;
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $rx                                                                       *
 *****************************************************************************/

static int
ble_l2cap_sm_rx_pair_req(uint16_t conn_handle, uint8_t state,
                         struct os_mbuf **om)
{
    struct ble_l2cap_sm_pair_cmd req;
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;
    uint8_t sm_status;
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
    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_NONE,
                                  -1, &prev);
    if (proc != NULL) {
        /* Pairing already in progress; abort old procedure and start new. */
        /* XXX: Check the spec on this. */
        ble_l2cap_sm_proc_remove(proc, prev);
        ble_l2cap_sm_proc_free(proc);
    }

    proc = ble_l2cap_sm_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
    } else {
        proc->conn_handle = conn_handle;
        proc->state = BLE_L2CAP_SM_PROC_STATE_PAIR;
        rc = ble_l2cap_sm_pair_req_handle(proc, &req, &sm_status);
        rc = ble_l2cap_sm_process_status(proc, rc, sm_status, 0, 1);
        if (rc == 0) {
            STAILQ_INSERT_HEAD(&ble_l2cap_sm_procs, proc, next);
        }
    }

    ble_hs_unlock();

    return rc;
}

static int
ble_l2cap_sm_rx_pair_rsp(uint16_t conn_handle, uint8_t state,
                         struct os_mbuf **om)
{
    struct ble_l2cap_sm_pair_cmd rsp;
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;
    uint8_t sm_status;
    int rc;

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
        rc = ble_l2cap_sm_pair_rsp_handle(proc, &rsp, &sm_status);
        if (rc != 0) {
            ble_l2cap_sm_proc_remove(proc, prev);
        }
    }
    ble_hs_unlock();

    rc = ble_l2cap_sm_process_status(proc, rc, sm_status, 1, 1);
    return rc;
}

static int
ble_l2cap_sm_rx_pair_confirm(uint16_t conn_handle, uint8_t state,
                             struct os_mbuf **om)
{
    struct ble_l2cap_sm_pair_confirm cmd;
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;
    uint8_t sm_status;
    int rc;

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SM_PAIR_CONFIRM_SZ);
    if (rc != 0) {
        return rc;
    }

    ble_l2cap_sm_pair_confirm_parse((*om)->om_data, (*om)->om_len, &cmd);

    BLE_HS_LOG(DEBUG, "rxed sm confirm cmd\n");

    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_CONFIRM,
                                  -1, &prev);
    if (proc != NULL) {
        rc = ble_l2cap_sm_confirm_handle(proc, &cmd, &sm_status);
        if (rc != 0) {
            ble_l2cap_sm_proc_remove(proc, prev);
        }
    }
    ble_hs_unlock();

    rc = ble_l2cap_sm_process_status(proc, rc, sm_status, 1, 1);
    return rc;
}

static int
ble_l2cap_sm_rx_pair_random(uint16_t conn_handle, uint8_t state,
                            struct os_mbuf **om)
{
    struct ble_l2cap_sm_pair_random cmd;
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;
    uint8_t sm_status;
    int rc;

    sm_status = 0;  /* Silence gcc warning. */

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
        rc = ble_l2cap_sm_random_handle(proc, &cmd, &sm_status);
        if (rc != 0) {
            ble_l2cap_sm_proc_remove(proc, prev);
        }
    }
    ble_hs_unlock();

    rc = ble_l2cap_sm_process_status(proc, rc, sm_status, 1, 1);
    return rc;
}

static int
ble_l2cap_sm_rx_pair_fail(uint16_t conn_handle, uint8_t state,
                          struct os_mbuf **om)
{
    struct ble_l2cap_sm_pair_fail cmd;
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;
    int rc;

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SM_PAIR_FAIL_SZ);
    if (rc != 0) {
        return rc;
    }

    ble_l2cap_sm_pair_fail_parse((*om)->om_data, (*om)->om_len, &cmd);

    BLE_HS_LOG(DEBUG, "rxed sm fail cmd; reason=%d\n", cmd.reason);

    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_NONE,
                                  -1, &prev);
    if (proc != NULL) {
        ble_l2cap_sm_proc_remove(proc, prev);
    }
    ble_hs_unlock();

    rc = ble_l2cap_sm_process_status(proc, rc, 0, 1, 0);
    return rc;
}

int
ble_l2cap_sm_rx_lt_key_req(struct hci_le_lt_key_req *evt)
{
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;
    uint8_t sm_status;
    int rc;

    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(evt->connection_handle,
                                  BLE_L2CAP_SM_PROC_STATE_LTK, 0, &prev);
    if (proc == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        rc = ble_l2cap_sm_lt_key_req_handle(proc, evt, &sm_status);
        if (rc != 0) {
            ble_l2cap_sm_proc_remove(proc, prev);
        }
    }
    ble_hs_unlock();

    rc = ble_l2cap_sm_process_status(proc, rc, sm_status, 1, 1);
    return rc;
}

void
ble_l2cap_sm_rx_encryption_change(struct hci_encrypt_change *evt)
{
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;
    int enc_enabled;

    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(evt->connection_handle,
                                  BLE_L2CAP_SM_PROC_STATE_ENC_CHANGE, -1,
                                  &prev);
    if (proc != NULL) {
        ble_l2cap_sm_proc_remove(proc, prev);
    }
    ble_hs_unlock();

    if (proc != NULL) {
        /* The pairing procedure is now complete. */
        enc_enabled = evt->encryption_enabled & 0x01; /* LE bit. */
        ble_l2cap_sm_gap_event(proc, BLE_HS_HCI_ERR(evt->status), enc_enabled);
        ble_l2cap_sm_proc_free(proc);
    }
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

int
ble_l2cap_sm_initiate(uint16_t conn_handle)
{
    struct ble_l2cap_sm_proc *proc;
    int rc;

    /* Make sure a pairing operation for this connection is not already in
     * progress.
     */
    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_NONE,
                                  -1, NULL);
    if (proc != NULL) {
        rc = BLE_HS_EALREADY;
        goto done;
    }

    proc = ble_l2cap_sm_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }
    proc->conn_handle = conn_handle;
    proc->state = BLE_L2CAP_SM_PROC_STATE_PAIR;
    proc->flags |= BLE_L2CAP_SM_PROC_F_INITIATOR;

    rc = ble_l2cap_sm_pair_go(proc);
    if (rc != 0) {
        ble_l2cap_sm_proc_free(proc);
        goto done;
    }

    STAILQ_INSERT_HEAD(&ble_l2cap_sm_procs, proc, next);

done:
    ble_hs_unlock();
    return rc;
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
ble_l2cap_sm_set_tk(uint16_t conn_handle, uint8_t *tk)
{
    struct ble_l2cap_sm_proc *proc;

    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_CONFIRM,
                                  -1, NULL);
    if (proc != NULL) {
        memcpy(proc->tk, tk, 16);
    }
    ble_hs_unlock();

    /* XXX: Proceed with pairing; send confirm command. */

    if (proc == NULL) {
        return BLE_HS_ENOENT;
    } else {
        return 0;
    }
}

void
ble_l2cap_sm_connection_broken(uint16_t conn_handle)
{
    struct ble_l2cap_sm_proc *proc;
    struct ble_l2cap_sm_proc *prev;

    ble_hs_lock();
    proc = ble_l2cap_sm_proc_find(conn_handle, BLE_L2CAP_SM_PROC_STATE_NONE,
                                  -1, &prev);
    if (proc != NULL) {
        /* Free the affected procedure object.  There is no need to notify the
         * application, as it has already been notified of the connection
         * failure.
         */
        ble_l2cap_sm_proc_remove(proc, prev);
        ble_l2cap_sm_proc_free(proc);
    }
    ble_hs_unlock();
}

int
ble_l2cap_sm_init(void)
{
    int rc;

    free(ble_l2cap_sm_proc_mem);

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
    }

    return 0;

err:
    free(ble_l2cap_sm_proc_mem);
    return rc;
}

#endif
