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
#include <string.h>
#include "testutil/testutil.h"
#include "ble_hs_test.h"
#include "host/ble_uuid.h"
#include "ble_hs_test_util.h"

#define DEBUG 0

#if DEBUG
#define DEBUG_PRINT(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...)
#endif

#define TEST_UUID_128_1_STR "0329a25a-c4c4-4923-8039-4bacfa18eb72"
#define TEST_UUID_128_1                                 \
    0x72, 0xeb, 0x18, 0xfa, 0xac, 0x4b, 0x39, 0x80,     \
    0x23, 0x49, 0xc4, 0xc4, 0x5a, 0xa2, 0x29, 0x03

#define TEST_UUID_128_2_STR "e3ec4966df944981a772985ff8d75dff"
#define TEST_UUID_128_2                                 \
    0xff, 0x5d, 0xd7, 0xf8, 0x5f, 0x98, 0x72, 0xa7,     \
    0x81, 0x49, 0x94, 0xdf, 0x66, 0x49, 0xec, 0xe3

#define TEST_UUID_128_3_STR "00002a37-0000-1000-8000-00805f9b34fb"
#define TEST_UUID_128_3                                 \
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,     \
    0x00, 0x10, 0x00, 0x00, 0x37, 0x2a, 0x00, 0x00
#define TEST_UUID_128_3_AS_16 ((uint16_t) 0x2a37)

#define TEST_32UUID1_STR "0x0329a25a"
#define TEST_32UUID1     ((uint32_t)0x0329a25a)

#define TEST_32UUID2_STR "e3ec4966"
#define TEST_32UUID2     ((uint32_t)0xe3ec4966)

#define TEST_16UUID1_STR "0x0329"
#define TEST_16UUID1     ((uint16_t)0x0329)

#define TEST_16UUID2_STR "e3ec"
#define TEST_16UUID2     ((uint16_t)0xe3ec)

static const ble_uuid128_t test_uuid128[4] = {
    BLE_UUID128_INIT(TEST_UUID_128_1),
    BLE_UUID128_INIT(TEST_UUID_128_2),
    BLE_UUID128_INIT(TEST_UUID_128_3)
};

static const char test_str128[][100] = {
    TEST_UUID_128_1_STR,
    TEST_UUID_128_2_STR,
    TEST_UUID_128_3_STR
};

static const ble_uuid32_t test_uuid32[3] = {
    BLE_UUID32_INIT(TEST_32UUID1),
    BLE_UUID32_INIT(TEST_32UUID2),
};

static const char test_str32[][100] = {
    TEST_32UUID1_STR,
    TEST_32UUID2_STR,
};

static const ble_uuid16_t test_uuid16[3] = {
    BLE_UUID16_INIT(TEST_16UUID1),
    BLE_UUID16_INIT(TEST_16UUID2),
};

static const char test_str16[][100] = {
    TEST_16UUID1_STR,
    TEST_16UUID2_STR,
};

TEST_CASE_SELF(ble_uuid_test) {
    uint8_t buf_16[2] = { 0x00, 0x18 };
    uint8_t buf_128[16] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                            0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };

    const ble_uuid_t *uuid16_1 = BLE_UUID16_DECLARE(0x1800);
    const ble_uuid_t *uuid16_2 = BLE_UUID16_DECLARE(0x1801);

    const ble_uuid_t *uuid128_1 =
        BLE_UUID128_DECLARE(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                            0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF);
    const ble_uuid_t *uuid128_2 =
        BLE_UUID128_DECLARE(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xEE);

    ble_uuid_any_t uuid;
    int rc;

    rc = ble_uuid_init_from_buf(&uuid, buf_16, 2);
    TEST_ASSERT(rc == 0);

    rc = ble_uuid_cmp(&uuid.u, uuid16_1);
    TEST_ASSERT(rc == 0);

    rc = ble_uuid_cmp(&uuid.u, uuid16_2);
    TEST_ASSERT(rc != 0);

    rc = ble_uuid_cmp(uuid16_1, uuid16_2);
    TEST_ASSERT(rc != 0);

    rc = ble_uuid_init_from_buf(&uuid, buf_128, 16);
    TEST_ASSERT(rc == 0);

    rc = ble_uuid_cmp(&uuid.u, uuid128_1);
    TEST_ASSERT(rc == 0);

    rc = ble_uuid_cmp(&uuid.u, uuid128_2);
    TEST_ASSERT(rc != 0);

    rc = ble_uuid_cmp(uuid128_1, uuid128_2);
    TEST_ASSERT(rc != 0);

    ble_hs_test_util_assert_mbufs_freed(NULL);
}

