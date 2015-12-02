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
#include "ble_hs_conn.h"
#include "ble_hs_att_cmd.h"
#include "ble_hs_att.h"

#define BLE_HS_ATT_BATCH_OP_NONE            0
#define BLE_HS_ATT_BATCH_OP_MTU             1
#define BLE_HS_ATT_BATCH_OP_FIND_INFO       2

struct ble_hs_att_batch_entry {
    SLIST_ENTRY(ble_hs_att_batch_entry) next;

    uint8_t op;
    uint16_t conn_handle;
    union {
        struct {
            int (*cb)(int status, uint16_t conn_handle, void *arg);
            void *cb_arg;
        } mtu;

        struct {
            uint16_t end_handle;

            int (*cb)(int status, uint16_t conn_handle, void *arg);
            void *cb_arg;
        } find_info;
    };
};

#define BLE_HS_ATT_BATCH_NUM_ENTRIES          4
static void *ble_hs_att_batch_entry_mem;
static struct os_mempool ble_hs_att_batch_entry_pool;

static SLIST_HEAD(, ble_hs_att_batch_entry) ble_hs_att_batch_list;

static struct ble_hs_att_batch_entry *
ble_hs_att_batch_entry_alloc(void)
{
    struct ble_hs_att_batch_entry *entry;

    entry = os_memblock_get(&ble_hs_att_batch_entry_pool);
    if (entry != NULL) {
        memset(entry, 0, sizeof *entry);
    }

    return entry;
}

static void
ble_hs_att_batch_entry_free(struct ble_hs_att_batch_entry *entry)
{
    int rc;

    rc = os_memblock_put(&ble_hs_att_batch_entry_pool, entry);
    assert(rc == 0);
}

static struct ble_hs_att_batch_entry *
ble_hs_att_batch_find(uint16_t conn_handle, uint8_t att_op)
{
    struct ble_hs_att_batch_entry *entry;

    SLIST_FOREACH(entry, &ble_hs_att_batch_list, next) {
        if (entry->conn_handle == conn_handle) {
            if (att_op == entry->op || att_op == BLE_HS_ATT_BATCH_OP_NONE) {
                return entry;
            } else {
                return NULL;
            }
        }
    }

    return NULL;
}

static int
ble_hs_att_batch_new_entry(uint16_t conn_handle,
                           struct ble_hs_att_batch_entry **entry,
                           struct ble_hs_conn **conn)
{
    *entry = NULL;

    /* Ensure we have a connection with the specified handle. */
    *conn = ble_hs_conn_find(conn_handle);
    if (*conn == NULL) {
        return ENOTCONN;
    }

    *entry = ble_hs_att_batch_entry_alloc();
    if (*entry == NULL) {
        return ENOMEM;
    }

    (*entry)->conn_handle = conn_handle;

    SLIST_INSERT_HEAD(&ble_hs_att_batch_list, *entry, next);

    return 0;
}

void
ble_hs_att_batch_rx_error(struct ble_hs_conn *conn,
                          struct ble_hs_att_error_rsp *rsp)
{
    struct ble_hs_att_batch_entry *entry;

    entry = ble_hs_att_batch_find(conn->bhc_handle, BLE_HS_ATT_BATCH_OP_NONE);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    switch (entry->op) {
    case BLE_HS_ATT_BATCH_OP_NONE:
        break;

    case BLE_HS_ATT_BATCH_OP_FIND_INFO:
        /* XXX: Branch on error status. */
        break;

    default:
        assert(0);
        break;
    }
}

void
ble_hs_att_batch_rx_mtu(struct ble_hs_conn *conn, uint16_t peer_mtu)
{
    struct ble_hs_att_batch_entry *entry;
    struct ble_l2cap_chan *chan;

    entry = ble_hs_att_batch_find(conn->bhc_handle, BLE_HS_ATT_BATCH_OP_MTU);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    assert(chan != NULL);

    ble_hs_att_set_peer_mtu(chan, peer_mtu);

    /* XXX: Call success callback. */
}

int
ble_hs_att_batch_mtu(uint16_t conn_handle)
{
    struct ble_hs_att_mtu_cmd req;
    struct ble_hs_att_batch_entry *entry;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    rc = ble_hs_att_batch_new_entry(conn_handle, &entry, &conn);
    if (rc != 0) {
        goto err;
    }
    entry->op = BLE_HS_ATT_BATCH_OP_MTU;

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);

    req.bhamc_mtu = chan->blc_my_mtu;
    rc = ble_hs_att_clt_tx_mtu(conn, &req);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    if (entry != NULL) {
        SLIST_REMOVE_HEAD(&ble_hs_att_batch_list, next);
        ble_hs_att_batch_entry_free(entry);
    }
    return rc;
}

void
ble_hs_att_batch_rx_find_info(struct ble_hs_conn *conn, int status,
                              uint16_t last_handle_id)
{
    struct ble_hs_att_batch_entry *entry;

    entry = ble_hs_att_batch_find(conn->bhc_handle,
                                  BLE_HS_ATT_BATCH_OP_FIND_INFO);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    if (status != 0) {
        /* XXX: Call failure callback. */
        return;
    }

    if (last_handle_id == 0xffff) {
        /* XXX: Call success callback. */
        return;
    }

    /* XXX: Send follow up request. */
}

int
ble_hs_att_batch_find_info(uint16_t conn_handle, uint16_t att_start_handle,
                           uint16_t att_end_handle)
{
    struct ble_hs_att_find_info_req req;
    struct ble_hs_att_batch_entry *entry;
    struct ble_hs_conn *conn;
    int rc;

    rc = ble_hs_att_batch_new_entry(conn_handle, &entry, &conn);
    if (rc != 0) {
        goto err;
    }
    entry->op = BLE_HS_ATT_BATCH_OP_FIND_INFO;
    entry->conn_handle = conn_handle;
    entry->find_info.end_handle = att_end_handle;

    req.bhafq_start_handle = att_start_handle;
    req.bhafq_end_handle = att_end_handle;
    rc = ble_hs_att_clt_tx_find_info(conn, &req);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    if (entry != NULL) {
        SLIST_REMOVE_HEAD(&ble_hs_att_batch_list, next);
        ble_hs_att_batch_entry_free(entry);
    }
    return rc;
}

int
ble_hs_att_batch_init(void)
{
    int rc;

    free(ble_hs_att_batch_entry_mem);

    ble_hs_att_batch_entry_mem = malloc(
        OS_MEMPOOL_BYTES(BLE_HS_ATT_BATCH_NUM_ENTRIES,
                         sizeof (struct ble_hs_att_batch_entry)));
    if (ble_hs_att_batch_entry_mem == NULL) {
        rc = ENOMEM;
        goto err;
    }

    rc = os_mempool_init(&ble_hs_att_batch_entry_pool,
                         BLE_HS_ATT_BATCH_NUM_ENTRIES,
                         sizeof (struct ble_hs_att_batch_entry),
                         ble_hs_att_batch_entry_mem,
                         "ble_hs_att_batch_entry_pool");
    if (rc != 0) {
        goto err;
    }

    SLIST_INIT(&ble_hs_att_batch_list);

    return 0;

err:
    free(ble_hs_att_batch_entry_mem);
    ble_hs_att_batch_entry_mem = NULL;

    return rc;
}
