/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
#include "ble_gatt_priv.h"

struct ble_gattc_entry {
    STAILQ_ENTRY(ble_gattc_entry) next;

    uint8_t op;
    uint8_t flags;
    uint16_t conn_handle;
    uint32_t tx_time; /* OS ticks. */
    union {
        struct {
            int (*cb)(int status, uint16_t conn_handle, uint16_t mtu,
                      void *arg);
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
            ble_gatt_attr_fn *cb;
            void *cb_arg;
        } indicate;
    };
};

#define BLE_GATT_HEARTBEAT_PERIOD               1000 /* Milliseconds. */
#define BLE_GATT_UNRESPONSIVE_TIMEOUT           5000 /* Milliseconds. */

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
#define BLE_GATT_OP_WRITE_NO_RSP                9
#define BLE_GATT_OP_WRITE                       10
#define BLE_GATT_OP_INDICATE                    11
#define BLE_GATT_OP_MAX                         12

static struct os_callout_func ble_gattc_heartbeat_timer;

typedef int ble_gattc_kick_fn(struct ble_gattc_entry *entry);
typedef void ble_gattc_err_fn(struct ble_gattc_entry *entry, int status);

static int ble_gattc_kick_mtu(struct ble_gattc_entry *entry);
static int ble_gattc_kick_disc_all_svcs(struct ble_gattc_entry *entry);
static int ble_gattc_kick_disc_svc_uuid(struct ble_gattc_entry *entry);
static int ble_gattc_kick_find_inc_svcs(struct ble_gattc_entry *entry);
static int ble_gattc_kick_disc_all_chrs(struct ble_gattc_entry *entry);
static int ble_gattc_kick_disc_chr_uuid(struct ble_gattc_entry *entry);
static int ble_gattc_kick_disc_all_dscs(struct ble_gattc_entry *entry);
static int ble_gattc_kick_read(struct ble_gattc_entry *entry);
static int ble_gattc_kick_read_uuid(struct ble_gattc_entry *entry);
static int ble_gattc_kick_write_no_rsp(struct ble_gattc_entry *entry);
static int ble_gattc_kick_write(struct ble_gattc_entry *entry);
static int ble_gattc_kick_indicate(struct ble_gattc_entry *entry);

static void ble_gattc_err_mtu(struct ble_gattc_entry *entry, int status);
static void ble_gattc_err_disc_all_svcs(struct ble_gattc_entry *entry,
                                        int status);
static void ble_gattc_err_disc_svc_uuid(struct ble_gattc_entry *entry,
                                        int status);
static void ble_gattc_err_find_inc_svcs(struct ble_gattc_entry *entry,
                                        int status);
static void ble_gattc_err_disc_all_chrs(struct ble_gattc_entry *entry,
                                        int status);
static void ble_gattc_err_disc_chr_uuid(struct ble_gattc_entry *entry,
                                        int status);
static void ble_gattc_err_disc_all_dscs(struct ble_gattc_entry *entry,
                                        int status);
static void ble_gattc_err_read(struct ble_gattc_entry *entry, int status);
static void ble_gattc_err_read_uuid(struct ble_gattc_entry *entry, int status);
static void ble_gattc_err_write(struct ble_gattc_entry *entry, int status);
static void ble_gattc_err_indicate(struct ble_gattc_entry *entry, int status);

static int
ble_gattc_find_inc_svcs_rx_adata(struct ble_gattc_entry *entry,
                                 struct ble_hs_conn *conn,
                                 struct ble_att_read_type_adata *adata);
static int ble_gattc_find_inc_svcs_rx_complete(struct ble_gattc_entry *entry,
                                               struct ble_hs_conn *conn,
                                               int status);
static int ble_gattc_find_inc_svcs_rx_read_rsp(struct ble_gattc_entry *entry,
                                               struct ble_hs_conn *conn,
                                               int status,
                                               void *value, int value_len);
static int
ble_gattc_disc_all_chrs_rx_adata(struct ble_gattc_entry *entry,
                                 struct ble_hs_conn *conn,
                                 struct ble_att_read_type_adata *adata);
static int ble_gattc_disc_all_chrs_rx_complete(struct ble_gattc_entry *entry,
                                               struct ble_hs_conn *conn,
                                               int status);
static int
ble_gattc_disc_chr_uuid_rx_adata(struct ble_gattc_entry *entry,
                                 struct ble_hs_conn *conn,
                                 struct ble_att_read_type_adata *adata);
static int ble_gattc_disc_chr_uuid_rx_complete(struct ble_gattc_entry *entry,
                                               struct ble_hs_conn *conn,
                                               int status);
static int ble_gattc_read_rx_read_rsp(struct ble_gattc_entry *entry,
                                      struct ble_hs_conn *conn, int status,
                                      void *value, int value_len);
static int
ble_gattc_read_uuid_rx_adata(struct ble_gattc_entry *entry,
                             struct ble_hs_conn *conn,
                             struct ble_att_read_type_adata *adata);
static int ble_gattc_read_uuid_rx_complete(struct ble_gattc_entry *entry,
                                           struct ble_hs_conn *conn,
                                           int status);

struct ble_gattc_dispatch_entry {
    ble_gattc_kick_fn *kick_cb;
    ble_gattc_err_fn *err_cb;
};

static const struct ble_gattc_dispatch_entry
    ble_gattc_dispatch[BLE_GATT_OP_MAX] = {

    [BLE_GATT_OP_MTU] = {
        .kick_cb = ble_gattc_kick_mtu,
        .err_cb = ble_gattc_err_mtu,
    },
    [BLE_GATT_OP_DISC_ALL_SVCS] = {
        .kick_cb = ble_gattc_kick_disc_all_svcs,
        .err_cb = ble_gattc_err_disc_all_svcs,
    },
    [BLE_GATT_OP_DISC_SVC_UUID] = {
        .kick_cb = ble_gattc_kick_disc_svc_uuid,
        .err_cb = ble_gattc_err_disc_svc_uuid,
    },
    [BLE_GATT_OP_FIND_INC_SVCS] = {
        .kick_cb = ble_gattc_kick_find_inc_svcs,
        .err_cb = ble_gattc_err_find_inc_svcs,
    },
    [BLE_GATT_OP_DISC_ALL_CHRS] = {
        .kick_cb = ble_gattc_kick_disc_all_chrs,
        .err_cb = ble_gattc_err_disc_all_chrs,
    },
    [BLE_GATT_OP_DISC_CHRS_UUID] = {
        .kick_cb = ble_gattc_kick_disc_chr_uuid,
        .err_cb = ble_gattc_err_disc_chr_uuid,
    },
    [BLE_GATT_OP_DISC_ALL_DSCS] = {
        .kick_cb = ble_gattc_kick_disc_all_dscs,
        .err_cb = ble_gattc_err_disc_all_dscs,
    },
    [BLE_GATT_OP_READ] = {
        .kick_cb = ble_gattc_kick_read,
        .err_cb = ble_gattc_err_read,
    },
    [BLE_GATT_OP_READ_UUID] = {
        .kick_cb = ble_gattc_kick_read_uuid,
        .err_cb = ble_gattc_err_read_uuid,
    },
    [BLE_GATT_OP_WRITE_NO_RSP] = {
        .kick_cb = ble_gattc_kick_write_no_rsp,
        .err_cb = NULL,
    },
    [BLE_GATT_OP_WRITE] = {
        .kick_cb = ble_gattc_kick_write,
        .err_cb = ble_gattc_err_write,
    },
    [BLE_GATT_OP_INDICATE] = {
        .kick_cb = ble_gattc_kick_indicate,
        .err_cb = ble_gattc_err_indicate,
    },
};

