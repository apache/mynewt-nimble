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

#include <syscfg/syscfg.h>
#include <testutil/testutil.h>

#if MYNEWT_VAL(SELFTEST)

TEST_SUITE_DECL(ble_ll_aa_test_suite);
TEST_SUITE_DECL(ble_ll_crypto_test_suite);
TEST_SUITE_DECL(ble_ll_csa2_test_suite);
TEST_SUITE_DECL(ble_ll_isoal_test_suite);
TEST_SUITE_DECL(ble_ll_iso_test_suite);
TEST_SUITE_DECL(ble_ll_cs_drbg_test_suite);

int
main(int argc, char **argv)
{
    ble_ll_aa_test_suite();
    ble_ll_crypto_test_suite();
    ble_ll_csa2_test_suite();
    ble_ll_isoal_test_suite();
    ble_ll_iso_test_suite();
    ble_ll_cs_drbg_test_suite();

    return tu_any_failed;
}

#endif
