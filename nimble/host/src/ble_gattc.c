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

#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "os/os_mempool.h"
#include "nimble/ble.h"
#include "ble_hs_priv.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "ble_hs_conn.h"
#include "ble_att_cmd.h"
#include "ble_att_priv.h"
#include "ble_fsm_priv.h"
#include "ble_gatt_priv.h"

/*****************************************************************************
 * $definitions / declarations                                               *
 *****************************************************************************/

#define BLE_GATT_HEARTBEAT_PERIOD               1000    /* Milliseconds. */
#define BLE_GATT_UNRESPONSIVE_TIMEOUT           30000   /* Milliseconds. */

#define BLE_GATT_OP_NONE                        UINT8_MAX
#define BLE_GATT_OP_MTU                         0
#define BLE_GATT_OP_DISC_ALL_SVCS               1
#define BLE_GATT_OP_DISC_SVC_UUID               2
#define BLE_GATT_OP_FIND_INC_SVCS               3
#define BLE_GATT_OP_DISC_ALL_CHRS               4
#define BLE_GATT_OP_DISC_CHRS_UUID              5
#define BLE_GATT_OP_DISC_ALL_DSCS               6
#define BLE_GATT_OP_READ                        7
#define BLE_GATT_OP_READ_UUID                   8
#define BLE_GATT_OP_READ_LONG                   9
#define BLE_GATT_OP_READ_MULT                   10
#define BLE_GATT_OP_WRITE_NO_RSP                11
#define BLE_GATT_OP_WRITE                       12
#define BLE_GATT_OP_WRITE_LONG                  13
#define BLE_GATT_OP_WRITE_RELIABLE              14
#define BLE_GATT_OP_NOTIFY                      15
#define BLE_GATT_OP_INDICATE                    16
#define BLE_GATT_OP_MAX                         17

/** Represents an in-progress GATT procedure. */
struct ble_gattc_proc {
    struct ble_fsm_proc fsm_proc;

    union {
        struct {
            ble_gatt_mtu_fn *cb;
            void *cb_arg;
        } mtu;

        struct {
            uint16_t prev_handle;
            ble_gatt_disc_svc_fn *cb;
            void *cb_arg;
        } disc_all_svcs;

        struct {
            uint8_t service_uuid[16];
            uint16_t prev_handle;
            ble_gatt_disc_svc_fn *cb;
            void *cb_arg;
        } disc_svc_uuid;

        struct {
            uint16_t prev_handle;
            uint16_t end_handle;

            uint16_t cur_start;
            uint16_t cur_end;

            ble_gatt_disc_svc_fn *cb;
            void *cb_arg;
        } find_inc_svcs;

        struct {
            uint16_t prev_handle;
            uint16_t end_handle;
            ble_gatt_chr_fn *cb;
            void *cb_arg;
        } disc_all_chrs;

        struct {
            uint8_t chr_uuid[16];
            uint16_t prev_handle;
            uint16_t end_handle;
            ble_gatt_chr_fn *cb;
            void *cb_arg;
        } disc_chr_uuid;

        struct {
            uint16_t chr_def_handle;
            uint16_t prev_handle;
            uint16_t end_handle;
            ble_gatt_dsc_fn *cb;
            void *cb_arg;
        } disc_all_dscs;

        struct {
            uint16_t handle;
            ble_gatt_attr_fn *cb;
            void *cb_arg;
        } read;

        struct {
            uint16_t handle;
            uint16_t offset;
            ble_gatt_attr_fn *cb;
            void *cb_arg;
        } read_long;

        struct {
            uint16_t *handles;
            uint8_t num_handles;
            ble_gatt_mult_attr_fn *cb;
            void *cb_arg;
        } read_mult;

        struct {
            uint16_t prev_handle;
            uint16_t end_handle;
            uint8_t uuid128[16];
            ble_gatt_attr_fn *cb;
            void *cb_arg;
        } read_uuid;

        struct {
            struct ble_gatt_attr attr;
            ble_gatt_attr_fn *cb;
            void *cb_arg;
        } write;

        struct {
            struct ble_gatt_attr attr;
            uint16_t length;
            ble_gatt_attr_fn *cb;
            void *cb_arg;
        } write_long;

        struct {
            struct ble_gatt_attr *attrs;
            int num_attrs;
            int cur_attr;
            ble_gatt_reliable_attr_fn *cb;
            void *cb_arg;
        } write_reliable;

        struct {
            struct ble_gatt_attr attr;
            ble_gatt_attr_fn *cb;
            void *cb_arg;
        } notify;

        struct {
            struct ble_gatt_attr attr;
            ble_gatt_attr_fn *cb;
            void *cb_arg;
        } indicate;
    };
};

/**
 * Handles unresponsive timeouts and periodic retries in case of resource
 * shortage.
 */
static struct os_callout_func ble_gattc_heartbeat_timer;

/**
 * Kick functions - these trigger the pending ATT transmit for an active GATT
 * procedure.
 */
typedef int ble_gattc_kick_fn(struct ble_gattc_proc *proc);
typedef void ble_gattc_err_fn(struct ble_gattc_proc *proc, int status,
                              uint16_t att_handle);

static int ble_gattc_mtu_kick(struct ble_gattc_proc *proc);
static int ble_gattc_disc_all_svcs_kick(struct ble_gattc_proc *proc);
static int ble_gattc_disc_svc_uuid_kick(struct ble_gattc_proc *proc);
static int ble_gattc_find_inc_svcs_kick(struct ble_gattc_proc *proc);
static int ble_gattc_disc_all_chrs_kick(struct ble_gattc_proc *proc);
static int ble_gattc_disc_chr_uuid_kick(struct ble_gattc_proc *proc);
static int ble_gattc_disc_all_dscs_kick(struct ble_gattc_proc *proc);
static int ble_gattc_read_kick(struct ble_gattc_proc *proc);
static int ble_gattc_read_uuid_kick(struct ble_gattc_proc *proc);
static int ble_gattc_read_long_kick(struct ble_gattc_proc *proc);
static int ble_gattc_read_mult_kick(struct ble_gattc_proc *proc);
static int ble_gattc_write_no_rsp_kick(struct ble_gattc_proc *proc);
static int ble_gattc_write_kick(struct ble_gattc_proc *proc);
static int ble_gattc_write_long_kick(struct ble_gattc_proc *proc);
static int ble_gattc_write_reliable_kick(struct ble_gattc_proc *proc);
static int ble_gattc_notify_kick(struct ble_gattc_proc *proc);
static int ble_gattc_indicate_kick(struct ble_gattc_proc *proc);

/**
 * Error functions - these handle an incoming ATT error response and apply it
 * to the appropriate active GATT procedure.
 */
static void ble_gattc_mtu_err(struct ble_gattc_proc *proc, int status,
                              uint16_t att_handle);
static void ble_gattc_disc_all_svcs_err(struct ble_gattc_proc *proc,
                                        int status, uint16_t att_handle);
static void ble_gattc_disc_svc_uuid_err(struct ble_gattc_proc *proc,
                                        int status, uint16_t att_handle);
static void ble_gattc_find_inc_svcs_err(struct ble_gattc_proc *proc,
                                        int status, uint16_t att_handle);
static void ble_gattc_disc_all_chrs_err(struct ble_gattc_proc *proc,
                                        int status, uint16_t att_handle);
static void ble_gattc_disc_chr_uuid_err(struct ble_gattc_proc *proc,
                                        int status, uint16_t att_handle);
static void ble_gattc_disc_all_dscs_err(struct ble_gattc_proc *proc,
                                        int status, uint16_t att_handle);
static void ble_gattc_read_err(struct ble_gattc_proc *proc, int status,
                               uint16_t att_handle);
static void ble_gattc_read_uuid_err(struct ble_gattc_proc *proc, int status,
                                    uint16_t att_handle);
static void ble_gattc_read_long_err(struct ble_gattc_proc *proc, int status,
                                    uint16_t att_handle);
static void ble_gattc_read_mult_err(struct ble_gattc_proc *proc, int status,
                                    uint16_t att_handle);
static void ble_gattc_write_err(struct ble_gattc_proc *proc, int status,
                                uint16_t att_handle);
static void ble_gattc_write_long_err(struct ble_gattc_proc *proc, int status,
                                     uint16_t att_handle);
static void ble_gattc_write_reliable_err(struct ble_gattc_proc *proc,
                                         int status, uint16_t att_handle);
static void ble_gattc_indicate_err(struct ble_gattc_proc *proc, int status,
                                   uint16_t att_handle);

/**
 * Receive functions - these handle specific incoming responses and apply them
 * to the appropriate active GATT procedure.
 */
static int
ble_gattc_find_inc_svcs_rx_adata(struct ble_gattc_proc *proc,
                                 struct ble_att_read_type_adata *adata);
static int
ble_gattc_find_inc_svcs_rx_complete(struct ble_gattc_proc *proc,
                                    int status);
static int
ble_gattc_find_inc_svcs_rx_read_rsp(struct ble_gattc_proc *proc,
                                    int status, void *value, int value_len);
static int
ble_gattc_disc_all_chrs_rx_adata(struct ble_gattc_proc *proc,
                                 struct ble_att_read_type_adata *adata);
static int
ble_gattc_disc_all_chrs_rx_complete(struct ble_gattc_proc *proc,
                                    int status);
static int
ble_gattc_disc_chr_uuid_rx_adata(struct ble_gattc_proc *proc,
                                 struct ble_att_read_type_adata *adata);
static int
ble_gattc_disc_chr_uuid_rx_complete(struct ble_gattc_proc *proc,
                                    int status);
static int
ble_gattc_read_rx_read_rsp(struct ble_gattc_proc *proc, int status,
                           void *value, int value_len);
static int
ble_gattc_read_long_rx_read_rsp(struct ble_gattc_proc *proc, int status,
                                void *value, int value_len);
static int
ble_gattc_read_uuid_rx_adata(struct ble_gattc_proc *proc,
                             struct ble_att_read_type_adata *adata);
static int
ble_gattc_read_uuid_rx_complete(struct ble_gattc_proc *proc, int status);
static int
ble_gattc_write_long_rx_prep(struct ble_gattc_proc *proc,
                             int status, struct ble_att_prep_write_cmd *rsp,
                             void *attr_data, uint16_t attr_len);
static int
ble_gattc_write_long_rx_exec(struct ble_gattc_proc *proc, int status);

static int
ble_gattc_write_reliable_rx_prep(struct ble_gattc_proc *proc, int status,
                                 struct ble_att_prep_write_cmd *rsp,
                                 void *attr_data, uint16_t attr_len);
static int
ble_gattc_write_reliable_rx_exec(struct ble_gattc_proc *proc, int status);


typedef int ble_gattc_rx_adata_fn(struct ble_gattc_proc *proc,
                                  struct ble_att_read_type_adata *adata);
struct ble_gattc_rx_adata_entry {
    uint8_t op;
    ble_gattc_rx_adata_fn *cb;
};

typedef int ble_gattc_rx_complete_fn(struct ble_gattc_proc *proc, int status);
struct ble_gattc_rx_complete_entry {
    uint8_t op;
    ble_gattc_rx_complete_fn *cb;
};

typedef int ble_gattc_rx_attr_fn(struct ble_gattc_proc *proc, int status,
                                 void *value, int value_len);
struct ble_gattc_rx_attr_entry {
    uint8_t op;
    ble_gattc_rx_attr_fn *cb;
};

typedef int ble_gattc_rx_prep_fn(struct ble_gattc_proc *proc, int status,
                                 struct ble_att_prep_write_cmd *rsp,
                                 void *attr_data, uint16_t attr_len);

struct ble_gattc_rx_prep_entry {
    uint8_t op;
    ble_gattc_rx_prep_fn *cb;
};

typedef int ble_gattc_rx_exec_fn(struct ble_gattc_proc *proc, int status);

struct ble_gattc_rx_exec_entry {
    uint8_t op;
    ble_gattc_rx_exec_fn *cb;
};

static const struct ble_gattc_rx_adata_entry
    ble_gattc_rx_read_type_elem_entries[] = {

    { BLE_GATT_OP_FIND_INC_SVCS,    ble_gattc_find_inc_svcs_rx_adata },
    { BLE_GATT_OP_DISC_ALL_CHRS,    ble_gattc_disc_all_chrs_rx_adata },
    { BLE_GATT_OP_DISC_CHRS_UUID,   ble_gattc_disc_chr_uuid_rx_adata },
    { BLE_GATT_OP_READ_UUID,        ble_gattc_read_uuid_rx_adata },
};

