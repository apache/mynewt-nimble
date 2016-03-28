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
#include <assert.h>
#include "console/console.h"
#include "nimble/ble.h"
#include "nimble/nimble_opt.h"
#include "ble_hs_priv.h"

#if NIMBLE_OPT_SM

#define BLE_L2CAP_SM_PROC_OP_NONE               ((uint8_t)-1)

#define BLE_L2CAP_SM_PROC_OP_PAIR               0
#define BLE_L2CAP_SM_PROC_OP_CONFIRM            1
#define BLE_L2CAP_SM_PROC_OP_RANDOM             2
#define BLE_L2CAP_SM_PROC_OP_FAIL               3
#define BLE_L2CAP_SM_PROC_OP_LTK                4
#define BLE_L2CAP_SM_PROC_OP_LTK_TXED           5
#define BLE_L2CAP_SM_PROC_OP_ENC_CHANGE         6
#define BLE_L2CAP_SM_PROC_OP_CNT                7

#define BLE_L2CAP_SM_PROC_F_INITIATOR           0x01

#define BLE_L2CAP_SM_KEYGEN_METHOD_JW           0

typedef uint16_t ble_l2cap_sm_proc_flags;

struct ble_l2cap_sm_proc {
    struct ble_fsm_proc fsm_proc;
    ble_l2cap_sm_proc_flags flags;
    uint8_t keygen_method;

    /* XXX: Minimum security requirements. */

    union {
        struct {
            struct ble_l2cap_sm_pair_cmd pair_req;
            struct ble_l2cap_sm_pair_cmd pair_rsp;
            uint8_t confirm_their[16];
            uint8_t rand_our[16];
            uint8_t rand_their[16];
        } phase_1_2;

        struct {
            uint8_t key[16];
            uint8_t handle;
        } hci;

        struct {
            uint8_t reason;
        } fail;
    };
};

/** Used for extracting proc entries from the fsm list. */
struct ble_l2cap_sm_extract_arg {
    uint16_t conn_handle;
    uint8_t op;
    int8_t initiator; /* 0=no, 1=yes, -1=don't-care. */
};

typedef int ble_l2cap_sm_kick_fn(struct ble_l2cap_sm_proc *proc);

typedef int ble_l2cap_sm_rx_fn(uint16_t conn_handle, uint8_t op,
                               struct os_mbuf **om);

static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_noop;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_pair_req;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_pair_rsp;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_pair_confirm;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_pair_random;
static ble_l2cap_sm_rx_fn ble_l2cap_sm_rx_pair_fail;

static ble_l2cap_sm_kick_fn ble_l2cap_sm_pair_kick;
static ble_l2cap_sm_kick_fn ble_l2cap_sm_confirm_kick;
static ble_l2cap_sm_kick_fn ble_l2cap_sm_random_kick;
static ble_l2cap_sm_kick_fn ble_l2cap_sm_fail_kick;
static ble_l2cap_sm_kick_fn ble_l2cap_sm_lt_key_req_kick;

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

static ble_l2cap_sm_kick_fn * const
ble_l2cap_sm_kick[BLE_L2CAP_SM_PROC_OP_CNT] = {
    [BLE_L2CAP_SM_PROC_OP_PAIR]     = ble_l2cap_sm_pair_kick,
    [BLE_L2CAP_SM_PROC_OP_CONFIRM]  = ble_l2cap_sm_confirm_kick,
    [BLE_L2CAP_SM_PROC_OP_RANDOM]   = ble_l2cap_sm_random_kick,
    [BLE_L2CAP_SM_PROC_OP_FAIL]     = ble_l2cap_sm_fail_kick,
    [BLE_L2CAP_SM_PROC_OP_LTK]      = ble_l2cap_sm_lt_key_req_kick,
};

static void ble_l2cap_sm_rx_lt_key_req_reply_ack(struct ble_hci_ack *ack,
                                                 void *arg);