TEST_CASE_SELF(ble_uuid_from_string_test) {
    int rc;
    ble_uuid_any_t uuid128[3];
    ble_uuid_any_t uuid32[3];
    ble_uuid_any_t uuid16[3];

    for (int i = 0; i < 2; i++) {
        DEBUG_PRINT("Test 128bit: %d\n", i);
        ble_uuid_from_str((ble_uuid_any_t * ) &uuid128[i], test_str128[i]);
        DEBUG_PRINT("Original:\n");
        for (int j = 0; j < 16; j++) {
            DEBUG_PRINT("%2x, ", test_uuid128[i].value[j]);
        }
        DEBUG_PRINT("\nConverted:\n");
        for (int j = 0; j < 16; j++) {
            DEBUG_PRINT("%2x, ", uuid128[i].u128.value[j]);
        }
        DEBUG_PRINT("\n");
        DEBUG_PRINT("The same: %s\n",
                    ble_uuid_cmp((const ble_uuid_t *) &uuid128[i].u128,
                                 (const ble_uuid_t *) &test_uuid128[i]) == 0
                    ? "true" : "false");
        rc = ble_uuid_cmp((const ble_uuid_t *) &uuid128[i].u128,
                          (const ble_uuid_t *) &test_uuid128[i]);
        TEST_ASSERT(rc == 0);
    }

    DEBUG_PRINT("\n");
    DEBUG_PRINT("\n");

    for (int i = 0; i < 2; i++) {
        DEBUG_PRINT("Test 32bit: %d\n", i);
        ble_uuid_from_str((ble_uuid_any_t * ) &uuid32[i], test_str32[i]);
        DEBUG_PRINT("Original:\n");
        DEBUG_PRINT("%x, ", test_uuid32[i].value);
        DEBUG_PRINT("\nConverted:\n");
        DEBUG_PRINT("%x, ", uuid32[i].u32.value);
        DEBUG_PRINT("\n");
        DEBUG_PRINT("The same: %s\n",
                    ble_uuid_cmp((const ble_uuid_t *) &uuid32[i].u32,
                                 (const ble_uuid_t *) &test_uuid32[i]) == 0
                    ? "true" : "false");
        rc = ble_uuid_cmp((const ble_uuid_t *) &uuid32[i].u32,
                          (const ble_uuid_t *) &test_uuid32[i]);
        TEST_ASSERT(rc == 0);
    }

    DEBUG_PRINT("\n");
    DEBUG_PRINT("\n");

    for (int i = 0; i < 2; i++) {
        DEBUG_PRINT("Test 16bit: %d\n", i);
        ble_uuid_from_str((ble_uuid_any_t * ) &uuid16[i], test_str16[i]);
        DEBUG_PRINT("Original:\n");
        DEBUG_PRINT("%x, ", test_uuid16[i].value);
        DEBUG_PRINT("\nConverted:\n");
        DEBUG_PRINT("%x, ", uuid16[i].u16.value);
        DEBUG_PRINT("\n");
        DEBUG_PRINT("The same: %s\n",
                    ble_uuid_cmp((const ble_uuid_t *) &uuid16[i].u16,
                                 (const ble_uuid_t *) &test_uuid16[i]) == 0
                    ? "true" : "false");
        rc = ble_uuid_cmp((const ble_uuid_t *) &uuid16[i].u16,
                          (const ble_uuid_t *) &test_uuid16[i]);
        TEST_ASSERT(rc == 0);
    }

    ble_hs_test_util_assert_mbufs_freed(NULL);
}

