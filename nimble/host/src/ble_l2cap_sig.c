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
 * L2CAP Signaling (channel ID = 5).
 *
 * Design overview:
 *
 * L2CAP sig procedures are initiated by the application via function calls.
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
 * 2. The only resource protected by the mutex is the list of active procedures
 *    (ble_l2cap_sig_procs).  Thread-safety is achieved by locking the mutex
 *    during removal and insertion operations.  Procedure objects are only
 *    modified while they are not in the list.
 */

#include <string.h>
#include <errno.h>
#include "console/console.h"
#include "nimble/ble.h"
#include "ble_hs_priv.h"

/*****************************************************************************
 * $definitions / declarations                                               *
 *****************************************************************************/

#define BLE_L2CAP_SIG_UNRESPONSIVE_TIMEOUT      30000   /* Milliseconds. */

#define BLE_L2CAP_SIG_PROC_OP_UPDATE            0
#define BLE_L2CAP_SIG_PROC_OP_MAX               1

struct ble_l2cap_sig_proc {
    STAILQ_ENTRY(ble_l2cap_sig_proc) next;

    uint32_t exp_os_ticks;
    uint16_t conn_handle;
    uint8_t op;
    uint8_t id;

    union {
        struct {
            ble_l2cap_sig_update_fn *cb;
            void *cb_arg;
        } update;
    };
};

STAILQ_HEAD(ble_l2cap_sig_proc_list, ble_l2cap_sig_proc);

static struct ble_l2cap_sig_proc_list ble_l2cap_sig_procs;

typedef int ble_l2cap_sig_rx_fn(uint16_t conn_handle,
                                struct ble_l2cap_sig_hdr *hdr,
                                struct os_mbuf **om);

static ble_l2cap_sig_rx_fn ble_l2cap_sig_rx_noop;
static ble_l2cap_sig_rx_fn ble_l2cap_sig_update_req_rx;
static ble_l2cap_sig_rx_fn ble_l2cap_sig_update_rsp_rx;

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

static uint8_t ble_l2cap_sig_cur_id;

static void *ble_l2cap_sig_proc_mem;
static struct os_mempool ble_l2cap_sig_proc_pool;

/*****************************************************************************
 * $debug                                                                    *
 *****************************************************************************/

static void
ble_l2cap_sig_dbg_assert_proc_not_inserted(struct ble_l2cap_sig_proc *proc)
{
#if BLE_HS_DEBUG
    struct ble_l2cap_sig_proc *cur;

    STAILQ_FOREACH(cur, &ble_l2cap_sig_procs, next) {
        BLE_HS_DBG_ASSERT(cur != proc);
    }
#endif
}

/*****************************************************************************
 * $misc                                                                     *
 *****************************************************************************/

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
 */
static void
ble_l2cap_sig_proc_free(struct ble_l2cap_sig_proc *proc)
{
    int rc;

    if (proc != NULL) {
        ble_l2cap_sig_dbg_assert_proc_not_inserted(proc);

        rc = os_memblock_put(&ble_l2cap_sig_proc_pool, proc);
        BLE_HS_DBG_ASSERT_EVAL(rc == 0);
    }
}

static void
ble_l2cap_sig_proc_insert(struct ble_l2cap_sig_proc *proc)
{
    ble_l2cap_sig_dbg_assert_proc_not_inserted(proc);

    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());
    STAILQ_INSERT_HEAD(&ble_l2cap_sig_procs, proc, next);
}

/**
 * Tests if a proc entry fits the specified criteria.
 *
 * @param proc                  The procedure to test.
 * @param conn_handle           The connection handle to match against.
 * @param op                    The op code to match against/
 * @param id                    The identifier to match against.
 *                                  0=Ignore this criterion.
 *
 * @return                      1 if the proc matches; 0 otherwise.
 */
static int
ble_l2cap_sig_proc_matches(struct ble_l2cap_sig_proc *proc,
                           uint16_t conn_handle, uint8_t op, uint8_t id)
{
    if (conn_handle != proc->conn_handle) {
        return 0;
    }

    if (op != proc->op) {
        return 0;
    }

    if (id != proc->id) {
        return 0;
    }

    return 1;
}

/**
 * Searches the main proc list for an "expecting" entry whose connection handle
 * and op code match those specified.  If a matching entry is found, it is
 * removed from the list and returned.
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
    struct ble_l2cap_sig_proc *proc;
    struct ble_l2cap_sig_proc *prev;

    ble_hs_lock();

    prev = NULL;
    STAILQ_FOREACH(proc, &ble_l2cap_sig_procs, next) {
        if (ble_l2cap_sig_proc_matches(proc, conn_handle, op, identifier)) {
            if (prev == NULL) {
                STAILQ_REMOVE_HEAD(&ble_l2cap_sig_procs, next);
            } else {
                STAILQ_REMOVE_AFTER(&ble_l2cap_sig_procs, prev, next);
            }
            break;
        }
    }

    ble_hs_unlock();

    return proc;
}

static int
ble_l2cap_sig_rx_noop(uint16_t conn_handle,
                      struct ble_l2cap_sig_hdr *hdr,
                      struct os_mbuf **om)
{
    return BLE_HS_ENOTSUP;
}

/*****************************************************************************
 * $update                                                                   *
 *****************************************************************************/