static void *ble_l2cap_sm_proc_mem;
static struct os_mempool ble_l2cap_sm_proc_pool;

static struct ble_fsm ble_l2cap_sm_fsm;

/*****************************************************************************
 * $debug                                                                    *
 *****************************************************************************/

#ifdef BLE_HS_DEBUG
static uint8_t ble_l2cap_sm_dbg_next_rand[16];
static uint8_t ble_l2cap_sm_dbg_next_rand_set;

void
ble_l2cap_sm_dbg_set_next_rand(uint8_t *next_rand)
{
    memcpy(ble_l2cap_sm_dbg_next_rand, next_rand, 16);
    ble_l2cap_sm_dbg_next_rand_set = 1;
}

int
ble_l2cap_sm_dbg_num_procs(void)
{
    struct ble_fsm_proc *proc;
    int cnt;

    ble_fsm_lock(&ble_l2cap_sm_fsm);

    cnt = 0;
    STAILQ_FOREACH(proc, &ble_l2cap_sm_fsm.procs, next) {
        cnt++;
    }

    ble_fsm_unlock(&ble_l2cap_sm_fsm);

    return cnt;
}

#endif

/*****************************************************************************
 * $mutex                                                                    *
 *****************************************************************************/

int
ble_l2cap_sm_locked_by_cur_task(void)
{
    return ble_fsm_locked_by_cur_task(&ble_l2cap_sm_fsm);
}

/*****************************************************************************
 * $misc                                                                     *
 *****************************************************************************/

static int
ble_l2cap_sm_proc_kick(struct ble_fsm_proc *proc)
{
    ble_l2cap_sm_kick_fn *kick_cb;
    int rc;

    assert(proc->op < BLE_L2CAP_SM_PROC_OP_CNT);
    kick_cb = ble_l2cap_sm_kick[proc->op];
    assert(kick_cb != NULL);

    rc = kick_cb((struct ble_l2cap_sm_proc *)proc);
    return rc;
}

/**
 * Lock restrictions: None.
 */
static ble_l2cap_sm_rx_fn *
ble_l2cap_sm_dispatch_get(uint8_t op)
{
    if (op > sizeof ble_l2cap_sm_dispatch / sizeof ble_l2cap_sm_dispatch[0]) {
        return NULL;
    }

    return ble_l2cap_sm_dispatch[op];
}

/**
 * Allocates a proc entry.
 *
 * Lock restrictions: None.
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
 * Frees the specified proc entry.  No-op if passed a null pointer.
 *
 * Lock restrictions: None.
 */
static void
ble_l2cap_sm_proc_free(struct ble_fsm_proc *proc)
{
    int rc;

    if (proc != NULL) {
        rc = os_memblock_put(&ble_l2cap_sm_proc_pool, proc);
        assert(rc == 0);
    }
}

/**
 * Lock restrictions: None.
 */
static int
ble_l2cap_sm_proc_new(uint16_t conn_handle, uint8_t op,
                      struct ble_l2cap_sm_proc **out_proc)
{
    *out_proc = ble_l2cap_sm_proc_alloc();
    if (*out_proc == NULL) {
        return BLE_HS_ENOMEM;
    }

    memset(*out_proc, 0, sizeof **out_proc);
    (*out_proc)->fsm_proc.op = op;
    (*out_proc)->fsm_proc.conn_handle = conn_handle;
    (*out_proc)->fsm_proc.tx_time = os_time_get();

    return 0;
}

static int
ble_l2cap_sm_proc_extract_cb(struct ble_fsm_proc *proc, void *arg)
{
    struct ble_l2cap_sm_extract_arg *extarg;

    extarg = arg;

    if (extarg->conn_handle != proc->conn_handle) {
        return BLE_FSM_EXTRACT_EKEEP_CONTINUE;
    }

    if (extarg->op != BLE_L2CAP_SM_PROC_OP_NONE && extarg->op != proc->op) {
        return BLE_FSM_EXTRACT_EKEEP_CONTINUE;
    }

    if (extarg->initiator != -1 &&
        extarg->initiator != !!(proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR)) {

        return BLE_FSM_EXTRACT_EKEEP_CONTINUE;
    }

    return BLE_FSM_EXTRACT_EMOVE_STOP;
}

