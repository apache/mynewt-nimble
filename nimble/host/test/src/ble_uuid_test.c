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

#include <stddef.h>
#include <string.h>
#include "testutil/testutil.h"
#include "host/ble_hs_test.h"
#include "host/ble_uuid.h"
#include "ble_hs_test_util.h"

TEST_CASE(ble_uuid_test_128_to_16)
{
    uint16_t uuid16;

    /*** RFCOMM */
    uuid16 = ble_uuid_128_to_16(((uint8_t[]) {
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00
    }));
    TEST_ASSERT(uuid16 == 0x0003);

    /*** BNEP */
    uuid16 = ble_uuid_128_to_16(((uint8_t[]) {
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00
    }));
    TEST_ASSERT(uuid16 == 0x000f);

    /*** L2CAP */
    uuid16 = ble_uuid_128_to_16(((uint8_t[]) {
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00
    }));
    TEST_ASSERT(uuid16 == 0x0100);

    /*** ObEXObjectPush */
    uuid16 = ble_uuid_128_to_16(((uint8_t[]) {
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x05, 0x11, 0x00, 0x00
    }));
    TEST_ASSERT(uuid16 == 0x1105);

    /*** Invalid base. */
    uuid16 = ble_uuid_128_to_16(((uint8_t[]) {
        0xfb, 0x34, 0x9c, 0x5f, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00
    }));
    TEST_ASSERT(uuid16 == 0);

    /*** Invalid prefix. */
    uuid16 = ble_uuid_128_to_16(((uint8_t[]) {
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x03, 0x00, 0x00, 0x01
    }));
    TEST_ASSERT(uuid16 == 0);

    /*** 16-bit UUID of 0. */
    uuid16 = ble_uuid_128_to_16(((uint8_t[]) {
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    }));
}

TEST_SUITE(ble_uuid_test_suite)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_uuid_test_128_to_16();
}

int
ble_uuid_test_all(void)
{
    ble_uuid_test_suite();

    return tu_any_failed;
}
