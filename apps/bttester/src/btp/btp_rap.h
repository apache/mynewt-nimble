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

#ifndef H_BTP_RAP_
#define H_BTP_RAP_

#include "nimble/ble.h"
#include <stdint.h>

#ifndef __packed
#define __packed    __attribute__((__packed__))
#endif

/* RAP Service */
/* commands */
#define BTP_RAP_READ_SUPPORTED_COMMANDS 0x01
struct btp_rap_read_supported_commands_rp {
    uint8_t data[0];
} __packed;

#define BTP_RAP_SET_TEST_METHOD 0x02
struct btp_rap_set_test_method_cmd {
    ble_addr_t address;
    uint8_t test_method;
} __packed;

#define BTP_RAP_START_RANGING 0x03
struct btp_rap_start_ranging_cmd {
    ble_addr_t address;
    uint8_t flags;
    uint8_t local_role;
} __packed;

#define BTP_RAP_STOP_RANGING 0x04
struct btp_rap_stop_ranging_cmd {
    ble_addr_t address;
} __packed;

#define BTP_RAP_SET_TEST_CS_SUBEVENT_DATA 0x05
struct btp_rap_set_test_cs_subevent_data_cmd {
    ble_addr_t address;
    uint8_t data_len;
    uint8_t data[0];
} __packed;

/* RAP events */
#define BTP_RAP_EV_RANGING_COMPLETE 0x80
struct btp_rap_ranging_complete_ev {
    ble_addr_t address;
    uint8_t status;
} __packed;

#endif /* H_BTP_RAP_                        */
