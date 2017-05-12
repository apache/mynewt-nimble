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

#include "testutil/testutil.h"
#include "host/ble_hs_test.h"
#include "ble_hs_test_util.h"

static void
ble_store_test_util_verify_peer_deleted(const ble_addr_t *addr)
{
    union ble_store_value value;
    union ble_store_key key;
    ble_addr_t addrs[64];
    int num_addrs;
    int rc;
    int i;

    memset(&key, 0, sizeof key);
    key.sec.peer_addr = *addr;
    rc = ble_store_read(BLE_STORE_OBJ_TYPE_OUR_SEC, &key, &value);
    TEST_ASSERT(rc == BLE_HS_ENOENT);
    rc = ble_store_read(BLE_STORE_OBJ_TYPE_PEER_SEC, &key, &value);
    TEST_ASSERT(rc == BLE_HS_ENOENT);

    memset(&key, 0, sizeof key);
    key.cccd.peer_addr = *addr;
    rc = ble_store_read(BLE_STORE_OBJ_TYPE_CCCD, &key, &value);
    TEST_ASSERT(rc == BLE_HS_ENOENT);

    rc = ble_store_util_bonded_peers(addrs, &num_addrs,
                                     sizeof addrs / sizeof addrs[0]);
    TEST_ASSERT_FATAL(rc == 0);
    for (i = 0; i < num_addrs; i++) {
        TEST_ASSERT(ble_addr_cmp(addr, addrs + i) != 0);
    }
}

TEST_CASE(ble_store_test_peers)
{
    struct ble_store_value_sec secs[4] = {
        {
            .peer_addr = { BLE_ADDR_PUBLIC,     { 1, 2, 3, 4, 5, 6 } },
            .ltk_present = 1,
        },
        {
            /* Address value is a duplicate of above, but type differs. */
            .peer_addr = { BLE_ADDR_RANDOM,     { 1, 2, 3, 4, 5, 6 } },
            .ltk_present = 1,
        },
        {
            .peer_addr = { BLE_ADDR_PUBLIC,     { 2, 3, 4, 5, 6, 7 } },
            .ltk_present = 1,
        },
        {
            .peer_addr = { BLE_ADDR_RANDOM,     { 3, 4, 5, 6, 7, 8 } },
            .ltk_present = 1,
        },
    };
    ble_addr_t peer_addrs[4];
    int num_addrs;
    int rc;
    int i;

    for (i = 0; i < sizeof secs / sizeof secs[0]; i++) {
        rc = ble_store_write_our_sec(secs + i);
        TEST_ASSERT_FATAL(rc == 0);
        rc = ble_store_write_peer_sec(secs + i);
        TEST_ASSERT_FATAL(rc == 0);
    }

    rc = ble_store_util_bonded_peers(peer_addrs, &num_addrs,
                                     sizeof peer_addrs / sizeof peer_addrs[0]);
    TEST_ASSERT_FATAL(rc == 0);

    TEST_ASSERT(num_addrs == sizeof secs / sizeof secs[0]);
    for (i = 0; i < num_addrs; i++) {
        TEST_ASSERT(ble_addr_cmp(&peer_addrs[i], &secs[i].peer_addr) == 0);
    }
}

TEST_CASE(ble_store_test_delete_peer)
{
    struct ble_store_value_sec secs[2] = {
        {
            .peer_addr = { BLE_ADDR_PUBLIC,     { 1, 2, 3, 4, 5, 6 } },
            .ltk_present = 1,
        },
        {
            /* Address value is a duplicate of above, but type differs. */
            .peer_addr = { BLE_ADDR_RANDOM,     { 1, 2, 3, 4, 5, 6 } },
            .ltk_present = 1,
        },
    };
    struct ble_store_value_cccd cccds[3] = {
        /* First two belong to first peer. */
        {
            .peer_addr = secs[0].peer_addr,
            .chr_val_handle = 5,
        },
        {
            .peer_addr = secs[0].peer_addr,
            .chr_val_handle = 8,
        },

        /* Last belongs to second peer. */
        {
            .peer_addr = secs[1].peer_addr,
            .chr_val_handle = 5,
        },
    };

    int rc;
    int i;

    for (i = 0; i < sizeof secs / sizeof secs[0]; i++) {
        rc = ble_store_write_our_sec(secs + i);
        TEST_ASSERT_FATAL(rc == 0);
        rc = ble_store_write_peer_sec(secs + i);
        TEST_ASSERT_FATAL(rc == 0);
    }

    for (i = 0; i < sizeof cccds / sizeof cccds[0]; i++) {
        rc = ble_store_write_cccd(cccds + i);
        TEST_ASSERT_FATAL(rc == 0);
    }

    /* Delete first peer. */
    rc = ble_store_util_delete_peer(&secs[0].peer_addr);
    TEST_ASSERT_FATAL(rc == 0);

    /* Ensure all traces of first peer have been removed. */
    ble_store_test_util_verify_peer_deleted(&secs[0].peer_addr);

    /* Delete second peer. */
    rc = ble_store_util_delete_peer(&secs[1].peer_addr);
    TEST_ASSERT_FATAL(rc == 0);

    /* Ensure all traces of first peer have been removed. */
    ble_store_test_util_verify_peer_deleted(&secs[1].peer_addr);
}

TEST_SUITE(ble_store_suite)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_store_test_peers();
    ble_store_test_delete_peer();
}

int
ble_store_test_all(void)
{
    ble_store_suite();

    return tu_any_failed;
}