static const struct ble_gattc_rx_complete_entry
    ble_gattc_rx_read_type_complete_entries[] = {

    { BLE_GATT_OP_FIND_INC_SVCS,    ble_gattc_find_inc_svcs_rx_complete },
    { BLE_GATT_OP_DISC_ALL_CHRS,    ble_gattc_disc_all_chrs_rx_complete },
    { BLE_GATT_OP_DISC_CHRS_UUID,   ble_gattc_disc_chr_uuid_rx_complete },
    { BLE_GATT_OP_READ_UUID,        ble_gattc_read_uuid_rx_complete },
};

static const struct ble_gattc_rx_attr_entry ble_gattc_rx_read_rsp_entries[] = {
    { BLE_GATT_OP_READ,             ble_gattc_read_rx_read_rsp },
    { BLE_GATT_OP_READ_LONG,        ble_gattc_read_long_rx_read_rsp },
    { BLE_GATT_OP_FIND_INC_SVCS,    ble_gattc_find_inc_svcs_rx_read_rsp },
};

static const struct ble_gattc_rx_prep_entry ble_gattc_rx_prep_entries[] = {
    { BLE_GATT_OP_WRITE_LONG,       ble_gattc_write_long_rx_prep },
    { BLE_GATT_OP_WRITE_RELIABLE,   ble_gattc_write_reliable_rx_prep },
};

static const struct ble_gattc_rx_exec_entry ble_gattc_rx_exec_entries[] = {
    { BLE_GATT_OP_WRITE_LONG,       ble_gattc_write_long_rx_exec },
    { BLE_GATT_OP_WRITE_RELIABLE,   ble_gattc_write_reliable_rx_exec },
};

/**
 * Dispatch entries - this array maps GATT procedure types to their
 * corresponding kick and error functions.
 */
static const struct ble_gattc_dispatch_entry {
    ble_gattc_kick_fn *kick_cb;
    ble_gattc_err_fn *err_cb;
} ble_gattc_dispatch[BLE_GATT_OP_MAX] = {
    [BLE_GATT_OP_MTU] = {
        .kick_cb = ble_gattc_mtu_kick,
        .err_cb = ble_gattc_mtu_err,
    },
    [BLE_GATT_OP_DISC_ALL_SVCS] = {
        .kick_cb = ble_gattc_disc_all_svcs_kick,
        .err_cb = ble_gattc_disc_all_svcs_err,
    },
    [BLE_GATT_OP_DISC_SVC_UUID] = {
        .kick_cb = ble_gattc_disc_svc_uuid_kick,
        .err_cb = ble_gattc_disc_svc_uuid_err,
    },
    [BLE_GATT_OP_FIND_INC_SVCS] = {
        .kick_cb = ble_gattc_find_inc_svcs_kick,
        .err_cb = ble_gattc_find_inc_svcs_err,
    },
    [BLE_GATT_OP_DISC_ALL_CHRS] = {
        .kick_cb = ble_gattc_disc_all_chrs_kick,
        .err_cb = ble_gattc_disc_all_chrs_err,
    },
    [BLE_GATT_OP_DISC_CHRS_UUID] = {
        .kick_cb = ble_gattc_disc_chr_uuid_kick,
        .err_cb = ble_gattc_disc_chr_uuid_err,
    },
    [BLE_GATT_OP_DISC_ALL_DSCS] = {
        .kick_cb = ble_gattc_disc_all_dscs_kick,
        .err_cb = ble_gattc_disc_all_dscs_err,
    },
    [BLE_GATT_OP_READ] = {
        .kick_cb = ble_gattc_read_kick,
        .err_cb = ble_gattc_read_err,
    },
    [BLE_GATT_OP_READ_UUID] = {
        .kick_cb = ble_gattc_read_uuid_kick,
        .err_cb = ble_gattc_read_uuid_err,
    },
    [BLE_GATT_OP_READ_LONG] = {
        .kick_cb = ble_gattc_read_long_kick,
        .err_cb = ble_gattc_read_long_err,
    },
    [BLE_GATT_OP_READ_MULT] = {
        .kick_cb = ble_gattc_read_mult_kick,
        .err_cb = ble_gattc_read_mult_err,
    },
    [BLE_GATT_OP_WRITE_NO_RSP] = {
        .kick_cb = ble_gattc_write_no_rsp_kick,
        .err_cb = NULL,
    },
    [BLE_GATT_OP_WRITE] = {
        .kick_cb = ble_gattc_write_kick,
        .err_cb = ble_gattc_write_err,
    },
    [BLE_GATT_OP_WRITE_LONG] = {
        .kick_cb = ble_gattc_write_long_kick,
        .err_cb = ble_gattc_write_long_err,
    },
    [BLE_GATT_OP_WRITE_RELIABLE] = {
        .kick_cb = ble_gattc_write_reliable_kick,
        .err_cb = ble_gattc_write_reliable_err,
    },
    [BLE_GATT_OP_NOTIFY] = {
        .kick_cb = ble_gattc_notify_kick,
        .err_cb = NULL,
    },
    [BLE_GATT_OP_INDICATE] = {
        .kick_cb = ble_gattc_indicate_kick,
        .err_cb = ble_gattc_indicate_err,
    },
};

static void *ble_gattc_proc_mem;
static struct os_mempool ble_gattc_proc_pool;

static struct ble_fsm ble_gattc_fsm;

/*****************************************************************************
 * $mutex                                                                    *
 *****************************************************************************/

void
ble_gattc_lock(void)
{
    ble_fsm_lock(&ble_gattc_fsm);
}

void
ble_gattc_unlock(void)
{
    ble_fsm_unlock(&ble_gattc_fsm);
}

int
ble_gattc_locked_by_cur_task(void)
{
    return ble_fsm_locked_by_cur_task(&ble_gattc_fsm);
}

/*****************************************************************************
 * $rx entry                                                                 *
 *****************************************************************************/

/**
 * Lock restrictions: None.
 */
static const void *
ble_gattc_rx_entry_find(uint8_t op, const void *rx_entries, int num_entries)
{
    struct gen_entry {
        uint8_t op;
        void (*cb)(void);
    };

    const struct gen_entry *entries;
    int i;

    entries = rx_entries;
    for (i = 0; i < num_entries; i++) {
        if (entries[i].op == op) {
            return entries + i;
        }
    }

    return NULL;
}

/*****************************************************************************
 * $proc                                                                    *
 *****************************************************************************/

/**
 * Allocates a proc entry.
 *
 * Lock restrictions: None.
 *
 * @return                      An entry on success; null on failure.
 */
static struct ble_gattc_proc *
ble_gattc_proc_alloc(void)
{
    struct ble_gattc_proc *proc;

    proc = os_memblock_get(&ble_gattc_proc_pool);
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
ble_gattc_proc_free(struct ble_fsm_proc *proc)
{
    int rc;

    if (proc != NULL) {
        rc = os_memblock_put(&ble_gattc_proc_pool, proc);
        assert(rc == 0);
    }
}

/**
 * Tests if a proc entry fits the specified criteria.
 *
 * Lock restrictions:
 *     o Caller locks gattc.
 *
 * @param proc                  The procedure to test.
 * @param conn_handle           The connection handle to match against.
 * @param op                    The op code to match against, or
 *                                  BLE_GATT_OP_NONE to ignore this criterion.
 * @param expecting_only        1=Only match entries expecting a response;
 *                                  0=Ignore this criterion.
 *
 * @return                      1 if the proc matches; 0 otherwise.
 */
static int
ble_gattc_proc_matches(struct ble_fsm_proc *proc, uint16_t conn_handle,
                       uint8_t op, int expecting_only)
{
    if (conn_handle != proc->conn_handle) {
        return 0;
    }

    if (op != proc->op && op != BLE_GATT_OP_NONE) {
        return 0;
    }

    if (expecting_only &&
        !(proc->flags & BLE_FSM_PROC_F_EXPECTING)) {

        return 0;
    }

    return 1;
}

/**
 * Sets the specified proc entry's "pending" flag (i.e., indicates that the
 * GATT procedure is stalled until it transmits its next ATT request.
 *
 * Lock restrictions:
 *     o Caller locks gattc.
 */
static void
ble_gattc_proc_set_pending(struct ble_gattc_proc *proc)
{
    ble_fsm_proc_set_pending(&proc->fsm_proc);
    ble_hs_kick_gatt();
}

/**
 * Creates a new proc entry and sets its fields to the specified values.  The
 * entry is automatically inserted into the global proc list, and its "pending"
 * flag is set.
 *
 * Lock restrictions:
 *     o Caller locks gattc.
 *
 * @param conn_handle           The handle of the connection associated with
 *                                  the GATT procedure.
 * @param op                    The op code for the proc entry (one of the
 *                                  BLE_GATT_OP_[...] constants).
 * @param out_proc              On success, the new entry's address gets
 *                                  written here.
 *
 * @return                      0 on success; BLE_HS error code on failure.
 */
static int
ble_gattc_new_proc(uint16_t conn_handle, uint8_t op,
                   struct ble_gattc_proc **out_proc)
{
    struct ble_gattc_proc *proc;

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        return BLE_HS_ENOMEM;
    }

    memset(proc, 0, sizeof *proc);
    proc->fsm_proc.op = op;
    proc->fsm_proc.conn_handle = conn_handle;

    STAILQ_INSERT_TAIL(&ble_gattc_fsm.procs, &proc->fsm_proc, next);

    *out_proc = proc;

    return 0;
}

/*****************************************************************************
 * $util                                                                     *
 *****************************************************************************/

/**
 * Retrieves the dispatch entry with the specified op code.
 *
 * Lock restrictions: None.
 */
static const struct ble_gattc_dispatch_entry *
ble_gattc_dispatch_get(uint8_t op)
{
    assert(op < BLE_GATT_OP_MAX);
    return ble_gattc_dispatch + op;
}

