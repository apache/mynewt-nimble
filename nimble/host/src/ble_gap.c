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
#include <errno.h>
#include <string.h>
#include "ble_hs_work.h"
#include "ble_gap.h"

/**
 * Performs the Direct Connection Establishment Procedure, as described in
 * vol. 3, part C, section 9.3.8.
 *
 * @param addr_type             The peer's address type; one of:
 *                                  o BLE_HCI_ADV_PEER_ADDR_PUBLIC
 *                                  o BLE_HCI_ADV_PEER_ADDR_RANDOM
 *                                  o BLE_HCI_ADV_PEER_ADDR_PUBLIC_IDENT
 *                                  o BLE_HCI_ADV_PEER_ADDR_RANDOM_IDENT
 * @param addr                  The address of the peer to connect to.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gap_direct_connection_establishment(uint8_t addr_type, uint8_t *addr)
{
    struct ble_hs_work_entry *entry;

    entry = ble_hs_work_entry_alloc();
    if (entry == NULL) {
        return ENOMEM;
    }

    entry->bwe_type = BLE_HS_WORK_TYPE_DIRECT_CONNECT;
    entry->bwe_direct_connect.bwdc_peer_addr_type = addr_type;
    memcpy(entry->bwe_direct_connect.bwdc_peer_addr, addr,
           sizeof entry->bwe_direct_connect.bwdc_peer_addr);

    ble_hs_work_enqueue(entry);

    return 0;
}
