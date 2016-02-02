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
#include <errno.h>
#include <assert.h>
#include "ble_hs_priv.h"
#include "ble_hs_conn.h"
#include "ble_l2cap_priv.h"
#include "ble_att_cmd.h"
#include "ble_att_priv.h"

static uint16_t ble_att_preferred_mtu;

/** Dispatch table for incoming ATT requests.  Sorted by op code. */
typedef int ble_att_rx_fn(uint16_t conn_handle, struct os_mbuf **om);
struct ble_att_rx_dispatch_entry {
    uint8_t bde_op;
    ble_att_rx_fn *bde_fn;
};

static const struct ble_att_rx_dispatch_entry ble_att_rx_dispatch[] = {
    { BLE_ATT_OP_ERROR_RSP,            ble_att_clt_rx_error },
    { BLE_ATT_OP_MTU_REQ,              ble_att_svr_rx_mtu },
    { BLE_ATT_OP_MTU_RSP,              ble_att_clt_rx_mtu },
    { BLE_ATT_OP_FIND_INFO_REQ,        ble_att_svr_rx_find_info },
    { BLE_ATT_OP_FIND_INFO_RSP,        ble_att_clt_rx_find_info },
    { BLE_ATT_OP_FIND_TYPE_VALUE_REQ,  ble_att_svr_rx_find_type_value },
    { BLE_ATT_OP_FIND_TYPE_VALUE_RSP,  ble_att_clt_rx_find_type_value },
    { BLE_ATT_OP_READ_TYPE_REQ,        ble_att_svr_rx_read_type },
    { BLE_ATT_OP_READ_TYPE_RSP,        ble_att_clt_rx_read_type },
    { BLE_ATT_OP_READ_REQ,             ble_att_svr_rx_read },
    { BLE_ATT_OP_READ_RSP,             ble_att_clt_rx_read },
    { BLE_ATT_OP_READ_BLOB_REQ,        ble_att_svr_rx_read_blob },
    { BLE_ATT_OP_READ_BLOB_RSP,        ble_att_clt_rx_read_blob },
    { BLE_ATT_OP_READ_MULT_REQ,        ble_att_svr_rx_read_mult },
    { BLE_ATT_OP_READ_MULT_RSP,        ble_att_clt_rx_read_mult },
    { BLE_ATT_OP_READ_GROUP_TYPE_REQ,  ble_att_svr_rx_read_group_type },
    { BLE_ATT_OP_READ_GROUP_TYPE_RSP,  ble_att_clt_rx_read_group_type },
    { BLE_ATT_OP_WRITE_REQ,            ble_att_svr_rx_write },
    { BLE_ATT_OP_WRITE_RSP,            ble_att_clt_rx_write },
    { BLE_ATT_OP_PREP_WRITE_REQ,       ble_att_svr_rx_prep_write },
    { BLE_ATT_OP_PREP_WRITE_RSP,       ble_att_clt_rx_prep_write },
    { BLE_ATT_OP_EXEC_WRITE_REQ,       ble_att_svr_rx_exec_write },
    { BLE_ATT_OP_EXEC_WRITE_RSP,       ble_att_clt_rx_exec_write },
    { BLE_ATT_OP_NOTIFY_REQ,           ble_att_svr_rx_notify },
    { BLE_ATT_OP_INDICATE_REQ,         ble_att_svr_rx_indicate },
    { BLE_ATT_OP_INDICATE_RSP,         ble_att_clt_rx_indicate },
};

#define BLE_ATT_RX_DISPATCH_SZ \
    (sizeof ble_att_rx_dispatch / sizeof ble_att_rx_dispatch[0])

/**
 * Lock restrictions: None.
 */
