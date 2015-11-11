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

#include <stddef.h>
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "ble_hs_conn.h"
#include "ble_hs_ack.h"

typedef int ble_hs_ack_rx_cmd_complete_fn(uint16_t ocf, uint8_t *params,
                                          int param_len);

struct ble_hs_ack_cmd_complete_dispatch_entry {
    uint8_t hac_ocf;
    ble_hs_ack_rx_cmd_complete_fn *hac_fn;
};

static const struct ble_hs_ack_cmd_complete_dispatch_entry
        ble_hs_ack_cmd_complete_dispatch[] = {
    { 0, NULL }, /* XXX */
};

#define BLE_HS_ACK_CMD_COMPLETE_DISPATCH_SZ         \
    (sizeof ble_hs_ack_cmd_complete_dispatch /      \
     sizeof ble_hs_ack_cmd_complete_dispatch[0])

typedef int ble_hs_ack_rx_cmd_status_fn(uint16_t ocf, uint8_t status);

struct ble_hs_ack_cmd_status_dispatch_entry {
    uint8_t hat_ocf;
    ble_hs_ack_rx_cmd_status_fn *hat_fn;
};

static const struct ble_hs_ack_cmd_status_dispatch_entry
        ble_hs_ack_cmd_status_dispatch[] = {
    { BLE_HCI_OCF_LE_CREATE_CONN, ble_hs_conn_rx_cmd_status_create_conn },
};

#define BLE_HS_ACK_CMD_STATUS_DISPATCH_SZ       \
    (sizeof ble_hs_ack_cmd_status_dispatch /    \
     sizeof ble_hs_ack_cmd_status_dispatch[0])

static const struct ble_hs_ack_cmd_complete_dispatch_entry *
ble_hs_cmd_complete_find_entry(uint16_t ocf)
{
    const struct ble_hs_ack_cmd_complete_dispatch_entry *entry;
    int i;

    for (i = 0; i < 0/*XXX BLE_HS_ACK_CMD_COMPLETE_DISPATCH_SZ*/; i++) {
        entry = ble_hs_ack_cmd_complete_dispatch + i;
        if (entry->hac_ocf == ocf) {
            return entry;
        }
    }

    return NULL;
}

static const struct ble_hs_ack_cmd_status_dispatch_entry *
ble_hs_cmd_status_find_entry(uint16_t ocf)
{
    const struct ble_hs_ack_cmd_status_dispatch_entry *entry;
    int i;

    for (i = 0; i < BLE_HS_ACK_CMD_STATUS_DISPATCH_SZ; i++) {
        entry = ble_hs_ack_cmd_status_dispatch + i;
        if (entry->hat_ocf == ocf) {
            return entry;
        }
    }

    return NULL;
}

int
ble_hs_ack_rx_cmd_complete(uint16_t ocf, uint8_t *params, int param_len)
{
    const struct ble_hs_ack_cmd_complete_dispatch_entry *entry;
    int rc;

    entry = ble_hs_cmd_complete_find_entry(ocf);
    if (entry != NULL) {
        rc = entry->hac_fn(ocf, params, param_len);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

int
ble_hs_ack_rx_cmd_status(uint16_t ocf, uint8_t status)
{
    const struct ble_hs_ack_cmd_status_dispatch_entry *entry;
    int rc;

    entry = ble_hs_cmd_status_find_entry(ocf);
    if (entry != NULL) {
        rc = entry->hat_fn(ocf, status);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}
