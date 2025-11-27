/*
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

#include "ble_ll_iso_priv.h"
#include <controller/ble_ll.h>
#include <controller/ble_ll_iso.h>
#include <controller/ble_ll_isoal.h>
#include <controller/ble_ll_tmr.h>
#include <nimble/hci_common.h>
#include <stdint.h>
#include <sys/errno.h>
#include <syscfg/syscfg.h>

#if MYNEWT_VAL(BLE_LL_ISO)

#define HCI_ISO_PKTHDR_OVERHEAD (sizeof(struct os_mbuf_pkthdr))
#define HCI_ISO_BUF_OVERHEAD    (sizeof(struct os_mbuf) + HCI_ISO_PKTHDR_OVERHEAD)

/* Core 5.4 | Vol 4, Part E, 4.1.1
 * The ISO_Data_Packet_Length parameter of this command specifies
 * the maximum buffer size for each HCI ISO Data packet (excluding the header
 * but including optional fields such as ISO_SDU_Length).
 */
#define HCI_ISO_DATA_PKT_LEN MYNEWT_VAL(BLE_TRANSPORT_ISO_SIZE)
#define HCI_ISO_DATA_BUF_SIZE                                                 \
    OS_ALIGN(HCI_ISO_DATA_PKT_LEN + sizeof(struct ble_hci_iso), 4)

#define HCI_ISO_TIMESTAMP_LEN 4
#define HCI_ISO_DATA_PKTHDR_LEN                                               \
    (sizeof(struct ble_hci_iso) + HCI_ISO_TIMESTAMP_LEN +                     \
     sizeof(struct ble_hci_iso_data))

#define HCI_ISO_BUF_COUNT MYNEWT_VAL(BLE_TRANSPORT_ISO_FROM_HS_COUNT)
#define HCI_ISO_BUF_SIZE  (HCI_ISO_DATA_BUF_SIZE + HCI_ISO_BUF_OVERHEAD)
static os_membuf_t hci_iso_data_pkt_membuf[OS_MEMPOOL_SIZE(HCI_ISO_BUF_COUNT, HCI_ISO_BUF_SIZE)];
static struct os_mbuf_pool hci_iso_data_pkt_mbuf_pool;
static struct os_mempool hci_iso_data_pkt_mempool;

STAILQ_HEAD(ble_ll_iso_conn_q, ble_ll_iso_conn);
struct ble_ll_iso_conn_q ll_iso_conn_q;

