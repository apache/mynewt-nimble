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

/* btp_bass.c - Bluetooth Broadcast Audio Stream Service Tester */

#include "syscfg/syscfg.h"
#include <stdint.h>

#if MYNEWT_VAL(BLE_AUDIO)

#include "btp/btp_bass.h"


#include "btp/btp.h"
#include "console/console.h"

#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "math.h"

#include "audio/ble_audio_broadcast_source.h"
#include "services/bass/ble_audio_svc_bass.h"
#include "audio/ble_audio.h"
#include "host/ble_iso.h"

#include "bsp/bsp.h"

static uint8_t
supported_commands(const void *cmd, uint16_t cmd_len,
                   void *rsp, uint16_t *rsp_len)
{
    return BTP_STATUS_SUCCESS;
}

static const struct btp_handler handlers[] = {
    {
        .opcode = BTP_BASS_READ_SUPPORTED_COMMANDS,
        .index = BTP_INDEX_NONE,
        .expect_len = 0,
        .func = supported_commands,
    },
};

uint8_t
tester_init_bass(void)
{
    tester_register_command_handlers(BTP_SERVICE_ID_BASS, handlers,
                                     ARRAY_SIZE(handlers));

    return BTP_STATUS_SUCCESS;
}

uint8_t
tester_unregister_bass(void)
{
    return BTP_STATUS_SUCCESS;
}

#endif /* MYNEWT_VAL(BLE_AUDIO) */

