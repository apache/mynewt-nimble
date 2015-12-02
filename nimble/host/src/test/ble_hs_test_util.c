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
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "testutil/testutil.h"
#include "host/host_hci.h"
#include "host/ble_hs.h"
#include "ble_hs_ack.h"
#include "ble_hs_conn.h"
#include "ble_gap_conn.h"
#include "ble_hs_hci_batch.h"
#include "ble_l2cap.h"
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

void
ble_hs_test_util_create_conn(uint16_t handle, uint8_t *addr)
{
    struct hci_le_conn_complete evt;
    int rc;

    rc = ble_gap_direct_connection_establishment(0, addr);
    TEST_ASSERT(rc == 0);

    ble_hs_hci_batch_process_next();

    ble_hs_test_util_rx_le_ack(BLE_HCI_OCF_LE_CREATE_CONN, BLE_ERR_SUCCESS);

    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    memcpy(evt.peer_addr, addr, 6);
    rc = ble_gap_conn_rx_conn_complete(&evt);
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
        host_hci_handle_pb_bc_join(conn->bhc_handle, BLE_HCI_PB_FULL, 0);
    hci_hdr.hdh_len = OS_MBUF_PKTHDR(om)->omp_len;

    rc = ble_l2cap_rx(conn, &hci_hdr, om);

    return rc;
}

void
ble_hs_test_util_init(void)
{
    int rc;

    rc = ble_hs_init(10);
    TEST_ASSERT_FATAL(rc == 0);

    ble_hs_test_util_prev_tx = NULL;
}
