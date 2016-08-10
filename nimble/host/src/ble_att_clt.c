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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "os/os_mempool.h"
#include "nimble/ble.h"
#include "host/ble_uuid.h"
#include "ble_hs_priv.h"

static int
ble_att_clt_init_req(uint16_t initial_sz, struct os_mbuf **out_txom)
{
    struct os_mbuf *om;
    void *buf;
    int rc;

    *out_txom = NULL;

    om = ble_hs_mbuf_l2cap_pkt();
    if (om == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    buf = os_mbuf_extend(om, initial_sz);
    if (buf == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    /* The caller expects the initial buffer to be at the start of the mbuf. */
    BLE_HS_DBG_ASSERT(buf == om->om_data);

    *out_txom = om;
    return 0;

err:
    os_mbuf_free_chain(om);
    return rc;
}

static int
ble_att_clt_tx_req(uint16_t conn_handle, struct os_mbuf *txom)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    BLE_HS_DBG_ASSERT_EVAL(txom->om_len >= 1);
    ble_att_inc_tx_stat(txom->om_data[0]);

    ble_hs_lock();

    ble_att_conn_chan_find(conn_handle, &conn, &chan);
    ble_att_truncate_to_mtu(chan, txom);
    rc = ble_l2cap_tx(conn, chan, txom);

    ble_hs_unlock();

    if (rc != 0) {
        os_mbuf_free_chain(txom);
    }

    return rc;
}

/*****************************************************************************
 * $error response                                                           *
 *****************************************************************************/

int
ble_att_clt_rx_error(uint16_t conn_handle, struct os_mbuf **rxom)
{
    struct ble_att_error_rsp rsp;
    int rc;

    rc = ble_hs_mbuf_pullup_base(rxom, BLE_ATT_ERROR_RSP_SZ);
    if (rc != 0) {
        return rc;
    }

    ble_att_error_rsp_parse((*rxom)->om_data, (*rxom)->om_len, &rsp);
    BLE_ATT_LOG_CMD(0, "error rsp", conn_handle, ble_att_error_rsp_log, &rsp);

    ble_gattc_rx_err(conn_handle, &rsp);

    return 0;
}

/*****************************************************************************
 * $mtu exchange                                                             *
 *****************************************************************************/

int
ble_att_clt_tx_mtu(uint16_t conn_handle, const struct ble_att_mtu_cmd *req)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    struct os_mbuf *txom;
    int rc;

    BLE_ATT_LOG_CMD(1, "mtu req", conn_handle, ble_att_mtu_cmd_log, req);

    if (req->bamc_mtu < BLE_ATT_MTU_DFLT) {
        return BLE_HS_EINVAL;
    }

    rc = ble_att_clt_init_req(BLE_ATT_MTU_CMD_SZ, &txom);
    if (rc != 0) {
        return rc;
    }
    ble_att_mtu_req_write(txom->om_data, txom->om_len, req);

    rc = ble_att_clt_tx_req(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    ble_hs_lock();

    ble_att_conn_chan_find(conn_handle, &conn, &chan);
    chan->blc_flags |= BLE_L2CAP_CHAN_F_TXED_MTU;

    ble_hs_unlock();

    return rc;
}

int
ble_att_clt_rx_mtu(uint16_t conn_handle, struct os_mbuf **rxom)
{
    struct ble_att_mtu_cmd cmd;
    struct ble_l2cap_chan *chan;
    uint16_t mtu;
    int rc;

    mtu = 0;

    rc = ble_hs_mbuf_pullup_base(rxom, BLE_ATT_MTU_CMD_SZ);
    if (rc == 0) {
        ble_att_mtu_cmd_parse((*rxom)->om_data, (*rxom)->om_len, &cmd);
        BLE_ATT_LOG_CMD(0, "mtu rsp", conn_handle, ble_att_mtu_cmd_log, &cmd);

        ble_hs_lock();

        ble_att_conn_chan_find(conn_handle, NULL, &chan);
        ble_att_set_peer_mtu(chan, cmd.bamc_mtu);
        mtu = ble_l2cap_chan_mtu(chan);

        ble_hs_unlock();
    }

    ble_gattc_rx_mtu(conn_handle, rc, mtu);
    return rc;
}

/*****************************************************************************
 * $find information                                                         *
 *****************************************************************************/

int
ble_att_clt_tx_find_info(uint16_t conn_handle,
                         const struct ble_att_find_info_req *req)
{
#if !NIMBLE_OPT(ATT_CLT_FIND_INFO)
    return BLE_HS_ENOTSUP;
#endif

    struct os_mbuf *txom;
    int rc;

    BLE_ATT_LOG_CMD(1, "find info req", conn_handle,
                    ble_att_find_info_req_log, req);

    if (req->bafq_start_handle == 0 ||
        req->bafq_start_handle > req->bafq_end_handle) {

        return BLE_HS_EINVAL;
    }

    rc = ble_att_clt_init_req(BLE_ATT_FIND_INFO_REQ_SZ, &txom);
    if (rc != 0) {
        return rc;
    }
    ble_att_find_info_req_write(txom->om_data, txom->om_len, req);

    rc = ble_att_clt_tx_req(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_att_clt_parse_find_info_entry(struct os_mbuf **rxom, uint8_t rsp_format,
                                  struct ble_att_find_info_idata *idata)
{
    uint16_t uuid16;
    int entry_len;
    int rc;

    switch (rsp_format) {
    case BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT:
        entry_len = 2 + 2;
        break;

    case BLE_ATT_FIND_INFO_RSP_FORMAT_128BIT:
        entry_len = 2 + 16;
        break;

    default:
        return BLE_HS_EBADDATA;
    }

    rc = ble_hs_mbuf_pullup_base(rxom, entry_len);
    if (rc != 0) {
        return rc;
    }

    idata->attr_handle = le16toh((*rxom)->om_data);

    switch (rsp_format) {
    case BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT:
        uuid16 = le16toh((*rxom)->om_data + 2);
        rc = ble_uuid_16_to_128(uuid16, idata->uuid128);
        if (rc != 0) {
            return BLE_HS_EBADDATA;
        }
        break;

    case BLE_ATT_FIND_INFO_RSP_FORMAT_128BIT:
        rc = os_mbuf_copydata(*rxom, 2, 16, idata->uuid128);
        if (rc != 0) {
            return BLE_HS_EBADDATA;
        }
        break;

    default:
        BLE_HS_DBG_ASSERT(0);
        break;
    }

    os_mbuf_adj(*rxom, entry_len);
    return 0;
}

int
ble_att_clt_rx_find_info(uint16_t conn_handle, struct os_mbuf **om)
{
#if !NIMBLE_OPT(ATT_CLT_FIND_INFO)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_att_find_info_idata idata;
    struct ble_att_find_info_rsp rsp;
    int rc;

    rc = ble_hs_mbuf_pullup_base(om, BLE_ATT_FIND_INFO_RSP_BASE_SZ);
    if (rc != 0) {
        goto done;
    }

    ble_att_find_info_rsp_parse((*om)->om_data, (*om)->om_len, &rsp);
    BLE_ATT_LOG_CMD(0, "find info rsp", conn_handle, ble_att_find_info_rsp_log,
                    &rsp);

    /* Strip the response base from the front of the mbuf. */
    os_mbuf_adj((*om), BLE_ATT_FIND_INFO_RSP_BASE_SZ);

    while (OS_MBUF_PKTLEN(*om) > 0) {
        rc = ble_att_clt_parse_find_info_entry(om, rsp.bafp_format, &idata);
        if (rc != 0) {
            goto done;
        }

        /* Hand find-info entry to GATT. */
        ble_gattc_rx_find_info_idata(conn_handle, &idata);
    }

    rc = 0;

done:
    /* Notify GATT that response processing is done. */
    ble_gattc_rx_find_info_complete(conn_handle, rc);
    return rc;
}

/*****************************************************************************
 * $find by type value                                                       *
 *****************************************************************************/

int
ble_att_clt_tx_find_type_value(uint16_t conn_handle,
                               const struct ble_att_find_type_value_req *req,
                               const void *attribute_value, int value_len)
{
#if !NIMBLE_OPT(ATT_CLT_FIND_TYPE)
    return BLE_HS_ENOTSUP;
#endif

    BLE_ATT_LOG_CMD(1, "find type value req", conn_handle,
                    ble_att_find_type_value_req_log, req);

    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->bavq_start_handle == 0 ||
        req->bavq_start_handle > req->bavq_end_handle) {

        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_init_req(BLE_ATT_FIND_TYPE_VALUE_REQ_BASE_SZ, &txom);
    if (rc != 0) {
        goto err;
    }

    ble_att_find_type_value_req_write(txom->om_data, txom->om_len, req);
    rc = os_mbuf_append(txom, attribute_value, value_len);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = ble_att_clt_tx_req(conn_handle, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

static int
ble_att_clt_parse_find_type_value_hinfo(
    struct os_mbuf **om, struct ble_att_find_type_value_hinfo *dst)
{
    int rc;

    rc = os_mbuf_copydata(*om, 0, BLE_ATT_FIND_TYPE_VALUE_HINFO_BASE_SZ, dst);
    if (rc != 0) {
        return BLE_HS_EBADDATA;
    }

    dst->attr_handle = TOFROMLE16(dst->attr_handle);
    dst->group_end_handle = TOFROMLE16(dst->group_end_handle);

    return 0;
}

int
ble_att_clt_rx_find_type_value(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT(ATT_CLT_FIND_TYPE)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_att_find_type_value_hinfo hinfo;
    int rc;

    BLE_ATT_LOG_EMPTY_CMD(0, "find type value rsp", conn_handle);

    /* Reponse consists of a one-byte opcode (already verified) and a variable
     * length Handles-Information-List field.  Strip the opcode from the
     * response.
     */
    os_mbuf_adj(*rxom, BLE_ATT_FIND_TYPE_VALUE_RSP_BASE_SZ);

    /* Parse the Handles-Information-List field, passing each entry to GATT. */
    rc = 0;
    while (OS_MBUF_PKTLEN(*rxom) > 0) {
        rc = ble_att_clt_parse_find_type_value_hinfo(rxom, &hinfo);
        if (rc != 0) {
            break;
        }

        ble_gattc_rx_find_type_value_hinfo(conn_handle, &hinfo);
        os_mbuf_adj(*rxom, BLE_ATT_FIND_TYPE_VALUE_HINFO_BASE_SZ);
    }

    /* Notify GATT client that the full response has been parsed. */
    ble_gattc_rx_find_type_value_complete(conn_handle, rc);

    return 0;
}

/*****************************************************************************
 * $read by type                                                             *
 *****************************************************************************/

int
ble_att_clt_tx_read_type(uint16_t conn_handle,
                         const struct ble_att_read_type_req *req,
                         const void *uuid128)
{
#if !NIMBLE_OPT(ATT_CLT_READ_TYPE)
    return BLE_HS_ENOTSUP;
#endif

    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    BLE_ATT_LOG_CMD(1, "read type req", conn_handle,
                    ble_att_read_type_req_log, req);

    if (req->batq_start_handle == 0 ||
        req->batq_start_handle > req->batq_end_handle) {

        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_init_req(BLE_ATT_READ_TYPE_REQ_BASE_SZ, &txom);
    if (rc != 0) {
        goto err;
    }

    ble_att_read_type_req_write(txom->om_data, txom->om_len, req);
    rc = ble_uuid_append(txom, uuid128);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = ble_att_clt_tx_req(conn_handle, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return 0;
}

static int
ble_att_clt_parse_read_type_adata(struct os_mbuf **om, int data_len,
                                  struct ble_att_read_type_adata *adata)
{
    int rc;

    rc = ble_hs_mbuf_pullup_base(om, data_len);
    if (rc != 0) {
        return rc;
    }

    adata->att_handle = le16toh((*om)->om_data + 0);
    adata->value_len = data_len - BLE_ATT_READ_TYPE_ADATA_BASE_SZ;
    adata->value = (*om)->om_data + BLE_ATT_READ_TYPE_ADATA_BASE_SZ;

    return 0;
}

int
ble_att_clt_rx_read_type(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT(ATT_CLT_READ_TYPE)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_att_read_type_adata adata;
    struct ble_att_read_type_rsp rsp;
    int rc;

    rc = ble_hs_mbuf_pullup_base(rxom, BLE_ATT_READ_TYPE_RSP_BASE_SZ);
    if (rc != 0) {
        goto done;
    }

    ble_att_read_type_rsp_parse((*rxom)->om_data, (*rxom)->om_len, &rsp);
    BLE_ATT_LOG_CMD(0, "read type rsp", conn_handle, ble_att_read_type_rsp_log,
                    &rsp);

    /* Strip the response base from the front of the mbuf. */
    os_mbuf_adj(*rxom, BLE_ATT_READ_TYPE_RSP_BASE_SZ);

    /* Parse the Attribute Data List field, passing each entry to the GATT. */
    while (OS_MBUF_PKTLEN(*rxom) > 0) {
        rc = ble_att_clt_parse_read_type_adata(rxom, rsp.batp_length, &adata);
        if (rc != 0) {
            goto done;
        }

        ble_gattc_rx_read_type_adata(conn_handle, &adata);
        os_mbuf_adj(*rxom, rsp.batp_length);
    }

done:
    /* Notify GATT that the response is done being parsed. */
    ble_gattc_rx_read_type_complete(conn_handle, rc);
    return rc;

}

/*****************************************************************************
 * $read                                                                     *
 *****************************************************************************/

int
ble_att_clt_tx_read(uint16_t conn_handle, const struct ble_att_read_req *req)
{
#if !NIMBLE_OPT(ATT_CLT_READ)
    return BLE_HS_ENOTSUP;
#endif

    struct os_mbuf *txom;
    int rc;

    BLE_ATT_LOG_CMD(1, "read req", conn_handle, ble_att_read_req_log, req);

    if (req->barq_handle == 0) {
        return BLE_HS_EINVAL;
    }

    rc = ble_att_clt_init_req(BLE_ATT_READ_REQ_SZ, &txom);
    if (rc != 0) {
        return rc;
    }
    ble_att_read_req_write(txom->om_data, txom->om_len, req);

    rc = ble_att_clt_tx_req(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_att_clt_rx_read(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT(ATT_CLT_READ)
    return BLE_HS_ENOTSUP;
#endif

    BLE_ATT_LOG_EMPTY_CMD(0, "read rsp", conn_handle);

    /* Reponse consists of a one-byte opcode (already verified) and a variable
     * length Attribute Value field.  Strip the opcode from the response.
     */
    os_mbuf_adj(*rxom, BLE_ATT_READ_RSP_BASE_SZ);

    /* Pass the Attribute Value field to GATT. */
    ble_gattc_rx_read_rsp(conn_handle, 0, rxom);
    return 0;
}

/*****************************************************************************
 * $read blob                                                                *
 *****************************************************************************/

int
ble_att_clt_tx_read_blob(uint16_t conn_handle,
                         const struct ble_att_read_blob_req *req)
{
#if !NIMBLE_OPT(ATT_CLT_READ_BLOB)
    return BLE_HS_ENOTSUP;
#endif

    struct os_mbuf *txom;
    int rc;

    BLE_ATT_LOG_CMD(1, "read blob req", conn_handle,
                    ble_att_read_blob_req_log, req);

    if (req->babq_handle == 0) {
        return BLE_HS_EINVAL;
    }

    rc = ble_att_clt_init_req(BLE_ATT_READ_BLOB_REQ_SZ, &txom);
    if (rc != 0) {
        return rc;
    }
    ble_att_read_blob_req_write(txom->om_data, txom->om_len, req);

    rc = ble_att_clt_tx_req(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_att_clt_rx_read_blob(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT(ATT_CLT_READ_BLOB)
    return BLE_HS_ENOTSUP;
#endif

    BLE_ATT_LOG_EMPTY_CMD(0, "read blob rsp", conn_handle);

    /* Reponse consists of a one-byte opcode (already verified) and a variable
     * length Attribute Value field.  Strip the opcode from the response.
     */
    os_mbuf_adj(*rxom, BLE_ATT_READ_BLOB_RSP_BASE_SZ);

    /* Pass the Attribute Value field to GATT. */
    ble_gattc_rx_read_blob_rsp(conn_handle, 0, rxom);
    return 0;
}

/*****************************************************************************
 * $read multiple                                                            *
 *****************************************************************************/

static int
ble_att_clt_build_read_mult_req(const uint16_t *att_handles,
                                int num_att_handles,
                                struct os_mbuf **out_txom)
{
    struct os_mbuf *txom;
    void *buf;
    int rc;
    int i;

    *out_txom = NULL;

    rc = ble_att_clt_init_req(BLE_ATT_READ_MULT_REQ_BASE_SZ, &txom);
    if (rc != 0) {
        goto err;
    }
    ble_att_read_mult_req_write(txom->om_data, txom->om_len);

    for (i = 0; i < num_att_handles; i++) {
        buf = os_mbuf_extend(txom, 2);
        if (buf == NULL) {
            rc = BLE_HS_ENOMEM;
            goto err;
        }

        htole16(buf, att_handles[i]);
    }

    *out_txom = txom;
    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_att_clt_tx_read_mult(uint16_t conn_handle, const uint16_t *att_handles,
                         int num_att_handles)
{
#if !NIMBLE_OPT(ATT_CLT_READ_MULT)
    return BLE_HS_ENOTSUP;
#endif

    struct os_mbuf *txom;
    int rc;

    BLE_ATT_LOG_EMPTY_CMD(1, "reqd mult req", conn_handle);

    if (num_att_handles < 1) {
        return BLE_HS_EINVAL;
    }

    rc = ble_att_clt_build_read_mult_req(att_handles, num_att_handles, &txom);
    if (rc != 0) {
        return rc;
    }

    rc = ble_att_clt_tx_req(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_att_clt_rx_read_mult(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT(ATT_CLT_READ_MULT)
    return BLE_HS_ENOTSUP;
#endif

    BLE_ATT_LOG_EMPTY_CMD(0, "read mult rsp", conn_handle);

    /* Reponse consists of a one-byte opcode (already verified) and a variable
     * length Attribute Value field.  Strip the opcode from the response.
     */
    os_mbuf_adj(*rxom, BLE_ATT_READ_MULT_RSP_BASE_SZ);

    /* Pass the Attribute Value field to GATT. */
    ble_gattc_rx_read_mult_rsp(conn_handle, 0, rxom);
    return 0;
}

/*****************************************************************************
 * $read by group type                                                       *
 *****************************************************************************/

int
ble_att_clt_tx_read_group_type(uint16_t conn_handle,
                               const struct ble_att_read_group_type_req *req,
                               const void *uuid128)
{
#if !NIMBLE_OPT(ATT_CLT_READ_GROUP_TYPE)
    return BLE_HS_ENOTSUP;
#endif

    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    BLE_ATT_LOG_CMD(1, "read group type req", conn_handle,
                    ble_att_read_group_type_req_log, req);

    if (req->bagq_start_handle == 0 ||
        req->bagq_start_handle > req->bagq_end_handle) {

        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_init_req(BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ, &txom);
    if (rc != 0) {
        goto err;
    }
    ble_att_read_group_type_req_write(txom->om_data, txom->om_len, req);

    rc = ble_uuid_append(txom, uuid128);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_clt_tx_req(conn_handle, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

static int
ble_att_clt_parse_read_group_type_adata(
    struct os_mbuf **om, int data_len,
    struct ble_att_read_group_type_adata *adata)
{
    int rc;

    if (data_len < BLE_ATT_READ_GROUP_TYPE_ADATA_BASE_SZ + 1) {
        return BLE_HS_EMSGSIZE;
    }

    rc = ble_hs_mbuf_pullup_base(om, data_len);
    if (rc != 0) {
        return rc;
    }

    adata->att_handle = le16toh((*om)->om_data + 0);
    adata->end_group_handle = le16toh((*om)->om_data + 2);
    adata->value_len = data_len - BLE_ATT_READ_GROUP_TYPE_ADATA_BASE_SZ;
    adata->value = (*om)->om_data + BLE_ATT_READ_GROUP_TYPE_ADATA_BASE_SZ;

    return 0;
}

int
ble_att_clt_rx_read_group_type(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT(ATT_CLT_READ_GROUP_TYPE)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_att_read_group_type_adata adata;
    struct ble_att_read_group_type_rsp rsp;
    int rc;

    rc = ble_hs_mbuf_pullup_base(rxom, BLE_ATT_READ_GROUP_TYPE_RSP_BASE_SZ);
    if (rc != 0) {
        goto done;
    }

    ble_att_read_group_type_rsp_parse((*rxom)->om_data, (*rxom)->om_len, &rsp);
    BLE_ATT_LOG_CMD(0, "read group type rsp", conn_handle,
                    ble_att_read_group_type_rsp_log, &rsp);

    /* Strip the base from the front of the response. */
    os_mbuf_adj(*rxom, BLE_ATT_READ_GROUP_TYPE_RSP_BASE_SZ);

    /* Parse the Attribute Data List field, passing each entry to GATT. */
    while (OS_MBUF_PKTLEN(*rxom) > 0) {
        rc = ble_att_clt_parse_read_group_type_adata(rxom, rsp.bagp_length,
                                                     &adata);
        if (rc != 0) {
            goto done;
        }

        ble_gattc_rx_read_group_type_adata(conn_handle, &adata);
        os_mbuf_adj(*rxom, rsp.bagp_length);
    }

done:
    /* Notify GATT that the response is done being parsed. */
    ble_gattc_rx_read_group_type_complete(conn_handle, rc);
    return rc;
}

/*****************************************************************************
 * $write                                                                    *
 *****************************************************************************/

static int
ble_att_clt_tx_write_req_or_cmd(uint16_t conn_handle,
                                const struct ble_att_write_req *req,
                                struct os_mbuf *txom, int is_req)
{
    int rc;

    txom = os_mbuf_prepend_pullup(txom, BLE_ATT_WRITE_REQ_BASE_SZ);
    if (txom == NULL) {
        return BLE_HS_ENOMEM;
    }

    if (is_req) {
        ble_att_write_req_write(txom->om_data, BLE_ATT_WRITE_REQ_BASE_SZ, req);
    } else {
        ble_att_write_cmd_write(txom->om_data, BLE_ATT_WRITE_REQ_BASE_SZ, req);
    }

    rc = ble_att_clt_tx_req(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_att_clt_tx_write_req(uint16_t conn_handle,
                         const struct ble_att_write_req *req,
                         struct os_mbuf *txom)
{
#if !NIMBLE_OPT(ATT_CLT_WRITE)
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    BLE_ATT_LOG_CMD(1, "write req", conn_handle, ble_att_write_cmd_log, req);

    rc = ble_att_clt_tx_write_req_or_cmd(conn_handle, req, txom, 1);
    return rc;
}

int
ble_att_clt_tx_write_cmd(uint16_t conn_handle,
                         const struct ble_att_write_req *req,
                         struct os_mbuf *txom)
{
#if !NIMBLE_OPT(ATT_CLT_WRITE_NO_RSP)
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    BLE_ATT_LOG_CMD(1, "write cmd", conn_handle, ble_att_write_cmd_log, req);

    rc = ble_att_clt_tx_write_req_or_cmd(conn_handle, req, txom, 0);
    return rc;
}

int
ble_att_clt_rx_write(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT(ATT_CLT_WRITE)
    return BLE_HS_ENOTSUP;
#endif

    BLE_ATT_LOG_EMPTY_CMD(0, "write rsp", conn_handle);

    /* No payload. */
    ble_gattc_rx_write_rsp(conn_handle);
    return 0;
}

/*****************************************************************************
 * $prepare write request                                                    *
 *****************************************************************************/

int
ble_att_clt_tx_prep_write(uint16_t conn_handle,
                          const struct ble_att_prep_write_cmd *req,
                          struct os_mbuf *txom)
{
#if !NIMBLE_OPT(ATT_CLT_PREP_WRITE)
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    BLE_ATT_LOG_CMD(1, "prep write req", conn_handle,
                    ble_att_prep_write_cmd_log, req);

    if (req->bapc_handle == 0) {
        rc = BLE_HS_EINVAL;
        goto err;
    }

    if (req->bapc_offset + OS_MBUF_PKTLEN(txom) > BLE_ATT_ATTR_MAX_LEN) {
        rc = BLE_HS_EINVAL;
        goto err;
    }

    if (OS_MBUF_PKTLEN(txom) >
        ble_att_mtu(conn_handle) - BLE_ATT_PREP_WRITE_CMD_BASE_SZ) {

        rc = BLE_HS_EINVAL;
        goto err;
    }

    txom = os_mbuf_prepend_pullup(txom, BLE_ATT_PREP_WRITE_CMD_BASE_SZ);
    if (txom == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    ble_att_prep_write_req_write(txom->om_data, BLE_ATT_PREP_WRITE_CMD_BASE_SZ,
                                 req);

    rc = ble_att_clt_tx_req(conn_handle, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_att_clt_rx_prep_write(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT(ATT_CLT_PREP_WRITE)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_att_prep_write_cmd rsp;
    int rc;

    /* Initialize some values in case of early error. */
    memset(&rsp, 0, sizeof rsp);

    rc = ble_hs_mbuf_pullup_base(rxom, BLE_ATT_PREP_WRITE_CMD_BASE_SZ);
    if (rc != 0) {
        goto done;
    }

    ble_att_prep_write_rsp_parse((*rxom)->om_data, (*rxom)->om_len, &rsp);
    BLE_ATT_LOG_CMD(0, "prep write rsp", conn_handle,
                    ble_att_prep_write_cmd_log, &rsp);

    /* Strip the base from the front of the response. */
    os_mbuf_adj(*rxom, BLE_ATT_PREP_WRITE_CMD_BASE_SZ);

done:
    /* Notify GATT client that the full response has been parsed. */
    ble_gattc_rx_prep_write_rsp(conn_handle, rc, &rsp, rxom);
    return rc;
}

/*****************************************************************************
 * $execute write request                                                    *
 *****************************************************************************/

int
ble_att_clt_tx_exec_write(uint16_t conn_handle,
                          const struct ble_att_exec_write_req *req)
{
#if !NIMBLE_OPT(ATT_CLT_EXEC_WRITE)
    return BLE_HS_ENOTSUP;
#endif

    struct os_mbuf *txom;
    int rc;

    BLE_ATT_LOG_CMD(1, "exec write req", conn_handle,
                    ble_att_exec_write_req_log, req);

    if ((req->baeq_flags & BLE_ATT_EXEC_WRITE_F_RESERVED) != 0) {
        return BLE_HS_EINVAL;
    }

    rc = ble_att_clt_init_req(BLE_ATT_EXEC_WRITE_REQ_SZ, &txom);
    if (rc != 0) {
        return rc;
    }
    ble_att_exec_write_req_write(txom->om_data, txom->om_len, req);

    rc = ble_att_clt_tx_req(conn_handle, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_att_clt_rx_exec_write(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT(ATT_CLT_EXEC_WRITE)
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    BLE_ATT_LOG_EMPTY_CMD(0, "exec write rsp", conn_handle);

    rc = ble_hs_mbuf_pullup_base(rxom, BLE_ATT_EXEC_WRITE_RSP_SZ);
    if (rc == 0) {
        ble_att_exec_write_rsp_parse((*rxom)->om_data, (*rxom)->om_len);
    }

    ble_gattc_rx_exec_write_rsp(conn_handle, rc);
    return rc;
}

/*****************************************************************************
 * $handle value notification                                                *
 *****************************************************************************/

int
ble_att_clt_tx_notify(uint16_t conn_handle,
                      const struct ble_att_notify_req *req,
                      struct os_mbuf *txom)
{
#if !NIMBLE_OPT(ATT_CLT_NOTIFY)
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    BLE_ATT_LOG_CMD(1, "notify req", conn_handle, ble_att_notify_req_log, req);

    if (req->banq_handle == 0) {
        rc = BLE_HS_EINVAL;
        goto err;
    }

    txom = os_mbuf_prepend_pullup(txom, BLE_ATT_NOTIFY_REQ_BASE_SZ);
    if (txom == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }
    ble_att_notify_req_write(txom->om_data, BLE_ATT_NOTIFY_REQ_BASE_SZ, req);

    rc = ble_att_clt_tx_req(conn_handle, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

/*****************************************************************************
 * $handle value indication                                                  *
 *****************************************************************************/

int
ble_att_clt_tx_indicate(uint16_t conn_handle,
                        const struct ble_att_indicate_req *req,
                        struct os_mbuf *txom)
{
#if !NIMBLE_OPT(ATT_CLT_INDICATE)
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    BLE_ATT_LOG_CMD(1, "indicate req", conn_handle, ble_att_indicate_req_log,
                    req);

    if (req->baiq_handle == 0) {
        rc = BLE_HS_EINVAL;
        goto err;
    }

    txom = os_mbuf_prepend_pullup(txom, BLE_ATT_INDICATE_REQ_BASE_SZ);
    if (txom == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    ble_att_indicate_req_write(txom->om_data, BLE_ATT_INDICATE_REQ_BASE_SZ,
                               req);

    rc = ble_att_clt_tx_req(conn_handle, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_att_clt_rx_indicate(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT(ATT_CLT_INDICATE)
    return BLE_HS_ENOTSUP;
#endif

    BLE_ATT_LOG_EMPTY_CMD(0, "indicate rsp", conn_handle);

    /* No payload. */
    ble_gattc_rx_indicate_rsp(conn_handle);
    return 0;
}
