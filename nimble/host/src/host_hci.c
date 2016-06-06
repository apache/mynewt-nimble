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
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "os/os.h"
#include "console/console.h"
#include "nimble/hci_common.h"
#include "nimble/hci_transport.h"
#include "host/host_hci.h"
#include "ble_hs_priv.h"
#include "host_dbg_priv.h"

_Static_assert(sizeof (struct hci_data_hdr) == BLE_HCI_DATA_HDR_SZ,
               "struct hci_data_hdr must be 4 bytes");

typedef int host_hci_event_fn(uint8_t event_code, uint8_t *data, int len);
static host_hci_event_fn host_hci_rx_disconn_complete;
static host_hci_event_fn host_hci_rx_encrypt_change;
static host_hci_event_fn host_hci_rx_num_completed_pkts;
static host_hci_event_fn host_hci_rx_le_meta;

typedef int host_hci_le_event_fn(uint8_t subevent, uint8_t *data, int len);
static host_hci_le_event_fn host_hci_rx_le_conn_complete;
static host_hci_le_event_fn host_hci_rx_le_adv_rpt;
static host_hci_le_event_fn host_hci_rx_le_conn_upd_complete;
static host_hci_le_event_fn host_hci_rx_le_lt_key_req;
static host_hci_le_event_fn host_hci_rx_le_conn_parm_req;

static uint16_t host_hci_buffer_sz;
static uint8_t host_hci_max_pkts;

/* Statistics */
struct host_hci_stats
{
    uint32_t events_rxd;
    uint32_t good_acks_rxd;
    uint32_t bad_acks_rxd;
    uint32_t unknown_events_rxd;
};

#define HOST_HCI_TIMEOUT        50      /* Milliseconds. */

/** Dispatch table for incoming HCI events.  Sorted by event code field. */
struct host_hci_event_dispatch_entry {
    uint8_t hed_event_code;
    host_hci_event_fn *hed_fn;
};

static const struct host_hci_event_dispatch_entry host_hci_event_dispatch[] = {
    { BLE_HCI_EVCODE_DISCONN_CMP, host_hci_rx_disconn_complete },
    { BLE_HCI_EVCODE_ENCRYPT_CHG, host_hci_rx_encrypt_change },
    { BLE_HCI_EVCODE_NUM_COMP_PKTS, host_hci_rx_num_completed_pkts },
    { BLE_HCI_EVCODE_LE_META, host_hci_rx_le_meta },
};

#define HOST_HCI_EVENT_DISPATCH_SZ \
    (sizeof host_hci_event_dispatch / sizeof host_hci_event_dispatch[0])

/** Dispatch table for incoming LE meta events.  Sorted by subevent field. */
struct host_hci_le_event_dispatch_entry {
    uint8_t hmd_subevent;
    host_hci_le_event_fn *hmd_fn;
};

static const struct host_hci_le_event_dispatch_entry
        host_hci_le_event_dispatch[] = {
    { BLE_HCI_LE_SUBEV_CONN_COMPLETE, host_hci_rx_le_conn_complete },
    { BLE_HCI_LE_SUBEV_ADV_RPT, host_hci_rx_le_adv_rpt },
    { BLE_HCI_LE_SUBEV_CONN_UPD_COMPLETE, host_hci_rx_le_conn_upd_complete },
    { BLE_HCI_LE_SUBEV_LT_KEY_REQ, host_hci_rx_le_lt_key_req },
    { BLE_HCI_LE_SUBEV_REM_CONN_PARM_REQ, host_hci_rx_le_conn_parm_req },
    { BLE_HCI_LE_SUBEV_ENH_CONN_COMPLETE, host_hci_rx_le_conn_complete },
};

#define HOST_HCI_LE_EVENT_DISPATCH_SZ \
    (sizeof host_hci_le_event_dispatch / sizeof host_hci_le_event_dispatch[0])

