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
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "os/os.h"
#include "console/console.h"
#include "nimble/hci_common.h"
#include "nimble/hci_transport.h"
#include "host/host_hci.h"
#include "host/ble_hs.h"
#include "host_dbg.h"
#include "ble_hs_conn.h"
#include "ble_l2cap.h"
#include "ble_hs_ack.h"
#include "ble_gap_conn.h"

_Static_assert(sizeof (struct hci_data_hdr) == BLE_HCI_DATA_HDR_SZ,
               "struct hci_data_hdr must be 4 bytes");

static int host_hci_rx_disconn_complete(uint8_t event_code, uint8_t *data,
                                        int len);
static int host_hci_rx_cmd_complete(uint8_t event_code, uint8_t *data,
                                    int len);
static int host_hci_rx_cmd_status(uint8_t event_code, uint8_t *data, int len);
static int host_hci_rx_le_meta(uint8_t event_code, uint8_t *data, int len);
static int host_hci_rx_le_conn_complete(uint8_t subevent, uint8_t *data,
                                        int len);

#define HCI_CMD_BUFS        (8)
#define HCI_CMD_BUF_SIZE    (260)       /* XXX: temporary, Fix later */
struct os_mempool g_hci_cmd_pool;
os_membuf_t g_hci_cmd_buf[OS_MEMPOOL_SIZE(HCI_CMD_BUFS, HCI_CMD_BUF_SIZE)];

/* XXX: this might be transport layer*/
#define HCI_NUM_OS_EVENTS       (32)
#define HCI_OS_EVENT_BUF_SIZE   (sizeof(struct os_event))

struct os_mempool g_hci_os_event_pool;
os_membuf_t g_hci_os_event_buf[OS_MEMPOOL_SIZE(HCI_NUM_OS_EVENTS, 
                                               HCI_OS_EVENT_BUF_SIZE)];

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

struct host_hci_stats g_host_hci_stats;

/** The opcode of the current unacked HCI command; 0 if none. */
uint16_t host_hci_outstanding_opcode;

/** Dispatch table for incoming HCI events.  Sorted by event code field. */
typedef int host_hci_event_fn(uint8_t event_code, uint8_t *data, int len);
struct host_hci_event_dispatch_entry {
    uint8_t hed_event_code;
    host_hci_event_fn *hed_fn;
};

static const struct host_hci_event_dispatch_entry host_hci_event_dispatch[] = {
    { BLE_HCI_EVCODE_COMMAND_COMPLETE, host_hci_rx_cmd_complete },
    { BLE_HCI_EVCODE_COMMAND_STATUS, host_hci_rx_cmd_status },
    { BLE_HCI_EVCODE_DISCONN_CMP, host_hci_rx_disconn_complete },
    { BLE_HCI_EVCODE_LE_META, host_hci_rx_le_meta },
};

#define HOST_HCI_EVENT_DISPATCH_SZ \
    (sizeof host_hci_event_dispatch / sizeof host_hci_event_dispatch[0])

/** Dispatch table for incoming LE meta events.  Sorted by subevent field. */
typedef int host_hci_le_event_fn(uint8_t subevent, uint8_t *data, int len);
struct host_hci_le_event_dispatch_entry {
    uint8_t hmd_subevent;
    host_hci_le_event_fn *hmd_fn;
};

static const struct host_hci_le_event_dispatch_entry
        host_hci_le_event_dispatch[] = {
    { BLE_HCI_LE_SUBEV_CONN_COMPLETE, host_hci_rx_le_conn_complete },
};

