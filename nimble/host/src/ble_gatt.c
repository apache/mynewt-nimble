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
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "ble_hs_uuid.h"
#include "ble_hs_conn.h"
#include "ble_att_cmd.h"
#include "ble_att.h"

#define BLE_ATT_UUID_PRIMARY_SERVICE    0x2800

struct ble_gatt_entry {
    STAILQ_ENTRY(ble_gatt_entry) next;

    uint8_t op;
    uint8_t flags;
    uint16_t conn_handle;
    union {
        struct {
            int (*cb)(int status, uint16_t conn_handle, void *arg);
            void *cb_arg;
        } mtu;

        struct {
            uint16_t next_handle;
            uint16_t end_handle;

            int (*cb)(int status, uint16_t conn_handle, void *arg);
            void *cb_arg;
        } find_info;

        struct {
            uint16_t prev_handle;
            ble_gatt_disc_service_fn *cb;
            void *cb_arg;
        } disc_all_services;

        struct {
            uint8_t service_uuid[16];
            uint16_t prev_handle;
            ble_gatt_disc_service_fn *cb;
            void *cb_arg;
        } disc_service_uuid;
    };
};

#define BLE_GATT_OP_NONE                        UINT8_MAX
#define BLE_GATT_OP_MTU                         0
#define BLE_GATT_OP_FIND_INFO                   1
#define BLE_GATT_OP_DISC_ALL_SERVICES           2
#define BLE_GATT_OP_DISC_SERVICE_UUID           3
#define BLE_GATT_OP_MAX                         4

typedef int ble_gatt_kick_fn(struct ble_gatt_entry *entry);
typedef int ble_gatt_rx_err_fn(struct ble_gatt_entry *entry,
                               struct ble_hs_conn *conn,
                               struct ble_att_error_rsp *rsp);

static int ble_gatt_kick_mtu(struct ble_gatt_entry *entry);
static int ble_gatt_kick_find_info(struct ble_gatt_entry *entry);
static int ble_gatt_kick_disc_all_services(struct ble_gatt_entry *entry);
static int ble_gatt_kick_disc_service_uuid(struct ble_gatt_entry *entry);

static int ble_gatt_rx_err_disc_all_services(struct ble_gatt_entry *entry,
                                             struct ble_hs_conn *conn,
                                             struct ble_att_error_rsp *rsp);
static int ble_gatt_rx_err_disc_service_uuid(struct ble_gatt_entry *entry,
                                             struct ble_hs_conn *conn,
                                             struct ble_att_error_rsp *rsp);

struct ble_gatt_dispatch_entry {
    ble_gatt_kick_fn *kick_cb;
    ble_gatt_rx_err_fn *rx_err_cb;
};

static const struct ble_gatt_dispatch_entry
    ble_gatt_dispatch[BLE_GATT_OP_MAX] = {

    [BLE_GATT_OP_MTU] = {
        .kick_cb = ble_gatt_kick_mtu,
        .rx_err_cb = NULL,
    },
    [BLE_GATT_OP_FIND_INFO] = {
        .kick_cb = ble_gatt_kick_find_info,
        .rx_err_cb = NULL,
    },
    [BLE_GATT_OP_DISC_ALL_SERVICES] = {
        .kick_cb = ble_gatt_kick_disc_all_services,
        .rx_err_cb = ble_gatt_rx_err_disc_all_services,
    },
    [BLE_GATT_OP_DISC_SERVICE_UUID] = {
        .kick_cb = ble_gatt_kick_disc_service_uuid,
        .rx_err_cb = ble_gatt_rx_err_disc_service_uuid,
    },
};

#define BLE_GATT_ENTRY_F_PENDING    0x01
#define BLE_GATT_ENTRY_F_EXPECTING  0x02

#define BLE_GATT_NUM_ENTRIES          4
static void *ble_gatt_entry_mem;
static struct os_mempool ble_gatt_entry_pool;

static STAILQ_HEAD(, ble_gatt_entry) ble_gatt_list;

