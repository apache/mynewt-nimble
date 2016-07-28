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
#include "ble_hs_priv.h"

int
ble_l2cap_sig_init_cmd(uint8_t op, uint8_t id, uint8_t payload_len,
                       struct os_mbuf **out_om, void **out_payload_buf)
{
    struct ble_l2cap_sig_hdr hdr;
    struct os_mbuf *txom;
    void *v;
    int rc;

    *out_om = NULL;
    *out_payload_buf = NULL;

    txom = ble_hs_mbuf_l2cap_pkt();
    if (txom == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    v = os_mbuf_extend(txom, BLE_L2CAP_SIG_HDR_SZ + payload_len);
    if (v == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    hdr.op = op;
    hdr.identifier = id;
    hdr.length = TOFROMLE16(payload_len);

    ble_l2cap_sig_hdr_write(v, BLE_L2CAP_SIG_HDR_SZ, &hdr);

    *out_om = txom;
    *out_payload_buf = (uint8_t *)v + BLE_L2CAP_SIG_HDR_SZ;

    return 0;

err:
    os_mbuf_free(txom);
    return rc;
}

static void
ble_l2cap_sig_hdr_swap(struct ble_l2cap_sig_hdr *dst,
                       struct ble_l2cap_sig_hdr *src)
{
    dst->op = src->op;
    dst->identifier = src->identifier;
    dst->length = TOFROMLE16(src->length);
}

void
ble_l2cap_sig_hdr_parse(void *payload, uint16_t len,
                        struct ble_l2cap_sig_hdr *dst)
{
    BLE_HS_DBG_ASSERT(len >= BLE_L2CAP_SIG_HDR_SZ);
    ble_l2cap_sig_hdr_swap(dst, payload);
}

void
ble_l2cap_sig_hdr_write(void *payload, uint16_t len,
                        struct ble_l2cap_sig_hdr *src)
{
    BLE_HS_DBG_ASSERT(len >= BLE_L2CAP_SIG_HDR_SZ);
    ble_l2cap_sig_hdr_swap(payload, src);
}
static void
ble_l2cap_sig_reject_swap(struct ble_l2cap_sig_reject *dst,
                          struct ble_l2cap_sig_reject *src)
{
    dst->reason = TOFROMLE16(src->reason);
}

static void
ble_l2cap_sig_reject_write(void *payload, uint16_t len,
                           struct ble_l2cap_sig_reject *src,
                           void *data, int data_len)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(len >= BLE_L2CAP_SIG_REJECT_MIN_SZ + data_len);

    ble_l2cap_sig_reject_swap(payload, src);

    u8ptr = payload;
    u8ptr += BLE_L2CAP_SIG_REJECT_MIN_SZ;
    memcpy(u8ptr, data, data_len);
}

int
ble_l2cap_sig_reject_tx(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                        uint8_t id, uint16_t reason,
                        void *data, int data_len)
{
    struct ble_l2cap_sig_reject cmd;
    struct os_mbuf *txom;
    void *payload_buf;
    int rc;

    rc = ble_l2cap_sig_init_cmd(BLE_L2CAP_SIG_OP_REJECT, id,
                                BLE_L2CAP_SIG_REJECT_MIN_SZ + data_len, &txom,
                                &payload_buf);
    if (rc != 0) {
        return rc;
    }

    cmd.reason = reason;
    ble_l2cap_sig_reject_write(payload_buf, txom->om_len, &cmd,
                               data, data_len);

    STATS_INC(ble_l2cap_stats, sig_rx);
    rc = ble_l2cap_tx(conn, chan, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_l2cap_sig_reject_invalid_cid_tx(struct ble_hs_conn *conn,
                                    struct ble_l2cap_chan *chan, uint8_t id,
                                    uint16_t src_cid, uint16_t dst_cid)
{
    int rc;

    struct {
        uint16_t local_cid;
        uint16_t remote_cid;
    } data = {
        .local_cid = dst_cid,
        .remote_cid = src_cid,
    };

    rc = ble_l2cap_sig_reject_tx(conn, chan, id,
                                 BLE_L2CAP_SIG_ERR_INVALID_CID,
                                 &data, sizeof data);
    return rc;
}

static void
ble_l2cap_sig_update_req_swap(struct ble_l2cap_sig_update_req *dst,
                              struct ble_l2cap_sig_update_req *src)
{
    dst->itvl_min = TOFROMLE16(src->itvl_min);
    dst->itvl_max = TOFROMLE16(src->itvl_max);
    dst->slave_latency = TOFROMLE16(src->slave_latency);
    dst->timeout_multiplier = TOFROMLE16(src->timeout_multiplier);
}

void
ble_l2cap_sig_update_req_parse(void *payload, int len,
                               struct ble_l2cap_sig_update_req *dst)
{
    BLE_HS_DBG_ASSERT(len >= BLE_L2CAP_SIG_UPDATE_REQ_SZ);
    ble_l2cap_sig_update_req_swap(dst, payload);
}

void
ble_l2cap_sig_update_req_write(void *payload, int len,
                               struct ble_l2cap_sig_update_req *src)
{
    BLE_HS_DBG_ASSERT(len >= BLE_L2CAP_SIG_UPDATE_REQ_SZ);
    ble_l2cap_sig_update_req_swap(payload, src);
}

int
ble_l2cap_sig_update_req_tx(struct ble_hs_conn *conn,
                            struct ble_l2cap_chan *chan, uint8_t id,
                            struct ble_l2cap_sig_update_req *req)
{
    struct os_mbuf *txom;
    void *payload_buf;
    int rc;

    rc = ble_l2cap_sig_init_cmd(BLE_L2CAP_SIG_OP_UPDATE_REQ, id,
                                BLE_L2CAP_SIG_UPDATE_REQ_SZ, &txom,
                                &payload_buf);
    if (rc != 0) {
        return rc;
    }

    ble_l2cap_sig_update_req_write(payload_buf, BLE_L2CAP_SIG_UPDATE_REQ_SZ,
                                   req);

    rc = ble_l2cap_tx(conn, chan, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
ble_l2cap_sig_update_rsp_swap(struct ble_l2cap_sig_update_rsp *dst,
                              struct ble_l2cap_sig_update_rsp *src)
{
    dst->result = TOFROMLE16(src->result);
}

void
ble_l2cap_sig_update_rsp_parse(void *payload, int len,
                               struct ble_l2cap_sig_update_rsp *dst)
{
    BLE_HS_DBG_ASSERT(len >= BLE_L2CAP_SIG_UPDATE_RSP_SZ);
    ble_l2cap_sig_update_rsp_swap(dst, payload);
}

void
ble_l2cap_sig_update_rsp_write(void *payload, int len,
                               struct ble_l2cap_sig_update_rsp *src)
{
    BLE_HS_DBG_ASSERT(len >= BLE_L2CAP_SIG_UPDATE_RSP_SZ);
    ble_l2cap_sig_update_rsp_swap(payload, src);
}

int
ble_l2cap_sig_update_rsp_tx(struct ble_hs_conn *conn,
                            struct ble_l2cap_chan *chan, uint8_t id,
                            uint16_t result)
{
    struct ble_l2cap_sig_update_rsp rsp;
    struct os_mbuf *txom;
    void *payload_buf;
    int rc;

    rc = ble_l2cap_sig_init_cmd(BLE_L2CAP_SIG_OP_UPDATE_RSP, id,
                                BLE_L2CAP_SIG_UPDATE_RSP_SZ, &txom,
                                &payload_buf);
    if (rc != 0) {
        return rc;
    }

    rsp.result = result;
    ble_l2cap_sig_update_rsp_write(payload_buf, BLE_L2CAP_SIG_UPDATE_RSP_SZ,
                                   &rsp);

    rc = ble_l2cap_tx(conn, chan, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
