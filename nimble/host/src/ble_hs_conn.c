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

#include <assert.h>
#include <string.h>
#include <errno.h>
#include "os/os.h"
#include "host/host_hci.h"
#include "ble_hs_priv.h"
#include "ble_l2cap_priv.h"
#include "ble_l2cap_sig.h"
#include "ble_l2cap_sm.h"
#include "ble_att_priv.h"
#include "ble_gatt_priv.h"
#include "ble_hs_conn.h"

static SLIST_HEAD(, ble_hs_conn) ble_hs_conns;
static struct os_mempool ble_hs_conn_pool;

static os_membuf_t *ble_hs_conn_elem_mem;

static struct os_mutex ble_hs_conn_mutex;

void
ble_hs_conn_lock(void)
{
    struct os_task *owner;
    int rc;

    owner = ble_hs_conn_mutex.mu_owner;
    if (owner != NULL) {
        assert(owner != os_sched_get_current_task());
    }

    rc = os_mutex_pend(&ble_hs_conn_mutex, 0xffffffff);
    assert(rc == 0 || rc == OS_NOT_STARTED);
}

void
ble_hs_conn_unlock(void)
{
    int rc;

    rc = os_mutex_release(&ble_hs_conn_mutex);
    assert(rc == 0 || rc == OS_NOT_STARTED);
}

int
ble_hs_conn_locked_by_cur_task(void)
{
    struct os_task *owner;

    owner = ble_hs_conn_mutex.mu_owner;
    return owner != NULL && owner == os_sched_get_current_task();
}

/**
 * Lock restrictions: none.
 */
