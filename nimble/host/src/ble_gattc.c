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
 * GATT client - Generic Attribute Profile; client operations.
 *
 * Design overview:
 *
 * GATT client procedures are initiated by the application via function calls.
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
 *    (ble_gattc_procs).  Thread-safety is achieved by locking the mutex during
 *    removal and insertion operations.  Procedure objects are only modified
 *    while they are not in the list.  This is sufficient, as the host parent
 *    task is the only task which inspects or modifies individual procedure
 *    entries.  Tasks have the following permissions regarding procedure
 *    entries:
 *
 *                | insert  | remove    | inspect   | modify
 *    ------------+---------+-----------|-----------|---------
 *    parent task | X       | X         | X         | X
 *    other tasks | X       |           |           |
 */

#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "os/os_mempool.h"
#include "nimble/ble.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "ble_hs_priv.h"

/*****************************************************************************
 * $definitions / declarations                                               *
 *****************************************************************************/

#define BLE_GATT_UNRESPONSIVE_TIMEOUT           (30 * OS_TICKS_PER_SEC)

#define BLE_GATT_OP_NONE                        UINT8_MAX
#define BLE_GATT_OP_MTU                         0
#define BLE_GATT_OP_DISC_ALL_SVCS               1
#define BLE_GATT_OP_DISC_SVC_UUID               2
#define BLE_GATT_OP_FIND_INC_SVCS               3
#define BLE_GATT_OP_DISC_ALL_CHRS               4
#define BLE_GATT_OP_DISC_CHR_UUID               5
#define BLE_GATT_OP_DISC_ALL_DSCS               6
#define BLE_GATT_OP_READ                        7
#define BLE_GATT_OP_READ_UUID                   8
#define BLE_GATT_OP_READ_LONG                   9
#define BLE_GATT_OP_READ_MULT                   10
#define BLE_GATT_OP_WRITE                       11
#define BLE_GATT_OP_WRITE_LONG                  12
#define BLE_GATT_OP_WRITE_RELIABLE              13
#define BLE_GATT_OP_INDICATE                    14
#define BLE_GATT_OP_MAX                         15

/** Represents an in-progress GATT procedure. */
struct ble_gattc_proc {
    STAILQ_ENTRY(ble_gattc_proc) next;

    uint32_t exp_os_ticks;
    uint16_t conn_handle;
    uint8_t op;

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
            uint16_t chr_val_handle;
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
            ble_gatt_attr_fn *cb;
            void *cb_arg;
        } read_uuid;

        struct {
            uint16_t handle;
            uint16_t offset;
            ble_gatt_attr_fn *cb;
            void *cb_arg;
        } read_long;

        struct {
            ble_gatt_attr_fn *cb;
            void *cb_arg;
        } read_mult;

        struct {
            uint16_t att_handle;
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
            struct ble_gatt_attr attrs[NIMBLE_OPT(GATT_WRITE_MAX_ATTRS)];
            uint8_t num_attrs;
            uint8_t cur_attr;
            uint16_t length;
            ble_gatt_reliable_attr_fn *cb;
            void *cb_arg;
        } write_reliable;

        struct {
            uint16_t chr_val_handle;
        } indicate;
    };
};

STAILQ_HEAD(ble_gattc_proc_list, ble_gattc_proc);

/**
 * Error functions - these handle an incoming ATT error response and apply it
 * to the appropriate active GATT procedure.
 */
typedef void ble_gattc_err_fn(struct ble_gattc_proc *proc, int status,
                              uint16_t att_handle);
static ble_gattc_err_fn ble_gattc_mtu_err;
static ble_gattc_err_fn ble_gattc_disc_all_svcs_err;
static ble_gattc_err_fn ble_gattc_disc_svc_uuid_err;
static ble_gattc_err_fn ble_gattc_find_inc_svcs_err;
static ble_gattc_err_fn ble_gattc_disc_all_chrs_err;
static ble_gattc_err_fn ble_gattc_disc_chr_uuid_err;
static ble_gattc_err_fn ble_gattc_disc_all_dscs_err;
static ble_gattc_err_fn ble_gattc_read_err;
static ble_gattc_err_fn ble_gattc_read_uuid_err;
static ble_gattc_err_fn ble_gattc_read_long_err;
static ble_gattc_err_fn ble_gattc_read_mult_err;
static ble_gattc_err_fn ble_gattc_write_err;
static ble_gattc_err_fn ble_gattc_write_long_err;
static ble_gattc_err_fn ble_gattc_write_reliable_err;
static ble_gattc_err_fn ble_gattc_indicate_err;

static ble_gattc_err_fn * const ble_gattc_err_dispatch[BLE_GATT_OP_MAX] = {
    [BLE_GATT_OP_MTU]               = ble_gattc_mtu_err,
    [BLE_GATT_OP_DISC_ALL_SVCS]     = ble_gattc_disc_all_svcs_err,
    [BLE_GATT_OP_DISC_SVC_UUID]     = ble_gattc_disc_svc_uuid_err,
    [BLE_GATT_OP_FIND_INC_SVCS]     = ble_gattc_find_inc_svcs_err,
    [BLE_GATT_OP_DISC_ALL_CHRS]     = ble_gattc_disc_all_chrs_err,
    [BLE_GATT_OP_DISC_CHR_UUID]     = ble_gattc_disc_chr_uuid_err,
    [BLE_GATT_OP_DISC_ALL_DSCS]     = ble_gattc_disc_all_dscs_err,
    [BLE_GATT_OP_READ]              = ble_gattc_read_err,
    [BLE_GATT_OP_READ_UUID]         = ble_gattc_read_uuid_err,
    [BLE_GATT_OP_READ_LONG]         = ble_gattc_read_long_err,
    [BLE_GATT_OP_READ_MULT]         = ble_gattc_read_mult_err,
    [BLE_GATT_OP_WRITE]             = ble_gattc_write_err,
    [BLE_GATT_OP_WRITE_LONG]        = ble_gattc_write_long_err,
    [BLE_GATT_OP_WRITE_RELIABLE]    = ble_gattc_write_reliable_err,
    [BLE_GATT_OP_INDICATE]          = ble_gattc_indicate_err,
};

/**
 * Receive functions - these handle specific incoming responses and apply them
 * to the appropriate active GATT procedure.
 */
typedef int ble_gattc_rx_adata_fn(struct ble_gattc_proc *proc,
                                  struct ble_att_read_type_adata *adata);

typedef int ble_gattc_rx_prep_fn(struct ble_gattc_proc *proc, int status,
                                 struct ble_att_prep_write_cmd *rsp,
                                 struct os_mbuf **om);

typedef int ble_gattc_rx_attr_fn(struct ble_gattc_proc *proc, int status,
                                 struct os_mbuf **om);

typedef int ble_gattc_rx_complete_fn(struct ble_gattc_proc *proc, int status);
typedef int ble_gattc_rx_exec_fn(struct ble_gattc_proc *proc, int status);

static ble_gattc_rx_adata_fn ble_gattc_find_inc_svcs_rx_adata;
static ble_gattc_rx_complete_fn ble_gattc_find_inc_svcs_rx_complete;
static ble_gattc_rx_attr_fn ble_gattc_find_inc_svcs_rx_read_rsp;
static ble_gattc_rx_adata_fn ble_gattc_disc_all_chrs_rx_adata;
static ble_gattc_rx_complete_fn ble_gattc_disc_all_chrs_rx_complete;
static ble_gattc_rx_adata_fn ble_gattc_disc_chr_uuid_rx_adata;
static ble_gattc_rx_complete_fn ble_gattc_disc_chr_uuid_rx_complete;
static ble_gattc_rx_attr_fn ble_gattc_read_rx_read_rsp;
static ble_gattc_rx_attr_fn ble_gattc_read_long_rx_read_rsp;
static ble_gattc_rx_adata_fn ble_gattc_read_uuid_rx_adata;
static ble_gattc_rx_complete_fn ble_gattc_read_uuid_rx_complete;
static ble_gattc_rx_prep_fn ble_gattc_write_long_rx_prep;
static ble_gattc_rx_exec_fn ble_gattc_write_long_rx_exec;
static ble_gattc_rx_prep_fn ble_gattc_write_reliable_rx_prep;
static ble_gattc_rx_exec_fn ble_gattc_write_reliable_rx_exec;

static const struct ble_gattc_rx_adata_entry {
    uint8_t op;
    ble_gattc_rx_adata_fn *cb;
} ble_gattc_rx_read_type_elem_entries[] = {
    { BLE_GATT_OP_FIND_INC_SVCS,    ble_gattc_find_inc_svcs_rx_adata },
    { BLE_GATT_OP_DISC_ALL_CHRS,    ble_gattc_disc_all_chrs_rx_adata },
    { BLE_GATT_OP_DISC_CHR_UUID,    ble_gattc_disc_chr_uuid_rx_adata },
    { BLE_GATT_OP_READ_UUID,        ble_gattc_read_uuid_rx_adata },
};

static const struct ble_gattc_rx_complete_entry {
    uint8_t op;
    ble_gattc_rx_complete_fn *cb;
} ble_gattc_rx_read_type_complete_entries[] = {
    { BLE_GATT_OP_FIND_INC_SVCS,    ble_gattc_find_inc_svcs_rx_complete },
    { BLE_GATT_OP_DISC_ALL_CHRS,    ble_gattc_disc_all_chrs_rx_complete },
    { BLE_GATT_OP_DISC_CHR_UUID,    ble_gattc_disc_chr_uuid_rx_complete },
    { BLE_GATT_OP_READ_UUID,        ble_gattc_read_uuid_rx_complete },
};

static const struct ble_gattc_rx_attr_entry {
    uint8_t op;
    ble_gattc_rx_attr_fn *cb;
} ble_gattc_rx_read_rsp_entries[] = {
    { BLE_GATT_OP_READ,             ble_gattc_read_rx_read_rsp },
    { BLE_GATT_OP_READ_LONG,        ble_gattc_read_long_rx_read_rsp },
    { BLE_GATT_OP_FIND_INC_SVCS,    ble_gattc_find_inc_svcs_rx_read_rsp },
};

static const struct ble_gattc_rx_prep_entry {
    uint8_t op;
    ble_gattc_rx_prep_fn *cb;
} ble_gattc_rx_prep_entries[] = {
    { BLE_GATT_OP_WRITE_LONG,       ble_gattc_write_long_rx_prep },
    { BLE_GATT_OP_WRITE_RELIABLE,   ble_gattc_write_reliable_rx_prep },
};

static const struct ble_gattc_rx_exec_entry {
    uint8_t op;
    ble_gattc_rx_exec_fn *cb;
} ble_gattc_rx_exec_entries[] = {
    { BLE_GATT_OP_WRITE_LONG,       ble_gattc_write_long_rx_exec },
    { BLE_GATT_OP_WRITE_RELIABLE,   ble_gattc_write_reliable_rx_exec },
};

/* Maintains the list of active GATT client procedures. */
static void *ble_gattc_proc_mem;
static struct os_mempool ble_gattc_proc_pool;
static struct ble_gattc_proc_list ble_gattc_procs;

/* Statistics. */
STATS_SECT_DECL(ble_gattc_stats) ble_gattc_stats;
STATS_NAME_START(ble_gattc_stats)
    STATS_NAME(ble_gattc_stats, mtu)
    STATS_NAME(ble_gattc_stats, mtu_fail)
    STATS_NAME(ble_gattc_stats, disc_all_svcs)
    STATS_NAME(ble_gattc_stats, disc_all_svcs_fail)
    STATS_NAME(ble_gattc_stats, disc_svc_uuid)
    STATS_NAME(ble_gattc_stats, disc_svc_uuid_fail)
    STATS_NAME(ble_gattc_stats, find_inc_svcs)
    STATS_NAME(ble_gattc_stats, find_inc_svcs_fail)
    STATS_NAME(ble_gattc_stats, disc_all_chrs)
    STATS_NAME(ble_gattc_stats, disc_all_chrs_fail)
    STATS_NAME(ble_gattc_stats, disc_chrs_uuid)
    STATS_NAME(ble_gattc_stats, disc_chrs_uuid_fail)
    STATS_NAME(ble_gattc_stats, disc_all_dscs)
    STATS_NAME(ble_gattc_stats, disc_all_dscs_fail)
    STATS_NAME(ble_gattc_stats, read)
    STATS_NAME(ble_gattc_stats, read_fail)
    STATS_NAME(ble_gattc_stats, read_uuid)
    STATS_NAME(ble_gattc_stats, read_uuid_fail)
    STATS_NAME(ble_gattc_stats, read_long)
    STATS_NAME(ble_gattc_stats, read_long_fail)
    STATS_NAME(ble_gattc_stats, read_mult)
    STATS_NAME(ble_gattc_stats, read_mult_fail)
    STATS_NAME(ble_gattc_stats, write_no_rsp)
    STATS_NAME(ble_gattc_stats, write_no_rsp_fail)
    STATS_NAME(ble_gattc_stats, write)
    STATS_NAME(ble_gattc_stats, write_fail)
    STATS_NAME(ble_gattc_stats, write_long)
    STATS_NAME(ble_gattc_stats, write_long_fail)
    STATS_NAME(ble_gattc_stats, write_reliable)
    STATS_NAME(ble_gattc_stats, write_reliable_fail)
    STATS_NAME(ble_gattc_stats, notify)
    STATS_NAME(ble_gattc_stats, notify_fail)
    STATS_NAME(ble_gattc_stats, indicate)
    STATS_NAME(ble_gattc_stats, indicate_fail)
    STATS_NAME(ble_gattc_stats, proc_timeout)