static const struct ble_att_rx_dispatch_entry *
ble_att_rx_dispatch_entry_find(uint8_t op)
{
    const struct ble_att_rx_dispatch_entry *entry;
    int i;

    for (i = 0; i < BLE_ATT_RX_DISPATCH_SZ; i++) {
        entry = ble_att_rx_dispatch + i;
        if (entry->bde_op == op) {
            return entry;
        }

        if (entry->bde_op > op) {
            break;
        }
    }

    return NULL;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_conn_chan_find(uint16_t conn_handle, struct ble_hs_conn **out_conn,
                       struct ble_l2cap_chan **out_chan)
{
    *out_chan = NULL;

    *out_conn = ble_hs_conn_find(conn_handle);
    if (*out_conn == NULL) {
        return BLE_HS_ENOTCONN;
    }

    *out_chan = ble_hs_conn_chan_find(*out_conn, BLE_L2CAP_CID_ATT);
    assert(*out_chan != NULL);

    return 0;
}

/**
 * Lock restrictions: Caller must NOT lock ble_hs_conn mutex.
 */
uint16_t
ble_att_mtu(uint16_t conn_handle)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint16_t mtu;

    ble_hs_conn_lock();

    ble_att_conn_chan_find(conn_handle, &conn, &chan);
    if (chan != NULL) {
        mtu = ble_l2cap_chan_mtu(chan);
    } else {
        mtu = 0;
    }

    ble_hs_conn_unlock();

    return mtu;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
static int
ble_att_rx(uint16_t conn_handle, struct os_mbuf **om)
{
    const struct ble_att_rx_dispatch_entry *entry;
    uint8_t op;
    int rc;

    rc = os_mbuf_copydata(*om, 0, 1, &op);
    if (rc != 0) {
        return BLE_HS_EMSGSIZE;
    }

    entry = ble_att_rx_dispatch_entry_find(op);
    if (entry == NULL) {
        return BLE_HS_EINVAL;
    }

    rc = entry->bde_fn(conn_handle, om);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Lock restrictions: None.
 */
void
ble_att_set_notify_cb(ble_att_svr_notify_fn *cb, void *cb_arg)
{
    ble_att_svr_notify_cb = cb;
    ble_att_svr_notify_cb_arg = cb_arg;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
void
ble_att_set_peer_mtu(struct ble_l2cap_chan *chan, uint16_t peer_mtu)
{
    if (peer_mtu < BLE_ATT_MTU_DFLT) {
        peer_mtu = BLE_ATT_MTU_DFLT;
    }

    chan->blc_peer_mtu = peer_mtu;
}

/**
 * Lock restrictions: None.
 */
int
ble_att_set_preferred_mtu(uint16_t mtu)
{
    if (mtu < BLE_ATT_MTU_DFLT) {
        return BLE_HS_EINVAL;
    }
    if (mtu > BLE_ATT_MTU_MAX) {
        return BLE_HS_EINVAL;
    }

    ble_att_preferred_mtu = mtu;

    /* XXX: Set my_mtu for established connections that haven't exchanged. */

    return 0;
}

/**
 * Lock restrictions: None.
 */
struct ble_l2cap_chan *
ble_att_create_chan(void)
{
    struct ble_l2cap_chan *chan;

    chan = ble_l2cap_chan_alloc();
    if (chan == NULL) {
        return NULL;
    }

    chan->blc_cid = BLE_L2CAP_CID_ATT;
    chan->blc_my_mtu = ble_att_preferred_mtu;
    chan->blc_default_mtu = BLE_ATT_MTU_DFLT;
    chan->blc_rx_fn = ble_att_rx;

    return chan;
}

/**
 * Allocates an mbuf for use as an ATT request or response.
 *
 * Lock restrictions: None.
 */
struct os_mbuf *
ble_att_get_pkthdr(void)
{
    struct os_mbuf *om;

    om = os_mbuf_get_pkthdr(&ble_hs_mbuf_pool, 0);
    if (om == NULL) {
        return NULL;
    }

    /* Make room in the buffer for various headers.  XXX Check this number. */
    om->om_data += 8;

    return om;
}

/**
 * Lock restrictions: None.
 */
void
ble_att_init(void)
{
    ble_att_preferred_mtu = BLE_ATT_MTU_PREFERRED_DFLT;
}