typedef int ble_gattc_rx_adata_fn(struct ble_gattc_entry *entry,
                                  struct ble_hs_conn *conn, 
                                  struct ble_att_read_type_adata *adata);
struct ble_gattc_rx_adata_entry {
    uint8_t op;
    ble_gattc_rx_adata_fn *cb;
};

typedef int ble_gattc_rx_complete_fn(struct ble_gattc_entry *entry,
                                     struct ble_hs_conn *conn,
                                     int rc);
struct ble_gattc_rx_complete_entry {
    uint8_t op;
    ble_gattc_rx_complete_fn *cb;
};

typedef int ble_gattc_rx_attr_fn(struct ble_gattc_entry *entry,
                                 struct ble_hs_conn *conn, int status,
                                 void *value, int value_len);
struct ble_gattc_rx_attr_entry {
    uint8_t op;
    ble_gattc_rx_attr_fn *cb;
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
    { BLE_GATT_OP_READ, ble_gattc_read_rx_read_rsp },
    { BLE_GATT_OP_FIND_INC_SVCS, ble_gattc_find_inc_svcs_rx_read_rsp },
};

#define BLE_GATT_ENTRY_F_PENDING    0x01
#define BLE_GATT_ENTRY_F_EXPECTING  0x02
#define BLE_GATT_ENTRY_F_CONGESTED  0x04
#define BLE_GATT_ENTRY_F_NO_MEM     0x08

#define BLE_GATT_NUM_ENTRIES        16
static void *ble_gattc_entry_mem;
static struct os_mempool ble_gattc_entry_pool;

static STAILQ_HEAD(, ble_gattc_entry) ble_gattc_list;

/*****************************************************************************
 * $rx entry                                                                 *
 *****************************************************************************/

#define BLE_GATTC_RX_ENTRY_FIND(op_arg, rx_entries, result) do              \
{                                                                           \
    int BGREF_i;                                                            \
                                                                            \
    (result) = NULL;                                                        \
    for (BGREF_i = 0;                                                       \
         BGREF_i < sizeof (rx_entries) / sizeof (rx_entries)[0];            \
         BGREF_i++) {                                                       \
                                                                            \
        if ((rx_entries)[BGREF_i].op == (op_arg)) {                         \
            (result) = (rx_entries) + BGREF_i;                              \
            break;                                                          \
        }                                                                   \
    }                                                                       \
} while (0)

/*****************************************************************************
 * @entry                                                                    *
 *****************************************************************************/

static const struct ble_gattc_dispatch_entry *
ble_gattc_dispatch_get(uint8_t op)
{
    assert(op < BLE_GATT_OP_MAX);
    return ble_gattc_dispatch + op;
}

static struct ble_gattc_entry *
ble_gattc_entry_alloc(void)
{
    struct ble_gattc_entry *entry;

    entry = os_memblock_get(&ble_gattc_entry_pool);
    if (entry != NULL) {
        memset(entry, 0, sizeof *entry);
    }

    return entry;
}

static void
ble_gattc_entry_free(struct ble_gattc_entry *entry)
{
    int rc;

    rc = os_memblock_put(&ble_gattc_entry_pool, entry);
    assert(rc == 0);
}

static void
ble_gattc_entry_remove(struct ble_gattc_entry *entry,
                      struct ble_gattc_entry *prev)
{
    if (prev == NULL) {
        assert(STAILQ_FIRST(&ble_gattc_list) == entry);
        STAILQ_REMOVE_HEAD(&ble_gattc_list, next);
    } else {
        assert(STAILQ_NEXT(prev, next) == entry);
        STAILQ_NEXT(prev, next) = STAILQ_NEXT(entry, next);
    }
}

static void
ble_gattc_entry_remove_free(struct ble_gattc_entry *entry,
                           struct ble_gattc_entry *prev)
{
    ble_gattc_entry_remove(entry, prev);
    ble_gattc_entry_free(entry);
}

static int
ble_gattc_entry_matches(struct ble_gattc_entry *entry, uint16_t conn_handle,
                       uint8_t att_op, int expecting_only)
{
    if (conn_handle != entry->conn_handle) {
        return 0;
    }

    if (att_op != entry->op && att_op != BLE_GATT_OP_NONE) {
        return 0;
    }

    if (expecting_only &&
        !(entry->flags & BLE_GATT_ENTRY_F_EXPECTING)) {

        return 0;
    }

    return 1;
}

static struct ble_gattc_entry *
ble_gattc_find(uint16_t conn_handle, uint8_t att_op, int expecting_only,
               struct ble_gattc_entry **out_prev)
{
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;

    prev = NULL;
    STAILQ_FOREACH(entry, &ble_gattc_list, next) {
        if (ble_gattc_entry_matches(entry, conn_handle, att_op,
                                    expecting_only)) {
            if (out_prev != NULL) {
                *out_prev = prev;
            }
            return entry;
        }

        prev = entry;
    }

    return NULL;
}

static void
ble_gattc_entry_set_pending(struct ble_gattc_entry *entry)
{
    assert(!(entry->flags & BLE_GATT_ENTRY_F_PENDING));

    entry->flags &= ~BLE_GATT_ENTRY_F_EXPECTING;
    entry->flags |= BLE_GATT_ENTRY_F_PENDING;
    ble_hs_kick_gatt();
}

static void
ble_gattc_entry_set_expecting(struct ble_gattc_entry *entry,
                             struct ble_gattc_entry *prev)
{
    assert(!(entry->flags & BLE_GATT_ENTRY_F_EXPECTING));

    ble_gattc_entry_remove(entry, prev);
    entry->flags &= ~BLE_GATT_ENTRY_F_PENDING;
    entry->flags |= BLE_GATT_ENTRY_F_EXPECTING;
    entry->tx_time = os_time_get();
    STAILQ_INSERT_TAIL(&ble_gattc_list, entry, next);
}

static int
ble_gattc_new_entry(uint16_t conn_handle, uint8_t op,
                    struct ble_gattc_entry **entry)
{
    struct ble_hs_conn *conn;

    *entry = NULL;

    /* Ensure we have a connection with the specified handle. */
    conn = ble_hs_conn_find(conn_handle);
    if (conn == NULL) {
        return BLE_HS_ENOTCONN;
    }

    *entry = ble_gattc_entry_alloc();
    if (*entry == NULL) {
        return BLE_HS_ENOMEM;
    }

    memset(*entry, 0, sizeof **entry);
    (*entry)->op = op;
    (*entry)->conn_handle = conn_handle;

    STAILQ_INSERT_TAIL(&ble_gattc_list, *entry, next);

    ble_gattc_entry_set_pending(*entry);

    return 0;
}

static int
ble_gattc_entry_can_pend(struct ble_gattc_entry *entry)
{
    return !(entry->flags & (BLE_GATT_ENTRY_F_CONGESTED |
                             BLE_GATT_ENTRY_F_NO_MEM |
                             BLE_GATT_ENTRY_F_EXPECTING));
}

