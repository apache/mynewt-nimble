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
#include "ble_gap_conn.h"
#include "ble_hs_conn.h"
#include "ble_hs_ack.h"

typedef int ble_hs_ack_rx_fn(struct ble_hs_ack *ack);

struct ble_hs_ack_dispatch_entry {
    uint8_t bhe_ocf;
    ble_hs_ack_rx_fn *bhe_fn;
};

static const struct ble_hs_ack_dispatch_entry ble_hs_ack_dispatch[] = {
    { BLE_HCI_OCF_LE_CREATE_CONN, ble_gap_conn_rx_ack_create_conn },
    { BLE_HCI_OCF_LE_SET_ADV_PARAMS, ble_gap_conn_rx_ack_set_adv_params },
};

#define BLE_HS_ACK_DISPATCH_SZ  \
    (sizeof ble_hs_ack_dispatch / sizeof ble_hs_ack_dispatch[0])

static const struct ble_hs_ack_dispatch_entry *
ble_hs_ack_find_dispatch_entry(uint16_t ocf)
{
    const struct ble_hs_ack_dispatch_entry *entry;
    int i;

    for (i = 0; i < BLE_HS_ACK_DISPATCH_SZ; i++) {
        entry = ble_hs_ack_dispatch + i;
        if (entry->bhe_ocf == ocf) {
            return entry;
        }
    }

    return NULL;
}

int
ble_hs_ack_rx(struct ble_hs_ack *ack)
{
    const struct ble_hs_ack_dispatch_entry *entry;
    int rc;

    entry = ble_hs_ack_find_dispatch_entry(ack->bha_ocf);
    if (entry != NULL) {
        rc = entry->bhe_fn(ack);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}
