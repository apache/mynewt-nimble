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

#include "audio/ble_audio_codec.h"
#include "services/pacs/ble_audio_svc_pacs.h"
#include "syscfg/syscfg.h"
#include "host/ble_hs.h"

/* Below is to unmangle comma separated Metadata octets from MYNEWT_VAL */
#define _Args(...) __VA_ARGS__
#define STRIP_PARENS(X) X
#define UNMANGLE_MYNEWT_VAL(X) STRIP_PARENS(_Args X)

#define BLE_SVC_AUDIO_PACS_LC3_CODEC_ID             0x06

static uint8_t ble_svc_audio_pacs_lc3_src_codec_spec_caps[] = BLE_AUDIO_BUILD_CODEC_CAPS(
    MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SRC_SAMPLING_FREQUENCIES),
    MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SRC_FRAME_DURATIONS),
#ifdef MYNEWT_VAL_BLE_SVC_AUDIO_PACS_LC3_SRC_AUDIO_CHANNEL_COUNTS
    MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SRC_AUDIO_CHANNEL_COUNTS),
#else
    ,
#endif
    MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SRC_MIN_OCTETS_PER_CODEC_FRAME),
    MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SRC_MAX_OCTETS_PER_CODEC_FRAME),
#ifdef MYNEWT_VAL_BLE_SVC_AUDIO_PACS_LC3_SRC_MAX_CODEC_FRAMES_PER_SDU
    MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SRC_MAX_CODEC_FRAMES_PER_SDU),
#endif
);

static uint8_t ble_svc_audio_pacs_lc3_snk_codec_spec_caps[] = BLE_AUDIO_BUILD_CODEC_CAPS(
    MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SNK_SAMPLING_FREQUENCIES),
    MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SNK_FRAME_DURATIONS),
    #ifdef MYNEWT_VAL_BLE_SVC_AUDIO_PACS_LC3_SRC_AUDIO_CHANNEL_COUNTS
        MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SNK_AUDIO_CHANNEL_COUNTS),
    #else
    ,
    #endif
        MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SNK_MIN_OCTETS_PER_CODEC_FRAME),
    MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SRC_MAX_OCTETS_PER_CODEC_FRAME),
    #ifdef MYNEWT_VAL_BLE_SVC_AUDIO_PACS_LC3_SRC_MAX_CODEC_FRAMES_PER_SDU
        MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SNK_MAX_CODEC_FRAMES_PER_SDU),
    #endif
);

#ifdef MYNEWT_VAL_BLE_SVC_AUDIO_PACS_LC3_SRC_METADATA
static uint8_t ble_svc_audio_pacs_lc3_src_metadata[] =
{ UNMANGLE_MYNEWT_VAL(MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SRC_METADATA)) };
#endif

#ifdef MYNEWT_VAL_BLE_SVC_AUDIO_PACS_LC3_SNK_METADATA
static uint8_t ble_svc_audio_pacs_lc3_snk_metadata[] =
{ UNMANGLE_MYNEWT_VAL(MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SNK_METADATA)) };
#endif

static struct ble_audio_codec_register_params src_codec_params = {
    .codec_id = {
        .format = BLE_SVC_AUDIO_PACS_LC3_CODEC_ID,
        .company_id = 0x00,
        .vendor_specific = 0x00
    },
    .codec_spec_caps_len = sizeof(ble_svc_audio_pacs_lc3_src_codec_spec_caps),
    .codec_spec_caps = ble_svc_audio_pacs_lc3_src_codec_spec_caps,
#ifdef MYNEWT_VAL_BLE_SVC_AUDIO_PACS_LC3_SRC_METADATA
    .metadata_len = sizeof(ble_svc_audio_pacs_lc3_src_metadata),
    .metadata = ble_svc_audio_pacs_lc3_src_metadata,
#else
    .metadata_len = 0,
#endif
    .direction = BLE_AUDIO_CODEC_DIR_SOURCE_BIT
};

static struct ble_audio_codec_register_params snk_codec_params = {
    .codec_id = {
        .format = BLE_SVC_AUDIO_PACS_LC3_CODEC_ID,
        .company_id = 0x00,
        .vendor_specific = 0x00
    },
    .codec_spec_caps_len = sizeof(ble_svc_audio_pacs_lc3_snk_codec_spec_caps),
    .codec_spec_caps = ble_svc_audio_pacs_lc3_snk_codec_spec_caps,
#ifdef MYNEWT_VAL_BLE_SVC_AUDIO_PACS_LC3_SNK_METADATA
    .metadata_len = sizeof(ble_svc_audio_pacs_lc3_snk_metadata),
    .metadata = ble_svc_audio_pacs_lc3_snk_metadata,
#else
    .metadata_len = 0,
#endif

    .direction = BLE_AUDIO_CODEC_DIR_SINK_BIT
};

static int
codec_register(void)
{
    int rc;

    rc = ble_audio_codec_register(&src_codec_params, NULL);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_audio_codec_register(&snk_codec_params, NULL);
    SYSINIT_PANIC_ASSERT(rc == 0);

    return 0;
}

int
ble_svc_audio_pacs_lc3_set_avail_contexts(uint16_t conn_handle,
                                          uint16_t sink_contexts,
                                          uint16_t source_contexts)
{
    return ble_svc_audio_pacs_avail_contexts_set(conn_handle, sink_contexts,
                                                 source_contexts);
}

void
ble_svc_audio_pacs_lc3_init(void)
{
    struct ble_svc_audio_pacs_set_param src_params = {
        .audio_locations = MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SRC_AUDIO_LOCATIONS),
        .supported_contexts = MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SRC_SUP_CONTEXTS)
    };
    struct ble_svc_audio_pacs_set_param snk_params = {
        .audio_locations = MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SNK_SUP_AUDIO_LOCATIONS),
        .supported_contexts = MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SNK_SUP_CONTEXTS)
    };
    int rc;

    rc = codec_register();
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_svc_audio_pacs_set(BLE_AUDIO_CODEC_DIR_SOURCE_BIT, &src_params);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_svc_audio_pacs_set(BLE_AUDIO_CODEC_DIR_SINK_BIT, &snk_params);
    SYSINIT_PANIC_ASSERT(rc == 0);

    (void)rc;
}