static void
ble_gattc_heartbeat(void *arg)
{
    struct ble_gattc_entry *entry;
    uint32_t now;
    int rc;

    now = os_time_get();

    STAILQ_FOREACH(entry, &ble_gattc_list, next) {
        if (entry->flags & BLE_GATT_ENTRY_F_NO_MEM) {
            entry->flags &= ~BLE_GATT_ENTRY_F_NO_MEM;
            if (ble_gattc_entry_can_pend(entry)) {
                ble_gattc_entry_set_pending(entry);
            }
        } else if (entry->flags & BLE_GATT_ENTRY_F_EXPECTING) {
            if (now - entry->tx_time >= BLE_GATT_UNRESPONSIVE_TIMEOUT) {
                rc = ble_gap_conn_terminate(entry->conn_handle);
                assert(rc == 0); /* XXX */
            }
        }
    }

    rc = os_callout_reset(&ble_gattc_heartbeat_timer.cf_c,
                          BLE_GATT_HEARTBEAT_PERIOD * OS_TICKS_PER_SEC / 1000);
    assert(rc == 0);
}

/**
 * @return                      1 if the transmit should be postponed; else 0.
 */
static int
ble_gattc_tx_postpone_chk(struct ble_gattc_entry *entry, int rc)
{
    switch (rc) {
    case BLE_HS_ECONGESTED:
        entry->flags |= BLE_GATT_ENTRY_F_CONGESTED;
        return 1;

    case BLE_HS_ENOMEM:
        entry->flags |= BLE_GATT_ENTRY_F_NO_MEM;
        return 1;

    default:
        return 0;
    }
}

/*****************************************************************************
 * @mtu                                                                      *
 *****************************************************************************/

static int
ble_gattc_mtu_cb(struct ble_gattc_entry *entry, int status, uint16_t mtu)
{
    int rc;

    if (entry->mtu.cb == NULL) {
        rc = 0;
    } else {
        rc = entry->mtu.cb(entry->conn_handle, status, mtu, entry->mtu.cb_arg);
    }

    return rc;
}

static int
ble_gattc_kick_mtu(struct ble_gattc_entry *entry)
{
    struct ble_att_mtu_cmd req;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
        goto err;
    }

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    assert(chan != NULL);

    req.bamc_mtu = chan->blc_my_mtu;
    rc = ble_att_clt_tx_mtu(conn, &req);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    if (ble_gattc_tx_postpone_chk(entry, rc)) {
        return BLE_HS_EAGAIN;
    }

    ble_gattc_mtu_cb(entry, rc, 0);
    return rc;
}

static void
ble_gattc_err_mtu(struct ble_gattc_entry *entry, int status)
{
    ble_gattc_mtu_cb(entry, status, 0);
}

void
ble_gattc_rx_mtu(struct ble_hs_conn *conn, uint16_t chan_mtu)
{
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;

    entry = ble_gattc_find(conn->bhc_handle, BLE_GATT_OP_MTU, 1, &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    ble_gattc_mtu_cb(entry, 0, chan_mtu);
    ble_gattc_entry_remove_free(entry, prev);
}

int
ble_gattc_exchange_mtu(uint16_t conn_handle)
{
    struct ble_gattc_entry *entry;
    int rc;

    rc = ble_gattc_new_entry(conn_handle, BLE_GATT_OP_MTU, &entry);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $discover all services                                                    *
 *****************************************************************************/

static int
ble_gattc_disc_all_svcs_cb(struct ble_gattc_entry *entry,
                           int status, struct ble_gatt_service *service)
{
    int rc;

    if (entry->disc_all_svcs.cb == NULL) {
        rc = 0;
    } else {
        rc = entry->disc_all_svcs.cb(entry->conn_handle, status, service,
                                     entry->disc_all_svcs.cb_arg);
    }

    return rc;
}

static int
ble_gattc_kick_disc_all_svcs(struct ble_gattc_entry *entry)
{
    struct ble_att_read_group_type_req req;
    struct ble_hs_conn *conn;
    uint8_t uuid128[16];
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
        goto err;
    }

    rc = ble_uuid_16_to_128(BLE_ATT_UUID_PRIMARY_SERVICE, uuid128);
    assert(rc == 0);

    req.bagq_start_handle = entry->disc_all_svcs.prev_handle + 1;
    req.bagq_end_handle = 0xffff;
    rc = ble_att_clt_tx_read_group_type(conn, &req, uuid128);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    if (ble_gattc_tx_postpone_chk(entry, rc)) {
        return BLE_HS_EAGAIN;
    }

    ble_gattc_disc_all_svcs_cb(entry, rc, NULL);
    return rc;
}

static void
ble_gattc_err_disc_all_svcs(struct ble_gattc_entry *entry, int status)
{
    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Discovery is complete. */
        status = 0;
    }

    ble_gattc_disc_all_svcs_cb(entry, status, NULL);
}

void
ble_gattc_rx_read_group_type_entry(struct ble_hs_conn *conn,
                                   struct ble_att_read_group_type_adata *adata)
{
    struct ble_gatt_service service;
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;
    uint16_t uuid16;
    int cbrc;
    int rc;

    entry = ble_gattc_find(conn->bhc_handle, BLE_GATT_OP_DISC_ALL_SVCS, 1,
                           &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

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

    entry->disc_all_svcs.prev_handle = adata->end_group_handle;

    service.start_handle = adata->att_handle;
    service.end_handle = adata->end_group_handle;

    rc = 0;

done:
    cbrc = ble_gattc_disc_all_svcs_cb(entry, rc, &service);
    if (rc != 0 || cbrc != 0) {
        ble_gattc_entry_remove_free(entry, prev);
    }
}

void
ble_gattc_rx_read_group_type_complete(struct ble_hs_conn *conn, int rc)
{
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;

    entry = ble_gattc_find(conn->bhc_handle, BLE_GATT_OP_DISC_ALL_SVCS, 1,
                           &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    if (rc != 0 || entry->disc_all_svcs.prev_handle == 0xffff) {
        /* Error or all svcs discovered. */
        ble_gattc_disc_all_svcs_cb(entry, rc, NULL);
        ble_gattc_entry_remove_free(entry, prev);
    } else {
        /* Send follow-up request. */
        ble_gattc_entry_set_pending(entry);
    }
}

int
ble_gattc_disc_all_svcs(uint16_t conn_handle, ble_gatt_disc_svc_fn *cb,
                        void *cb_arg)
{
    struct ble_gattc_entry *entry;
    int rc;

    rc = ble_gattc_new_entry(conn_handle, BLE_GATT_OP_DISC_ALL_SVCS,
                             &entry);
    if (rc != 0) {
        return rc;
    }
    entry->disc_all_svcs.prev_handle = 0x0000;
    entry->disc_all_svcs.cb = cb;
    entry->disc_all_svcs.cb_arg = cb_arg;

    return 0;
}

/*****************************************************************************
 * $discover service by uuid                                                 *
 *****************************************************************************/

static int
ble_gattc_disc_svc_uuid_cb(struct ble_gattc_entry *entry, int status,
                              struct ble_gatt_service *service)
{
    int rc;

    if (entry->disc_svc_uuid.cb == NULL) {
        rc = 0;
    } else {
        rc = entry->disc_svc_uuid.cb(entry->conn_handle, status, service,
                                     entry->disc_svc_uuid.cb_arg);
    }

    return rc;
}

static int
ble_gattc_kick_disc_svc_uuid(struct ble_gattc_entry *entry)
{
    struct ble_att_find_type_value_req req;
    struct ble_hs_conn *conn;
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
        goto err;
    }

    req.bavq_start_handle = entry->disc_svc_uuid.prev_handle + 1;
    req.bavq_end_handle = 0xffff;
    req.bavq_attr_type = BLE_ATT_UUID_PRIMARY_SERVICE;

    rc = ble_att_clt_tx_find_type_value(conn, &req,
                                        entry->disc_svc_uuid.service_uuid, 16);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    if (ble_gattc_tx_postpone_chk(entry, rc)) {
        return BLE_HS_EAGAIN;
    }

    ble_gattc_disc_svc_uuid_cb(entry, rc, NULL);
    return rc;
}

static void
ble_gattc_err_disc_svc_uuid(struct ble_gattc_entry *entry, int status)
{
    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Discovery is complete. */
        status = 0;
    }

