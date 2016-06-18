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
#include "ble_hs_priv.h"

static int ble_hs_pvcy_initialized;
static uint8_t ble_hs_pvcy_id_addr[6];
static uint8_t ble_hs_pvcy_id_addr_type;
static uint8_t ble_hs_pvcy_nrpa[6];
uint8_t ble_hs_pvcy_irk[16];


/** Use this as a default IRK if none gets set. */
const uint8_t default_irk[16] = {
    0xef, 0x8d, 0xe2, 0x16, 0x4f, 0xec, 0x43, 0x0d,
    0xbf, 0x5b, 0xdd, 0x34, 0xc0, 0x53, 0x1e, 0xb8,
};

static int
ble_hs_pvcy_gen_static_random_addr(uint8_t *addr)
{
    int rc;

    rc = ble_hci_util_rand(addr, 6);
    /* TODO -- set bits properly */
    addr[5] |= 0xc0;

    return rc;
}

static int
ble_hs_pvcy_set_addr_timeout(uint16_t timeout)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_RESOLV_PRIV_ADDR_TO_LEN];
    int rc;

    rc = host_hci_cmd_build_set_resolv_priv_addr_timeout(
            timeout, buf, sizeof(buf));

    return rc;
}

static int
ble_hs_pvcy_set_resolve_enabled(int enable)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_ADDR_RESOL_ENA_LEN];
    int rc;

    rc = host_hci_cmd_build_set_addr_res_en(enable, buf, sizeof(buf));
    if (rc != 0) {
        return rc;
    }

    rc = ble_hci_cmd_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_hs_pvcy_remove_entry(uint8_t addr_type, uint8_t *addr)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_RMV_FROM_RESOLV_LIST_LEN];
    int rc;

    rc = host_hci_cmd_build_remove_from_resolv_list(
        addr_type, addr, buf, sizeof(buf));
    if (rc != 0) {
        return rc;
    }

    rc = ble_hci_cmd_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_hs_pvcy_clear_entries(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN ];
    int rc;

    rc = host_hci_cmd_build_clear_resolv_list(buf, sizeof(buf));
    if (rc != 0) {
        return rc;
    }

    rc = ble_hci_cmd_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_hs_pvcy_add_entry(uint8_t *addr, uint8_t addr_type, uint8_t *irk)
{
    struct hci_add_dev_to_resolving_list add;
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_ADD_TO_RESOLV_LIST_LEN];
    int rc;

    add.addr_type = addr_type;
    memcpy(add.addr, addr, 6);
    memcpy(add.local_irk, ble_hs_pvcy_our_irk(), 16);
    memcpy(add.peer_irk, irk, 16);

    rc = host_hci_cmd_build_add_to_resolv_list(&add, buf, sizeof(buf));
    if (rc != 0) {
        return rc;
    }

    rc = ble_hci_cmd_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_hs_pvcy_our_nrpa(uint8_t *addr)
{
    memcpy(addr, ble_hs_pvcy_nrpa, 6);
}

int 
ble_hs_pvcy_set_our_nrpa(void)
{
    int rc;
    uint8_t addr[6];

    rc = ble_hci_util_rand(addr, 6);
    assert(rc == 0);
    addr[5] &= ~(0xc0);
    memcpy(ble_hs_pvcy_nrpa, addr, 6);

    return ble_hs_util_set_random_addr(addr);
}

uint8_t *
ble_hs_pvcy_our_id_addr(uint8_t *type)
{
    if (!ble_hs_pvcy_initialized) {
        ble_hs_pvcy_set_our_id_addr(NULL);
    }

    if (type != NULL) {
        *type = ble_hs_pvcy_id_addr_type;
    }

    return ble_hs_pvcy_id_addr;
}

void
ble_hs_pvcy_set_our_id_addr(uint8_t *addr)
{
    uint8_t random_addr[6];
    int rc;

    if (!ble_hs_pvcy_initialized) {
        /* Set up the periodic change of our RPA. */
        rc = ble_hs_pvcy_set_addr_timeout(ble_hs_cfg.rpa_timeout);
        assert(rc == 0);
    }

    if (addr != NULL) {
        memcpy(ble_hs_pvcy_id_addr, addr, 6);
        ble_hs_pvcy_id_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
    } else {
        /* Generate a new static random address. */
        ble_hs_pvcy_gen_static_random_addr(random_addr);
        rc = ble_hs_util_set_random_addr(random_addr);
        assert(rc == 0);

        ble_hs_pvcy_id_addr_type = BLE_HCI_ADV_OWN_ADDR_RANDOM;
        memcpy(ble_hs_pvcy_id_addr, random_addr, 6);
    }

    ble_hs_pvcy_initialized = 1;
}

void
ble_hs_pvcy_set_our_irk(uint8_t *irk)
{
    uint8_t tmp_addr[6];
    uint8_t new_irk[16];

    memset(new_irk, 0, sizeof(new_irk));

    if (irk != NULL) {
        memcpy(new_irk, irk, 16);
    } else {
        memcpy(new_irk, default_irk, 16);
    }

    /* Clear the resolving list if this is a new IRK. */
    if (memcmp(ble_hs_pvcy_irk, new_irk, 16) != 0) {
        memcpy(ble_hs_pvcy_irk, new_irk, 16);

        ble_hs_pvcy_set_resolve_enabled(0);
        ble_hs_pvcy_clear_entries();
        ble_hs_pvcy_set_resolve_enabled(1);

        /* Push our identity to the controller as a keycache entry with a null
         * MAC address. The controller uses this entry to generate an RPA when
         * we do advertising with own-addr-type = rpa.
         */
        memset(tmp_addr, 0, 6);
        ble_hs_pvcy_add_entry(tmp_addr, 0, ble_hs_pvcy_irk);
    }
}

uint8_t *
ble_hs_pvcy_our_irk(void)
{
    if (!ble_hs_pvcy_initialized) {
        ble_hs_pvcy_set_our_id_addr(NULL);
    }

    return ble_hs_pvcy_irk;
}
