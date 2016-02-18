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
#include "ble_hs_priv.h"

int
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

int
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

int
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

/**
 * Locking restrictions:
 *     o Caller unlocks ble_hs_conn.
 */
static int
ble_l2cap_sig_tx(uint16_t conn_handle, struct os_mbuf *txom)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    STATS_INC(ble_l2cap_stats, sig_tx);

    ble_hs_conn_lock();

    rc = ble_hs_misc_conn_chan_find_reqd(conn_handle, BLE_L2CAP_CID_SIG,
                                         &conn, &chan);
    if (rc == 0) {
        rc = ble_l2cap_tx(conn, chan, txom);
    }

    ble_hs_conn_unlock();

    return rc;
}

int
ble_l2cap_sig_reject_tx(uint16_t conn_handle, uint8_t id, uint16_t reason)
{
    /* XXX: Add support for optional data field. */

    struct ble_l2cap_sig_reject cmd;
    struct ble_l2cap_sig_hdr hdr;
    struct os_mbuf *txom;
    void *v;
    int rc;

    txom = ble_hs_misc_pkthdr();
    if (txom == NULL) {
        return BLE_HS_ENOMEM;
    }

    v = os_mbuf_extend(txom,
                       BLE_L2CAP_SIG_HDR_SZ + BLE_L2CAP_SIG_REJECT_MIN_SZ);
    if (v == NULL) {
        return BLE_HS_ENOMEM;
    }

    hdr.op = BLE_L2CAP_SIG_OP_REJECT;
    hdr.identifier = id;
    hdr.length = BLE_L2CAP_SIG_REJECT_MIN_SZ;
    cmd.reason = reason;

    rc = ble_l2cap_sig_reject_write(
        v, BLE_L2CAP_SIG_HDR_SZ + BLE_L2CAP_SIG_REJECT_MIN_SZ, &hdr, &cmd);
    if (rc != 0) {
        return rc;
    }

    STATS_INC(ble_l2cap_stats, sig_rx);
    rc = ble_l2cap_sig_tx(conn_handle, txom);
    return rc;
}

int
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

int
ble_l2cap_sig_update_req_write(void *payload, int len,
                               struct ble_l2cap_sig_hdr *hdr,
                               struct ble_l2cap_sig_update_req *req)
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

    if (len < BLE_L2CAP_SIG_UPDATE_REQ_SZ) {
        return BLE_HS_EINVAL;
    }

    htole16(u8ptr + 0, req->itvl_min);
    htole16(u8ptr + 2, req->itvl_max);
    htole16(u8ptr + 4, req->slave_latency);
    htole16(u8ptr + 6, req->timeout_multiplier);

    return 0;
}

int
ble_l2cap_sig_update_rsp_parse(void *payload, int len,
                               struct ble_l2cap_sig_update_rsp *cmd)
{
    uint8_t *u8ptr;

    u8ptr = payload;

    if (len < BLE_L2CAP_SIG_UPDATE_RSP_SZ) {
        return BLE_HS_EMSGSIZE;
    }

    cmd->result = le16toh(u8ptr);

    return 0;
}

int
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

int
ble_l2cap_sig_update_req_tx(uint16_t conn_handle, uint8_t id,
                            struct ble_l2cap_sig_update_req *req)
{
    struct ble_l2cap_sig_hdr hdr;
    struct os_mbuf *txom;
    void *v;
    int rc;

    txom = ble_hs_misc_pkthdr();
    if (txom == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    v = os_mbuf_extend(txom,
                       BLE_L2CAP_SIG_HDR_SZ + BLE_L2CAP_SIG_UPDATE_REQ_SZ);
    if (v == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    hdr.op = BLE_L2CAP_SIG_OP_UPDATE_REQ;
    hdr.identifier = id;
    hdr.length = BLE_L2CAP_SIG_UPDATE_REQ_SZ;

    rc = ble_l2cap_sig_update_req_write(
        v, BLE_L2CAP_SIG_HDR_SZ + BLE_L2CAP_SIG_UPDATE_REQ_SZ, &hdr, req);
    assert(rc == 0);

    rc = ble_l2cap_sig_tx(conn_handle, txom);
    txom = NULL;

done:
    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_l2cap_sig_update_rsp_tx(uint16_t conn_handle, uint8_t id, uint16_t result)
{
    struct ble_l2cap_sig_update_rsp rsp;
    struct ble_l2cap_sig_hdr hdr;
    struct os_mbuf *txom;
    void *v;
    int rc;

    txom = ble_hs_misc_pkthdr();
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
    hdr.identifier = id;
    hdr.length = BLE_L2CAP_SIG_UPDATE_RSP_SZ;
    rsp.result = result;

    rc = ble_l2cap_sig_update_rsp_write(
        v, BLE_L2CAP_SIG_HDR_SZ + BLE_L2CAP_SIG_UPDATE_RSP_SZ, &hdr, &rsp);
    assert(rc == 0);

    rc = ble_l2cap_sig_tx(conn_handle, txom);
    return rc;

err:
    os_mbuf_free_chain(txom);
    return rc;
}