static int
ble_gattc_heartbeat_extract_cb(struct ble_fsm_proc *proc, void *arg)
{
    uint32_t *now;

    now = arg;

    if (proc->flags & BLE_FSM_PROC_F_EXPECTING) {
        if (*now - proc->tx_time >= BLE_GATT_UNRESPONSIVE_TIMEOUT) {
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
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_heartbeat(void *unused)
{
    struct ble_fsm_proc_list temp_list;
    struct ble_fsm_proc *proc;
    uint32_t now;
    int rc;

    ble_hs_misc_assert_no_locks();

    now = os_time_get();

    /* Remove timed-out procedures from the main list and insert them into a
     * temporary list.  For any stalled procedures, set their pending bit so
     * they can be retried.
     */
    ble_fsm_proc_extract_list(&ble_gattc_fsm, &temp_list,
                              ble_gattc_heartbeat_extract_cb, &now);

    /* Terminate the connection associated with each timed-out procedure. */
    STAILQ_FOREACH(proc, &temp_list, next) {
        ble_gap_terminate(proc->conn_handle);
    }

    /* Concatenate the list of timed out procedures back onto the end of the
     * main list.
     */
    ble_fsm_proc_concat(&ble_gattc_fsm, &temp_list);

    rc = os_callout_reset(&ble_gattc_heartbeat_timer.cf_c,
                          BLE_GATT_HEARTBEAT_PERIOD * OS_TICKS_PER_SEC / 1000);
    assert(rc == 0);
}

/**
 * Returns a pointer to a GATT error object with the specified fields.  The
 * returned object is statically allocated, so this function is not reentrant.
 * This function should only ever be called by the ble_hs task.
 *
 * Lock restrictions: None.
 */
struct ble_gatt_error *
ble_gattc_error(int status, uint16_t att_handle)
{
    static struct ble_gatt_error error;

    if (status == 0) {
        return NULL;
    }

    error.status = status;
    error.att_handle = att_handle;
    return &error;
}

/*****************************************************************************
 * $mtu                                                                      *
 *****************************************************************************/

/**
 * Calls an mtu-exchange proc's callback with the specified parameters.  If the
 * proc has no callback, this function is a no-op.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_mtu_cb(struct ble_gattc_proc *proc, int status, uint16_t att_handle,
                 uint16_t mtu)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (proc->mtu.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->mtu.cb(proc->fsm_proc.conn_handle,
                          ble_gattc_error(status, att_handle),
                          mtu, proc->mtu.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified mtu-exchange proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_mtu_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_mtu_cmd req;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
        assert(chan != NULL);

        if (chan->blc_flags & BLE_L2CAP_CHAN_F_TXED_MTU) {
            rc = BLE_HS_EALREADY;
        } else {
            req.bamc_mtu = chan->blc_my_mtu;
            rc = ble_att_clt_tx_mtu(conn, &req);
        }
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_mtu_cb(proc, rc, 0, 0);
        return BLE_HS_EDONE;
    }
}

/**
 * Handles an incoming ATT error response for the specified mtu-exchange proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_mtu_err(struct ble_gattc_proc *proc, int status, uint16_t att_handle)
{
    ble_gattc_mtu_cb(proc, status, att_handle, 0);
}

/**
 * Handles an incoming ATT exchange mtu response for the specified mtu-exchange
 * proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_mtu_rx_rsp(struct ble_gattc_proc *proc,
                     int status, uint16_t chan_mtu)
{
    ble_gattc_mtu_cb(proc, status, 0, chan_mtu);
    return 1;
}

/**
 * Initiates GATT procedure: Exchange MTU.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The argument to pass to the callback function.
 */
int
ble_gattc_exchange_mtu(uint16_t conn_handle, ble_gatt_mtu_fn *cb, void *cb_arg)
{
    struct ble_gattc_proc *proc;
    int rc;

    ble_gattc_lock();

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_MTU, &proc);
    if (rc == 0) {
        proc->mtu.cb = cb;
        proc->mtu.cb_arg = cb_arg;
        ble_gattc_proc_set_pending(proc);
    }

    ble_gattc_unlock();

    return rc;
}

/*****************************************************************************
 * $discover all services                                                    *
 *****************************************************************************/

/**
 * Calls a discover-all-services proc's callback with the specified parameters.
 * If the proc has no callback, this function is a no-op.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_disc_all_svcs_cb(struct ble_gattc_proc *proc,
                           uint16_t status, uint16_t att_handle,
                           struct ble_gatt_service *service)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (proc->disc_all_svcs.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->disc_all_svcs.cb(proc->fsm_proc.conn_handle,
                                    ble_gattc_error(status, att_handle),
                                    service, proc->disc_all_svcs.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified discover-all-services proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_disc_all_svcs_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_read_group_type_req req;
    struct ble_hs_conn *conn;
    uint8_t uuid128[16];
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        rc = ble_uuid_16_to_128(BLE_ATT_UUID_PRIMARY_SERVICE, uuid128);
        assert(rc == 0);

        req.bagq_start_handle = proc->disc_all_svcs.prev_handle + 1;
        req.bagq_end_handle = 0xffff;
        rc = ble_att_clt_tx_read_group_type(conn, &req, uuid128);
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_disc_all_svcs_cb(proc, rc, 0, NULL);
        return BLE_HS_EDONE;
    }
}

/**
 * Handles an incoming ATT error response for the specified
 * discover-all-services proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_disc_all_svcs_err(struct ble_gattc_proc *proc, int status,
                            uint16_t att_handle)
{
    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Discovery is complete. */
        status = 0;
    }

    ble_gattc_disc_all_svcs_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming attribute data entry from a read-group-type response for
 * the specified discover-all-services proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_disc_all_svcs_rx_adata(struct ble_gattc_proc *proc,
                                 struct ble_att_read_group_type_adata *adata)
{
    struct ble_gatt_service service;
    uint16_t uuid16;
    int cbrc;
    int rc;

    switch (adata->value_len) {
    case 2:
        uuid16 = le16toh(adata->value);
        rc = ble_uuid_16_to_128(uuid16, service.uuid128);
        if (rc != 0) {
            goto done;
        }
        break;

    case 16:
        memcpy(service.uuid128, adata->value, 16);
        break;

    default:
        rc = BLE_HS_EMSGSIZE;
        goto done;
    }

    if (adata->end_group_handle <= proc->disc_all_svcs.prev_handle) {
        /* Peer sent services out of order; terminate procedure. */
        rc = BLE_HS_EBADDATA;
        goto done;
    }

    proc->disc_all_svcs.prev_handle = adata->end_group_handle;

    service.start_handle = adata->att_handle;
    service.end_handle = adata->end_group_handle;

    rc = 0;

done:
    cbrc = ble_gattc_disc_all_svcs_cb(proc, rc, 0, &service);
    if (rc == 0) {
        rc = cbrc;
    }
    return rc;
}

/**
 * Handles a notification that an incoming read-group-type response has been
 * fully processed.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_disc_all_svcs_rx_complete(struct ble_gattc_proc *proc, int status)
{
    if (status != 0 || proc->disc_all_svcs.prev_handle == 0xffff) {
        /* Error or all svcs discovered. */
        ble_gattc_disc_all_svcs_cb(proc, status, 0, NULL);
        return 1;
    } else {
        /* Send follow-up request. */
        ble_gattc_proc_set_pending(proc);
        return 0;
    }
}

/**
 * Initiates GATT procedure: Discover All Primary Services.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The argument to pass to the callback function.
 */
int
ble_gattc_disc_all_svcs(uint16_t conn_handle, ble_gatt_disc_svc_fn *cb,
                        void *cb_arg)
{
#if !NIMBLE_OPT_GATT_DISC_ALL_SVCS
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_DISC_ALL_SVCS, &proc);
    if (rc != 0) {
        return rc;
    }
    proc->disc_all_svcs.prev_handle = 0x0000;
    proc->disc_all_svcs.cb = cb;
    proc->disc_all_svcs.cb_arg = cb_arg;
    ble_gattc_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $discover service by uuid                                                 *
 *****************************************************************************/

/**
 * Calls a discover-service-by-uuid proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_disc_svc_uuid_cb(struct ble_gattc_proc *proc, int status,
                           uint16_t att_handle,
                           struct ble_gatt_service *service)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (proc->disc_svc_uuid.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->disc_svc_uuid.cb(proc->fsm_proc.conn_handle,
                                    ble_gattc_error(status, att_handle),
                                    service, proc->disc_svc_uuid.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified discover-service-by-uuid proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_disc_svc_uuid_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_find_type_value_req req;
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        req.bavq_start_handle = proc->disc_svc_uuid.prev_handle + 1;
        req.bavq_end_handle = 0xffff;
        req.bavq_attr_type = BLE_ATT_UUID_PRIMARY_SERVICE;

        rc = ble_att_clt_tx_find_type_value(conn, &req,
                                            proc->disc_svc_uuid.service_uuid,
                                            16);
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_disc_svc_uuid_cb(proc, rc, 0, NULL);
        return BLE_HS_EDONE;
    }
}

/**
 * Handles an incoming ATT error response for the specified
 * discover-service-by-uuid proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_disc_svc_uuid_err(struct ble_gattc_proc *proc, int status,
                            uint16_t att_handle)
{
    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Discovery is complete. */
        status = 0;
    }

    ble_gattc_disc_svc_uuid_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming "handles info" entry from a find-type-value response for
 * the specified discover-service-by-uuid proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_disc_svc_uuid_rx_hinfo(struct ble_gattc_proc *proc,
                                 struct ble_att_find_type_value_hinfo *hinfo)
{
    struct ble_gatt_service service;
    int cbrc;
    int rc;

    if (hinfo->group_end_handle <= proc->disc_svc_uuid.prev_handle) {
        /* Peer sent services out of order; terminate procedure. */
        rc = BLE_HS_EBADDATA;
        goto done;
    }

    proc->disc_svc_uuid.prev_handle = hinfo->group_end_handle;

    service.start_handle = hinfo->attr_handle;
    service.end_handle = hinfo->group_end_handle;
    memcpy(service.uuid128, proc->disc_svc_uuid.service_uuid, 16);

    rc = 0;

done:
    cbrc = ble_gattc_disc_svc_uuid_cb(proc, rc, 0, &service);
    if (rc != 0) {
        rc = cbrc;
    }
    return rc;
}

/**
 * Handles a notification that a find-type-value response has been fully
 * processed for the specified discover-service-by-uuid proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_disc_svc_uuid_rx_complete(struct ble_gattc_proc *proc, int status)
{
    if (status != 0 || proc->disc_svc_uuid.prev_handle == 0xffff) {
        /* Error or all svcs discovered. */
        ble_gattc_disc_svc_uuid_cb(proc, status, 0, NULL);
        return 1;
    } else {
        /* Send follow-up request. */
        ble_gattc_proc_set_pending(proc);
        return 0;
    }
}

/**
 * Initiates GATT procedure: Discover Primary Service by Service UUID.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param service_uuid128       The 128-bit UUID of the service to discover.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The argument to pass to the callback function.
 */
int
ble_gattc_disc_svc_by_uuid(uint16_t conn_handle, void *service_uuid128,
                           ble_gatt_disc_svc_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT_GATT_DISC_SVC_UUID
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_DISC_SVC_UUID, &proc);
    if (rc != 0) {
        return rc;
    }
    memcpy(proc->disc_svc_uuid.service_uuid, service_uuid128, 16);
    proc->disc_svc_uuid.prev_handle = 0x0000;
    proc->disc_svc_uuid.cb = cb;
    proc->disc_svc_uuid.cb_arg = cb_arg;
    ble_gattc_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $find included svcs                                                       *
 *****************************************************************************/

/**
 * Calls a find-included-services proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_find_inc_svcs_cb(struct ble_gattc_proc *proc, int status,
                           uint16_t att_handle,
                           struct ble_gatt_service *service)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (proc->find_inc_svcs.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->find_inc_svcs.cb(proc->fsm_proc.conn_handle,
                                    ble_gattc_error(status, att_handle),
                                    service, proc->find_inc_svcs.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified find-included-services proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_find_inc_svcs_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_read_type_req read_type_req;
    struct ble_att_read_req read_req;
    struct ble_hs_conn *conn;
    uint8_t uuid128[16];
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        if (proc->find_inc_svcs.cur_start == 0) {
            /* Find the next included service. */
            read_type_req.batq_start_handle =
                proc->find_inc_svcs.prev_handle + 1;
            read_type_req.batq_end_handle = proc->find_inc_svcs.end_handle;

            rc = ble_uuid_16_to_128(BLE_ATT_UUID_INCLUDE, uuid128);
            assert(rc == 0);

            rc = ble_att_clt_tx_read_type(conn, &read_type_req, uuid128);
        } else {
            /* Read the UUID of the previously found service. */
            read_req.barq_handle = proc->find_inc_svcs.cur_start;
            rc = ble_att_clt_tx_read(conn, &read_req);
        }
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_find_inc_svcs_cb(proc, rc, 0, NULL);
        return BLE_HS_EDONE;
    }
}

/**
 * Handles an incoming ATT error response for the specified
 * find-included-services proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_find_inc_svcs_err(struct ble_gattc_proc *proc, int status,
                            uint16_t att_handle)
{
    if (proc->find_inc_svcs.cur_start == 0 &&
        status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {

        /* Discovery is complete. */
        status = 0;
    }

    ble_gattc_find_inc_svcs_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming read-response for the specified find-included-services
 * proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_find_inc_svcs_rx_read_rsp(struct ble_gattc_proc *proc, int status,
                                    void *value, int value_len)
{
    struct ble_gatt_service service;
    int cbrc;
    int rc;

    if (proc->find_inc_svcs.cur_start == 0) {
        /* Unexpected read response; terminate procedure. */
        rc = BLE_HS_EBADDATA;
        goto done;
    }

    if (status != 0) {
        rc = status;
        goto done;
    }

    if (value_len != 16) {
        /* Invalid UUID. */
        rc = BLE_HS_EBADDATA;
        goto done;
    }

    service.start_handle = proc->find_inc_svcs.cur_start;
    service.end_handle = proc->find_inc_svcs.cur_end;
    memcpy(service.uuid128, value, 16);

    /* We are done with this service; proceed to the next. */
    proc->find_inc_svcs.cur_start = 0;
    proc->find_inc_svcs.cur_end = 0;
    ble_gattc_proc_set_pending(proc);

    rc = 0;

done:
    cbrc = ble_gattc_find_inc_svcs_cb(proc, rc, 0, &service);
    if (rc == 0) {
        rc = cbrc;
    }
    return rc;
}

