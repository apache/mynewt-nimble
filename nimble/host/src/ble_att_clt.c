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
#include "host/ble_hs_uuid.h"
#include "ble_gatt_priv.h"
#include "ble_hs_priv.h"
#include "ble_hs_conn.h"
#include "ble_att_cmd.h"
#include "ble_att_priv.h"

static int
ble_att_clt_prep_req(struct ble_hs_conn *conn, struct ble_l2cap_chan **chan,
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

    *txom = ble_att_get_pkthdr();
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

int
ble_att_clt_rx_error(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                     struct os_mbuf **om)
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

    ble_gattc_rx_err(conn->bhc_handle, &rsp);

    return 0;
}

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

    rc = ble_att_clt_prep_req(conn, &chan, &txom, BLE_ATT_MTU_CMD_SZ);
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

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_att_clt_rx_mtu(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                   struct os_mbuf **om)
{
    struct ble_att_mtu_cmd rsp;
    int rc;

    *om = os_mbuf_pullup(*om, BLE_ATT_MTU_CMD_SZ);
    if (*om == NULL) {
        return BLE_HS_ENOMEM;
    }

    rc = ble_att_mtu_cmd_parse((*om)->om_data, (*om)->om_len, &rsp);
    assert(rc == 0);

    ble_att_set_peer_mtu(chan, rsp.bamc_mtu);

    ble_gattc_rx_mtu(conn, ble_l2cap_chan_mtu(chan));

    return 0;
}