static void
hci_iso_sdu_send(uint16_t conn_handle, const struct os_mbuf *sdu,
                 uint32_t timestamp, uint16_t seq_num, bool valid)
{
    struct os_mbuf *om;
    struct ble_hci_iso_data *hci_iso_data;
    struct ble_hci_iso *hci_iso;
    uint32_t *ts;
    uint16_t iso_sdu_len;
    uint16_t iso_sdu_frag_len;
    uint16_t iso_sdu_offset;
    uint8_t pkt_status_flag;
    uint8_t pb_flag;
    int rc;

    om = os_mbuf_get_pkthdr(&hci_iso_data_pkt_mbuf_pool, 0);
    BLE_LL_ASSERT(om);

    /* Prepare room for HCI ISO SDU packet header */
    hci_iso = os_mbuf_extend(om, sizeof(*hci_iso));
    BLE_LL_ASSERT(hci_iso);

    ts = os_mbuf_extend(om, sizeof(*ts));
    BLE_LL_ASSERT(ts);
    put_le32(ts, timestamp);

    /* Prepare room for ISO SDU header */
    hci_iso_data = os_mbuf_extend(om, sizeof(*hci_iso_data));
    BLE_LL_ASSERT(hci_iso_data);

    hci_iso_data->packet_seq_num = htole16(seq_num);
    if (sdu == NULL) {
        iso_sdu_len = 0;
        pkt_status_flag = BLE_HCI_ISO_PKT_STATUS_LOST;
    } else {
        iso_sdu_len = os_mbuf_len(sdu);
        pkt_status_flag = valid ? BLE_HCI_ISO_PKT_STATUS_VALID
                                : BLE_HCI_ISO_PKT_STATUS_INVALID;
    }
    hci_iso_data->sdu_len =
        htole16(BLE_HCI_ISO_SDU_LENGTH_DEFINE(iso_sdu_len, pkt_status_flag));

    iso_sdu_frag_len = min(OS_MBUF_TRAILINGSPACE(om), iso_sdu_len);
    if (iso_sdu_frag_len > 0) {
        os_mbuf_appendfrom(om, sdu, 0, iso_sdu_frag_len);
    }
    iso_sdu_offset = iso_sdu_frag_len;

    pb_flag = iso_sdu_frag_len == iso_sdu_len ? BLE_HCI_ISO_PB_COMPLETE
                                              : BLE_HCI_ISO_PB_FIRST;
    hci_iso->handle = htole16(BLE_HCI_ISO_HANDLE(conn_handle, pb_flag, true));
    hci_iso->length =
        htole16(HCI_ISO_TIMESTAMP_LEN + sizeof(*hci_iso_data) + iso_sdu_frag_len);

    rc = ble_transport_to_hs_iso(om);
    if (rc != 0) {
        os_mbuf_free_chain(om);
        BLE_LL_ASSERT(0);
    }

    while (iso_sdu_offset < iso_sdu_len) {
        om = os_mbuf_get_pkthdr(&hci_iso_data_pkt_mbuf_pool, 0);
        BLE_LL_ASSERT(om);

        /* Prepare room for HCI ISO SDU packet header */
        hci_iso = os_mbuf_extend(om, sizeof(*hci_iso));
        BLE_LL_ASSERT(hci_iso);

        iso_sdu_frag_len =
            min(OS_MBUF_TRAILINGSPACE(om), iso_sdu_len - iso_sdu_offset);
        if (iso_sdu_frag_len > 0) {
            os_mbuf_appendfrom(om, sdu, iso_sdu_offset, iso_sdu_frag_len);
        }
        iso_sdu_offset += iso_sdu_frag_len;

        pb_flag = iso_sdu_offset < iso_sdu_len ? BLE_HCI_ISO_PB_CONTINUATION
                                               : BLE_HCI_ISO_PB_LAST;
        hci_iso->handle = htole16(BLE_HCI_ISO_HANDLE(conn_handle, pb_flag, false));
        hci_iso->length = htole16(iso_sdu_frag_len);

        rc = ble_transport_to_hs_iso(om);
        if (rc != 0) {
            os_mbuf_free_chain(om);
            BLE_LL_ASSERT(0);
        }
    }
}

static struct ble_ll_iso_data_path_cb hci_iso_data_path_cb = {
    .sdu_out = hci_iso_sdu_send,
};

static void
iso_test_sdu_send(uint16_t conn_handle, const struct os_mbuf *sdu,
                  uint32_t timestamp, uint16_t seq_num, bool valid)
{
    struct ble_ll_iso_conn *conn;
    struct ble_ll_iso_rx *rx;

    conn = ble_ll_iso_conn_find_by_handle(conn_handle);
    if (conn == NULL) {
        return;
    }

    rx = conn->rx;
    BLE_LL_ASSERT(rx);

    if (sdu == NULL) {
        rx->test.missed_sdu_count++;
        return;
    }

    if (!valid) {
        rx->test.failed_sdu_count++;
        return;
    }

    /* Zero size SDU expected */
    if (rx->test.payload_type == 0b00 && os_mbuf_len(sdu) != 0) {
        rx->test.failed_sdu_count++;
        return;
    }

    /* Max size SDU expected */
    if (rx->test.payload_type == 0b10 && os_mbuf_len(sdu) != rx->params->max_sdu) {
        rx->test.failed_sdu_count++;
        return;
    }

    rx->test.received_sdu_count++;
}