/**
 * Handles an incoming "attribute data" entry from a read-by-type response for
 * the specified find-included-services proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_find_inc_svcs_rx_adata(struct ble_gattc_proc *proc,
                                 struct ble_att_read_type_adata *adata)
{
    struct ble_gatt_service service;
    uint16_t uuid16;
    int call_cb;
    int cbrc;
    int rc;

    if (proc->find_inc_svcs.cur_start != 0) {
        /* We only read one 128-bit UUID service at a time.  Ignore the
         * additional services in the response.
         */
        return 0;
    }

    call_cb = 1;

    if (adata->att_handle <= proc->find_inc_svcs.prev_handle) {
        /* Peer sent services out of order; terminate procedure. */
        rc = BLE_HS_EBADDATA;
        goto done;
    }

    proc->find_inc_svcs.prev_handle = adata->att_handle;

    switch (adata->value_len) {
    case BLE_GATTS_INC_SVC_LEN_NO_UUID:
        proc->find_inc_svcs.cur_start = le16toh(adata->value + 0);
        proc->find_inc_svcs.cur_end = le16toh(adata->value + 2);
        call_cb = 0;
        break;

    case BLE_GATTS_INC_SVC_LEN_UUID:
        service.start_handle = le16toh(adata->value + 0);
        service.end_handle = le16toh(adata->value + 2);
        uuid16 = le16toh(adata->value + 4);
        rc = ble_uuid_16_to_128(uuid16, service.uuid128);
        if (rc != 0) {
            rc = BLE_HS_EBADDATA;
            goto done;
        }

        break;

    default:
        return BLE_HS_EBADDATA;
    }

    rc = 0;

done:
    if (call_cb) {
        cbrc = ble_gattc_find_inc_svcs_cb(proc, 0, 0, &service);
        if (rc != 0) {
            rc = cbrc;
        }
    }

    return rc;
}

/**
 * Handles a notification that a read-by-type response has been fully
 * processed for the specified find-included-services proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_find_inc_svcs_rx_complete(struct ble_gattc_proc *proc, int status)
{
    if (status != 0 || proc->find_inc_svcs.prev_handle == 0xffff) {
        /* Error or all svcs discovered. */
        ble_gattc_find_inc_svcs_cb(proc, status, 0, NULL);
        return 1;
    } else {
        /* Send follow-up request. */
        ble_gattc_proc_set_pending(proc);
        return 0;
    }
}

/**
 * Initiates GATT procedure: Find Included Services.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param start_handle          The handle to begin the search at (generally
 *                                  the service definition handle).
 * @param end_handle            The handle to end the search at (generally the
 *                                  last handle in the service).
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The argument to pass to the callback function.
 */
int
ble_gattc_find_inc_svcs(uint16_t conn_handle, uint16_t start_handle,
                        uint16_t end_handle,
                        ble_gatt_disc_svc_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT_GATT_FIND_INC_SVCS
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_FIND_INC_SVCS, &proc);
    if (rc != 0) {
        return rc;
    }
    proc->find_inc_svcs.prev_handle = start_handle - 1;
    proc->find_inc_svcs.end_handle = end_handle;
    proc->find_inc_svcs.cb = cb;
    proc->find_inc_svcs.cb_arg = cb_arg;
    ble_gattc_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $discover all characteristics                                             *
 *****************************************************************************/

/**
 * Calls a discover-all-characteristics proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_disc_all_chrs_cb(struct ble_gattc_proc *proc, int status,
                           uint16_t att_handle, struct ble_gatt_chr *chr)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (proc->disc_all_chrs.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->disc_all_chrs.cb(proc->fsm_proc.conn_handle,
                                    ble_gattc_error(status, att_handle), chr,
                                    proc->disc_all_chrs.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified discover-all-characteristics
 * proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_disc_all_chrs_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_read_type_req req;
    struct ble_hs_conn *conn;
    uint8_t uuid128[16];
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        rc = ble_uuid_16_to_128(BLE_ATT_UUID_CHARACTERISTIC, uuid128);
        assert(rc == 0);

        req.batq_start_handle = proc->disc_all_chrs.prev_handle + 1;
        req.batq_end_handle = proc->disc_all_chrs.end_handle;

        rc = ble_att_clt_tx_read_type(conn, &req, uuid128);
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_disc_all_chrs_cb(proc, rc, 0, NULL);
        return BLE_HS_EDONE;
    }
}

/**
 * Handles an incoming ATT error response for the specified
 * discover-all-characteristics proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_disc_all_chrs_err(struct ble_gattc_proc *proc, int status,
                            uint16_t att_handle)
{
    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Discovery is complete. */
        status = 0;
    }

    ble_gattc_disc_all_chrs_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming "attribute data" entry from a read-by-type response for
 * the specified discover-all-characteristics proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_disc_all_chrs_rx_adata(struct ble_gattc_proc *proc,
                                 struct ble_att_read_type_adata *adata)
{
    struct ble_gatt_chr chr;
    uint16_t uuid16;
    int cbrc;
    int rc;

    memset(&chr, 0, sizeof chr);
    chr.decl_handle = adata->att_handle;

    switch (adata->value_len) {
    case BLE_GATT_CHR_DECL_SZ_16:
        uuid16 = le16toh(adata->value + 3);
        rc = ble_uuid_16_to_128(uuid16, chr.uuid128);
        if (rc != 0) {
            rc = BLE_HS_EBADDATA;
            goto done;
        }
        break;

    case BLE_GATT_CHR_DECL_SZ_128:
        memcpy(chr.uuid128, adata->value + 3, 16);
        break;

    default:
        rc = BLE_HS_EBADDATA;
        goto done;
    }

    chr.properties = adata->value[0];
    chr.value_handle = le16toh(adata->value + 1);

    if (adata->att_handle <= proc->disc_all_chrs.prev_handle) {
        /* Peer sent characteristics out of order; terminate procedure. */
        rc = BLE_HS_EBADDATA;
        goto done;
    }
    proc->disc_all_chrs.prev_handle = adata->att_handle;

    rc = 0;

done:
    cbrc = ble_gattc_disc_all_chrs_cb(proc, rc, 0, &chr);
    if (rc == 0) {
        rc = cbrc;
    }

    return rc;
}

/**
 * Handles a notification that a read-by-type response has been fully
 * processed for the specified discover-all-characteristics proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_disc_all_chrs_rx_complete(struct ble_gattc_proc *proc, int status)
{
    if (status != 0 ||
        proc->disc_all_chrs.prev_handle == proc->disc_all_chrs.end_handle) {

        /* Error or all svcs discovered. */
        ble_gattc_disc_all_chrs_cb(proc, status, 0, NULL);
        return 1;
    } else {
        /* Send follow-up request. */
        ble_gattc_proc_set_pending(proc);
        return 0;
    }
}

/**
 * Initiates GATT procedure: Discover All Characteristics of a Service.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param start_handle          The handle to begin the search at (generally
 *                                  the service definition handle).
 * @param end_handle            The handle to end the search at (generally the
 *                                  last handle in the service).
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The argument to pass to the callback function.
 */
int
ble_gattc_disc_all_chrs(uint16_t conn_handle, uint16_t start_handle,
                        uint16_t end_handle, ble_gatt_chr_fn *cb,
                        void *cb_arg)
{
#if !NIMBLE_OPT_GATT_DISC_ALL_CHRS
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_DISC_ALL_CHRS, &proc);
    if (rc != 0) {
        return rc;
    }
    proc->disc_all_chrs.prev_handle = start_handle - 1;
    proc->disc_all_chrs.end_handle = end_handle;
    proc->disc_all_chrs.cb = cb;
    proc->disc_all_chrs.cb_arg = cb_arg;
    ble_gattc_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $discover characteristic by uuid                                          *
 *****************************************************************************/

/**
 * Calls a discover-characteristic-by-uuid proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_disc_chr_uuid_cb(struct ble_gattc_proc *proc, int status,
                           uint16_t att_handle, struct ble_gatt_chr *chr)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (proc->disc_chr_uuid.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->disc_chr_uuid.cb(proc->fsm_proc.conn_handle,
                                    ble_gattc_error(status, att_handle), chr,
                                    proc->disc_chr_uuid.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified
 * discover-characteristic-by-uuid proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_disc_chr_uuid_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_read_type_req req;
    struct ble_hs_conn *conn;
    uint8_t uuid128[16];
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        rc = ble_uuid_16_to_128(BLE_ATT_UUID_CHARACTERISTIC, uuid128);
        assert(rc == 0);

        req.batq_start_handle = proc->disc_chr_uuid.prev_handle + 1;
        req.batq_end_handle = proc->disc_chr_uuid.end_handle;

        rc = ble_att_clt_tx_read_type(conn, &req, uuid128);
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_disc_chr_uuid_cb(proc, rc, 0, NULL);
        return BLE_HS_EDONE;
    }
}

/**
 * Handles an incoming ATT error response for the specified
 * discover-characteristic-by-uuid proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_disc_chr_uuid_err(struct ble_gattc_proc *proc, int status,
                            uint16_t att_handle)
{
    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Discovery is complete. */
        status = 0;
    }

    ble_gattc_disc_chr_uuid_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming "attribute data" entry from a read-by-type response for
 * the specified discover-characteristics-by-uuid proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_disc_chr_uuid_rx_adata(struct ble_gattc_proc *proc,
                                 struct ble_att_read_type_adata *adata)
{
    struct ble_gatt_chr chr;
    uint16_t uuid16;
    int cbrc;
    int rc;

    memset(&chr, 0, sizeof chr);
    chr.decl_handle = adata->att_handle;

    switch (adata->value_len) {
    case BLE_GATT_CHR_DECL_SZ_16:
        uuid16 = le16toh(adata->value + 3);
        rc = ble_uuid_16_to_128(uuid16, chr.uuid128);
        if (rc != 0) {
            rc = BLE_HS_EBADDATA;
            goto done;
        }
        break;

    case BLE_GATT_CHR_DECL_SZ_128:
        memcpy(chr.uuid128, adata->value + 3, 16);
        break;

    default:
        rc = BLE_HS_EBADDATA;
        goto done;
    }

    chr.properties = adata->value[0];
    chr.value_handle = le16toh(adata->value + 1);

    if (adata->att_handle <= proc->disc_chr_uuid.prev_handle) {
        /* Peer sent characteristics out of order; terminate procedure. */
        rc = BLE_HS_EBADDATA;
        goto done;
    }

    proc->disc_chr_uuid.prev_handle = adata->att_handle;

    rc = 0;

done:
    if (rc != 0 ||
        memcmp(chr.uuid128, proc->disc_chr_uuid.chr_uuid, 16) == 0) {

        cbrc = ble_gattc_disc_chr_uuid_cb(proc, rc, 0, &chr);
        if (rc == 0) {
            rc = cbrc;
        }
    }

    return rc;
}

/**
 * Handles a notification that a read-by-type response has been fully
 * processed for the specified discover-characteristics-by-uuid proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_disc_chr_uuid_rx_complete(struct ble_gattc_proc *proc, int status)
{
    if (status != 0 ||
        proc->disc_chr_uuid.prev_handle == proc->disc_chr_uuid.end_handle) {

        /* Error or all svcs discovered. */
        ble_gattc_disc_chr_uuid_cb(proc, status, 0, NULL);
        return 1;
    } else {
        /* Send follow-up request. */
        ble_gattc_proc_set_pending(proc);
        return 0;
    }
}

/**
 * Initiates GATT procedure: Discover Characteristics by UUID.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param start_handle          The handle to begin the search at (generally
 *                                  the service definition handle).
 * @param end_handle            The handle to end the search at (generally the
 *                                  last handle in the service).
 * @param chr_uuid128           The 128-bit UUID of the characteristic to
 *                                  discover.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The argument to pass to the callback function.
 */
int
ble_gattc_disc_chrs_by_uuid(uint16_t conn_handle, uint16_t start_handle,
                            uint16_t end_handle, void *uuid128,
                            ble_gatt_chr_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT_GATT_DISC_CHR_UUID
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_DISC_CHRS_UUID, &proc);
    if (rc != 0) {
        return rc;
    }
    memcpy(proc->disc_chr_uuid.chr_uuid, uuid128, 16);
    proc->disc_chr_uuid.prev_handle = start_handle - 1;
    proc->disc_chr_uuid.end_handle = end_handle;
    proc->disc_chr_uuid.cb = cb;
    proc->disc_chr_uuid.cb_arg = cb_arg;
    ble_gattc_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $discover all characteristic descriptors                                  *
 *****************************************************************************/

