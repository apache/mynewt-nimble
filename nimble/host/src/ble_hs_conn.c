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
#include "os/os.h"
#include "host/host_hci.h"
#include "ble_hs_priv.h"

/** At least three channels required per connection (sig, att, sm). */
#define BLE_HS_CONN_MIN_CHANS       3

static SLIST_HEAD(, ble_hs_conn) ble_hs_conns;
static struct os_mempool ble_hs_conn_pool;

static os_membuf_t *ble_hs_conn_elem_mem;

static const uint8_t ble_hs_conn_null_addr[6];

int
ble_hs_conn_can_alloc(void)
{
#if !NIMBLE_OPT(CONNECT)
    return 0;
#endif

    return ble_hs_conn_pool.mp_num_free >= 1 &&
           ble_l2cap_chan_pool.mp_num_free >= BLE_HS_CONN_MIN_CHANS &&
           ble_gatts_conn_can_alloc();
}

struct ble_l2cap_chan *
ble_hs_conn_chan_find(struct ble_hs_conn *conn, uint16_t cid)
{
#if !NIMBLE_OPT(CONNECT)
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

int
ble_hs_conn_chan_insert(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan)
{
#if !NIMBLE_OPT(CONNECT)
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

struct ble_hs_conn *
ble_hs_conn_alloc(void)
{
#if !NIMBLE_OPT(CONNECT)
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

    /* XXX: We should create the SM channel even if not configured.  We need it
     * to reject SM messages.
     */
#if NIMBLE_OPT(SM)
    chan = ble_sm_create_chan();
    if (chan == NULL) {
        goto err;
    }
    rc = ble_hs_conn_chan_insert(conn, chan);
    if (rc != 0) {
        goto err;
    }
#endif

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

static void
ble_hs_conn_delete_chan(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan)
{
    if (conn->bhc_rx_chan == chan) {
        conn->bhc_rx_chan = NULL;
    }

    SLIST_REMOVE(&conn->bhc_channels, chan, ble_l2cap_chan, blc_next);
    ble_l2cap_chan_free(chan);
}

void
ble_hs_conn_free(struct ble_hs_conn *conn)
{
#if !NIMBLE_OPT(CONNECT)
    return;
#endif

    struct ble_l2cap_chan *chan;
    int rc;

    if (conn == NULL) {
        return;
    }

    ble_gatts_conn_deinit(&conn->bhc_gatt_svr);

    ble_att_svr_prep_clear(&conn->bhc_att_svr.basc_prep_list);

    while ((chan = SLIST_FIRST(&conn->bhc_channels)) != NULL) {
        ble_hs_conn_delete_chan(conn, chan);
    }

    rc = os_memblock_put(&ble_hs_conn_pool, conn);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

    STATS_INC(ble_hs_stats, conn_delete);
}

void
ble_hs_conn_insert(struct ble_hs_conn *conn)
{
#if !NIMBLE_OPT(CONNECT)
    return;
#endif

    BLE_HS_DBG_ASSERT(ble_hs_thread_safe());

    BLE_HS_DBG_ASSERT_EVAL(ble_hs_conn_find(conn->bhc_handle) == NULL);
    SLIST_INSERT_HEAD(&ble_hs_conns, conn, bhc_next);
}

void
ble_hs_conn_remove(struct ble_hs_conn *conn)
{
#if !NIMBLE_OPT(CONNECT)
    return;
#endif

    BLE_HS_DBG_ASSERT(ble_hs_thread_safe());

    SLIST_REMOVE(&ble_hs_conns, conn, ble_hs_conn, bhc_next);
}

struct ble_hs_conn *
ble_hs_conn_find(uint16_t conn_handle)
{
#if !NIMBLE_OPT(CONNECT)
    return NULL;
#endif

    struct ble_hs_conn *conn;

    BLE_HS_DBG_ASSERT(ble_hs_thread_safe());

    SLIST_FOREACH(conn, &ble_hs_conns, bhc_next) {
        if (conn->bhc_handle == conn_handle) {
            return conn;
        }
    }

    return NULL;
}

struct ble_hs_conn *
ble_hs_conn_find_by_addr(uint8_t addr_type, uint8_t *addr)
{
#if !NIMBLE_OPT(CONNECT)
    return NULL;
#endif

    struct ble_hs_conn *conn;

    BLE_HS_DBG_ASSERT(ble_hs_thread_safe());

    SLIST_FOREACH(conn, &ble_hs_conns, bhc_next) {
        if (conn->bhc_addr_type == addr_type &&
            memcmp(conn->bhc_addr, addr, 6) == 0) {

            return conn;
        }
    }

    return NULL;
}

struct ble_hs_conn *
ble_hs_conn_find_by_idx(int idx)
{
#if !NIMBLE_OPT(CONNECT)
    return NULL;
#endif

    struct ble_hs_conn *conn;
    int i;

    BLE_HS_DBG_ASSERT(ble_hs_thread_safe());

    i = 0;
    SLIST_FOREACH(conn, &ble_hs_conns, bhc_next) {
        if (i == idx) {
            return conn;
        }

        i++;
    }

    return NULL;
}

int
ble_hs_conn_exists(uint16_t conn_handle)
{
#if !NIMBLE_OPT(CONNECT)
    return 0;
#endif
    return ble_hs_conn_find(conn_handle) != NULL;
}

/**
 * Retrieves the first connection in the list.
 */
struct ble_hs_conn *
ble_hs_conn_first(void)
{
#if !NIMBLE_OPT(CONNECT)
    return NULL;
#endif

    BLE_HS_DBG_ASSERT(ble_hs_thread_safe());
    return SLIST_FIRST(&ble_hs_conns);
}

void
ble_hs_conn_peer_effective_addr(struct ble_hs_conn *conn, uint8_t *out_addr)
{
    switch (conn->bhc_addr_type) {
    case BLE_ADDR_TYPE_PUBLIC:
    case BLE_ADDR_TYPE_RANDOM:
        memcpy(out_addr, conn->bhc_addr, 6);
        break;

    case BLE_ADDR_TYPE_RPA_PUB_DEFAULT:
    case BLE_ADDR_TYPE_RPA_RND_DEFAULT:
        memcpy(out_addr, conn->peer_rpa_addr, 6);
        break;

    default:
        BLE_HS_DBG_ASSERT(0);
        break;
    }
}

void
ble_hs_conn_our_effective_addr(struct ble_hs_conn *conn,
                               uint8_t *out_addr_type, uint8_t *out_addr)
{
    uint8_t ident_addr_type;
    uint8_t *ident_addr;

    ident_addr = bls_hs_priv_get_local_identity_addr(&ident_addr_type);

    if (memcmp(conn->our_rpa_addr, ble_hs_conn_null_addr, 6) == 0) {
        *out_addr_type = ident_addr_type;
        memcpy(out_addr, ident_addr, 6);
    } else {
        switch (ident_addr_type) {
        case BLE_ADDR_TYPE_PUBLIC:
            *out_addr_type = BLE_ADDR_TYPE_RPA_PUB_DEFAULT;
            break;

        case BLE_ADDR_TYPE_RANDOM:
            *out_addr_type = BLE_ADDR_TYPE_RPA_RND_DEFAULT;
            break;

        default:
            BLE_HS_DBG_ASSERT(0);
        }

        memcpy(out_addr, conn->our_rpa_addr, 6);
    }
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