static void
ble_l2cap_sig_update_call_cb(struct ble_l2cap_sig_proc *proc, int status)
{
    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());

    if (status != 0) {
        STATS_INC(ble_l2cap_stats, update_fail);
    }

    if (proc->update.cb != NULL) {
        proc->update.cb(status, proc->update.cb_arg);
    }
}

int
ble_l2cap_sig_update_req_rx(uint16_t conn_handle,
                            struct ble_l2cap_sig_hdr *hdr,
                            struct os_mbuf **om)
{
    struct ble_l2cap_sig_update_req req;
    struct ble_gap_upd_params params;
    struct ble_l2cap_chan *chan;
    ble_hs_conn_flags_t conn_flags;
    struct ble_hs_conn *conn;
    uint16_t l2cap_result;
    int sig_err;
    int rc;

    l2cap_result = 0; /* Silence spurious gcc warning. */

    rc = ble_hs_mbuf_pullup_base(om, BLE_L2CAP_SIG_UPDATE_REQ_SZ);
    if (rc != 0) {
        return rc;
    }

    rc = ble_hs_atomic_conn_flags(conn_handle, &conn_flags);
    if (rc != 0) {
        return rc;
    }

    /* Only a master can process an update request. */
    sig_err = !(conn_flags & BLE_HS_CONN_F_MASTER);
    if (!sig_err) {
        ble_l2cap_sig_update_req_parse((*om)->om_data, (*om)->om_len, &req);

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
    }

    /* Send L2CAP response. */
    ble_hs_lock();
    ble_hs_misc_conn_chan_find_reqd(conn_handle, BLE_L2CAP_CID_SIG,
                                    &conn, &chan);
    if (!sig_err) {
        rc = ble_l2cap_sig_update_rsp_tx(conn, chan, hdr->identifier,
                                         l2cap_result);
    } else {
        ble_l2cap_sig_reject_tx(conn, chan, hdr->identifier,
                                BLE_L2CAP_SIG_ERR_CMD_NOT_UNDERSTOOD,
                                NULL, 0);
        rc = BLE_HS_L2C_ERR(BLE_L2CAP_SIG_ERR_CMD_NOT_UNDERSTOOD);
    }
    ble_hs_unlock();

    return rc;
}

static int
ble_l2cap_sig_update_rsp_rx(uint16_t conn_handle,
                            struct ble_l2cap_sig_hdr *hdr,
                            struct os_mbuf **om)
{
    struct ble_l2cap_sig_update_rsp rsp;
    struct ble_l2cap_sig_proc *proc;
    int cb_status;
    int rc;

    proc = ble_l2cap_sig_proc_extract(conn_handle,
                                      BLE_L2CAP_SIG_PROC_OP_UPDATE,
                                      hdr->identifier);
    if (proc == NULL) {
        return BLE_HS_ENOENT;
    }

    rc = ble_hs_mbuf_pullup_base(om, BLE_L2CAP_SIG_UPDATE_RSP_SZ);
    if (rc != 0) {
        cb_status = rc;
        goto done;
    }

    ble_l2cap_sig_update_rsp_parse((*om)->om_data, (*om)->om_len, &rsp);

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
    ble_l2cap_sig_proc_free(proc);
    return rc;
}

