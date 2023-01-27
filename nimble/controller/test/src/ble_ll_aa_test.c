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
#include <controller/ble_ll_utils.h>
#include <testutil/testutil.h>

TEST_CASE_SELF(ble_ll_aa_test_1)
{
    uint32_t seed_aa;
    uint32_t aa;

    seed_aa = 0x78e52493;

    /* BIG Control */
    aa = ble_ll_utils_calc_big_aa(seed_aa, 0);
    TEST_ASSERT(aa == 0x7a412493);

    /* BISes */
    aa = ble_ll_utils_calc_big_aa(seed_aa, 1);
    TEST_ASSERT(aa == 0x85e32493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 2);
    TEST_ASSERT(aa == 0x79d52493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 3);
    TEST_ASSERT(aa == 0x86752493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 4);
    TEST_ASSERT(aa == 0x7a572493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 5);
    TEST_ASSERT(aa == 0x85f12493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 6);
    TEST_ASSERT(aa == 0x79d32493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 7);
    TEST_ASSERT(aa == 0x86732493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 8);
    TEST_ASSERT(aa == 0x7b652493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 9);
    TEST_ASSERT(aa == 0x85c72493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 10);
    TEST_ASSERT(aa == 0x78e12493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 11);
    TEST_ASSERT(aa == 0x86412493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 12);
    TEST_ASSERT(aa == 0x7b632493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 13);
    TEST_ASSERT(aa == 0x85d52493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 14);
    TEST_ASSERT(aa == 0x78f72493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 15);
    TEST_ASSERT(aa == 0x86572493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 16);
    TEST_ASSERT(aa == 0x7b712493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 17);
    TEST_ASSERT(aa == 0x85d32493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 18);
    TEST_ASSERT(aa == 0x78c52493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 19);
    TEST_ASSERT(aa == 0x87652493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 20);
    TEST_ASSERT(aa == 0x7b472493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 21);
    TEST_ASSERT(aa == 0x84e12493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 22);
    TEST_ASSERT(aa == 0x78c32493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 23);
    TEST_ASSERT(aa == 0x87632493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 24);
    TEST_ASSERT(aa == 0x7b552493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 25);
    TEST_ASSERT(aa == 0x84f72493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 26);
    TEST_ASSERT(aa == 0x78d12493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 27);
    TEST_ASSERT(aa == 0x87712493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 28);
    TEST_ASSERT(aa == 0x7b532493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 29);
    TEST_ASSERT(aa == 0x84c52493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 30);
    TEST_ASSERT(aa == 0x79e72493);
    aa = ble_ll_utils_calc_big_aa(seed_aa, 31);
    TEST_ASSERT(aa == 0x87472493);
}

TEST_SUITE(ble_ll_aa_test_suite)
{
    ble_ll_aa_test_1();
}
