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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "os/os_mempool.h"
#include "nimble/ble.h"
#include "host/ble_uuid.h"
#include "ble_gatt_priv.h"
#include "ble_hs_priv.h"
#include "ble_hs_conn.h"
#include "ble_att_cmd.h"
#include "ble_att_priv.h"

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
static int
ble_att_clt_init_req(struct ble_hs_conn *conn, struct ble_l2cap_chan **chan,
                     struct os_mbuf **txom, uint16_t initial_sz)
{
    void *buf;
    int rc;

    *txom = NULL;

    *chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    assert(*chan != NULL);

    if (!ble_hs_conn_can_tx(conn)) {
        rc = BLE_HS_ECONGESTED;
        goto err;
    }

    *txom = ble_hs_misc_pkthdr();
    if (*txom == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    buf = os_mbuf_extend(*txom, initial_sz);
    if (buf == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    /* The caller expects the initial buffer to be at the start of the mbuf. */
    assert(buf == (*txom)->om_data);

    return 0;

err:
    os_mbuf_free_chain(*txom);
    *txom = NULL;
    return rc;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
static int
ble_att_clt_append_blob(struct ble_l2cap_chan *chan, struct os_mbuf *txom,
                        void *blob, int blob_len)
{
    uint16_t mtu;
    int extra_len;
    int cmd_len;
    int rc;

    if (blob_len < 0) {
        return BLE_HS_EINVAL;
    }
    if (blob_len == 0) {
        return 0;
    }

    mtu = ble_l2cap_chan_mtu(chan);
    cmd_len = OS_MBUF_PKTLEN(txom) + blob_len;
    extra_len = cmd_len - mtu;
    if (extra_len > 0) {
        blob_len -= extra_len;
    }

    rc = os_mbuf_append(txom, blob, blob_len);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $error response                                                           *
 *****************************************************************************/

/**
 * Lock restrictions: Caller must NOT lock ble_hs_conn mutex.
 */
int
ble_att_clt_rx_error(uint16_t conn_handle, struct os_mbuf **om)
{
    struct ble_att_error_rsp rsp;
    int rc;

    *om = os_mbuf_pullup(*om, BLE_ATT_ERROR_RSP_SZ);
    if (*om == NULL) {
        return BLE_HS_ENOMEM;
    }

    rc = ble_att_error_rsp_parse((*om)->om_data, (*om)->om_len, &rsp);
    if (rc != 0) {
        return rc;
    }

    ble_gattc_rx_err(conn_handle, &rsp);

    return 0;
}

/*****************************************************************************
 * $mtu exchange                                                             *
 *****************************************************************************/

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_tx_mtu(struct ble_hs_conn *conn, struct ble_att_mtu_cmd *req)
{
    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->bamc_mtu < BLE_ATT_MTU_DFLT) {
        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_init_req(conn, &chan, &txom, BLE_ATT_MTU_CMD_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_mtu_req_write(txom->om_data, txom->om_len, req);
    assert(rc == 0);

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    chan->blc_flags |= BLE_L2CAP_CHAN_F_TXED_MTU;

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

/**
 * Lock restrictions: Caller must NOT lock ble_hs_conn mutex.
 */
int
ble_att_clt_rx_mtu(uint16_t conn_handle, struct os_mbuf **om)
{
    struct ble_att_mtu_cmd rsp;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint16_t mtu;
    int rc;

    mtu = 0;

    *om = os_mbuf_pullup(*om, BLE_ATT_MTU_CMD_SZ);
    if (*om == NULL) {
        rc = BLE_HS_ENOMEM;
    } else {
        rc = ble_att_mtu_cmd_parse((*om)->om_data, (*om)->om_len, &rsp);
        assert(rc == 0);

        ble_hs_conn_lock();

        conn = ble_hs_conn_find(conn_handle);
        if (conn == NULL) {
            rc = BLE_HS_ENOTCONN;
        } else {
            chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
            assert(chan != NULL);

            ble_att_set_peer_mtu(chan, rsp.bamc_mtu);
            mtu = ble_l2cap_chan_mtu(chan);
            rc = 0;
        }

        ble_hs_conn_unlock();
    }

    ble_gattc_rx_mtu(conn_handle, rc, mtu);
    return rc;
}

/*****************************************************************************
 * $find information                                                         *
 *****************************************************************************/

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_tx_find_info(struct ble_hs_conn *conn,
                         struct ble_att_find_info_req *req)
{
#if !NIMBLE_OPT_ATT_CLT_FIND_INFO
    return BLE_HS_ENOTSUP;
#endif

    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->bafq_start_handle == 0 ||
        req->bafq_start_handle > req->bafq_end_handle) {

        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_init_req(conn, &chan, &txom, BLE_ATT_FIND_INFO_REQ_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_find_info_req_write(txom->om_data, txom->om_len, req);
    assert(rc == 0);

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_rx_find_info(uint16_t conn_handle, struct os_mbuf **om)
{
#if !NIMBLE_OPT_ATT_CLT_FIND_INFO
    return BLE_HS_ENOTSUP;
#endif

    struct ble_att_find_info_rsp rsp;
    struct ble_att_find_info_idata idata;
    struct os_mbuf *rxom;
    uint16_t uuid16;
    int rc;

    rxom = NULL;

    *om = os_mbuf_pullup(*om, BLE_ATT_FIND_INFO_RSP_BASE_SZ);
    if (*om == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    /* Double indirection is unwieldy; use rxom for remainder of function. */
    rxom = *om;

    rc = ble_att_find_info_rsp_parse(rxom->om_data, rxom->om_len, &rsp);
    assert(rc == 0);
    os_mbuf_adj(rxom, BLE_ATT_FIND_INFO_RSP_BASE_SZ);

    idata.attr_handle = 0;
    while (OS_MBUF_PKTLEN(rxom) > 0) {
        rxom = os_mbuf_pullup(rxom, 2);
        if (rxom == NULL) {
            rc = BLE_HS_EBADDATA;
            goto done;
        }

        idata.attr_handle = le16toh(rxom->om_data);
        os_mbuf_adj(rxom, 2);

        switch (rsp.bafp_format) {
        case BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT:
            rxom = os_mbuf_pullup(rxom, 2);
            if (rxom == NULL) {
                rc = BLE_HS_EBADDATA;
                goto done;
            }
            uuid16 = le16toh(rxom->om_data);
            os_mbuf_adj(rxom, 2);

            rc = ble_uuid_16_to_128(uuid16, idata.uuid128);
            if (rc != 0) {
                rc = BLE_HS_EBADDATA;
                goto done;
            }
            break;

        case BLE_ATT_FIND_INFO_RSP_FORMAT_128BIT:
            rc = os_mbuf_copydata(rxom, 0, 16, idata.uuid128);
            if (rc != 0) {
                rc = BLE_HS_EBADDATA;
                goto done;
            }
            os_mbuf_adj(rxom, 16);
            break;

        default:
            rc = BLE_HS_EBADDATA;
            goto done;
        }

        ble_gattc_rx_find_info_idata(conn_handle, &idata);
    }

    rc = 0;

done:
    *om = rxom;

    ble_gattc_rx_find_info_complete(conn_handle, rc);
    return rc;
}

/*****************************************************************************
 * $find by type value                                                       *
 *****************************************************************************/

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_tx_find_type_value(struct ble_hs_conn *conn,
                               struct ble_att_find_type_value_req *req,
                               void *attribute_value, int value_len)
{
#if !NIMBLE_OPT_ATT_CLT_FIND_TYPE
    return BLE_HS_ENOTSUP;
#endif

    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->bavq_start_handle == 0 ||
        req->bavq_start_handle > req->bavq_end_handle) {

        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_init_req(conn, &chan, &txom,
                              BLE_ATT_FIND_TYPE_VALUE_REQ_BASE_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_find_type_value_req_write(txom->om_data, txom->om_len, req);
    assert(rc == 0);

    rc = os_mbuf_append(txom, attribute_value, value_len);
    if (rc != 0) {
        rc = BLE_HS_EMSGSIZE;
        goto err;
    }

    rc = ble_l2cap_tx(conn, chan, txom);
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
    struct os_mbuf **om, struct ble_att_find_type_value_hinfo *hinfo)
{
    *om = os_mbuf_pullup(*om, BLE_ATT_FIND_TYPE_VALUE_HINFO_BASE_SZ);
    if (*om == NULL) {
        return BLE_HS_ENOMEM;
    }

    hinfo->attr_handle = le16toh((*om)->om_data + 0);
    hinfo->group_end_handle = le16toh((*om)->om_data + 2);

    return 0;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_rx_find_type_value(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT_ATT_CLT_FIND_TYPE
    return BLE_HS_ENOTSUP;
#endif

    struct ble_att_find_type_value_hinfo hinfo;
    int rc;

    /* Reponse consists of a one-byte opcode (already verified) and a variable
     * length Handles Information List field.  Strip the opcode from the
     * response.
     */
    os_mbuf_adj(*rxom, BLE_ATT_FIND_TYPE_VALUE_RSP_BASE_SZ);

    /* Parse the Handles Information List field, passing each entry to GATT. */
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

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_tx_read_type(struct ble_hs_conn *conn,
                         struct ble_att_read_type_req *req,
                         void *uuid128)
{
#if !NIMBLE_OPT_ATT_CLT_READ_TYPE
    return BLE_HS_ENOTSUP;
#endif

    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->batq_start_handle == 0 ||
        req->batq_start_handle > req->batq_end_handle) {

        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_init_req(conn, &chan, &txom,
                              BLE_ATT_READ_TYPE_REQ_BASE_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_read_type_req_write(txom->om_data, txom->om_len, req);
    assert(rc == 0);

    rc = ble_uuid_append(txom, uuid128);
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

/**
 * Lock restrictions: None.
 */
static int
ble_att_clt_parse_read_type_adata(struct os_mbuf **om, int data_len,
                                  struct ble_att_read_type_adata *adata)
{
    if (data_len <= BLE_ATT_READ_TYPE_ADATA_BASE_SZ) {
        return BLE_HS_EMSGSIZE;
    }

    *om = os_mbuf_pullup(*om, data_len);
    if (*om == NULL) {
        return BLE_HS_ENOMEM;
    }

    adata->att_handle = le16toh((*om)->om_data + 0);
    adata->value_len = data_len - BLE_ATT_READ_TYPE_ADATA_BASE_SZ;
    adata->value = (*om)->om_data + BLE_ATT_READ_TYPE_ADATA_BASE_SZ;

    return 0;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_rx_read_type(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT_ATT_CLT_READ_TYPE
    return BLE_HS_ENOTSUP;
#endif

    struct ble_att_read_type_adata adata;
    struct ble_att_read_type_rsp rsp;
    int rc;

    *rxom = os_mbuf_pullup(*rxom, BLE_ATT_READ_TYPE_RSP_BASE_SZ);
    if (*rxom == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    rc = ble_att_read_type_rsp_parse((*rxom)->om_data, (*rxom)->om_len, &rsp);
    assert(rc == 0);

    /* Strip the base from the front of the response. */
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

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_tx_read(struct ble_hs_conn *conn, struct ble_att_read_req *req)
{
#if !NIMBLE_OPT_ATT_CLT_READ
    return BLE_HS_ENOTSUP;
#endif

    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->barq_handle == 0) {
        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_init_req(conn, &chan, &txom, BLE_ATT_READ_REQ_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_read_req_write(txom->om_data, txom->om_len, req);
    assert(rc == 0);

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

/**
 * Lock restrictions: Caller must NOT lock ble_hs_conn mutex.
 */
int
ble_att_clt_rx_read(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT_ATT_CLT_READ
    return BLE_HS_ENOTSUP;
#endif

    void *value;
    int value_len;
    int rc;

    value = NULL;
    value_len = 0;

    /* Reponse consists of a one-byte opcode (already verified) and a variable
     * length Attribute Value field.  Strip the opcode from the response.
     */
    os_mbuf_adj(*rxom, BLE_ATT_READ_RSP_BASE_SZ);

    /* Pass the Attribute Value field to the GATT. */
    *rxom = os_mbuf_pullup(*rxom, OS_MBUF_PKTLEN(*rxom));
    if (*rxom == NULL) {
        rc = BLE_HS_EBADDATA;
        goto done;
    }

    value_len = (*rxom)->om_len;
    value = (*rxom)->om_data;

    rc = 0;

done:
    ble_gattc_rx_read_rsp(conn_handle, rc, value, value_len);
    return rc;
}

/*****************************************************************************
 * $read blob                                                                *
 *****************************************************************************/

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_tx_read_blob(struct ble_hs_conn *conn,
                         struct ble_att_read_blob_req *req)
{
#if !NIMBLE_OPT_ATT_CLT_READ_BLOB
    return BLE_HS_ENOTSUP;
#endif

    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->babq_handle == 0) {
        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_init_req(conn, &chan, &txom, BLE_ATT_READ_BLOB_REQ_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_read_blob_req_write(txom->om_data, txom->om_len, req);
    assert(rc == 0);

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_rx_read_blob(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT_ATT_CLT_READ_BLOB
    return BLE_HS_ENOTSUP;
#endif

    void *value;
    int value_len;
    int rc;

    value = NULL;
    value_len = 0;

    /* Reponse consists of a one-byte opcode (already verified) and a variable
     * length Attribute Value field.  Strip the opcode from the response.
     */
    os_mbuf_adj(*rxom, BLE_ATT_READ_BLOB_RSP_BASE_SZ);

    *rxom = os_mbuf_pullup(*rxom, OS_MBUF_PKTLEN(*rxom));
    if (*rxom == NULL) {
        rc = BLE_HS_EBADDATA;
        goto done;
    }

    value_len = (*rxom)->om_len;
    value = (*rxom)->om_data;

    rc = 0;

done:
    /* Pass the Attribute Value field to GATT. */
    ble_gattc_rx_read_blob_rsp(conn_handle, rc, value, value_len);
    return rc;
}

/*****************************************************************************
 * $read multiple                                                            *
 *****************************************************************************/

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_tx_read_mult(struct ble_hs_conn *conn,
                         uint16_t *handles, int num_handles)
{
#if !NIMBLE_OPT_ATT_CLT_READ_MULT
    return BLE_HS_ENOTSUP;
#endif

    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    void *buf;
    int rc;
    int i;

    txom = NULL;

    if (num_handles < 1) {
        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_init_req(conn, &chan, &txom,
                              BLE_ATT_READ_MULT_REQ_BASE_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_read_mult_req_write(txom->om_data, txom->om_len);
    assert(rc == 0);

    for (i = 0; i < num_handles; i++) {
        buf = os_mbuf_extend(txom, 2);
        if (buf == NULL) {
            rc = BLE_HS_ENOMEM;
            goto err;
        }

        htole16(buf, handles[i]);
    }

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_rx_read_mult(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT_ATT_CLT_READ_MULT
    return BLE_HS_ENOTSUP;
#endif

    void *value;
    int value_len;
    int rc;

    value = NULL;
    value_len = 0;

    /* Reponse consists of a one-byte opcode (already verified) and a variable
     * length Attribute Value field.  Strip the opcode from the response.
     */
    os_mbuf_adj(*rxom, BLE_ATT_READ_MULT_RSP_BASE_SZ);

    *rxom = os_mbuf_pullup(*rxom, OS_MBUF_PKTLEN(*rxom));
    if (*rxom == NULL) {
        rc = BLE_HS_EBADDATA;
        goto done;
    }

    value_len = (*rxom)->om_len;
    value = (*rxom)->om_data;

    rc = 0;

done:
    /* Pass the Attribute Value field to GATT. */
    ble_gattc_rx_read_mult_rsp(conn_handle, rc, value, value_len);
    return rc;
}

/*****************************************************************************
 * $read by group type                                                       *
 *****************************************************************************/

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_tx_read_group_type(struct ble_hs_conn *conn,
                               struct ble_att_read_group_type_req *req,
                               void *uuid128)
{
#if !NIMBLE_OPT_ATT_CLT_READ_GROUP_TYPE
    return BLE_HS_ENOTSUP;
#endif

    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->bagq_start_handle == 0 ||
        req->bagq_start_handle > req->bagq_end_handle) {

        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_init_req(conn, &chan, &txom,
                              BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_read_group_type_req_write(txom->om_data, txom->om_len, req);
    assert(rc == 0);

    rc = ble_uuid_append(txom, uuid128);
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

/**
 * Lock restrictions: None.
 */
static int
ble_att_clt_parse_read_group_type_adata(
    struct os_mbuf **om, int data_len,
    struct ble_att_read_group_type_adata *adata)
{
    if (data_len < BLE_ATT_READ_GROUP_TYPE_ADATA_BASE_SZ + 1) {
        return BLE_HS_EMSGSIZE;
    }

    *om = os_mbuf_pullup(*om, data_len);
    if (*om == NULL) {
        return BLE_HS_ENOMEM;
    }

    adata->att_handle = le16toh((*om)->om_data + 0);
    adata->end_group_handle = le16toh((*om)->om_data + 2);
    adata->value_len = data_len - BLE_ATT_READ_GROUP_TYPE_ADATA_BASE_SZ;
    adata->value = (*om)->om_data + BLE_ATT_READ_GROUP_TYPE_ADATA_BASE_SZ;

    return 0;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_rx_read_group_type(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT_ATT_CLT_READ_GROUP_TYPE
    return BLE_HS_ENOTSUP;
#endif

    struct ble_att_read_group_type_adata adata;
    struct ble_att_read_group_type_rsp rsp;
    int rc;

    *rxom = os_mbuf_pullup(*rxom, BLE_ATT_READ_GROUP_TYPE_RSP_BASE_SZ);
    if (*rxom == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    rc = ble_att_read_group_type_rsp_parse((*rxom)->om_data, (*rxom)->om_len,
                                           &rsp);
    assert(rc == 0);

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

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
static int
ble_att_clt_tx_write_req_or_cmd(struct ble_hs_conn *conn,
                                struct ble_att_write_req *req,
                                void *value, uint16_t value_len,
                                int is_req)
{
    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    rc = ble_att_clt_init_req(conn, &chan, &txom, BLE_ATT_WRITE_REQ_BASE_SZ);
    if (rc != 0) {
        goto err;
    }

    if (is_req) {
        rc = ble_att_write_req_write(txom->om_data, txom->om_len, req);
    } else {
        rc = ble_att_write_cmd_write(txom->om_data, txom->om_len, req);
    }
    assert(rc == 0);

    rc = ble_att_clt_append_blob(chan, txom, value, value_len);
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_tx_write_req(struct ble_hs_conn *conn,
                         struct ble_att_write_req *req,
                         void *value, uint16_t value_len)
{
#if !NIMBLE_OPT_ATT_CLT_WRITE
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    rc = ble_att_clt_tx_write_req_or_cmd(conn, req, value, value_len, 1);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_tx_write_cmd(struct ble_hs_conn *conn,
                         struct ble_att_write_req *req,
                         void *value, uint16_t value_len)
{
#if !NIMBLE_OPT_ATT_CLT_WRITE_NO_RSP
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    rc = ble_att_clt_tx_write_req_or_cmd(conn, req, value, value_len, 0);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_rx_write(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT_ATT_CLT_WRITE
    return BLE_HS_ENOTSUP;
#endif

    /* No payload. */
    ble_gattc_rx_write_rsp(conn_handle);
    return 0;
}

/*****************************************************************************
 * $prepare write request                                                    *
 *****************************************************************************/

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_tx_prep_write(struct ble_hs_conn *conn,
                          struct ble_att_prep_write_cmd *req,
                          void *value, uint16_t value_len)
{
#if !NIMBLE_OPT_ATT_CLT_PREP_WRITE
    return BLE_HS_ENOTSUP;
#endif

    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->bapc_handle == 0) {
        rc = BLE_HS_EINVAL;
        goto err;
    }

    if (req->bapc_offset + value_len > BLE_ATT_ATTR_MAX_LEN) {
        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_init_req(conn, &chan, &txom,
                              BLE_ATT_PREP_WRITE_CMD_BASE_SZ);
    if (rc != 0) {
        goto err;
    }

    if (value_len >
        ble_l2cap_chan_mtu(chan) - BLE_ATT_PREP_WRITE_CMD_BASE_SZ) {

        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_prep_write_req_write(txom->om_data, txom->om_len, req);
    assert(rc == 0);

    rc = ble_att_clt_append_blob(chan, txom, value, value_len);
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_rx_prep_write(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT_ATT_CLT_PREP_WRITE
    return BLE_HS_ENOTSUP;
#endif

    struct ble_att_prep_write_cmd rsp;
    int rc;

    if (OS_MBUF_PKTLEN(*rxom) < BLE_ATT_PREP_WRITE_CMD_BASE_SZ) {
        rc = BLE_HS_EBADDATA;
        goto err;
    }

    *rxom = os_mbuf_pullup(*rxom, OS_MBUF_PKTLEN(*rxom));
    if (*rxom == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = ble_att_prep_write_rsp_parse((*rxom)->om_data, (*rxom)->om_len,
                                      &rsp);
    assert(rc == 0);

    /* Strip the base from the front of the response. */
    os_mbuf_adj(*rxom, BLE_ATT_PREP_WRITE_CMD_BASE_SZ);

    ble_gattc_rx_prep_write_rsp(conn_handle, 0, &rsp, (*rxom)->om_data,
                                (*rxom)->om_len);
    return 0;

err:
    ble_gattc_rx_prep_write_rsp(conn_handle, rc, NULL, NULL, 0);
    return rc;
}

/*****************************************************************************
 * $execute write request                                                    *
 *****************************************************************************/

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_tx_exec_write(struct ble_hs_conn *conn,
                          struct ble_att_exec_write_req *req)
{
#if !NIMBLE_OPT_ATT_CLT_EXEC_WRITE
    return BLE_HS_ENOTSUP;
#endif

    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if ((req->baeq_flags & BLE_ATT_EXEC_WRITE_F_RESERVED) != 0) {
        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_init_req(conn, &chan, &txom, BLE_ATT_EXEC_WRITE_REQ_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_exec_write_req_write(txom->om_data, txom->om_len, req);
    assert(rc == 0);

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_rx_exec_write(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT_ATT_CLT_EXEC_WRITE
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    *rxom = os_mbuf_pullup(*rxom, BLE_ATT_EXEC_WRITE_RSP_SZ);
    if (*rxom == NULL) {
        rc = BLE_HS_EBADDATA;
    } else {
        rc = ble_att_exec_write_rsp_parse((*rxom)->om_data, (*rxom)->om_len);
        assert(rc == 0);
    }

    ble_gattc_rx_exec_write_rsp(conn_handle, rc);
    return rc;
}

/*****************************************************************************
 * $handle value notification                                                *
 *****************************************************************************/

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_tx_notify(struct ble_hs_conn *conn, struct ble_att_notify_req *req,
                      void *value, uint16_t value_len)
{
#if !NIMBLE_OPT_ATT_CLT_NOTIFY
    return BLE_HS_ENOTSUP;
#endif

    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->banq_handle == 0) {
        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_init_req(conn, &chan, &txom, BLE_ATT_NOTIFY_REQ_BASE_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_notify_req_write(txom->om_data, txom->om_len, req);
    assert(rc == 0);

    rc = ble_att_clt_append_blob(chan, txom, value, value_len);
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_tx(conn, chan, txom);
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

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_tx_indicate(struct ble_hs_conn *conn,
                        struct ble_att_indicate_req *req,
                        void *value, uint16_t value_len)
{
#if !NIMBLE_OPT_ATT_CLT_INDICATE
    return BLE_HS_ENOTSUP;
#endif

    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->baiq_handle == 0) {
        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_init_req(conn, &chan, &txom,
                              BLE_ATT_INDICATE_REQ_BASE_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_indicate_req_write(txom->om_data, txom->om_len, req);
    assert(rc == 0);

    rc = ble_att_clt_append_blob(chan, txom, value, value_len);
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
int
ble_att_clt_rx_indicate(uint16_t conn_handle, struct os_mbuf **rxom)
{
#if !NIMBLE_OPT_ATT_CLT_INDICATE
    return BLE_HS_ENOTSUP;
#endif

    /* No payload. */
    ble_gattc_rx_indicate_rsp(conn_handle);
    return 0;
}