/**
 * Searches the main proc list for an "expecting" entry whose connection handle
 * and op code match those specified.  If a matching entry is found, it is
 * removed from the list and returned.
 *
 * Lock restrictions:
 *     o Caller unlocks l2cap_sm.
 *
 * @param conn_handle           The connection handle to match against.
 * @param op                    The op code to match against.
 * @param identifier            The identifier to match against.
 *
 * @return                      The matching proc entry on success;
 *                                  null on failure.
 */
static struct ble_l2cap_sm_proc *
ble_l2cap_sm_proc_extract(struct ble_l2cap_sm_extract_arg *arg)
{
    struct ble_l2cap_sm_proc *proc;
    int rc;

    rc = ble_fsm_proc_extract(&ble_l2cap_sm_fsm,
                              (struct ble_fsm_proc **)&proc,
                              ble_l2cap_sm_proc_extract_cb, arg);

    if (rc != 0) {
        proc = NULL;
    }

    return proc;
}

/**
 * Sets the specified proc entry's "pending" flag (i.e., indicates that the
 * L2CAP sm procedure is stalled until it transmits its next request).
 *
 * Lock restrictions: None.
 */
static void
ble_l2cap_sm_proc_set_pending(struct ble_l2cap_sm_proc *proc)
{
    ble_fsm_proc_set_pending(&proc->fsm_proc);
    ble_hs_kick_l2cap_sm();
}

static void
ble_l2cap_sm_set_fail_state(struct ble_l2cap_sm_proc *proc,
                            uint8_t fail_reason)
{
    proc->fsm_proc.op = BLE_L2CAP_SM_PROC_OP_FAIL;
    proc->fail.reason = fail_reason;
    ble_l2cap_sm_proc_set_pending(proc);
}

/**
 * Lock restrictions: None.
 */
static int
ble_l2cap_sm_rx_noop(uint16_t conn_handle, uint8_t op, struct os_mbuf **om)
{
    return 0;
}

/*****************************************************************************
 * $pair                                                                     *
 *****************************************************************************/

static int
ble_l2cap_sm_pair_req_handle(struct ble_l2cap_sm_proc *proc,
                            struct ble_l2cap_sm_pair_cmd *req)
{
    proc->phase_1_2.pair_req = *req;
    ble_l2cap_sm_proc_set_pending(proc);

    return 0;
}

static int
ble_l2cap_sm_pair_rsp_handle(struct ble_l2cap_sm_proc *proc,
                             struct ble_l2cap_sm_pair_cmd *rsp)
{
    proc->phase_1_2.pair_rsp = *rsp;
    proc->fsm_proc.op = BLE_L2CAP_SM_PROC_OP_CONFIRM;
    ble_l2cap_sm_proc_set_pending(proc);

    /* XXX: Assume legacy "Just Works" for now. */
    proc->keygen_method = BLE_L2CAP_SM_KEYGEN_METHOD_JW;

    return 0;
}