/**
 * Calls a discover-all-descriptors proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_disc_all_dscs_cb(struct ble_gattc_proc *proc, int status,
                           uint16_t att_handle, struct ble_gatt_dsc *dsc)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (proc->disc_all_dscs.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->disc_all_dscs.cb(proc->fsm_proc.conn_handle,
                                    ble_gattc_error(status, att_handle),
                                    proc->disc_all_dscs.chr_def_handle,
                                    dsc, proc->disc_all_dscs.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified discover-all-descriptors proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_disc_all_dscs_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_find_info_req req;
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {

        req.bafq_start_handle = proc->disc_all_dscs.prev_handle + 1;
        req.bafq_end_handle = proc->disc_all_dscs.end_handle;

        rc = ble_att_clt_tx_find_info(conn, &req);
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_disc_all_dscs_cb(proc, rc, 0, NULL);
        return BLE_HS_EDONE;
    }
}

/**
 * Handles an incoming ATT error response for the specified
 * discover-all-descriptors proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_disc_all_dscs_err(struct ble_gattc_proc *proc, int status,
                            uint16_t att_handle)
{
    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Discovery is complete. */
        status = 0;
    }

    ble_gattc_disc_all_dscs_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming "information data" entry from a find-information
 * response for the specified discover-all-descriptors proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_disc_all_dscs_rx_idata(struct ble_gattc_proc *proc,
                                 struct ble_att_find_info_idata *idata)
{
    struct ble_gatt_dsc dsc;
    int cbrc;
    int rc;

    if (idata->attr_handle <= proc->disc_all_dscs.prev_handle) {
        /* Peer sent descriptors out of order; terminate procedure. */
        rc = BLE_HS_EBADDATA;
        goto done;
    }
    proc->disc_all_dscs.prev_handle = idata->attr_handle;

    rc = 0;

done:
    dsc.handle = idata->attr_handle;
    memcpy(dsc.uuid128, idata->uuid128, 16);

    cbrc = ble_gattc_disc_all_dscs_cb(proc, rc, 0, &dsc);
    if (rc == 0) {
        rc = cbrc;
    }
    return rc;
}

/**
 * Handles a notification that a find-information response has been fully
 * processed for the specified discover-all-descriptors proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_disc_all_dscs_rx_complete(struct ble_gattc_proc *proc, int status)
{
    if (status != 0 ||
        proc->disc_all_dscs.prev_handle == proc->disc_all_dscs.end_handle) {

        /* Error or all descriptors discovered. */
        ble_gattc_disc_all_dscs_cb(proc, status, 0, NULL);
        return 1;
    } else {
        /* Send follow-up request. */
        ble_gattc_proc_set_pending(proc);
        return 0;
    }
}

/**
 * Initiates GATT procedure: Discover All Characteristic Descriptors.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param chr_def_handle        The handle of the characteristic definition
 *                                  attribute.
 * @param chr_end_handle        The last handle in the characteristic
 *                                  definition.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The argument to pass to the callback function.
 */
int
ble_gattc_disc_all_dscs(uint16_t conn_handle, uint16_t chr_def_handle,
                        uint16_t chr_end_handle,
                        ble_gatt_dsc_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT_GATT_DISC_ALL_DSCS
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_DISC_ALL_DSCS, &proc);
    if (rc != 0) {
        return rc;
    }
    proc->disc_all_dscs.chr_def_handle = chr_def_handle;
    proc->disc_all_dscs.prev_handle = chr_def_handle + 1;
    proc->disc_all_dscs.end_handle = chr_end_handle;
    proc->disc_all_dscs.cb = cb;
    proc->disc_all_dscs.cb_arg = cb_arg;
    ble_gattc_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $read                                                                     *
 *****************************************************************************/

/**
 * Calls a read-characteristic proc's callback with the specified parameters.
 * If the proc has no callback, this function is a no-op.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_read_cb(struct ble_gattc_proc *proc, int status,
                  uint16_t att_handle, struct ble_gatt_attr *attr)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (proc->read.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->read.cb(proc->fsm_proc.conn_handle,
                           ble_gattc_error(status, att_handle), attr,
                           proc->read.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified read-characteristic-value
 * proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_read_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_read_req req;
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        req.barq_handle = proc->read.handle;
        rc = ble_att_clt_tx_read(conn, &req);
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_read_cb(proc, rc, 0, NULL);
        return BLE_HS_EDONE;
    }
}

/**
 * Handles an incoming ATT error response for the specified
 * read-characteristic-value proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_read_err(struct ble_gattc_proc *proc, int status,
                   uint16_t att_handle)
{
    ble_gattc_read_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming read-response for the specified
 * read-characteristic-value proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_read_rx_read_rsp(struct ble_gattc_proc *proc, int status,
                           void *value, int value_len)
{
    struct ble_gatt_attr attr;

    attr.handle = proc->read.handle;
    attr.offset = 0;
    attr.value_len = value_len;
    attr.value = value;

    ble_gattc_read_cb(proc, status, 0, &attr);

    /* The read operation only has a single request / response exchange. */
    return 1;
}

/**
 * Initiates GATT procedure: Read Characteristic Value.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to read.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The argument to pass to the callback function.
 */
int
ble_gattc_read(uint16_t conn_handle, uint16_t attr_handle,
               ble_gatt_attr_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT_GATT_READ
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_READ, &proc);
    if (rc != 0) {
        return rc;
    }
    proc->read.handle = attr_handle;
    proc->read.cb = cb;
    proc->read.cb_arg = cb_arg;
    ble_gattc_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $read by uuid                                                             *
 *****************************************************************************/

/**
 * Calls a read-using-characteristic-uuid proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_read_uuid_cb(struct ble_gattc_proc *proc, int status,
                       uint16_t att_handle, struct ble_gatt_attr *attr)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (proc->read_uuid.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->read_uuid.cb(proc->fsm_proc.conn_handle,
                                 ble_gattc_error(status, att_handle), attr,
                                 proc->read_uuid.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified read-using-characteristic-uuid
 * proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_read_uuid_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_read_type_req req;
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        req.batq_start_handle = proc->read_uuid.prev_handle + 1;
        req.batq_end_handle = proc->read_uuid.end_handle;
        rc = ble_att_clt_tx_read_type(conn, &req, proc->read_uuid.uuid128);
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_read_uuid_cb(proc, rc, 0, NULL);
        return BLE_HS_EDONE;
    }
}

/**
 * Handles an incoming ATT error response for the specified
 * read-using-characteristic-uuid proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_read_uuid_err(struct ble_gattc_proc *proc, int status,
                        uint16_t att_handle)
{
    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Read is complete. */
        status = 0;
    }
    ble_gattc_read_uuid_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming "attribute data" entry from a read-by-type response for
 * the specified read-using-characteristic-uuid proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_read_uuid_rx_adata(struct ble_gattc_proc *proc,
                             struct ble_att_read_type_adata *adata)
{
    struct ble_gatt_attr attr;
    int rc;

    attr.handle = adata->att_handle;
    attr.offset = 0;
    attr.value_len = adata->value_len;
    attr.value = adata->value;

    rc = ble_gattc_read_uuid_cb(proc, 0, 0, &attr);
    if (rc != 0) {
        return rc;
    }

    proc->read_uuid.prev_handle = adata->att_handle;

    return 0;
}

/**
 * Handles a notification that a read-by-type response has been fully
 * processed for the specified read-using-characteristic-uuid proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_read_uuid_rx_complete(struct ble_gattc_proc *proc, int status)
{
    if (status != 0 ||
        proc->read_uuid.prev_handle == proc->read_uuid.end_handle) {

        /* Error or entire range read. */
        ble_gattc_read_uuid_cb(proc, status, 0, NULL);
        return 1;
    } else {
        /* Send follow-up request. */
        ble_gattc_proc_set_pending(proc);
        return 0;
    }
}

/**
 * Initiates GATT procedure: Read Using Characteristic UUID.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param start_handle          The first handle to search (generally the
 *                                  handle of the service definition).
 * @param end_handle            The last handle to search (generally the
 *                                  last handle in the service definition).
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The argument to pass to the callback function.
 */
int
ble_gattc_read_by_uuid(uint16_t conn_handle, uint16_t start_handle,
                       uint16_t end_handle, void *uuid128,
                       ble_gatt_attr_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT_GATT_READ_UUID
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_READ_UUID, &proc);
    if (rc != 0) {
        return rc;
    }
    proc->read_uuid.prev_handle = start_handle - 1;
    proc->read_uuid.end_handle = end_handle;
    memcpy(proc->read_uuid.uuid128, uuid128, 16);
    proc->read_uuid.cb = cb;
    proc->read_uuid.cb_arg = cb_arg;
    ble_gattc_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $read long                                                                *
 *****************************************************************************/

/**
 * Calls a read-long-characteristic proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_read_long_cb(struct ble_gattc_proc *proc, int status,
                       uint16_t att_handle, struct ble_gatt_attr *attr)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (proc->read_long.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->read_long.cb(proc->fsm_proc.conn_handle,
                                ble_gattc_error(status, att_handle), attr,
                                proc->read_long.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified read-long-characteristic proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_read_long_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_read_blob_req blob_req;
    struct ble_att_read_req read_req;
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        if (proc->read_long.offset == 0) {
            read_req.barq_handle = proc->read_long.handle;
            rc = ble_att_clt_tx_read(conn, &read_req);
        } else {
            blob_req.babq_handle = proc->read_long.handle;
            blob_req.babq_offset = proc->read_long.offset;
            rc = ble_att_clt_tx_read_blob(conn, &blob_req);
        }
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_read_long_cb(proc, rc, 0, NULL);
        return BLE_HS_EDONE;
    }
}

/**
 * Handles an incoming ATT error response for the specified
 * read-long-characteristic proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_read_long_err(struct ble_gattc_proc *proc, int status,
                        uint16_t att_handle)
{
    ble_gattc_read_long_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming read-response for the specified
 * read-long-characteristic-values proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_read_long_rx_read_rsp(struct ble_gattc_proc *proc, int status,
                                void *value, int value_len)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    struct ble_gatt_attr attr;
    uint16_t mtu;
    int rc;

    attr.handle = proc->read_long.handle;
    attr.offset = proc->read_long.offset;
    attr.value_len = value_len;
    attr.value = value;

    /* Report partial payload to application. */
    rc = ble_gattc_read_long_cb(proc, status, 0, &attr);
    if (rc != 0 || status != 0) {
        return 1;
    }

    /* Determine if this is the end of the attribute value. */
    ble_hs_conn_lock();

    rc = ble_att_conn_chan_find(proc->fsm_proc.conn_handle, &conn, &chan);
    if (rc == 0) {
        mtu = ble_l2cap_chan_mtu(chan);
    }

    ble_hs_conn_unlock();

    if (rc != 0) {
        return 1;
    }

    if (value_len < mtu - 1) {
        ble_gattc_read_long_cb(proc, 0, 0, NULL);
        return 1;
    } else {
        proc->read_long.offset += value_len;

        /* Send follow-up request. */
        ble_gattc_proc_set_pending(proc);
        return 0;
    }
}

/**
 * Initiates GATT procedure: Read Long Characteristic Values.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param handle                The handle of the characteristic value to read.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The argument to pass to the callback function.
 */
int
ble_gattc_read_long(uint16_t conn_handle, uint16_t handle,
                    ble_gatt_attr_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT_GATT_READ_LONG
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_READ_LONG, &proc);
    if (rc != 0) {
        return rc;
    }
    proc->read_long.handle = handle;
    proc->read_long.offset = 0;
    proc->read_long.cb = cb;
    proc->read_long.cb_arg = cb_arg;
    ble_gattc_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $read multiple                                                            *
 *****************************************************************************/