STATS_NAME_END(ble_gattc_stats)

/*****************************************************************************
 * $debug                                                                    *
 *****************************************************************************/

static void
ble_gattc_dbg_assert_proc_not_inserted(struct ble_gattc_proc *proc)
{
#if BLE_HS_DEBUG
    struct ble_gattc_proc *cur;

    ble_hs_lock();

    STAILQ_FOREACH(cur, &ble_gattc_procs, next) {
        BLE_HS_DBG_ASSERT(cur != proc);
    }

    ble_hs_unlock();
#endif
}

/*****************************************************************************
 * $log                                                                      *
 *****************************************************************************/

static void
ble_gattc_log_proc_init(char *name)
{
    BLE_HS_LOG(INFO, "GATT procedure initiated: %s", name);
}

static void
ble_gattc_log_uuid(const void *uuid128)
{
    const uint8_t *u8p;

    u8p = uuid128;
    BLE_HS_LOG(INFO, "%02x%02x%02x%02x-%02x%02x-%02x%02x-"
                     "%02x%02x%02x%02x%02x%02x%02x%02x",
               u8p[15], u8p[14], u8p[13], u8p[12],
               u8p[11], u8p[10], u8p[9], u8p[8],
               u8p[7], u8p[6], u8p[5], u8p[4],
               u8p[3], u8p[2], u8p[1], u8p[0]);
}

static void
ble_gattc_log_disc_svc_uuid(struct ble_gattc_proc *proc)
{
    ble_gattc_log_proc_init("discover service by uuid; uuid=");
    ble_gattc_log_uuid(proc->disc_svc_uuid.service_uuid);
    BLE_HS_LOG(INFO, "\n");
}

static void
ble_gattc_log_find_inc_svcs(struct ble_gattc_proc *proc)
{
    ble_gattc_log_proc_init("find included services; ");
    BLE_HS_LOG(INFO, "start_handle=%d end_handle=%d\n",
               proc->find_inc_svcs.prev_handle + 1,
               proc->find_inc_svcs.end_handle);
}

static void
ble_gattc_log_disc_all_chrs(struct ble_gattc_proc *proc)
{
    ble_gattc_log_proc_init("discover all characteristics; ");
    BLE_HS_LOG(INFO, "start_handle=%d end_handle=%d\n",
               proc->disc_all_chrs.prev_handle + 1,
               proc->disc_all_chrs.end_handle);
}

static void
ble_gattc_log_disc_chr_uuid(struct ble_gattc_proc *proc)
{
    ble_gattc_log_proc_init("discover characteristics by uuid; ");
    BLE_HS_LOG(INFO, "start_handle=%d end_handle=%d uuid=",
               proc->disc_chr_uuid.prev_handle + 1,
               proc->disc_chr_uuid.end_handle);
    ble_gattc_log_uuid(proc->disc_chr_uuid.chr_uuid);
    BLE_HS_LOG(INFO, "\n");
}

static void
ble_gattc_log_disc_all_dscs(struct ble_gattc_proc *proc)
{
    ble_gattc_log_proc_init("discover all descriptors; ");
    BLE_HS_LOG(INFO, "chr_val_handle=%d end_handle=%d\n",
               proc->disc_all_dscs.chr_val_handle,
               proc->disc_all_dscs.end_handle);
}

static void
ble_gattc_log_read(uint16_t att_handle)
{
    ble_gattc_log_proc_init("read; ");
    BLE_HS_LOG(INFO, "att_handle=%d\n", att_handle);
}

static void
ble_gattc_log_read_uuid(uint16_t start_handle, uint16_t end_handle,
                        const uint8_t *uuid128)
{
    uint16_t uuid16;

    ble_gattc_log_proc_init("read by uuid; ");
    BLE_HS_LOG(INFO, "start_handle=%d end_handle=%d uuid=",
               start_handle, end_handle);

    uuid16 = ble_uuid_128_to_16(uuid128);
    if (uuid16 != 0) {
        BLE_HS_LOG(INFO, "0x%04x", uuid16);
    } else {
        ble_gattc_log_uuid(uuid128);
    }
    BLE_HS_LOG(INFO, "\n");
}

static void
ble_gattc_log_read_long(struct ble_gattc_proc *proc)
{
    ble_gattc_log_proc_init("read long; ");
    BLE_HS_LOG(INFO, "att_handle=%d\n", proc->read_long.handle);
}

static void
ble_gattc_log_read_mult(const uint16_t *handles, uint8_t num_handles)
{
    int i;

    ble_gattc_log_proc_init("read multiple; ");
    BLE_HS_LOG(INFO, "att_handles=");
    for (i = 0; i < num_handles; i++) {
        BLE_HS_LOG(INFO, "%s%d", i != 0 ? "," : "", handles[i]);
    }
    BLE_HS_LOG(INFO, "\n");
}

static void
ble_gattc_log_write(uint16_t att_handle, uint16_t len, int expecting_rsp)
{
    char *name;

    if (expecting_rsp) {
        name = "write; ";
    } else {
        name = "write no rsp; ";
    }

    ble_gattc_log_proc_init(name);
    BLE_HS_LOG(INFO, "att_handle=%d len=%d\n", att_handle, len);
}

static void
ble_gattc_log_write_long(struct ble_gattc_proc *proc)
{
    ble_gattc_log_proc_init("write long; ");
    BLE_HS_LOG(INFO, "att_handle=%d len=%d\n",
               proc->write_long.attr.handle,
               OS_MBUF_PKTLEN(proc->write_long.attr.om));
}

static void
ble_gattc_log_write_reliable(struct ble_gattc_proc *proc)
{
    int i;

    ble_gattc_log_proc_init("write reliable; ");
    BLE_HS_LOG(INFO, "att_handles=");
    for (i = 0; i < proc->write_reliable.num_attrs; i++) {
        BLE_HS_LOG(INFO, "%s%d", i != 0 ? "," : "",
                   proc->write_reliable.attrs[i].handle);
    }
    BLE_HS_LOG(INFO, "\n");
}

static void
ble_gattc_log_notify(uint16_t att_handle)
{
    ble_gattc_log_proc_init("notify; ");
    BLE_HS_LOG(INFO, "att_handle=%d\n", att_handle);
}

static void
ble_gattc_log_indicate(uint16_t att_handle)
{
    ble_gattc_log_proc_init("indicate; ");
    BLE_HS_LOG(INFO, "att_handle=%d\n", att_handle);
}

/*****************************************************************************
 * $rx entry                                                                 *
 *****************************************************************************/

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
 */
static void
ble_gattc_proc_free(struct ble_gattc_proc *proc)
{
    int rc;
    int i;

    if (proc != NULL) {
        ble_gattc_dbg_assert_proc_not_inserted(proc);

        switch (proc->op) {
        case BLE_GATT_OP_WRITE_LONG:
            os_mbuf_free_chain(proc->write_long.attr.om);
            break;

        case BLE_GATT_OP_WRITE_RELIABLE:
            for (i = 0; i < proc->write_reliable.num_attrs; i++) {
                os_mbuf_free_chain(proc->write_reliable.attrs[i].om);
            }
            break;

        default:
            break;
        }

        rc = os_memblock_put(&ble_gattc_proc_pool, proc);
        BLE_HS_DBG_ASSERT_EVAL(rc == 0);
    }
}

static void
ble_gattc_proc_insert(struct ble_gattc_proc *proc)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    ble_hs_lock();
    STAILQ_INSERT_TAIL(&ble_gattc_procs, proc, next);
    ble_hs_unlock();
}

static void
ble_gattc_proc_set_timer(struct ble_gattc_proc *proc)
{
    proc->exp_os_ticks = os_time_get() + BLE_GATT_UNRESPONSIVE_TIMEOUT;
}

static void
ble_gattc_process_status(struct ble_gattc_proc *proc, int status)
{
    if (status == 0) {
        ble_gattc_proc_set_timer(proc);
        ble_gattc_proc_insert(proc);
    } else {
        ble_gattc_proc_free(proc);
    }
}

/*****************************************************************************
 * $util                                                                     *
 *****************************************************************************/

/**
 * Retrieves the dispatch entry with the specified op code.
 */
static ble_gattc_err_fn *
ble_gattc_err_dispatch_get(uint8_t op)
{
    BLE_HS_DBG_ASSERT(op < BLE_GATT_OP_MAX);
    return ble_gattc_err_dispatch[op];
}

/**
 * Tests if a proc entry fits the specified criteria.
 *
 * @param proc                  The procedure to test.
 * @param conn_handle           The connection handle to match against.
 * @param op                    The op code to match against, or
 *                                  BLE_GATT_OP_NONE to ignore this criterion.
 *
 * @return                      1 if the proc matches; 0 otherwise.
 */
static int
ble_gattc_proc_matches(struct ble_gattc_proc *proc, uint16_t conn_handle,
                       uint8_t op)
{
    if (conn_handle != proc->conn_handle) {
        return 0;
    }

    if (op != proc->op && op != BLE_GATT_OP_NONE) {
        return 0;
    }

    return 1;
}

static struct ble_gattc_proc *
ble_gattc_extract(uint16_t conn_handle, uint8_t op)
{
    struct ble_gattc_proc *proc;
    struct ble_gattc_proc *prev;

    /* Only the parent task is allowed to remove entries from the list. */
    BLE_HS_DBG_ASSERT(ble_hs_is_parent_task());

    ble_hs_lock();

    prev = NULL;
    STAILQ_FOREACH(proc, &ble_gattc_procs, next) {
        if (ble_gattc_proc_matches(proc, conn_handle, op)) {
            if (prev == NULL) {
                STAILQ_REMOVE_HEAD(&ble_gattc_procs, next);
            } else {
                STAILQ_REMOVE_AFTER(&ble_gattc_procs, prev, next);
            }
            break;
        }
        prev = proc;
    }

    ble_hs_unlock();

    return proc;
}

static int
ble_gattc_conn_op_matches(struct ble_gattc_proc *proc, uint16_t conn_handle,
                          uint8_t op)
{
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE &&
        conn_handle != proc->conn_handle) {

        return 0;
    }

    if (op != BLE_GATT_OP_NONE && op != proc->op) {
        return 0;
    }

    return 1;
}

static void
ble_gattc_extract_by_conn_op(uint16_t conn_handle, uint8_t op,
                             struct ble_gattc_proc_list *dst_list)
{
    struct ble_gattc_proc *proc;
    struct ble_gattc_proc *prev;
    struct ble_gattc_proc *next;

    /* Only the parent task is allowed to remove entries from the list. */
    BLE_HS_DBG_ASSERT(ble_hs_is_parent_task());

    STAILQ_INIT(dst_list);

    ble_hs_lock();

    prev = NULL;
    proc = STAILQ_FIRST(&ble_gattc_procs);
    while (proc != NULL) {
        next = STAILQ_NEXT(proc, next);

        if (ble_gattc_conn_op_matches(proc, conn_handle, op)) {
            if (prev == NULL) {
                STAILQ_REMOVE_HEAD(&ble_gattc_procs, next);
            } else {
                STAILQ_REMOVE_AFTER(&ble_gattc_procs, prev, next);
            }
            STAILQ_INSERT_TAIL(dst_list, proc, next);
        } else {
            prev = proc;
        }

        proc = next;
    }

    ble_hs_unlock();
}

static void
ble_gattc_extract_expired(struct ble_gattc_proc_list *dst_list)
{
    struct ble_gattc_proc *proc;
    struct ble_gattc_proc *prev;
    struct ble_gattc_proc *next;
    uint32_t now;
    int32_t time_diff;

    /* Only the parent task is allowed to remove entries from the list. */
    BLE_HS_DBG_ASSERT(ble_hs_is_parent_task());

    now = os_time_get();
    STAILQ_INIT(dst_list);

    ble_hs_lock();

    prev = NULL;
    proc = STAILQ_FIRST(&ble_gattc_procs);
    while (proc != NULL) {
        next = STAILQ_NEXT(proc, next);

        time_diff = now - proc->exp_os_ticks;
        if (time_diff >= 0) {
            if (prev == NULL) {
                STAILQ_REMOVE_HEAD(&ble_gattc_procs, next);
            } else {
                STAILQ_REMOVE_AFTER(&ble_gattc_procs, prev, next);
            }
            STAILQ_INSERT_TAIL(dst_list, proc, next);
        }

        prev = proc;
        proc = next;
    }

    ble_hs_unlock();
}

static struct ble_gattc_proc *
ble_gattc_extract_with_rx_entry(uint16_t conn_handle,
                                const void *rx_entries, int num_entries,
                                const void **out_rx_entry)
{
    struct ble_gattc_proc *proc;
    struct ble_gattc_proc *prev;
    const void *rx_entry;

    /* Only the parent task is allowed to remove entries from the list. */
    BLE_HS_DBG_ASSERT(ble_hs_is_parent_task());

    ble_hs_lock();

    prev = NULL;
    STAILQ_FOREACH(proc, &ble_gattc_procs, next) {
        if (proc->conn_handle == conn_handle) {
            rx_entry = ble_gattc_rx_entry_find(proc->op, rx_entries,
                                               num_entries);
            if (rx_entry != NULL) {
                if (prev == NULL) {
                    STAILQ_REMOVE_HEAD(&ble_gattc_procs, next);
                } else {
                    STAILQ_REMOVE_AFTER(&ble_gattc_procs, prev, next);
                }

                *out_rx_entry = rx_entry;
                break;
            }
        }

        prev = proc;
    }

    ble_hs_unlock();

    return proc;
}