static int
ble_l2cap_sm_pair_kick(struct ble_l2cap_sm_proc *proc)
{
    struct ble_l2cap_sm_pair_cmd cmd;
    int rc;

    /* XXX: Assume legacy "Just Works" for now. */
    cmd.io_cap = 3;
    cmd.oob_data_flag = 0;
    cmd.authreq = 0;
    cmd.max_enc_key_size = 16;
    cmd.init_key_dist = 0;
    cmd.resp_key_dist = 0;

    rc = ble_l2cap_sm_pair_cmd_tx(proc->fsm_proc.conn_handle, 0, &cmd);
    if (rc != 0) {
        ble_l2cap_sm_set_fail_state(proc, BLE_L2CAP_SM_ERR_UNSPECIFIED);
        return BLE_HS_EAGAIN;
    }

    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        proc->phase_1_2.pair_req = cmd;
    } else {
        proc->keygen_method = BLE_L2CAP_SM_KEYGEN_METHOD_JW;
        proc->fsm_proc.op = BLE_L2CAP_SM_PROC_OP_CONFIRM;
        proc->phase_1_2.pair_rsp = cmd;
    }

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
    static uint8_t zeros[16];

    struct ble_hs_conn *conn;
    int rc;

    ble_hs_conn_lock();
    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
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
    ble_hs_conn_unlock();

    if (conn == NULL) {
        return BLE_HS_ENOTCONN;
    }

    memcpy(k, zeros, 16); /* XXX: Assume legacy "Just Works." */

    rc = ble_l2cap_sm_pair_cmd_write(
        preq, BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_CMD_SZ, 1,
        &proc->phase_1_2.pair_req);
    assert(rc == 0);

    rc = ble_l2cap_sm_pair_cmd_write(
        pres, BLE_L2CAP_SM_HDR_SZ + BLE_L2CAP_SM_PAIR_CMD_SZ, 0,
        &proc->phase_1_2.pair_rsp);
    assert(rc == 0);

    return 0;
}

static int
ble_l2cap_sm_confirm_kick(struct ble_l2cap_sm_proc *proc)
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

    /* XXX: Generate random value for proc->rand_our.value. */
#ifdef BLE_HS_DEBUG
    if (ble_l2cap_sm_dbg_next_rand_set) {
        ble_l2cap_sm_dbg_next_rand_set = 0;
        memcpy(proc->phase_1_2.rand_our, ble_l2cap_sm_dbg_next_rand, 16);
    }
#endif

    rc = ble_l2cap_sm_confirm_prepare_args(proc, k, preq, pres, &iat, &rat,
                                           ia, ra);
    if (rc != 0) {
        ble_l2cap_sm_set_fail_state(proc, BLE_L2CAP_SM_ERR_UNSPECIFIED);
        return BLE_HS_EAGAIN;
    }

    rc = ble_l2cap_sm_alg_c1(k, proc->phase_1_2.rand_our, preq, pres, iat, rat,
                             ia, ra, cmd.value);
    if (rc != 0) {
        ble_l2cap_sm_set_fail_state(proc, BLE_L2CAP_SM_ERR_UNSPECIFIED);
        return BLE_HS_EAGAIN;
    }

    rc = ble_l2cap_sm_pair_confirm_tx(proc->fsm_proc.conn_handle, &cmd);
    if (rc != 0) {
        ble_l2cap_sm_set_fail_state(proc, BLE_L2CAP_SM_ERR_UNSPECIFIED);
        return BLE_HS_EAGAIN;
    }

    if (!(proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR)) {
        proc->fsm_proc.op = BLE_L2CAP_SM_PROC_OP_RANDOM;
    }

    return 0;
}

static int
ble_l2cap_sm_confirm_handle(struct ble_l2cap_sm_proc *proc,
                            struct ble_l2cap_sm_pair_confirm *cmd)
{
    memcpy(proc->phase_1_2.confirm_their, cmd->value, 16);

    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        proc->fsm_proc.op = BLE_L2CAP_SM_PROC_OP_RANDOM;
    }
    ble_l2cap_sm_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $random                                                                  *
 *****************************************************************************/

static int
ble_l2cap_sm_random_kick(struct ble_l2cap_sm_proc *proc)
{
    struct ble_l2cap_sm_pair_random cmd;
    int rc;

    memcpy(cmd.value, proc->phase_1_2.rand_our, 16);
    rc = ble_l2cap_sm_pair_random_tx(proc->fsm_proc.conn_handle, &cmd);
    if (rc != 0) {
        ble_l2cap_sm_set_fail_state(proc, BLE_L2CAP_SM_ERR_UNSPECIFIED);
        return BLE_HS_EAGAIN;
    }

    if (!(proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR)) {
        proc->fsm_proc.op = BLE_L2CAP_SM_PROC_OP_LTK;
    }

    return 0;
}

