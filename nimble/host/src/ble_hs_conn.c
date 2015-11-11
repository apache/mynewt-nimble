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
#include "ble_l2cap.h"
#include "ble_hs_conn.h"
#include "ble_hs_att.h"

#define BLE_HS_CONN_MAX 16

static SLIST_HEAD(, ble_hs_conn) ble_hs_conns;
static struct os_mempool ble_hs_conn_pool;
static struct os_mutex ble_hs_conn_mutex;

#define BLE_HS_CONN_STATE_NULL      0
#define BLE_HS_CONN_STATE_UNACKED   1
#define BLE_HS_CONN_STATE_ACKED     2

static struct {
    uint8_t bhcp_state;

    uint8_t bhcp_addr[BLE_DEV_ADDR_LEN];
    unsigned bhcp_use_white:1;
} ble_hs_conn_cur;

void
ble_hs_conn_lock(void)
{
    int rc;

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

static void
ble_hs_conn_free(struct ble_hs_conn *conn)
{
    struct ble_l2cap_chan *chan;
    int rc;

    if (conn == NULL) {
        return;
    }

    SLIST_REMOVE(&ble_hs_conns, conn, ble_hs_conn, bhc_next);

    while ((chan = SLIST_FIRST(&conn->bhc_channels)) != NULL) {
        SLIST_REMOVE(&conn->bhc_channels, chan, ble_l2cap_chan, blc_next);
        ble_l2cap_chan_free(chan);
    }

    rc = os_memblock_put(&ble_hs_conn_pool, conn);
    assert(rc == 0);
}

static struct ble_hs_conn *
ble_hs_conn_alloc(void)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;

    conn = os_memblock_get(&ble_hs_conn_pool);
    if (conn == NULL) {
        goto err;
    }

    conn->bhc_att_mtu = BLE_HS_ATT_MTU_DFLT;

    SLIST_INIT(&conn->bhc_channels);

    chan = ble_hs_att_create_chan();
    if (chan == NULL) {
        goto err;
    }
    SLIST_INSERT_HEAD(&conn->bhc_channels, chan, blc_next);

    return conn;

err:
    ble_hs_conn_free(conn);
    return NULL;
}

static void
ble_hs_conn_insert(struct ble_hs_conn *conn)
{
    ble_hs_conn_lock();

    assert(ble_hs_conn_find(conn->bhc_handle) == NULL);
    SLIST_INSERT_HEAD(&ble_hs_conns, conn, bhc_next);

    ble_hs_conn_unlock();
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

int
ble_hs_conn_pending(void)
{
    return ble_hs_conn_cur.bhcp_state != BLE_HS_CONN_STATE_NULL;
}

int
ble_hs_conn_initiate(struct hci_create_conn *hcc)
{
    int rc;

    ble_hs_conn_lock();

    if (ble_hs_conn_pending()) {
        rc = EALREADY;
        goto done;
    }

    ble_hs_conn_cur.bhcp_state = BLE_HS_CONN_STATE_UNACKED;
    memcpy(ble_hs_conn_cur.bhcp_addr, hcc->peer_addr,
           sizeof ble_hs_conn_cur.bhcp_addr);
    ble_hs_conn_cur.bhcp_use_white = 0;

    rc = host_hci_cmd_le_create_connection(hcc);
    if (rc != 0) {
        ble_hs_conn_cur.bhcp_state = BLE_HS_CONN_STATE_NULL;
        goto done;
    }

    rc = 0;

done:
    ble_hs_conn_unlock();
    return rc;
}

int
ble_hs_conn_rx_cmd_status_create_conn(uint16_t ocf, uint8_t status)
{
    int rc;

    ble_hs_conn_lock();

    if (ble_hs_conn_cur.bhcp_state != BLE_HS_CONN_STATE_UNACKED) {
        rc = ENOENT;
        goto done;
    }

    if (status == BLE_ERR_SUCCESS) {
        ble_hs_conn_cur.bhcp_state = BLE_HS_CONN_STATE_ACKED;
    } else {
        ble_hs_conn_cur.bhcp_state = BLE_HS_CONN_STATE_NULL;
    }

    rc = 0;

done:
    ble_hs_conn_unlock();
    return rc;
}

int
ble_hs_conn_rx_conn_complete(struct hci_le_conn_complete *evt)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_conn_lock();

    if (ble_hs_conn_cur.bhcp_state != BLE_HS_CONN_STATE_ACKED) {
        rc = ENOENT;
        goto done;
    }

    /* Clear pending connection. */
    ble_hs_conn_cur.bhcp_state = BLE_HS_CONN_STATE_NULL;

    if (evt->status != BLE_ERR_SUCCESS) {
        rc = 0;
        goto done;
    }

    /* XXX: Ensure device address is expected. */
    /* XXX: Ensure event fields are acceptable. */

    conn = ble_hs_conn_alloc();
    if (conn == NULL) {
        rc = ENOMEM;
        goto done;
    }

    conn->bhc_handle = evt->connection_handle;
    memcpy(conn->bhc_addr, evt->peer_addr, sizeof conn->bhc_addr);

    ble_hs_conn_insert(conn);

    /* XXX: Notify someone (GAP?) of new connection. */

    rc = 0;

done:
    ble_hs_conn_unlock();
    return rc;
}

int 
ble_hs_conn_init(void)
{
    os_membuf_t *connection_mem;
    int rc;

    connection_mem = malloc(
        OS_MEMPOOL_SIZE(BLE_HS_CONN_MAX,
                        sizeof (struct ble_hs_conn)));
    if (connection_mem == NULL) {
        return ENOMEM;
    }

    rc = os_mempool_init(&ble_hs_conn_pool, BLE_HS_CONN_MAX,
                         sizeof (struct ble_hs_conn),
                         connection_mem, "ble_hs_conn_pool");
    if (rc != 0) {
        return EINVAL; // XXX
    }

    SLIST_INIT(&ble_hs_conns);

    rc = os_mutex_init(&ble_hs_conn_mutex);
    if (rc != 0) {
        return EINVAL; // XXX
    }

    return 0;
}
