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
#include "ble_l2cap.h"
#include "ble_att_cmd.h"
#include "ble_att.h"

/** Dispatch table for incoming ATT requests.  Sorted by op code. */
typedef int ble_att_rx_fn(struct ble_hs_conn *conn,
                          struct ble_l2cap_chan *chan,
                          struct os_mbuf **om);
struct ble_att_rx_dispatch_entry {
    uint8_t bde_op;
    ble_att_rx_fn *bde_fn;
};

static struct ble_att_rx_dispatch_entry ble_att_rx_dispatch[] = {
    { BLE_ATT_OP_MTU_REQ,              ble_att_svr_rx_mtu },
    { BLE_ATT_OP_MTU_RSP,              ble_att_clt_rx_mtu },
    { BLE_ATT_OP_FIND_INFO_REQ,        ble_att_svr_rx_find_info },
    { BLE_ATT_OP_FIND_INFO_RSP,        ble_att_clt_rx_find_info },
    { BLE_ATT_OP_FIND_TYPE_VALUE_REQ,  ble_att_svr_rx_find_type_value },
    { BLE_ATT_OP_READ_TYPE_REQ,        ble_att_svr_rx_read_type },
    { BLE_ATT_OP_READ_REQ,             ble_att_svr_rx_read },
    { BLE_ATT_OP_WRITE_REQ,            ble_att_svr_rx_write },
};

#define BLE_ATT_RX_DISPATCH_SZ \
    (sizeof ble_att_rx_dispatch / sizeof ble_att_rx_dispatch[0])

static struct ble_att_rx_dispatch_entry *
ble_att_rx_dispatch_entry_find(uint8_t op)
{
    struct ble_att_rx_dispatch_entry *entry;
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

static int
ble_att_rx(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
           struct os_mbuf **om)
{
    struct ble_att_rx_dispatch_entry *entry;
    uint8_t op;
    int rc;

    rc = os_mbuf_copydata(*om, 0, 1, &op);
    if (rc != 0) {
        return EMSGSIZE;
    }

    entry = ble_att_rx_dispatch_entry_find(op);
    if (entry == NULL) {
        return EINVAL;
    }

    rc = entry->bde_fn(conn, chan, om);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_att_set_peer_mtu(struct ble_l2cap_chan *chan, uint16_t peer_mtu)
{
    if (peer_mtu < BLE_ATT_MTU_DFLT) {
        peer_mtu = BLE_ATT_MTU_DFLT;
    }

    chan->blc_peer_mtu = peer_mtu;
}

struct ble_l2cap_chan *
ble_att_create_chan(void)
{
    struct ble_l2cap_chan *chan;

    chan = ble_l2cap_chan_alloc();
    if (chan == NULL) {
        return NULL;
    }

    chan->blc_cid = BLE_L2CAP_CID_ATT;
    chan->blc_my_mtu = BLE_ATT_MTU_DFLT;
    chan->blc_default_mtu = BLE_ATT_MTU_DFLT;
    chan->blc_rx_fn = ble_att_rx;

    return chan;
}