static const struct ble_gatt_dispatch_entry *
ble_gatt_dispatch_get(uint8_t op)
{
    assert(op < BLE_GATT_OP_MAX);
    return ble_gatt_dispatch + op;
}

static struct ble_gatt_entry *
ble_gatt_entry_alloc(void)
{
    struct ble_gatt_entry *entry;

    entry = os_memblock_get(&ble_gatt_entry_pool);
    if (entry != NULL) {
        memset(entry, 0, sizeof *entry);
    }

    return entry;
}

static void
ble_gatt_entry_free(struct ble_gatt_entry *entry)
{
    int rc;

    rc = os_memblock_put(&ble_gatt_entry_pool, entry);
    assert(rc == 0);
}

static void
ble_gatt_entry_remove(struct ble_gatt_entry *entry,
                      struct ble_gatt_entry *prev)
{
    if (prev == NULL) {
        assert(STAILQ_FIRST(&ble_gatt_list) == entry);
        STAILQ_REMOVE_HEAD(&ble_gatt_list, next);
    } else {
        STAILQ_NEXT(prev, next) = STAILQ_NEXT(entry, next);
    }
}

static void
ble_gatt_entry_remove_free(struct ble_gatt_entry *entry,
                           struct ble_gatt_entry *prev)
{
    ble_gatt_entry_remove(entry, prev);
    ble_gatt_entry_free(entry);
}

