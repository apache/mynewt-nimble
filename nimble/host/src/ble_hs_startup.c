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

#include <stddef.h>
#include <string.h>
#include "host/host_hci.h"
#include "host/ble_hs.h"
#include "ble_hs_priv.h"

static int
ble_hs_startup_le_read_sup_f_tx(void)
{
    uint8_t ack_params[BLE_HCI_RD_LOC_SUPP_FEAT_RSPLEN];
    uint8_t buf[BLE_HCI_CMD_HDR_LEN];
    uint8_t ack_params_len;
    int rc;

    host_hci_cmd_build_le_read_loc_supp_feat(buf, sizeof buf);
    rc = ble_hci_cmd_tx(buf, ack_params, sizeof ack_params, &ack_params_len);
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

    host_hci_cmd_build_le_read_buffer_size(buf, sizeof buf);
    rc = ble_hci_cmd_tx(buf, ack_params, sizeof ack_params, &ack_params_len);
    if (rc != 0) {
        return rc;
    }

    if (ack_params_len != BLE_HCI_RD_BUF_SIZE_RSPLEN) {
        return BLE_HS_ECONTROLLER;
    }

    pktlen = le16toh(ack_params + 0);
    max_pkts = ack_params[2];

    rc = host_hci_set_buf_size(pktlen, max_pkts);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_hs_startup_le_set_evmask_tx(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_LE_EVENT_MASK_LEN];
    int rc;

    /* [ Default event set ]. */
    host_hci_cmd_build_le_set_event_mask(0x000000000000021f, buf, sizeof buf);
    rc = ble_hci_cmd_tx_empty_ack(buf);
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

    /* [ Default event set | LE-meta event ]. */
    host_hci_cmd_build_set_event_mask(0x20001fffffffffff, buf, sizeof buf);
    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_hs_startup_reset_tx(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN];
    int rc;

    host_hci_cmd_build_reset(buf, sizeof buf);
    rc = ble_hci_cmd_tx_empty_ack(buf);
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
        assert(0);
        return rc;
    }

    rc = ble_hs_startup_le_set_evmask_tx();
    if (rc != 0) {
        assert(0);
        return rc;
    }

    rc = ble_hs_startup_le_read_buf_sz_tx();
    if (rc != 0) {
        assert(0);
        return rc;
    }

    /* XXX: Read buffer size. */

    rc = ble_hs_startup_le_read_sup_f_tx();
    if (rc != 0) {
        assert(0);
        return rc;
    }

    /* XXX: Read BD_ADDR. */
    ble_hs_priv_update_identity(g_dev_addr);
    ble_hs_priv_update_irk(NULL);

    return rc;
}
