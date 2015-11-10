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

static SLIST_HEAD(, ble_hs_conn) ble_hs_conns =
    SLIST_HEAD_INITIALIZER(ble_hs_conns); 

static struct os_mempool ble_hs_conn_pool;

static struct {
    unsigned bhcp_valid:1;

    uint8_t bhcp_addr[6];
    unsigned bhcp_use_white:1;
} ble_hs_conn_pending;

struct ble_hs_conn *
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

    /* XXX: Allocated entries always go into the list.  Allocation and
     * insertion may need to be split into separate operations.
     */
    SLIST_INSERT_HEAD(&ble_hs_conns, conn, bhc_next);

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

    SLIST_REMOVE(&ble_hs_conns, conn, ble_hs_conn, bhc_next);

    while ((chan = SLIST_FIRST(&conn->bhc_channels)) != NULL) {
        SLIST_REMOVE(&conn->bhc_channels, chan, ble_l2cap_chan, blc_next);
        ble_l2cap_chan_free(chan);
    }

    rc = os_memblock_put(&ble_hs_conn_pool, conn);
    assert(rc == 0);
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

/**
 * Initiates a connection using the GAP Direct Connection Establishment
 * Procedure.
 * XXX: This will likely be moved.
 *
 * @return 0 on success; nonzero on failure.
 */
int
ble_hs_conn_initiate_direct(int addr_type, uint8_t *addr)
{
    struct hci_create_conn hcc;
    int rc;

    /* Make sure no connection attempt is already in progress. */
    if (ble_hs_conn_pending.bhcp_valid) {
        return EALREADY;
    }

    hcc.scan_itvl = 0x0010;
    hcc.scan_window = 0x0010;
    hcc.filter_policy = BLE_HCI_CONN_FILT_NO_WL;
    hcc.peer_addr_type = addr_type;
    memcpy(hcc.peer_addr, addr, sizeof hcc.peer_addr);
    hcc.own_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
    hcc.conn_itvl_min = 24;
    hcc.conn_itvl_min = 40;
    hcc.conn_latency = 0;
    hcc.supervision_timeout = 0x0100; // XXX
    hcc.min_ce_len = 0x0010; // XXX
    hcc.min_ce_len = 0x0300; // XXX

    ble_hs_conn_pending.bhcp_valid = 1;
    memcpy(ble_hs_conn_pending.bhcp_addr, addr,
           sizeof ble_hs_conn_pending.bhcp_addr);
    ble_hs_conn_pending.bhcp_use_white = 0;

    rc = host_hci_cmd_le_create_connection(&hcc);
    if (rc != 0) {
        ble_hs_conn_pending.bhcp_valid = 0;
        return rc;
    }

    return 0;
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

    return 0;
}