static int
ble_l2cap_sm_random_handle(struct ble_l2cap_sm_proc *proc,
                           struct ble_l2cap_sm_pair_random *cmd)
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
        ble_l2cap_sm_set_fail_state(proc, BLE_L2CAP_SM_ERR_UNSPECIFIED);
        return 0;
    }

    rc = ble_l2cap_sm_alg_c1(k, cmd->value, preq, pres, iat, rat,
                             ia, ra, confirm_val);
    if (rc != 0) {
        ble_l2cap_sm_set_fail_state(proc, BLE_L2CAP_SM_ERR_UNSPECIFIED);
        return 0;
    }

    if (memcmp(proc->phase_1_2.confirm_their, confirm_val, 16) != 0) {
        /* Random number mismatch. */
        ble_l2cap_sm_set_fail_state(proc, BLE_L2CAP_SM_ERR_CONFIRM_MISMATCH);
        return 0;
    }

    memcpy(proc->phase_1_2.rand_their, cmd->value, 16);

    if (proc->flags & BLE_L2CAP_SM_PROC_F_INITIATOR) {
        proc->fsm_proc.op = BLE_L2CAP_SM_PROC_OP_LTK;
    }
    ble_l2cap_sm_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $fail                                                                    *
 *****************************************************************************/

static int
ble_l2cap_sm_fail_kick(struct ble_l2cap_sm_proc *proc)
{
    struct ble_l2cap_sm_pair_fail cmd;

    cmd.reason = proc->fail.reason;
    ble_l2cap_sm_pair_fail_tx(proc->fsm_proc.conn_handle, &cmd);

    return BLE_HS_EDONE;
}

static int
ble_l2cap_sm_fail_handle(struct ble_l2cap_sm_proc *proc,
                         struct ble_l2cap_sm_pair_fail *cmd)
{
    ble_gap_security_fail(proc->fsm_proc.conn_handle,
                          BLE_HS_SM_ERR(cmd->reason));

    /* Procedure should now be terminated (return nonzero). */
    return 1;
}

/*****************************************************************************
 * $hci                                                                      *
 *****************************************************************************/

static int
ble_l2cap_sm_lt_key_req_reply_ack_handle(struct ble_l2cap_sm_proc *proc,
                                         struct ble_hci_ack *ack)
{
    if (ack->bha_status != 0) {
        ble_gap_security_fail(proc->fsm_proc.conn_handle, ack->bha_status);
    } else {
        proc->fsm_proc.op = BLE_L2CAP_SM_PROC_OP_ENC_CHANGE;
    }

    return ack->bha_status;
}

static int
ble_l2cap_sm_lt_key_req_reply_tx(void *arg)
{
    struct hci_lt_key_req_reply cmd;
    struct ble_l2cap_sm_proc *proc;
    int rc;

    proc = arg;

    assert(proc->fsm_proc.op == BLE_L2CAP_SM_PROC_OP_LTK_TXED);

    cmd.conn_handle = proc->fsm_proc.conn_handle;
    memcpy(cmd.long_term_key, proc->hci.key, 16);

    ble_hci_sched_set_ack_cb(ble_l2cap_sm_rx_lt_key_req_reply_ack, NULL);

    rc = host_hci_cmd_le_lt_key_req_reply(&cmd);
    return rc;
}

static int
ble_l2cap_sm_lt_key_req_kick(struct ble_l2cap_sm_proc *proc)
{
    int rc;

    rc = ble_hci_sched_enqueue(ble_l2cap_sm_lt_key_req_reply_tx, proc,
                               &proc->hci.handle);
    if (rc != 0) {
        ble_gap_security_fail(proc->fsm_proc.conn_handle, rc);
        return BLE_HS_EDONE;
    }

    proc->fsm_proc.op = BLE_L2CAP_SM_PROC_OP_LTK_TXED;

    return 0;
}

