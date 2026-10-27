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

#include "host/ble_hs.h"
#include "audio/ble_audio_scan_delegator.h"
#include "../../../src/ble_audio_broadcast_sink_priv.h"

TEST_CASE_SELF(ble_audio_broadcast_sink_config_test_subgroups_bound)
{
    struct ble_audio_scan_delegator_sync_opt sync_opt = { 0 };
    int rc;

    sync_opt.pa_sync = BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_PAST_NOT_AVAILABLE;
    sync_opt.num_subgroups = BLE_AUDIO_SCAN_DELEGATOR_SUBGROUP_MAX + 1;

    rc = ble_audio_broadcast_sink_config(0, BLE_HS_CONN_HANDLE_NONE, &sync_opt);

    TEST_ASSERT(rc == BLE_HS_EINVAL);
}
