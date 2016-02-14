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
#include "ble_hs_priv.h"
#include "ble_hs_conn.h"
#include "ble_gap_priv.h"
#include "ble_l2cap_priv.h"
#include "ble_att_priv.h"
#include "ble_fsm_priv.h"
#include "ble_l2cap_priv.h"
#include "ble_l2cap_sig.h"

/*****************************************************************************
 * $definitions / declarations                                               *
 *****************************************************************************/

#define BLE_L2CAP_SIG_HEARTBEAT_PERIOD          1000    /* Milliseconds. */
#define BLE_L2CAP_SIG_UNRESPONSIVE_TIMEOUT      30000   /* Milliseconds. */

#define BLE_L2CAP_SIG_PROC_OP_UPDATE            0
#define BLE_L2CAP_SIG_PROC_OP_MAX               1

struct ble_l2cap_sig_proc {
    struct ble_fsm_proc fsm_proc;
    uint8_t id;

    union {
        struct {
            struct ble_l2cap_sig_update_params params;
            ble_l2cap_sig_update_fn *cb;
            void *cb_arg;
        } update;
    };
};

/**
 * Handles unresponsive timeouts and periodic retries in case of resource
 * shortage.
 */
static struct os_callout_func ble_l2cap_sig_heartbeat_timer;

typedef int ble_l2cap_sig_kick_fn(struct ble_l2cap_sig_proc *proc);

typedef int ble_l2cap_sig_rx_fn(uint16_t conn_handle,
                                struct ble_l2cap_sig_hdr *hdr,
                                struct os_mbuf **om);

static int ble_l2cap_sig_rx_noop(uint16_t conn_handle,
                                 struct ble_l2cap_sig_hdr *hdr,
                                 struct os_mbuf **om);
static int ble_l2cap_sig_update_req_rx(uint16_t conn_handle,
                                       struct ble_l2cap_sig_hdr *hdr,
                                       struct os_mbuf **om);
static int ble_l2cap_sig_update_rsp_rx(uint16_t conn_handle,
                                       struct ble_l2cap_sig_hdr *hdr,
                                       struct os_mbuf **om);

static int ble_l2cap_sig_update_kick(struct ble_l2cap_sig_proc *proc);

static ble_l2cap_sig_rx_fn * const ble_l2cap_sig_dispatch[] = {
    [BLE_L2CAP_SIG_OP_REJECT]               = ble_l2cap_sig_rx_noop,
    [BLE_L2CAP_SIG_OP_CONNECT_RSP]          = ble_l2cap_sig_rx_noop,
    [BLE_L2CAP_SIG_OP_CONFIG_RSP]           = ble_l2cap_sig_rx_noop,
    [BLE_L2CAP_SIG_OP_DISCONN_RSP]          = ble_l2cap_sig_rx_noop,
    [BLE_L2CAP_SIG_OP_ECHO_RSP]             = ble_l2cap_sig_rx_noop,
    [BLE_L2CAP_SIG_OP_INFO_RSP]             = ble_l2cap_sig_rx_noop,
    [BLE_L2CAP_SIG_OP_CREATE_CHAN_RSP]      = ble_l2cap_sig_rx_noop,
    [BLE_L2CAP_SIG_OP_MOVE_CHAN_RSP]        = ble_l2cap_sig_rx_noop,
    [BLE_L2CAP_SIG_OP_MOVE_CHAN_CONF_RSP]   = ble_l2cap_sig_rx_noop,
    [BLE_L2CAP_SIG_OP_UPDATE_REQ]           = ble_l2cap_sig_update_req_rx,
    [BLE_L2CAP_SIG_OP_UPDATE_RSP]           = ble_l2cap_sig_update_rsp_rx,
    [BLE_L2CAP_SIG_OP_CREDIT_CONNECT_RSP]   = ble_l2cap_sig_rx_noop,
};

static ble_l2cap_sig_kick_fn * const ble_l2cap_sig_kick[] = {
    [BLE_L2CAP_SIG_PROC_OP_UPDATE]          = ble_l2cap_sig_update_kick,
};

static uint8_t ble_l2cap_sig_cur_id;

