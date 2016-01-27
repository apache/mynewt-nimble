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

#include <string.h>
#include <errno.h>
#include <assert.h>
#include "console/console.h"
#include "nimble/ble.h"
#include "ble_hs_priv.h"
#include "ble_hs_conn.h"
#include "ble_l2cap_priv.h"
#include "ble_att_priv.h"
#include "ble_l2cap.h"
#include "ble_l2cap_sig.h"

typedef int ble_l2cap_sig_rx_fn(struct ble_hs_conn *conn,
                                struct ble_l2cap_chan *chan,
                                struct ble_l2cap_sig_hdr *hdr,
                                struct os_mbuf **om);

static int ble_l2cap_sig_update_req_rx(struct ble_hs_conn *conn,
                                       struct ble_l2cap_chan *chan,
                                       struct ble_l2cap_sig_hdr *hdr,
                                       struct os_mbuf **om);

static ble_l2cap_sig_rx_fn *ble_l2cap_sig_dispatch[] = {
    [BLE_L2CAP_SIG_OP_UPDATE_REQ] = ble_l2cap_sig_update_req_rx,
};

static ble_l2cap_sig_rx_fn *
ble_l2cap_sig_dispatch_get(uint8_t op)
{
    if (op > BLE_L2CAP_SIG_OP_MAX) {
        return NULL;
    }

    return ble_l2cap_sig_dispatch[op];
}

static int
ble_l2cap_sig_hdr_parse(void *payload, uint16_t len,
                        struct ble_l2cap_sig_hdr *hdr)
{
    uint8_t *u8ptr;

    if (len < BLE_L2CAP_SIG_HDR_SZ) {
        return BLE_HS_EBADDATA;
    }

    u8ptr = payload;
    hdr->op = u8ptr[0];
    hdr->identifier = u8ptr[1];
    hdr->length = le16toh(u8ptr + 2);

    return 0;
}

static int
ble_l2cap_sig_hdr_write(void *payload, uint16_t len,
                        struct ble_l2cap_sig_hdr *hdr)
{
    uint8_t *u8ptr;

    if (len < BLE_L2CAP_SIG_HDR_SZ) {
        return BLE_HS_EBADDATA;
    }

    u8ptr = payload;
    u8ptr[0] = hdr->op;
    u8ptr[1] = hdr->identifier;
    htole16(u8ptr + 2, hdr->length);

    return 0;
}

static int
ble_l2cap_sig_reject_write(void *payload, uint16_t len,
                           struct ble_l2cap_sig_hdr *hdr,
                           struct ble_l2cap_sig_reject *cmd)
{
    uint8_t *u8ptr;
    int rc;

    u8ptr = payload;
    rc = ble_l2cap_sig_hdr_write(u8ptr, len, hdr);
    if (rc != 0) {
        return rc;
    }

    u8ptr += BLE_L2CAP_SIG_HDR_SZ;
    len -= BLE_L2CAP_SIG_HDR_SZ;

    if (len < BLE_L2CAP_SIG_REJECT_MIN_SZ) {
        return BLE_HS_EMSGSIZE;
    }

    htole16(u8ptr, cmd->reason);

    return 0;
}

static int
ble_l2cap_sig_reject_tx(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                        uint8_t identifier, uint16_t reason)
{
    /* XXX: Add support for optional data field. */

    struct ble_l2cap_sig_reject cmd;
    struct ble_l2cap_sig_hdr hdr;
    struct os_mbuf *txom;
    void *v;
    int rc;

    txom = ble_att_get_pkthdr();
    if (txom == NULL) {
        return BLE_HS_ENOMEM;
    }

    v = os_mbuf_extend(txom,
                       BLE_L2CAP_SIG_HDR_SZ + BLE_L2CAP_SIG_REJECT_MIN_SZ);
    if (v == NULL) {
        return BLE_HS_ENOMEM;
    }

    hdr.op = BLE_L2CAP_SIG_OP_REJECT;
    hdr.identifier = identifier;
    hdr.length = BLE_L2CAP_SIG_REJECT_MIN_SZ;
    cmd.reason = reason;

    rc = ble_l2cap_sig_reject_write(
        v, BLE_L2CAP_SIG_HDR_SZ + BLE_L2CAP_SIG_REJECT_MIN_SZ, &hdr, &cmd);
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_tx(conn, chan, txom);
    return rc;
}

static int
ble_l2cap_sig_update_req_parse(void *payload, int len,
                               struct ble_l2cap_sig_update_req *req)
{
    uint8_t *u8ptr;

    if (len < BLE_L2CAP_SIG_UPDATE_REQ_SZ) {
        return BLE_HS_EBADDATA;
    }

    u8ptr = payload;

    req->itvl_min = le16toh(u8ptr + 0);
    req->itvl_max = le16toh(u8ptr + 2);
    req->slave_latency = le16toh(u8ptr + 4);
    req->timeout_multiplier = le16toh(u8ptr + 6);

    return 0;
}

static int
ble_l2cap_sig_update_rsp_write(void *payload, int len,
                               struct ble_l2cap_sig_hdr *hdr,
                               struct ble_l2cap_sig_update_rsp *cmd)
{
    uint8_t *u8ptr;
    int rc;

    u8ptr = payload;
    rc = ble_l2cap_sig_hdr_write(u8ptr, len, hdr);
    if (rc != 0) {
        return rc;
    }

