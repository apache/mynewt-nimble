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

#include "sysinit/sysinit.h"
#include "testutil/testutil.h"

TEST_SUITE_DECL(ble_audio_base_parse_test_suite);
TEST_CASE_DECL(ble_audio_listener_register_test);

TEST_SUITE(ble_audio_test)
{
    ble_audio_base_parse_test_suite();
    ble_audio_listener_register_test();
}

int
main(int argc, char **argv)
{
    sysinit();

    ble_audio_test();

    return tu_any_failed;
}
