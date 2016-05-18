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

int identity_initalized;
static uint8_t identity_addr[6];
static uint8_t identity_addr_type;

uint8_t g_irk[16];


/* use this as a default IRK if there is none stored in the ble_store */
const uint8_t default_irk[16] = {
    0xef, 0x8d, 0xe2, 0x16, 0x4f, 0xec, 0x43, 0x0d,
    0xbf, 0x5b, 0xdd, 0x34, 0xc0, 0x53, 0x1e, 0xb8,
};

static int
ble_hs_generate_static_random_addr(uint8_t *addr) {
    int rc;
    rc = ble_hci_util_rand(addr, 6);
    /* TODO -- set bits properly */
    addr[5] |= 0xc0;
    return rc;
}

int ble_hs_priv_set_nrpa(void) {
    int rc;
    uint8_t addr[6];
    rc = ble_hci_util_rand(addr, 6);
    assert(rc == 0);
    addr[5] &= ~(0xc0);
    return ble_hs_util_set_random_addr(addr);
}

int
ble_hs_priv_get_identity_addr_type(uint8_t *addr_type) {
    if(identity_initalized) {
        return -1;
    }
    *addr_type = identity_addr_type;
    return 0;
}

static int
ble_hs_priv_set_addr_to(uint16_t timeout) {
    int rc;
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_RESOLV_PRIV_ADDR_TO_LEN];
    rc = host_hci_cmd_set_resolvable_private_address_timeout(
            timeout, buf, sizeof(buf));
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


int
ble_keycache_write_irk_entry(uint8_t *addr, uint8_t addr_type, uint8_t *irk)
{
    struct hci_add_dev_to_resolving_list add;
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_ADD_TO_RESOLV_LIST_LEN];
    int rc;

    /* build the message data */
    add.addr_type = addr_type;
    memcpy(add.addr, addr, 6);
    memcpy(add.local_irk, ble_hs_priv_get_local_irk(), 16);
    memcpy(add.peer_irk, irk, 16);

    rc = host_hci_cmd_add_device_to_resolving_list(&add, buf, sizeof(buf));

    if (0 == rc) {
        rc = ble_hci_cmd_tx(buf, NULL, 0, NULL);
    }

    return rc;
}


void
ble_hs_priv_update_identity(uint8_t *addr) {

    int rc;
    uint8_t random_addr[6];
    int first_init = (identity_initalized == 0);

    /* only do this stuff once */
    if(first_init) {
        /* set up the periodic change of our NRA */
        rc = ble_hs_priv_set_addr_to(ble_hs_cfg.privacy_resolvable_addr_timeout);
        assert(rc == 0);
    }

    /* did we get passed an address */
    if(addr) {
        memcpy(identity_addr, addr, 6);
        identity_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
    } else if (0)
    {
        /* if that fails, try to get a static random address from nvram */
        /* TODO */
    } else
    {
        /* generate a new static random address and store to memory */
        ble_hs_generate_static_random_addr(random_addr);
        rc = ble_hs_util_set_random_addr(random_addr);
        assert(rc == 0);

        identity_addr_type = BLE_HCI_ADV_OWN_ADDR_RANDOM;
        memcpy(identity_addr, random_addr, 6);
        /* TODO store it */
    }

    identity_initalized = 1;
}

void
ble_hs_priv_update_irk(uint8_t *irk)
{
    uint8_t tmp_addr[6];
    int irk_changed;
    uint8_t new_irk[16];

    memset(new_irk, 0, sizeof(new_irk));

    /* now try to get an IRK from internal nvram */
    if (irk) {
        memcpy(new_irk, irk, 16);
    } else if(0) {
        /* TODO call to application internal nvram */
    } else {
        /* if that fails, use the default IRK */
        memcpy(new_irk, default_irk, 16);
    }

    irk_changed = memcmp(g_irk, new_irk, 16);

    /* if we are changing our IRK, we need to clear this. Also, for
     * first time, clear this  */
    if (irk_changed) {
        memcpy(g_irk, new_irk, 16);

        /* we need to clear this cache since all the other entries used
         * our IRK */
        ble_keycache_set_status(0);
        ble_keycache_clear_irk_enties();
        ble_keycache_set_status(1);
        /* push our identity to the controller as a keycache entry.with a zero
         * MAC address. The controller uses this entry to generate a RPA when
         * we do advertising with own addr type = rpa */
        memset(tmp_addr, 0, 6);
        ble_keycache_write_irk_entry(tmp_addr, 0 ,g_irk);
    }
}

uint8_t *
bls_hs_priv_get_local_identity_addr(uint8_t *type) {

    if(!identity_initalized) {
        ble_hs_priv_update_identity(NULL);
    }

    if (type) {
        *type = identity_addr_type;
    }
    return identity_addr;
}

void
bls_hs_priv_copy_local_identity_addr(uint8_t *pdst, uint8_t* addr_type) {
    uint8_t *addr = bls_hs_priv_get_local_identity_addr(addr_type);
    memcpy(pdst,addr,6);
}

uint8_t *
ble_hs_priv_get_local_irk(void)
{
    if(!identity_initalized) {
        ble_hs_priv_update_identity(NULL);
    }
    return g_irk;
}