static int
ble_gatt_entry_matches(struct ble_gatt_entry *entry, uint16_t conn_handle,
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

static struct ble_gatt_entry *
ble_gatt_find(uint16_t conn_handle, uint8_t att_op, int expecting_only,
              struct ble_gatt_entry **out_prev)
{
    struct ble_gatt_entry *entry;
    struct ble_gatt_entry *prev;

    prev = NULL;
    STAILQ_FOREACH(entry, &ble_gatt_list, next) {
        if (ble_gatt_entry_matches(entry, conn_handle, att_op,
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
ble_gatt_entry_set_pending(struct ble_gatt_entry *entry)
{
    assert(!(entry->flags & BLE_GATT_ENTRY_F_PENDING));

    entry->flags &= ~BLE_GATT_ENTRY_F_EXPECTING;
    entry->flags |= BLE_GATT_ENTRY_F_PENDING;
    ble_hs_kick_gatt();
}

static void
ble_gatt_entry_set_expecting(struct ble_gatt_entry *entry,
                             struct ble_gatt_entry *prev)
{
    assert(!(entry->flags & BLE_GATT_ENTRY_F_EXPECTING));

    ble_gatt_entry_remove(entry, prev);
    entry->flags &= ~BLE_GATT_ENTRY_F_PENDING;
    entry->flags |= BLE_GATT_ENTRY_F_EXPECTING;
    STAILQ_INSERT_TAIL(&ble_gatt_list, entry, next);
}

static int
ble_gatt_new_entry(uint16_t conn_handle, struct ble_gatt_entry **entry)
{
    struct ble_hs_conn *conn;

    *entry = NULL;

    /* Ensure we have a connection with the specified handle. */
    conn = ble_hs_conn_find(conn_handle);
    if (conn == NULL) {
        return ENOTCONN;
    }

    *entry = ble_gatt_entry_alloc();
    if (*entry == NULL) {
        return ENOMEM;
    }

    memset(*entry, 0, sizeof **entry);
    (*entry)->conn_handle = conn_handle;

    STAILQ_INSERT_TAIL(&ble_gatt_list, *entry, next);

    ble_gatt_entry_set_pending(*entry);

    return 0;
}

static int
ble_gatt_kick_mtu(struct ble_gatt_entry *entry)
{
    struct ble_att_mtu_cmd req;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        return ENOTCONN;
    }

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    assert(chan != NULL);

    req.bamc_mtu = chan->blc_my_mtu;
    rc = ble_att_clt_tx_mtu(conn, &req);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_gatt_kick_find_info(struct ble_gatt_entry *entry)
{
    struct ble_att_find_info_req req;
    struct ble_hs_conn *conn;
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        return ENOTCONN;
    }

    req.bafq_start_handle = entry->find_info.next_handle;
    req.bafq_end_handle = entry->find_info.end_handle;
    rc = ble_att_clt_tx_find_info(conn, &req);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_gatt_kick_disc_all_services(struct ble_gatt_entry *entry)
{
    struct ble_att_read_group_type_req req;
    struct ble_hs_conn *conn;
    uint8_t uuid128[16];
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        return ENOTCONN;
    }

    rc = ble_hs_uuid_from_16bit(BLE_ATT_UUID_PRIMARY_SERVICE, uuid128);
    assert(rc == 0);

    req.bagq_start_handle = entry->disc_all_services.prev_handle + 1;
    req.bagq_end_handle = 0xffff;
    rc = ble_att_clt_tx_read_group_type(conn, &req, uuid128);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_gatt_kick_disc_service_uuid(struct ble_gatt_entry *entry)
{
    struct ble_att_find_type_value_req req;
    struct ble_hs_conn *conn;
    int rc;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        return ENOTCONN;
    }

    req.bavq_start_handle = entry->disc_service_uuid.prev_handle + 1;
    req.bavq_end_handle = 0xffff;
    req.bavq_attr_type = BLE_ATT_UUID_PRIMARY_SERVICE;

    rc = ble_att_clt_tx_find_type_value(conn, &req,
                                        entry->disc_service_uuid.service_uuid,
                                        16);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_gatt_wakeup(void)
{
    const struct ble_gatt_dispatch_entry *dispatch;
    struct ble_gatt_entry *entry;
    struct ble_gatt_entry *prev;
    struct ble_gatt_entry *next;
    struct ble_gatt_entry *last;
    int rc;

    last = STAILQ_LAST(&ble_gatt_list, ble_gatt_entry, next);

    prev = NULL;
    entry = STAILQ_FIRST(&ble_gatt_list);
    while (prev != last) {
        next = STAILQ_NEXT(entry, next);

        if (entry->flags & BLE_GATT_ENTRY_F_PENDING) {
            dispatch = ble_gatt_dispatch_get(entry->op);
            rc = dispatch->kick_cb(entry);
            if (rc == 0) {
                ble_gatt_entry_set_expecting(entry, prev);
            } else {
                ble_gatt_entry_remove_free(entry, prev);
            }
        }

        prev = entry;
        entry = next;
    }
}

static int
ble_gatt_rx_err_disc_all_services(struct ble_gatt_entry *entry,
                                  struct ble_hs_conn *conn,
                                  struct ble_att_error_rsp *rsp)
{
    uint8_t status;

    if (rsp->baep_error_code == BLE_ATT_ERR_ATTR_NOT_FOUND) {
        /* Discovery is complete. */
        status = 0;
    } else {
        /* Discovery failure. */
        status = rsp->baep_error_code;
    }

    entry->disc_all_services.cb(conn->bhc_handle, status, NULL,
                                entry->disc_all_services.cb_arg);

    return 0;
}

static int
ble_gatt_rx_err_disc_service_uuid(struct ble_gatt_entry *entry,
                                  struct ble_hs_conn *conn,
                                  struct ble_att_error_rsp *rsp)
{
    uint8_t status;

    if (rsp->baep_error_code == BLE_ATT_ERR_ATTR_NOT_FOUND) {
        /* Discovery is complete. */
        status = 0;
    } else {
        /* Discovery failure. */
        status = rsp->baep_error_code;
    }

    entry->disc_service_uuid.cb(conn->bhc_handle, status, NULL,
                                entry->disc_service_uuid.cb_arg);

    return 0;
}

void
ble_gatt_rx_err(struct ble_hs_conn *conn, struct ble_att_error_rsp *rsp)
{
    const struct ble_gatt_dispatch_entry *dispatch;
    struct ble_gatt_entry *entry;
    struct ble_gatt_entry *prev;

    entry = ble_gatt_find(conn->bhc_handle, BLE_GATT_OP_NONE, 1, &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    dispatch = ble_gatt_dispatch_get(entry->op);
    dispatch->rx_err_cb(entry, conn, rsp);

    ble_gatt_entry_remove_free(entry, prev);
}

int
ble_gatt_exchange_mtu(uint16_t conn_handle)
{
    struct ble_gatt_entry *entry;
    int rc;

    rc = ble_gatt_new_entry(conn_handle, &entry);
    if (rc != 0) {
        return rc;
    }
    entry->op = BLE_GATT_OP_MTU;

    return 0;
}

void
ble_gatt_rx_mtu(struct ble_hs_conn *conn, uint16_t chan_mtu)
{
    struct ble_gatt_entry *entry;
    struct ble_gatt_entry *prev;

    entry = ble_gatt_find(conn->bhc_handle, BLE_GATT_OP_MTU, 1, &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    /* XXX: Call success callback. */
    ble_gatt_entry_remove_free(entry, prev);
}

void
ble_gatt_rx_find_info(struct ble_hs_conn *conn, int status,
                      uint16_t last_handle_id)
{
    struct ble_gatt_entry *entry;
    struct ble_gatt_entry *prev;

    entry = ble_gatt_find(conn->bhc_handle, BLE_GATT_OP_FIND_INFO, 1, &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    if (status != 0) {
        /* XXX: Call failure callback. */
        ble_gatt_entry_remove_free(entry, prev);
        return;
    }

    if (last_handle_id == 0xffff) {
        /* XXX: Call success callback. */
        ble_gatt_entry_remove_free(entry, prev);
        return;
    }

    /* Send follow-up request. */
    entry->find_info.next_handle = last_handle_id + 1;
    ble_gatt_entry_set_pending(entry);
}

int
ble_gatt_find_info(uint16_t conn_handle, uint16_t att_start_handle,
                   uint16_t att_end_handle)
{
    struct ble_gatt_entry *entry;
    int rc;

    rc = ble_gatt_new_entry(conn_handle, &entry);
    if (rc != 0) {
        return rc;
    }
    entry->op = BLE_GATT_OP_FIND_INFO;
    entry->conn_handle = conn_handle;
    entry->find_info.next_handle = att_start_handle;
    entry->find_info.end_handle = att_end_handle;

    return 0;
}

void
ble_gatt_rx_read_group_type_adata(struct ble_hs_conn *conn,
                                  struct ble_att_clt_adata *adata)
{
    struct ble_gatt_service service;
    struct ble_gatt_entry *entry;
    struct ble_gatt_entry *prev;
    uint16_t uuid16;
    int cbrc;
    int rc;

    entry = ble_gatt_find(conn->bhc_handle, BLE_GATT_OP_DISC_ALL_SERVICES, 1,
                          &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    switch (adata->value_len) {
    case 2:
        uuid16 = le16toh(adata->value);
        rc = ble_hs_uuid_from_16bit(uuid16, service.uuid128);
        if (rc != 0) {
            goto done;
        }
        break;

    case 16:
        memcpy(service.uuid128, adata->value, 16);
        break;

    default:
        rc = EMSGSIZE;
        goto done;
    }

    entry->disc_all_services.prev_handle = adata->end_group_handle;

    service.start_handle = adata->att_handle;
    service.end_handle = adata->end_group_handle;

    rc = 0;

done:
    cbrc = entry->disc_all_services.cb(conn->bhc_handle, rc, &service,
                                       entry->disc_all_services.cb_arg);
    if (rc != 0 || cbrc != 0) {
        ble_gatt_entry_remove_free(entry, prev);
    }
}

void
ble_gatt_rx_read_group_type_complete(struct ble_hs_conn *conn, int rc)
{
    struct ble_gatt_entry *entry;
    struct ble_gatt_entry *prev;

    entry = ble_gatt_find(conn->bhc_handle, BLE_GATT_OP_DISC_ALL_SERVICES, 1,
                          &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    if (rc != 0 || entry->disc_all_services.prev_handle == 0xffff) {
        /* Error or all services discovered. */
        entry->disc_all_services.cb(conn->bhc_handle, rc, NULL,
                                    entry->disc_all_services.cb_arg);
        ble_gatt_entry_remove_free(entry, prev);
    } else {
        /* Send follow-up request. */
        ble_gatt_entry_set_pending(entry);
    }
}

void
ble_gatt_rx_find_type_value_hinfo(struct ble_hs_conn *conn,
                                  struct ble_att_clt_adata *adata)
{
    struct ble_gatt_service service;
    struct ble_gatt_entry *entry;
    struct ble_gatt_entry *prev;
    int rc;

    entry = ble_gatt_find(conn->bhc_handle, BLE_GATT_OP_DISC_SERVICE_UUID, 1,
                          &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    service.start_handle = adata->att_handle;
    service.end_handle = adata->end_group_handle;
    memcpy(service.uuid128, entry->disc_service_uuid.service_uuid, 16);

    rc = entry->disc_service_uuid.cb(conn->bhc_handle, 0, &service,
                                     entry->disc_service_uuid.cb_arg);
    if (rc != 0) {
        ble_gatt_entry_remove_free(entry, prev);
    }
}

void
ble_gatt_rx_find_type_value_complete(struct ble_hs_conn *conn, int rc)
{
    struct ble_gatt_entry *entry;
    struct ble_gatt_entry *prev;

    entry = ble_gatt_find(conn->bhc_handle, BLE_GATT_OP_DISC_SERVICE_UUID, 1,
                          &prev);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    if (rc != 0 || entry->disc_service_uuid.prev_handle == 0xffff) {
        /* Error or all services discovered. */
        entry->disc_service_uuid.cb(conn->bhc_handle, rc, NULL,
                                    entry->disc_service_uuid.cb_arg);
        ble_gatt_entry_remove_free(entry, prev);
    } else {
        /* Send follow-up request. */
        ble_gatt_entry_set_pending(entry);
    }
}

int
ble_gatt_disc_all_services(uint16_t conn_handle, ble_gatt_disc_service_fn *cb,
                           void *cb_arg)
{
    struct ble_gatt_entry *entry;
    int rc;

    rc = ble_gatt_new_entry(conn_handle, &entry);
    if (rc != 0) {
        return rc;
    }
    entry->op = BLE_GATT_OP_DISC_ALL_SERVICES;
    entry->disc_all_services.prev_handle = 0x0000;
    entry->disc_all_services.cb = cb;
    entry->disc_all_services.cb_arg = cb_arg;

    return 0;
}

int
ble_gatt_disc_service_by_uuid(uint16_t conn_handle, void *service_uuid128,
                              ble_gatt_disc_service_fn *cb, void *cb_arg)
{
    struct ble_gatt_entry *entry;
    int rc;

    rc = ble_gatt_new_entry(conn_handle, &entry);
    if (rc != 0) {
        return rc;
    }
    entry->op = BLE_GATT_OP_DISC_SERVICE_UUID;
    memcpy(entry->disc_service_uuid.service_uuid, service_uuid128, 16);
    entry->disc_service_uuid.prev_handle = 0x0000;
    entry->disc_service_uuid.cb = cb;
    entry->disc_service_uuid.cb_arg = cb_arg;

    return 0;
}

int
ble_gatt_init(void)
{
    int rc;

    free(ble_gatt_entry_mem);

    ble_gatt_entry_mem = malloc(
        OS_MEMPOOL_BYTES(BLE_GATT_NUM_ENTRIES,
                         sizeof (struct ble_gatt_entry)));
    if (ble_gatt_entry_mem == NULL) {
        rc = ENOMEM;
        goto err;
    }

    rc = os_mempool_init(&ble_gatt_entry_pool,
                         BLE_GATT_NUM_ENTRIES,
                         sizeof (struct ble_gatt_entry),
                         ble_gatt_entry_mem,
                         "ble_gatt_entry_pool");
    if (rc != 0) {
        goto err;
    }

    STAILQ_INIT(&ble_gatt_list);

    return 0;

err:
    free(ble_gatt_entry_mem);
    ble_gatt_entry_mem = NULL;

    return rc;
}
