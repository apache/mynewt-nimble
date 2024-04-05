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

#ifndef BTP_PACS_H
#define BTP_PACS_H

#include <stdint.h>

#ifndef __packed
#define __packed    __attribute__((__packed__))
#endif

#define BTP_PACS_READ_SUPPORTED_COMMANDS        0x01
struct btp_pacs_read_supported_commands_rp {
    uint8_t data[0];
} __packed;

#define BTP_PACS_UPDATE_CHARACTERISTIC          0x02
struct btp_pacs_update_characteristic_cmd {
    uint8_t char_id;
} __packed;

#define BTP_PACS_SET_LOCATION                   0x03

#define BTP_PACS_SET_AVAILABLE_CONTEXTS         0x04
struct btp_pacs_set_available_contexts_cmd {
    uint16_t sink_contexts;
    uint16_t source_contexts;
} __packed;

#define BTP_PACS_SET_SUPPORTED_CONTEXTS         0x05
struct btp_pacs_set_supported_contexts_cmd {
    uint16_t sink_contexts;
    uint16_t source_contexts;
} __packed;

#define BTP_PACS_CHARACTERISTIC_SINK_PAC                        0x01
#define BTP_PACS_CHARACTERISTIC_SOURCE_PAC                      0x02
#define BTP_PACS_CHARACTERISTIC_SINK_AUDIO_LOCATIONS            0x03
#define BTP_PACS_CHARACTERISTIC_SOURCE_AUDIO_LOCATIONS          0x04
#define BTP_PACS_CHARACTERISTIC_AVAILABLE_AUDIO_CONTEXTS        0x05

#endif /* BTP_PACS_H*/
