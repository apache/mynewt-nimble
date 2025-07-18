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

#include <controller/ble_ll.h>
#include <controller/ble_ll_iso.h>
#include <controller/ble_ll_isoal.h>
#include <controller/ble_ll_tmr.h>
#include <nimble/hci_common.h>
#include <stdint.h>
#include <sys/errno.h>
#include <syscfg/syscfg.h>

#if MYNEWT_VAL(BLE_LL_ISO)

#define HCI_ISO_PKTHDR_OVERHEAD     (sizeof(struct os_mbuf_pkthdr))
#define HCI_ISO_BUF_OVERHEAD        (sizeof(struct os_mbuf) + HCI_ISO_PKTHDR_OVERHEAD)

/* Core 5.4 | Vol 4, Part E, 4.1.1
 * The ISO_Data_Packet_Length parameter of this command specifies
 * the maximum buffer size for each HCI ISO Data packet (excluding the header
 * but including optional fields such as ISO_SDU_Length).
 */
#define HCI_ISO_DATA_PKT_LEN        MYNEWT_VAL(BLE_TRANSPORT_ISO_SIZE)
#define HCI_ISO_DATA_BUF_SIZE       OS_ALIGN(HCI_ISO_DATA_PKT_LEN + \
                                             sizeof(struct ble_hci_iso), 4)

#define HCI_ISO_TIMESTAMP_LEN       4
#define HCI_ISO_DATA_PKTHDR_LEN     (sizeof(struct ble_hci_iso) + \
                                     HCI_ISO_TIMESTAMP_LEN + \
                                     sizeof(struct ble_hci_iso_data))

#define HCI_ISO_BUF_COUNT           MYNEWT_VAL(BLE_TRANSPORT_ISO_FROM_HS_COUNT)
#define HCI_ISO_BUF_SIZE            (HCI_ISO_DATA_BUF_SIZE + HCI_ISO_BUF_OVERHEAD)
static os_membuf_t hci_iso_data_pkt_membuf[OS_MEMPOOL_SIZE(HCI_ISO_BUF_COUNT,
                                                           HCI_ISO_BUF_SIZE)];
static struct os_mbuf_pool hci_iso_data_pkt_mbuf_pool;
static struct os_mempool hci_iso_data_pkt_mempool;

STAILQ_HEAD(ble_ll_iso_conn_q, ble_ll_iso_conn);
struct ble_ll_iso_conn_q ll_iso_conn_q;

static void
hci_iso_sdu_send(uint16_t conn_handle, const struct os_mbuf *sdu, uint32_t timestamp,
                 uint16_t seq_num, bool valid)
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
        pkt_status_flag = valid ? BLE_HCI_ISO_PKT_STATUS_VALID : BLE_HCI_ISO_PKT_STATUS_INVALID;
    }
    hci_iso_data->sdu_len = htole16(BLE_HCI_ISO_SDU_LENGTH_DEFINE(iso_sdu_len, pkt_status_flag));

    iso_sdu_frag_len = min(OS_MBUF_TRAILINGSPACE(om), iso_sdu_len);
    if (iso_sdu_frag_len > 0) {
        os_mbuf_appendfrom(om, sdu, 0, iso_sdu_frag_len);
    }
    iso_sdu_offset = iso_sdu_frag_len;

    pb_flag = iso_sdu_frag_len == iso_sdu_len ? BLE_HCI_ISO_PB_COMPLETE : BLE_HCI_ISO_PB_FIRST;
    hci_iso->handle = htole16(BLE_HCI_ISO_HANDLE(conn_handle, pb_flag, true));
    hci_iso->length = htole16(HCI_ISO_TIMESTAMP_LEN + sizeof(*hci_iso_data) + iso_sdu_frag_len);

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

        iso_sdu_frag_len = min(OS_MBUF_TRAILINGSPACE(om), iso_sdu_len - iso_sdu_offset);
        if (iso_sdu_frag_len > 0) {
            os_mbuf_appendfrom(om, sdu, iso_sdu_offset, iso_sdu_frag_len);
        }
        iso_sdu_offset += iso_sdu_frag_len;

        pb_flag = iso_sdu_offset < iso_sdu_len ? BLE_HCI_ISO_PB_CONTINUATION : BLE_HCI_ISO_PB_LAST;
        hci_iso->handle = htole16(BLE_HCI_ISO_HANDLE(conn_handle, pb_flag, false));
        hci_iso->length = htole16(iso_sdu_frag_len);

        rc = ble_transport_to_hs_iso(om);
        if (rc != 0) {
            os_mbuf_free_chain(om);
            BLE_LL_ASSERT(0);
        }
    }
}

