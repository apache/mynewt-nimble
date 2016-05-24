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

#include <string.h>
#include "testutil/testutil.h"
#include "nimble/ble.h"
#include "ble_hs_test_util.h"
#include "ble_hs_test_util_store.h"

static int ble_hs_test_util_store_max_our_ltks;
static int ble_hs_test_util_store_max_peer_ltks;
static int ble_hs_test_util_store_max_cccds;

static struct ble_store_value_ltk *ble_hs_test_util_store_our_ltks;
static struct ble_store_value_ltk *ble_hs_test_util_store_peer_ltks;
static struct ble_store_value_cccd *ble_hs_test_util_store_cccds;
int ble_hs_test_util_store_num_our_ltks;
int ble_hs_test_util_store_num_peer_ltks;
int ble_hs_test_util_store_num_cccds;


#define BLE_HS_TEST_UTIL_STORE_WRITE_GEN(store, num_vals, max_vals, \
                                         val, idx) do               \
{                                                                   \
    if ((idx) == -1) {                                              \
        if ((num_vals) >= (max_vals)) {                             \
            return BLE_HS_ENOMEM;                                   \
        }                                                           \
        store[(num_vals)] = (val);                                  \
        (num_vals)++;                                               \
    } else {                                                        \
        store[(idx)] = val;                                         \
    }                                                               \
    return 0;                                                       \
} while (0) 

void
ble_hs_test_util_store_init(int max_our_ltks, int max_peer_ltks, int max_cccds)
{
    free(ble_hs_test_util_store_our_ltks);
    free(ble_hs_test_util_store_peer_ltks);
    free(ble_hs_test_util_store_cccds);

    ble_hs_test_util_store_our_ltks = malloc(
        ble_hs_test_util_store_max_our_ltks *
        sizeof *ble_hs_test_util_store_our_ltks);
    TEST_ASSERT_FATAL(ble_hs_test_util_store_our_ltks != NULL);

    ble_hs_test_util_store_peer_ltks = malloc(
        ble_hs_test_util_store_max_peer_ltks *
        sizeof *ble_hs_test_util_store_peer_ltks);
    TEST_ASSERT_FATAL(ble_hs_test_util_store_peer_ltks != NULL);

    ble_hs_test_util_store_cccds = malloc(
        ble_hs_test_util_store_max_cccds *
        sizeof *ble_hs_test_util_store_cccds);
    TEST_ASSERT_FATAL(ble_hs_test_util_store_cccds != NULL);

    ble_hs_test_util_store_max_our_ltks = max_our_ltks;
    ble_hs_test_util_store_max_peer_ltks = max_peer_ltks;
    ble_hs_test_util_store_max_cccds = max_cccds;
    ble_hs_test_util_store_num_our_ltks = 0;
    ble_hs_test_util_store_num_peer_ltks = 0;
    ble_hs_test_util_store_num_cccds = 0;
}

static int
ble_hs_test_util_store_read_ltk(struct ble_store_value_ltk *store,
                                int num_values,
                                struct ble_store_key_ltk *key,
                                struct ble_store_value_ltk *value)
{
    struct ble_store_value_ltk *ltk;
    int i;

    for (i = 0; i < num_values; i++) {
        ltk = store + i;
        if (ltk->ediv == key->ediv && ltk->rand_num == key->rand_num) {
            *value = *ltk;
            return 0;
        }
    }

    return BLE_HS_ENOENT;
}

static int
ble_hs_test_util_store_find_cccd(struct ble_store_key_cccd *key)
{
    struct ble_store_value_cccd *cccd;
    int skipped;
    int i;

    skipped = 0;
    for (i = 0; i < ble_hs_test_util_store_num_cccds; i++) {
        cccd = ble_hs_test_util_store_cccds + i;

        if (key->peer_addr_type != BLE_STORE_PEER_ADDR_TYPE_NONE) {
            if (cccd->peer_addr_type != key->peer_addr_type) {
                continue;
            }

            if (memcmp(cccd->peer_addr, key->peer_addr, 6) != 0) {
                continue;
            }
        }

        if (key->chr_val_handle != 0) {
            if (cccd->chr_val_handle != key->chr_val_handle) {
                continue;
            }
        }

        if (key->idx > skipped) {
            skipped++;
            continue;
        }

        return i;
    }

    return -1;
}

static int
ble_hs_test_util_store_read_cccd(struct ble_store_key_cccd *key,
                                 struct ble_store_value_cccd *value)
{
    int idx;

    idx = ble_hs_test_util_store_find_cccd(key);
    if (idx == -1) {
        return BLE_HS_ENOENT;
    }

    *value = ble_hs_test_util_store_cccds[idx];
    return 0;
}

int
ble_hs_test_util_store_read(int obj_type, union ble_store_key *key,
                            union ble_store_value *dst)
{
    switch (obj_type) {
    case BLE_STORE_OBJ_TYPE_OUR_LTK:
        return ble_hs_test_util_store_read_ltk(
            ble_hs_test_util_store_our_ltks,
            ble_hs_test_util_store_num_our_ltks,
            &key->ltk,
            &dst->ltk);

    case BLE_STORE_OBJ_TYPE_PEER_LTK:
        return ble_hs_test_util_store_read_ltk(
            ble_hs_test_util_store_peer_ltks,
            ble_hs_test_util_store_num_peer_ltks,
            &key->ltk,
            &dst->ltk);

    case BLE_STORE_OBJ_TYPE_CCCD:
        return ble_hs_test_util_store_read_cccd(&key->cccd, &dst->cccd);

    default:
        TEST_ASSERT_FATAL(0);
        return BLE_HS_EUNKNOWN;
    }
}

int
ble_hs_test_util_store_write(int obj_type, union ble_store_value *value)
{
    struct ble_store_key_cccd key_cccd;
    int idx;

    switch (obj_type) {
    case BLE_STORE_OBJ_TYPE_OUR_LTK:
        BLE_HS_TEST_UTIL_STORE_WRITE_GEN(
            ble_hs_test_util_store_our_ltks,
            ble_hs_test_util_store_num_our_ltks,
            ble_hs_test_util_store_max_our_ltks,
            value->ltk, -1);

    case BLE_STORE_OBJ_TYPE_PEER_LTK:
        BLE_HS_TEST_UTIL_STORE_WRITE_GEN(
            ble_hs_test_util_store_peer_ltks,
            ble_hs_test_util_store_num_peer_ltks,
            ble_hs_test_util_store_max_peer_ltks,
            value->ltk, -1);

    case BLE_STORE_OBJ_TYPE_CCCD:
        ble_store_key_from_value_cccd(&key_cccd, &value->cccd);
        idx = ble_hs_test_util_store_find_cccd(&key_cccd);
        BLE_HS_TEST_UTIL_STORE_WRITE_GEN(
            ble_hs_test_util_store_cccds,
            ble_hs_test_util_store_num_cccds,
            ble_hs_test_util_store_max_cccds,
            value->cccd, idx);

    default:
        TEST_ASSERT_FATAL(0);
        return BLE_HS_EUNKNOWN;
    }

    return 0;
}
