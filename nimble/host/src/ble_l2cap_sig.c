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
#include "ble_l2cap_priv.h"
#include "ble_l2cap_sig.h"

/*****************************************************************************
 * $definitions / declarations                                               *
 *****************************************************************************/

#define BLE_L2CAP_SIG_NUM_PROCS                 16      /* XXX Configurable. */
#define BLE_L2CAP_SIG_HEARTBEAT_PERIOD          1000    /* Milliseconds. */
#define BLE_L2CAP_SIG_UNRESPONSIVE_TIMEOUT      30000   /* Milliseconds. */

#define BLE_L2CAP_SIG_PROC_OP_UPDATE            0
#define BLE_L2CAP_SIG_PROC_OP_MAX               1

struct ble_l2cap_sig_proc {
    STAILQ_ENTRY(ble_l2cap_sig_proc) next;

    uint8_t op;
    uint8_t id;
    uint8_t flags;
    uint16_t conn_handle;
    uint32_t tx_time; /* OS ticks. */
    union {
        struct {
            struct ble_l2cap_sig_update_params params;
            ble_l2cap_sig_update_fn *cb;
            void *cb_arg;
        } update;
    };
};

/** Procedure has a tx pending. */
#define BLE_L2CAP_SIG_PROC_F_PENDING    0x01

/** Procedure currently expects an ATT response. */
#define BLE_L2CAP_SIG_PROC_F_EXPECTING  0x02

/** Procedure failed to tx due to too many outstanding txes. */
#define BLE_L2CAP_SIG_PROC_F_CONGESTED  0x04

/** Procedure failed to tx due to memory exhaustion. */
#define BLE_L2CAP_SIG_PROC_F_NO_MEM     0x08

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

STAILQ_HEAD(ble_l2cap_sig_proc_list, ble_l2cap_sig_proc);
static struct ble_l2cap_sig_proc_list ble_l2cap_sig_list;

/*****************************************************************************
 * $debug                                                                    *
 *****************************************************************************/

/**
 * Ensures all procedure entries are in a valid state.
 *
 * Lock restrictions: None.
 */
static void
ble_l2cap_sig_assert_sanity(void)
{
#ifdef BLE_HS_DEBUG
    struct ble_l2cap_sig_proc *proc;
    unsigned mask;
    int num_set;

    STAILQ_FOREACH(proc, &ble_l2cap_sig_list, next) {
        /* Ensure exactly one flag is set. */
        num_set = 0;
        mask = 0x01;
        while (mask <= UINT8_MAX) {
            if (proc->flags & mask) {
                num_set++;
            }
            mask <<= 1;
        }

        assert(num_set == 1);
    }
#endif
}

static struct os_mutex ble_l2cap_sig_mutex;

/*****************************************************************************
 * $mutex                                                                    *
 *****************************************************************************/

static void
ble_l2cap_sig_lock(void)
{
    struct os_task *owner;
    int rc;

    owner = ble_l2cap_sig_mutex.mu_owner;
    if (owner != NULL) {
        assert(owner != os_sched_get_current_task());
    }

    rc = os_mutex_pend(&ble_l2cap_sig_mutex, 0xffffffff);
    assert(rc == 0 || rc == OS_NOT_STARTED);
}

static void
ble_l2cap_sig_unlock(void)
{
    int rc;

    rc = os_mutex_release(&ble_l2cap_sig_mutex);
    assert(rc == 0 || rc == OS_NOT_STARTED);
}

int
ble_l2cap_sig_locked_by_cur_task(void)
{
    struct os_task *owner;

    owner = ble_l2cap_sig_mutex.mu_owner;
    return owner != NULL && owner == os_sched_get_current_task();
}

/*****************************************************************************
 * $misc                                                                     *
 *****************************************************************************/

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

/**
 * Determines if the specified proc entry's "pending" flag can be set.
 *
 * Lock restrictions: None.
 */