/**
 * Searches the main proc list for an entry whose connection handle and op code
 * match those specified.  If a matching entry is found, it is removed from the
 * list and returned.
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
    ble_gattc_extract_with_rx_entry(                                          \
        (conn_handle), (rx_entries),                                          \
        sizeof (rx_entries) / sizeof (rx_entries)[0],                         \
        (const void **)(out_rx_entry))


/**
 * Causes all GATT procedures matching the specified criteria to fail with the
 * specified status code.
 */
static void
ble_gattc_fail_procs(uint16_t conn_handle, uint8_t op, int status)
{
    struct ble_gattc_proc_list temp_list;
    struct ble_gattc_proc *proc;
    ble_gattc_err_fn *err_cb;

    /* Remove all procs with the specified conn handle-op-pair and insert them
     * into the temporary list.
     */
    ble_gattc_extract_by_conn_op(conn_handle, op, &temp_list);

    /* Notify application of failed procedures and free the corresponding proc
     * entries.
     */
    while ((proc = STAILQ_FIRST(&temp_list)) != NULL) {
        err_cb = ble_gattc_err_dispatch_get(proc->op);
        err_cb(proc, status, 0);

        STAILQ_REMOVE_HEAD(&temp_list, next);
        ble_gattc_proc_free(proc);
    }
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
ble_gattc_heartbeat(void)
{
    struct ble_gattc_proc_list exp_list;
    struct ble_gattc_proc *proc;

    /* Remove timed-out procedures from the main list and insert them into a
     * temporary list.  For any stalled procedures, set their pending bit so
     * they can be retried.
     */
    ble_gattc_extract_expired(&exp_list);

    /* Terminate the connection associated with each timed-out procedure. */
    while ((proc = STAILQ_FIRST(&exp_list)) != NULL) {
        STATS_INC(ble_gattc_stats, proc_timeout);
        ble_gap_terminate(proc->conn_handle, BLE_ERR_REM_USER_CONN_TERM);

        STAILQ_REMOVE_HEAD(&exp_list, next);
        ble_gattc_proc_free(proc);
    }

    return BLE_HS_FOREVER;
}

/**
 * Returns a pointer to a GATT error object with the specified fields.  The
 * returned object is statically allocated, so this function is not reentrant.
 * This function should only ever be called by the ble_hs task.
 */
static struct ble_gatt_error *
ble_gattc_error(int status, uint16_t att_handle)
{
    static struct ble_gatt_error error;

    /* For consistency, always indicate a handle of 0 on success. */
    if (status == 0 || status == BLE_HS_EDONE) {
        att_handle = 0;
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
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_mtu_cb(struct ble_gattc_proc *proc, int status, uint16_t att_handle,
                 uint16_t mtu)
{
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0 && status != BLE_HS_EDONE) {
        STATS_INC(ble_gattc_stats, mtu_fail);
    }

    if (proc->mtu.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->mtu.cb(proc->conn_handle,
                          ble_gattc_error(status, att_handle),
                          mtu, proc->mtu.cb_arg);
    }

    return rc;
}

/**
 * Handles an incoming ATT error response for the specified mtu-exchange proc.
 */
static void
ble_gattc_mtu_err(struct ble_gattc_proc *proc, int status, uint16_t att_handle)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);
    ble_gattc_mtu_cb(proc, status, att_handle, 0);
}

/**
 * Initiates GATT procedure: Exchange MTU.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_exchange_mtu(uint16_t conn_handle, ble_gatt_mtu_fn *cb, void *cb_arg)
{
    struct ble_att_mtu_cmd req;
    struct ble_gattc_proc *proc;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    STATS_INC(ble_gattc_stats, mtu);

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_GATT_OP_MTU;
    proc->conn_handle = conn_handle;
    proc->mtu.cb = cb;
    proc->mtu.cb_arg = cb_arg;

    ble_gattc_log_proc_init("exchange mtu\n");

    ble_hs_lock();
    rc = ble_att_conn_chan_find(proc->conn_handle, &conn, &chan);
    if (rc == 0) {
        req.bamc_mtu = chan->blc_my_mtu;
    }
    ble_hs_unlock();

    if (rc != 0) {
        goto done;
    }

    rc = ble_att_clt_tx_mtu(proc->conn_handle, &req);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, mtu_fail);
    }

    ble_gattc_process_status(proc, rc);
    return rc;
}

/*****************************************************************************
 * $discover all services                                                    *
 *****************************************************************************/

/**
 * Calls a discover-all-services proc's callback with the specified parameters.
 * If the proc has no callback, this function is a no-op.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_disc_all_svcs_cb(struct ble_gattc_proc *proc,
                           uint16_t status, uint16_t att_handle,
                           struct ble_gatt_svc *service)
{
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());
    BLE_HS_DBG_ASSERT(service != NULL || status != 0);
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0 && status != BLE_HS_EDONE) {
        STATS_INC(ble_gattc_stats, disc_all_svcs_fail);
    }

    if (proc->disc_all_svcs.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->disc_all_svcs.cb(proc->conn_handle,
                                    ble_gattc_error(status, att_handle),
                                    service, proc->disc_all_svcs.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified discover-all-services proc.
 */
static int
ble_gattc_disc_all_svcs_go(struct ble_gattc_proc *proc, int cb_on_err)
{
    struct ble_att_read_group_type_req req;
    uint8_t uuid128[16];
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    rc = ble_uuid_16_to_128(BLE_ATT_UUID_PRIMARY_SERVICE, uuid128);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

    req.bagq_start_handle = proc->disc_all_svcs.prev_handle + 1;
    req.bagq_end_handle = 0xffff;
    rc = ble_att_clt_tx_read_group_type(proc->conn_handle, &req, uuid128);

    if (rc != 0) {
        if (cb_on_err) {
            ble_gattc_disc_all_svcs_cb(proc, rc, 0, NULL);
        }
        return rc;
    }

    return 0;
}

/**
 * Handles an incoming ATT error response for the specified
 * discover-all-services proc.
 */
static void
ble_gattc_disc_all_svcs_err(struct ble_gattc_proc *proc, int status,
                            uint16_t att_handle)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Discovery is complete. */
        status = BLE_HS_EDONE;
    }

    ble_gattc_disc_all_svcs_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming attribute data entry from a read-group-type response for
 * the specified discover-all-services proc.
 */
static int
ble_gattc_disc_all_svcs_rx_adata(struct ble_gattc_proc *proc,
                                 struct ble_att_read_group_type_adata *adata)
{
    struct ble_gatt_svc service;
    uint16_t uuid16;
    int cbrc;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

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
        rc = BLE_HS_EBADDATA;
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
    if (rc != 0 || cbrc != 0) {
        return BLE_HS_EDONE;
    } else {
        return 0;
    }
}

/**
 * Handles a notification that an incoming read-group-type response has been
 * fully processed.
 */
static int
ble_gattc_disc_all_svcs_rx_complete(struct ble_gattc_proc *proc, int status)
{
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0) {
        ble_gattc_disc_all_svcs_cb(proc, status, 0, NULL);
        return BLE_HS_EDONE;
    }

    if (proc->disc_all_svcs.prev_handle == 0xffff) {
        /* Service discovery complete. */
        ble_gattc_disc_all_svcs_cb(proc, BLE_HS_EDONE, 0, NULL);
        return BLE_HS_EDONE;
    }

    /* Send follow-up request. */
    rc = ble_gattc_disc_all_svcs_go(proc, 1);
    if (rc != 0) {
        return BLE_HS_EDONE;
    }

    return 0;
}

/**
 * Initiates GATT procedure: Discover All Primary Services.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 */
int
ble_gattc_disc_all_svcs(uint16_t conn_handle, ble_gatt_disc_svc_fn *cb,
                        void *cb_arg)
{
#if !NIMBLE_OPT(GATT_DISC_ALL_SVCS)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    STATS_INC(ble_gattc_stats, disc_all_svcs);

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_GATT_OP_DISC_ALL_SVCS;
    proc->conn_handle = conn_handle;
    proc->disc_all_svcs.prev_handle = 0x0000;
    proc->disc_all_svcs.cb = cb;
    proc->disc_all_svcs.cb_arg = cb_arg;

    ble_gattc_log_proc_init("discover all services\n");

    rc = ble_gattc_disc_all_svcs_go(proc, 0);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, disc_all_svcs_fail);
    }

    ble_gattc_process_status(proc, rc);
    return rc;
}

/*****************************************************************************
 * $discover service by uuid                                                 *
 *****************************************************************************/

/**
 * Calls a discover-service-by-uuid proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_disc_svc_uuid_cb(struct ble_gattc_proc *proc, int status,
                           uint16_t att_handle,
                           struct ble_gatt_svc *service)
{
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());
    BLE_HS_DBG_ASSERT(service != NULL || status != 0);
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0 && status != BLE_HS_EDONE) {
        STATS_INC(ble_gattc_stats, disc_svc_uuid_fail);
    }

    if (proc->disc_svc_uuid.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->disc_svc_uuid.cb(proc->conn_handle,
                                    ble_gattc_error(status, att_handle),
                                    service, proc->disc_svc_uuid.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified discover-service-by-uuid proc.
 */
static int
ble_gattc_disc_svc_uuid_go(struct ble_gattc_proc *proc, int cb_on_err)
{
    struct ble_att_find_type_value_req req;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    req.bavq_start_handle = proc->disc_svc_uuid.prev_handle + 1;
    req.bavq_end_handle = 0xffff;
    req.bavq_attr_type = BLE_ATT_UUID_PRIMARY_SERVICE;

    rc = ble_att_clt_tx_find_type_value(proc->conn_handle, &req,
                                        proc->disc_svc_uuid.service_uuid, 16);
    if (rc != 0) {
        if (cb_on_err) {
            ble_gattc_disc_svc_uuid_cb(proc, rc, 0, NULL);
        }
        return rc;
    }

    return 0;
}

/**
 * Handles an incoming ATT error response for the specified
 * discover-service-by-uuid proc.
 */
static void
ble_gattc_disc_svc_uuid_err(struct ble_gattc_proc *proc, int status,
                            uint16_t att_handle)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Discovery is complete. */
        status = BLE_HS_EDONE;
    }

    ble_gattc_disc_svc_uuid_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming "handles info" entry from a find-type-value response for
 * the specified discover-service-by-uuid proc.
 */
static int
ble_gattc_disc_svc_uuid_rx_hinfo(struct ble_gattc_proc *proc,
                                 struct ble_att_find_type_value_hinfo *hinfo)
{
    struct ble_gatt_svc service;
    int cbrc;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

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
    if (rc != 0 || cbrc != 0) {
        return BLE_HS_EDONE;
    } else {
        return 0;
    }
}

/**
 * Handles a notification that a find-type-value response has been fully
 * processed for the specified discover-service-by-uuid proc.
 */
static int
ble_gattc_disc_svc_uuid_rx_complete(struct ble_gattc_proc *proc, int status)
{
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0) {
        ble_gattc_disc_svc_uuid_cb(proc, status, 0, NULL);
        return BLE_HS_EDONE;
    }

    if (proc->disc_svc_uuid.prev_handle == 0xffff) {
        /* Service discovery complete. */
        ble_gattc_disc_svc_uuid_cb(proc, BLE_HS_EDONE, 0, NULL);
        return BLE_HS_EDONE;
    }

    /* Send follow-up request. */
    rc = ble_gattc_disc_svc_uuid_go(proc, 1);
    if (rc != 0) {
        return BLE_HS_EDONE;
    }

    return 0;
}

/**
 * Initiates GATT procedure: Discover Primary Service by Service UUID.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param service_uuid128       The 128-bit UUID of the service to discover.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_disc_svc_by_uuid(uint16_t conn_handle, const void *svc_uuid128,
                           ble_gatt_disc_svc_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT(GATT_DISC_SVC_UUID)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    STATS_INC(ble_gattc_stats, disc_svc_uuid);

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_GATT_OP_DISC_SVC_UUID;
    proc->conn_handle = conn_handle;
    memcpy(proc->disc_svc_uuid.service_uuid, svc_uuid128, 16);
    proc->disc_svc_uuid.prev_handle = 0x0000;
    proc->disc_svc_uuid.cb = cb;
    proc->disc_svc_uuid.cb_arg = cb_arg;

    ble_gattc_log_disc_svc_uuid(proc);

    rc = ble_gattc_disc_svc_uuid_go(proc, 0);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, disc_svc_uuid_fail);
    }

    ble_gattc_process_status(proc, rc);
    return rc;
}

/*****************************************************************************
 * $find included svcs                                                       *
 *****************************************************************************/

/**
 * Calls a find-included-services proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_find_inc_svcs_cb(struct ble_gattc_proc *proc, int status,
                           uint16_t att_handle,
                           struct ble_gatt_svc *service)
{
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());
    BLE_HS_DBG_ASSERT(service != NULL || status != 0);
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0 && status != BLE_HS_EDONE) {
        STATS_INC(ble_gattc_stats, find_inc_svcs_fail);
    }

    if (proc->find_inc_svcs.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->find_inc_svcs.cb(proc->conn_handle,
                                    ble_gattc_error(status, att_handle),
                                    service, proc->find_inc_svcs.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified find-included-services proc.
 */
