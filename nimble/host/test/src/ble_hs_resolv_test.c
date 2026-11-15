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

#include <string.h>
#include "testutil/testutil.h"
#include "ble_hs_test.h"
#include "ble_hs_test_util.h"

#if BLE_HS_RESOLV_ENABLED

/* Core Specification Vol 3, Part H, Appendix D.7: IRK
 * ec0234a357c8ad05341010a60a397d9b, prand 708194, hash 0dfbaa; stored least
 * significant octet first.
 */
static const uint8_t ble_hs_resolv_test_irk[16] = {
    0x9b, 0x7d, 0x39, 0x0a, 0xa6, 0x10, 0x10, 0x34,
    0x05, 0xad, 0xc8, 0x57, 0xa3, 0x34, 0x02, 0xec
};

/* RPA 70:81:94:0D:FB:AA */
static const uint8_t ble_hs_resolv_test_rpa[6] = {
    0xaa, 0xfb, 0x0d, 0x94, 0x81, 0x70
};

static const ble_addr_t ble_hs_resolv_test_id = {
    BLE_ADDR_PUBLIC, { 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 }
};

static void
ble_hs_resolv_test_util_rpa(ble_addr_t *addr)
{
    addr->type = BLE_ADDR_RANDOM;
    memcpy(addr->val, ble_hs_resolv_test_rpa, 6);
}

TEST_CASE_SELF(ble_hs_resolv_test_case_ah)
{
    uint8_t hash[3];

    TEST_ASSERT_FATAL(ble_sm_alg_ah(ble_hs_resolv_test_irk,
                                    ble_hs_resolv_test_rpa + 3, hash) == 0);
    TEST_ASSERT(memcmp(hash, ble_hs_resolv_test_rpa, 3) == 0);
}

TEST_CASE_SELF(ble_hs_resolv_test_case_addr)
{
    ble_addr_t addr;
    ble_addr_t rpa;
    ble_addr_t id;

    ble_hs_resolv_clear();

    /* Empty list: address is not changed. */
    ble_hs_resolv_test_util_rpa(&addr);
    TEST_ASSERT(ble_hs_resolv_addr(&addr) == 0);
    TEST_ASSERT(addr.type == BLE_ADDR_RANDOM);

    TEST_ASSERT_FATAL(ble_hs_resolv_add(&ble_hs_resolv_test_id,
                                        ble_hs_resolv_test_irk) == 0);
    TEST_ASSERT(ble_hs_resolv_peer_rpa(&ble_hs_resolv_test_id,
                                       &rpa) == BLE_HS_ENOENT);

    /* RPA is reported as identity address and remembered. */
    TEST_ASSERT(ble_hs_resolv_addr(&addr) == 1);
    TEST_ASSERT(addr.type == BLE_ADDR_PUBLIC_ID);
    TEST_ASSERT(memcmp(addr.val, ble_hs_resolv_test_id.val, 6) == 0);
    TEST_ASSERT(ble_hs_resolv_peer_rpa(&ble_hs_resolv_test_id, &rpa) == 0);
    TEST_ASSERT(rpa.type == BLE_ADDR_RANDOM);
    TEST_ASSERT(memcmp(rpa.val, ble_hs_resolv_test_rpa, 6) == 0);

    /* Identity address type can be given as resolved type as well. */
    id = ble_hs_resolv_test_id;
    id.type = BLE_ADDR_PUBLIC_ID;
    TEST_ASSERT(ble_hs_resolv_peer_rpa(&id, &rpa) == 0);

    /* RPA of another peer and static random address are not changed. */
    ble_hs_resolv_test_util_rpa(&addr);
    addr.val[0] ^= 1;
    TEST_ASSERT(ble_hs_resolv_addr(&addr) == 0);
    TEST_ASSERT(addr.type == BLE_ADDR_RANDOM);
    addr = (ble_addr_t){ BLE_ADDR_RANDOM, { 1, 2, 3, 4, 5, 0xc6 } };
    TEST_ASSERT(ble_hs_resolv_addr(&addr) == 0);
    TEST_ASSERT(addr.type == BLE_ADDR_RANDOM);

    /* Peer seen with its identity address: no RPA to connect to. */
    addr = ble_hs_resolv_test_id;
    TEST_ASSERT(ble_hs_resolv_addr(&addr) == 0);
    TEST_ASSERT(addr.type == BLE_ADDR_PUBLIC);
    TEST_ASSERT(ble_hs_resolv_peer_rpa(&ble_hs_resolv_test_id,
                                       &rpa) == BLE_HS_ENOENT);
}