static void *ble_l2cap_sig_proc_mem;
static struct os_mempool ble_l2cap_sig_proc_pool;

static struct ble_fsm ble_l2cap_sig_fsm;

/*****************************************************************************
 * $mutex                                                                    *
 *****************************************************************************/

void
ble_l2cap_sig_lock(void)
{
    ble_fsm_lock(&ble_l2cap_sig_fsm);
}

void
ble_l2cap_sig_unlock(void)
{
    ble_fsm_unlock(&ble_l2cap_sig_fsm);
}

int
ble_l2cap_sig_locked_by_cur_task(void)
{
    return ble_fsm_locked_by_cur_task(&ble_l2cap_sig_fsm);
}

/*****************************************************************************
 * $misc                                                                     *
 *****************************************************************************/

/**
 * Lock restrictions: None.
 */
static ble_l2cap_sig_kick_fn *
ble_l2cap_sig_kick_get(uint8_t op)
{
    if (op > BLE_L2CAP_SIG_PROC_OP_MAX) {
        return NULL;
    }

    return ble_l2cap_sig_kick[op];
}

static int
ble_l2cap_sig_proc_kick(struct ble_fsm_proc *proc)
{
    ble_l2cap_sig_kick_fn *kick_cb;
    int rc;

    kick_cb = ble_l2cap_sig_kick_get(proc->op);
    rc = kick_cb((struct ble_l2cap_sig_proc *)proc);

    return rc;
}

/**
 * Lock restrictions:
 *     o Caller unlocks ble_hs_conn.
 */
static int
ble_l2cap_sig_conn_chan_find(uint16_t conn_handle,
                             struct ble_hs_conn **out_conn,
                             struct ble_l2cap_chan **out_chan)
{
    ble_hs_conn_lock();

    *out_conn = ble_hs_conn_find(conn_handle);
    if (*out_conn != NULL) {
        *out_chan = ble_hs_conn_chan_find(*out_conn, BLE_L2CAP_CID_SIG);
        assert(*out_chan != NULL);
    }

    ble_hs_conn_unlock();

    if (*out_conn == NULL) {
        return BLE_HS_ENOTCONN;
    } else {
        return 0;
    }
}

/**
 * Lock restrictions: None.
 */
static uint8_t
ble_l2cap_sig_next_id(void)
{
    ble_l2cap_sig_cur_id++;
    if (ble_l2cap_sig_cur_id == 0) {
        /* An ID of 0 is illegal. */
        ble_l2cap_sig_cur_id = 1;
    }

    return ble_l2cap_sig_cur_id;
}

/**
 * Lock restrictions: None.
 */
static ble_l2cap_sig_rx_fn *
ble_l2cap_sig_dispatch_get(uint8_t op)
{
    if (op > BLE_L2CAP_SIG_OP_MAX) {
        return NULL;
    }

    return ble_l2cap_sig_dispatch[op];
}

/**
 * Allocates a proc entry.
 *
 * Lock restrictions: None.
 *
 * @return                      An entry on success; null on failure.
 */
