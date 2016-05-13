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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <host/ble_gap.h>
#include <host/ble_keycache.h>
#include "ble_hs_priv.h"

#define MAC_ADDR_LEN    6
#define KEYDATA_SIZE_BYTES (sizeof(struct ble_gap_key_parms))

/* FOR NOW we implement this as a simple RAM cache for testing.
 * The config module is still being invented and I don't want
 * to duplicate that work */

struct keycache_entry {
    uint8_t valid;
    uint8_t key[MAC_ADDR_LEN + 1];
    uint8_t data[KEYDATA_SIZE_BYTES];
};

/* a global key cache */
static struct keycache_entry *p_keycache;
static int keycache_entries;

int
ble_keycache_find(uint8_t addr_type, uint8_t *key_addr, struct ble_gap_key_parms *pout_entry)
{
    int i;
    for(i = 0; i < keycache_entries; i++) {
        if (p_keycache[i].valid) {
            if ((memcmp(key_addr, p_keycache[i].key, MAC_ADDR_LEN) == 0) &&
                ( p_keycache[i].key[6] == addr_type)) {
                if (pout_entry) {
                    memcpy(pout_entry, p_keycache[i].data, KEYDATA_SIZE_BYTES);
                }
                return 0;
            }
        }
    }

    return -1;
}

static int
ble_keycache_write_irk_entry(struct ble_gap_key_parms *pkeys)
{
    struct hci_add_dev_to_resolving_list add;
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_ADD_TO_RESOLV_LIST_LEN];
    int rc;

    if(!pkeys->irk_valid) {
        return -1;
    }

    /* build the message data */
    memcpy(add.addr, pkeys->addr, 6);
    memcpy(add.local_irk, ble_hs_priv_get_local_irk(), 16);
    memcpy(add.peer_irk, pkeys->irk, 16);

    rc = host_hci_cmd_add_device_to_resolving_list(&add, buf, sizeof(buf));

    if (0 == rc) {
        rc = ble_hci_cmd_tx(buf, NULL, 0, NULL);
    }

    return rc;
}

int
ble_keycache_add(uint8_t addr_type, uint8_t *key_addr, struct ble_gap_key_parms *pkeys)
{
    int i;
    int entry = -1;
    /* loop through once to find the entry. Pick the first empty one
     * or overwrite the old one */
    for(i = 0; i < keycache_entries; i++) {
        if ((!p_keycache[i].valid) && (entry == -1)) {
            entry = i;
        } else if ((memcmp(key_addr, p_keycache[i].key, MAC_ADDR_LEN) == 0) &&
                        ( p_keycache[i].key[6] == addr_type)) {
            entry = i;
        }
    }

    if(entry >= 0 ) {
        int rc;
        memcpy(p_keycache[i].data, pkeys, KEYDATA_SIZE_BYTES);
        memcpy(p_keycache[i].key, key_addr, MAC_ADDR_LEN);
        p_keycache[i].key[6] = addr_type;
        p_keycache[i].valid = 1;
        /* plumb this down to the HW. If it fails, return error,
         * and remove from this keycache .*/
         rc = ble_keycache_write_irk_entry(pkeys);

         if(rc) {
             p_keycache[i].valid = 0;
         }
         return rc;
    }
    return -1;
}

static int
ble_keycache_remove_irk_entry(uint8_t addr_type, uint8_t *addr)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_RMV_FROM_RESOLV_LIST_LEN];
    int rc;

    /* build the message data */
    rc = host_hci_cmd_remove_device_from_resolving_list(addr_type, addr,
                                                   buf, sizeof(buf));

    if (0 == rc) {
        rc = ble_hci_cmd_tx(buf, NULL, 0, NULL);
    }

    return rc;
}

 int
 ble_keycache_delete(uint8_t addr_type, uint8_t *key_addr)
{
    int i;
    for(i = 0; i < keycache_entries; i++) {
        if (p_keycache[i].valid) {
            if ((memcmp(key_addr, p_keycache[i].key, MAC_ADDR_LEN) == 0) &&
                    ( p_keycache[i].key[6] == addr_type)) {
                p_keycache[i].valid = 0;
                ble_keycache_remove_irk_entry(addr_type, key_addr);
            }
        }
    }
    return 0;
}

static int
ble_keycache_clear_irk_enties(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN ];
    int rc;

    /* build the message data */
    rc = host_hci_cmd_clear_resolving_list(buf, sizeof(buf));

    if (0 == rc) {
        rc = ble_hci_cmd_tx(buf, NULL, 0, NULL);
    }

    return rc;
}

static int
ble_keycache_set_status(int enable)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_ADDR_RESOL_ENA_LEN];
    int rc;
    rc = host_hci_cmd_set_addr_resolution_enable(enable, buf, sizeof(buf));

    if (0 == rc) {
        rc = ble_hci_cmd_tx(buf, NULL, 0, NULL);
    }

    return rc;
}

int
ble_keycache_init(int max_entries)
{
    if (p_keycache) {
        return -1;
    }

    /* TODO this should really be a flash based implementation */
    p_keycache = calloc(max_entries, sizeof(struct keycache_entry));

    if (NULL == p_keycache) {
        return -2;
    }
    keycache_entries = max_entries;

    /* clear entries in the hardware */
    ble_keycache_set_status(0);
    ble_keycache_clear_irk_enties();
    ble_keycache_set_status(1);
    return 0;
}
