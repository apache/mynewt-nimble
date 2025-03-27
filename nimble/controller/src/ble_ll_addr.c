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

/* FIXME: both should be static and accessible only via dedicated set/get APIs;
 *        extern declared in nimble/ble.h should be removed
 */
uint8_t g_dev_addr[BLE_DEV_ADDR_LEN];
uint8_t g_random_addr[BLE_DEV_ADDR_LEN];

static uint8_t g_dev_addr_hci[BLE_DEV_ADDR_LEN];

static bool
is_addr_empty(const uint8_t *addr)
{
    return memcmp(addr, BLE_ADDR_ANY, BLE_DEV_ADDR_LEN) == 0;
}

int
ble_ll_addr_public_set(const uint8_t *addr)
{
    memcpy(g_dev_addr_hci, addr, BLE_DEV_ADDR_LEN);

    return 0;
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
        g_dev_addr[i] = pub_dev_addr & 0xff;
        pub_dev_addr >>= 8;
    }

    if (!is_addr_empty(g_dev_addr_hci)) {
        /* Use public address set externally */
        memcpy(g_dev_addr, g_dev_addr_hci, BLE_DEV_ADDR_LEN);
    } else {
        /* Set public address from provider API, if available */
#if MYNEWT_API_ble_addr_provider_public
        ble_ll_addr_provide_public(g_dev_addr);
#endif
    }

#if MYNEWT_VAL(BLE_LL_ADDR_INIT_RANDOM)
    /* Set random address from provider API, if available */
#if MYNEWT_API_ble_addr_provider_random
    ble_ll_addr_provide_static(g_random_addr);
#endif
#endif

    return 0;
}