    ble_gattc_disc_svc_uuid_cb(entry, status, NULL);
}

void
ble_gattc_rx_find_type_value_hinfo(struct ble_hs_conn *conn,
                                   struct ble_att_find_type_value_hinfo *hinfo)
{
    struct ble_gatt_service service;
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;
    int rc;

    entry = ble_gattc_find(conn->bhc_handle, BLE_GATT_OP_DISC_SVC_UUID, 1,
                           &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    entry->disc_svc_uuid.prev_handle = hinfo->group_end_handle;

    service.start_handle = hinfo->attr_handle;
    service.end_handle = hinfo->group_end_handle;
    memcpy(service.uuid128, entry->disc_svc_uuid.service_uuid, 16);

    rc = ble_gattc_disc_svc_uuid_cb(entry, 0, &service);
    if (rc != 0) {
        ble_gattc_entry_remove_free(entry, prev);
    }
}

void
ble_gattc_rx_find_type_value_complete(struct ble_hs_conn *conn, int status)
{
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;

    entry = ble_gattc_find(conn->bhc_handle, BLE_GATT_OP_DISC_SVC_UUID, 1,
                           &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    if (status != 0 || entry->disc_svc_uuid.prev_handle == 0xffff) {
        /* Error or all svcs discovered. */
        ble_gattc_disc_svc_uuid_cb(entry, status, NULL);
        ble_gattc_entry_remove_free(entry, prev);
    } else {
        /* Send follow-up request. */
        ble_gattc_entry_set_pending(entry);
    }
}

int
ble_gattc_disc_svc_by_uuid(uint16_t conn_handle, void *service_uuid128,
                           ble_gatt_disc_svc_fn *cb, void *cb_arg)
{
    struct ble_gattc_entry *entry;
    int rc;

    rc = ble_gattc_new_entry(conn_handle, BLE_GATT_OP_DISC_SVC_UUID,
                             &entry);
    if (rc != 0) {
        return rc;
    }
    memcpy(entry->disc_svc_uuid.service_uuid, service_uuid128, 16);
    entry->disc_svc_uuid.prev_handle = 0x0000;
    entry->disc_svc_uuid.cb = cb;
    entry->disc_svc_uuid.cb_arg = cb_arg;

    return 0;
}

/*****************************************************************************
 * $find included svcs                                                   *
 *****************************************************************************/

static int
ble_gattc_find_inc_svcs_cb(struct ble_gattc_entry *entry, int status,
                           struct ble_gatt_service *service)
{
    int rc;

    if (entry->find_inc_svcs.cb == NULL) {
        rc = 0;
    } else {
        rc = entry->find_inc_svcs.cb(entry->conn_handle, status, service,
                                     entry->find_inc_svcs.cb_arg);
    }

    return rc;
}

static int
ble_gattc_kick_find_inc_svcs(struct ble_gattc_entry *entry)
{
    struct ble_att_read_type_req read_type_req;
    struct ble_att_read_req read_req;
    struct ble_hs_conn *conn;
    uint8_t uuid128[16];
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
        goto err;
    }

    if (entry->find_inc_svcs.cur_start == 0) {
        /* Find the next included service. */
        read_type_req.batq_start_handle = entry->find_inc_svcs.prev_handle + 1;
        read_type_req.batq_end_handle = entry->find_inc_svcs.end_handle;

        rc = ble_uuid_16_to_128(BLE_ATT_UUID_INCLUDE, uuid128);
        assert(rc == 0);

        rc = ble_att_clt_tx_read_type(conn, &read_type_req, uuid128);
    } else {
        /* Read the UUID of the previously found service. */
        read_req.barq_handle = entry->find_inc_svcs.cur_start;
        rc = ble_att_clt_tx_read(conn, &read_req);
    }
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    if (ble_gattc_tx_postpone_chk(entry, rc)) {
        return BLE_HS_EAGAIN;
    }

    ble_gattc_find_inc_svcs_cb(entry, rc, NULL);
    return rc;
}

static void
ble_gattc_err_find_inc_svcs(struct ble_gattc_entry *entry, int status)
{
    if (entry->find_inc_svcs.cur_start == 0 &&
        status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {

        /* Discovery is complete. */
        status = 0;
    }

    ble_gattc_find_inc_svcs_cb(entry, status, NULL);
}

static int
ble_gattc_find_inc_svcs_rx_read_rsp(struct ble_gattc_entry *entry,
                                    struct ble_hs_conn *conn, int status,
                                    void *value, int value_len)
{
    struct ble_gatt_service service;
    int rc;

    if (entry->find_inc_svcs.cur_start == 0) {
        /* Unexpected read response; terminate procedure. */
        rc = BLE_HS_EBADDATA; // XXX
        goto done;
    }

    if (status != 0) {
        rc = status;
        goto done;
    }

    if (value_len != 16) {
        /* Invalid UUID. */
        rc = BLE_HS_EBADDATA;
    }

    service.start_handle = entry->find_inc_svcs.cur_start;
    service.end_handle = entry->find_inc_svcs.cur_end;
    memcpy(service.uuid128, value, 16);

    /* We are done with this service; proceed to the next. */
    entry->find_inc_svcs.cur_start = 0;
    entry->find_inc_svcs.cur_end = 0;
    ble_gattc_entry_set_pending(entry);

    rc = 0;

done:
    ble_gattc_find_inc_svcs_cb(entry, rc, &service);
    return rc;
}

static int
ble_gattc_find_inc_svcs_rx_adata(struct ble_gattc_entry *entry,
                                 struct ble_hs_conn *conn,
                                 struct ble_att_read_type_adata *adata)
{
    struct ble_gatt_service service;
    uint16_t uuid16;
    int rc;

    if (entry->find_inc_svcs.cur_start != 0) {
        /* We only read one 128-bit UUID service at a time.  Ignore the
         * additional services in the response.
         */
        return 0;
    }

    entry->find_inc_svcs.prev_handle = adata->att_handle;

    switch (adata->value_len) {
    case BLE_GATTS_INC_SVC_LEN_NO_UUID:
        entry->find_inc_svcs.cur_start = le16toh(adata->value + 0);
        entry->find_inc_svcs.cur_end = le16toh(adata->value + 2);
        break;

    case BLE_GATTS_INC_SVC_LEN_UUID:
        service.start_handle = le16toh(adata->value + 0);
        service.end_handle = le16toh(adata->value + 2);
        uuid16 = le16toh(adata->value + 4);
        rc = ble_uuid_16_to_128(uuid16, service.uuid128);
        if (rc != 0) {
            return BLE_HS_EBADDATA;
        }

        rc = ble_gattc_find_inc_svcs_cb(entry, 0, &service);
        if (rc != 0) {
            return rc;
        }
        break;

    default:
        return BLE_HS_EBADDATA;
    }

    return 0;
}

static int
ble_gattc_find_inc_svcs_rx_complete(struct ble_gattc_entry *entry,
                                    struct ble_hs_conn *conn, int status)
{
    if (status != 0 || entry->find_inc_svcs.prev_handle == 0xffff) {
        /* Error or all svcs discovered. */
        ble_gattc_find_inc_svcs_cb(entry, status, NULL);
        return 1;
    } else {
        /* Send follow-up request. */
        ble_gattc_entry_set_pending(entry);
        return 0;
    }
}

int
ble_gattc_find_inc_svcs(uint16_t conn_handle, uint16_t start_handle,
                        uint16_t end_handle,
                        ble_gatt_disc_svc_fn *cb, void *cb_arg)
{
    struct ble_gattc_entry *entry;
    int rc;

    rc = ble_gattc_new_entry(conn_handle, BLE_GATT_OP_FIND_INC_SVCS, &entry);
    if (rc != 0) {
        return rc;
    }
    entry->find_inc_svcs.prev_handle = start_handle - 1;
    entry->find_inc_svcs.end_handle = end_handle;
    entry->find_inc_svcs.cb = cb;
    entry->find_inc_svcs.cb_arg = cb_arg;

    return 0;
}

/*****************************************************************************
 * $discover all characteristics                                             *
 *****************************************************************************/

static int
ble_gattc_disc_all_chrs_cb(struct ble_gattc_entry *entry, int status,
                           struct ble_gatt_chr *chr)
{
    int rc;

    if (entry->disc_all_chrs.cb == NULL) {
        rc = 0;
    } else {
        rc = entry->disc_all_chrs.cb(entry->conn_handle, status, chr,
                                     entry->disc_all_chrs.cb_arg);
    }

    return rc;
}

static int
ble_gattc_kick_disc_all_chrs(struct ble_gattc_entry *entry)
{
    struct ble_att_read_type_req req;
    struct ble_hs_conn *conn;
    uint8_t uuid128[16];
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
        goto err;
    }

    rc = ble_uuid_16_to_128(BLE_ATT_UUID_CHARACTERISTIC, uuid128);
    assert(rc == 0);

    req.batq_start_handle = entry->disc_all_chrs.prev_handle + 1;
    req.batq_end_handle = entry->disc_all_chrs.end_handle;

    rc = ble_att_clt_tx_read_type(conn, &req, uuid128);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    if (ble_gattc_tx_postpone_chk(entry, rc)) {
        return BLE_HS_EAGAIN;
    }

    ble_gattc_disc_all_chrs_cb(entry, rc, NULL);
    return rc;
}

static void
ble_gattc_err_disc_all_chrs(struct ble_gattc_entry *entry, int status)
{
    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Discovery is complete. */
        status = 0;
    }

