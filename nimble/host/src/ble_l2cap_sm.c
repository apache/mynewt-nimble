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
#include <assert.h>
#include "console/console.h"
#include "nimble/ble.h"
#include "ble_hs_priv.h"
#include "ble_hs_conn.h"
#include "ble_gap_priv.h"
#include "ble_l2cap_priv.h"
#include "ble_att_priv.h"
#include "ble_l2cap_priv.h"
#include "ble_l2cap_sm.h"

typedef int ble_l2cap_sm_rx_fn(uint16_t conn_handle, uint8_t op,
                               struct os_mbuf **om);

static int ble_l2cap_sm_rx_noop(uint16_t conn_handle, uint8_t op,
                                struct os_mbuf **om);
static int ble_l2cap_sm_pair_req_rx(uint16_t conn_handle, uint8_t op,
                                    struct os_mbuf **om);

static ble_l2cap_sm_rx_fn * const ble_l2cap_sm_dispatch[] = {
   [BLE_L2CAP_SM_OP_PAIR_REQ] = ble_l2cap_sm_pair_req_rx,
   [BLE_L2CAP_SM_OP_PAIR_RSP] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_PAIR_CONFIRM] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_PAIR_RANDOM] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_PAIR_FAILED] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_ENC_INFO] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_MASTER_ID] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_IDENTITY_INFO] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_IDENTITY_ADDR_INFO] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_SIGN_INFO] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_SEC_REQ] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_PAIR_PUBLIC_KEY] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_PAIR_DHKEY_CHECK] = ble_l2cap_sm_rx_noop,
   [BLE_L2CAP_SM_OP_PAIR_KEYPRESS_NOTIFY] = ble_l2cap_sm_rx_noop,
};

/**
 * Lock restrictions: None.
 */
static ble_l2cap_sm_rx_fn *
ble_l2cap_sm_dispatch_get(uint8_t op)
{
    if (op > sizeof ble_l2cap_sm_dispatch / sizeof ble_l2cap_sm_dispatch[0]) {
        return NULL;
    }

    return ble_l2cap_sm_dispatch[op];
}

/**
 * Lock restrictions:
 *     o Caller unlocks ble_hs_conn.
 */
static int
ble_l2cap_sm_conn_chan_find(uint16_t conn_handle,
                            struct ble_hs_conn **out_conn,
                            struct ble_l2cap_chan **out_chan)
{
    ble_hs_conn_lock();

    *out_conn = ble_hs_conn_find(conn_handle);
    if (*out_conn != NULL) {
        *out_chan = ble_hs_conn_chan_find(*out_conn, BLE_L2CAP_CID_SM);
        assert(*out_chan != NULL);
    }

    ble_hs_conn_unlock();

    if (*out_conn == NULL) {
        return BLE_HS_ENOTCONN;
    } else {
        return 0;
    }
}

/**
 * Lock restrictions: None.
 */
static int
ble_l2cap_sm_rx_noop(uint16_t conn_handle, uint8_t op, struct os_mbuf **om)
{
    return 0;
}

static int
ble_l2cap_sm_pair_req_rx(uint16_t conn_handle, uint8_t op, struct os_mbuf **om)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    struct os_mbuf *txom;
    uint8_t *u;
    int rc;

    txom = NULL;

    rc = ble_l2cap_sm_conn_chan_find(conn_handle, &conn, &chan);
    if (rc != 0) {
        rc = BLE_HS_EUNKNOWN;
        goto err;
    }

    txom = ble_hs_misc_pkthdr();
    if (txom == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    u = os_mbuf_extend(txom, 2);
    if (u == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    /* Op. */
    u[0] = BLE_L2CAP_SM_OP_PAIR_FAILED;

    /* Reason: Pairing not supported. */
    u[1] = 5;

    rc = ble_l2cap_tx(conn, chan, txom);
    return rc;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

/**
 * Lock restrictions:
 *     o Caller unlocks ble_hs_conn.
 */
static int
ble_l2cap_sm_rx(uint16_t conn_handle, struct os_mbuf **om)
{
    ble_l2cap_sm_rx_fn *rx_cb;
    uint8_t op;
    int rc;

    BLE_HS_LOG(DEBUG, "L2CAP - rxed security manager msg: ");
    ble_hs_misc_log_mbuf(*om);
    BLE_HS_LOG(DEBUG, "\n");

    *om = os_mbuf_pullup(*om, 1);
    if (*om == NULL) {
        return BLE_HS_EBADDATA;
    }
    op = *(*om)->om_data;

    /* Strip L2CAP sm header from the front of the mbuf. */
    os_mbuf_adj(*om, 1);

    rx_cb = ble_l2cap_sm_dispatch_get(op);
    if (rx_cb != NULL) {
        rc = rx_cb(conn_handle, op, om);
    } else {
        rc = BLE_HS_ENOTSUP;
    }

    return rc;
}
/**
 * Lock restrictions: None.
 */
struct ble_l2cap_chan *
ble_l2cap_sm_create_chan(void)
{
    struct ble_l2cap_chan *chan;

    chan = ble_l2cap_chan_alloc();
    if (chan == NULL) {
        return NULL;
    }

    chan->blc_cid = BLE_L2CAP_CID_SM;
    chan->blc_my_mtu = BLE_L2CAP_SM_MTU;
    chan->blc_default_mtu = BLE_L2CAP_SM_MTU;
    chan->blc_rx_fn = ble_l2cap_sm_rx;

    return chan;
}