int
ble_hs_conn_can_alloc(void)
{
#if !NIMBLE_OPT_CONNECT
    return 0;
#endif

    return ble_hs_conn_pool.mp_num_free >= 1;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
struct ble_l2cap_chan *
ble_hs_conn_chan_find(struct ble_hs_conn *conn, uint16_t cid)
{
#if !NIMBLE_OPT_CONNECT
    return NULL;
#endif

    struct ble_l2cap_chan *chan;

    SLIST_FOREACH(chan, &conn->bhc_channels, blc_next) {
        if (chan->blc_cid == cid) {
            return chan;
        }
        if (chan->blc_cid > cid) {
            return NULL;
        }
    }

    return NULL;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex if connection has been
 * inserted.
 */
int
ble_hs_conn_chan_insert(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan)
{
#if !NIMBLE_OPT_CONNECT
    return BLE_HS_ENOTSUP;
#endif

    struct ble_l2cap_chan *prev;
    struct ble_l2cap_chan *cur;

    prev = NULL;
    SLIST_FOREACH(cur, &conn->bhc_channels, blc_next) {
        if (cur->blc_cid == chan->blc_cid) {
            return BLE_HS_EALREADY;
        }
        if (cur->blc_cid > chan->blc_cid) {
            break;
        }

        prev = cur;
    }

    if (prev == NULL) {
        SLIST_INSERT_HEAD(&conn->bhc_channels, chan, blc_next);
    } else {
        SLIST_INSERT_AFTER(prev, chan, blc_next);
    }

    return 0;
}

/**
 * Lock restrictions: none.
 */
struct ble_hs_conn *
ble_hs_conn_alloc(void)
{
#if !NIMBLE_OPT_CONNECT
    return NULL;
#endif

    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

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
    rc = ble_hs_conn_chan_insert(conn, chan);
    if (rc != 0) {
        goto err;
    }

    chan = ble_l2cap_sig_create_chan();
    if (chan == NULL) {
        goto err;
    }
    rc = ble_hs_conn_chan_insert(conn, chan);
    if (rc != 0) {
        goto err;
    }

    chan = ble_l2cap_sm_create_chan();
    if (chan == NULL) {
        goto err;
    }
    rc = ble_hs_conn_chan_insert(conn, chan);
    if (rc != 0) {
        goto err;
    }

    rc = ble_gatts_conn_init(&conn->bhc_gatt_svr);
    if (rc != 0) {
        goto err;
    }

    STATS_INC(ble_hs_stats, conn_create);

    return conn;

err:
    ble_hs_conn_free(conn);
    return NULL;
}

/**
 * Lock restrictions: none.
 */
static void
ble_hs_conn_delete_chan(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan)
{
    if (conn->bhc_rx_chan == chan) {
        conn->bhc_rx_chan = NULL;
    }

    SLIST_REMOVE(&conn->bhc_channels, chan, ble_l2cap_chan, blc_next);
    ble_l2cap_chan_free(chan);
}

/**
 * Lock restrictions: none.
 */
void
ble_hs_conn_free(struct ble_hs_conn *conn)
{
#if !NIMBLE_OPT_CONNECT
    return;
#endif

    struct ble_l2cap_chan *chan;
    int rc;

    if (conn == NULL) {
        return;
    }

    ble_gatts_conn_deinit(&conn->bhc_gatt_svr);

    ble_att_svr_prep_clear(&conn->bhc_att_svr);

    while ((chan = SLIST_FIRST(&conn->bhc_channels)) != NULL) {
        ble_hs_conn_delete_chan(conn, chan);
    }

    rc = os_memblock_put(&ble_hs_conn_pool, conn);
    assert(rc == 0);

    STATS_INC(ble_hs_stats, conn_delete);
}

/**
 * Lock restrictions: Caller must NOT lock ble_hs_conn mutex.
 */
void
ble_hs_conn_insert(struct ble_hs_conn *conn)
{
#if !NIMBLE_OPT_CONNECT
    return;
#endif

    ble_hs_conn_lock();

    assert(ble_hs_conn_find(conn->bhc_handle) == NULL);
    SLIST_INSERT_HEAD(&ble_hs_conns, conn, bhc_next);

    ble_hs_conn_unlock();
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
void
ble_hs_conn_remove(struct ble_hs_conn *conn)
{
#if !NIMBLE_OPT_CONNECT
    return;
#endif

    SLIST_REMOVE(&ble_hs_conns, conn, ble_hs_conn, bhc_next);
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
struct ble_hs_conn *
ble_hs_conn_find(uint16_t conn_handle)
{
#if !NIMBLE_OPT_CONNECT
    return NULL;
#endif

    struct ble_hs_conn *conn;

    SLIST_FOREACH(conn, &ble_hs_conns, bhc_next) {
        if (conn->bhc_handle == conn_handle) {
            return conn;
        }
    }

    return NULL;
}

/**
 * Lock restrictions: Caller must NOT lock ble_hs_conn mutex.
 */
int
ble_hs_conn_exists(uint16_t conn_handle)
{
#if !NIMBLE_OPT_CONNECT
    return 0;
#endif

    struct ble_hs_conn *conn;

    ble_hs_conn_lock();
    conn = ble_hs_conn_find(conn_handle);
    ble_hs_conn_unlock();

    return conn != NULL;
}

/**
 * Retrieves the first connection in the list.
 *
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
struct ble_hs_conn *
ble_hs_conn_first(void)
{
#if !NIMBLE_OPT_CONNECT
    return NULL;
#endif

    return SLIST_FIRST(&ble_hs_conns);
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
static void
ble_hs_conn_txable_transition(struct ble_hs_conn *conn)
{
    ble_gattc_connection_txable(conn->bhc_handle);
}

/**
 * Lock restrictions: Caller must NOT lock ble_hs_conn mutex.
 */
void
ble_hs_conn_rx_num_completed_pkts(uint16_t handle, uint16_t num_pkts)
{
#if !NIMBLE_OPT_CONNECT
    return;
#endif

    struct ble_hs_conn *conn;
    int could_tx;
    int can_tx;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(handle);
    if (conn != NULL) {
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

    ble_hs_conn_unlock();
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_hs_conn_can_tx(struct ble_hs_conn *conn)
{
#if !NIMBLE_OPT_CONNECT
    return 0;
#endif

    return ble_hs_cfg.max_outstanding_pkts_per_conn == 0 ||
           conn->bhc_outstanding_pkts <
                ble_hs_cfg.max_outstanding_pkts_per_conn;
}

/**
 * Lock restrictions: None.
 */
static void
ble_hs_conn_free_mem(void)
{
    free(ble_hs_conn_elem_mem);
    ble_hs_conn_elem_mem = NULL;
}

/**
 * Lock restrictions: None.
 */
int 
ble_hs_conn_init(void)
{
    int rc;

    ble_hs_conn_free_mem();

    rc = os_mutex_init(&ble_hs_conn_mutex);
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    ble_hs_conn_elem_mem = malloc(
        OS_MEMPOOL_BYTES(ble_hs_cfg.max_connections,
                         sizeof (struct ble_hs_conn)));
    if (ble_hs_conn_elem_mem == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }
    rc = os_mempool_init(&ble_hs_conn_pool, ble_hs_cfg.max_connections,
                         sizeof (struct ble_hs_conn),
                         ble_hs_conn_elem_mem, "ble_hs_conn_pool");
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    SLIST_INIT(&ble_hs_conns);

    return 0;

err:
    ble_hs_conn_free_mem();
    return rc;
}