static struct ble_l2cap_sig_proc *
ble_l2cap_sig_proc_alloc(void)
{
    struct ble_l2cap_sig_proc *proc;

    proc = os_memblock_get(&ble_l2cap_sig_proc_pool);
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
ble_l2cap_sig_proc_free(struct ble_fsm_proc *proc)
{
    int rc;

    if (proc != NULL) {
        rc = os_memblock_put(&ble_l2cap_sig_proc_pool, proc);
        assert(rc == 0);
    }
}

/**
 * Lock restrictions: None.
 */
static int
ble_l2cap_sig_new_proc(uint16_t conn_handle, uint8_t op,
                       struct ble_l2cap_sig_proc **out_proc)
{
    *out_proc = ble_l2cap_sig_proc_alloc();
    if (*out_proc == NULL) {
        return BLE_HS_ENOMEM;
    }

    memset(*out_proc, 0, sizeof **out_proc);
    (*out_proc)->fsm_proc.op = op;
    (*out_proc)->fsm_proc.conn_handle = conn_handle;
    (*out_proc)->fsm_proc.tx_time = os_time_get();

    STAILQ_INSERT_TAIL(&ble_l2cap_sig_fsm.procs, &(*out_proc)->fsm_proc, next);

    return 0;
}

/**
 * Tests if a proc entry fits the specified criteria.
 *
 * Lock restrictions: None.
 *
 * @param proc                  The procedure to test.
 * @param conn_handle           The connection handle to match against.
 * @param op                    The op code to match against/
 * @param id                    The identifier to match against.
 * @param expecting_only        1=Only match entries expecting a response;
 *                                  0=Ignore this criterion.
 *
 * @return                      1 if the proc matches; 0 otherwise.
 */
static int
ble_l2cap_sig_proc_matches(struct ble_l2cap_sig_proc *proc,
                           uint16_t conn_handle, uint8_t op, uint8_t id,
                           int expecting_only)
{
    if (conn_handle != proc->fsm_proc.conn_handle) {
        return 0;
    }

    if (op != proc->fsm_proc.op) {
        return 0;
    }

    if (id != proc->id) {
        return 0;
    }

    if (expecting_only && !(proc->fsm_proc.flags & BLE_FSM_PROC_F_EXPECTING)) {
        return 0;
    }

    return 1;
}

struct ble_l2cap_sig_proc_extract_arg {
    uint16_t conn_handle;
    uint8_t op;
    uint8_t id;
};

static int
ble_l2cap_sig_proc_extract_cb(struct ble_fsm_proc *proc, void *arg)
{
    struct ble_l2cap_sig_proc_extract_arg *extract_arg;

    extract_arg = arg;

    if (ble_l2cap_sig_proc_matches((struct ble_l2cap_sig_proc *)proc,
                                   extract_arg->conn_handle, extract_arg->op,
                                   extract_arg->id, 1)) {
        return BLE_FSM_EXTRACT_EMOVE_STOP;
    } else {
        return BLE_FSM_EXTRACT_EKEEP_CONTINUE;
    }
}

/**
 * Searches the main proc list for an "expecting" entry whose connection handle
 * and op code match those specified.  If a matching entry is found, it is
 * removed from the list and returned.
 *
 * Lock restrictions:
 *     o Caller unlocks l2cap_sig.
 *
 * @param conn_handle           The connection handle to match against.
 * @param op                    The op code to match against.
 * @param identifier            The identifier to match against.
 *
 * @return                      The matching proc entry on success;
 *                                  null on failure.
 */
static struct ble_l2cap_sig_proc *
ble_l2cap_sig_proc_extract(uint16_t conn_handle, uint8_t op,
                           uint8_t identifier)
{
    struct ble_l2cap_sig_proc_extract_arg extract_arg;
    struct ble_l2cap_sig_proc *proc;
    int rc;

    extract_arg.conn_handle = conn_handle;
    extract_arg.op = op;
    extract_arg.id = identifier;

    rc = ble_fsm_proc_extract(&ble_l2cap_sig_fsm,
                              (struct ble_fsm_proc **)&proc,
                              ble_l2cap_sig_proc_extract_cb, &extract_arg);

    if (rc != 0) {
        proc = NULL;
    }

    return proc;
}

/**
 * Sets the specified proc entry's "pending" flag (i.e., indicates that the
 * L2CAP sig procedure is stalled until it transmits its next request).
 *
 * Lock restrictions: None.
 */
static void
ble_l2cap_sig_proc_set_pending(struct ble_l2cap_sig_proc *proc)
{
    ble_fsm_proc_set_pending(&proc->fsm_proc);
    ble_hs_kick_l2cap_sig();
}

/**
 * Lock restrictions: None.
 */
static int
ble_l2cap_sig_rx_noop(uint16_t conn_handle,
                      struct ble_l2cap_sig_hdr *hdr,
                      struct os_mbuf **om)
{
    return 0;
}

/*****************************************************************************
 * $update                                                                   *
 *****************************************************************************/

/**
 * Lock restrictions:
 *     o Caller unlocks ble_hs_conn.
 */
static void
ble_l2cap_sig_update_call_cb(struct ble_l2cap_sig_proc *proc, int status)
{
    assert(!ble_hs_conn_locked_by_cur_task());

    if (proc->update.cb != NULL) {
        proc->update.cb(status, proc->update.cb_arg);
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks ble_hs_conn.
 */
int
ble_l2cap_sig_update_req_rx(uint16_t conn_handle,
                            struct ble_l2cap_sig_hdr *hdr,
                            struct os_mbuf **om)
{
    struct ble_l2cap_sig_update_req req;
    struct ble_gap_upd_params params;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint16_t l2cap_result;
    int rc;

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SIG_UPDATE_REQ_SZ);
    if (rc != 0) {
        return rc;
    }

    ble_hs_conn_lock();
    rc = ble_l2cap_sig_conn_chan_find(conn_handle, &conn, &chan);
    if (rc == 0) {
        rc = ble_l2cap_sig_update_req_parse((*om)->om_data, (*om)->om_len,
                                            &req);
        assert(rc == 0);

        /* Only a master can process an update request. */
        if (!(conn->bhc_flags & BLE_HS_CONN_F_MASTER)) {
            ble_l2cap_sig_reject_tx(conn, chan, hdr->identifier,
                                    BLE_L2CAP_ERR_CMD_NOT_UNDERSTOOD);
            rc = BLE_HS_ENOTSUP;
        }
    }
    ble_hs_conn_unlock();

    if (rc != 0) {
        return rc;
    }

    params.itvl_min = req.itvl_min;
    params.itvl_max = req.itvl_max;
    params.latency = req.slave_latency;
    params.supervision_timeout = req.timeout_multiplier;
    params.min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN;
    params.max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN;

    /* Ask application if slave's connection parameters are acceptable. */
    rc = ble_gap_rx_l2cap_update_req(conn_handle, &params);
    if (rc == 0) {
        /* Application agrees to accept parameters; schedule update. */
        rc = ble_gap_update_params(conn_handle, &params);
        if (rc != 0) {
            return rc;
        }
        l2cap_result = BLE_L2CAP_SIG_UPDATE_RSP_RESULT_ACCEPT;
    } else {
        l2cap_result = BLE_L2CAP_SIG_UPDATE_RSP_RESULT_REJECT;
    }

    /* Send L2CAP response. */
    ble_hs_conn_lock();
    rc = ble_l2cap_sig_conn_chan_find(conn_handle, &conn, &chan);
    if (rc == 0) {
        rc = ble_l2cap_sig_update_rsp_tx(conn, chan, hdr->identifier,
                                         l2cap_result);
    }
    ble_hs_conn_unlock();

    return rc;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_l2cap_sig_update_rsp_rx(uint16_t conn_handle,
                            struct ble_l2cap_sig_hdr *hdr,
                            struct os_mbuf **om)
{
    struct ble_l2cap_sig_update_rsp rsp;
    struct ble_l2cap_sig_proc *proc;
    int cb_status;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = ble_l2cap_sig_proc_extract(conn_handle,
                                      BLE_L2CAP_SIG_PROC_OP_UPDATE,
                                      hdr->identifier);
    if (proc ==  NULL) {
        return BLE_HS_ENOENT;
    }

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SIG_UPDATE_RSP_SZ);
    if (rc != 0) {
        cb_status = rc;
        goto done;
    }

    rc = ble_l2cap_sig_update_rsp_parse((*om)->om_data, (*om)->om_len, &rsp);
    assert(rc == 0);

    switch (rsp.result) {
    case BLE_L2CAP_SIG_UPDATE_RSP_RESULT_ACCEPT:
        cb_status = 0;
        rc = 0;
        break;

    case BLE_L2CAP_SIG_UPDATE_RSP_RESULT_REJECT:
        cb_status = BLE_HS_EREJECT;
        rc = 0;
        break;

    default:
        cb_status = BLE_HS_EBADDATA;
        rc = BLE_HS_EBADDATA;
        break;
    }

done:
    ble_l2cap_sig_update_call_cb(proc, cb_status);
    ble_l2cap_sig_proc_free(&proc->fsm_proc);
    return rc;
}

/**
 * Lock restrictions:
 *     o Caller locks ble_hs_conn.
 */
static int
ble_l2cap_sig_update_kick(struct ble_l2cap_sig_proc *proc)
{
    struct ble_l2cap_sig_update_req req;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        /* Not connected; abort the proedure. */
        rc = BLE_HS_ENOTCONN;
    } else if (conn->bhc_flags & BLE_HS_CONN_F_MASTER) {
        /* Only the slave can initiate the L2CAP connection update
         * procedure.
         */
        rc = BLE_HS_EINVAL;
    } else {
        chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_SIG);
        assert(chan != NULL);

        proc->id = ble_l2cap_sig_next_id();
        req.itvl_min = proc->update.params.itvl_min;
        req.itvl_max = proc->update.params.itvl_max;
        req.slave_latency = proc->update.params.slave_latency;
        req.timeout_multiplier = proc->update.params.timeout_multiplier;

        rc = ble_l2cap_sig_update_req_tx(conn, chan, proc->id, &req);
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        ble_l2cap_sig_update_call_cb(proc, rc);
        return BLE_HS_EDONE;
    }
}