static int
ble_l2cap_sm_lt_key_req_handle(struct ble_l2cap_sm_proc *proc,
                               struct hci_le_lt_key_req *evt)
{
    uint8_t key[16];
    /* XXX: Assume legacy "Just Works". */
    uint8_t k[16] = {0};
    int rc;

    /* Generate the key. */
    rc = ble_l2cap_sm_alg_s1(k, proc->phase_1_2.rand_our,
                             proc->phase_1_2.rand_their, key);
    if (rc != 0) {
        ble_gap_security_fail(proc->fsm_proc.conn_handle, rc);
        return rc;
    }

    ble_l2cap_sm_proc_set_pending(proc);
    memcpy(proc->hci.key, key, sizeof key);

    return 0;
}

/*****************************************************************************
 * $rx                                                                       *
 *****************************************************************************/

static int
ble_l2cap_sm_rx_pair_req(uint16_t conn_handle, uint8_t op, struct os_mbuf **om)
{
    struct ble_l2cap_sm_pair_cmd req;
    struct ble_l2cap_sm_proc *proc;
    int rc;

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SM_PAIR_CMD_SZ);
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_sm_pair_cmd_parse((*om)->om_data, (*om)->om_len, &req);
    assert(rc == 0);

    BLE_HS_LOG(DEBUG, "rxed sm pair req; io_cap=0x%02x oob_data_flag=%d "
                      "authreq=0x%02x max_enc_key_size=%d "
                      "init_key_dist=0x%02x resp_key_dist=0x%02x\n",
               req.io_cap, req.oob_data_flag, req.authreq,
               req.max_enc_key_size, req.init_key_dist, req.resp_key_dist);

    /* XXX: Check connection state; reject if not appropriate. */

    proc = ble_l2cap_sm_proc_extract(
        &(struct ble_l2cap_sm_extract_arg) {
            .conn_handle = conn_handle,
            .op = BLE_L2CAP_SM_PROC_OP_NONE,
            .initiator = -1,
        }
    );
    if (proc != NULL) {
        /* Pairing already in progress; abort old procedure and start new. */
        /* XXX: Check the spec on this. */
        ble_l2cap_sm_proc_free(&proc->fsm_proc);
    }

    rc = ble_l2cap_sm_proc_new(conn_handle, BLE_L2CAP_SM_PROC_OP_PAIR,
                               &proc);
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_sm_pair_req_handle(proc, &req);
    ble_fsm_process_rx_status(&ble_l2cap_sm_fsm, &proc->fsm_proc, rc);

    return rc;
}

static int
ble_l2cap_sm_rx_pair_rsp(uint16_t conn_handle, uint8_t op, struct os_mbuf **om)
{
    struct ble_l2cap_sm_pair_cmd rsp;
    struct ble_l2cap_sm_proc *proc;
    int rc;

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SM_PAIR_CMD_SZ);
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_sm_pair_cmd_parse((*om)->om_data, (*om)->om_len, &rsp);
    assert(rc == 0);

    BLE_HS_LOG(DEBUG, "rxed sm pair rsp; io_cap=0x%02x oob_data_flag=%d "
                      "authreq=0x%02x max_enc_key_size=%d "
                      "init_key_dist=0x%02x resp_key_dist=0x%02x\n",
               rsp.io_cap, rsp.oob_data_flag, rsp.authreq,
               rsp.max_enc_key_size, rsp.init_key_dist, rsp.resp_key_dist);

    proc = ble_l2cap_sm_proc_extract(
        &(struct ble_l2cap_sm_extract_arg) {
            .conn_handle = conn_handle,
            .op = BLE_L2CAP_SM_PROC_OP_PAIR,
            .initiator = 1,
        }
    );
    if (proc == NULL) {
        return BLE_HS_ENOTCONN;
    }

    rc = ble_l2cap_sm_pair_rsp_handle(proc, &rsp);
    ble_fsm_process_rx_status(&ble_l2cap_sm_fsm, &proc->fsm_proc, rc);

    return 0;
}

