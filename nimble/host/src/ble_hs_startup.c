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
#include "host/ble_hs_hci.h"
#include "ble_hs_priv.h"

static int
ble_hs_startup_le_read_sup_f_tx(void)
{
    uint8_t ack_params[BLE_HCI_RD_LOC_SUPP_FEAT_RSPLEN];
    uint8_t ack_params_len;
    uint32_t feat;
    int rc;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_RD_LOC_SUPP_FEAT),
                           NULL,0, ack_params, sizeof ack_params,
                           &ack_params_len);
    if (rc != 0) {
        return rc;
    }

    if (ack_params_len != BLE_HCI_RD_LOC_SUPP_FEAT_RSPLEN) {
        return BLE_HS_ECONTROLLER;
    }

    /* For now 32-bits of features is enough */
    feat = get_le32(ack_params);
    ble_hs_hci_set_le_supported_feat(feat);

    return 0;
}

static int
ble_hs_startup_le_read_buf_sz_tx(uint16_t *out_pktlen, uint8_t *out_max_pkts)
{
    uint8_t ack_params[BLE_HCI_RD_BUF_SIZE_RSPLEN];
    uint8_t ack_params_len;
    int rc;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_RD_BUF_SIZE), NULL, 0,
                           ack_params, sizeof ack_params, &ack_params_len);
    if (rc != 0) {
        return rc;
    }

    if (ack_params_len != BLE_HCI_RD_BUF_SIZE_RSPLEN) {
        return BLE_HS_ECONTROLLER;
    }

    *out_pktlen = get_le16(ack_params + 0);
    *out_max_pkts = ack_params[2];

    return 0;
}

static int
ble_hs_startup_read_buf_sz_tx(uint16_t *out_pktlen, uint16_t *out_max_pkts)
{
    uint8_t ack_params[BLE_HCI_IP_RD_BUF_SIZE_RSPLEN];
    uint8_t ack_params_len;
    int rc;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_INFO_PARAMS,
                                      BLE_HCI_OCF_IP_RD_BUF_SIZE), NULL, 0,
                           ack_params, sizeof ack_params, &ack_params_len);
    if (rc != 0) {
        return rc;
    }

    if (ack_params_len != BLE_HCI_IP_RD_BUF_SIZE_RSPLEN) {
        return BLE_HS_ECONTROLLER;
    }

    *out_pktlen = get_le16(ack_params + 0);
    *out_max_pkts = get_le16(ack_params + 3);

    return 0;
}

static int
ble_hs_startup_read_buf_sz(void)
{
    uint16_t le_pktlen;
    uint16_t max_pkts;
    uint16_t pktlen;
    uint8_t le_max_pkts;
    int rc;

    rc = ble_hs_startup_le_read_buf_sz_tx(&le_pktlen, &le_max_pkts);
    if (rc != 0) {
        return rc;
    }

    if (le_pktlen != 0) {
        pktlen = le_pktlen;
        max_pkts = le_max_pkts;   
    } else {
        rc = ble_hs_startup_read_buf_sz_tx(&pktlen, &max_pkts);
        if (rc != 0) {
            return rc;
        }
    }

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
    uint8_t ack_params_len;
    int rc;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_INFO_PARAMS,
                                      BLE_HCI_OCF_IP_RD_BD_ADDR),
                           NULL, 0, ack_params, sizeof ack_params,
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
    uint8_t buf[BLE_HCI_SET_LE_EVENT_MASK_LEN];
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
     *     0x0000000000000400 LE Directed Advertising Report Event
     *     0x0000000000000800 LE PHY Update Complete Event
     *     0x0000000000001000 LE Extended Advertising Report Event
     *     0x0000000000010000 LE Extended Scan Timeout Event
     *     0x0000000000020000 LE Extended Advertising Set Terminated Event
     *     0x0000000000040000 LE Scan Request Received Event
     *     0x0000000000080000 LE Channel Selection Algorithm Event
     */
    ble_hs_hci_cmd_build_le_set_event_mask(0x00000000000F1A7F, buf, sizeof buf);
    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                                BLE_HCI_OCF_LE_SET_EVENT_MASK),
                                     buf, sizeof(buf));
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_hs_startup_set_evmask_tx(void)
{
    uint8_t buf[BLE_HCI_SET_EVENT_MASK_LEN];
    int rc;

    /**
     * Enable the following events:
     *     0x0000000000000010 Disconnection Complete Event
     *     0x0000000000000080 Encryption Change Event
     *     0x0000000000008000 Hardware Error Event
     *     0x0000000002000000 Data Buffer Overflow Event
     *     0x2000000000000000 LE Meta-Event
     */
    ble_hs_hci_cmd_build_set_event_mask(0x2000000002008090, buf, sizeof buf);
    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_CTLR_BASEBAND,
                                                BLE_HCI_OCF_CB_SET_EVENT_MASK),
                                     buf, sizeof(buf));
    if (rc != 0) {
        return rc;
    }

    /**
     * Enable the following events:
     *     0x0000000000800000 Authenticated Payload Timeout Event
     */
    ble_hs_hci_cmd_build_set_event_mask2(0x0000000000800000, buf, sizeof buf);
    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_CTLR_BASEBAND,
                                                BLE_HCI_OCF_CB_SET_EVENT_MASK2),
                                     buf, sizeof(buf));
    if (rc != 0) {
        BLE_HS_LOG(WARN, "ble_hs_startup_set_evmask_tx() failed\n");
    }

    return 0;
}

static int
ble_hs_startup_reset_tx(void)
{
    int rc;

    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_CTLR_BASEBAND,
                                                BLE_HCI_OCF_CB_RESET),
                                     NULL, 0);
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

    rc = ble_hs_startup_read_buf_sz();
    if (rc != 0) {
        return rc;
    }

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