uint16_t
host_hci_opcode_join(uint8_t ogf, uint16_t ocf)
{
    return (ogf << 10) | ocf;
}

uint16_t
host_hci_handle_pb_bc_join(uint16_t handle, uint8_t pb, uint8_t bc)
{
    BLE_HS_DBG_ASSERT(handle <= 0x0fff);
    BLE_HS_DBG_ASSERT(pb <= 0x03);
    BLE_HS_DBG_ASSERT(bc <= 0x03);

    return (handle  << 0)   |
           (pb      << 12)  |
           (bc      << 14);
}

static const struct host_hci_event_dispatch_entry *
host_hci_dispatch_entry_find(uint8_t event_code)
{
    const struct host_hci_event_dispatch_entry *entry;
    int i;

    for (i = 0; i < HOST_HCI_EVENT_DISPATCH_SZ; i++) {
        entry = host_hci_event_dispatch + i;
        if (entry->hed_event_code == event_code) {
            return entry;
        }
    }

    return NULL;
}

static const struct host_hci_le_event_dispatch_entry *
host_hci_le_dispatch_entry_find(uint8_t event_code)
{
    const struct host_hci_le_event_dispatch_entry *entry;
    int i;

    for (i = 0; i < HOST_HCI_LE_EVENT_DISPATCH_SZ; i++) {
        entry = host_hci_le_event_dispatch + i;
        if (entry->hmd_subevent == event_code) {
            return entry;
        }
    }

    return NULL;
}

static int
host_hci_rx_disconn_complete(uint8_t event_code, uint8_t *data, int len)
{
    struct hci_disconn_complete evt;

    if (len < BLE_HCI_EVENT_DISCONN_COMPLETE_LEN) {
        return BLE_HS_EMSGSIZE;
    }

    evt.status = data[2];
    evt.connection_handle = le16toh(data + 3);
    evt.reason = data[5];

    ble_gap_rx_disconn_complete(&evt);

    return 0;
}

