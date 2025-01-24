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

#include <stdint.h>
#include <syscfg/syscfg.h>
#include <nimble/hci_common.h>
#include <controller/ble_ll.h>
#include <controller/ble_ll_isoal.h>
#include <controller/ble_ll_iso.h>
#include <controller/ble_ll_tmr.h>

#if MYNEWT_VAL(BLE_LL_ISO)

STAILQ_HEAD(ble_ll_iso_conn_q, ble_ll_iso_conn);
struct ble_ll_iso_conn_q ll_iso_conn_q;

int
ble_ll_iso_setup_iso_data_path(const uint8_t *cmdbuf, uint8_t cmdlen,
                               uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_setup_iso_data_path_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_setup_iso_data_path_rp *rsp = (void *)rspbuf;
    struct ble_ll_iso_conn *conn;
    uint16_t conn_handle;

    conn_handle = le16toh(cmd->conn_handle);

    conn = ble_ll_iso_conn_find_by_handle(conn_handle);
    if (!conn) {
        return BLE_ERR_UNK_CONN_ID;
    }

    /* Only input for now since we only support BIS */
    if (cmd->data_path_dir) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* We do not (yet) support any vendor-specific data path */
    if (cmd->data_path_id) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    rsp->conn_handle = cmd->conn_handle;
    *rsplen = sizeof(*rsp);

    return 0;
}

int
ble_ll_iso_remove_iso_data_path(const uint8_t *cmdbuf, uint8_t cmdlen,
                                uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_remove_iso_data_path_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_remove_iso_data_path_rp *rsp = (void *)rspbuf;

    /* XXX accepts anything for now */
    rsp->conn_handle = cmd->conn_handle;
    *rsplen = sizeof(*rsp);

    return 0;
}

int
ble_ll_iso_read_tx_sync(const uint8_t *cmdbuf, uint8_t cmdlen,
                        uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_read_iso_tx_sync_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_read_iso_tx_sync_rp *rsp = (void *)rspbuf;
    struct ble_ll_iso_conn *iso_conn;
    uint16_t handle;

    handle = le16toh(cmd->conn_handle);
    iso_conn = ble_ll_iso_conn_find_by_handle(handle);
    if (!iso_conn) {
        return BLE_ERR_UNK_CONN_ID;
    }

    rsp->conn_handle = cmd->conn_handle;
    rsp->packet_seq_num = htole16(iso_conn->mux.last_tx_packet_seq_num);
    rsp->tx_timestamp = htole32(iso_conn->mux.last_tx_timestamp);
    put_le24(rsp->time_offset, 0);

    *rsplen = sizeof(*rsp);

    return 0;
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
    STAILQ_INIT(&ll_iso_conn_q);
    ble_ll_isoal_init();
}

void
ble_ll_iso_reset(void)
{
    STAILQ_INIT(&ll_iso_conn_q);
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

int
ble_ll_iso_pdu_get(struct ble_ll_iso_conn *conn, uint8_t idx, uint8_t *llid, void *dptr)
{
    return ble_ll_isoal_mux_pdu_get(&conn->mux, idx, llid, dptr);
}

void
ble_ll_iso_conn_init(struct ble_ll_iso_conn *conn, uint16_t conn_handle,
                     uint8_t max_pdu, uint32_t iso_interval_us,
                     uint32_t sdu_interval_us, uint8_t bn, uint8_t pte,
                     uint8_t framing)
{
    os_sr_t sr;

    memset(conn, 0, sizeof(*conn));

    conn->handle = conn_handle;
    ble_ll_isoal_mux_init(&conn->mux, max_pdu, iso_interval_us, sdu_interval_us,
                          bn, pte, BLE_LL_ISOAL_MUX_IS_FRAMED(framing),
                          framing == BLE_HCI_ISO_FRAMING_FRAMED_UNSEGMENTED);

    OS_ENTER_CRITICAL(sr);
    STAILQ_INSERT_TAIL(&ll_iso_conn_q, conn, iso_conn_q_next);
    OS_EXIT_CRITICAL(sr);
}

void
ble_ll_iso_conn_free(struct ble_ll_iso_conn *conn)
{
    os_sr_t sr;

    OS_ENTER_CRITICAL(sr);
    STAILQ_REMOVE(&ll_iso_conn_q, conn, ble_ll_iso_conn, iso_conn_q_next);
    OS_EXIT_CRITICAL(sr);

    ble_ll_isoal_mux_free(&conn->mux);
}

int
ble_ll_iso_conn_event_start(struct ble_ll_iso_conn *conn, uint32_t timestamp)
{
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