static struct ble_ll_iso_data_path_cb hci_iso_data_path_if = {
    .sdu_send = hci_iso_sdu_send,
};

static void
iso_test_sdu_send(uint16_t conn_handle, const struct os_mbuf *sdu, uint32_t timestamp,
                  uint16_t seq_num, bool valid)
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
    if (rx->test.payload_type == 0b10 && os_mbuf_len(sdu) != conn->mux.max_sdu) {
        rx->test.failed_sdu_count++;
        return;
    }

    rx->test.received_sdu_count++;
}

static struct ble_ll_iso_data_path_cb test_iso_data_path_if = {
    .sdu_send = iso_test_sdu_send,
};

static const struct ble_ll_iso_data_path_cb *
ble_ll_iso_data_path_get(uint8_t data_path_id)
{
    if (data_path_id == BLE_HCI_ISO_DATA_PATH_ID_HCI) {
        return &hci_iso_data_path_if;
    }

    /* We do not (yet) support any vendor-specific data path */
    return NULL;
}

int
ble_ll_iso_setup_iso_data_path(const uint8_t *cmdbuf, uint8_t cmdlen,
                               uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_setup_iso_data_path_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_setup_iso_data_path_rp *rsp = (void *)rspbuf;
    const struct ble_ll_iso_data_path_cb *data_path;
    struct ble_ll_iso_conn *conn;
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

    if (conn->mux.bn == 0) {
        return BLE_ERR_UNSUPPORTED;
    }

    data_path = ble_ll_iso_data_path_get(cmd->data_path_id);
    if (data_path == NULL) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    if (cmd->data_path_dir == BLE_HCI_ISO_DATA_PATH_DIR_INPUT) {
        /* Input (Host to Controller) */
        if (conn->tx == NULL || conn->tx->data_path != NULL) {
            return BLE_ERR_CMD_DISALLOWED;
        }
        conn->tx->data_path = data_path;
    } else {
        /* Output (Controller to Host) */
        if (conn->rx == NULL || conn->rx->data_path != NULL) {
            return BLE_ERR_CMD_DISALLOWED;
        }
        conn->rx->data_path = data_path;
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
    remove_tx = cmd->data_path_dir & (1 << BLE_HCI_ISO_DATA_PATH_DIR_INPUT);
    if (remove_tx && (conn->tx == NULL || conn->tx->data_path == NULL)) {
        rc = BLE_ERR_CMD_DISALLOWED;
        goto done;
    }

    /* Output (Controller to Host) */
    remove_rx = cmd->data_path_dir & (1 << BLE_HCI_ISO_DATA_PATH_DIR_OUTPUT);
    if (remove_rx && (conn->rx == NULL || conn->rx->data_path == NULL)) {
        rc = BLE_ERR_CMD_DISALLOWED;
        goto done;
    }

    if (remove_tx) {
        conn->tx->data_path = NULL;
    }

    if (remove_rx) {
        conn->rx->data_path = NULL;
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
    if (tx == NULL || conn->mux.bn == 0) {
        return BLE_ERR_UNSUPPORTED;
    }

    if (tx->data_path != NULL) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    if (cmd->payload_type > BLE_HCI_PAYLOAD_TYPE_MAXIMUM_LENGTH) {
        return BLE_ERR_INV_LMP_LL_PARM;
    }

    tx->test.payload_type = cmd->payload_type;
    tx->data_path = &test_iso_data_path_if;

    rsp->conn_handle = cmd->conn_handle;

    *rsplen = sizeof(*rsp);

    return BLE_ERR_SUCCESS;
}

int
ble_ll_iso_receive_test(const uint8_t *cmdbuf, uint8_t cmdlen,
                        uint8_t *rspbuf, uint8_t *rsplen)
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
    if (rx == NULL || conn->mux.bn == 0) {
        return BLE_ERR_UNSUPPORTED;
    }

    if (rx->data_path != NULL) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    rx->test.payload_type = cmd->payload_type;
    rx->test.received_sdu_count = 0;
    rx->test.missed_sdu_count = 0;
    rx->test.failed_sdu_count = 0;
    rx->data_path = &test_iso_data_path_if;

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
    if (rx == NULL || rx->data_path != &test_iso_data_path_if) {
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
ble_ll_iso_end_test(const uint8_t *cmdbuf, uint8_t cmdlen,
                    uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_iso_test_end_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_iso_test_end_rp *rsp = (void *)rspbuf;
    struct ble_ll_iso_conn *conn;
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

    if (conn->rx && conn->rx->data_path == &test_iso_data_path_if) {
        conn->rx->data_path = NULL;
        rsp->received_sdu_count = conn->rx->test.received_sdu_count;
        rsp->missed_sdu_count = conn->rx->test.missed_sdu_count;
        rsp->failed_sdu_count = conn->rx->test.failed_sdu_count;
        rc = BLE_ERR_SUCCESS;
    }

    if (conn->tx && conn->tx->data_path == &test_iso_data_path_if) {
        conn->tx->data_path = NULL;
        rc = BLE_ERR_SUCCESS;
    }

    *rsplen = sizeof(*rsp);

    return rc;
}

int
ble_ll_iso_read_tx_sync(const uint8_t *cmdbuf, uint8_t cmdlen,
                        uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_read_iso_tx_sync_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_read_iso_tx_sync_rp *rsp = (void *)rspbuf;
    struct ble_ll_iso_conn *conn;
    uint16_t handle;

    handle = le16toh(cmd->conn_handle);

    conn = ble_ll_iso_conn_find_by_handle(handle);
    if (conn == NULL) {
        return BLE_ERR_UNK_CONN_ID;
    }

    rsp->conn_handle = cmd->conn_handle;
    rsp->packet_seq_num = htole16(conn->mux.last_tx_packet_seq_num);
    rsp->tx_timestamp = htole32(conn->mux.last_tx_timestamp);
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

    rc = os_mempool_init(&hci_iso_data_pkt_mempool, HCI_ISO_BUF_COUNT,
                         HCI_ISO_BUF_SIZE, &hci_iso_data_pkt_membuf[0],
                         "iso_data_pkt_mempool");
    BLE_LL_ASSERT(rc == 0);

    rc = os_mbuf_pool_init(&hci_iso_data_pkt_mbuf_pool, &hci_iso_data_pkt_mempool,
                           HCI_ISO_BUF_SIZE, HCI_ISO_BUF_COUNT);
    BLE_LL_ASSERT(rc == 0);

    STAILQ_INIT(&ll_iso_conn_q);
    ble_ll_isoal_init();
}

void
ble_ll_iso_reset(void)
{
    ble_ll_isoal_reset();
}

int
ble_ll_iso_data_in(struct os_mbuf *om)
{
    struct ble_hci_iso *hci_iso;
    struct ble_hci_iso_data *hci_iso_data;
    struct ble_ll_iso_conn *conn;
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

    data_hdr_len = 0;
    if ((pb_flag == BLE_HCI_ISO_PB_FIRST) ||
        (pb_flag == BLE_HCI_ISO_PB_COMPLETE)) {
        blehdr = BLE_MBUF_HDR_PTR(om);
        blehdr->txiso.packet_seq_num = ++conn->mux.sdu_counter;
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
        ble_ll_isoal_mux_sdu_enqueue(&conn->mux, om);
    }

    return 0;
}

static int
ble_ll_iso_test_pdu_get(struct ble_ll_iso_conn *conn, uint8_t idx, uint32_t pkt_counter, uint8_t *llid, uint8_t *dptr)
{
    struct ble_ll_iso_tx *tx;
    uint32_t payload_len;
    uint16_t rem_len;
    uint8_t sdu_idx;
    uint8_t pdu_idx;
    int pdu_len;

    tx = conn->tx;

    BLE_LL_ASSERT(!conn->mux.framed);

    sdu_idx = idx / conn->mux.pdu_per_sdu;
    pdu_idx = idx - sdu_idx * conn->mux.pdu_per_sdu;

    switch (tx->test.payload_type) {
    case BLE_HCI_PAYLOAD_TYPE_ZERO_LENGTH:
        *llid = 0b00;
        pdu_len = 0;
        break;
    case BLE_HCI_PAYLOAD_TYPE_VARIABLE_LENGTH:
        payload_len = max(tx->test.rand + (sdu_idx * pdu_idx), 4);

        rem_len = payload_len - pdu_idx * conn->mux.max_pdu;
        if (rem_len == 0) {
            *llid = 0b01;
            pdu_len = 0;
        } else {
            *llid = rem_len > conn->mux.max_pdu;
            pdu_len = min(conn->mux.max_pdu, rem_len);
        }

        memset(dptr, 0, pdu_len);

        if (payload_len == rem_len) {
            put_le32(dptr, pkt_counter);
        }

        break;
    case BLE_HCI_PAYLOAD_TYPE_MAXIMUM_LENGTH:
        payload_len = conn->mux.max_sdu;

        rem_len = payload_len - pdu_idx * conn->mux.max_pdu;
        if (rem_len == 0) {
            *llid = 0b01;
            pdu_len = 0;
        } else {
            *llid = rem_len > conn->mux.max_pdu;
            pdu_len = min(conn->mux.max_pdu, rem_len);
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
ble_ll_iso_pdu_get(struct ble_ll_iso_conn *conn, uint8_t idx, uint32_t pkt_counter, uint8_t *llid, void *dptr)
{
    if (conn->tx && conn->tx->data_path == &test_iso_data_path_if) {
        return ble_ll_iso_test_pdu_get(conn, idx, pkt_counter, llid, dptr);
    }

    return ble_ll_isoal_mux_pdu_get(&conn->mux, idx, llid, dptr);
}

int
ble_ll_iso_data_pdu_in(struct ble_ll_iso_conn *conn, uint8_t idx, struct os_mbuf *om)
{
    ble_ll_isoal_mux_pdu_enqueue(&conn->mux, idx, om);
    return 0;
}

static void
iso_sdu_send_cb(struct ble_ll_isoal_mux *mux, const struct os_mbuf *om, uint32_t timestamp,
                uint16_t seq_num, bool valid)
{
    struct ble_ll_iso_conn *conn;
    struct ble_ll_iso_rx *rx;

    conn = CONTAINER_OF(mux, struct ble_ll_iso_conn, mux);

    rx = conn->rx;
    if (rx == NULL) {
        return;
    }

    if (rx->data_path != NULL) {
        rx->data_path->sdu_send(conn->handle, om, timestamp, seq_num, valid);
    }
}

static const struct ble_ll_isoal_mux_cb isoal_mux_cb = {
    .sdu_send = iso_sdu_send_cb,
};

void
ble_ll_iso_conn_add(struct ble_ll_iso_conn *conn)
{
    os_sr_t sr;

    OS_ENTER_CRITICAL(sr);
    STAILQ_INSERT_TAIL(&ll_iso_conn_q, conn, iso_conn_q_next);
    OS_EXIT_CRITICAL(sr);

    ble_ll_isoal_mux_cb_set(&conn->mux, &isoal_mux_cb);
}

void
ble_ll_iso_conn_rem(struct ble_ll_iso_conn *conn)
{
    os_sr_t sr;

    OS_ENTER_CRITICAL(sr);
    STAILQ_REMOVE(&ll_iso_conn_q, conn, ble_ll_iso_conn, iso_conn_q_next);
    OS_EXIT_CRITICAL(sr);

    ble_ll_isoal_mux_cb_set(&conn->mux, NULL);
}

int
ble_ll_iso_conn_event_start(struct ble_ll_iso_conn *conn, uint32_t timestamp)
{
    if (conn->tx && conn->tx->data_path == &test_iso_data_path_if) {
        conn->tx->test.rand = ble_ll_rand() % conn->mux.max_sdu;
    }

    ble_ll_isoal_mux_event_start(&conn->mux, timestamp);

    return 0;
}

int
ble_ll_iso_conn_event_done(struct ble_ll_iso_conn *conn)
{
    conn->num_completed_pkt += ble_ll_isoal_mux_event_done(&conn->mux);

    return conn->num_completed_pkt;
}

#endif /* BLE_LL_ISO */
