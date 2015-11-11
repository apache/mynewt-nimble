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
#include <string.h>
#include <errno.h>
#include "os/os.h"
#include "host/host_hci.h"
#include "ble_hs_conn.h"
#include "ble_gap_conn.h"

/**
 * Initiates a connection using the GAP Direct Connection Establishment
 * Procedure.
 *
 * @return 0 on success; nonzero on failure.
 */
int
ble_gap_conn_initiate_direct(int addr_type, uint8_t *addr)
{
    struct hci_create_conn hcc;
    int rc;

    /* Make sure no connection attempt is already in progress. */
    if (ble_hs_conn_pending()) {
        return EALREADY;
    }

    hcc.scan_itvl = 0x0010;
    hcc.scan_window = 0x0010;
    hcc.filter_policy = BLE_HCI_CONN_FILT_NO_WL;
    hcc.peer_addr_type = addr_type;
    memcpy(hcc.peer_addr, addr, sizeof hcc.peer_addr);
    hcc.own_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
    hcc.conn_itvl_min = 24;
    hcc.conn_itvl_max = 40;
    hcc.conn_latency = 0;
    hcc.supervision_timeout = 0x0100; // XXX
    hcc.min_ce_len = 0x0010; // XXX
    hcc.min_ce_len = 0x0300; // XXX

    rc = ble_hs_conn_initiate(&hcc);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
