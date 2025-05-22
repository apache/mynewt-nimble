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

/* btp_pacs.c - Bluetooth Published Audio Capacity Service Tester */

#include "syscfg/syscfg.h"

#if MYNEWT_VAL(BLE_AUDIO)

#include "audio/ble_audio.h"
#include "audio/ble_audio_codec.h"
#include "btp/bttester.h"
#include "host/ble_gap.h"
#include "os/util.h"
#include <stdint.h>

#include "btp/btp.h"
#include "btp/btp_pacs.h"
#include "services/pacs/ble_audio_svc_pacs.h"
#include "services/pacs/ble_audio_svc_pacs_lc3.h"

#define BLE_SVC_AUDIO_PACS_LC3_CODEC_ID             0x06
#define BTTESTER_SUPPORTED_CTXTS                    0x07

struct set_avail_cb_data {
    uint16_t src_ctxts;
    uint16_t snk_ctxts;
};

#ifdef MYNEWT_VAL_BLE_SVC_AUDIO_PACS_LC3_SNK_METADATA
static uint8_t ble_svc_audio_pacs_lc3_snk_metadata[] =
{ UNMANGLE_MYNEWT_VAL(MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SNK_METADATA)) };
#endif

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

int
set_available(uint16_t conn_handle, void *arg)
{
    int rc;
    struct set_avail_cb_data *avail_data = arg;

    rc = ble_svc_audio_pacs_avail_contexts_set(conn_handle,
                                               avail_data->snk_ctxts,
                                               avail_data->src_ctxts);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
pacs_set_available_contexts(const void *cmd, uint16_t cmd_len,
                            void *rsp, uint16_t *rsp_len)
{
    const struct btp_pacs_set_available_contexts_cmd *cp = cmd;
    uint16_t source_contexts = le16toh(cp->source_contexts);
    uint16_t sink_conexts = le16toh(cp->sink_contexts);
    struct set_avail_cb_data cb_data;

    /* If this originated from pacs_update_characteristic - we update with unspecified */
    if (sink_conexts == BTP_PACS_CHARACTERISTIC_AVAILABLE_AUDIO_CONTEXTS) {
        cb_data.snk_ctxts = BLE_AUDIO_CONTEXT_TYPE_UNSPECIFIED;
        cb_data.src_ctxts = BLE_AUDIO_CONTEXT_TYPE_UNSPECIFIED;
    } else {
        cb_data.snk_ctxts = sink_conexts;
        cb_data.src_ctxts = source_contexts;
    }

    ble_gap_conn_foreach_handle(set_available, &cb_data);

    return BTP_STATUS_SUCCESS;
}

static uint8_t
pacs_set_snk_location(void)
{
    int rc;
    struct ble_svc_audio_pacs_set_param snk_params = {
        .audio_locations = 0,
        .supported_contexts = MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SNK_SUP_CONTEXTS)
    };

    rc = ble_svc_audio_pacs_set(BLE_AUDIO_CODEC_DIR_SINK_BIT, &snk_params);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
pacs_set_src_location(void)
{
    int rc;
    struct ble_svc_audio_pacs_set_param src_params = {
        .audio_locations = 0,
        .supported_contexts = MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SRC_SUP_CONTEXTS)
    };

    rc = ble_svc_audio_pacs_set(BLE_AUDIO_CODEC_DIR_SOURCE_BIT, &src_params);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
pacs_update_characteristic(const void *cmd, uint16_t cmd_len,
                           void *rsp, uint16_t *rsp_len)
{
    int rc;
    const struct btp_pacs_update_characteristic_cmd *cp = cmd;

    switch (cp->char_id) {
    case BTP_PACS_CHARACTERISTIC_SINK_PAC:
        rc = ble_audio_codec_register(&snk_codec_params, NULL);
        if (rc) {
            return BTP_STATUS_FAILED;
        }
        break;
    case BTP_PACS_CHARACTERISTIC_SOURCE_PAC:
        rc = ble_audio_codec_register(&src_codec_params, NULL);
        if (rc) {
            return BTP_STATUS_FAILED;
        }
        break;
    case BTP_PACS_CHARACTERISTIC_SINK_AUDIO_LOCATIONS:
        rc = pacs_set_snk_location();
        if (rc) {
            return BTP_STATUS_FAILED;
        }
        break;
    case BTP_PACS_CHARACTERISTIC_SOURCE_AUDIO_LOCATIONS:
        rc = pacs_set_src_location();
        if (rc) {
            return BTP_STATUS_FAILED;
        }
        break;
    case BTP_PACS_CHARACTERISTIC_AVAILABLE_AUDIO_CONTEXTS:
        rc = pacs_set_available_contexts(cmd, cmd_len, rsp, rsp_len);
        if (rc) {
            return BTP_STATUS_FAILED;
        }
        break;
    default:
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
pacs_set_supported_contexts(const void *cmd, uint16_t cmd_len,
                            void *rsp, uint16_t *rsp_len)
{
    int rc;
    const struct btp_pacs_set_supported_contexts_cmd *sup_ctxts = cmd;

    struct ble_svc_audio_pacs_set_param src_params = {
        .audio_locations = 0,
        .supported_contexts = sup_ctxts->source_contexts,
    };
    struct ble_svc_audio_pacs_set_param snk_params = {
        .audio_locations = 0,
        .supported_contexts = sup_ctxts->sink_contexts,
    };

    rc = ble_svc_audio_pacs_set(BLE_AUDIO_CODEC_DIR_SOURCE_BIT, &src_params);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    rc = ble_svc_audio_pacs_set(BLE_AUDIO_CODEC_DIR_SINK_BIT, &snk_params);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
supported_commands(const void *cmd, uint16_t cmd_len,
                   void *rsp, uint16_t *rsp_len)
{
    struct btp_pacs_read_supported_commands_rp *rp = rsp;

    *rsp_len = tester_supported_commands(BTP_SERVICE_ID_PACS, rp->data);
    *rsp_len += sizeof(*rp);

    return BTP_STATUS_SUCCESS;
}

static const struct btp_handler handlers[] = {
    {
        .opcode = BTP_PACS_READ_SUPPORTED_COMMANDS,
        .expect_len = 0,
        .func = supported_commands,
    },
    {
        .opcode = BTP_PACS_UPDATE_CHARACTERISTIC,
        .expect_len = sizeof(struct btp_pacs_update_characteristic_cmd),
        .func = pacs_update_characteristic,
    },
    {
        .opcode = BTP_PACS_SET_AVAILABLE_CONTEXTS,
        .expect_len = sizeof(struct btp_pacs_set_available_contexts_cmd),
        .func = pacs_set_available_contexts,
    },
    {
        .opcode = BTP_PACS_SET_SUPPORTED_CONTEXTS,
        .expect_len = sizeof(struct btp_pacs_set_supported_contexts_cmd),
        .func = pacs_set_supported_contexts,
    },
};

uint8_t
tester_init_pacs(void)
{
    int rc;
    struct ble_svc_audio_pacs_set_param src_params = {
        .audio_locations = MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SRC_AUDIO_LOCATIONS),
        .supported_contexts = BTTESTER_SUPPORTED_CTXTS
    };
    struct ble_svc_audio_pacs_set_param snk_params = {
        .audio_locations = MYNEWT_VAL(BLE_SVC_AUDIO_PACS_LC3_SRC_AUDIO_LOCATIONS),
        .supported_contexts = BTTESTER_SUPPORTED_CTXTS
    };

    rc = ble_svc_audio_pacs_set(BLE_AUDIO_CODEC_DIR_SOURCE_BIT, &src_params);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_svc_audio_pacs_set(BLE_AUDIO_CODEC_DIR_SINK_BIT, &snk_params);
    SYSINIT_PANIC_ASSERT(rc == 0);

    tester_register_command_handlers(BTP_SERVICE_ID_PACS, handlers,
                                     ARRAY_SIZE(handlers));

    return BTP_STATUS_SUCCESS;
}

uint8_t
tester_unregister_pacs(void)
{
    return BTP_STATUS_SUCCESS;
}

#endif /* MYNEWT_VAL(BLE_AUDIO)*/
