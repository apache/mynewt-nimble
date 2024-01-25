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

#ifndef H_BLE_AUDIO_BROADCAST_SINK_PRIV_
#define H_BLE_AUDIO_BROADCAST_SINK_PRIV_

#include <stdint.h>
#include "audio/ble_audio.h"
#include "audio/ble_audio_broadcast_sink.h"
#include "audio/ble_audio_scan_delegator.h"

int ble_audio_broadcast_sink_config(
        uint8_t source_id, uint16_t conn_handle,
        const struct ble_audio_scan_delegator_sync_opt *sync_opt);
void ble_audio_broadcast_sink_code_set(
        uint8_t source_id, const uint8_t broadcast_code[16]);

#endif /* H_BLE_AUDIO_BROADCAST_SINK_PRIV_ */
