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

#include <string.h>
#include <errno.h>
#include <assert.h>
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "testutil/testutil.h"
#include "host/host_hci.h"
#include "ble_hs_priv.h"
#include "ble_hci_ack.h"
#include "ble_hci_sched.h"
#include "ble_hs_conn.h"
#include "ble_gap_conn.h"
#include "ble_l2cap.h"
#include "ble_att_cmd.h"
#include "ble_hs_test_util.h"

struct os_mbuf *ble_hs_test_util_prev_tx;

void
ble_hs_test_util_build_cmd_complete(uint8_t *dst, int len,
                                    uint8_t param_len, uint8_t num_pkts,
                                    uint16_t opcode)
{
    TEST_ASSERT(len >= BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN);

    dst[0] = BLE_HCI_EVCODE_COMMAND_COMPLETE;
    dst[1] = 3 + param_len;
    dst[2] = num_pkts;
    htole16(dst + 3, opcode);
}

void
ble_hs_test_util_build_cmd_status(uint8_t *dst, int len,
                                  uint8_t status, uint8_t num_pkts,
                                  uint16_t opcode)
{
    TEST_ASSERT(len >= BLE_HCI_EVENT_CMD_STATUS_LEN);

    dst[0] = BLE_HCI_EVCODE_COMMAND_STATUS;
    dst[1] = BLE_HCI_EVENT_CMD_STATUS_LEN;
    dst[2] = status;
    dst[3] = num_pkts;
    htole16(dst + 4, opcode);
}

struct ble_hs_conn *
ble_hs_test_util_create_conn(uint16_t handle, uint8_t *addr)
{
    struct hci_le_conn_complete evt;
    struct ble_hs_conn *conn;
    int rc;

    rc = ble_gap_conn_direct_connect(0, addr);
    TEST_ASSERT(rc == 0);

    ble_hci_sched_wakeup();

    ble_hs_test_util_rx_le_ack(BLE_HCI_OCF_LE_CREATE_CONN, BLE_ERR_SUCCESS);

    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = handle;
    memcpy(evt.peer_addr, addr, 6);
    rc = ble_gap_conn_rx_conn_complete(&evt);

    conn = ble_hs_conn_find(handle);
    TEST_ASSERT_FATAL(conn != NULL);

    return conn;
}

void
ble_hs_test_util_rx_ack(uint16_t opcode, uint8_t status)
{
    uint8_t buf[BLE_HCI_EVENT_CMD_STATUS_LEN];
    int rc;

    ble_hs_test_util_build_cmd_status(buf, sizeof buf, status, 1, opcode);
    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == 0);
}

void
ble_hs_test_util_rx_hci_buf_size_ack(uint16_t buf_size)
{
    uint8_t buf[BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN +
                BLE_HCI_RD_BUF_SIZE_RSPLEN + 1];
    int rc;

    ble_hs_test_util_build_cmd_complete(buf, sizeof buf,
                                        BLE_HCI_RD_BUF_SIZE_RSPLEN + 1, 1,
                                        (BLE_HCI_OGF_LE << 10) |
                                            BLE_HCI_OCF_LE_RD_BUF_SIZE);

    buf[BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN + 0] = 0;
    htole16(buf + BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN + 1, buf_size);
    buf[BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN + 3] = 1;

    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == 0);
}

void
ble_hs_test_util_rx_le_ack(uint16_t ocf, uint8_t status)
{
    ble_hs_test_util_rx_ack((BLE_HCI_OGF_LE << 10) | ocf, status);
}

int
ble_hs_test_util_l2cap_rx_payload_flat(struct ble_hs_conn *conn,
                                       struct ble_l2cap_chan *chan,
                                       const void *data, int len)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    int rc;

    om = os_mbuf_get_pkthdr(&ble_hs_mbuf_pool, 0);
    TEST_ASSERT_FATAL(om != NULL);

    om->om_data += BLE_L2CAP_HDR_SZ;

    rc = os_mbuf_append(om, data, len);
    TEST_ASSERT_FATAL(rc == 0);

    om = ble_l2cap_prepend_hdr(om, chan->blc_cid);
    TEST_ASSERT_FATAL(om != NULL);

    hci_hdr.hdh_handle_pb_bc =
        host_hci_handle_pb_bc_join(conn->bhc_handle,
                                   BLE_HCI_PB_FIRST_FLUSH, 0);
    hci_hdr.hdh_len = OS_MBUF_PKTHDR(om)->omp_len;

    rc = ble_l2cap_rx(conn, &hci_hdr, om);

    return rc;
}

void
ble_hs_test_util_rx_att_err_rsp(struct ble_hs_conn *conn, uint8_t req_op,
                                uint8_t error_code)
{
    struct ble_att_error_rsp rsp;
    struct ble_l2cap_chan *chan;
    uint8_t buf[BLE_ATT_ERROR_RSP_SZ];
    int rc;

    rsp.baep_req_op = req_op;
    rsp.baep_handle = conn->bhc_handle;
    rsp.baep_error_code = error_code;

    rc = ble_att_error_rsp_write(buf, sizeof buf, &rsp);
    TEST_ASSERT_FATAL(rc == 0);

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    TEST_ASSERT_FATAL(chan != NULL);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn, chan, buf, sizeof buf);
    TEST_ASSERT(rc == 0);
}

void
ble_hs_test_util_rx_startup_acks(void)
{
    /* Receive acknowledgements for the startup sequence.  We sent the
     * corresponding requests when the host task was started.
     */
    ble_hs_test_util_rx_hci_buf_size_ack(0xffff);
    ble_hs_test_util_rx_ack(
        (BLE_HCI_OGF_CTLR_BASEBAND << 10) | BLE_HCI_OCF_CB_SET_EVENT_MASK,
        0);
}

void
ble_hs_test_util_rx_num_completed_pkts_event(
    struct ble_hs_test_util_num_completed_pkts_entry *entries)
{
    struct ble_hs_test_util_num_completed_pkts_entry *entry;
    uint8_t buf[1024];
    int num_entries;
    int off;
    int rc;
    int i;

    /* Count number of entries. */
    num_entries = 0;
    for (entry = entries; entry->handle_id != 0; entry++) {
        num_entries++;
    }
    assert(num_entries <= UINT8_MAX);

    buf[0] = BLE_HCI_EVCODE_NUM_COMP_PKTS;
    buf[2] = num_entries;

    off = 3;
    for (i = 0; i < num_entries; i++) {
        htole16(buf + off, entries[i].handle_id);
        off += 2;
    }
    for (i = 0; i < num_entries; i++) {
        htole16(buf + off, entries[i].num_pkts);
        off += 2;
    }

    buf[1] = off - 2;

    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == 0);
}

void
ble_hs_test_util_init(void)
{
    int rc;

    rc = ble_hs_init(10);
    TEST_ASSERT_FATAL(rc == 0);

    ble_hs_test_util_prev_tx = NULL;
}