int
ble_l2cap_sig_update(uint16_t conn_handle,
                     struct ble_l2cap_sig_update_params *params,
                     ble_l2cap_sig_update_fn *cb, void *cb_arg)
{
    struct ble_l2cap_sig_update_req req;
    struct ble_l2cap_sig_proc *proc;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    proc = NULL;

    STATS_INC(ble_l2cap_stats, update_init);

    ble_hs_lock();

    ble_hs_misc_conn_chan_find_reqd(conn_handle, BLE_L2CAP_CID_SIG,
                                    &conn, &chan);
    if (conn->bhc_flags & BLE_HS_CONN_F_MASTER) {
        /* Only the slave can initiate the L2CAP connection update
         * procedure.
         */
        rc = BLE_HS_EINVAL;
        goto done;
    }

    proc = ble_l2cap_sig_proc_alloc();
    if (proc == NULL) {
        STATS_INC(ble_l2cap_stats, update_fail);
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_L2CAP_SIG_PROC_OP_UPDATE;
    proc->id = ble_l2cap_sig_next_id();
    proc->conn_handle = conn_handle;
    proc->exp_os_ticks = os_time_get() + BLE_L2CAP_SIG_UNRESPONSIVE_TIMEOUT;
    proc->update.cb = cb;
    proc->update.cb_arg = cb_arg;

    req.itvl_min = params->itvl_min;
    req.itvl_max = params->itvl_max;
    req.slave_latency = params->slave_latency;
    req.timeout_multiplier = params->timeout_multiplier;

    rc = ble_l2cap_sig_update_req_tx(conn, chan, proc->id, &req);
    if (rc == 0) {
        ble_l2cap_sig_proc_insert(proc);
    }

done:
    ble_hs_unlock();

    if (rc != 0) {
        ble_l2cap_sig_proc_free(proc);
    }

    return rc;
}

static int
ble_l2cap_sig_rx(uint16_t conn_handle, struct os_mbuf **om)
{
    struct ble_l2cap_sig_hdr hdr;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    ble_l2cap_sig_rx_fn *rx_cb;
    int rc;

    STATS_INC(ble_l2cap_stats, sig_rx);
    BLE_HS_LOG(DEBUG, "L2CAP - rxed signalling msg: ");
    ble_hs_log_mbuf(*om);
    BLE_HS_LOG(DEBUG, "\n");

    rc = ble_hs_mbuf_pullup_base(om, BLE_L2CAP_SIG_HDR_SZ);
    if (rc != 0) {
        return rc;
    }

    ble_l2cap_sig_hdr_parse((*om)->om_data, (*om)->om_len, &hdr);

    /* Strip L2CAP sig header from the front of the mbuf. */
    os_mbuf_adj(*om, BLE_L2CAP_SIG_HDR_SZ);

    if (OS_MBUF_PKTLEN(*om) != hdr.length) {
        return BLE_HS_EBADDATA;
    }

    rx_cb = ble_l2cap_sig_dispatch_get(hdr.op);
    if (rx_cb == NULL) {
        ble_hs_lock();
        ble_hs_misc_conn_chan_find_reqd(conn_handle, BLE_L2CAP_CID_SIG,
                                        &conn, &chan);
        ble_l2cap_sig_reject_tx(conn, chan, hdr.identifier,
                                BLE_L2CAP_SIG_ERR_CMD_NOT_UNDERSTOOD,
                                NULL, 0);
        rc = BLE_HS_L2C_ERR(BLE_L2CAP_SIG_ERR_CMD_NOT_UNDERSTOOD);
        ble_hs_unlock();
    } else {
        rc = rx_cb(conn_handle, &hdr, om);
    }

    return rc;
}

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

static void
ble_l2cap_sig_extract_expired(struct ble_l2cap_sig_proc_list *dst_list)
{
    struct ble_l2cap_sig_proc *proc;
    struct ble_l2cap_sig_proc *prev;
    struct ble_l2cap_sig_proc *next;
    uint32_t now;
    int32_t time_diff;

    now = os_time_get();
    STAILQ_INIT(dst_list);

    ble_hs_lock();

    prev = NULL;
    proc = STAILQ_FIRST(&ble_l2cap_sig_procs);
    while (proc != NULL) {
        next = STAILQ_NEXT(proc, next);
    
        time_diff = now - proc->exp_os_ticks;
        if (time_diff >= 0) {
            if (prev == NULL) {
                STAILQ_REMOVE_HEAD(&ble_l2cap_sig_procs, next);
            } else {
                STAILQ_REMOVE_AFTER(&ble_l2cap_sig_procs, prev, next);
            }
            STAILQ_INSERT_TAIL(dst_list, proc, next);
        }

        proc = next;
    }

    ble_hs_unlock();
}

/**
 * Applies periodic checks and actions to all active procedures.
 *
 * All procedures that have been expecting a response for longer than 30
 * seconds are aborted and their corresponding connection is terminated.
 *
 * Called by the heartbeat timer; executed at least once a second.
 *
 * @return                      The number of ticks until this function should
 *                                  be called again; currently always
 *                                  UINT32_MAX.
 */
int32_t
ble_l2cap_sig_heartbeat(void)
{
    struct ble_l2cap_sig_proc_list temp_list;
    struct ble_l2cap_sig_proc *proc;

    /* Remove timed-out procedures from the main list and insert them into a
     * temporary list.
     */
    ble_l2cap_sig_extract_expired(&temp_list);

    /* Terminate the connection associated with each timed-out procedure. */
    STAILQ_FOREACH(proc, &temp_list, next) {
        STATS_INC(ble_l2cap_stats, proc_timeout);
        ble_gap_terminate(proc->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    return BLE_HS_FOREVER;
}

int
ble_l2cap_sig_init(void)
{
    int rc;

    free(ble_l2cap_sig_proc_mem);

    STAILQ_INIT(&ble_l2cap_sig_procs);

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

    return 0;

err:
    free(ble_l2cap_sig_proc_mem);
    return rc;
}
