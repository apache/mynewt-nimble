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

#include <stddef.h>
#include <errno.h>
#include <string.h>
#include "testutil/testutil.h"
#include "host/ble_hs_test.h"
#include "ble_hs_test_util.h"

static void
ble_hs_pvcy_test_util_start_host(int num_expected_irks)
{
    int rc;
    int i;

    /* Clear our IRK.  This ensures the full startup sequence, including
     * setting the default IRK, takes place.  We need this so that we can plan
     * which HCI acks to fake.
     */
    rc = ble_hs_test_util_set_our_irk((uint8_t[16]){0}, -1, 0);
    TEST_ASSERT_FATAL(rc == 0);
    ble_hs_test_util_prev_hci_tx_clear();

    ble_hs_test_util_set_startup_acks();

    for (i = 0; i < num_expected_irks; i++) {
        ble_hs_test_util_append_ack(
            BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_ADD_RESOLV_LIST), 0); 
    }

    rc = ble_hs_start();
    TEST_ASSERT_FATAL(rc == 0);

    /* Discard startup HCI commands. */
    ble_hs_test_util_prev_tx_queue_adj(11);
}

static void
ble_hs_pvcy_test_util_add_irk(const struct ble_store_value_sec *value_sec)
{
    int rc;

    ble_hs_test_util_set_ack(
        BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_ADD_RESOLV_LIST), 0); 
    ble_hs_test_util_append_ack(
        BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_PRIVACY_MODE), 0);

    rc = ble_store_write_peer_sec(value_sec);
    TEST_ASSERT_FATAL(rc == 0);

    ble_hs_test_util_verify_tx_add_irk(value_sec->peer_addr.type,
                                       value_sec->peer_addr.val,
                                       value_sec->irk,
                                       ble_hs_pvcy_default_irk);

    ble_hs_test_util_verify_tx_set_priv_mode(value_sec->peer_addr.type,
                                             value_sec->peer_addr.val,
                                             BLE_GAP_PRIVATE_MODE_DEVICE);
}

TEST_CASE(ble_hs_pvcy_test_case_restore_irks)
{
    struct ble_store_value_sec value_sec1;
    struct ble_store_value_sec value_sec2;

    ble_hs_test_util_init();

    /*** No persisted IRKs. */
    ble_hs_pvcy_test_util_start_host(0);

    /*** One persisted IRK. */

    /* Persist IRK; ensure it automatically gets added to the list. */
    value_sec1 = (struct ble_store_value_sec) {
        .peer_addr = { BLE_ADDR_PUBLIC, { 1, 2, 3, 4, 5, 6 } },
        .key_size = 16,
        .ediv = 1,
        .rand_num = 2,
        .irk = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 },
        .irk_present = 1,
    };
    ble_hs_pvcy_test_util_add_irk(&value_sec1);

    /* Ensure it gets added to list on startup. */
    ble_hs_pvcy_test_util_start_host(1);
    ble_hs_test_util_verify_tx_add_irk(value_sec1.peer_addr.type,
                                       value_sec1.peer_addr.val,
                                       value_sec1.irk,
                                       ble_hs_pvcy_default_irk);

    /* Two persisted IRKs. */
    value_sec2 = (struct ble_store_value_sec) {
        .peer_addr = { BLE_ADDR_PUBLIC, { 2, 3, 4, 5, 6, 7 } },
        .key_size = 16,
        .ediv = 12,
        .rand_num = 20,
        .irk = { 4, 4, 4, 4, 5, 5, 5, 6, 6, 6, 9, 9, 9, 9, 9, 10 },
        .irk_present = 1,
    };
    ble_hs_pvcy_test_util_add_irk(&value_sec2);

    /* Ensure both get added to list on startup. */
    ble_hs_pvcy_test_util_start_host(2);
    ble_hs_test_util_verify_tx_add_irk(value_sec1.peer_addr.type,
                                       value_sec1.peer_addr.val,
                                       value_sec1.irk,
                                       ble_hs_pvcy_default_irk);
    ble_hs_test_util_verify_tx_add_irk(value_sec2.peer_addr.type,
                                       value_sec2.peer_addr.val,
                                       value_sec2.irk,
                                       ble_hs_pvcy_default_irk);
}

TEST_SUITE(ble_hs_pvcy_test_suite_irk)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_hs_pvcy_test_case_restore_irks();
}

int
ble_hs_pvcy_test_all(void)
{
    ble_hs_pvcy_test_suite_irk();

    return tu_any_failed;
}