static int
ble_l2cap_sm_rx_pair_confirm(uint16_t conn_handle, uint8_t op,
                             struct os_mbuf **om)
{
    struct ble_l2cap_sm_pair_confirm cmd;
    struct ble_l2cap_sm_proc *proc;
    int rc;

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SM_PAIR_CONFIRM_SZ);
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_sm_pair_confirm_parse((*om)->om_data, (*om)->om_len, &cmd);
    assert(rc == 0);

    BLE_HS_LOG(DEBUG, "rxed sm confirm cmd\n");

    proc = ble_l2cap_sm_proc_extract(
        &(struct ble_l2cap_sm_extract_arg) {
            .conn_handle = conn_handle,
            .op = BLE_L2CAP_SM_PROC_OP_CONFIRM,
            .initiator = -1,
        }
    );
    if (proc == NULL) {
        return BLE_HS_ENOTCONN;
    }

    rc = ble_l2cap_sm_confirm_handle(proc, &cmd);
    ble_fsm_process_rx_status(&ble_l2cap_sm_fsm, &proc->fsm_proc, rc);

    return 0;
}

static int
ble_l2cap_sm_rx_pair_random(uint16_t conn_handle, uint8_t op,
                            struct os_mbuf **om)
{
    struct ble_l2cap_sm_pair_random cmd;
    struct ble_l2cap_sm_proc *proc;
    int rc;

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SM_PAIR_RANDOM_SZ);
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_sm_pair_random_parse((*om)->om_data, (*om)->om_len, &cmd);
    assert(rc == 0);

    BLE_HS_LOG(DEBUG, "rxed sm random cmd\n");

    proc = ble_l2cap_sm_proc_extract(
        &(struct ble_l2cap_sm_extract_arg) {
            .conn_handle = conn_handle,
            .op = BLE_L2CAP_SM_PROC_OP_RANDOM,
            .initiator = -1,
        }
    );
    if (proc == NULL) {
        return BLE_HS_ENOTCONN;
    }

    rc = ble_l2cap_sm_random_handle(proc, &cmd);
    ble_fsm_process_rx_status(&ble_l2cap_sm_fsm, &proc->fsm_proc, rc);

    return 0;
}

static int
ble_l2cap_sm_rx_pair_fail(uint16_t conn_handle, uint8_t op,
                          struct os_mbuf **om)
{
    struct ble_l2cap_sm_pair_fail cmd;
    struct ble_l2cap_sm_proc *proc;
    int rc;

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SM_PAIR_FAIL_SZ);
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_sm_pair_fail_parse((*om)->om_data, (*om)->om_len, &cmd);
    assert(rc == 0);

    BLE_HS_LOG(DEBUG, "rxed sm fail cmd; reason=%d\n", cmd.reason);

    proc = ble_l2cap_sm_proc_extract(
        &(struct ble_l2cap_sm_extract_arg) {
            .conn_handle = conn_handle,
            .op = BLE_L2CAP_SM_PROC_OP_NONE,
            .initiator = -1,
        }
    );
    if (proc == NULL) {
        return BLE_HS_ENOTCONN;
    }

    rc = ble_l2cap_sm_fail_handle(proc, &cmd);
    ble_fsm_process_rx_status(&ble_l2cap_sm_fsm, &proc->fsm_proc, rc);

    return 0;
}