static struct ble_ll_iso_data_path_cb test_iso_data_path_cb = {
    .sdu_out = iso_test_sdu_send,
};

static const struct ble_ll_iso_data_path_cb *
ble_ll_iso_data_path_get(uint8_t data_path_id)
{
    if (data_path_id == BLE_HCI_ISO_DATA_PATH_ID_HCI) {
        return &hci_iso_data_path_cb;
    }

    /* We do not (yet) support any vendor-specific data path */
    return NULL;
}

static void
iso_sdu_cb(struct ble_ll_isoal_demux *demux, const struct os_mbuf *om,
           uint32_t timestamp, uint16_t seq_num, bool valid)
{
    struct ble_ll_iso_rx *rx;

    rx = CONTAINER_OF(demux, struct ble_ll_iso_rx, demux);

    if (rx->data_path != NULL && rx->data_path->sdu_out != NULL) {
        rx->data_path->sdu_out(rx->conn->handle, om, timestamp, seq_num, valid);
    }
}

static const struct ble_ll_isoal_demux_cb isoal_demux_cb = {
    .sdu_cb = iso_sdu_cb,
};

int
ble_ll_iso_setup_iso_data_path(const uint8_t *cmdbuf, uint8_t cmdlen,
                               uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_setup_iso_data_path_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_setup_iso_data_path_rp *rsp = (void *)rspbuf;
    const struct ble_ll_iso_data_path_cb *data_path;
    struct ble_ll_iso_conn *conn;
    struct ble_ll_iso_rx *rx;
    struct ble_ll_iso_tx *tx;
    uint16_t conn_handle;

    if (cmdlen < sizeof(*cmd) || cmdlen != sizeof(*cmd) + cmd->codec_config_len) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (cmd->data_path_dir != BLE_HCI_ISO_DATA_PATH_DIR_INPUT &&
        cmd->data_path_dir != BLE_HCI_ISO_DATA_PATH_DIR_OUTPUT) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    conn_handle = le16toh(cmd->conn_handle);

    conn = ble_ll_iso_conn_find_by_handle(conn_handle);
    if (!conn) {
        return BLE_ERR_UNK_CONN_ID;
    }

    data_path = ble_ll_iso_data_path_get(cmd->data_path_id);
    if (data_path == NULL) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    if (cmd->data_path_dir == BLE_HCI_ISO_DATA_PATH_DIR_INPUT) {
        /* Input (Host to Controller) */
        tx = conn->tx;
        if (tx == NULL || tx->data_path != NULL) {
            return BLE_ERR_CMD_DISALLOWED;
        }
        if (tx->params->bn == 0) {
            return BLE_ERR_UNSUPPORTED;
        }

        tx->data_path = data_path;
    } else {
        /* Output (Controller to Host) */
        rx = conn->rx;
        if (rx == NULL || rx->data_path != NULL) {
            return BLE_ERR_CMD_DISALLOWED;
        }
        if (rx->params->bn == 0) {
            return BLE_ERR_UNSUPPORTED;
        }

        rx->data_path = data_path;
        ble_ll_isoal_demux_cb_set(&rx->demux, &isoal_demux_cb);
    }

    rsp->conn_handle = cmd->conn_handle;

    *rsplen = sizeof(*rsp);

    return BLE_ERR_SUCCESS;
}