/**
 * Lock restrictions: None.
 */
int
ble_l2cap_sig_update(uint16_t conn_handle,
                     struct ble_l2cap_sig_update_params *params,
                     ble_l2cap_sig_update_fn *cb, void *cb_arg)
{
    struct ble_l2cap_sig_proc *proc;
    int rc;

    rc = ble_l2cap_sig_new_proc(conn_handle, BLE_L2CAP_SIG_PROC_OP_UPDATE,
                                &proc);
    if (rc != 0) {
        return rc;
    }

    proc->update.params = *params;
    proc->update.cb = cb;
    proc->update.cb_arg = cb_arg;

    ble_l2cap_sig_proc_set_pending(proc);

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks ble_hs_conn.
 */
static int
ble_l2cap_sig_rx(uint16_t conn_handle, struct os_mbuf **om)
{
    struct ble_l2cap_sig_hdr hdr;
    struct ble_l2cap_chan *chan;
    ble_l2cap_sig_rx_fn *rx_cb;
    struct ble_hs_conn *conn;
    int rc;

    BLE_HS_LOG(DEBUG, "L2CAP - rxed signalling msg: ");
    ble_hs_misc_log_mbuf(*om);
    BLE_HS_LOG(DEBUG, "\n");

    rc = ble_hs_misc_pullup_base(om, BLE_L2CAP_SIG_HDR_SZ);
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_sig_hdr_parse((*om)->om_data, (*om)->om_len, &hdr);
    assert(rc == 0);

    /* Strip L2CAP sig header from the front of the mbuf. */
    os_mbuf_adj(*om, BLE_L2CAP_SIG_HDR_SZ);

    if (OS_MBUF_PKTLEN(*om) != hdr.length) {
        return BLE_HS_EBADDATA;
    }

    rx_cb = ble_l2cap_sig_dispatch_get(hdr.op);
    if (rx_cb == NULL) {
        ble_hs_conn_lock();
        rc = ble_l2cap_sig_conn_chan_find(conn_handle, &conn, &chan);
        if (rc == 0) {
            ble_l2cap_sig_reject_tx(conn, chan, hdr.identifier,
                                    BLE_L2CAP_ERR_CMD_NOT_UNDERSTOOD);
        }
        ble_hs_conn_unlock();

        rc = BLE_HS_ENOTSUP;
    } else {
        rc = rx_cb(conn_handle, &hdr, om);
    }

    return rc;
}

/**
 * Lock restrictions: None.
 */
struct ble_l2cap_chan *
ble_l2cap_sig_create_chan(void)
{
    struct ble_l2cap_chan *chan;

    chan = ble_l2cap_chan_alloc();
    if (chan == NULL) {
        return NULL;
    }

    chan->blc_cid = BLE_L2CAP_CID_SIG;
    chan->blc_my_mtu = BLE_L2CAP_SIG_MTU;
    chan->blc_default_mtu = BLE_L2CAP_SIG_MTU;
    chan->blc_rx_fn = ble_l2cap_sig_rx;

    return chan;
}

static int
ble_l2cap_sig_heartbeat_extract_cb(struct ble_fsm_proc *proc, void *arg)
{
    uint32_t *now;

    now = arg;

    if (proc->flags & BLE_FSM_PROC_F_EXPECTING) {
        if (*now - proc->tx_time >= BLE_L2CAP_SIG_UNRESPONSIVE_TIMEOUT) {
            return BLE_FSM_EXTRACT_EMOVE_CONTINUE;
        }
    }

    /* If a proc failed due to low memory, don't extract it, but set its 
     * pending bit.
     */
    if (proc->flags & BLE_FSM_PROC_F_NO_MEM) {
        proc->flags &= ~BLE_FSM_PROC_F_NO_MEM;
        if (ble_fsm_proc_can_pend(proc)) {
            ble_fsm_proc_set_pending(proc);
        }
    }

    return BLE_FSM_EXTRACT_EKEEP_CONTINUE;
}

/**
 * Applies periodic checks and actions to all active procedures.
 *
 * All procedures that failed due to memory exaustion have their pending flag
 * set so they can be retried.
 *
 * All procedures that have been expecting a response for longer than five
 * seconds are aborted, and their corresponding connection is terminated.
 *
 * Called by the heartbeat timer; executed every second.
 *
 * Lock restrictions: None.
 */
static void
ble_l2cap_sig_heartbeat(void *unused)
{
    struct ble_fsm_proc_list temp_list;
    struct ble_fsm_proc *proc;
    uint32_t ticks;
    uint32_t now;
    int rc;

    ble_hs_misc_assert_no_locks();

    now = os_time_get();

    /* Remove timed-out procedures from the main list and insert them into a
     * temporary list.  For any stalled procedures, set their pending bit so
     * they can be retried.
     */
    ble_fsm_proc_extract_list(&ble_l2cap_sig_fsm, &temp_list,
                              ble_l2cap_sig_heartbeat_extract_cb, &now);

    /* Terminate the connection associated with each timed-out procedure. */
    STAILQ_FOREACH(proc, &temp_list, next) {
        ble_gap_terminate(proc->conn_handle);
    }

    /* Concatenate the list of timed out procedures back onto the end of the
     * main list.
     */
    ble_fsm_proc_concat(&ble_l2cap_sig_fsm, &temp_list);

    ticks = BLE_L2CAP_SIG_HEARTBEAT_PERIOD * OS_TICKS_PER_SEC / 1000;
    rc = os_callout_reset(&ble_l2cap_sig_heartbeat_timer.cf_c, ticks);
        
    assert(rc == 0);
}

/**
 * Lock restrictions:
 *     o Caller unlocks ble_hs_conn.
 */
void
ble_l2cap_sig_wakeup(void)
{
    ble_fsm_wakeup(&ble_l2cap_sig_fsm);
}

/**
 * Lock restrictions: None.
 */
int
ble_l2cap_sig_init(void)
{
    int rc;

    free(ble_l2cap_sig_proc_mem);

    rc = ble_fsm_new(&ble_l2cap_sig_fsm, ble_l2cap_sig_proc_kick,
                     ble_l2cap_sig_proc_free);
    if (rc != 0) {
        goto err;
    }

    if (ble_hs_cfg.max_l2cap_sig_procs > 0) {
        ble_l2cap_sig_proc_mem = malloc(
            OS_MEMPOOL_BYTES(ble_hs_cfg.max_l2cap_sig_procs,
                             sizeof (struct ble_l2cap_sig_proc)));
        if (ble_l2cap_sig_proc_mem == NULL) {
            rc = BLE_HS_ENOMEM;
            goto err;
        }

        rc = os_mempool_init(&ble_l2cap_sig_proc_pool,
                             ble_hs_cfg.max_l2cap_sig_procs,
                             sizeof (struct ble_l2cap_sig_proc),
                             ble_l2cap_sig_proc_mem,
                             "ble_l2cap_sig_proc_pool");
        if (rc != 0) {
            goto err;
        }
    }

    os_callout_func_init(&ble_l2cap_sig_heartbeat_timer, &ble_hs_evq,
                         ble_l2cap_sig_heartbeat, NULL);

    return 0;

err:
    free(ble_l2cap_sig_proc_mem);
    return rc;
}