/**
 * Calls a read-multiple-characteristics proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_read_mult_cb(struct ble_gattc_proc *proc, int status,
                       uint16_t att_handle, uint8_t *attr_data,
                       uint16_t attr_data_len)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (proc->read_mult.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->read_mult.cb(proc->fsm_proc.conn_handle,
                                ble_gattc_error(status, att_handle),
                                proc->read_mult.handles,
                                proc->read_mult.num_handles,
                                attr_data, attr_data_len,
                                proc->read_mult.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified read-multiple-characteristics
 * proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_read_mult_kick(struct ble_gattc_proc *proc)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        rc = ble_att_clt_tx_read_mult(conn, proc->read_mult.handles,
                                      proc->read_mult.num_handles);
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_read_mult_cb(proc, rc, 0, NULL, 0);
        return BLE_HS_EDONE;
    }
}

/**
 * Handles an incoming ATT error response for the specified
 * read-multiple-characteristics proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_read_mult_err(struct ble_gattc_proc *proc, int status,
                        uint16_t att_handle)
{
    ble_gattc_read_mult_cb(proc, status, att_handle, NULL, 0);
}

/**
 * Handles an incoming read-multiple-response for the specified
 * read-multiple-characteristic-values proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_read_mult_rx_read_mult_rsp(struct ble_gattc_proc *proc, int status,
                                     void *value, int value_len)
{
    ble_gattc_read_mult_cb(proc, status, 0, value, value_len);

    /* The read multiple operation only has a single request / response
     * exchange.
     */
    return 1;
}

/**
 * Initiates GATT procedure: Read Multiple Characteristic Values.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param handles               An array of 16-bit attribute handles to read.
 * @param num_handles           The number of entries in the "handles" array.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The argument to pass to the callback function.
 */
int
ble_gattc_read_mult(uint16_t conn_handle, uint16_t *handles,
                    uint8_t num_handles, ble_gatt_mult_attr_fn *cb,
                    void *cb_arg)
{
#if !NIMBLE_OPT_GATT_READ_MULT
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_READ_MULT, &proc);
    if (rc != 0) {
        return rc;
    }
    proc->read_mult.handles = handles;
    proc->read_mult.num_handles = num_handles;
    proc->read_mult.cb = cb;
    proc->read_mult.cb_arg = cb_arg;
    ble_gattc_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $write no response                                                        *
 *****************************************************************************/

/**
 * Calls a write-characteristic-value proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_write_cb(struct ble_gattc_proc *proc, int status,
                   uint16_t att_handle)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (proc->write.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->write.cb(proc->fsm_proc.conn_handle,
                            ble_gattc_error(status, att_handle),
                            &proc->write.attr, proc->write.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified write-without-response proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_write_no_rsp_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_write_req req;
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        req.bawq_handle = proc->write.attr.handle;
        rc = ble_att_clt_tx_write_cmd(conn, &req, proc->write.attr.value,
                                      proc->write.attr.value_len);
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        /* No response expected; call callback immediately and return 'done' to
         * indicate the proc should be freed.
         */
        ble_gattc_write_cb(proc, 0, 0);
        return BLE_HS_EDONE;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_write_cb(proc, rc, 0);
        return BLE_HS_EDONE;
    }
}

/**
 * Initiates GATT procedure: Write Without Response.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to write
 *                                  to.
 * @param value                 The value to write to the characteristic.
 * @param value_len             The number of bytes to write.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The argument to pass to the callback function.
 */
int
ble_gattc_write_no_rsp(uint16_t conn_handle, uint16_t attr_handle, void *value,
                       uint16_t value_len, ble_gatt_attr_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT_GATT_WRITE_NO_RSP
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_WRITE_NO_RSP, &proc);
    if (rc != 0) {
        return rc;
    }

    proc->write.attr.handle = attr_handle;
    proc->write.attr.value = value;
    proc->write.attr.value_len = value_len;
    proc->write.cb = cb;
    proc->write.cb_arg = cb_arg;
    ble_gattc_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $write                                                                    *
 *****************************************************************************/

/**
 * Triggers a pending transmit for the specified write-characteristic-value
 * proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_write_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_write_req req;
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        req.bawq_handle = proc->write.attr.handle;
        rc = ble_att_clt_tx_write_req(conn, &req, proc->write.attr.value,
                                      proc->write.attr.value_len);
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_write_cb(proc, rc, 0);
        return BLE_HS_EDONE;
    }
}

/**
 * Handles an incoming ATT error response for the specified
 * write-characteristic-value proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_write_err(struct ble_gattc_proc *proc, int status,
                    uint16_t att_handle)
{
    ble_gattc_write_cb(proc, status, att_handle);
}

/**
 * Handles an incoming write-response for the specified
 * write-characteristic-value proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_write_rx_rsp(struct ble_gattc_proc *proc)
{
    ble_gattc_write_cb(proc, 0, 0);

    /* The write operation only has a single request / response exchange. */
    return 1;
}

/**
 * Initiates GATT procedure: Write Characteristic Value.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to write
 *                                  to.
 * @param value                 The value to write to the characteristic.
 * @param value_len             The number of bytes to write.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The argument to pass to the callback function.
 */
int
ble_gattc_write(uint16_t conn_handle, uint16_t attr_handle, void *value,
                uint16_t value_len, ble_gatt_attr_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT_GATT_WRITE
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_WRITE, &proc);
    if (rc != 0) {
        return rc;
    }

    proc->write.attr.handle = attr_handle;
    proc->write.attr.value = value;
    proc->write.attr.value_len = value_len;
    proc->write.cb = cb;
    proc->write.cb_arg = cb_arg;
    ble_gattc_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $write long                                                               *
 *****************************************************************************/

/**
 * Calls a write-long-characteristic-value proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_write_long_cb(struct ble_gattc_proc *proc, int status,
                        uint16_t att_handle)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (proc->write_long.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->write_long.cb(proc->fsm_proc.conn_handle,
                                 ble_gattc_error(status, att_handle),
                                 &proc->write_long.attr,
                                 proc->write_long.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified
 * write-long-characteristic-value proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_write_long_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_prep_write_cmd prep_req;
    struct ble_att_exec_write_req exec_req;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int max_sz;
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        if (proc->write_long.attr.offset < proc->write_long.attr.value_len) {
            chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
            assert(chan != NULL);

            max_sz = ble_l2cap_chan_mtu(chan) - BLE_ATT_PREP_WRITE_CMD_BASE_SZ;
            if (proc->write_long.attr.offset + max_sz >
                proc->write_long.attr.value_len) {

                proc->write_long.length = proc->write_long.attr.value_len -
                                          proc->write_long.attr.offset;
            } else {
                proc->write_long.length = max_sz;
            }

            prep_req.bapc_handle = proc->write_long.attr.handle;
            prep_req.bapc_offset = proc->write_long.attr.offset;
            rc = ble_att_clt_tx_prep_write(conn, &prep_req,
                                           proc->write_long.attr.value +
                                               proc->write_long.attr.offset,
                                           proc->write_long.length);
        } else {
            exec_req.baeq_flags = BLE_ATT_EXEC_WRITE_F_CONFIRM;
            rc = ble_att_clt_tx_exec_write(conn, &exec_req);
        }
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_write_long_cb(proc, rc, 0);
        return BLE_HS_EDONE;
    }
}

/**
 * Handles an incoming ATT error response for the specified
 * write-long-characteristic-value proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_write_long_err(struct ble_gattc_proc *proc, int status,
                         uint16_t att_handle)
{
    ble_gattc_write_long_cb(proc, status, att_handle);
}

/**
 * Handles an incoming prepare-write-response for the specified
 * write-long-cahracteristic-values proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_write_long_rx_prep(struct ble_gattc_proc *proc,
                             int status, struct ble_att_prep_write_cmd *rsp,
                             void *attr_data, uint16_t attr_len)
{
    int rc;

    if (status != 0) {
        rc = status;
        goto err;
    }

    /* Verify the response. */
    if (rsp->bapc_handle != proc->write_long.attr.handle) {
        rc = BLE_HS_EBADDATA;
        goto err;
    }
    if (rsp->bapc_offset != proc->write_long.attr.offset) {
        rc = BLE_HS_EBADDATA;
        goto err;
    }
    if (rsp->bapc_offset + attr_len > proc->write_long.attr.value_len) {
        rc = BLE_HS_EBADDATA;
        goto err;
    }
    if (attr_len != proc->write_long.length) {
        rc = BLE_HS_EBADDATA;
        goto err;
    }
    if (memcmp(attr_data, proc->write_long.attr.value + rsp->bapc_offset,
               attr_len) != 0) {
        rc = BLE_HS_EBADDATA;
        goto err;
    }

    proc->write_long.attr.offset += attr_len;
    ble_gattc_proc_set_pending(proc);

    return 0;

err:
    /* XXX: Might need to cancel pending writes. */
    ble_gattc_write_long_cb(proc, rc, 0);
    return 1;
}

/**
 * Handles an incoming execute-write-response for the specified
 * write-long-characteristic-values proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_write_long_rx_exec(struct ble_gattc_proc *proc, int status)
{
    ble_gattc_write_long_cb(proc, status, 0);
    return 1;
}

/**
 * Initiates GATT procedure: Write Long Characteristic Values.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to write
 *                                  to.
 * @param value                 The value to write to the characteristic.
 * @param value_len             The number of bytes to write.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The argument to pass to the callback function.
 */
int
ble_gattc_write_long(uint16_t conn_handle, uint16_t attr_handle, void *value,
                     uint16_t value_len, ble_gatt_attr_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT_GATT_WRITE_LONG
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_WRITE_LONG, &proc);
    if (rc != 0) {
        return rc;
    }

    proc->write_long.attr.handle = attr_handle;
    proc->write_long.attr.offset = 0;
    proc->write_long.attr.value = value;
    proc->write_long.attr.value_len = value_len;
    proc->write_long.cb = cb;
    proc->write_long.cb_arg = cb_arg;
    ble_gattc_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $write reliable                                                           *
 *****************************************************************************/

/**
 * Calls a write-long-characteristic-value proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_write_reliable_cb(struct ble_gattc_proc *proc, int status,
                            uint16_t att_handle)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (proc->write_reliable.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->write_reliable.cb(proc->fsm_proc.conn_handle,
                                     ble_gattc_error(status, att_handle),
                                     proc->write_reliable.attrs,
                                     proc->write_reliable.num_attrs,
                                     proc->write_reliable.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified
 * write-reliable-characteristic-value proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_write_reliable_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_prep_write_cmd prep_req;
    struct ble_att_exec_write_req exec_req;
    struct ble_gatt_attr *attr;
    struct ble_hs_conn *conn;
    int attr_idx;
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        attr_idx = proc->write_reliable.cur_attr;
        if (attr_idx < proc->write_reliable.num_attrs) {
            attr = proc->write_reliable.attrs + attr_idx;
            prep_req.bapc_handle = attr->handle;
            prep_req.bapc_offset = 0;
            rc = ble_att_clt_tx_prep_write(conn, &prep_req, attr->value,
                                           attr->value_len);
        } else {
            exec_req.baeq_flags = BLE_ATT_EXEC_WRITE_F_CONFIRM;
            rc = ble_att_clt_tx_exec_write(conn, &exec_req);
        }
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_write_reliable_cb(proc, rc, 0);
        return BLE_HS_EDONE;
    }
}

/**
 * Handles an incoming ATT error response for the specified
 * write-reliable-characteristic-value proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_write_reliable_err(struct ble_gattc_proc *proc, int status,
                             uint16_t att_handle)
{
    ble_gattc_write_reliable_cb(proc, status, att_handle);
}

/**
 * Handles an incoming prepare-write-response for the specified
 * write-reliable-cahracteristic-values proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_write_reliable_rx_prep(struct ble_gattc_proc *proc,
                                 int status,
                                 struct ble_att_prep_write_cmd *rsp,
                                 void *attr_data, uint16_t attr_len)
{
    struct ble_gatt_attr *attr;
    int rc;

    if (status != 0) {
        rc = status;
        goto err;
    }

    if (proc->write_reliable.cur_attr >= proc->write_reliable.num_attrs) {
        rc = BLE_HS_EBADDATA;
        goto err;
    }
    attr = proc->write_reliable.attrs + proc->write_reliable.cur_attr;

    /* Verify the response. */
    if (rsp->bapc_handle != attr->handle) {
        rc = BLE_HS_EBADDATA;
        goto err;
    }
    if (rsp->bapc_offset != 0) {
        rc = BLE_HS_EBADDATA;
        goto err;
    }
    if (attr_len != attr->value_len) {
        rc = BLE_HS_EBADDATA;
        goto err;
    }
    if (memcmp(attr_data, attr->value, attr_len) != 0) {
        rc = BLE_HS_EBADDATA;
        goto err;
    }

    proc->write_reliable.cur_attr++;
    ble_gattc_proc_set_pending(proc);

    return 0;

err:
    /* XXX: Might need to cancel pending writes. */
    ble_gattc_write_reliable_cb(proc, rc, 0);
    return 1;
}