    ble_gattc_disc_all_chrs_cb(entry, status, NULL);
}

static int
ble_gattc_disc_all_chrs_rx_adata(struct ble_gattc_entry *entry,
                                 struct ble_hs_conn *conn,
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

    entry->disc_all_chrs.prev_handle = adata->att_handle;

    chr.properties = adata->value[0];
    chr.value_handle = le16toh(adata->value + 1);

    rc = 0;

done:
    cbrc = ble_gattc_disc_all_chrs_cb(entry, rc, &chr);
    if (rc == 0) {
        rc = cbrc;
    }

    return rc;
}

static int
ble_gattc_disc_all_chrs_rx_complete(struct ble_gattc_entry *entry,
                                    struct ble_hs_conn *conn, int status)
{
    if (status != 0 || entry->disc_all_chrs.prev_handle ==
                       entry->disc_all_chrs.end_handle) {
        /* Error or all svcs discovered. */
        ble_gattc_disc_all_chrs_cb(entry, status, NULL);
        return 1;
    } else {
        /* Send follow-up request. */
        ble_gattc_entry_set_pending(entry);
        return 0;
    }
}

int
ble_gattc_disc_all_chrs(uint16_t conn_handle, uint16_t start_handle,
                        uint16_t end_handle, ble_gatt_chr_fn *cb,
                        void *cb_arg)
{
    struct ble_gattc_entry *entry;
    int rc;

    rc = ble_gattc_new_entry(conn_handle, BLE_GATT_OP_DISC_ALL_CHRS, &entry);
    if (rc != 0) {
        return rc;
    }
    entry->disc_all_chrs.prev_handle = start_handle - 1;
    entry->disc_all_chrs.end_handle = end_handle;
    entry->disc_all_chrs.cb = cb;
    entry->disc_all_chrs.cb_arg = cb_arg;

    return 0;
}

/*****************************************************************************
 * $discover characteristic by uuid                                          *
 *****************************************************************************/

static int
ble_gattc_disc_chr_uuid_cb(struct ble_gattc_entry *entry, int status,
                           struct ble_gatt_chr *chr)
{
    int rc;

    if (entry->disc_chr_uuid.cb == NULL) {
        rc = 0;
    } else {
        rc = entry->disc_chr_uuid.cb(entry->conn_handle, status, chr,
                                     entry->disc_chr_uuid.cb_arg);
    }

    return rc;
}

static int
ble_gattc_kick_disc_chr_uuid(struct ble_gattc_entry *entry)
{
    struct ble_att_read_type_req req;
    struct ble_hs_conn *conn;
    uint8_t uuid128[16];
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
        goto err;
    }

    rc = ble_uuid_16_to_128(BLE_ATT_UUID_CHARACTERISTIC, uuid128);
    assert(rc == 0);

    req.batq_start_handle = entry->disc_chr_uuid.prev_handle + 1;
    req.batq_end_handle = entry->disc_chr_uuid.end_handle;

    rc = ble_att_clt_tx_read_type(conn, &req, uuid128);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    if (ble_gattc_tx_postpone_chk(entry, rc)) {
        return BLE_HS_EAGAIN;
    }

    ble_gattc_disc_chr_uuid_cb(entry, rc, NULL);
    return rc;
}

static void
ble_gattc_err_disc_chr_uuid(struct ble_gattc_entry *entry, int status)
{
    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Discovery is complete. */
        status = 0;
    }

    ble_gattc_disc_chr_uuid_cb(entry, status, NULL);
}

static int
ble_gattc_disc_chr_uuid_rx_adata(struct ble_gattc_entry *entry,
                                 struct ble_hs_conn *conn,
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

    entry->disc_chr_uuid.prev_handle = adata->att_handle;

    chr.properties = adata->value[0];
    chr.value_handle = le16toh(adata->value + 1);

    rc = 0;

done:
    if (memcmp(chr.uuid128, entry->disc_chr_uuid.chr_uuid, 16) == 0) {
        cbrc = ble_gattc_disc_chr_uuid_cb(entry, rc, &chr);
        if (rc == 0) {
            rc = cbrc;
        }
    }

    return rc;
}

static int
ble_gattc_disc_chr_uuid_rx_complete(struct ble_gattc_entry *entry,
                                    struct ble_hs_conn *conn, int status)
{
    if (status != 0 || entry->disc_chr_uuid.prev_handle ==
                       entry->disc_chr_uuid.end_handle) {
        /* Error or all svcs discovered. */
        ble_gattc_disc_chr_uuid_cb(entry, status, NULL);
        return 1;
    } else {
        /* Send follow-up request. */
        ble_gattc_entry_set_pending(entry);
        return 0;
    }
}

int
ble_gattc_disc_chrs_by_uuid(uint16_t conn_handle, uint16_t start_handle,
                            uint16_t end_handle, void *uuid128,
                            ble_gatt_chr_fn *cb, void *cb_arg)
{
    struct ble_gattc_entry *entry;
    int rc;

    rc = ble_gattc_new_entry(conn_handle, BLE_GATT_OP_DISC_CHRS_UUID, &entry);
    if (rc != 0) {
        return rc;
    }
    memcpy(entry->disc_chr_uuid.chr_uuid, uuid128, 16);
    entry->disc_chr_uuid.prev_handle = start_handle - 1;
    entry->disc_chr_uuid.end_handle = end_handle;
    entry->disc_chr_uuid.cb = cb;
    entry->disc_chr_uuid.cb_arg = cb_arg;

    return 0;
}

