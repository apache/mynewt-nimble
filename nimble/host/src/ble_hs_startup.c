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

#include <stddef.h>
#include <string.h>
#include "host/ble_hs.h"
#include "ble_hs_priv.h"

static int
ble_hs_startup_le_read_sup_f_tx(void)
{
    uint8_t ack_params[BLE_HCI_RD_LOC_SUPP_FEAT_RSPLEN];
    uint8_t buf[BLE_HCI_CMD_HDR_LEN];
    uint8_t ack_params_len;
    int rc;

    ble_hs_hci_cmd_build_le_read_loc_supp_feat(buf, sizeof buf);
    rc = ble_hs_hci_cmd_tx(buf, ack_params, sizeof ack_params,
                           &ack_params_len);
    if (rc != 0) {
        return rc;
    }

    if (ack_params_len != BLE_HCI_RD_LOC_SUPP_FEAT_RSPLEN) {
        return BLE_HS_ECONTROLLER;
    }

    /* XXX: Do something with the supported features bit map. */

    return 0;
}

static int
ble_hs_startup_le_read_buf_sz_tx(void)
{
    uint16_t pktlen;
    uint8_t ack_params[BLE_HCI_RD_BUF_SIZE_RSPLEN];
    uint8_t buf[BLE_HCI_CMD_HDR_LEN];
    uint8_t ack_params_len;
    uint8_t max_pkts;
    int rc;

    ble_hs_hci_cmd_build_le_read_buffer_size(buf, sizeof buf);
    rc = ble_hs_hci_cmd_tx(buf, ack_params, sizeof ack_params,
                           &ack_params_len);
    if (rc != 0) {
        return rc;
    }

    if (ack_params_len != BLE_HCI_RD_BUF_SIZE_RSPLEN) {
        return BLE_HS_ECONTROLLER;
    }

    pktlen = get_le16(ack_params + 0);
    max_pkts = ack_params[2];

    rc = ble_hs_hci_set_buf_sz(pktlen, max_pkts);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_hs_startup_read_bd_addr(void)
{
    uint8_t ack_params[BLE_HCI_IP_RD_BD_ADDR_ACK_PARAM_LEN];
    uint8_t buf[BLE_HCI_CMD_HDR_LEN];
    uint8_t ack_params_len;
    int rc;

    ble_hs_hci_cmd_build_read_bd_addr(buf, sizeof buf);
    rc = ble_hs_hci_cmd_tx(buf, ack_params, sizeof ack_params,
                           &ack_params_len);
    if (rc != 0) {
        return rc;
    }

    if (ack_params_len != sizeof ack_params) {
        return BLE_HS_ECONTROLLER;
    }

    ble_hs_id_set_pub(ack_params);
    return 0;
}

static int
ble_hs_startup_le_set_evmask_tx(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_LE_EVENT_MASK_LEN];
    int rc;

    /**
     * Enable the following LE events:
     *     0x0000000000000001 LE Connection Complete Event
     *     0x0000000000000002 LE Advertising Report Event
     *     0x0000000000000004 LE Connection Update Complete Event
     *     0x0000000000000008 LE Read Remote Used Features Complete Event
     *     0x0000000000000010 LE Long Term Key Request Event
     *     0x0000000000000020 LE Remote Connection Parameter Request Event
     *     0x0000000000000040 LE Data Length Change Event
     *     0x0000000000000200 LE Enhanced Connection Complete Event
     */
    ble_hs_hci_cmd_build_le_set_event_mask(0x000000000000027f,
                                           buf, sizeof buf);
    rc = ble_hs_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_hs_startup_set_evmask_tx(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_EVENT_MASK_LEN];
    int rc;

    /**
     * Enable the following events:
     *     0x0000000000000001 Inquiry Complete Event
     *     0x0000000000000002 Inquiry Result Event
     *     0x0000000000000004 Connection Complete Event
     *     0x0000000000000008 Connection Request Event
     *     0x0000000000000010 Disconnection Complete Event
     *     0x0000000000000020 Authentication Complete Event
     *     0x0000000000000040 Remote Name Request Complete Event
     *     0x0000000000000080 Encryption Change Event
     *     0x0000000000000100 Change Connection Link Key Complete Event
     *     0x0000000000000200 Master Link Key Complete Event
     *     0x0000000000000400 Read Remote Supported Features Complete Event
     *     0x0000000000000800 Read Remote Version Information Complete Event
     *     0x0000000000001000 QoS Setup Complete Event
     *     0x0000000000002000 Reserved
     *     0x0000000000004000 Reserved
     *     0x0000000000008000 Hardware Error Event
     *     0x0000000000010000 Flush Occurred Event
     *     0x0000000000020000 Role Change Event
     *     0x0000000000040000 Reserved
     *     0x0000000000080000 Mode Change Event
     *     0x0000000000100000 Return Link Keys Event
     *     0x0000000000200000 PIN Code Request Event
     *     0x0000000000400000 Link Key Request Event
     *     0x0000000000800000 Link Key Notification Event
     *     0x0000000001000000 Loopback Command Event
     *     0x0000000002000000 Data Buffer Overflow Event
     *     0x0000000004000000 Max Slots Change Event
     *     0x0000000008000000 Read Clock Offset Complete Event
     *     0x0000000010000000 Connection Packet Type Changed Event
     *     0x0000000020000000 QoS Violation Event
     *     0x0000000040000000 Page Scan Mode Change Event [deprecated]
     *     0x0000000080000000 Page Scan Repetition Mode Change Event
     *     0x0000000100000000 Flow Specification Complete Event
     *     0x0000000200000000 Inquiry Result with RSSI Event
     *     0x0000000400000000 Read Remote Extended Features Complete Event
     *     0x0000080000000000 Synchronous Connection Complete Event
     *     0x0000100000000000 Synchronous Connection Changed Event
     *     0x0000800000000000 Encryption Key Refresh Complete Event
     *     0x2000000000000000 LE Meta-Event
     */
    ble_hs_hci_cmd_build_set_event_mask(0x20009807ffffffff, buf, sizeof buf);
    rc = ble_hs_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    /**
     * Enable the following events:
     *     0x0000000000800000 Authenticated Payload Timeout Event
     */
    ble_hs_hci_cmd_build_set_event_mask2(0x0000000000800000, buf, sizeof buf);
    rc = ble_hs_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        BLE_HS_LOG(WARN, "ble_hs_startup_set_evmask_tx() failed\n");
    }

    return 0;
}

static int
ble_hs_startup_reset_tx(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN];
    int rc;

    ble_hs_hci_cmd_build_reset(buf, sizeof buf);
    rc = ble_hs_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_hs_startup_go(void)
{
    int rc;

    rc = ble_hs_startup_reset_tx();
    if (rc != 0) {
        return rc;
    }

    /* XXX: Read local supported commands. */
    /* XXX: Read local supported features. */

    rc = ble_hs_startup_set_evmask_tx();
    if (rc != 0) {
        return rc;
    }

    rc = ble_hs_startup_le_set_evmask_tx();
    if (rc != 0) {
        return rc;
    }

    rc = ble_hs_startup_le_read_buf_sz_tx();
    if (rc != 0) {
        return rc;
    }

    /* XXX: Read buffer size. */

    rc = ble_hs_startup_le_read_sup_f_tx();
    if (rc != 0) {
        return rc;
    }

    rc = ble_hs_startup_read_bd_addr();
    if (rc != 0) {
        return rc;
    }

    ble_hs_pvcy_set_our_irk(NULL);

    return 0;
}
