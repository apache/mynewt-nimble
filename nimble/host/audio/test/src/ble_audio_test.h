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
#ifndef _BLE_AUDIO_BSNK_TEST_H
#define _BLE_AUDIO_BSNK_TEST_H

#include <stdio.h>
#include <string.h>

#include "os/mynewt.h"
#include "testutil/testutil.h"

#include "host/ble_hs.h"
#include "host/audio/ble_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_AUDIO_TEST_ADDR_VALID               (&ble_audio_test_addr)
#define BLE_AUDIO_TEST_ADDR_INVALID             NULL
#define BLE_AUDIO_TEST_ADV_SID_VALID            0x0F
#define BLE_AUDIO_TEST_ADV_SID_INVALID          0x10
#define BLE_AUDIO_TEST_BROADCAST_ID_VALID       0x000000
#define BLE_AUDIO_TEST_BROADCAST_ID_INVALID     (BLE_AUDIO_BROADCAST_ID_MASK + 1)
#define BLE_AUDIO_TEST_BROADCAST_CODE_VALID     (&ble_audio_test_broadcast_code[0])
#define BLE_AUDIO_TEST_BROADCAST_CODE_INVALID   (&ble_audio_test_broadcast_code_invalid[0])
#define BLE_AUDIO_TEST_PA_INTERVAL_VALID        0x0006
#define BLE_AUDIO_TEST_PA_INTERVAL_INVALID      0x0005

extern const ble_addr_t ble_audio_test_addr;
extern const char ble_audio_test_broadcast_code[];
extern const char ble_audio_test_broadcast_code_invalid[];

#ifdef __cplusplus
}
#endif
#endif /* _BLE_AUDIO_BSNK_TEST_H */