int
ble_att_clt_tx_find_info(struct ble_hs_conn *conn,
                         struct ble_att_find_info_req *req)
{
    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->bafq_start_handle == 0 ||
        req->bafq_start_handle > req->bafq_end_handle) {

        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_prep_req(conn, &chan, &txom, BLE_ATT_FIND_INFO_REQ_SZ);
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

int
ble_att_clt_rx_find_info(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                         struct os_mbuf **om)
{
    struct ble_att_find_info_rsp rsp;
    struct os_mbuf *rxom;
    uint16_t handle_id;
    uint16_t uuid16;
    uint8_t uuid128[16];
    int off;
    int rc;

    *om = os_mbuf_pullup(*om, BLE_ATT_FIND_INFO_RSP_BASE_SZ);
    if (*om == NULL) {
        return BLE_HS_ENOMEM;
    }

    /* Double indirection is unwieldy; use rxom for remainder of function. */
    rxom = *om;

    rc = ble_att_find_info_rsp_parse(rxom->om_data, rxom->om_len, &rsp);
    assert(rc == 0);

    handle_id = 0;
    off = BLE_ATT_FIND_INFO_RSP_BASE_SZ;
    while (off < OS_MBUF_PKTLEN(rxom)) {
        rc = os_mbuf_copydata(rxom, off, 2, &handle_id);
        if (rc != 0) {
            return BLE_HS_EINVAL;
        }
        off += 2;
        handle_id = le16toh(&handle_id);

        switch (rsp.bafp_format) {
        case BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT:
            rc = os_mbuf_copydata(rxom, off, 2, &uuid16);
            if (rc != 0) {
                return BLE_HS_EINVAL;
            }
            off += 2;
            uuid16 = le16toh(&uuid16);

            rc = ble_hs_uuid_from_16bit(uuid16, uuid128);
            if (rc != 0) {
                return BLE_HS_EINVAL;
            }
            break;

        case BLE_ATT_FIND_INFO_RSP_FORMAT_128BIT:
            rc = os_mbuf_copydata(rxom, off, 16, &uuid128);
            if (rc != 0) {
                rc = BLE_HS_EINVAL;
            }
            off += 16;
            break;

        default:
            return BLE_HS_EINVAL;
        }

        /* XXX: Notify GATT of handle-uuid pair. */
    }

    return 0;
}

int
ble_att_clt_tx_read_type(struct ble_hs_conn *conn,
                         struct ble_att_read_type_req *req,
                         void *uuid128)
{
    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->batq_start_handle == 0 ||
        req->batq_start_handle > req->batq_end_handle) {

        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_prep_req(conn, &chan, &txom,
                              BLE_ATT_READ_TYPE_REQ_BASE_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_read_type_req_write(txom->om_data, txom->om_len, req);
    assert(rc == 0);

    rc = ble_hs_uuid_append(txom, uuid128);
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

static int
ble_att_clt_parse_type_attribute_data(struct os_mbuf **om, int data_len,
                                      struct ble_att_clt_adata *adata)
{
    if (data_len <= BLE_ATT_READ_TYPE_ADATA_BASE_SZ) {
        return BLE_HS_EMSGSIZE;
    }

    *om = os_mbuf_pullup(*om, data_len);
    if (*om == NULL) {
        return BLE_HS_ENOMEM;
    }

    adata->att_handle = le16toh((*om)->om_data + 0);
    adata->end_group_handle = 0;
    adata->value_len = data_len - BLE_ATT_READ_TYPE_ADATA_BASE_SZ;
    adata->value = (*om)->om_data + BLE_ATT_READ_TYPE_ADATA_BASE_SZ;

    return 0;
}

int
ble_att_clt_rx_read_type(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                         struct os_mbuf **rxom)
{
    struct ble_att_read_type_rsp rsp;
    struct ble_att_clt_adata adata;
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
        rc = ble_att_clt_parse_type_attribute_data(rxom, rsp.batp_length,
                                                   &adata);
        if (rc != 0) {
            goto done;
        }

        ble_gattc_rx_read_type_adata(conn, &adata);
        os_mbuf_adj(*rxom, rsp.batp_length);
    }

done:
    /* Notify GATT that the response is done being parsed. */
    ble_gattc_rx_read_type_complete(conn, rc);

    return 0;

}

int
ble_att_clt_tx_read(struct ble_hs_conn *conn, struct ble_att_read_req *req)
{
    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->barq_handle == 0) {
        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_prep_req(conn, &chan, &txom, BLE_ATT_READ_REQ_SZ);
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

int
ble_att_clt_rx_read(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                    struct os_mbuf **rxom)
{
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
    ble_gattc_rx_read_rsp(conn, rc, value, value_len);
    return rc;
}

int
ble_att_clt_tx_read_group_type(struct ble_hs_conn *conn,
                               struct ble_att_read_group_type_req *req,
                               void *uuid128)
{
    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->bagq_start_handle == 0 ||
        req->bagq_start_handle > req->bagq_end_handle) {

        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_prep_req(conn, &chan, &txom,
                              BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_read_group_type_req_write(txom->om_data, txom->om_len, req);
    assert(rc == 0);

    rc = ble_hs_uuid_append(txom, uuid128);
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

static int
ble_att_clt_parse_group_attribute_data(struct os_mbuf **om, int data_len,
                                       struct ble_att_clt_adata *adata)
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

int
ble_att_clt_rx_read_group_type(struct ble_hs_conn *conn,
                               struct ble_l2cap_chan *chan,
                               struct os_mbuf **rxom)
{
    struct ble_att_read_group_type_rsp rsp;
    struct ble_att_clt_adata adata;
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
        rc = ble_att_clt_parse_group_attribute_data(rxom, rsp.bagp_length,
                                                    &adata);
        if (rc != 0) {
            goto done;
        }

        ble_gattc_rx_read_group_type_adata(conn, &adata);
        os_mbuf_adj(*rxom, rsp.bagp_length);
    }

done:
    /* Notify GATT that the response is done being parsed. */
    ble_gattc_rx_read_group_type_complete(conn, rc);

    return 0;
}

int
ble_att_clt_tx_find_type_value(struct ble_hs_conn *conn,
                               struct ble_att_find_type_value_req *req,
                               void *attribute_value, int value_len)
{
    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->bavq_start_handle == 0 ||
        req->bavq_start_handle > req->bavq_end_handle) {

        rc = BLE_HS_EINVAL;
        goto err;
    }

    rc = ble_att_clt_prep_req(conn, &chan, &txom,
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
ble_att_clt_parse_handles_info(struct os_mbuf **om,
                               struct ble_att_clt_adata *adata)
{
    *om = os_mbuf_pullup(*om, BLE_ATT_FIND_TYPE_VALUE_HINFO_BASE_SZ);
    if (*om == NULL) {
        return BLE_HS_ENOMEM;
    }

    adata->att_handle = le16toh((*om)->om_data + 0);
    adata->end_group_handle = le16toh((*om)->om_data + 2);
    adata->value_len = 0;
    adata->value = NULL;

    return 0;
}

int
ble_att_clt_rx_find_type_value(struct ble_hs_conn *conn,
                               struct ble_l2cap_chan *chan,
                               struct os_mbuf **rxom)
{
    struct ble_att_clt_adata adata;
    int rc;

    /* Reponse consists of a one-byte opcode (already verified) and a variable
     * length Handles Information List field.  Strip the opcode from the
     * response.
     */
    os_mbuf_adj(*rxom, BLE_ATT_FIND_TYPE_VALUE_RSP_BASE_SZ);

    /* Parse the Handles Information List field, passing each entry to GATT. */
    rc = 0;
    while (OS_MBUF_PKTLEN(*rxom) > 0) {
        rc = ble_att_clt_parse_handles_info(rxom, &adata);
        if (rc != 0) {
            break;
        }

        ble_gattc_rx_find_type_value_hinfo(conn, &adata);
        os_mbuf_adj(*rxom, BLE_ATT_FIND_TYPE_VALUE_HINFO_BASE_SZ);
    }

    /* Notify GATT client that the full response has been parsed. */
    ble_gattc_rx_find_type_value_complete(conn, rc);

    return 0;
}

static int
ble_att_clt_tx_write_req_or_cmd(struct ble_hs_conn *conn,
                                struct ble_att_write_req *req,
                                void *value, uint16_t value_len,
                                int is_req)
{
    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    uint16_t mtu;
    int extra_len;
    int rc;

    txom = NULL;

    rc = ble_att_clt_prep_req(conn, &chan, &txom, BLE_ATT_WRITE_REQ_BASE_SZ);
    if (rc != 0) {
        goto err;
    }

    if (is_req) {
        rc = ble_att_write_req_write(txom->om_data, txom->om_len, req);
    } else {
        rc = ble_att_write_cmd_write(txom->om_data, txom->om_len, req);
    }
    assert(rc == 0);

    mtu = ble_l2cap_chan_mtu(chan);
    extra_len = BLE_ATT_WRITE_REQ_BASE_SZ + value_len - mtu;
    if (extra_len > 0) {
        value_len -= extra_len;
    }

    rc = os_mbuf_append(txom, value, value_len);
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

int
ble_att_clt_tx_write_req(struct ble_hs_conn *conn,
                         struct ble_att_write_req *req,
                         void *value, uint16_t value_len)
{
    int rc;

    rc = ble_att_clt_tx_write_req_or_cmd(conn, req, value, value_len, 1);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_att_clt_tx_write_cmd(struct ble_hs_conn *conn,
                         struct ble_att_write_req *req,
                         void *value, uint16_t value_len)
{
    int rc;

    rc = ble_att_clt_tx_write_req_or_cmd(conn, req, value, value_len, 0);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_att_clt_rx_write(struct ble_hs_conn *conn,
                     struct ble_l2cap_chan *chan,
                     struct os_mbuf **rxom)
{
    /* No payload. */
    ble_gattc_rx_write_rsp(conn);

    return 0;
}