/**
 * Handles an incoming execute-write-response for the specified
 * write-reliable-characteristic-values proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_write_reliable_rx_exec(struct ble_gattc_proc *proc, int status)
{
    ble_gattc_write_reliable_cb(proc, status, 0);
    return 1;
}

/**
 * Initiates GATT procedure: Write Long Characteristic Values.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to write
 *                                  to.
 * @param value                 The value to write to the characteristic.
 * @param value_len             The number of bytes to write.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The argument to pass to the callback function.
 */
int
ble_gattc_write_reliable(uint16_t conn_handle, struct ble_gatt_attr *attrs,
                         int num_attrs, ble_gatt_reliable_attr_fn *cb,
                         void *cb_arg)
{
#if !NIMBLE_OPT_GATT_WRITE_RELIABLE
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_WRITE_RELIABLE, &proc);
    if (rc != 0) {
        return rc;
    }

    proc->write_reliable.attrs = attrs;
    proc->write_reliable.num_attrs = num_attrs;
    proc->write_reliable.cur_attr = 0;
    proc->write_reliable.cb = cb;
    proc->write_reliable.cb_arg = cb_arg;
    ble_gattc_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $notify                                                                   *
 *****************************************************************************/

/**
 * Triggers a pending transmit for the specified indication proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_notify_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_svr_access_ctxt ctxt;
    struct ble_att_notify_req req;
    struct ble_hs_conn *conn;
    int rc;

    if (proc->notify.attr.value == NULL) {
        rc = ble_att_svr_read_handle(BLE_HS_CONN_HANDLE_NONE,
                                     proc->notify.attr.handle, &ctxt,
                                     NULL);
        if (rc != 0) {
            /* Fatal error; application disallowed attribute read. */
            return BLE_HS_EDONE;
        }
    } else {
        ctxt.attr_data = proc->notify.attr.value;
        ctxt.data_len = proc->notify.attr.value_len;
        ctxt.offset = 0;
    }

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        proc->notify.attr.value = ctxt.attr_data;
        proc->notify.attr.value_len = ctxt.data_len;

        req.banq_handle = proc->notify.attr.handle;
        rc = ble_att_clt_tx_notify(conn, &req, proc->notify.attr.value,
                                   proc->notify.attr.value_len);
    }

    ble_hs_conn_unlock();

    if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
        return BLE_HS_EAGAIN;
    } else {
        return BLE_HS_EDONE;
    }
}

/**
 * Sends an attribute notification.  The content of the message is specified
 * in the attr parameter.
 *
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 *     o Caller unlocks ble_hs_conn.
 */
int
ble_gattc_notify_custom(uint16_t conn_handle, struct ble_gatt_attr *attr)
{
#if !NIMBLE_OPT_GATT_NOTIFY
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_NOTIFY, &proc);
    if (rc != 0) {
        return rc;
    }

    proc->notify.attr = *attr;
    ble_gattc_proc_set_pending(proc);

    return rc;
}

/**
 * Sends an attribute notification.  The content of the message is read from
 * the specified characteristic.
 *
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 *     o Caller locks ble_hs_conn.
 */
int
ble_gattc_notify(uint16_t conn_handle, uint16_t chr_val_handle)
{
#if !NIMBLE_OPT_GATT_NOTIFY
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gatt_attr attr;
    int rc;

    attr.handle = chr_val_handle;
    attr.offset = 0;
    attr.value_len = 0;
    attr.value = NULL;

    rc = ble_gattc_notify_custom(conn_handle, &attr);
    return rc;
}

/*****************************************************************************
 * $indicate                                                                 *
 *****************************************************************************/

/**
 * Calls an indication proc's callback with the specified parameters.  If the
 * proc has no callback, this function is a no-op.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_indicate_cb(struct ble_gattc_proc *proc, int status,
                      uint16_t att_handle)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (proc->indicate.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->indicate.cb(proc->fsm_proc.conn_handle,
                               ble_gattc_error(status, att_handle),
                               &proc->indicate.attr, proc->indicate.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified indication proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_indicate_kick(struct ble_gattc_proc *proc)
{
    struct ble_att_svr_access_ctxt ctxt;
    struct ble_att_indicate_req req;
    struct ble_hs_conn *conn;
    int rc;

    if (proc->indicate.attr.value == NULL) {
        rc = ble_att_svr_read_handle(BLE_HS_CONN_HANDLE_NONE,
                                     proc->indicate.attr.handle, &ctxt,
                                     NULL);
        if (rc != 0) {
            /* Fatal error; application disallowed attribute read. */
            return BLE_HS_EDONE;
        }
    } else {
        ctxt.attr_data = proc->indicate.attr.value;
        ctxt.data_len = proc->indicate.attr.value_len;
        ctxt.offset = 0;
    }

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else {
        proc->indicate.attr.value = ctxt.attr_data;
        proc->indicate.attr.value_len = ctxt.data_len;

        req.baiq_handle = proc->indicate.attr.handle;
        rc = ble_att_clt_tx_indicate(conn, &req, proc->indicate.attr.value,
                                     proc->indicate.attr.value_len);
    }

    ble_hs_conn_unlock();

    if (rc == 0) {
        return 0;
    } else {
        if (ble_fsm_tx_postpone_chk(&proc->fsm_proc, rc)) {
            return BLE_HS_EAGAIN;
        }

        ble_gattc_indicate_cb(proc, rc, 0);
        return BLE_HS_EDONE;
    }
}

/**
 * Handles an incoming ATT error response for the specified indication proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gattc_indicate_err(struct ble_gattc_proc *proc, int status,
                       uint16_t att_handle)
{
    ble_gattc_indicate_cb(proc, status, att_handle);
}

/**
 * Handles an incoming handle-value-confirmation for the specified indication
 * proc.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gattc_indicate_rx_rsp(struct ble_gattc_proc *proc)
{
    struct ble_hs_conn *conn;

    ble_gattc_indicate_cb(proc, 0, 0);

    /* Now that the confirmation has been received, we can send any subsequent
     * indication.
     */
    ble_hs_conn_lock();

    conn = ble_hs_conn_find(proc->fsm_proc.conn_handle);
    if (conn != NULL) {
        conn->bhc_gatt_svr.flags &= ~BLE_GATTS_CONN_F_INDICATION_TXED;

        /* Send the next indication if one is pending. */
        ble_gatts_send_notifications(conn);
    }

    ble_hs_conn_unlock();

    /* The indicate operation only has a single request / response exchange. */
    return 1;
}

/**
 * Sends an attribute indication.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 */
int
ble_gattc_indicate(uint16_t conn_handle, uint16_t chr_val_handle,
                   ble_gatt_attr_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT_GATT_INDICATE
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    rc = ble_gattc_new_proc(conn_handle, BLE_GATT_OP_INDICATE, &proc);
    if (rc != 0) {
        return rc;
    }

    proc->indicate.attr.handle = chr_val_handle;
    proc->indicate.cb = cb;
    proc->indicate.cb_arg = cb_arg;
    ble_gattc_proc_set_pending(proc);

    return 0;
}

/*****************************************************************************
 * $rx                                                                       *
 *****************************************************************************/

struct ble_gattc_rx_extract_arg {
    uint16_t conn_handle;
    uint8_t op;
    const void *rx_entries;
    int num_rx_entries;

    const void *out_rx_entry;
};

static int
ble_gattc_rx_extract_cb(struct ble_fsm_proc *proc, void *arg)
{
    struct ble_gattc_rx_extract_arg *extract_arg;
    const void *rx_entry;

    extract_arg = arg;

    rx_entry = NULL;

    if (ble_gattc_proc_matches(proc, extract_arg->conn_handle,
                               extract_arg->op, 1)) {
        if (extract_arg->rx_entries == NULL) {
            return BLE_FSM_EXTRACT_EMOVE_STOP;
        }

        rx_entry = ble_gattc_rx_entry_find(proc->op,
                                           extract_arg->rx_entries,
                                           extract_arg->num_rx_entries);
        assert(rx_entry != NULL);
        if (rx_entry != NULL) {
            extract_arg->out_rx_entry = rx_entry;
            return BLE_FSM_EXTRACT_EMOVE_STOP;
        }
    }

    return BLE_FSM_EXTRACT_EKEEP_CONTINUE;
}

static struct ble_gattc_proc *
ble_gattc_rx_extract_gen(uint16_t conn_handle, uint8_t op,
                         const void *rx_entries, int num_entries,
                         const void **out_rx_entry)
{
    struct ble_gattc_rx_extract_arg arg;
    struct ble_gattc_proc *proc;
    int rc;

    arg.conn_handle = conn_handle;
    arg.op = BLE_GATT_OP_NONE;
    arg.rx_entries = rx_entries;
    arg.num_rx_entries = num_entries;

    rc = ble_fsm_proc_extract(&ble_gattc_fsm, (struct ble_fsm_proc **)&proc,
                              ble_gattc_rx_extract_cb, &arg);

    if (out_rx_entry != NULL) {
        *out_rx_entry = arg.out_rx_entry;
    }
    if (rc != 0) {
        proc = NULL;
    }

    return proc;
}

/**
 * Searches the main proc list for an "expecting" entry whose connection handle
 * and op code match those specified.  If a matching entry is found, it is
 * removed from the list and returned.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection handle to match against.
 * @param op                    The op code to match against.
 *
 * @return                      The matching proc entry on success;
 *                                  null on failure.
 */
static struct ble_gattc_proc *
ble_gattc_rx_extract(uint16_t conn_handle, uint8_t op)
{
    struct ble_gattc_proc *proc;

    proc = ble_gattc_rx_extract_gen(conn_handle, op, NULL, 0, NULL);
    return proc;
}

/**
 * Searches the main proc list for an "expecting" entry whose connection handle
 * and op code match those specified.  If a matching entry is found, it is
 * removed from the list and returned.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The connection handle to match against.
 * @param rx_entries            The array of rx entries corresponding to the
 *                                  op code of the incoming response.
 * @param out_rx_entry          On success, the address of the matching rx
 *                                  entry is written to this pointer.
 *
 * @return                      The matching proc entry on success;
 *                                  null on failure.
 */
#define BLE_GATTC_RX_EXTRACT_RX_ENTRY(conn_handle, rx_entries, out_rx_entry)  \
    ble_gattc_rx_extract_gen((conn_handle), BLE_GATT_OP_NONE,                 \
                               (rx_entries),                                  \
                               sizeof (rx_entries) / sizeof (rx_entries)[0],  \
                               (const void **)(out_rx_entry))