/*****************************************************************************
 * $discover all characteristic descriptors                                  *
 *****************************************************************************/

static int
ble_gattc_disc_all_dscs_cb(struct ble_gattc_entry *entry, int status,
                           uint16_t dsc_handle, uint8_t *dsc_uuid128)
{
    int rc;

    if (entry->disc_all_dscs.cb == NULL) {
        rc = 0;
    } else {
        rc = entry->disc_all_dscs.cb(entry->conn_handle, status,
                                     entry->disc_all_dscs.chr_val_handle,
                                     dsc_handle, dsc_uuid128,
                                     entry->disc_all_dscs.cb_arg);
    }

    return rc;
}

static int
ble_gattc_kick_disc_all_dscs(struct ble_gattc_entry *entry)
{
    struct ble_att_find_info_req req;
    struct ble_hs_conn *conn;
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
        goto err;
    }

    req.bafq_start_handle = entry->disc_all_dscs.prev_handle + 1;
    req.bafq_end_handle = entry->disc_all_dscs.end_handle;

    rc = ble_att_clt_tx_find_info(conn, &req);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    if (ble_gattc_tx_postpone_chk(entry, rc)) {
        return BLE_HS_EAGAIN;
    }

    ble_gattc_disc_all_dscs_cb(entry, rc, 0, NULL);
    return rc;
}

static void
ble_gattc_err_disc_all_dscs(struct ble_gattc_entry *entry, int status)
{
    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Discovery is complete. */
        status = 0;
    }

    ble_gattc_disc_all_dscs_cb(entry, status, 0, NULL);
}

void
ble_gattc_rx_find_info_entry(struct ble_hs_conn *conn,
                             struct ble_att_find_info_idata *idata)
{
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;
    int rc;

    entry = ble_gattc_find(conn->bhc_handle, BLE_GATT_OP_DISC_ALL_DSCS, 1,
                           &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    rc = ble_gattc_disc_all_dscs_cb(entry, 0, idata->attr_handle,
                                    idata->uuid128);
    if (rc != 0) {
        ble_gattc_entry_remove_free(entry, prev);
    } else {
        entry->disc_all_dscs.prev_handle = idata->attr_handle;
    }
}

void
ble_gattc_rx_find_info_complete(struct ble_hs_conn *conn, int status)
{
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;

    entry = ble_gattc_find(conn->bhc_handle, BLE_GATT_OP_DISC_ALL_DSCS, 1,
                           &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    if (status != 0 || entry->disc_all_dscs.prev_handle ==
                       entry->disc_all_dscs.end_handle) {
        /* Error or all descriptors discovered. */
        ble_gattc_disc_all_dscs_cb(entry, status, 0, NULL);
        ble_gattc_entry_remove_free(entry, prev);
    } else {
        /* Send follow-up request. */
        ble_gattc_entry_set_pending(entry);
    }
}

int
ble_gattc_disc_all_dscs(uint16_t conn_handle, uint16_t chr_val_handle,
                        uint16_t chr_end_handle,
                        ble_gatt_dsc_fn *cb, void *cb_arg)
{
    struct ble_gattc_entry *entry;
    int rc;

    rc = ble_gattc_new_entry(conn_handle, BLE_GATT_OP_DISC_ALL_DSCS, &entry);
    if (rc != 0) {
        return rc;
    }
    entry->disc_all_dscs.chr_val_handle = chr_val_handle;
    entry->disc_all_dscs.prev_handle = chr_val_handle;
    entry->disc_all_dscs.end_handle = chr_end_handle;
    entry->disc_all_dscs.cb = cb;
    entry->disc_all_dscs.cb_arg = cb_arg;

    return 0;
}

/*****************************************************************************
 * $read                                                                     *
 *****************************************************************************/

static int
ble_gattc_read_cb(struct ble_gattc_entry *entry, int status,
                  struct ble_gatt_attr *attr)
{
    int rc;

    if (entry->read.cb == NULL) {
        rc = 0;
    } else {
        rc = entry->read.cb(entry->conn_handle, status, attr,
                            entry->read.cb_arg);
    }

    return rc;
}

static int
ble_gattc_kick_read(struct ble_gattc_entry *entry)
{
    struct ble_att_read_req req;
    struct ble_hs_conn *conn;
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
        goto err;
    }

    req.barq_handle = entry->read.handle;
    rc = ble_att_clt_tx_read(conn, &req);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    if (ble_gattc_tx_postpone_chk(entry, rc)) {
        return BLE_HS_EAGAIN;
    }

    ble_gattc_read_cb(entry, rc, NULL);
    return rc;
}

static void
ble_gattc_err_read(struct ble_gattc_entry *entry, int status)
{
    ble_gattc_read_cb(entry, status, NULL);
}

static int
ble_gattc_read_rx_read_rsp(struct ble_gattc_entry *entry,
                           struct ble_hs_conn *conn, int status,
                           void *value, int value_len)
{
    struct ble_gatt_attr attr;

    attr.handle = entry->read.handle;
    attr.value_len = value_len;
    attr.value = value;

    ble_gattc_read_cb(entry, status, &attr);

    /* The read operation only has a single request / response exchange. */
    return 1;
}

int
ble_gattc_read(uint16_t conn_handle, uint16_t attr_handle,
              ble_gatt_attr_fn *cb, void *cb_arg)
{
    struct ble_gattc_entry *entry;
    int rc;

    rc = ble_gattc_new_entry(conn_handle, BLE_GATT_OP_READ, &entry);
    if (rc != 0) {
        return rc;
    }
    entry->read.handle = attr_handle;
    entry->read.cb = cb;
    entry->read.cb_arg = cb_arg;

    return 0;
}

/*****************************************************************************
 * $read                                                                     *
 *****************************************************************************/

static int
ble_gattc_read_uuid_cb(struct ble_gattc_entry *entry, int status,
                       struct ble_gatt_attr *attr)
{
    int rc;

    if (entry->read_uuid.cb == NULL) {
        rc = 0;
    } else {
        rc = entry->read_uuid.cb(entry->conn_handle, status, attr,
                                 entry->read_uuid.cb_arg);
    }

    return rc;
}

static int
ble_gattc_kick_read_uuid(struct ble_gattc_entry *entry)
{
    struct ble_att_read_type_req req;
    struct ble_hs_conn *conn;
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
        goto err;
    }

    req.batq_start_handle = entry->read_uuid.prev_handle + 1;
    req.batq_end_handle = entry->read_uuid.end_handle;
    rc = ble_att_clt_tx_read_type(conn, &req, entry->read_uuid.uuid128);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    if (ble_gattc_tx_postpone_chk(entry, rc)) {
        return BLE_HS_EAGAIN;
    }

    ble_gattc_read_uuid_cb(entry, rc, NULL);
    return rc;
}

static void
ble_gattc_err_read_uuid(struct ble_gattc_entry *entry, int status)
{
    if (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        /* Read is complete. */
        status = 0;
    }
    ble_gattc_read_uuid_cb(entry, status, NULL);
}

static int
ble_gattc_read_uuid_rx_adata(struct ble_gattc_entry *entry,
                             struct ble_hs_conn *conn,
                             struct ble_att_read_type_adata *adata)
{
    struct ble_gatt_attr attr;
    int rc;

    attr.handle = adata->att_handle;
    attr.value_len = adata->value_len;
    attr.value = adata->value;

    rc = ble_gattc_read_uuid_cb(entry, 0, &attr);
    if (rc != 0) {
        return rc;
    }