TEST_CASE_SELF(ble_hs_resolv_test_case_conn_complete)
{
    struct ble_gap_conn_complete evt;

    ble_hs_resolv_clear();
    TEST_ASSERT_FATAL(ble_hs_resolv_add(&ble_hs_resolv_test_id,
                                        ble_hs_resolv_test_irk) == 0);

    memset(&evt, 0, sizeof(evt));
    evt.peer_addr_type = BLE_ADDR_RANDOM;
    memcpy(evt.peer_addr, ble_hs_resolv_test_rpa, 6);
    ble_hs_resolv_conn_complete(&evt);
    TEST_ASSERT(evt.peer_addr_type == BLE_ADDR_PUBLIC_ID);
    TEST_ASSERT(memcmp(evt.peer_addr, ble_hs_resolv_test_id.val, 6) == 0);
    TEST_ASSERT(memcmp(evt.peer_rpa, ble_hs_resolv_test_rpa, 6) == 0);

    /* Public peer address is not changed. */
    memset(&evt, 0, sizeof(evt));
    evt.peer_addr_type = BLE_ADDR_PUBLIC;
    memcpy(evt.peer_addr, ble_hs_resolv_test_rpa, 6);
    ble_hs_resolv_conn_complete(&evt);
    TEST_ASSERT(evt.peer_addr_type == BLE_ADDR_PUBLIC);
}

TEST_CASE_SELF(ble_hs_resolv_test_case_list)
{
    static const uint8_t zero_irk[16];
    ble_addr_t addr;
    ble_addr_t id;
    int i;

    ble_hs_resolv_clear();

    /* Adding again replaces the entry. */
    TEST_ASSERT(ble_hs_resolv_add(&ble_hs_resolv_test_id,
                                  ble_hs_resolv_test_irk) == 0);
    TEST_ASSERT(ble_hs_resolv_add(&ble_hs_resolv_test_id,
                                  ble_hs_resolv_test_irk) == 0);
    TEST_ASSERT(ble_hs_resolv_remove(&ble_hs_resolv_test_id) == 0);
    TEST_ASSERT(ble_hs_resolv_remove(&ble_hs_resolv_test_id) == BLE_HS_ENOENT);

    /* Removed peer is not resolved. */
    ble_hs_resolv_test_util_rpa(&addr);
    TEST_ASSERT(ble_hs_resolv_addr(&addr) == 0);

    /* Peer without IRK is not added. */
    TEST_ASSERT(ble_hs_resolv_add(&ble_hs_resolv_test_id, zero_irk) == 0);
    TEST_ASSERT(ble_hs_resolv_remove(&ble_hs_resolv_test_id) == BLE_HS_ENOENT);

    /* List full. */
    id = (ble_addr_t){ BLE_ADDR_PUBLIC, { 0 } };
    for (i = 0; i < MYNEWT_VAL(BLE_HOST_BASED_PRIVACY_LIST_SIZE); i++) {
        id.val[0] = i;
        TEST_ASSERT(ble_hs_resolv_add(&id, ble_hs_resolv_test_irk) == 0);
    }
    id.val[0] = 0xff;
    TEST_ASSERT(ble_hs_resolv_add(&id, ble_hs_resolv_test_irk) ==
                BLE_HS_ENOMEM);

    ble_hs_resolv_clear();
    TEST_ASSERT(ble_hs_resolv_remove(&id) == BLE_HS_ENOENT);
}

TEST_SUITE(ble_hs_resolv_test_suite)
{
    ble_hs_resolv_test_case_ah();
    ble_hs_resolv_test_case_addr();
    ble_hs_resolv_test_case_conn_complete();
    ble_hs_resolv_test_case_list();
}

#endif