int
ble_l2cap_sm_rx_lt_key_req(struct hci_le_lt_key_req *evt)
{
    struct ble_l2cap_sm_proc *proc;
    int rc;

    proc = ble_l2cap_sm_proc_extract(
        &(struct ble_l2cap_sm_extract_arg) {
            .conn_handle = evt->connection_handle,
            .op = BLE_L2CAP_SM_PROC_OP_LTK,
            .initiator = 0,
        }
    );
    if (proc == NULL) {
        return BLE_HS_ENOTCONN;
    }

    rc = ble_l2cap_sm_lt_key_req_handle(proc, evt);
    ble_fsm_process_rx_status(&ble_l2cap_sm_fsm, &proc->fsm_proc, rc);

    return 0;
}

static void
ble_l2cap_sm_rx_lt_key_req_reply_ack(struct ble_hci_ack *ack, void *arg)
{
    struct ble_l2cap_sm_proc *proc;
    uint16_t conn_handle;
    int rc;

    if (ack->bha_params_len != BLE_HCI_LT_KEY_REQ_REPLY_ACK_PARAM_LEN) {
        return;
    }

    conn_handle = le16toh(ack->bha_params + 1);

    proc = ble_l2cap_sm_proc_extract(
        &(struct ble_l2cap_sm_extract_arg) {
            .conn_handle = conn_handle,
            .op = BLE_L2CAP_SM_PROC_OP_LTK_TXED,
            .initiator = 0,
        }
    );
    if (proc == NULL) {
        return;
    }

    rc = ble_l2cap_sm_lt_key_req_reply_ack_handle(proc, ack);
    ble_fsm_process_rx_status(&ble_l2cap_sm_fsm, &proc->fsm_proc, rc);
}

void
ble_l2cap_sm_rx_encryption_change(struct hci_encrypt_change *evt)
{
    struct ble_gap_sec_params sec_params;
    struct ble_l2cap_sm_proc *proc;

    proc = ble_l2cap_sm_proc_extract(
        &(struct ble_l2cap_sm_extract_arg) {
            .conn_handle = evt->connection_handle,
            .op = BLE_L2CAP_SM_PROC_OP_ENC_CHANGE,
            .initiator = -1,
        }
    );
    if (proc == NULL) {
        return;
    }

    if (evt->status != 0) {
        ble_gap_security_fail(evt->connection_handle,
                              BLE_HS_SM_ERR(evt->status));
    } else {
        sec_params.enc_type = proc->keygen_method; // XXX
        sec_params.enc_enabled = evt->encryption_enabled & 0x01; /* LE bit. */
        sec_params.auth_enabled = 0; // XXX
        ble_gap_encryption_changed(evt->connection_handle, &sec_params);
    }

    /* The pairing procedure is now complete. */
    ble_l2cap_sm_proc_free(&proc->fsm_proc);
}

/**
 * Lock restrictions:
 *     o Caller unlocks ble_hs_conn.
 */
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

    rc = ble_hs_misc_pullup_base(om, 1);
    if (rc != 0) {
        return BLE_HS_EBADDATA;
    }
    op = *(*om)->om_data;

    /* Strip L2CAP SM header from the front of the mbuf. */
    os_mbuf_adj(*om, 1);

    rx_cb = ble_l2cap_sm_dispatch_get(op);
    if (rx_cb != NULL) {
        rc = rx_cb(conn_handle, op, om);
    } else {
        rc = BLE_HS_ENOTSUP;
    }

    assert(rc == 0);
    return rc;
}

/*****************************************************************************
 * $init                                                                     *
 *****************************************************************************/

/**
 * Lock restrictions: None.
 */
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

/**
 * Lock restrictions:
 *     o Caller unlocks ble_hs_conn.
 */
void
ble_l2cap_sm_wakeup(void)
{
    ble_fsm_wakeup(&ble_l2cap_sm_fsm);
}

int
ble_l2cap_sm_init(void)
{
    int rc;

    free(ble_l2cap_sm_proc_mem);

    rc = ble_fsm_new(&ble_l2cap_sm_fsm, ble_l2cap_sm_proc_kick,
                     ble_l2cap_sm_proc_free);
    if (rc != 0) {
        goto err;
    }

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
