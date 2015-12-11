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

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "ble_gap_conn.h"
#include "ble_hs_conn.h"
#include "ble_hci_ack.h"

static ble_hci_ack_fn *ble_hci_ack_cb;
static void *ble_hci_ack_arg;

void
ble_hci_ack_rx(struct ble_hci_ack *ack)
{
    ble_hci_ack_fn *cb;

    if (ble_hci_ack_cb != NULL) {
        cb = ble_hci_ack_cb;
        ble_hci_ack_cb = NULL;

        cb(ack, ble_hci_ack_arg);
    }
}

void
ble_hci_ack_set_callback(ble_hci_ack_fn *cb, void *arg)
{
    /* Don't allow the current callback to be replaced with another. */
    assert(ble_hci_ack_cb == NULL || cb == NULL);

    ble_hci_ack_cb = cb;
    ble_hci_ack_arg = arg;
}

void
ble_hci_ack_init(void)
{
    ble_hci_ack_set_callback(NULL, NULL);
}
