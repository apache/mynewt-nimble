/*
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

#include <stdint.h>
#include <syscfg/syscfg.h>
#include <controller/ble_ll.h>
#include <controller/ble_ll_addr.h>

static struct {
    uint8_t public_addr[BLE_DEV_ADDR_LEN];
    uint8_t random_addr[BLE_DEV_ADDR_LEN];
    uint8_t public_addr_hci[BLE_DEV_ADDR_LEN];
} g_ble_ll_addr;

static bool
is_addr_empty(const uint8_t *addr)
{
    return memcmp(addr, BLE_ADDR_ANY, BLE_DEV_ADDR_LEN) == 0;
}

int
ble_ll_addr_public_set(const uint8_t *addr)
{
    memcpy(g_ble_ll_addr.public_addr_hci, addr, BLE_DEV_ADDR_LEN);

    return 0;
}

const uint8_t *
ble_ll_addr_get(uint8_t addr_type)
{
    const uint8_t *addr;

    if (addr_type) {
        addr = g_ble_ll_addr.random_addr;
    } else {
        addr = g_ble_ll_addr.public_addr;
    }

    return addr;
}

const uint8_t *
ble_ll_addr_public_get(void)
{
    return g_ble_ll_addr.public_addr;
}


const uint8_t *
ble_ll_addr_random_get(void)
{
    return g_ble_ll_addr.random_addr;
}

int
ble_ll_addr_random_set(const uint8_t *addr)
{
    memcpy(g_ble_ll_addr.random_addr, addr, BLE_DEV_ADDR_LEN);

    return 0;
}

bool
ble_ll_addr_is_our(int addr_type, const uint8_t *addr)
{
    const uint8_t *our_addr;

    our_addr = ble_ll_addr_get(addr_type);

    return memcmp(our_addr, addr, BLE_DEV_ADDR_LEN) == 0;
}

bool
ble_ll_addr_is_valid_own_addr_type(uint8_t addr_type,
                                   const uint8_t *random_addr)
{
    const uint8_t *addr;

    switch (addr_type) {
    case BLE_HCI_ADV_OWN_ADDR_PUBLIC:
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    case BLE_HCI_ADV_OWN_ADDR_PRIV_PUB:
#endif
        addr = ble_ll_addr_public_get();
        break;
    case BLE_HCI_ADV_OWN_ADDR_RANDOM:
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    case BLE_HCI_ADV_OWN_ADDR_PRIV_RAND:
#endif
        addr = random_addr ? random_addr : ble_ll_addr_random_get();
        break;
    default:
        return false;
    }

    return !is_addr_empty(addr);
}

int
ble_ll_addr_init(void)
{
    uint64_t pub_dev_addr;
    int i;

    /* Set public device address from syscfg. It should be all-zero in normal
     * build so no need to add special check for that.
     */
    pub_dev_addr = MYNEWT_VAL(BLE_LL_PUBLIC_DEV_ADDR);
    for (i = 0; i < BLE_DEV_ADDR_LEN; i++) {
        g_ble_ll_addr.public_addr[i] = pub_dev_addr & 0xff;
        pub_dev_addr >>= 8;
    }

    if (!is_addr_empty(g_ble_ll_addr.public_addr_hci)) {
        /* Use public address set externally */
        memcpy(g_ble_ll_addr.public_addr, g_ble_ll_addr.public_addr_hci, BLE_DEV_ADDR_LEN);
    } else {
        /* Set public address from provider API, if available */
#if MYNEWT_API_ble_addr_provider_public
        ble_ll_addr_provide_public(g_ble_ll_addr.public_addr);
#endif
    }

#if MYNEWT_VAL(BLE_LL_ADDR_INIT_RANDOM)
    /* Set random address from provider API, if available */
#if MYNEWT_API_ble_addr_provider_random
    ble_ll_addr_provide_static(g_ble_ll_addr.random_addr);
#endif
#endif

    return 0;
}
