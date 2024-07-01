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

#ifndef H_BTP_BAP_
#define H_BTP_BAP_

#include "nimble/ble.h"
#include <stdint.h>

#ifndef __packed
#define __packed    __attribute__((__packed__))
#endif

/* BAP Service */
/* commands */
#define BTP_BAP_READ_SUPPORTED_COMMANDS         0x01
struct btp_bap_read_supported_commands_rp {
    uint8_t data[0];
} __packed;

#define BTP_BAP_DISCOVER                        0x02
#define BTP_BAP_SEND                            0x03
#define BTP_BAP_BROADCAST_SOURCE_SETUP          0x04
struct bap_broadcast_source_setup_cmd {
    uint8_t streams_per_subgroup;
    uint8_t subgroups;
    uint8_t sdu_interval[3];
    uint8_t framing;
    uint16_t max_sdu;
    uint8_t rtn;
    uint16_t max_transport_lat;
    uint8_t presentation_delay[3];
    uint8_t coding_fmt;
    uint16_t vid;
    uint16_t cid;
    uint8_t cc_ltvs_len;
    uint8_t cc_ltvs[];
} __packed;

struct bap_broadcast_source_setup_rp {
    uint32_t gap_settings;
    uint8_t broadcast_id[3];
} __packed;

#define BTP_BAP_BROADCAST_SOURCE_RELEASE        0x05
struct bap_bap_broadcast_source_release_cmd {
    uint8_t broadcast_id[3];
} __packed;

#define BTP_BAP_BROADCAST_ADV_START             0x06
struct bap_bap_broadcast_adv_start_cmd {
    uint8_t broadcast_id[3];
} __packed;

#define BTP_BAP_BROADCAST_ADV_STOP              0x07
struct bap_bap_broadcast_adv_stop_cmd {
    uint8_t broadcast_id[3];
} __packed;

#define BTP_BAP_BROADCAST_SOURCE_START          0x08
struct bap_bap_broadcast_source_start_cmd {
    uint8_t broadcast_id[3];
} __packed;

#define BTP_BAP_BROADCAST_SOURCE_STOP           0x09
struct bap_bap_broadcast_source_stop_cmd {
    uint8_t broadcast_id[3];
} __packed;

#define BTP_BAP_BROADCAST_SINK_SETUP            0xa
struct btp_bap_broadcast_sink_setup_cmd {
} __packed;

#define BTP_BAP_BROADCAST_SINK_STOP             0x0f
struct btp_bap_broadcast_sink_stop_cmd {
    ble_addr_t address;
    uint8_t broadcast_id[3];
} __packed;

#define BTP_BAP_SET_BROADCAST_CODE              0x17
struct btp_bap_set_broadcast_code_cmd {
        ble_addr_t addr;
        uint8_t source_id;
        uint8_t broadcast_code[16];
} __packed;

#define BTP_BAP_BROADCAST_SINK_RELEASE          0x0b
#define BTP_BAP_BROADCAST_SCAN_START            0x0c
#define BTP_BAP_BROADCAST_SCAN_STOP             0x0d
#define BTP_BAP_BROADCAST_SINK_SYNC             0x0e
#define BTP_BAP_BROADCAST_SINK_BIS_SYNC         0x10
#define BTP_BAP_DISCOVER_SCAN_DELEGATOR         0x11
#define BTP_BAP_BROADCAST_ASSISTANT_SCAN_START  0x12
#define BTP_BAP_BROADCAST_ASSISTANT_SCAN_STOP   0x13
#define BTP_BAP_ADD_BROADCAST_SRC               0x14
#define BTP_BAP_REMOVE_BROADCAST_SRC            0x15
#define BTP_BAP_MODIFY_BROADCAST_SRC            0x16
#define BTP_BAP_SET_BROADCAST_CODE              0x17
#define BTP_BAP_SEND_PAST                       0x18

#define BTP_BAP_EV_DISCOVERY_COMPLETED          0x80
#define BTP_BAP_EV_CODEC_CAP_FOUND              0x81
#define BTP_BAP_EV_ASE_FOUND                    0x82
#define BTP_BAP_EV_STREAM_RECEIVED              0x83
#define BTP_BAP_EV_BAA_FOUND                    0x84
#define BTP_BAP_EV_BIS_FOUND                    0x85
#define BTP_BAP_EV_BIS_SYNCED                   0x86
#define BTP_BAP_EV_BIS_STREAM_RECEIVED          0x87
#define BTP_BAP_EV_SCAN_DELEGATOR_FOUND         0x88
#define BTP_BAP_EV_BROADCAST_RECEIVE_STATE      0x89
#define BTP_BAP_EV_PA_SYNC_REQ                  0x8a

#endif /* H_BTP_BAP_                        */