static int
ble_gattc_find_inc_svcs_go(struct ble_gattc_proc *proc, int cb_on_err)
{
    struct ble_att_read_type_req read_type_req;
    struct ble_att_read_req read_req;
    uint8_t uuid128[16];
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (proc->find_inc_svcs.cur_start == 0) {
        /* Find the next included service. */
        read_type_req.batq_start_handle =
            proc->find_inc_svcs.prev_handle + 1;
        read_type_req.batq_end_handle = proc->find_inc_svcs.end_handle;

        rc = ble_uuid_16_to_128(BLE_ATT_UUID_INCLUDE, uuid128);
        BLE_HS_DBG_ASSERT_EVAL(rc == 0);

        rc = ble_att_clt_tx_read_type(proc->conn_handle,
                                      &read_type_req, uuid128);
    } else {
        /* Read the UUID of the previously found service. */
        read_req.barq_handle = proc->find_inc_svcs.cur_start;
        rc = ble_att_clt_tx_read(proc->conn_handle, &read_req);
    }

    if (rc != 0) {
        if (cb_on_err) {
            ble_gattc_find_inc_svcs_cb(proc, rc, 0, NULL);
        }
        return rc;
    }

    return 0;
}

/**
 * Handles an incoming ATT error response for the specified
 * find-included-services proc.
 */
static void
ble_gattc_find_inc_svcs_err(struct ble_gattc_proc *proc, int status,
                            uint16_t att_handle)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (proc->find_inc_svcs.cur_start == 0 &&
        status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {

        /* Discovery is complete. */
        status = BLE_HS_EDONE;
    }

    ble_gattc_find_inc_svcs_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming read-response for the specified find-included-services
 * proc.
 */
static int
ble_gattc_find_inc_svcs_rx_read_rsp(struct ble_gattc_proc *proc, int status,
                                    struct os_mbuf **om)
{
    struct ble_gatt_svc service;
    uint16_t om_len;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    rc = ble_hs_mbuf_to_flat(*om, service.uuid128, 16, &om_len);
    os_mbuf_free_chain(*om);
    *om = NULL;

    if (rc != 0 || om_len != 16) {
        /* Invalid UUID. */
        rc = BLE_HS_EBADDATA;
        goto err;
    }

    if (proc->find_inc_svcs.cur_start == 0) {
        /* Unexpected read response; terminate procedure. */
        rc = BLE_HS_EBADDATA;
        goto err;
    }

    if (status != 0) {
        rc = status;
        goto err;
    }

    /* Report discovered service to application. */
    service.start_handle = proc->find_inc_svcs.cur_start;
    service.end_handle = proc->find_inc_svcs.cur_end;
    rc = ble_gattc_find_inc_svcs_cb(proc, 0, 0, &service);
    if (rc != 0) {
        /* Application has indicated that the procedure should be aborted. */
        return BLE_HS_EDONE;
    }

    /* Proceed to the next service. */
    proc->find_inc_svcs.cur_start = 0;
    proc->find_inc_svcs.cur_end = 0;
    rc = ble_gattc_find_inc_svcs_go(proc, 1);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    ble_gattc_find_inc_svcs_cb(proc, rc, 0, NULL);
    return BLE_HS_EDONE;
}

/**
 * Handles an incoming "attribute data" entry from a read-by-type response for
 * the specified find-included-services proc.
 */
static int
ble_gattc_find_inc_svcs_rx_adata(struct ble_gattc_proc *proc,
                                 struct ble_att_read_type_adata *adata)
{
    struct ble_gatt_svc service;
    uint16_t uuid16;
    int call_cb;
    int cbrc;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

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
        rc = BLE_HS_EBADDATA;
        goto done;
    }

    rc = 0;

done:
    if (call_cb) {
        cbrc = ble_gattc_find_inc_svcs_cb(proc, 0, 0, &service);
        if (rc != 0) {
            rc = cbrc;
        }
    } else {
        cbrc = 0;
    }

    if (rc != 0 || cbrc != 0) {
        return BLE_HS_EDONE;
    } else {
        return 0;
    }
}

/**
 * Handles a notification that a read-by-type response has been fully
 * processed for the specified find-included-services proc.
 */
static int
ble_gattc_find_inc_svcs_rx_complete(struct ble_gattc_proc *proc, int status)
{
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0) {
        ble_gattc_find_inc_svcs_cb(proc, status, 0, NULL);
        return BLE_HS_EDONE;
    }

    if (proc->find_inc_svcs.prev_handle == 0xffff) {
        /* Procedure complete. */
        ble_gattc_find_inc_svcs_cb(proc, BLE_HS_EDONE, 0, NULL);
        return BLE_HS_EDONE;
    }

    /* Send follow-up request. */
    rc = ble_gattc_find_inc_svcs_go(proc, 1);
    if (rc != 0) {
        return BLE_HS_EDONE;
    }
    return 0;
}

/**
 * Initiates GATT procedure: Find Included Services.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param start_handle          The handle to begin the search at (generally
 *                                  the service definition handle).
 * @param end_handle            The handle to end the search at (generally the
 *                                  last handle in the service).
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_find_inc_svcs(uint16_t conn_handle, uint16_t start_handle,
                        uint16_t end_handle,
                        ble_gatt_disc_svc_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT(GATT_FIND_INC_SVCS)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    STATS_INC(ble_gattc_stats, find_inc_svcs);

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_GATT_OP_FIND_INC_SVCS;
    proc->conn_handle = conn_handle;
    proc->find_inc_svcs.prev_handle = start_handle - 1;
    proc->find_inc_svcs.end_handle = end_handle;
    proc->find_inc_svcs.cb = cb;
    proc->find_inc_svcs.cb_arg = cb_arg;

    ble_gattc_log_find_inc_svcs(proc);

    rc = ble_gattc_find_inc_svcs_go(proc, 0);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, find_inc_svcs_fail);
    }

    ble_gattc_process_status(proc, rc);
    return rc;
}

/*****************************************************************************
 * $discover all characteristics                                             *
 *****************************************************************************/

/**
 * Calls a discover-all-characteristics proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_disc_all_chrs_cb(struct ble_gattc_proc *proc, int status,
                           uint16_t att_handle, struct ble_gatt_chr *chr)
{
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());
    BLE_HS_DBG_ASSERT(chr != NULL || status != 0);
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0 && status != BLE_HS_EDONE) {
        STATS_INC(ble_gattc_stats, disc_all_chrs_fail);
    }

    if (proc->disc_all_chrs.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->disc_all_chrs.cb(proc->conn_handle,
                                    ble_gattc_error(status, att_handle), chr,
                                    proc->disc_all_chrs.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified discover-all-characteristics
 * proc.
 */
static int
ble_gattc_disc_all_chrs_go(struct ble_gattc_proc *proc, int cb_on_err)
{
    struct ble_att_read_type_req req;
    uint8_t uuid128[16];
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    rc = ble_uuid_16_to_128(BLE_ATT_UUID_CHARACTERISTIC, uuid128);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

    req.batq_start_handle = proc->disc_all_chrs.prev_handle + 1;
    req.batq_end_handle = proc->disc_all_chrs.end_handle;

    rc = ble_att_clt_tx_read_type(proc->conn_handle, &req, uuid128);

    if (rc != 0) {
        if (cb_on_err) {
            ble_gattc_disc_all_chrs_cb(proc, rc, 0, NULL);
        }
        return rc;
    }

    return 0;
}

/**
 * Handles an incoming ATT error response for the specified
 * discover-all-characteristics proc.
 */
static void
ble_gattc_disc_all_chrs_err(struct ble_gattc_proc *proc, int status,
                            uint16_t att_handle)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Discovery is complete. */
        status = BLE_HS_EDONE;
    }

    ble_gattc_disc_all_chrs_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming "attribute data" entry from a read-by-type response for
 * the specified discover-all-characteristics proc.
 */
static int
ble_gattc_disc_all_chrs_rx_adata(struct ble_gattc_proc *proc,
                                 struct ble_att_read_type_adata *adata)
{
    struct ble_gatt_chr chr;
    uint16_t uuid16;
    int cbrc;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    memset(&chr, 0, sizeof chr);
    chr.def_handle = adata->att_handle;

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
    chr.val_handle = le16toh(adata->value + 1);

    if (adata->att_handle <= proc->disc_all_chrs.prev_handle) {
        /* Peer sent characteristics out of order; terminate procedure. */
        rc = BLE_HS_EBADDATA;
        goto done;
    }
    proc->disc_all_chrs.prev_handle = adata->att_handle;

    rc = 0;

done:
    cbrc = ble_gattc_disc_all_chrs_cb(proc, rc, 0, &chr);
    if (rc != 0 || cbrc != 0) {
        return BLE_HS_EDONE;
    } else {
        return 0;
    }
}

/**
 * Handles a notification that a read-by-type response has been fully
 * processed for the specified discover-all-characteristics proc.
 */
static int
ble_gattc_disc_all_chrs_rx_complete(struct ble_gattc_proc *proc, int status)
{
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0) {
        ble_gattc_disc_all_chrs_cb(proc, status, 0, NULL);
        return BLE_HS_EDONE;
    }

    if (proc->disc_all_chrs.prev_handle == proc->disc_all_chrs.end_handle) {
        /* Characteristic discovery complete. */
        ble_gattc_disc_all_chrs_cb(proc, BLE_HS_EDONE, 0, NULL);
        return BLE_HS_EDONE;
    }

    /* Send follow-up request. */
    rc = ble_gattc_disc_all_chrs_go(proc, 1);
    if (rc != 0) {
        return BLE_HS_EDONE;
    }
    return 0;
}

/**
 * Initiates GATT procedure: Discover All Characteristics of a Service.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param start_handle          The handle to begin the search at (generally
 *                                  the service definition handle).
 * @param end_handle            The handle to end the search at (generally the
 *                                  last handle in the service).
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_disc_all_chrs(uint16_t conn_handle, uint16_t start_handle,
                        uint16_t end_handle, ble_gatt_chr_fn *cb,
                        void *cb_arg)
{
#if !NIMBLE_OPT(GATT_DISC_ALL_CHRS)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    STATS_INC(ble_gattc_stats, disc_all_chrs);

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_GATT_OP_DISC_ALL_CHRS;
    proc->conn_handle = conn_handle;
    proc->disc_all_chrs.prev_handle = start_handle - 1;
    proc->disc_all_chrs.end_handle = end_handle;
    proc->disc_all_chrs.cb = cb;
    proc->disc_all_chrs.cb_arg = cb_arg;

    ble_gattc_log_disc_all_chrs(proc);

    rc = ble_gattc_disc_all_chrs_go(proc, 0);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, disc_all_chrs_fail);
    }

    ble_gattc_process_status(proc, rc);
    return rc;
}

/*****************************************************************************
 * $discover characteristic by uuid                                          *
 *****************************************************************************/

/**
 * Calls a discover-characteristic-by-uuid proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_disc_chr_uuid_cb(struct ble_gattc_proc *proc, int status,
                           uint16_t att_handle, struct ble_gatt_chr *chr)
{
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());
    BLE_HS_DBG_ASSERT(chr != NULL || status != 0);
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0 && status != BLE_HS_EDONE) {
        STATS_INC(ble_gattc_stats, disc_chrs_uuid_fail);
    }

    if (proc->disc_chr_uuid.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->disc_chr_uuid.cb(proc->conn_handle,
                                    ble_gattc_error(status, att_handle), chr,
                                    proc->disc_chr_uuid.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified
 * discover-characteristic-by-uuid proc.
 */
static int
ble_gattc_disc_chr_uuid_go(struct ble_gattc_proc *proc, int cb_on_err)
{
    struct ble_att_read_type_req req;
    uint8_t uuid128[16];
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    rc = ble_uuid_16_to_128(BLE_ATT_UUID_CHARACTERISTIC, uuid128);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

    req.batq_start_handle = proc->disc_chr_uuid.prev_handle + 1;
    req.batq_end_handle = proc->disc_chr_uuid.end_handle;

    rc = ble_att_clt_tx_read_type(proc->conn_handle, &req, uuid128);

    if (rc != 0) {
        if (cb_on_err) {
            ble_gattc_disc_chr_uuid_cb(proc, rc, 0, NULL);
        }
        return rc;
    }

    return 0;
}

/**
 * Handles an incoming ATT error response for the specified
 * discover-characteristic-by-uuid proc.
 */
static void
ble_gattc_disc_chr_uuid_err(struct ble_gattc_proc *proc, int status,
                            uint16_t att_handle)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Discovery is complete. */
        status = BLE_HS_EDONE;
    }

    ble_gattc_disc_chr_uuid_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming "attribute data" entry from a read-by-type response for
 * the specified discover-characteristics-by-uuid proc.
 */
static int
ble_gattc_disc_chr_uuid_rx_adata(struct ble_gattc_proc *proc,
                                 struct ble_att_read_type_adata *adata)
{
    struct ble_gatt_chr chr;
    uint16_t uuid16;
    int cbrc;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    memset(&chr, 0, sizeof chr);
    chr.def_handle = adata->att_handle;

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
    chr.val_handle = le16toh(adata->value + 1);

    if (adata->att_handle <= proc->disc_chr_uuid.prev_handle) {
        /* Peer sent characteristics out of order; terminate procedure. */
        rc = BLE_HS_EBADDATA;
        goto done;
    }

    proc->disc_chr_uuid.prev_handle = adata->att_handle;

    rc = 0;

done:
    if (rc != 0) {
        /* Failure. */
        cbrc = ble_gattc_disc_chr_uuid_cb(proc, rc, 0, NULL);
    } else if (memcmp(chr.uuid128, proc->disc_chr_uuid.chr_uuid, 16) == 0) {
        /* Requested characteristic discovered. */
        cbrc = ble_gattc_disc_chr_uuid_cb(proc, 0, 0, &chr);
    } else {
        /* Uninteresting characteristic; ignore. */
        cbrc = 0;
    }

    if (rc != 0 || cbrc != 0) {
        return BLE_HS_EDONE;
    } else {
        return 0;
    }
}