#define HOST_HCI_LE_EVENT_DISPATCH_SZ \
    (sizeof host_hci_le_event_dispatch / sizeof host_hci_le_event_dispatch[0])

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

    for (i = 0; i < HOST_HCI_EVENT_DISPATCH_SZ; i++) {
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
    int rc;

    if (len < BLE_HCI_EVENT_DISCONN_COMPLETE_LEN) {
        return EMSGSIZE;
    }

    evt.status = data[0];
    evt.connection_handle = le16toh(data + 1);
    evt.reason = data[3];

    rc = ble_gap_conn_rx_disconn_complete(&evt);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
host_hci_rx_cmd_complete(uint8_t event_code, uint8_t *data, int len)
{
    struct ble_hs_ack ack;
    uint16_t opcode;
    uint8_t num_pkts;
    uint8_t *params;

    if (len < BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN) {
        /* XXX: Increment stat. */
        return EMSGSIZE;
    }

    num_pkts = data[2];
    opcode = le16toh(data + 3);
    params = data + 5;

    /* XXX: Process num_pkts field. */
    (void)num_pkts;

    if (opcode != BLE_HCI_OPCODE_NOP &&
        opcode != host_hci_outstanding_opcode) {

        ++g_host_hci_stats.bad_acks_rxd;
        return ENOENT;
    }

    if (opcode == host_hci_outstanding_opcode) {
        /* Mark the outstanding command as acked. */
        host_hci_outstanding_opcode = 0;
        /* XXX: Stop timer. */
    }

    ack.bha_opcode = opcode;
    ack.bha_params = params;
    ack.bha_params_len = len - BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN;
    if (ack.bha_params_len > 0) {
        ack.bha_status = params[0];
    } else {
        ack.bha_status = 255;
    }

    ble_hs_ack_rx(&ack);

    return 0;
}

static int
host_hci_rx_cmd_status(uint8_t event_code, uint8_t *data, int len)
{
    struct ble_hs_ack ack;
    uint16_t opcode;
    uint8_t num_pkts;
    uint8_t status;

    if (len < BLE_HCI_EVENT_CMD_STATUS_LEN) {
        /* XXX: Increment stat. */
        return EMSGSIZE;
    }

    status = data[2];
    num_pkts = data[3];
    opcode = le16toh(data + 4);

    /* XXX: Process num_pkts field. */
    (void)num_pkts;

    /* XXX: This check might be overaggressive for the command status event. */
    if (opcode != BLE_HCI_OPCODE_NOP &&
        opcode != host_hci_outstanding_opcode) {

        ++g_host_hci_stats.bad_acks_rxd;
        return ENOENT;
    }

    if (opcode == host_hci_outstanding_opcode) {
        /* Mark the outstanding command as acked. */
        host_hci_outstanding_opcode = 0;
        /* XXX: Stop timer. */
    }

    ack.bha_opcode = opcode;
    ack.bha_params = NULL;
    ack.bha_params_len = 0;
    ack.bha_status = status;

    ble_hs_ack_rx(&ack);

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
        return EMSGSIZE;
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
    int rc;

    if (len < BLE_HCI_LE_CONN_COMPLETE_LEN) {
        return EMSGSIZE;
    }

    evt.subevent_code = data[0];
    evt.status = data[1];
    evt.connection_handle = le16toh(data + 2);
    evt.role = data[4];
    evt.peer_addr_type = data[5];
    memcpy(evt.peer_addr, data + 6, BLE_DEV_ADDR_LEN);
    evt.conn_itvl = le16toh(data + 12);
    evt.conn_latency = le16toh(data + 14);
    evt.supervision_timeout = le16toh(data + 16);
    evt.master_clk_acc = data[18];

    rc = ble_gap_conn_rx_conn_complete(&evt);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
host_hci_set_buf_size(uint16_t pktlen, uint8_t max_pkts)
{
    if (pktlen == 0 || max_pkts == 0) {
        return EINVAL;
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
    ++g_host_hci_stats.events_rxd;

    /* Display to console */
    host_hci_dbg_event_disp(data);

    /* Process the event */
    event_code = data[0];
    param_len = data[1];

    event_len = param_len + 2;

    entry = host_hci_dispatch_entry_find(event_code);
    if (entry == NULL) {
        ++g_host_hci_stats.unknown_events_rxd;
        rc = ENOTSUP;
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
    assert(err == OS_OK);

    /* Free the event */
    err = os_memblock_put(&g_hci_os_event_pool, ev);
    assert(err == OS_OK);

    return rc;
}

/* XXX: For now, put this here */
int
ble_hci_transport_ctlr_event_send(uint8_t *hci_ev)
{
    os_error_t err;
    struct os_event *ev;

    assert(hci_ev != NULL);

    /* Get an event structure off the queue */
    ev = (struct os_event *)os_memblock_get(&g_hci_os_event_pool);
    if (!ev) {
        err = os_memblock_put(&g_hci_cmd_pool, hci_ev);
        assert(err == OS_OK);
        return -1;
    }

    /* Fill out the event and post to Link Layer */
    ev->ev_queued = 0;
    ev->ev_type = BLE_HOST_HCI_EVENT_CTLR_EVENT;
    ev->ev_arg = hci_ev;
    os_eventq_put(&ble_hs_evq, ev);

    return 0;
}

static int
host_hci_data_hdr_strip(struct os_mbuf *om, struct hci_data_hdr *hdr)
{
    int rc;

    rc = os_mbuf_copydata(om, 0, BLE_HCI_DATA_HDR_SZ, hdr);
    if (rc != 0) {
        return EMSGSIZE;
    }

    /* Strip HCI ACL data header from the front of the packet. */
    /* XXX: This is probably the wrong mbuf pool. */
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
    struct ble_hs_conn *connection;
    struct hci_data_hdr hci_hdr;
    uint16_t handle;
    int rc;

    rc = host_hci_data_hdr_strip(om, &hci_hdr);
    if (rc != 0) {
        goto done;
    }

    if (hci_hdr.hdh_len != OS_MBUF_PKTHDR(om)->omp_len) {
        rc = EMSGSIZE;
        goto done;
    }

    handle = BLE_HCI_DATA_HANDLE(hci_hdr.hdh_handle_pb_bc);
    connection = ble_hs_conn_find(handle);
    if (connection == NULL) {
        rc = ENOTCONN;
        goto done;
    }

    rc = ble_l2cap_rx(connection, &hci_hdr, om);
    om = NULL;

done:
    os_mbuf_free_chain(om); /* XXX: Wrong pool. */
    return rc;
}

uint16_t
host_hci_handle_pb_bc_join(uint16_t handle, uint8_t pb, uint8_t bc)
{
    assert(handle <= 0x0fff);
    assert(pb <= 0x03);
    assert(bc <= 0x03);

    return (handle  << 0)   |
           (pb      << 12)  |
           (bc      << 14);
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
                                   BLE_HCI_PB_FULL);
    if (om == NULL) {
        return ENOMEM;
    }

    rc = ble_hs_tx_data(om);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
host_hci_init(void)
{
    host_hci_outstanding_opcode = 0;
}
