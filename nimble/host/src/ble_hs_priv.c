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
#include <host/ble_keycache.h>
#include <stdio.h>
#include <string.h>
#include "ble_hs_priv.h"

int identity_initalized;
static uint8_t identity_addr[6];
static uint8_t identity_addr_type;
static uint8_t irk[16];

/* use this as a default IRK if there is none stored in the
 * keycache. */
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


void
ble_hs_priv_init_identity(uint8_t *addr) {

    struct ble_gap_key_parms parms;
    int rc;
    uint8_t random_addr[6];

    ble_hs_generate_static_random_addr(random_addr);
    rc = ble_hs_util_set_random_addr(random_addr);
    assert(rc == 0);

    if(identity_initalized) {
        return;
    }

    /* did we get passed an address at initialization */
    if(addr) {
        memcpy(identity_addr, addr, 6);
        identity_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
    } else if (0 )
    {
        /* if that fails, try to get a static random address from nvram */
        /* TODO */
    } else
    {
        identity_addr_type = BLE_HCI_ADV_OWN_ADDR_RANDOM;
        memcpy(identity_addr, random_addr, 6);
        /* TODO store it */
    }

    /* now try to get an IRK from internal nvram */
    if(0) {
        /* TODO */
    } else {
        /* if that fails, use the default IRK */
        memcpy(irk, default_irk, 16);
    }

    identity_initalized = 1;

    /* write a zero peer addr and addr type */
    memset(parms.irk, 0, 16);
    parms.irk_valid = 1;
    memset(parms.addr, 0, 6);
    parms.addr_valid = 1;
    parms.addr_type = 0;
    rc = ble_keycache_add(identity_addr_type, identity_addr, &parms);
    assert(rc == 0);

    /* set up the periodic change of our NRA */
    rc = ble_hs_priv_set_addr_to(ble_hs_cfg.privacy_resolvable_addr_timeout);
    assert(rc == 0);
}

uint8_t *
bls_hs_priv_get_local_identity_addr(uint8_t *type) {

    if(!identity_initalized) {
        ble_hs_priv_init_identity(NULL);
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
        ble_hs_priv_init_identity(NULL);
    }
    return irk;
}