/**
 * Handles a notification that a read-by-type response has been fully
 * processed for the specified discover-characteristics-by-uuid proc.
 */
static int
ble_gattc_disc_chr_uuid_rx_complete(struct ble_gattc_proc *proc, int status)
{
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0) {
        ble_gattc_disc_chr_uuid_cb(proc, status, 0, NULL);
        return BLE_HS_EDONE;
    }

    if (proc->disc_chr_uuid.prev_handle == proc->disc_chr_uuid.end_handle) {
        /* Characteristic discovery complete. */
        ble_gattc_disc_chr_uuid_cb(proc, BLE_HS_EDONE, 0, NULL);
        return BLE_HS_EDONE;
    }

    /* Send follow-up request. */
    rc = ble_gattc_disc_chr_uuid_go(proc, 1);
    if (rc != 0) {
        return BLE_HS_EDONE;
    }
    return 0;
}

/**
 * Initiates GATT procedure: Discover Characteristics by UUID.
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
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_disc_chrs_by_uuid(uint16_t conn_handle, uint16_t start_handle,
                            uint16_t end_handle, const void *uuid128,
                            ble_gatt_chr_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT(GATT_DISC_CHR_UUID)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    STATS_INC(ble_gattc_stats, disc_chrs_uuid);

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_GATT_OP_DISC_CHR_UUID;
    proc->conn_handle = conn_handle;
    memcpy(proc->disc_chr_uuid.chr_uuid, uuid128, 16);
    proc->disc_chr_uuid.prev_handle = start_handle - 1;
    proc->disc_chr_uuid.end_handle = end_handle;
    proc->disc_chr_uuid.cb = cb;
    proc->disc_chr_uuid.cb_arg = cb_arg;

    ble_gattc_log_disc_chr_uuid(proc);

    rc = ble_gattc_disc_chr_uuid_go(proc, 0);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, disc_chrs_uuid_fail);
    }

    ble_gattc_process_status(proc, rc);
    return rc;
}

/*****************************************************************************
 * $discover all characteristic descriptors                                  *
 *****************************************************************************/

/**
 * Calls a discover-all-descriptors proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_disc_all_dscs_cb(struct ble_gattc_proc *proc, int status,
                           uint16_t att_handle, struct ble_gatt_dsc *dsc)
{
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());
    BLE_HS_DBG_ASSERT(dsc != NULL || status != 0);
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0 && status != BLE_HS_EDONE) {
        STATS_INC(ble_gattc_stats, disc_all_dscs_fail);
    }

    if (proc->disc_all_dscs.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->disc_all_dscs.cb(proc->conn_handle,
                                    ble_gattc_error(status, att_handle),
                                    proc->disc_all_dscs.chr_val_handle,
                                    dsc, proc->disc_all_dscs.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified discover-all-descriptors proc.
 */
static int
ble_gattc_disc_all_dscs_go(struct ble_gattc_proc *proc, int cb_on_err)
{
    struct ble_att_find_info_req req;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    req.bafq_start_handle = proc->disc_all_dscs.prev_handle + 1;
    req.bafq_end_handle = proc->disc_all_dscs.end_handle;

    rc = ble_att_clt_tx_find_info(proc->conn_handle, &req);
    if (rc != 0) {
        if (cb_on_err) {
            ble_gattc_disc_all_dscs_cb(proc, rc, 0, NULL);
        }
        return rc;
    }

    return 0;
}

/**
 * Handles an incoming ATT error response for the specified
 * discover-all-descriptors proc.
 */
static void
ble_gattc_disc_all_dscs_err(struct ble_gattc_proc *proc, int status,
                            uint16_t att_handle)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Discovery is complete. */
        status = BLE_HS_EDONE;
    }

    ble_gattc_disc_all_dscs_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming "information data" entry from a find-information
 * response for the specified discover-all-descriptors proc.
 */
static int
ble_gattc_disc_all_dscs_rx_idata(struct ble_gattc_proc *proc,
                                 struct ble_att_find_info_idata *idata)
{
    struct ble_gatt_dsc dsc;
    int cbrc;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

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
    if (rc != 0 || cbrc != 0) {
        return BLE_HS_EDONE;
    } else {
        return 0;
    }
}

/**
 * Handles a notification that a find-information response has been fully
 * processed for the specified discover-all-descriptors proc.
 */
static int
ble_gattc_disc_all_dscs_rx_complete(struct ble_gattc_proc *proc, int status)
{
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0) {
        ble_gattc_disc_all_dscs_cb(proc, status, 0, NULL);
        return BLE_HS_EDONE;
    }

    if (proc->disc_all_dscs.prev_handle == proc->disc_all_dscs.end_handle) {
        /* All descriptors discovered. */
        ble_gattc_disc_all_dscs_cb(proc, BLE_HS_EDONE, 0, NULL);
        return BLE_HS_EDONE;
    }

    /* Send follow-up request. */
    rc = ble_gattc_disc_all_dscs_go(proc, 1);
    if (rc != 0) {
        return BLE_HS_EDONE;
    }

    return 0;
}

/**
 * Initiates GATT procedure: Discover All Characteristic Descriptors.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param chr_val_handle        The handle of the characteristic value
 *                                  attribute.
 * @param chr_end_handle        The last handle in the characteristic
 *                                  definition.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_disc_all_dscs(uint16_t conn_handle, uint16_t chr_val_handle,
                        uint16_t chr_end_handle,
                        ble_gatt_dsc_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT(GATT_DISC_ALL_DSCS)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    STATS_INC(ble_gattc_stats, disc_all_dscs);

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_GATT_OP_DISC_ALL_DSCS;
    proc->conn_handle = conn_handle;
    proc->disc_all_dscs.chr_val_handle = chr_val_handle;
    proc->disc_all_dscs.prev_handle = chr_val_handle;
    proc->disc_all_dscs.end_handle = chr_end_handle;
    proc->disc_all_dscs.cb = cb;
    proc->disc_all_dscs.cb_arg = cb_arg;

    ble_gattc_log_disc_all_dscs(proc);

    rc = ble_gattc_disc_all_dscs_go(proc, 0);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, disc_all_dscs_fail);
    }

    ble_gattc_process_status(proc, rc);
    return rc;
}

/*****************************************************************************
 * $read                                                                     *
 *****************************************************************************/

/**
 * Calls a read-characteristic proc's callback with the specified parameters.
 * If the proc has no callback, this function is a no-op.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_read_cb(struct ble_gattc_proc *proc, int status,
                  uint16_t att_handle, struct ble_gatt_attr *attr)
{
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());
    BLE_HS_DBG_ASSERT(attr != NULL || status != 0);
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0 && status != BLE_HS_EDONE) {
        STATS_INC(ble_gattc_stats, read_fail);
    }

    if (proc->read.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->read.cb(proc->conn_handle,
                           ble_gattc_error(status, att_handle), attr,
                           proc->read.cb_arg);
    }

    return rc;
}

/**
 * Handles an incoming ATT error response for the specified
 * read-characteristic-value proc.
 */
static void
ble_gattc_read_err(struct ble_gattc_proc *proc, int status,
                   uint16_t att_handle)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);
    ble_gattc_read_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming read-response for the specified
 * read-characteristic-value proc.
 */
static int
ble_gattc_read_rx_read_rsp(struct ble_gattc_proc *proc, int status,
                           struct os_mbuf **om)
{
    struct ble_gatt_attr attr;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    attr.handle = proc->read.handle;
    attr.offset = 0;
    attr.om = *om;

    ble_gattc_read_cb(proc, status, 0, &attr);

    /* Indicate to the caller whether the application consumed the mbuf. */
    *om = attr.om;

    /* The read operation only has a single request / response exchange. */
    return BLE_HS_EDONE;
}

/**
 * Initiates GATT procedure: Read Characteristic Value.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to read.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_read(uint16_t conn_handle, uint16_t attr_handle,
               ble_gatt_attr_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT(GATT_READ)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    struct ble_att_read_req req;
    int rc;

    STATS_INC(ble_gattc_stats, read);

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_GATT_OP_READ;
    proc->conn_handle = conn_handle;
    proc->read.handle = attr_handle;
    proc->read.cb = cb;
    proc->read.cb_arg = cb_arg;

    ble_gattc_log_read(attr_handle);

    req.barq_handle = attr_handle;
    rc = ble_att_clt_tx_read(proc->conn_handle, &req);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, read_fail);
    }

    ble_gattc_process_status(proc, rc);
    return rc;
}

/*****************************************************************************
 * $read by uuid                                                             *
 *****************************************************************************/

/**
 * Calls a read-using-characteristic-uuid proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_read_uuid_cb(struct ble_gattc_proc *proc, int status,
                       uint16_t att_handle, struct ble_gatt_attr *attr)
{
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());
    BLE_HS_DBG_ASSERT(attr != NULL || status != 0);
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0 && status != BLE_HS_EDONE) {
        STATS_INC(ble_gattc_stats, read_uuid_fail);
    }

    if (proc->read_uuid.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->read_uuid.cb(proc->conn_handle,
                                ble_gattc_error(status, att_handle), attr,
                                proc->read_uuid.cb_arg);
    }

    return rc;
}

/**
 * Handles an incoming ATT error response for the specified
 * read-using-characteristic-uuid proc.
 */
static void
ble_gattc_read_uuid_err(struct ble_gattc_proc *proc, int status,
                        uint16_t att_handle)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Read is complete. */
        status = 0;
    }
    ble_gattc_read_uuid_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming "attribute data" entry from a read-by-type response for
 * the specified read-using-characteristic-uuid proc.
 */
static int
ble_gattc_read_uuid_rx_adata(struct ble_gattc_proc *proc,
                             struct ble_att_read_type_adata *adata)
{
    struct ble_gatt_attr attr;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    attr.handle = adata->att_handle;
    attr.offset = 0;
    attr.om = ble_hs_mbuf_from_flat(adata->value, adata->value_len);
    if (attr.om == NULL) {
        rc = BLE_HS_ENOMEM;
    } else {
        rc = 0;
    }

    rc = ble_gattc_read_uuid_cb(proc, rc, 0, &attr);

    /* Free the attribute mbuf if the application has not consumed it. */
    os_mbuf_free_chain(attr.om);

    if (rc != 0) {
        return BLE_HS_EDONE;
    }

    return 0;
}

/**
 * Handles a notification that a read-by-type response has been fully
 * processed for the specified read-using-characteristic-uuid proc.
 */
static int
ble_gattc_read_uuid_rx_complete(struct ble_gattc_proc *proc, int status)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0) {
        ble_gattc_read_uuid_cb(proc, status, 0, NULL);
        return BLE_HS_EDONE;
    }

    /* XXX: We may need to send a follow-up request to address the possibility
     * of multiple characteristics with identical UUIDs.
     */
    ble_gattc_read_uuid_cb(proc, BLE_HS_EDONE, 0, NULL);
    return BLE_HS_EDONE;
}

/**
 * Initiates GATT procedure: Read Using Characteristic UUID.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param start_handle          The first handle to search (generally the
 *                                  handle of the service definition).
 * @param end_handle            The last handle to search (generally the
 *                                  last handle in the service definition).
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_read_by_uuid(uint16_t conn_handle, uint16_t start_handle,
                       uint16_t end_handle, const void *uuid128,
                       ble_gatt_attr_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT(GATT_READ_UUID)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_att_read_type_req req;
    struct ble_gattc_proc *proc;
    int rc;

    STATS_INC(ble_gattc_stats, read_uuid);

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_GATT_OP_READ_UUID;
    proc->conn_handle = conn_handle;
    proc->read_uuid.cb = cb;
    proc->read_uuid.cb_arg = cb_arg;

    ble_gattc_log_read_uuid(start_handle, end_handle, uuid128);

    req.batq_start_handle = start_handle;
    req.batq_end_handle = end_handle;
    rc = ble_att_clt_tx_read_type(conn_handle, &req, uuid128);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, read_uuid_fail);
    }

    ble_gattc_process_status(proc, rc);
    return rc;
}

/*****************************************************************************
 * $read long                                                                *
 *****************************************************************************/

/**
 * Calls a read-long-characteristic proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_read_long_cb(struct ble_gattc_proc *proc, int status,
                       uint16_t att_handle, struct ble_gatt_attr *attr)
{
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());
    BLE_HS_DBG_ASSERT(attr != NULL || status != 0);
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0 && status != BLE_HS_EDONE) {
        STATS_INC(ble_gattc_stats, read_long_fail);
    }

    if (proc->read_long.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->read_long.cb(proc->conn_handle,
                                ble_gattc_error(status, att_handle), attr,
                                proc->read_long.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified read-long-characteristic proc.
 */
static int
ble_gattc_read_long_go(struct ble_gattc_proc *proc, int cb_on_err)
{
    struct ble_att_read_blob_req blob_req;
    struct ble_att_read_req read_req;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (proc->read_long.offset == 0) {
        read_req.barq_handle = proc->read_long.handle;
        rc = ble_att_clt_tx_read(proc->conn_handle, &read_req);
    } else {
        blob_req.babq_handle = proc->read_long.handle;
        blob_req.babq_offset = proc->read_long.offset;
        rc = ble_att_clt_tx_read_blob(proc->conn_handle, &blob_req);
    }

    if (rc != 0) {
        if (cb_on_err) {
            ble_gattc_read_long_cb(proc, rc, 0, NULL);
        }
        return rc;
    }

    return 0;
}

/**
 * Handles an incoming ATT error response for the specified
 * read-long-characteristic proc.
 */