TEST_CASE_SELF(ble_uuid_from_string_sig_test) {
    int rc;
    ble_uuid_any_t uuid;

    ble_uuid16_t test_uuid = BLE_UUID16_INIT(TEST_UUID_128_3_AS_16);

    DEBUG_PRINT("Test %d\n", 0);
    ble_uuid_from_str(&uuid, test_str128[2]);
    DEBUG_PRINT("Original:\n");
    for (int j = 0; j < 16; j++) {
        DEBUG_PRINT("%2x, ", test_uuid128[2].value[j]);
    }
    DEBUG_PRINT("\nConverted:\n");
    DEBUG_PRINT("%x\n", uuid.u16.value);
    DEBUG_PRINT("The same: %s\n",
                ble_uuid_cmp((const ble_uuid_t *) &uuid.u16,
                             (const ble_uuid_t *) &test_uuid) == 0 ? "true"
                                                                   : "false");
    rc = ble_uuid_cmp((const ble_uuid_t *)&uuid.u16,
                      (const ble_uuid_t *)&test_uuid);
    TEST_ASSERT(rc == 0);

    ble_hs_test_util_assert_mbufs_freed(NULL);
}

TEST_CASE_SELF(ble_uuid_to_str_and_back_test) {
    int rc;
    ble_uuid128_t uuid = BLE_UUID128_INIT(TEST_UUID_128_1);
    ble_uuid_any_t final_uuid;
    char uuid_str[100];

    ble_uuid_to_str((const ble_uuid_t *)&uuid, uuid_str);

    DEBUG_PRINT("String: %s\n", uuid_str);

    rc = ble_uuid_from_str(&final_uuid, (const char *)uuid_str);
    TEST_ASSERT(rc == 0);

    rc = ble_uuid_cmp((const ble_uuid_t *)&uuid, (const ble_uuid_t *)&final_uuid.u128);
    TEST_ASSERT(rc == 0);

    ble_hs_test_util_assert_mbufs_freed(NULL);
}

TEST_CASE_SELF(ble_uuid_to_str_too_short) {
    int rc;
    ble_uuid_any_t final_uuid;
    char uuid_str[4] = "012";

    DEBUG_PRINT("String: %s\n", uuid_str);

    rc = ble_uuid_from_str(&final_uuid, (const char *)uuid_str);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    ble_hs_test_util_assert_mbufs_freed(NULL);
}

TEST_CASE_SELF(ble_uuid_to_str_sig_to_16_and_32) {
    int rc;
    ble_uuid_any_t final_uuid;
    char uuid_str16[37] = "0000ffff-0000-1000-8000-00805f9b34fb";
    char uuid_str32[37] = "ffffffff-0000-1000-8000-00805f9b34fb";

    DEBUG_PRINT("String16: %s\n", uuid_str16);
    DEBUG_PRINT("String32: %s\n", uuid_str32);

    rc = ble_uuid_from_str(&final_uuid, (const char *)uuid_str16);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(final_uuid.u.type == BLE_UUID_TYPE_16);
    TEST_ASSERT(final_uuid.u16.value == UINT16_MAX);
    memset(&final_uuid, 0, sizeof(ble_uuid_any_t));

    rc = ble_uuid_from_str(&final_uuid, (const char *)uuid_str32);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(final_uuid.u.type == BLE_UUID_TYPE_32);
    TEST_ASSERT(final_uuid.u32.value == UINT32_MAX);

    ble_hs_test_util_assert_mbufs_freed(NULL);
}

TEST_SUITE(ble_uuid_test_suite) {
    ble_uuid_test();
    ble_uuid_from_string_test();
    ble_uuid_from_string_sig_test();
    ble_uuid_to_str_and_back_test();
    ble_uuid_to_str_too_short();
    ble_uuid_to_str_sig_to_16_and_32();
}