static int
host_hci_rx_encrypt_change(uint8_t event_code, uint8_t *data, int len)
{
    struct hci_encrypt_change evt;

    if (len < BLE_HCI_EVENT_ENCRYPT_CHG_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    evt.status = data[2];
    evt.connection_handle = le16toh(data + 3);
    evt.encryption_enabled = data[5];

    ble_sm_enc_change_rx(&evt);

    return 0;
}

static int
host_hci_rx_num_completed_pkts(uint8_t event_code, uint8_t *data, int len)
{
    uint16_t num_pkts;
    uint16_t handle;
    uint8_t num_handles;
    int off;
    int i;

    if (len < BLE_HCI_EVENT_HDR_LEN + BLE_HCI_EVENT_NUM_COMP_PKTS_HDR_LEN) {
        return BLE_HS_EMSGSIZE;
    }

    off = BLE_HCI_EVENT_HDR_LEN;
    num_handles = data[off];
    if (len < BLE_HCI_EVENT_NUM_COMP_PKTS_HDR_LEN +
              num_handles * BLE_HCI_EVENT_NUM_COMP_PKTS_ENT_LEN) {
        return BLE_HS_EMSGSIZE;
    }
    off++;

    for (i = 0; i < num_handles; i++) {
        handle = le16toh(data + off + 2 * i);
        num_pkts = le16toh(data + off + 2 * num_handles + 2 * i);

        /* XXX: Do something with these values. */
        (void)handle;
        (void)num_pkts;
    }

    return 0;
}

static int
host_hci_rx_le_meta(uint8_t event_code, uint8_t *data, int len)
{
    const struct host_hci_le_event_dispatch_entry *entry;
    uint8_t subevent;
    int rc;

    if (len < BLE_HCI_EVENT_HDR_LEN + BLE_HCI_LE_MIN_LEN) {
        /* XXX: Increment stat. */
        return BLE_HS_EMSGSIZE;
    }

    subevent = data[2];
    entry = host_hci_le_dispatch_entry_find(subevent);
    if (entry != NULL) {
        rc = entry->hmd_fn(subevent, data + BLE_HCI_EVENT_HDR_LEN,
                           len - BLE_HCI_EVENT_HDR_LEN);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

static int
host_hci_rx_le_conn_complete(uint8_t subevent, uint8_t *data, int len)
{
    struct hci_le_conn_complete evt;
    int extended_offset = 0;
    int rc;

    if (len < BLE_HCI_LE_CONN_COMPLETE_LEN) {
        return BLE_HS_EMSGSIZE;
    }

    /* this code processes two different events that are really similar */
    if ((subevent == BLE_HCI_LE_SUBEV_ENH_CONN_COMPLETE) &&
        ( len < BLE_HCI_LE_ENH_CONN_COMPLETE_LEN)) {
        return BLE_HS_EMSGSIZE;
    }

    evt.subevent_code = data[0];
    evt.status = data[1];
    evt.connection_handle = le16toh(data + 2);
    evt.role = data[4];
    evt.peer_addr_type = data[5];
    memcpy(evt.peer_addr, data + 6, BLE_DEV_ADDR_LEN);

    /* enhanced connection event has the same information with these
     * extra fields stuffed into the middle */
    if (subevent == BLE_HCI_LE_SUBEV_ENH_CONN_COMPLETE) {
        memcpy(evt.local_rpa, data + 12, BLE_DEV_ADDR_LEN);
        memcpy(evt.peer_rpa, data + 18, BLE_DEV_ADDR_LEN);
        extended_offset = 12;
    } else {
        memset(evt.local_rpa, 0, BLE_DEV_ADDR_LEN);
        memset(evt.peer_rpa, 0, BLE_DEV_ADDR_LEN);
    }

    evt.conn_itvl = le16toh(data + 12 + extended_offset);
    evt.conn_latency = le16toh(data + 14 + extended_offset);
    evt.supervision_timeout = le16toh(data + 16 + extended_offset);
    evt.master_clk_acc = data[18 + extended_offset];

    if (evt.status == 0) {
        if (evt.role != BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER &&
            evt.role != BLE_HCI_LE_CONN_COMPLETE_ROLE_SLAVE) {

            return BLE_HS_EBADDATA;
        }
    }

    rc = ble_gap_rx_conn_complete(&evt);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
host_hci_le_adv_rpt_first_pass(uint8_t *data, int len,
                               uint8_t *out_num_reports, int *out_rssi_off)
{
    uint8_t num_reports;
    int data_len;
    int off;
    int i;

    if (len < BLE_HCI_LE_ADV_RPT_MIN_LEN) {
        return BLE_HS_EMSGSIZE;
    }

    num_reports = data[1];
    if (num_reports < BLE_HCI_LE_ADV_RPT_NUM_RPTS_MIN ||
        num_reports > BLE_HCI_LE_ADV_RPT_NUM_RPTS_MAX) {

        return BLE_HS_EBADDATA;
    }

    off = 2 +       /* Subevent code and num reports. */
          (1 +      /* Event type. */
           1 +      /* Address type. */
           6        /* Address. */
          ) * num_reports;
    if (off + num_reports >= len) {
        return BLE_HS_EMSGSIZE;
    }

    data_len = 0;
    for (i = 0; i < num_reports; i++) {
        data_len += data[off];
        off++;
    }

    off += data_len;

    /* Check if RSSI fields fit in the packet. */
    if (off + num_reports > len) {
        return BLE_HS_EMSGSIZE;
    }

    *out_num_reports = num_reports;
    *out_rssi_off = off;

    return 0;
}

static int
host_hci_rx_le_adv_rpt(uint8_t subevent, uint8_t *data, int len)
{
    struct ble_hs_adv adv;
    uint8_t num_reports;
    int rssi_off;
    int data_off;
    int off;
    int rc;
    int i;

    rc = host_hci_le_adv_rpt_first_pass(data, len, &num_reports, &rssi_off);
    if (rc != 0) {
        return rc;
    }

    data_off = 0;
    for (i = 0; i < num_reports; i++) {
        off = 2 + 0 * num_reports + i;
        adv.event_type = data[2 + 0 * num_reports + i];

        off = 2 + 1 * num_reports + i;
        adv.addr_type = data[2 + 1 * num_reports + i];

        off = 2 + 2 * num_reports + i * 6;
        memcpy(adv.addr, data + off, 6);

        off = 2 + 8 * num_reports + i;
        adv.length_data = data[off];

        off = 2 + 9 * num_reports + data_off;
        adv.data = data + off;
        data_off += adv.length_data;

        off = rssi_off + 1 * i;
        adv.rssi = data[off];

        ble_gap_rx_adv_report(&adv);
    }

    return 0;
}

static int
host_hci_rx_le_conn_upd_complete(uint8_t subevent, uint8_t *data, int len)
{
    struct hci_le_conn_upd_complete evt;

    if (len < BLE_HCI_LE_CONN_UPD_LEN) {
        return BLE_HS_EMSGSIZE;
    }

    evt.subevent_code = data[0];
    evt.status = data[1];
    evt.connection_handle = le16toh(data + 2);
    evt.conn_itvl = le16toh(data + 4);
    evt.conn_latency = le16toh(data + 6);
    evt.supervision_timeout = le16toh(data + 8);

    if (evt.status == 0) {
        if (evt.conn_itvl < BLE_HCI_CONN_ITVL_MIN ||
            evt.conn_itvl > BLE_HCI_CONN_ITVL_MAX) {

            return BLE_HS_EBADDATA;
        }
        if (evt.conn_latency < BLE_HCI_CONN_LATENCY_MIN ||
            evt.conn_latency > BLE_HCI_CONN_LATENCY_MAX) {

            return BLE_HS_EBADDATA;
        }
        if (evt.supervision_timeout < BLE_HCI_CONN_SPVN_TIMEOUT_MIN ||
            evt.supervision_timeout > BLE_HCI_CONN_SPVN_TIMEOUT_MAX) {

            return BLE_HS_EBADDATA;
        }
    }

    ble_gap_rx_update_complete(&evt);

    return 0;
}

static int
host_hci_rx_le_lt_key_req(uint8_t subevent, uint8_t *data, int len)
{
    struct hci_le_lt_key_req evt;

    if (len < BLE_HCI_LE_LT_KEY_REQ_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    evt.subevent_code = data[0];
    evt.connection_handle = le16toh(data + 1);
    evt.random_number = le64toh(data + 3);
    evt.encrypted_diversifier = le16toh(data + 11);

    ble_sm_ltk_req_rx(&evt);

    return 0;
}

static int
host_hci_rx_le_conn_parm_req(uint8_t subevent, uint8_t *data, int len)
{
    struct hci_le_conn_param_req evt;

    if (len < BLE_HCI_LE_REM_CONN_PARM_REQ_LEN) {
        return BLE_HS_EMSGSIZE;
    }

    evt.subevent_code = data[0];
    evt.connection_handle = le16toh(data + 1);
    evt.itvl_min = le16toh(data + 3);
    evt.itvl_max = le16toh(data + 5);
    evt.latency = le16toh(data + 7);
    evt.timeout = le16toh(data + 9);

    if (evt.itvl_min < BLE_HCI_CONN_ITVL_MIN ||
        evt.itvl_max > BLE_HCI_CONN_ITVL_MAX ||
        evt.itvl_min > evt.itvl_max) {

        return BLE_HS_EBADDATA;
    }
    if (evt.latency < BLE_HCI_CONN_LATENCY_MIN ||
        evt.latency > BLE_HCI_CONN_LATENCY_MAX) {

        return BLE_HS_EBADDATA;
    }
    if (evt.timeout < BLE_HCI_CONN_SPVN_TIMEOUT_MIN ||
        evt.timeout > BLE_HCI_CONN_SPVN_TIMEOUT_MAX) {

        return BLE_HS_EBADDATA;
    }

    ble_gap_rx_param_req(&evt);

    return 0;
}

int
host_hci_set_buf_size(uint16_t pktlen, uint8_t max_pkts)
{
    if (pktlen == 0 || max_pkts == 0) {
        return BLE_HS_EINVAL;
    }

    host_hci_buffer_sz = pktlen;
    host_hci_max_pkts = max_pkts;

    return 0;
}

int
host_hci_event_rx(uint8_t *data)
{
    const struct host_hci_event_dispatch_entry *entry;
    uint8_t event_code;
    uint8_t param_len;
    int event_len;
    int rc;

    /* Count events received */
    STATS_INC(ble_hs_stats, hci_event);

    /* Display to console */
    host_hci_dbg_event_disp(data);

    /* Process the event */
    event_code = data[0];
    param_len = data[1];

    event_len = param_len + 2;

    entry = host_hci_dispatch_entry_find(event_code);
    if (entry == NULL) {
        STATS_INC(ble_hs_stats, hci_invalid_ack);
        rc = BLE_HS_ENOTSUP;
    } else {
        rc = entry->hed_fn(event_code, data, event_len);
    }

    return rc;
}

int
host_hci_os_event_proc(struct os_event *ev)
{
    os_error_t err;
    int rc;

    rc = host_hci_event_rx(ev->ev_arg);

    /* Free the command buffer */
    err = os_memblock_put(&g_hci_cmd_pool, ev->ev_arg);
    BLE_HS_DBG_ASSERT_EVAL(err == OS_OK);

    /* Free the event */
    err = os_memblock_put(&g_hci_os_event_pool, ev);
    BLE_HS_DBG_ASSERT_EVAL(err == OS_OK);

    return rc;
}

/* XXX: For now, put this here */
int
ble_hci_transport_ctlr_event_send(uint8_t *hci_ev)
{
    struct os_event *ev;
    os_error_t err;
    int enqueue;

    BLE_HS_DBG_ASSERT(hci_ev != NULL);

    switch (hci_ev[0]) {
    case BLE_HCI_EVCODE_COMMAND_COMPLETE:
    case BLE_HCI_EVCODE_COMMAND_STATUS:
        if (hci_ev[3] == 0 && hci_ev[4] == 0) {
            enqueue = 1;
        } else {
            ble_hci_cmd_rx_ack(hci_ev);
            enqueue = 0;
        }
        break;

    default:
        enqueue = 1;
        break;
    }

    if (enqueue) {
        /* Get an event structure off the queue */
        ev = (struct os_event *)os_memblock_get(&g_hci_os_event_pool);
        if (!ev) {
            err = os_memblock_put(&g_hci_cmd_pool, hci_ev);
            BLE_HS_DBG_ASSERT_EVAL(err == OS_OK);
            return -1;
        }

        /* Fill out the event and post to host task. */
        ev->ev_queued = 0;
        ev->ev_type = BLE_HOST_HCI_EVENT_CTLR_EVENT;
        ev->ev_arg = hci_ev;
        ble_hs_event_enqueue(ev);
    }

    return 0;
}

static int
host_hci_data_hdr_strip(struct os_mbuf *om, struct hci_data_hdr *hdr)
{
    int rc;

    rc = os_mbuf_copydata(om, 0, BLE_HCI_DATA_HDR_SZ, hdr);
    if (rc != 0) {
        return BLE_HS_EMSGSIZE;
    }

    /* Strip HCI ACL data header from the front of the packet. */
    os_mbuf_adj(om, BLE_HCI_DATA_HDR_SZ);

    hdr->hdh_handle_pb_bc = le16toh(&hdr->hdh_handle_pb_bc);
    hdr->hdh_len = le16toh(&hdr->hdh_len);

    return 0;
}

/**
 * Called when a data packet is received from the controller.  This function
 * consumes the supplied mbuf, regardless of the outcome.
 *
 * @param om                    The incoming data packet, beginning with the
 *                                  HCI ACL data header.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
host_hci_data_rx(struct os_mbuf *om)
{
    struct hci_data_hdr hci_hdr;
    struct ble_hs_conn *conn;
    ble_l2cap_rx_fn *rx_cb;
    struct os_mbuf *rx_buf;
    uint16_t handle;
    int rc;

    rc = host_hci_data_hdr_strip(om, &hci_hdr);
    if (rc == 0) {
#if (BLETEST_THROUGHPUT_TEST == 0)
        BLE_HS_LOG(DEBUG, "host_hci_data_rx(): handle=%u pb=%x len=%u data=",
                   BLE_HCI_DATA_HANDLE(hci_hdr.hdh_handle_pb_bc), 
                   BLE_HCI_DATA_PB(hci_hdr.hdh_handle_pb_bc), 
                   hci_hdr.hdh_len);
        ble_hs_misc_log_mbuf(om);
        BLE_HS_LOG(DEBUG, "\n");
#endif

        if (hci_hdr.hdh_len != OS_MBUF_PKTHDR(om)->omp_len) {
            rc = BLE_HS_EBADDATA;
        } else {
            handle = BLE_HCI_DATA_HANDLE(hci_hdr.hdh_handle_pb_bc);

            ble_hs_lock();

            conn = ble_hs_conn_find(handle);
            if (conn == NULL) {
                rc = BLE_HS_ENOTCONN;
            } else {
                rc = ble_l2cap_rx(conn, &hci_hdr, om, &rx_cb, &rx_buf);
                om = NULL;
            }

            ble_hs_unlock();
        }
    }

    os_mbuf_free_chain(om);

    if (rc == 0) {
        BLE_HS_DBG_ASSERT(rx_cb != NULL);
        BLE_HS_DBG_ASSERT(rx_buf != NULL);
        rc = rx_cb(handle, &rx_buf);
        os_mbuf_free_chain(rx_buf);
    } else if (rc == BLE_HS_EAGAIN) {
        /* More fragments on the way. */
        rc = 0;
    }

    return rc;
}

static struct os_mbuf *
host_hci_data_hdr_prepend(struct os_mbuf *om, uint16_t handle, uint8_t pb_flag)
{
    struct hci_data_hdr hci_hdr;

    hci_hdr.hdh_handle_pb_bc = host_hci_handle_pb_bc_join(handle, pb_flag, 0);
    htole16(&hci_hdr.hdh_len, OS_MBUF_PKTHDR(om)->omp_len);

    om = os_mbuf_prepend(om, sizeof hci_hdr);
    if (om == NULL) {
        return NULL;
    }

    memcpy(om->om_data, &hci_hdr, sizeof hci_hdr);

    BLE_HS_LOG(DEBUG, "host tx hci data; handle=%d length=%d\n", handle,
               le16toh(&hci_hdr.hdh_len));

    return om;
}

/**
 * Transmits an HCI ACL data packet.  This function consumes the supplied mbuf,
 * regardless of the outcome.
 */
int
host_hci_data_tx(struct ble_hs_conn *connection, struct os_mbuf *om)
{
    int rc;

    /* XXX: Different transport mechanisms have different fragmentation
     * requirements.  For now, never fragment.
     */
    om = host_hci_data_hdr_prepend(om, connection->bhc_handle,
                                   BLE_HCI_PB_FIRST_NON_FLUSH);
    if (om == NULL) {
        return BLE_HS_ENOMEM;
    }

    BLE_HS_LOG(DEBUG, "host_hci_data_tx(): ");
    ble_hs_misc_log_mbuf(om);
    BLE_HS_LOG(DEBUG, "\n");

    rc = ble_hs_tx_data(om);
    if (rc != 0) {
        return rc;
    }

    connection->bhc_outstanding_pkts++;

    return 0;
}