    u8ptr += BLE_L2CAP_SIG_HDR_SZ;
    len -= BLE_L2CAP_SIG_HDR_SZ;

    if (len < BLE_L2CAP_SIG_UPDATE_RSP_SZ) {
        return BLE_HS_EMSGSIZE;
    }

    htole16(u8ptr, cmd->result);

    return 0;
}

static int
ble_l2cap_sig_update_rsp_tx(struct ble_hs_conn *conn,
                            struct ble_l2cap_chan *chan,
                            uint16_t identifier, uint16_t result)
{
    struct ble_l2cap_sig_update_rsp rsp;
    struct ble_l2cap_sig_hdr hdr;
    struct os_mbuf *txom;
    void *v;
    int rc;

    txom = ble_att_get_pkthdr();
    if (txom == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    v = os_mbuf_extend(txom,
                       BLE_L2CAP_SIG_HDR_SZ + BLE_L2CAP_SIG_UPDATE_RSP_SZ);
    if (v == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    hdr.op = BLE_L2CAP_SIG_OP_UPDATE_RSP;
    hdr.identifier = identifier;
    hdr.length = BLE_L2CAP_SIG_UPDATE_RSP_SZ;
    rsp.result = result;

    rc = ble_l2cap_sig_update_rsp_write(
        v, BLE_L2CAP_SIG_HDR_SZ + BLE_L2CAP_SIG_UPDATE_RSP_SZ, &hdr, &rsp);
    assert(rc == 0);

    rc = ble_l2cap_tx(conn, chan, txom);
    return rc;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

static int
ble_l2cap_sig_update_req_rx(struct ble_hs_conn *conn,
                            struct ble_l2cap_chan *chan,
                            struct ble_l2cap_sig_hdr *hdr,
                            struct os_mbuf **om)
{
    struct ble_l2cap_sig_update_req req;
    struct ble_gap_conn_upd_params params;
    int rc;

    *om = os_mbuf_pullup(*om, BLE_L2CAP_SIG_UPDATE_REQ_SZ);
    if (*om == NULL) {
        return BLE_HS_ENOMEM;
    }

    rc = ble_l2cap_sig_update_req_parse((*om)->om_data, (*om)->om_len, &req);
    assert(rc == 0);

    /* Only a master can process an update request. */
    if (!(conn->bhc_flags & BLE_HS_CONN_F_MASTER)) {
        ble_l2cap_sig_reject_tx(conn, chan, hdr->identifier,
                                BLE_L2CAP_ERR_CMD_NOT_UNDERSTOOD);
        return BLE_HS_ENOTSUP;
    }

    /* XXX: For now, always accept. */
    rc = ble_l2cap_sig_update_rsp_tx(conn, chan, hdr->identifier, 0);
    if (rc != 0) {
        return rc;
    }

    params.itvl_min = req.itvl_min;
    params.itvl_max = req.itvl_max;
    params.latency = req.slave_latency;
    params.supervision_timeout = req.timeout_multiplier;
    params.min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN;
    params.max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN;

    rc = ble_gap_conn_update_params(conn->bhc_handle, &params);
    if (rc != 0) {
        return rc;
    }

    return 0;
}


static int
ble_l2cap_sig_rx(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                 struct os_mbuf **om)
{
    ble_l2cap_sig_rx_fn *rx_cb;
    struct ble_l2cap_sig_hdr hdr;
    uint8_t u8;
    int rc;
    int i;

    console_printf("L2CAP - rxed signalling msg: ");
    for (i = 0; i < OS_MBUF_PKTLEN(*om); i++) {
        os_mbuf_copydata(*om, i, 1, &u8);
        console_printf("0x%02x ", u8);
    }
    console_printf("\n");

    *om = os_mbuf_pullup(*om, BLE_L2CAP_SIG_HDR_SZ);
    if (*om == NULL) {
        return BLE_HS_EBADDATA;
    }

    rc = ble_l2cap_sig_hdr_parse((*om)->om_data, (*om)->om_len, &hdr);
    assert(rc == 0);

    /* Strip L2CAP sig header from the front of the mbuf. */
    os_mbuf_adj(*om, BLE_L2CAP_SIG_HDR_SZ);

    if (OS_MBUF_PKTLEN(*om) != hdr.length) {
        /* XXX: Send reject? */
        return BLE_HS_EBADDATA;
    }

    rx_cb = ble_l2cap_sig_dispatch_get(hdr.op);
    if (rx_cb == NULL) {
        ble_l2cap_sig_reject_tx(conn, chan, hdr.identifier,
                                BLE_L2CAP_ERR_CMD_NOT_UNDERSTOOD);
        return BLE_HS_ENOTSUP;
    }

    rc = rx_cb(conn, chan, &hdr, om);
    return rc;
}

struct ble_l2cap_chan *
ble_l2cap_sig_create_chan(void)
{
    struct ble_l2cap_chan *chan;

    chan = ble_l2cap_chan_alloc();
    if (chan == NULL) {
        return NULL;
    }

    chan->blc_cid = BLE_L2CAP_CID_SIG;
    chan->blc_my_mtu = BLE_L2CAP_SIG_MTU;
    chan->blc_default_mtu = BLE_L2CAP_SIG_MTU;
    chan->blc_rx_fn = ble_l2cap_sig_rx;

    return chan;
}
