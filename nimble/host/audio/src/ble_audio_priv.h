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

#ifndef H_BLE_AUDIO_PRIV_
#define H_BLE_AUDIO_PRIV_

#include "audio/ble_audio.h"

#define MIN(_a, _b) ((_a) < (_b) ? (_a) : (_b))
#define MAX(_a, _b) ((_a) > (_b) ? (_a) : (_b))

#if MYNEWT_VAL(BLE_HS_DEBUG)
#define BLE_AUDIO_DBG_ASSERT(x) assert(x)
#define BLE_AUDIO_DBG_ASSERT_EVAL(x) assert(x)
#else
#define BLE_AUDIO_DBG_ASSERT(x)
#define BLE_AUDIO_DBG_ASSERT_EVAL(x) ((void)(x))
#endif

int ble_audio_event_listener_call(struct ble_audio_event *event);

#endif /* H_BLE_AUDIO_PRIV_ */