    entry->read_uuid.prev_handle = adata->att_handle;

    return 0;
}

static int
ble_gattc_read_uuid_rx_complete(struct ble_gattc_entry *entry,
                                struct ble_hs_conn *conn,
                                int status)
{
    if (status != 0 ||
        entry->read_uuid.prev_handle == entry->read_uuid.end_handle) {

        /* Error or entire range read. */
        ble_gattc_read_uuid_cb(entry, status, NULL);
        return 1;
    } else {
        /* Send follow-up request. */
        ble_gattc_entry_set_pending(entry);
        return 0;
    }
}

int
ble_gattc_read_uuid(uint16_t conn_handle, uint16_t start_handle,
                    uint16_t end_handle, void *uuid128,
                    ble_gatt_attr_fn *cb, void *cb_arg)
{
    struct ble_gattc_entry *entry;
    int rc;

    rc = ble_gattc_new_entry(conn_handle, BLE_GATT_OP_READ_UUID, &entry);
    if (rc != 0) {
        return rc;
    }
    entry->read_uuid.prev_handle = start_handle - 1;
    entry->read_uuid.end_handle = end_handle;
    memcpy(entry->read_uuid.uuid128, uuid128, 16);
    entry->read_uuid.cb = cb;
    entry->read_uuid.cb_arg = cb_arg;

    return 0;
}

/*****************************************************************************
 * @write no response                                                        *
 *****************************************************************************/

static int
ble_gattc_write_cb(struct ble_gattc_entry *entry, int status)
{
    int rc;

    if (entry->write.cb == NULL) {
        rc = 0;
    } else {
        rc = entry->write.cb(entry->conn_handle, status, &entry->write.attr,
                             entry->write.cb_arg);
    }

    return rc;
}

static int
ble_gattc_kick_write_no_rsp(struct ble_gattc_entry *entry)
{
    struct ble_att_write_req req;
    struct ble_hs_conn *conn;
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
        goto err;
    }

    req.bawq_handle = entry->write.attr.handle;
    rc = ble_att_clt_tx_write_cmd(conn, &req, entry->write.attr.value,
                                  entry->write.attr.value_len);
    if (rc != 0) {
        goto err;
    }

    /* No response expected; call callback immediately and return nonzero to
     * indicate the entry should be freed.
     */
    ble_gattc_write_cb(entry, 0);

    return 1;

err:
    if (ble_gattc_tx_postpone_chk(entry, rc)) {
        return BLE_HS_EAGAIN;
    }

    ble_gattc_write_cb(entry, rc);
    return rc;
}

int
ble_gattc_write_no_rsp(uint16_t conn_handle, uint16_t attr_handle, void *value,
                       uint16_t value_len, ble_gatt_attr_fn *cb, void *cb_arg)
{
    struct ble_gattc_entry *entry;
    int rc;

    rc = ble_gattc_new_entry(conn_handle, BLE_GATT_OP_WRITE_NO_RSP, &entry);
    if (rc != 0) {
        return rc;
    }

    entry->write.attr.handle = attr_handle;
    entry->write.attr.value = value;
    entry->write.attr.value_len = value_len;
    entry->write.cb = cb;
    entry->write.cb_arg = cb_arg;

    return 0;
}

/*****************************************************************************
 * @write                                                                    *
 *****************************************************************************/

static int
ble_gattc_kick_write(struct ble_gattc_entry *entry)
{
    struct ble_att_write_req req;
    struct ble_hs_conn *conn;
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
        goto err;
    }

    req.bawq_handle = entry->write.attr.handle;
    rc = ble_att_clt_tx_write_req(conn, &req, entry->write.attr.value,
                                  entry->write.attr.value_len);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    if (ble_gattc_tx_postpone_chk(entry, rc)) {
        return BLE_HS_EAGAIN;
    }

    ble_gattc_write_cb(entry, rc);
    return rc;
}

static void
ble_gattc_err_write(struct ble_gattc_entry *entry, int status)
{
    ble_gattc_write_cb(entry, status);
}