static int
ble_l2cap_sig_proc_can_pend(struct ble_l2cap_sig_proc *proc)
{
    return !(proc->flags & (BLE_L2CAP_SIG_PROC_F_CONGESTED |
                            BLE_L2CAP_SIG_PROC_F_NO_MEM |
                            BLE_L2CAP_SIG_PROC_F_EXPECTING));
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
ble_l2cap_sig_proc_free(struct ble_l2cap_sig_proc *proc)
{
    int rc;

    if (proc != NULL) {
        rc = os_memblock_put(&ble_l2cap_sig_proc_pool, proc);
        assert(rc == 0);
    }
}

/**
 * Removes the specified proc entry from a list without freeing it.
 *
 * Lock restrictions:
 *     o Caller locks l2cap_sig if the source list is ble_l2cap_sig_list.
 *
 * @param src_list              The list to remove the proc from.
 * @param proc                  The proc to remove.
 * @param prev                  The proc that is previous to "proc" in the
 *                                  list; null if "proc" is the list head.
 */
static void
ble_l2cap_sig_proc_remove(struct ble_l2cap_sig_proc_list *src_list,
                          struct ble_l2cap_sig_proc *proc,
                          struct ble_l2cap_sig_proc *prev)
{
    if (prev == NULL) {
        assert(STAILQ_FIRST(src_list) == proc);
        STAILQ_REMOVE_HEAD(src_list, next);
    } else {
        assert(STAILQ_NEXT(prev, next) == proc);
        STAILQ_NEXT(prev, next) = STAILQ_NEXT(proc, next);
    }
}

/**
 * Concatenates the specified list onto the end of the main proc list.
 *
 * Lock restrictions:
 *     o Caller unlocks l2cap_sig.
 */
static void
ble_l2cap_sig_proc_concat(struct ble_l2cap_sig_proc_list *tail_list)
{
    struct ble_l2cap_sig_proc *proc;

    /* Concatenate the tempororary list to the end of the main list. */
    if (!STAILQ_EMPTY(tail_list)) {
        ble_l2cap_sig_lock();

        if (STAILQ_EMPTY(&ble_l2cap_sig_list)) {
            ble_l2cap_sig_list = *tail_list;
        } else {
            proc = STAILQ_LAST(&ble_l2cap_sig_list, ble_l2cap_sig_proc, next);
            STAILQ_NEXT(proc, next) = STAILQ_FIRST(tail_list);
            ble_l2cap_sig_list.stqh_last = tail_list->stqh_last;
        }

        ble_l2cap_sig_unlock();
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
    (*out_proc)->op = op;
    (*out_proc)->conn_handle = conn_handle;
    (*out_proc)->tx_time = os_time_get();

    STAILQ_INSERT_TAIL(&ble_l2cap_sig_list, *out_proc, next);

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
    if (conn_handle != proc->conn_handle) {
        return 0;
    }

    if (op != proc->op) {
        return 0;
    }

    if (id != proc->id) {
        return 0;
    }

    if (expecting_only &&
        !(proc->flags & BLE_L2CAP_SIG_PROC_F_EXPECTING)) {

        return 0;
    }

    return 1;
}

/**
 * Searches the global proc list for an entry that fits the specified criteria.
 *
 * Lock restrictions: None.
 *
 * @param conn_handle           The connection handle to match against.
 * @param op                    The op code to match against.
 * @param id                    The identifier to match against.
 * @param expecting_only        1=Only match entries expecting a response;
 *                                  0=Ignore this criterion.
 * @param out_prev              On success, the address of the result's
 *                                  previous entry gets written here.  Pass
 *                                  null if you don't need this information.
 *
 * @return                      1 if the proc matches; 0 otherwise.
 */
static struct ble_l2cap_sig_proc *
ble_l2cap_sig_proc_find(uint16_t conn_handle, uint8_t op, uint8_t id,
                        int expecting_only,
                        struct ble_l2cap_sig_proc **out_prev)
{
    struct ble_l2cap_sig_proc *proc;
    struct ble_l2cap_sig_proc *prev;

    prev = NULL;
    STAILQ_FOREACH(proc, &ble_l2cap_sig_list, next) {
        if (ble_l2cap_sig_proc_matches(proc, conn_handle, op, id,
                                       expecting_only)) {
            if (out_prev != NULL) {
                *out_prev = prev;
            }
            return proc;
        }

        prev = proc;
    }

    return NULL;
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
    struct ble_l2cap_sig_proc *proc;
    struct ble_l2cap_sig_proc *prev;

    ble_l2cap_sig_lock();

    ble_l2cap_sig_assert_sanity();

    proc = ble_l2cap_sig_proc_find(conn_handle, op, identifier, 1, &prev);
    if (proc != NULL) {
        ble_l2cap_sig_proc_remove(&ble_l2cap_sig_list, proc, prev);
    }

    ble_l2cap_sig_unlock();

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
    assert(!(proc->flags & BLE_L2CAP_SIG_PROC_F_PENDING));

    proc->flags &= ~BLE_L2CAP_SIG_PROC_F_EXPECTING;
    proc->flags |= BLE_L2CAP_SIG_PROC_F_PENDING;
    ble_hs_kick_l2cap_sig();
}

/**
 * Sets the specified proc entry's "expecting" flag (i.e., indicates that the
 * L2CAP sig procedure is stalled until it receives a response).
 *
 * Lock restrictions: None.
 */
static void
ble_l2cap_sig_proc_set_expecting(struct ble_l2cap_sig_proc *proc,
                                 struct ble_l2cap_sig_proc *prev)
{
    assert(!(proc->flags & BLE_L2CAP_SIG_PROC_F_EXPECTING));

    proc->flags &= ~BLE_L2CAP_SIG_PROC_F_PENDING;
    proc->flags |= BLE_L2CAP_SIG_PROC_F_EXPECTING;
    proc->tx_time = os_time_get();
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

    *om = os_mbuf_pullup(*om, BLE_L2CAP_SIG_UPDATE_REQ_SZ);
    if (*om == NULL) {
        return BLE_HS_ENOMEM;
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
        } else {
            params.itvl_min = req.itvl_min;
            params.itvl_max = req.itvl_max;
            params.latency = req.slave_latency;
            params.supervision_timeout = req.timeout_multiplier;
            params.min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN;
            params.max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN;
        }
    }
    ble_hs_conn_unlock();

    if (rc != 0) {
        return rc;
    }

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

    if (OS_MBUF_PKTLEN(*om) < BLE_L2CAP_SIG_UPDATE_RSP_SZ) {
        cb_status = BLE_HS_EBADDATA;
        rc = BLE_HS_EBADDATA;
        goto done;
    }

    *om = os_mbuf_pullup(*om, BLE_L2CAP_SIG_UPDATE_RSP_SZ);
    if (*om == NULL) {
        cb_status = BLE_HS_ENOMEM;
        rc = BLE_HS_ENOMEM;
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
    ble_l2cap_sig_proc_free(proc);
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

    conn = ble_hs_conn_find(proc->conn_handle);
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

    console_printf("L2CAP - rxed signalling msg: ");
    ble_hs_misc_log_mbuf(*om);
    console_printf("\n");

    *om = os_mbuf_pullup(*om, BLE_L2CAP_SIG_HDR_SZ);
    if (*om == NULL) {
        return BLE_HS_EBADDATA;
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
    struct ble_l2cap_sig_proc *proc;
    uint32_t now;
    int rc;

    now = os_time_get();

    STAILQ_FOREACH(proc, &ble_l2cap_sig_list, next) {
        if (proc->flags & BLE_L2CAP_SIG_PROC_F_NO_MEM) {
            proc->flags &= ~BLE_L2CAP_SIG_PROC_F_NO_MEM;
            if (ble_l2cap_sig_proc_can_pend(proc)) {
                ble_l2cap_sig_proc_set_pending(proc);
            }
        } else if (proc->flags & BLE_L2CAP_SIG_PROC_F_EXPECTING) {
            if (now - proc->tx_time >= BLE_L2CAP_SIG_UNRESPONSIVE_TIMEOUT) {
                rc = ble_gap_terminate(proc->conn_handle);
                assert(rc == 0); /* XXX */
            }
        }
    }

    rc = os_callout_reset(
        &ble_l2cap_sig_heartbeat_timer.cf_c,
        BLE_L2CAP_SIG_HEARTBEAT_PERIOD * OS_TICKS_PER_SEC / 1000);
    assert(rc == 0);
}

/**
 * Lock restrictions:
 *     o Caller unlocks ble_hs_conn.
 */
void
ble_l2cap_sig_wakeup(void)
{
    struct ble_l2cap_sig_proc_list proc_list;
    struct ble_l2cap_sig_proc *proc;
    struct ble_l2cap_sig_proc *prev;
    struct ble_l2cap_sig_proc *next;
    ble_l2cap_sig_kick_fn *kick_cb;
    int rc;

    ble_hs_misc_assert_no_locks();

    STAILQ_INIT(&proc_list);

    /* Remove all procs with pending transmits and insert them into the
     * temporary list. Once the elements are moved, they can be processed
     * without keeping the l2cap_sig mutex locked.
     */
    ble_l2cap_sig_lock();

    ble_l2cap_sig_assert_sanity();

    prev = NULL;
    proc = STAILQ_FIRST(&ble_l2cap_sig_list);
    while (proc != NULL) {
        next = STAILQ_NEXT(proc, next);

        if (proc->flags & BLE_L2CAP_SIG_PROC_F_PENDING) {
            ble_l2cap_sig_proc_remove(&ble_l2cap_sig_list, proc, prev);
            STAILQ_INSERT_TAIL(&proc_list, proc, next);
        } else {
            prev = proc;
        }

        proc = next;
    }

    ble_l2cap_sig_unlock();

    /* Process each of the pending procs. */
    prev = NULL;
    proc = STAILQ_FIRST(&proc_list);
    while (proc != NULL) {
        next = STAILQ_NEXT(proc, next);

        kick_cb = ble_l2cap_sig_kick_get(proc->op);
        rc = kick_cb(proc);
        switch (rc) {
        case 0:
            /* Transmit succeeded.  Response expected. */
            ble_l2cap_sig_proc_set_expecting(proc, prev);

            /* Current proc remains; reseat prev. */
            prev = proc;
            break;

        case BLE_HS_EAGAIN:
            /* Transmit failed due to resource shortage.  Reschedule. */
            proc->flags &= ~BLE_L2CAP_SIG_PROC_F_PENDING;

            /* Current proc remains; reseat prev. */
            prev = proc;
            break;

        case BLE_HS_EDONE:
            /* Procedure complete. */
            ble_l2cap_sig_proc_remove(&proc_list, proc, prev);
            ble_l2cap_sig_proc_free(proc);

            /* Current proc removed; old prev still valid. */
            break;

        default:
            assert(0);
            break;
        }

        proc = next;
    }

    /* Concatenate the tempororary list to the end of the main list. */
    ble_l2cap_sig_proc_concat(&proc_list);
}

/**
 * Lock restrictions: None.
 */
int
ble_l2cap_sig_init(void)
{
    int rc;

    free(ble_l2cap_sig_proc_mem);

    rc = os_mutex_init(&ble_l2cap_sig_mutex);
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    ble_l2cap_sig_proc_mem = malloc(
        OS_MEMPOOL_BYTES(BLE_L2CAP_SIG_NUM_PROCS,
                         sizeof (struct ble_l2cap_sig_proc)));
    if (ble_l2cap_sig_proc_mem == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = os_mempool_init(&ble_l2cap_sig_proc_pool,
                         BLE_L2CAP_SIG_NUM_PROCS,
                         sizeof (struct ble_l2cap_sig_proc),
                         ble_l2cap_sig_proc_mem,
                         "ble_l2cap_sig_proc_pool");
    if (rc != 0) {
        goto err;
    }

    STAILQ_INIT(&ble_l2cap_sig_list);

    os_callout_func_init(&ble_l2cap_sig_heartbeat_timer, &ble_hs_evq,
                         ble_l2cap_sig_heartbeat, NULL);

    return 0;

err:
    free(ble_l2cap_sig_proc_mem);
    return rc;
}