/**
 * Dispatches an incoming ATT error-response to the appropriate active GATT
 * procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_err(uint16_t conn_handle, struct ble_att_error_rsp *rsp)
{
    const struct ble_gattc_dispatch_entry *dispatch;
    struct ble_gattc_proc *proc;

    ble_hs_misc_assert_no_locks();

    proc = ble_gattc_rx_extract(conn_handle, BLE_GATT_OP_NONE);
    if (proc != NULL) {
        dispatch = ble_gattc_dispatch_get(proc->fsm_proc.op);
        if (dispatch->err_cb != NULL) {
            dispatch->err_cb(proc, BLE_HS_ERR_ATT_BASE + rsp->baep_error_code,
                             rsp->baep_handle);
        }
        ble_gattc_proc_free(&proc->fsm_proc);
    }
}

/**
 * Dispatches an incoming ATT exchange-mtu-response to the appropriate active
 * GATT procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_mtu(uint16_t conn_handle, int status, uint16_t chan_mtu)
{
    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = ble_gattc_rx_extract(conn_handle, BLE_GATT_OP_MTU);
    if (proc != NULL) {
        rc = ble_gattc_mtu_rx_rsp(proc, status, chan_mtu);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/**
 * Dispatches an incoming "information data" entry from a
 * find-information-response to the appropriate active GATT procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_find_info_idata(uint16_t conn_handle,
                             struct ble_att_find_info_idata *idata)
{
#if !NIMBLE_OPT_ATT_CLT_FIND_INFO
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = ble_gattc_rx_extract(conn_handle, BLE_GATT_OP_DISC_ALL_DSCS);
    if (proc != NULL) {
        rc = ble_gattc_disc_all_dscs_rx_idata(proc, idata);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/**
 * Dispatches an incoming notification of the end of a
 * find-information-response to the appropriate active GATT procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_find_info_complete(uint16_t conn_handle, int status)
{
#if !NIMBLE_OPT_ATT_CLT_FIND_INFO
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = ble_gattc_rx_extract(conn_handle, BLE_GATT_OP_DISC_ALL_DSCS);
    if (proc != NULL) {
        rc = ble_gattc_disc_all_dscs_rx_complete(proc, status);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/**
 * Dispatches an incoming "handles info" entry from a
 * find-by-type-value-response to the appropriate active GATT procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_find_type_value_hinfo(uint16_t conn_handle,
                                   struct ble_att_find_type_value_hinfo *hinfo)
{
#if !NIMBLE_OPT_ATT_CLT_FIND_TYPE
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = ble_gattc_rx_extract(conn_handle, BLE_GATT_OP_DISC_SVC_UUID);
    if (proc != NULL) {
        rc = ble_gattc_disc_svc_uuid_rx_hinfo(proc, hinfo);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/**
 * Dispatches an incoming notification of the end of a
 * find-by-type-value-response to the appropriate active GATT procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_find_type_value_complete(uint16_t conn_handle, int status)
{
#if !NIMBLE_OPT_ATT_CLT_FIND_TYPE
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = ble_gattc_rx_extract(conn_handle, BLE_GATT_OP_DISC_SVC_UUID);
    if (proc != NULL) {
        rc = ble_gattc_disc_svc_uuid_rx_complete(proc, status);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/**
 * Dispatches an incoming "attribute data" entry from a read-by-type-response
 * to the appropriate active GATT procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_read_type_adata(uint16_t conn_handle,
                             struct ble_att_read_type_adata *adata)
{
#if !NIMBLE_OPT_ATT_CLT_READ_TYPE
    return;
#endif

    const struct ble_gattc_rx_adata_entry *rx_entry;
    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = BLE_GATTC_RX_EXTRACT_RX_ENTRY(conn_handle,
                                         ble_gattc_rx_read_type_elem_entries,
                                         &rx_entry);
    if (proc != NULL) {
        rc = rx_entry->cb(proc, adata);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/**
 * Dispatches an incoming notification of the end of a read-by-type-response to
 * the appropriate active GATT procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_read_type_complete(uint16_t conn_handle, int status)
{
#if !NIMBLE_OPT_ATT_CLT_READ_TYPE
    return;
#endif

    const struct ble_gattc_rx_complete_entry *rx_entry;
    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = BLE_GATTC_RX_EXTRACT_RX_ENTRY(
        conn_handle, ble_gattc_rx_read_type_complete_entries,
        &rx_entry);
    if (proc != NULL) {
        rc = rx_entry->cb(proc, status);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/**
 * Dispatches an incoming "attribute data" entry from a
 * read-by-group-type-response to the appropriate active GATT procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_read_group_type_adata(uint16_t conn_handle,
                                   struct ble_att_read_group_type_adata *adata)
{
#if !NIMBLE_OPT_ATT_CLT_READ_GROUP_TYPE
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = ble_gattc_rx_extract(conn_handle, BLE_GATT_OP_DISC_ALL_SVCS);
    if (proc != NULL) {
        rc = ble_gattc_disc_all_svcs_rx_adata(proc, adata);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/**
 * Dispatches an incoming notification of the end of a
 * read-by-group-type-response to the appropriate active GATT procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_read_group_type_complete(uint16_t conn_handle, int status)
{
#if !NIMBLE_OPT_ATT_CLT_READ_GROUP_TYPE
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = ble_gattc_rx_extract(conn_handle, BLE_GATT_OP_DISC_ALL_SVCS);
    if (proc != NULL) {
        rc = ble_gattc_disc_all_svcs_rx_complete(proc, status);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/**
 * Dispatches an incoming ATT read-response to the appropriate active GATT
 * procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_read_rsp(uint16_t conn_handle, int status, void *value,
                      int value_len)
{
#if !NIMBLE_OPT_ATT_CLT_READ
    return;
#endif

    const struct ble_gattc_rx_attr_entry *rx_entry;
    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = BLE_GATTC_RX_EXTRACT_RX_ENTRY(conn_handle,
                                         ble_gattc_rx_read_rsp_entries,
                                         &rx_entry);
    if (proc != NULL) {
        rc = rx_entry->cb(proc, status, value, value_len);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/**
 * Dispatches an incoming ATT read-blob-response to the appropriate active GATT
 * procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_read_blob_rsp(uint16_t conn_handle, int status,
                           void *value, int value_len)
{
#if !NIMBLE_OPT_ATT_CLT_READ_BLOB
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = ble_gattc_rx_extract(conn_handle, BLE_GATT_OP_READ_LONG);
    if (proc != NULL) {
        rc = ble_gattc_read_long_rx_read_rsp(proc, status, value, value_len);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/**
 * Dispatches an incoming ATT read-multiple-response to the appropriate active
 * GATT procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_read_mult_rsp(uint16_t conn_handle, int status,
                           void *value, int value_len)
{
#if !NIMBLE_OPT_ATT_CLT_READ_MULT
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = ble_gattc_rx_extract(conn_handle, BLE_GATT_OP_READ_MULT);
    if (proc != NULL) {
        rc = ble_gattc_read_mult_rx_read_mult_rsp(proc, status, value,
                                                  value_len);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/**
 * Dispatches an incoming ATT write-response to the appropriate active GATT
 * procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_write_rsp(uint16_t conn_handle)
{
#if !NIMBLE_OPT_ATT_CLT_WRITE
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = ble_gattc_rx_extract(conn_handle, BLE_GATT_OP_WRITE);
    if (proc != NULL) {
        rc = ble_gattc_write_rx_rsp(proc);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/**
 * Dispatches an incoming ATT prepare-write-response to the appropriate active
 * GATT procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_prep_write_rsp(uint16_t conn_handle, int status,
                            struct ble_att_prep_write_cmd *rsp,
                            void *attr_data, uint16_t attr_data_len)
{
#if !NIMBLE_OPT_ATT_CLT_PREP_WRITE
    return;
#endif

    const struct ble_gattc_rx_prep_entry *rx_entry;
    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = BLE_GATTC_RX_EXTRACT_RX_ENTRY(conn_handle,
                                         ble_gattc_rx_prep_entries,
                                         &rx_entry);
    if (proc != NULL) {
        rc = rx_entry->cb(proc, status, rsp, attr_data, attr_data_len);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/**
 * Dispatches an incoming ATT execute-write-response to the appropriate active
 * GATT procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_exec_write_rsp(uint16_t conn_handle, int status)
{
#if !NIMBLE_OPT_ATT_CLT_EXEC_WRITE
    return;
#endif

    const struct ble_gattc_rx_exec_entry *rx_entry;
    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = BLE_GATTC_RX_EXTRACT_RX_ENTRY(conn_handle,
                                         ble_gattc_rx_exec_entries,
                                         &rx_entry);
    if (proc != NULL) {
        rc = rx_entry->cb(proc, status);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/**
 * Dispatches an incoming ATT handle-value-confirmation to the appropriate
 * active GATT procedure.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_rx_indicate_rsp(uint16_t conn_handle)
{
#if !NIMBLE_OPT_ATT_CLT_INDICATE
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    ble_hs_misc_assert_no_locks();

    proc = ble_gattc_rx_extract(conn_handle, BLE_GATT_OP_INDICATE);
    if (proc != NULL) {
        rc = ble_gattc_indicate_rx_rsp(proc);
        ble_fsm_process_rx_status(&ble_gattc_fsm, &proc->fsm_proc, rc);
    }
}

/*****************************************************************************
 * $misc                                                                     *
 *****************************************************************************/

static int
ble_gattc_proc_kick(struct ble_fsm_proc *proc)
{
    const struct ble_gattc_dispatch_entry *dispatch;
    int rc;

    dispatch = ble_gattc_dispatch_get(proc->op);
    rc = dispatch->kick_cb((struct ble_gattc_proc *)proc);

    return rc;
}

/**
 * Triggers a transmission for each active GATT procedure with a pending send.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gattc_wakeup(void)
{
    ble_fsm_wakeup(&ble_gattc_fsm);
}

/**
 * Called when a BLE connection ends.  Frees all GATT resources associated with
 * the connection and cancels all relevant pending and in-progress GATT
 * procedures.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @param conn_handle           The handle of the connection that was
 *                                  terminated.
 */
void
ble_gattc_connection_broken(uint16_t conn_handle)
{
    const struct ble_gattc_dispatch_entry *dispatch;
    struct ble_fsm_proc_list temp_list;
    struct ble_fsm_proc *proc;
    struct ble_fsm_proc *prev;

    STAILQ_INIT(&temp_list);
    prev = NULL;

    /* Remove all procs with the specified conn handle and insert them into the
     * temporary list.
     */
    ble_gattc_lock();

    STAILQ_FOREACH(proc, &ble_gattc_fsm.procs, next) {
        if (proc->conn_handle == conn_handle) {
            ble_fsm_proc_remove(&ble_gattc_fsm.procs, proc, prev);
            STAILQ_INSERT_TAIL(&temp_list, proc, next);
        } else {
            prev = proc;
        }
    }

    ble_gattc_unlock();

    /* Notify application of failed procedures and free the corresponding proc
     * entries.
     */
    while ((proc = STAILQ_FIRST(&temp_list)) != NULL) {
        dispatch = ble_gattc_dispatch_get(proc->op);
        dispatch->err_cb((struct ble_gattc_proc *)proc, BLE_HS_ENOTCONN, 0);

        ble_fsm_proc_remove(&temp_list, proc, NULL);
        ble_gattc_proc_free(proc);
    }
}

/**
 * Called when a BLE connection transitions into a transmittable state.  Wakes
 * up all congested GATT procedures associated with the connection.
 *
 * Lock restrictions:
 *     o Caller unlocks gattc.
 *
 * @param conn_handle           The handle of the connection to test.
 */
void
ble_gattc_connection_txable(uint16_t conn_handle)
{
    struct ble_fsm_proc *proc;

    ble_gattc_lock();

    STAILQ_FOREACH(proc, &ble_gattc_fsm.procs, next) {
        if (proc->conn_handle == conn_handle &&
            proc->flags & BLE_FSM_PROC_F_CONGESTED) {

            proc->flags &= ~BLE_FSM_PROC_F_CONGESTED;
            if (ble_fsm_proc_can_pend(proc)) {
                ble_gattc_proc_set_pending((struct ble_gattc_proc *)proc);
            }
        }
    }

    ble_gattc_unlock();
}

/**
 * Indicates whether there are currently any active GATT procedures.
 *
 * Lock restrictions: None.
 */
int
ble_gattc_any_jobs(void)
{
    return !STAILQ_EMPTY(&ble_gattc_fsm.procs);
}

/**
 * XXX This function only exists because we can't set a timer before the OS
 * starts.  Maybe the OS issue can be fixed.
 *
 * Lock restrictions: None.
 */
void
ble_gattc_started(void)
{
    int rc;

    rc = os_callout_reset(&ble_gattc_heartbeat_timer.cf_c,
                          BLE_GATT_HEARTBEAT_PERIOD * OS_TICKS_PER_SEC / 1000);
    assert(rc == 0);
}

/**
 * Lock restrictions: None.
 */
int
ble_gattc_init(void)
{
    int rc;

    free(ble_gattc_proc_mem);

    rc = ble_fsm_new(&ble_gattc_fsm, ble_gattc_proc_kick, ble_gattc_proc_free);
    if (rc != 0) {
        goto err;
    }

    if (ble_hs_cfg.max_gattc_procs > 0) {
        ble_gattc_proc_mem = malloc(
            OS_MEMPOOL_BYTES(ble_hs_cfg.max_gattc_procs,
                             sizeof (struct ble_gattc_proc)));
        if (ble_gattc_proc_mem == NULL) {
            rc = BLE_HS_ENOMEM;
            goto err;
        }

        rc = os_mempool_init(&ble_gattc_proc_pool,
                             ble_hs_cfg.max_gattc_procs,
                             sizeof (struct ble_gattc_proc),
                             ble_gattc_proc_mem,
                             "ble_gattc_proc_pool");
        if (rc != 0) {
            goto err;
        }
    }

    os_callout_func_init(&ble_gattc_heartbeat_timer, &ble_hs_evq,
                         ble_gattc_heartbeat, NULL);

    return 0;

err:
    free(ble_gattc_proc_mem);
    ble_gattc_proc_mem = NULL;

    return rc;
}