static void
ble_gattc_read_long_err(struct ble_gattc_proc *proc, int status,
                        uint16_t att_handle)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);
    ble_gattc_read_long_cb(proc, status, att_handle, NULL);
}

/**
 * Handles an incoming read-response for the specified
 * read-long-characteristic-values proc.
 */
static int
ble_gattc_read_long_rx_read_rsp(struct ble_gattc_proc *proc, int status,
                                struct os_mbuf **om)
{
    struct ble_gatt_attr attr;
    uint16_t data_len;
    uint16_t mtu;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    data_len = OS_MBUF_PKTLEN(*om);

    attr.handle = proc->read_long.handle;
    attr.offset = proc->read_long.offset;
    attr.om = *om;

    /* Report partial payload to application. */
    rc = ble_gattc_read_long_cb(proc, status, 0, &attr);

    /* Indicate to the caller whether the application consumed the mbuf. */
    *om = attr.om;

    if (rc != 0 || status != 0) {
        return BLE_HS_EDONE;
    }

    /* Determine if this is the end of the attribute value. */
    mtu = ble_att_mtu(proc->conn_handle);
    if (mtu == 0) {
        /* No longer connected. */
        return BLE_HS_EDONE;
    }

    if (data_len < mtu - 1) {
        /* Response shorter than maximum allowed; read complete. */
        ble_gattc_read_long_cb(proc, BLE_HS_EDONE, 0, NULL);
        return BLE_HS_EDONE;
    }

    /* Send follow-up request. */
    proc->read_long.offset += data_len;
    rc = ble_gattc_read_long_go(proc, 1);
    if (rc != 0) {
        return BLE_HS_EDONE;
    }

    return 0;
}

/**
 * Initiates GATT procedure: Read Long Characteristic Values.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param handle                The handle of the characteristic value to read.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_read_long(uint16_t conn_handle, uint16_t handle,
                    ble_gatt_attr_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT(GATT_READ_LONG)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    STATS_INC(ble_gattc_stats, read_long);

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_GATT_OP_READ_LONG;
    proc->conn_handle = conn_handle;
    proc->read_long.handle = handle;
    proc->read_long.offset = 0;
    proc->read_long.cb = cb;
    proc->read_long.cb_arg = cb_arg;

    ble_gattc_log_read_long(proc);

    rc = ble_gattc_read_long_go(proc, 0);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, read_long_fail);
    }

    ble_gattc_process_status(proc, rc);
    return rc;
}

/*****************************************************************************
 * $read multiple                                                            *
 *****************************************************************************/

/**
 * Calls a read-multiple-characteristics proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_read_mult_cb(struct ble_gattc_proc *proc, int status,
                       uint16_t att_handle, struct os_mbuf **om)
{
    struct ble_gatt_attr attr;
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());
    BLE_HS_DBG_ASSERT(om != NULL || status != 0);
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0 && status != BLE_HS_EDONE) {
        STATS_INC(ble_gattc_stats, read_mult_fail);
    }

    attr.handle = 0;
    attr.offset = 0;
    if (om == NULL) {
        attr.om = NULL;
    } else {
        attr.om = *om;
    }

    if (proc->read_mult.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->read_mult.cb(proc->conn_handle,
                                ble_gattc_error(status, att_handle), &attr,
                                proc->read_mult.cb_arg);
    }

    /* Indicate to the caller whether the application consumed the mbuf. */
    if (om != NULL) {
        *om = attr.om;
    }

    return rc;
}

/**
 * Handles an incoming ATT error response for the specified
 * read-multiple-characteristics proc.
 */
static void
ble_gattc_read_mult_err(struct ble_gattc_proc *proc, int status,
                        uint16_t att_handle)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);
    ble_gattc_read_mult_cb(proc, status, att_handle, NULL);
}

/**
 * Initiates GATT procedure: Read Multiple Characteristic Values.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param handles               An array of 16-bit attribute handles to read.
 * @param num_handles           The number of entries in the "handles" array.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_read_mult(uint16_t conn_handle, const uint16_t *handles,
                    uint8_t num_handles, ble_gatt_attr_fn *cb,
                    void *cb_arg)
{
#if !NIMBLE_OPT(GATT_READ_MULT)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    STATS_INC(ble_gattc_stats, read_mult);

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_GATT_OP_READ_MULT;
    proc->conn_handle = conn_handle;
    proc->read_mult.cb = cb;
    proc->read_mult.cb_arg = cb_arg;

    ble_gattc_log_read_mult(handles, num_handles);

    rc = ble_att_clt_tx_read_mult(conn_handle, handles, num_handles);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, read_mult_fail);
    }

    ble_gattc_process_status(proc, rc);
    return rc;
}

/*****************************************************************************
 * $write no response                                                        *
 *****************************************************************************/

/**
 * Initiates GATT procedure: Write Without Response.  This function consumes
 * the supplied mbuf regardless of the outcome.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to write
 *                                  to.
 * @param txom                  The value to write to the characteristic.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_write_no_rsp(uint16_t conn_handle, uint16_t attr_handle,
                       struct os_mbuf *txom)
{
#if !NIMBLE_OPT(GATT_WRITE_NO_RSP)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_att_write_req req;
    int rc;

    STATS_INC(ble_gattc_stats, write_no_rsp);

    ble_gattc_log_write(attr_handle, OS_MBUF_PKTLEN(txom), 0);

    req.bawq_handle = attr_handle;
    rc = ble_att_clt_tx_write_cmd(conn_handle, &req, txom);
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, write);
    }

    return rc;
}

/**
 * Initiates GATT procedure: Write Without Response.  This function consumes
 * the supplied mbuf regardless of the outcome.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to write
 *                                  to.
 * @param value                 The value to write to the characteristic.
 * @param value_len             The number of bytes to write.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_write_no_rsp_flat(uint16_t conn_handle, uint16_t attr_handle,
                            const void *data, uint16_t data_len)
{
    struct os_mbuf *om;
    int rc;

    om = ble_hs_mbuf_from_flat(data, data_len);
    if (om == NULL) {
        return BLE_HS_ENOMEM;
    }

    rc = ble_gattc_write_no_rsp(conn_handle, attr_handle, om);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $write                                                                    *
 *****************************************************************************/

/**
 * Calls a write-characteristic-value proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_write_cb(struct ble_gattc_proc *proc, int status,
                   uint16_t att_handle)
{
    struct ble_gatt_attr attr;
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0 && status != BLE_HS_EDONE) {
        STATS_INC(ble_gattc_stats, write_fail);
    }

    if (proc->write.cb == NULL) {
        rc = 0;
    } else {
        memset(&attr, 0, sizeof attr);
        attr.handle = proc->write.att_handle;
        rc = proc->write.cb(proc->conn_handle,
                            ble_gattc_error(status, att_handle),
                            &attr, proc->write.cb_arg);
    }

    return rc;
}

/**
 * Handles an incoming ATT error response for the specified
 * write-characteristic-value proc.
 */
static void
ble_gattc_write_err(struct ble_gattc_proc *proc, int status,
                    uint16_t att_handle)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);
    ble_gattc_write_cb(proc, status, att_handle);
}

/**
 * Initiates GATT procedure: Write Characteristic Value.  This function
 * consumes the supplied mbuf regardless of the outcome.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to write
 *                                  to.
 * @param txom                  The value to write to the characteristic.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_write(uint16_t conn_handle, uint16_t attr_handle,
                struct os_mbuf *txom, ble_gatt_attr_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT(GATT_WRITE)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_att_write_req req;
    struct ble_gattc_proc *proc;
    int rc;

    STATS_INC(ble_gattc_stats, write);

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_GATT_OP_WRITE;
    proc->conn_handle = conn_handle;
    proc->write.att_handle = attr_handle;
    proc->write.cb = cb;
    proc->write.cb_arg = cb_arg;

    ble_gattc_log_write(attr_handle, OS_MBUF_PKTLEN(txom), 1);

    req.bawq_handle = attr_handle;
    rc = ble_att_clt_tx_write_req(conn_handle, &req, txom);
    txom = NULL;
    if (rc != 0) {
        goto done;
    }

done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, write_fail);
    }

    /* Free the mbuf in case the send failed. */
    os_mbuf_free_chain(txom);

    ble_gattc_process_status(proc, rc);
    return rc;
}

/**
 * Initiates GATT procedure: Write Characteristic Value (flat buffer version).
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to write
 *                                  to.
 * @param value                 The value to write to the characteristic.
 * @param value_len             The number of bytes to write.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_write_flat(uint16_t conn_handle, uint16_t attr_handle,
                     const void *data, uint16_t data_len,
                     ble_gatt_attr_fn *cb, void *cb_arg)
{
    struct os_mbuf *om;
    int rc;

    om = ble_hs_mbuf_from_flat(data, data_len);
    if (om == NULL) {
        return BLE_HS_ENOMEM;
    }

    rc = ble_gattc_write(conn_handle, attr_handle, om, cb, cb_arg);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $write long                                                               *
 *****************************************************************************/

/**
 * Calls a write-long-characteristic-value proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_write_long_cb(struct ble_gattc_proc *proc, int status,
                        uint16_t att_handle)
{
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0 && status != BLE_HS_EDONE) {
        STATS_INC(ble_gattc_stats, write_long_fail);
    }

    if (proc->write_long.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->write_long.cb(proc->conn_handle,
                                 ble_gattc_error(status, att_handle),
                                 &proc->write_long.attr,
                                 proc->write_long.cb_arg);
    }

    return rc;
}

/**
 * Triggers a pending transmit for the specified
 * write-long-characteristic-value proc.
 */
static int
ble_gattc_write_long_go(struct ble_gattc_proc *proc, int cb_on_err)
{
    struct ble_att_prep_write_cmd prep_req;
    struct ble_att_exec_write_req exec_req;
    struct os_mbuf *om;
    int max_sz;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    om = NULL;

    if (proc->write_long.attr.offset +
        OS_MBUF_PKTLEN(proc->write_long.attr.om) <= 0) {

        exec_req.baeq_flags = BLE_ATT_EXEC_WRITE_F_CONFIRM;
        rc = ble_att_clt_tx_exec_write(proc->conn_handle, &exec_req);
        goto done;
    }

    max_sz = ble_att_mtu(proc->conn_handle) -
             BLE_ATT_PREP_WRITE_CMD_BASE_SZ;
    if (max_sz <= 0) {
        /* Not connected. */
        rc = BLE_HS_ENOTCONN;
        goto done;
    }

    proc->write_long.length =
        min(max_sz,
            OS_MBUF_PKTLEN(proc->write_long.attr.om) -
                proc->write_long.attr.offset);

    om = ble_hs_mbuf_att_pkt();
    if (om == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    rc = os_mbuf_appendfrom(om, proc->write_long.attr.om,
                            proc->write_long.attr.offset,
                            proc->write_long.length);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    prep_req.bapc_handle = proc->write_long.attr.handle;
    prep_req.bapc_offset = proc->write_long.attr.offset;

    rc = ble_att_clt_tx_prep_write(proc->conn_handle, &prep_req, om);
    om = NULL;
    if (rc != 0) {
        goto done;
    }

done:
    os_mbuf_free_chain(om);

    if (rc != 0 && cb_on_err) {
        ble_gattc_write_long_cb(proc, rc, 0);
    }

    return rc;
}

/**
 * Handles an incoming ATT error response for the specified
 * write-long-characteristic-value proc.
 */
static void
ble_gattc_write_long_err(struct ble_gattc_proc *proc, int status,
                         uint16_t att_handle)
{
    struct ble_att_exec_write_req exec_req;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    /* If we have successfully queued any data, and the failure occurred before
     * we could send the execute write command, then erase all queued data.
     */
    if (proc->write_long.attr.offset > 0 &&
        proc->write_long.attr.offset <
            OS_MBUF_PKTLEN(proc->write_long.attr.om)) {

        exec_req.baeq_flags = 0;
        ble_att_clt_tx_exec_write(proc->conn_handle, &exec_req);
    }

    /* Report failure. */
    ble_gattc_write_long_cb(proc, status, att_handle);
}

/**
 * Handles an incoming prepare-write-response for the specified
 * write-long-cahracteristic-values proc.
 */
static int
ble_gattc_write_long_rx_prep(struct ble_gattc_proc *proc,
                             int status, struct ble_att_prep_write_cmd *rsp,
                             struct os_mbuf **rxom)
{
    struct os_mbuf *om;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    /* Let the caller free the mbuf. */
    om = *rxom;

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
    if (rsp->bapc_offset + OS_MBUF_PKTLEN(om) >
        OS_MBUF_PKTLEN(proc->write_long.attr.om)) {

        rc = BLE_HS_EBADDATA;
        goto err;
    }
    if (OS_MBUF_PKTLEN(om) != proc->write_long.length) {
        rc = BLE_HS_EBADDATA;
        goto err;
    }
    if (os_mbuf_cmpm(om, 0,
                     proc->write_long.attr.om, rsp->bapc_offset,
                     proc->write_long.length) != 0) {

        rc = BLE_HS_EBADDATA;
        goto err;
    }

    /* Send follow-up request. */
    proc->write_long.attr.offset += OS_MBUF_PKTLEN(om);
    rc = ble_gattc_write_long_go(proc, 1);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    /* XXX: Might need to cancel pending writes. */
    ble_gattc_write_long_cb(proc, rc, 0);
    return BLE_HS_EDONE;
}

/**
 * Handles an incoming execute-write-response for the specified
 * write-long-characteristic-values proc.
 */
static int
ble_gattc_write_long_rx_exec(struct ble_gattc_proc *proc, int status)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);
    ble_gattc_write_long_cb(proc, status, 0);
    return BLE_HS_EDONE;
}