void
ble_gattc_rx_write_rsp(struct ble_hs_conn *conn)
{
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;

    entry = ble_gattc_find(conn->bhc_handle, BLE_GATT_OP_WRITE, 1, &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    ble_gattc_write_cb(entry, 0);

    /* The write operation only has a single request / response exchange. */
    ble_gattc_entry_remove_free(entry, prev);
}

int
ble_gattc_write(uint16_t conn_handle, uint16_t attr_handle, void *value,
                uint16_t value_len, ble_gatt_attr_fn *cb, void *cb_arg)
{
    struct ble_gattc_entry *entry;
    int rc;

    rc = ble_gattc_new_entry(conn_handle, BLE_GATT_OP_WRITE, &entry);
    if (rc != 0) {
        return rc;
    }

    entry->write.attr.handle = attr_handle;
    entry->write.attr.value = value;
    entry->write.attr.value_len = value_len;
    entry->write.cb = cb;
    entry->write.cb_arg = cb_arg;

    return 0;
}

/*****************************************************************************
 * $notify                                                                   *
 *****************************************************************************/

int
ble_gattc_notify(struct ble_hs_conn *conn, uint16_t chr_val_handle)
{
    struct ble_att_svr_access_ctxt ctxt;
    struct ble_att_notify_req req;
    int rc;

    rc = ble_att_svr_read_handle(NULL, chr_val_handle, &ctxt, NULL);
    if (rc != 0) {
        return rc;
    }

    req.banq_handle = chr_val_handle;
    rc = ble_att_clt_tx_notify(conn, &req, ctxt.attr_data, ctxt.attr_len);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $indicate                                                                   *
 *****************************************************************************/

static int
ble_gattc_indicate_cb(struct ble_gattc_entry *entry, int status)
{
    int rc;

    if (entry->indicate.cb == NULL) {
        rc = 0;
    } else {
        rc = entry->indicate.cb(entry->conn_handle, status,
                                &entry->indicate.attr, entry->indicate.cb_arg);
    }

    return rc;
}

static int
ble_gattc_kick_indicate(struct ble_gattc_entry *entry)
{
    struct ble_att_svr_access_ctxt ctxt;
    struct ble_att_indicate_req req;
    struct ble_hs_conn *conn;
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
        goto err;
    }

    rc = ble_att_svr_read_handle(NULL, entry->indicate.attr.handle, &ctxt,
                                 NULL);
    if (rc != 0) {
        goto err;
    }
    entry->indicate.attr.value = ctxt.attr_data;
    entry->indicate.attr.value_len = ctxt.attr_len;

    req.baiq_handle = entry->indicate.attr.handle;
    rc = ble_att_clt_tx_indicate(conn, &req, entry->indicate.attr.value,
                                 entry->indicate.attr.value_len);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    if (ble_gattc_tx_postpone_chk(entry, rc)) {
        return BLE_HS_EAGAIN;
    }

    ble_gattc_indicate_cb(entry, rc);
    return rc;
}

static void
ble_gattc_err_indicate(struct ble_gattc_entry *entry, int status)
{
    ble_gattc_indicate_cb(entry, status);
}

void
ble_gattc_rx_indicate_rsp(struct ble_hs_conn *conn)
{
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;

    entry = ble_gattc_find(conn->bhc_handle, BLE_GATT_OP_INDICATE, 1, &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    /* Now that the confirmation has been received, we can send any subsequent
     * indication.
     */
    conn->bhc_gatt_svr.flags &= ~BLE_GATTS_CONN_F_INDICATION_TXED;

    ble_gattc_indicate_cb(entry, 0);

    /* The indicate operation only has a single request / response exchange. */
    ble_gattc_entry_remove_free(entry, prev);

    /* Send the next indication if one is pending. */
    ble_gatts_send_notifications(conn);
}

int
ble_gattc_indicate(uint16_t conn_handle, uint16_t chr_val_handle,
                   ble_gatt_attr_fn *cb, void *cb_arg)
{
    struct ble_gattc_entry *entry;
    int rc;

    rc = ble_gattc_new_entry(conn_handle, BLE_GATT_OP_INDICATE, &entry);
    if (rc != 0) {
        return rc;
    }

    entry->indicate.attr.handle = chr_val_handle;
    entry->indicate.cb = cb;
    entry->indicate.cb_arg = cb_arg;

    return 0;
}

/*****************************************************************************
 * $rx                                                                       *
 *****************************************************************************/

void
ble_gattc_rx_err(uint16_t conn_handle, struct ble_att_error_rsp *rsp)
{
    const struct ble_gattc_dispatch_entry *dispatch;
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;

    entry = ble_gattc_find(conn_handle, BLE_GATT_OP_NONE, 1, &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    dispatch = ble_gattc_dispatch_get(entry->op);
    if (dispatch->err_cb != NULL) {
        dispatch->err_cb(entry, BLE_HS_ERR_ATT_BASE + rsp->baep_error_code);
    }

    ble_gattc_entry_remove_free(entry, prev);
}

void
ble_gattc_rx_read_type_adata(struct ble_hs_conn *conn,
                             struct ble_att_read_type_adata *adata)
{
    const struct ble_gattc_rx_adata_entry *rx_entry;
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;
    int rc;

    entry = ble_gattc_find(conn->bhc_handle, BLE_GATT_OP_NONE, 1, &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    BLE_GATTC_RX_ENTRY_FIND(entry->op, ble_gattc_rx_read_type_elem_entries,
                            rx_entry);
    if (rx_entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    rc = rx_entry->cb(entry, conn, adata);
    if (rc != 0) {
        ble_gattc_entry_remove_free(entry, prev);
    }
}

void
ble_gattc_rx_read_type_complete(struct ble_hs_conn *conn, int status)
{
    const struct ble_gattc_rx_complete_entry *rx_entry;
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;
    int rc;

    entry = ble_gattc_find(conn->bhc_handle, BLE_GATT_OP_NONE, 1, &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    BLE_GATTC_RX_ENTRY_FIND(entry->op, ble_gattc_rx_read_type_complete_entries,
                            rx_entry);
    if (rx_entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    rc = rx_entry->cb(entry, conn, status);
    if (rc != 0) {
        ble_gattc_entry_remove_free(entry, prev);
    }
}

void
ble_gattc_rx_read_rsp(struct ble_hs_conn *conn, int status, void *value,
                      int value_len)
{
    const struct ble_gattc_rx_attr_entry *rx_entry;
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;
    int rc;

    entry = ble_gattc_find(conn->bhc_handle, BLE_GATT_OP_NONE, 1, &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }
    BLE_GATTC_RX_ENTRY_FIND(entry->op, ble_gattc_rx_read_rsp_entries,
                            rx_entry);
    if (rx_entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    rc = rx_entry->cb(entry, conn, status, value, value_len);
    if (rc != 0) {
        ble_gattc_entry_remove_free(entry, prev);
    }
}

/*****************************************************************************
 * $misc                                                                     *
 *****************************************************************************/

void
ble_gattc_wakeup(void)
{
    const struct ble_gattc_dispatch_entry *dispatch;
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;
    struct ble_gattc_entry *next;
    int rc;

    prev = NULL;
    entry = STAILQ_FIRST(&ble_gattc_list);
    while (entry != NULL) {
        next = STAILQ_NEXT(entry, next);

        if (entry->flags & BLE_GATT_ENTRY_F_PENDING) {
            dispatch = ble_gattc_dispatch_get(entry->op);
            rc = dispatch->kick_cb(entry);
            switch (rc) {
            case 0:
                /* Transmit succeeded.  Response expected. */
                ble_gattc_entry_set_expecting(entry, prev);
                /* Current entry got moved to back; old prev still valid. */
                break;

            case BLE_HS_EAGAIN:
                /* Transmit failed due to resource shortage.  Reschedule. */
                entry->flags &= ~BLE_GATT_ENTRY_F_PENDING;
                /* Current entry remains; reseat prev. */
                prev = entry;
                break;

            default:
                /* Transmit failed.  Abort procedure. */
                ble_gattc_entry_remove_free(entry, prev);
                /* Current entry removed; old prev still valid. */
                break;
            }
        } else {
            prev = entry;
        }

        entry = next;
    }
}

/**
 * Called when a BLE connection ends.  Frees all GATT resources associated with
 * the connection and cancels all relevant pending and in-progress GATT
 * procedures.
 */
void
ble_gattc_connection_broken(uint16_t conn_handle)
{
    const struct ble_gattc_dispatch_entry *dispatch;
    struct ble_gattc_entry *entry;
    struct ble_gattc_entry *prev;

    while (1) {
        entry = ble_gattc_find(conn_handle, BLE_GATT_OP_NONE, 0, &prev);
        if (entry == NULL) {
            break;
        }

        dispatch = ble_gattc_dispatch_get(entry->op);
        dispatch->err_cb(entry, BLE_HS_ENOTCONN);

        ble_gattc_entry_remove_free(entry, prev);
    }
}

/**
 * Called when a BLE connection transitions into a transmittable state.  Wakes
 * up all congested GATT procedures associated with the connection.
 */
void
ble_gattc_connection_txable(uint16_t conn_handle)
{
    struct ble_gattc_entry *entry;

    STAILQ_FOREACH(entry, &ble_gattc_list, next) {
        if (entry->conn_handle == conn_handle &&
            entry->flags & BLE_GATT_ENTRY_F_CONGESTED) {

            entry->flags &= ~BLE_GATT_ENTRY_F_CONGESTED;
            if (ble_gattc_entry_can_pend(entry)) {
                ble_gattc_entry_set_pending(entry);
            }
        }
    }
}

int
ble_gattc_any_jobs(void)
{
    return !STAILQ_EMPTY(&ble_gattc_list);
}

/**
 * XXX This function only exists because we can't set a timer before the OS
 * starts.  Maybe the OS issue can be fixed.
 */
void
ble_gattc_started(void)
{
    int rc;

    rc = os_callout_reset(&ble_gattc_heartbeat_timer.cf_c,
                          BLE_GATT_HEARTBEAT_PERIOD * OS_TICKS_PER_SEC / 1000);
    assert(rc == 0);
}

int
ble_gattc_init(void)
{
    int rc;

    free(ble_gattc_entry_mem);

    ble_gattc_entry_mem = malloc(
        OS_MEMPOOL_BYTES(BLE_GATT_NUM_ENTRIES,
                         sizeof (struct ble_gattc_entry)));
    if (ble_gattc_entry_mem == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = os_mempool_init(&ble_gattc_entry_pool,
                         BLE_GATT_NUM_ENTRIES,
                         sizeof (struct ble_gattc_entry),
                         ble_gattc_entry_mem,
                         "ble_gattc_entry_pool");
    if (rc != 0) {
        goto err;
    }

    STAILQ_INIT(&ble_gattc_list);

    os_callout_func_init(&ble_gattc_heartbeat_timer, &ble_hs_evq,
                         ble_gattc_heartbeat, NULL);

    return 0;

err:
    free(ble_gattc_entry_mem);
    ble_gattc_entry_mem = NULL;

    return rc;
}