int
ble_ll_iso_remove_iso_data_path(const uint8_t *cmdbuf, uint8_t cmdlen,
                                uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_remove_iso_data_path_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_remove_iso_data_path_rp *rsp = (void *)rspbuf;
    struct ble_ll_iso_conn *conn;
    struct ble_ll_iso_rx *rx;
    struct ble_ll_iso_tx *tx;
    uint16_t conn_handle;
    bool remove_rx;
    bool remove_tx;
    int rc;

    if (cmdlen != sizeof(*cmd)) {
        rc = BLE_ERR_INV_HCI_CMD_PARMS;
        goto done;
    }

    conn_handle = le16toh(cmd->conn_handle);

    conn = ble_ll_iso_conn_find_by_handle(conn_handle);
    if (!conn) {
        rc = BLE_ERR_UNK_CONN_ID;
        goto done;
    }

    /* Input (Host to Controller) */
    tx = conn->tx;
    remove_tx = cmd->data_path_dir & (1 << BLE_HCI_ISO_DATA_PATH_DIR_INPUT);
    if (remove_tx && (tx == NULL || tx->data_path == NULL)) {
        rc = BLE_ERR_CMD_DISALLOWED;
        goto done;
    }

    /* Output (Controller to Host) */
    rx = conn->rx;
    remove_rx = cmd->data_path_dir & (1 << BLE_HCI_ISO_DATA_PATH_DIR_OUTPUT);
    if (remove_rx && (rx == NULL || rx->data_path == NULL)) {
        rc = BLE_ERR_CMD_DISALLOWED;
        goto done;
    }

    if (remove_tx) {
        BLE_LL_ASSERT(tx != NULL);
        tx->data_path = NULL;
    }

    if (remove_rx) {
        BLE_LL_ASSERT(rx != NULL);
        ble_ll_isoal_demux_cb_set(&rx->demux, NULL);
        rx->data_path = NULL;
    }

    rc = BLE_ERR_SUCCESS;
done:
    rsp->conn_handle = cmd->conn_handle;

    *rsplen = sizeof(*rsp);

    return rc;
}