/**
 * Initiates GATT procedure: Write Long Characteristic Values.  This function
 * consumes the supplied mbuf regardless of the outcome.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to write
 *                                  to.
 * @param txom                  The value to write to the characteristic.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_write_long(uint16_t conn_handle, uint16_t attr_handle,
                     struct os_mbuf *txom, ble_gatt_attr_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT(GATT_WRITE_LONG)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    STATS_INC(ble_gattc_stats, write_long);

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_GATT_OP_WRITE_LONG;
    proc->conn_handle = conn_handle;
    proc->write_long.attr.handle = attr_handle;
    proc->write_long.attr.offset = 0;
    proc->write_long.attr.om = txom;
    proc->write_long.cb = cb;
    proc->write_long.cb_arg = cb_arg;

    /* The mbuf is consumed by the procedure. */
    txom = NULL;

    ble_gattc_log_write_long(proc);

    rc = ble_gattc_write_long_go(proc, 0);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, write_long_fail);
    }

    /* Free the mbuf in case of failure. */
    os_mbuf_free_chain(txom);

    ble_gattc_process_status(proc, rc);
    return rc;
}

/*****************************************************************************
 * $write reliable                                                           *
 *****************************************************************************/

/**
 * Calls a write-long-characteristic-value proc's callback with the specified
 * parameters.  If the proc has no callback, this function is a no-op.
 *
 * @return                      The return code of the callback (or 0 if there
 *                                  is no callback).
 */
static int
ble_gattc_write_reliable_cb(struct ble_gattc_proc *proc, int status,
                            uint16_t att_handle)
{
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());
    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != 0 && status != BLE_HS_EDONE) {
        STATS_INC(ble_gattc_stats, write_reliable_fail);
    }

    if (proc->write_reliable.cb == NULL) {
        rc = 0;
    } else {
        rc = proc->write_reliable.cb(proc->conn_handle,
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
 */
static int
ble_gattc_write_reliable_go(struct ble_gattc_proc *proc, int cb_on_err)
{
    struct ble_att_prep_write_cmd prep_req;
    struct ble_att_exec_write_req exec_req;
    struct ble_gatt_attr *attr;
    struct os_mbuf *om;
    uint16_t max_sz;
    int attr_idx;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    om = NULL;

    attr_idx = proc->write_reliable.cur_attr;

    if (attr_idx >= proc->write_reliable.num_attrs) {
        exec_req.baeq_flags = BLE_ATT_EXEC_WRITE_F_CONFIRM;
        rc = ble_att_clt_tx_exec_write(proc->conn_handle, &exec_req);
        goto done;
    }

    attr = proc->write_reliable.attrs + attr_idx;

    max_sz = ble_att_mtu(proc->conn_handle) - BLE_ATT_PREP_WRITE_CMD_BASE_SZ;
    if (max_sz <= 0) {
        /* Not connected. */
        rc = BLE_HS_ENOTCONN;
        goto done;
    }

    proc->write_reliable.length =
        min(max_sz, OS_MBUF_PKTLEN(attr->om) - attr->offset);

    om = ble_hs_mbuf_att_pkt();
    if (om == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    rc = os_mbuf_appendfrom(om, attr->om, attr->offset,
                            proc->write_reliable.length);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    prep_req.bapc_handle = attr->handle;
    prep_req.bapc_offset = attr->offset;

    rc = ble_att_clt_tx_prep_write(proc->conn_handle, &prep_req, om);
    om = NULL;
    if (rc != 0) {
        goto done;
    }

done:
    os_mbuf_free_chain(om);

    if (rc != 0 && cb_on_err) {
        ble_gattc_write_reliable_cb(proc, rc, 0);
    }

    return rc;
}

/**
 * Handles an incoming ATT error response for the specified
 * write-reliable-characteristic-value proc.
 */
static void
ble_gattc_write_reliable_err(struct ble_gattc_proc *proc, int status,
                             uint16_t att_handle)
{
    struct ble_att_exec_write_req exec_req;

    ble_gattc_dbg_assert_proc_not_inserted(proc);
    ble_gattc_write_reliable_cb(proc, status, att_handle);

    /* If we have successfully queued any data, and the failure occurred before
     * we could send the execute write command, then erase all queued data.
     */
    if (proc->write_reliable.cur_attr > 0 &&
        proc->write_reliable.cur_attr < proc->write_reliable.num_attrs) {

        exec_req.baeq_flags = 0;
        ble_att_clt_tx_exec_write(proc->conn_handle, &exec_req);
    }
}

/**
 * Handles an incoming prepare-write-response for the specified
 * write-reliable-cahracteristic-values proc.
 */
static int
ble_gattc_write_reliable_rx_prep(struct ble_gattc_proc *proc,
                                 int status,
                                 struct ble_att_prep_write_cmd *rsp,
                                 struct os_mbuf **rxom)
{
    struct ble_gatt_attr *attr;
    struct os_mbuf *om;
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    /* Let the caller free the mbuf. */
    om = *rxom;

    if (status != 0) {
        rc = status;
        goto err;
    }

    if (proc->write_reliable.cur_attr >= proc->write_reliable.num_attrs) {
        /* Expecting an execute write response, not a prepare write response. */
        rc = BLE_HS_EBADDATA;
        goto err;
    }
    attr = proc->write_reliable.attrs + proc->write_reliable.cur_attr;

    /* Verify the response. */
    if (rsp->bapc_handle != attr->handle) {
        rc = BLE_HS_EBADDATA;
        goto err;
    }
    if (rsp->bapc_offset != attr->offset) {
        rc = BLE_HS_EBADDATA;
        goto err;
    }
    if (os_mbuf_cmpm(attr->om, rsp->bapc_offset, om, 0,
                     proc->write_reliable.length) != 0) {

        rc = BLE_HS_EBADDATA;
        goto err;
    }

    /* Send follow-up request. */
    attr->offset += proc->write_reliable.length;
    if (attr->offset >= OS_MBUF_PKTLEN(attr->om)) {
        attr->offset = 0;
        proc->write_reliable.cur_attr++;
    }
    rc = ble_gattc_write_reliable_go(proc, 1);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    /* XXX: Might need to cancel pending writes. */
    ble_gattc_write_reliable_cb(proc, rc, 0);
    return BLE_HS_EDONE;
}

/**
 * Handles an incoming execute-write-response for the specified
 * write-reliable-characteristic-values proc.
 */
static int
ble_gattc_write_reliable_rx_exec(struct ble_gattc_proc *proc, int status)
{
    ble_gattc_dbg_assert_proc_not_inserted(proc);
    ble_gattc_write_reliable_cb(proc, status, 0);
    return BLE_HS_EDONE;
}

/**
 * Initiates GATT procedure: Reliable Writes.  This function consumes the
 * supplied mbufs regardless of the outcome.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attrs                 An array of attribute descriptors; specifies
 *                                  which characteristics to write to and what
 *                                  data to write to them.  The mbuf pointer in
 *                                  each attribute is set to NULL by this
 *                                  function.
 * @param num_attrs             The number of characteristics to write; equal
 *                                  to the number of elements in the 'attrs'
 *                                  array.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 */
int
ble_gattc_write_reliable(uint16_t conn_handle,
                         struct ble_gatt_attr *attrs,
                         int num_attrs,
                         ble_gatt_reliable_attr_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT(GATT_WRITE_RELIABLE)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;
    int i;

    proc = NULL;

    STATS_INC(ble_gattc_stats, write_reliable);

    if (num_attrs > NIMBLE_OPT(GATT_WRITE_MAX_ATTRS)) {
        rc = BLE_HS_EINVAL;
        goto done;
    }

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_GATT_OP_WRITE_RELIABLE;
    proc->conn_handle = conn_handle;
    proc->write_reliable.num_attrs = num_attrs;
    proc->write_reliable.cur_attr = 0;
    proc->write_reliable.cb = cb;
    proc->write_reliable.cb_arg = cb_arg;

    for (i = 0; i < num_attrs; i++) {
        proc->write_reliable.attrs[i] = attrs[i];
        proc->write_reliable.attrs[i].offset = 0;

        /* Consume mbuf from caller. */
        attrs[i].om = NULL;
    }

    ble_gattc_log_write_reliable(proc);

    rc = ble_gattc_write_reliable_go(proc, 1);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, write_reliable_fail);
    }

    /* Free supplied mbufs in case something failed. */
    for (i = 0; i < num_attrs; i++) {
        os_mbuf_free_chain(attrs[i].om);
        attrs[i].om = NULL;
    }

    ble_gattc_process_status(proc, rc);
    return rc;
}

/*****************************************************************************
 * $notify                                                                   *
 *****************************************************************************/

/**
 * Sends a "free-form" characteristic notification.  This function consumes the
 * supplied mbuf regardless of the outcome.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param chr_val_handle        The attribute handle to indicate in the
 *                                  outgoing notification.
 * @param txom                  The value to write to the characteristic.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_notify_custom(uint16_t conn_handle, uint16_t chr_val_handle,
                        struct os_mbuf *txom)
{
#if !NIMBLE_OPT(GATT_NOTIFY)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_att_notify_req req;
    int rc;

    STATS_INC(ble_gattc_stats, notify);

    ble_gattc_log_notify(chr_val_handle);

    if (txom == NULL) {
        /* No custom attribute data; read the value from the specified
         * attribute.
         */
        txom = ble_hs_mbuf_att_pkt();
        if (txom == NULL) {
            rc = BLE_HS_ENOMEM;
            goto err;
        }
        rc = ble_att_svr_read_handle(BLE_HS_CONN_HANDLE_NONE,
                                     chr_val_handle, 0, txom, NULL);
        if (rc != 0) {
            /* Fatal error; application disallowed attribute read. */
            rc = BLE_HS_EAPP;
            goto err;
        }
    }

    req.banq_handle = chr_val_handle;
    rc = ble_att_clt_tx_notify(conn_handle, &req, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, notify_fail);
    }

    os_mbuf_free_chain(txom);

    return rc;
}

/**
 * Sends a characteristic notification.  The content of the message is read
 * from the specified characteristic.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param chr_val_handle        The value attribute handle of the
 *                                  characteristic to include in the outgoing
 *                                  notification.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_notify(uint16_t conn_handle, uint16_t chr_val_handle)
{
#if !NIMBLE_OPT(GATT_NOTIFY)
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    rc = ble_gattc_notify_custom(conn_handle, chr_val_handle, NULL);

    /* Tell the application that a notification transmission was attempted. */
    ble_gap_notify_tx_event(rc, conn_handle, chr_val_handle, 0);

    return rc;
}

/*****************************************************************************
 * $indicate                                                                 *
 *****************************************************************************/

/**
 * Handles an incoming ATT error response for the specified indication proc.
 * A device should never send an error in response to an indication.  If this
 * happens, we treat it like a confirmation (indication ack), but report the
 * error status to the application.
 */
static void
ble_gattc_indicate_err(struct ble_gattc_proc *proc, int status,
                       uint16_t att_handle)
{
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    if (status != BLE_HS_ENOTCONN) {
        rc = ble_gatts_rx_indicate_ack(proc->conn_handle,
                                       proc->indicate.chr_val_handle);
        if (rc != 0) {
            return;
        }
    }

    /* Tell the application about the received acknowledgment. */
    ble_gap_notify_tx_event(status, proc->conn_handle,
                            proc->indicate.chr_val_handle, 1);

    /* Send the next indication if one is pending. */
    ble_gatts_send_next_indicate(proc->conn_handle);
}

/**
 * Handles an incoming handle-value-confirmation for the specified indication
 * proc.
 */
static void
ble_gattc_indicate_rx_rsp(struct ble_gattc_proc *proc)
{
    int rc;

    ble_gattc_dbg_assert_proc_not_inserted(proc);

    rc = ble_gatts_rx_indicate_ack(proc->conn_handle,
                                   proc->indicate.chr_val_handle);
    if (rc != 0) {
        return;
    }

    /* Tell the application about the received acknowledgment. */
    ble_gap_notify_tx_event(BLE_HS_EDONE, proc->conn_handle,
                            proc->indicate.chr_val_handle, 1);

    /* Send the next indication if one is pending. */
    ble_gatts_send_next_indicate(proc->conn_handle);
}

/**
 * Causes the indication in progress for the specified connection (if any) to
 * fail with a status code of BLE_HS_ENOTCONN;
 */
void
ble_gatts_indicate_fail_notconn(uint16_t conn_handle)
{
    ble_gattc_fail_procs(conn_handle, BLE_GATT_OP_INDICATE, BLE_HS_ENOTCONN);
}

/**
 * Sends a characteristic indication.  The content of the message is read from
 * the specified characteristic.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param chr_val_handle        The value attribute handle of the
 *                                  characteristic to include in the outgoing
 *                                  indication.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gattc_indicate(uint16_t conn_handle, uint16_t chr_val_handle)
{
#if !NIMBLE_OPT(GATT_INDICATE)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_att_indicate_req req;
    struct ble_gattc_proc *proc;
    struct ble_hs_conn *conn;
    struct os_mbuf *om;
    int rc;

    STATS_INC(ble_gattc_stats, indicate);

    om = NULL;

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    proc->op = BLE_GATT_OP_INDICATE;
    proc->conn_handle = conn_handle;
    proc->indicate.chr_val_handle = chr_val_handle;

    ble_gattc_log_indicate(chr_val_handle);

    om = ble_hs_mbuf_att_pkt();
    if (om == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    rc = ble_att_svr_read_handle(BLE_HS_CONN_HANDLE_NONE, chr_val_handle, 0,
                                 om, NULL);
    if (rc != 0) {
        /* Fatal error; application disallowed attribute read. */
        BLE_HS_DBG_ASSERT(0);
        rc = BLE_HS_EAPP;
        goto done;
    }

    req.baiq_handle = chr_val_handle;
    rc = ble_att_clt_tx_indicate(conn_handle, &req, om);
    om = NULL;
    if (rc != 0) {
        goto done;
    }

    ble_hs_lock();
    conn = ble_hs_conn_find(conn_handle);
    if (conn != NULL) {
        BLE_HS_DBG_ASSERT(conn->bhc_gatt_svr.indicate_val_handle == 0);
        conn->bhc_gatt_svr.indicate_val_handle = chr_val_handle;
    }
    ble_hs_unlock();


