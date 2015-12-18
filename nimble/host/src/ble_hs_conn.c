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

#include <assert.h>
#include <string.h>
#include <errno.h>
#include "os/os.h"
#include "host/host_hci.h"
#include "ble_hs_priv.h"
#include "ble_l2cap.h"
#include "ble_l2cap_sig.h"
#include "ble_att_priv.h"
#include "ble_gatt_priv.h"
#include "ble_hs_conn.h"

#define BLE_HS_CONN_MAX         16

#define BLE_HS_CONN_MAX_OUTSTANDING_PKTS    10

static SLIST_HEAD(, ble_hs_conn) ble_hs_conns;
static struct os_mempool ble_hs_conn_pool;

static os_membuf_t *ble_hs_conn_elem_mem;

struct ble_hs_conn *
ble_hs_conn_alloc(void)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;

    conn = os_memblock_get(&ble_hs_conn_pool);
    if (conn == NULL) {
        goto err;
    }
    memset(conn, 0, sizeof *conn);

    SLIST_INIT(&conn->bhc_channels);

    chan = ble_att_create_chan();
    if (chan == NULL) {
        goto err;
    }
    SLIST_INSERT_HEAD(&conn->bhc_channels, chan, blc_next);

    chan = ble_l2cap_sig_create_chan();
    if (chan == NULL) {
        goto err;
    }
    SLIST_INSERT_HEAD(&conn->bhc_channels, chan, blc_next);

    return conn;

err:
    ble_hs_conn_free(conn);
    return NULL;
}

void
ble_hs_conn_free(struct ble_hs_conn *conn)
{
    struct ble_l2cap_chan *chan;
    int rc;

    if (conn == NULL) {
        return;
    }

    while ((chan = SLIST_FIRST(&conn->bhc_channels)) != NULL) {
        SLIST_REMOVE(&conn->bhc_channels, chan, ble_l2cap_chan, blc_next);
        ble_l2cap_chan_free(chan);
    }

    /* XXX: Free contents of rx and tx queues. */

    rc = os_memblock_put(&ble_hs_conn_pool, conn);
    assert(rc == 0);
}

void
ble_hs_conn_insert(struct ble_hs_conn *conn)
{
    assert(ble_hs_conn_find(conn->bhc_handle) == NULL);
    SLIST_INSERT_HEAD(&ble_hs_conns, conn, bhc_next);
}

void
ble_hs_conn_remove(struct ble_hs_conn *conn)
{
    SLIST_REMOVE(&ble_hs_conns, conn, ble_hs_conn, bhc_next);
}

struct ble_hs_conn *
ble_hs_conn_find(uint16_t con_handle)
{
    struct ble_hs_conn *conn;

    SLIST_FOREACH(conn, &ble_hs_conns, bhc_next) {
        if (conn->bhc_handle == con_handle) {
            return conn;
        }
    }

    return NULL;
}

/**
 * Retrieves the first connection in the list.
 * XXX: This is likely temporary.
 */
struct ble_hs_conn *
ble_hs_conn_first(void)
{
    return SLIST_FIRST(&ble_hs_conns);
}

struct ble_l2cap_chan *
ble_hs_conn_chan_find(struct ble_hs_conn *conn, uint16_t cid)
{
    struct ble_l2cap_chan *chan;

    SLIST_FOREACH(chan, &conn->bhc_channels, blc_next) {
        if (chan->blc_cid == cid) {
            return chan;
        }
    }

    return NULL;
}

static void
ble_hs_conn_txable_transition(struct ble_hs_conn *conn)
{
    ble_gatt_connection_txable(conn->bhc_handle);
}

void
ble_hs_conn_rx_num_completed_pkts(uint16_t handle, uint16_t num_pkts)
{
    struct ble_hs_conn *conn;
    int could_tx;
    int can_tx;

    conn = ble_hs_conn_find(handle);
    if (conn == NULL) {
        return;
    }

    could_tx = ble_hs_conn_can_tx(conn);

    if (num_pkts > conn->bhc_outstanding_pkts) {
        num_pkts = conn->bhc_outstanding_pkts;
    }
    conn->bhc_outstanding_pkts -= num_pkts;

    can_tx = ble_hs_conn_can_tx(conn);

    if (!could_tx && can_tx) {
        ble_hs_conn_txable_transition(conn);
    }
}

int
ble_hs_conn_can_tx(struct ble_hs_conn *conn)
{
    return conn->bhc_outstanding_pkts < BLE_HS_CONN_MAX_OUTSTANDING_PKTS;
}

static void
ble_hs_conn_free_mem(void)
{
    free(ble_hs_conn_elem_mem);
    ble_hs_conn_elem_mem = NULL;
}

int 
ble_hs_conn_init(void)
{
    int rc;

    ble_hs_conn_free_mem();

    ble_hs_conn_elem_mem = malloc(
        OS_MEMPOOL_BYTES(BLE_HS_CONN_MAX,
                         sizeof (struct ble_hs_conn)));
    if (ble_hs_conn_elem_mem == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }
    rc = os_mempool_init(&ble_hs_conn_pool, BLE_HS_CONN_MAX,
                         sizeof (struct ble_hs_conn),
                         ble_hs_conn_elem_mem, "ble_hs_conn_pool");
    if (rc != 0) {
        rc = BLE_HS_EINVAL; // XXX
        goto err;
    }


    SLIST_INIT(&ble_hs_conns);

    return 0;

err:
    ble_hs_conn_free_mem();
    return rc;
}