int
ble_ll_iso_transmit_test(const uint8_t *cmdbuf, uint8_t cmdlen,
                         uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_iso_receive_test_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_iso_receive_test_rp *rsp = (void *)rspbuf;
    struct ble_ll_iso_conn *conn;
    struct ble_ll_iso_tx *tx;
    uint16_t handle;

    if (cmdlen != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    handle = le16toh(cmd->conn_handle);

    conn = ble_ll_iso_conn_find_by_handle(handle);
    if (conn == NULL) {
        return BLE_ERR_UNK_CONN_ID;
    }

    tx = conn->tx;
    if (tx == NULL || tx->params->bn == 0) {
        return BLE_ERR_UNSUPPORTED;
    }

    if (tx->data_path != NULL) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    if (cmd->payload_type > BLE_HCI_PAYLOAD_TYPE_MAXIMUM_LENGTH) {
        return BLE_ERR_INV_LMP_LL_PARM;
    }

    tx->test.payload_type = cmd->payload_type;
    tx->data_path = &test_iso_data_path_cb;

    rsp->conn_handle = cmd->conn_handle;

    *rsplen = sizeof(*rsp);

    return BLE_ERR_SUCCESS;
}

int
ble_ll_iso_receive_test(const uint8_t *cmdbuf, uint8_t cmdlen, uint8_t *rspbuf,
                        uint8_t *rsplen)
{
    const struct ble_hci_le_iso_receive_test_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_iso_transmit_test_rp *rsp = (void *)rspbuf;
    struct ble_ll_iso_conn *conn;
    struct ble_ll_iso_rx *rx;
    uint16_t handle;

    if (cmdlen != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    handle = le16toh(cmd->conn_handle);

    conn = ble_ll_iso_conn_find_by_handle(handle);
    if (conn == NULL) {
        return BLE_ERR_UNK_CONN_ID;
    }

    rx = conn->rx;
    if (rx == NULL || rx->params->bn == 0) {
        return BLE_ERR_UNSUPPORTED;
    }

    if (rx->data_path != NULL) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    rx->test.payload_type = cmd->payload_type;
    rx->test.received_sdu_count = 0;
    rx->test.missed_sdu_count = 0;
    rx->test.failed_sdu_count = 0;
    rx->data_path = &test_iso_data_path_cb;

    rsp->conn_handle = cmd->conn_handle;

    *rsplen = sizeof(*rsp);

    return BLE_ERR_SUCCESS;
}

int
ble_ll_iso_read_counters_test(const uint8_t *cmdbuf, uint8_t cmdlen,
                              uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_iso_read_test_counters_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_iso_read_test_counters_rp *rsp = (void *)rspbuf;
    struct ble_ll_iso_conn *conn;
    struct ble_ll_iso_rx *rx;
    uint16_t handle;

    if (cmdlen != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    handle = le16toh(cmd->conn_handle);

    conn = ble_ll_iso_conn_find_by_handle(handle);
    if (conn == NULL) {
        return BLE_ERR_UNK_CONN_ID;
    }

    rx = conn->rx;
    if (rx == NULL || rx->data_path != &test_iso_data_path_cb) {
        return BLE_ERR_UNSUPPORTED;
    }

    rsp->conn_handle = cmd->conn_handle;
    rsp->received_sdu_count = rx->test.received_sdu_count;
    rsp->missed_sdu_count = rx->test.missed_sdu_count;
    rsp->failed_sdu_count = rx->test.failed_sdu_count;

    *rsplen = sizeof(*rsp);

    return BLE_ERR_SUCCESS;
}

int
ble_ll_iso_end_test(const uint8_t *cmdbuf, uint8_t cmdlen, uint8_t *rspbuf,
                    uint8_t *rsplen)
{
    const struct ble_hci_le_iso_test_end_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_iso_test_end_rp *rsp = (void *)rspbuf;
    struct ble_ll_iso_conn *conn;
    struct ble_ll_iso_rx *rx;
    struct ble_ll_iso_tx *tx;
    uint16_t handle;
    int rc;

    if (cmdlen != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    handle = le16toh(cmd->conn_handle);

    conn = ble_ll_iso_conn_find_by_handle(handle);
    if (conn == NULL) {
        return BLE_ERR_UNK_CONN_ID;
    }

    rc = BLE_ERR_UNSUPPORTED;

    memset(rsp, 0, sizeof(*rsp));
    rsp->conn_handle = htole16(handle);

    rx = conn->rx;
    if (rx != NULL && rx->data_path == &test_iso_data_path_cb) {
        rx->data_path = NULL;
        rsp->received_sdu_count = htole32(rx->test.received_sdu_count);
        rsp->missed_sdu_count = htole32(rx->test.missed_sdu_count);
        rsp->failed_sdu_count = htole32(rx->test.failed_sdu_count);
        rc = BLE_ERR_SUCCESS;
    }

    tx = conn->tx;
    if (tx != NULL && tx->data_path == &test_iso_data_path_cb) {
        tx->data_path = NULL;
        rc = BLE_ERR_SUCCESS;
    }

    *rsplen = sizeof(*rsp);

    return rc;
}

int
ble_ll_iso_read_tx_sync(const uint8_t *cmdbuf, uint8_t cmdlen, uint8_t *rspbuf,
                        uint8_t *rsplen)
{
    const struct ble_hci_le_read_iso_tx_sync_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_read_iso_tx_sync_rp *rsp = (void *)rspbuf;
    struct ble_ll_iso_conn *conn;
    struct ble_ll_iso_tx *tx;
    uint16_t handle;

    handle = le16toh(cmd->conn_handle);

    conn = ble_ll_iso_conn_find_by_handle(handle);
    if (conn == NULL) {
        return BLE_ERR_UNK_CONN_ID;
    }

    tx = conn->tx;
    if (tx == NULL) {
        return BLE_ERR_UNSUPPORTED;
    }

    rsp->conn_handle = cmd->conn_handle;
    rsp->packet_seq_num = htole16(tx->mux.last_tx_packet_seq_num);
    rsp->tx_timestamp = htole32(tx->mux.last_tx_timestamp);
    put_le24(rsp->time_offset, 0);

    *rsplen = sizeof(*rsp);

    return BLE_ERR_SUCCESS;
}

struct ble_ll_iso_conn *
ble_ll_iso_conn_find_by_handle(uint16_t conn_handle)
{
    struct ble_ll_iso_conn *conn;

    STAILQ_FOREACH(conn, &ll_iso_conn_q, iso_conn_q_next) {
        if (conn_handle == conn->handle) {
            return conn;
        }
    }

    return NULL;
}

void
ble_ll_iso_init(void)
{
    int rc;

    rc = os_mempool_init(&hci_iso_data_pkt_mempool, HCI_ISO_BUF_COUNT, HCI_ISO_BUF_SIZE,
                         &hci_iso_data_pkt_membuf[0], "iso_data_pkt_mempool");
    BLE_LL_ASSERT(rc == 0);

    rc = os_mbuf_pool_init(&hci_iso_data_pkt_mbuf_pool, &hci_iso_data_pkt_mempool,
                           HCI_ISO_BUF_SIZE, HCI_ISO_BUF_COUNT);
    BLE_LL_ASSERT(rc == 0);

    STAILQ_INIT(&ll_iso_conn_q);
}

void
ble_ll_iso_reset(void)
{}

int
ble_ll_hci_iso_data_in(struct os_mbuf *om)
{
    struct ble_hci_iso *hci_iso;
    struct ble_hci_iso_data *hci_iso_data;
    struct ble_ll_iso_conn *conn;
    struct ble_ll_iso_tx *tx;
    struct ble_mbuf_hdr *blehdr;
    uint16_t data_hdr_len;
    uint16_t handle;
    uint16_t conn_handle;
    uint16_t length;
    uint16_t pb_flag;
    uint16_t ts_flag;
    uint32_t timestamp = 0;

    hci_iso = (void *)om->om_data;

    handle = le16toh(hci_iso->handle);
    conn_handle = BLE_HCI_ISO_CONN_HANDLE(handle);
    pb_flag = BLE_HCI_ISO_PB_FLAG(handle);
    ts_flag = BLE_HCI_ISO_TS_FLAG(handle);
    length = BLE_HCI_ISO_LENGTH(le16toh(hci_iso->length));

    conn = ble_ll_iso_conn_find_by_handle(conn_handle);
    if (!conn) {
        os_mbuf_free_chain(om);
        return BLE_ERR_UNK_CONN_ID;
    }

    tx = conn->tx;
    if (tx == NULL) {
        os_mbuf_free_chain(om);
        return BLE_ERR_UNSUPPORTED;
    }

    data_hdr_len = 0;
    if ((pb_flag == BLE_HCI_ISO_PB_FIRST) ||
        (pb_flag == BLE_HCI_ISO_PB_COMPLETE)) {
        blehdr = BLE_MBUF_HDR_PTR(om);
        blehdr->txiso.packet_seq_num = ++tx->mux.sdu_counter;
        blehdr->txiso.cpu_timestamp = ble_ll_tmr_get();

        if (ts_flag) {
            timestamp = get_le32(om->om_data + sizeof(*hci_iso));
            data_hdr_len += sizeof(uint32_t);
        }
        blehdr->txiso.hci_timestamp = timestamp;

        hci_iso_data = (void *)(om->om_data + sizeof(*hci_iso) + data_hdr_len);
        data_hdr_len += sizeof(*hci_iso_data);
    }
    os_mbuf_adj(om, sizeof(*hci_iso) + data_hdr_len);

    if (OS_MBUF_PKTLEN(om) != length - data_hdr_len) {
        os_mbuf_free_chain(om);
        return BLE_ERR_MEM_CAPACITY;
    }

    switch (pb_flag) {
    case BLE_HCI_ISO_PB_FIRST:
        BLE_LL_ASSERT(!conn->frag);
        conn->frag = om;
        om = NULL;
        break;
    case BLE_HCI_ISO_PB_CONTINUATION:
        BLE_LL_ASSERT(conn->frag);
        os_mbuf_concat(conn->frag, om);
        om = NULL;
        break;
    case BLE_HCI_ISO_PB_COMPLETE:
        BLE_LL_ASSERT(!conn->frag);
        break;
    case BLE_HCI_ISO_PB_LAST:
        BLE_LL_ASSERT(conn->frag);
        os_mbuf_concat(conn->frag, om);
        om = conn->frag;
        conn->frag = NULL;
        break;
    default:
        BLE_LL_ASSERT(0);
        break;
    }

    if (om) {
        ble_ll_isoal_mux_sdu_put(&tx->mux, om);
    }

    return 0;
}

static int
ble_ll_iso_test_pdu_get(struct ble_ll_isoal_mux *mux, uint8_t idx,
                        uint32_t pkt_counter, uint8_t *llid, uint8_t *dptr,
                        uint8_t payload_type, uint32_t rand)
{
    struct ble_ll_iso_tx *tx;
    uint32_t payload_len;
    uint16_t rem_len;
    uint8_t sdu_idx;
    uint8_t pdu_idx;
    int pdu_len;

    tx = CONTAINER_OF(mux, struct ble_ll_iso_tx, mux);
    BLE_LL_ASSERT(!tx->params->framed);

    sdu_idx = idx / mux->pdu_per_sdu;
    pdu_idx = idx - sdu_idx * mux->pdu_per_sdu;

    switch (payload_type) {
    case BLE_HCI_PAYLOAD_TYPE_ZERO_LENGTH:
        *llid = 0b00;
        pdu_len = 0;
        break;
    case BLE_HCI_PAYLOAD_TYPE_VARIABLE_LENGTH:
        payload_len = max(rand + (sdu_idx * pdu_idx), 4);

        rem_len = payload_len - pdu_idx * tx->params->max_pdu;
        if (rem_len == 0) {
            *llid = 0b01;
            pdu_len = 0;
        } else {
            *llid = rem_len > tx->params->max_pdu;
            pdu_len = min(tx->params->max_pdu, rem_len);
        }

        memset(dptr, 0, pdu_len);

        if (payload_len == rem_len) {
            put_le32(dptr, pkt_counter);
        }

        break;
    case BLE_HCI_PAYLOAD_TYPE_MAXIMUM_LENGTH:
        payload_len = tx->params->max_sdu;

        rem_len = payload_len - pdu_idx * tx->params->max_pdu;
        if (rem_len == 0) {
            *llid = 0b01;
            pdu_len = 0;
        } else {
            *llid = rem_len > tx->params->max_pdu;
            pdu_len = min(tx->params->max_pdu, rem_len);
        }

        memset(dptr, 0, pdu_len);

        if (payload_len == rem_len) {
            put_le32(dptr, pkt_counter);
        }

        break;
    default:
        BLE_LL_ASSERT(0);
    }

    return pdu_len;
}

int
ble_ll_iso_tx_pdu_get(struct ble_ll_iso_tx *tx, uint8_t idx,
                      uint32_t pkt_counter, uint8_t *llid, void *dptr)
{
    if (tx->data_path == &test_iso_data_path_cb) {
        return ble_ll_iso_test_pdu_get(&tx->mux, idx, pkt_counter, llid, dptr,
                                       tx->test.payload_type, tx->test.rand);
    }

    return ble_ll_isoal_mux_pdu_get(&tx->mux, idx, llid, dptr);
}

static void
ble_ll_iso_conn_register(struct ble_ll_iso_conn *conn)
{
    os_sr_t sr;

    OS_ENTER_CRITICAL(sr);
    STAILQ_INSERT_TAIL(&ll_iso_conn_q, conn, iso_conn_q_next);
    OS_EXIT_CRITICAL(sr);
}

static void
ble_ll_iso_conn_unregister(struct ble_ll_iso_conn *conn)
{
    os_sr_t sr;

    OS_ENTER_CRITICAL(sr);
    STAILQ_REMOVE(&ll_iso_conn_q, conn, ble_ll_iso_conn, iso_conn_q_next);
    OS_EXIT_CRITICAL(sr);
}

void
ble_ll_iso_conn_init(struct ble_ll_iso_conn *conn, uint16_t conn_handle,
                     struct ble_ll_iso_rx *rx, struct ble_ll_iso_tx *tx)
{
    memset(conn, 0, sizeof(*conn));
    conn->handle = conn_handle;
    conn->rx = rx;
    conn->tx = tx;

    ble_ll_iso_conn_register(conn);
}

void
ble_ll_iso_conn_reset(struct ble_ll_iso_conn *conn)
{
    ble_ll_iso_conn_unregister(conn);

    if (conn->rx != NULL) {
        ble_ll_iso_rx_reset(conn->rx);
    }

    if (conn->tx != NULL) {
        ble_ll_iso_tx_reset(conn->tx);
    }

    if (conn->frag != NULL) {
        os_mbuf_free_chain(conn->frag);
        conn->frag = NULL;
    }
}

static void
ble_ll_iso_params_to_isoal_config(const struct ble_ll_iso_params *params,
                                  struct ble_ll_isoal_config *config)
{
    memset(config, 0, sizeof(*config));
    config->iso_interval_us = params->iso_interval * 1250;
    config->sdu_interval_us = params->sdu_interval;
    config->max_sdu = params->max_sdu;
    config->max_pdu = params->max_pdu;
    config->bn = params->bn;
    config->pte = params->pte;
    config->framed = params->framed;
    config->unsegmented = params->framing_mode;
}

void
ble_ll_iso_tx_init(struct ble_ll_iso_tx *tx, struct ble_ll_iso_conn *conn,
                   const struct ble_ll_iso_params *params)
{
    struct ble_ll_isoal_config config;

    memset(tx, 0, sizeof(*tx));
    tx->conn = conn;
    tx->params = params;

    ble_ll_iso_params_to_isoal_config(params, &config);

    ble_ll_isoal_mux_init(&tx->mux, &config);
}

void
ble_ll_iso_tx_reset(struct ble_ll_iso_tx *tx)
{
    ble_ll_isoal_mux_reset(&tx->mux);
}

int
ble_ll_iso_tx_event_start(struct ble_ll_iso_tx *tx, uint32_t timestamp)
{
    ble_ll_isoal_mux_event_start(&tx->mux, timestamp);

    return 0;
}

int
ble_ll_iso_tx_event_done(struct ble_ll_iso_tx *tx)
{
    tx->conn->num_completed_pkt += ble_ll_isoal_mux_event_done(&tx->mux);

    return tx->conn->num_completed_pkt;
}

void
ble_ll_iso_rx_init(struct ble_ll_iso_rx *rx, struct ble_ll_iso_conn *conn,
                   const struct ble_ll_iso_params *params)
{
    struct ble_ll_isoal_config config;

    memset(rx, 0, sizeof(*rx));
    rx->conn = conn;
    rx->params = params;

    ble_ll_iso_params_to_isoal_config(params, &config);

    ble_ll_isoal_demux_init(&rx->demux, &config);
}

void
ble_ll_iso_rx_reset(struct ble_ll_iso_rx *rx)
{
    ble_ll_isoal_demux_reset(&rx->demux);
}

int
ble_ll_iso_rx_event_start(struct ble_ll_iso_rx *rx, uint32_t timestamp)
{
    ble_ll_isoal_demux_event_start(&rx->demux, timestamp);

    return 0;
}

int
ble_ll_iso_rx_event_done(struct ble_ll_iso_rx *rx)
{
    ble_ll_isoal_demux_event_done(&rx->demux);

    return 0;
}

int
ble_ll_iso_rx_pdu_put(struct ble_ll_iso_rx *rx, uint8_t idx, struct os_mbuf *om)
{
    ble_ll_isoal_demux_pdu_put(&rx->demux, idx, om);

    return 0;
}

#endif /* BLE_LL_ISO */