done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, indicate_fail);
    }

    /* Tell the application that an indication transmission was attempted. */
    ble_gap_notify_tx_event(rc, conn_handle, chr_val_handle, 1);

    ble_gattc_process_status(proc, rc);
    os_mbuf_free_chain(om);
    return rc;
}

/*****************************************************************************
 * $rx                                                                       *
 *****************************************************************************/

/**
 * Dispatches an incoming ATT error-response to the appropriate active GATT
 * procedure.
 */
void
ble_gattc_rx_err(uint16_t conn_handle, struct ble_att_error_rsp *rsp)
{
    struct ble_gattc_proc *proc;
    ble_gattc_err_fn *err_cb;

    proc = ble_gattc_extract(conn_handle, BLE_GATT_OP_NONE);
    if (proc != NULL) {
        err_cb = ble_gattc_err_dispatch_get(proc->op);
        if (err_cb != NULL) {
            err_cb(proc, BLE_HS_ERR_ATT_BASE + rsp->baep_error_code,
                   rsp->baep_handle);
        }
        ble_gattc_proc_free(proc);
    }
}

/**
 * Dispatches an incoming ATT exchange-mtu-response to the appropriate active
 * GATT procedure.
 */
void
ble_gattc_rx_mtu(uint16_t conn_handle, int status, uint16_t chan_mtu)
{
    struct ble_gattc_proc *proc;

    proc = ble_gattc_extract(conn_handle, BLE_GATT_OP_MTU);
    if (proc != NULL) {
        ble_gattc_mtu_cb(proc, status, 0, chan_mtu);
        ble_gattc_process_status(proc, BLE_HS_EDONE);
    }
}

/**
 * Dispatches an incoming "information data" entry from a
 * find-information-response to the appropriate active GATT procedure.
 */
void
ble_gattc_rx_find_info_idata(uint16_t conn_handle,
                             struct ble_att_find_info_idata *idata)
{
#if !NIMBLE_OPT(ATT_CLT_FIND_INFO)
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    proc = ble_gattc_extract(conn_handle, BLE_GATT_OP_DISC_ALL_DSCS);
    if (proc != NULL) {
        rc = ble_gattc_disc_all_dscs_rx_idata(proc, idata);
        ble_gattc_process_status(proc, rc);
    }
}

/**
 * Dispatches an incoming notification of the end of a
 * find-information-response to the appropriate active GATT procedure.
 */
void
ble_gattc_rx_find_info_complete(uint16_t conn_handle, int status)
{
#if !NIMBLE_OPT(ATT_CLT_FIND_INFO)
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    proc = ble_gattc_extract(conn_handle, BLE_GATT_OP_DISC_ALL_DSCS);
    if (proc != NULL) {
        rc = ble_gattc_disc_all_dscs_rx_complete(proc, status);
        ble_gattc_process_status(proc, rc);
    }
}

/**
 * Dispatches an incoming "handles info" entry from a
 * find-by-type-value-response to the appropriate active GATT procedure.
 */
void
ble_gattc_rx_find_type_value_hinfo(uint16_t conn_handle,
                                   struct ble_att_find_type_value_hinfo *hinfo)
{
#if !NIMBLE_OPT(ATT_CLT_FIND_TYPE)
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    proc = ble_gattc_extract(conn_handle, BLE_GATT_OP_DISC_SVC_UUID);
    if (proc != NULL) {
        rc = ble_gattc_disc_svc_uuid_rx_hinfo(proc, hinfo);
        ble_gattc_process_status(proc, rc);
    }
}

/**
 * Dispatches an incoming notification of the end of a
 * find-by-type-value-response to the appropriate active GATT procedure.
 */
void
ble_gattc_rx_find_type_value_complete(uint16_t conn_handle, int status)
{
#if !NIMBLE_OPT(ATT_CLT_FIND_TYPE)
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    proc = ble_gattc_extract(conn_handle, BLE_GATT_OP_DISC_SVC_UUID);
    if (proc != NULL) {
        rc = ble_gattc_disc_svc_uuid_rx_complete(proc, status);
        ble_gattc_process_status(proc, rc);
    }
}

/**
 * Dispatches an incoming "attribute data" entry from a read-by-type-response
 * to the appropriate active GATT procedure.
 */
void
ble_gattc_rx_read_type_adata(uint16_t conn_handle,
                             struct ble_att_read_type_adata *adata)
{
#if !NIMBLE_OPT(ATT_CLT_READ_TYPE)
    return;
#endif

    const struct ble_gattc_rx_adata_entry *rx_entry;
    struct ble_gattc_proc *proc;
    int rc;

    proc = BLE_GATTC_RX_EXTRACT_RX_ENTRY(conn_handle,
                                         ble_gattc_rx_read_type_elem_entries,
                                         &rx_entry);
    if (proc != NULL) {
        rc = rx_entry->cb(proc, adata);
        ble_gattc_process_status(proc, rc);
    }
}

/**
 * Dispatches an incoming notification of the end of a read-by-type-response to
 * the appropriate active GATT procedure.
 */
void
ble_gattc_rx_read_type_complete(uint16_t conn_handle, int status)
{
#if !NIMBLE_OPT(ATT_CLT_READ_TYPE)
    return;
#endif

    const struct ble_gattc_rx_complete_entry *rx_entry;
    struct ble_gattc_proc *proc;
    int rc;

    proc = BLE_GATTC_RX_EXTRACT_RX_ENTRY(
        conn_handle, ble_gattc_rx_read_type_complete_entries,
        &rx_entry);
    if (proc != NULL) {
        rc = rx_entry->cb(proc, status);
        ble_gattc_process_status(proc, rc);
    }
}

/**
 * Dispatches an incoming "attribute data" entry from a
 * read-by-group-type-response to the appropriate active GATT procedure.
 */
void
ble_gattc_rx_read_group_type_adata(uint16_t conn_handle,
                                   struct ble_att_read_group_type_adata *adata)
{
#if !NIMBLE_OPT(ATT_CLT_READ_GROUP_TYPE)
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    proc = ble_gattc_extract(conn_handle, BLE_GATT_OP_DISC_ALL_SVCS);
    if (proc != NULL) {
        rc = ble_gattc_disc_all_svcs_rx_adata(proc, adata);
        ble_gattc_process_status(proc, rc);
    }
}

/**
 * Dispatches an incoming notification of the end of a
 * read-by-group-type-response to the appropriate active GATT procedure.
 */
void
ble_gattc_rx_read_group_type_complete(uint16_t conn_handle, int status)
{
#if !NIMBLE_OPT(ATT_CLT_READ_GROUP_TYPE)
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    proc = ble_gattc_extract(conn_handle, BLE_GATT_OP_DISC_ALL_SVCS);
    if (proc != NULL) {
        rc = ble_gattc_disc_all_svcs_rx_complete(proc, status);
        ble_gattc_process_status(proc, rc);
    }
}

/**
 * Dispatches an incoming ATT read-response to the appropriate active GATT
 * procedure.
 */
void
ble_gattc_rx_read_rsp(uint16_t conn_handle, int status, struct os_mbuf **om)
{
#if !NIMBLE_OPT(ATT_CLT_READ)
    return;
#endif

    const struct ble_gattc_rx_attr_entry *rx_entry;
    struct ble_gattc_proc *proc;
    int rc;

    proc = BLE_GATTC_RX_EXTRACT_RX_ENTRY(conn_handle,
                                         ble_gattc_rx_read_rsp_entries,
                                         &rx_entry);
    if (proc != NULL) {
        rc = rx_entry->cb(proc, status, om);
        ble_gattc_process_status(proc, rc);
    }
}

/**
 * Dispatches an incoming ATT read-blob-response to the appropriate active GATT
 * procedure.
 */
void
ble_gattc_rx_read_blob_rsp(uint16_t conn_handle, int status,
                           struct os_mbuf **om)
{
#if !NIMBLE_OPT(ATT_CLT_READ_BLOB)
    return;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    proc = ble_gattc_extract(conn_handle, BLE_GATT_OP_READ_LONG);
    if (proc != NULL) {
        rc = ble_gattc_read_long_rx_read_rsp(proc, status, om);
        ble_gattc_process_status(proc, rc);
    }
}

/**
 * Dispatches an incoming ATT read-multiple-response to the appropriate active
 * GATT procedure.
 */
void
ble_gattc_rx_read_mult_rsp(uint16_t conn_handle, int status,
                           struct os_mbuf **om)
{
#if !NIMBLE_OPT(ATT_CLT_READ_MULT)
    return;
#endif

    struct ble_gattc_proc *proc;

    proc = ble_gattc_extract(conn_handle, BLE_GATT_OP_READ_MULT);
    if (proc != NULL) {
        ble_gattc_read_mult_cb(proc, status, 0, om);
        ble_gattc_process_status(proc, BLE_HS_EDONE);
    }
}

/**
 * Dispatches an incoming ATT write-response to the appropriate active GATT
 * procedure.
 */
void
ble_gattc_rx_write_rsp(uint16_t conn_handle)
{
#if !NIMBLE_OPT(ATT_CLT_WRITE)
    return;
#endif

    struct ble_gattc_proc *proc;

    proc = ble_gattc_extract(conn_handle, BLE_GATT_OP_WRITE);
    if (proc != NULL) {
        ble_gattc_write_cb(proc, 0, 0);
        ble_gattc_process_status(proc, BLE_HS_EDONE);
    }
}

/**
 * Dispatches an incoming ATT prepare-write-response to the appropriate active
 * GATT procedure.
 */
void
ble_gattc_rx_prep_write_rsp(uint16_t conn_handle, int status,
                            struct ble_att_prep_write_cmd *rsp,
                            struct os_mbuf **om)
{
#if !NIMBLE_OPT(ATT_CLT_PREP_WRITE)
    return;
#endif

    const struct ble_gattc_rx_prep_entry *rx_entry;
    struct ble_gattc_proc *proc;
    int rc;

    proc = BLE_GATTC_RX_EXTRACT_RX_ENTRY(conn_handle,
                                         ble_gattc_rx_prep_entries,
                                         &rx_entry);
    if (proc != NULL) {
        rc = rx_entry->cb(proc, status, rsp, om);
        ble_gattc_process_status(proc, rc);
    }
}

/**
 * Dispatches an incoming ATT execute-write-response to the appropriate active
 * GATT procedure.
 */
void
ble_gattc_rx_exec_write_rsp(uint16_t conn_handle, int status)
{
#if !NIMBLE_OPT(ATT_CLT_EXEC_WRITE)
    return;
#endif

    const struct ble_gattc_rx_exec_entry *rx_entry;
    struct ble_gattc_proc *proc;
    int rc;

    proc = BLE_GATTC_RX_EXTRACT_RX_ENTRY(conn_handle,
                                         ble_gattc_rx_exec_entries, &rx_entry);
    if (proc != NULL) {
        rc = rx_entry->cb(proc, status);
        ble_gattc_process_status(proc, rc);
    }
}

/**
 * Dispatches an incoming ATT handle-value-confirmation to the appropriate
 * active GATT procedure.
 */
void
ble_gattc_rx_indicate_rsp(uint16_t conn_handle)
{
#if !NIMBLE_OPT(ATT_CLT_INDICATE)
    return;
#endif

    struct ble_gattc_proc *proc;

    proc = ble_gattc_extract(conn_handle, BLE_GATT_OP_INDICATE);
    if (proc != NULL) {
        ble_gattc_indicate_rx_rsp(proc);
        ble_gattc_process_status(proc, BLE_HS_EDONE);
    }
}

/*****************************************************************************
 * $misc                                                                     *
 *****************************************************************************/

/**
 * Called when a BLE connection ends.  Frees all GATT resources associated with
 * the connection and cancels all relevant pending and in-progress GATT
 * procedures.
 *
 * @param conn_handle           The handle of the connection that was
 *                                  terminated.
 */
void
ble_gattc_connection_broken(uint16_t conn_handle)
{
    ble_gattc_fail_procs(conn_handle, BLE_GATT_OP_NONE, BLE_HS_ENOTCONN);
}

/**
 * Indicates whether there are currently any active GATT client procedures.
 */
int
ble_gattc_any_jobs(void)
{
    return !STAILQ_EMPTY(&ble_gattc_procs);
}

int
ble_gattc_init(void)
{
    int rc;

    free(ble_gattc_proc_mem);

    STAILQ_INIT(&ble_gattc_procs);

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

    rc = stats_init_and_reg(
        STATS_HDR(ble_gattc_stats), STATS_SIZE_INIT_PARMS(ble_gattc_stats,
        STATS_SIZE_32), STATS_NAME_INIT_PARMS(ble_gattc_stats), "ble_gattc");
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    return 0;

err:
    free(ble_gattc_proc_mem);
    ble_gattc_proc_mem = NULL;

    return rc;
}
