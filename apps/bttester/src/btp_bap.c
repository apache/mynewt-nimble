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

/* btp_bap.c - Bluetooth Basic Audio Profile Tester */

#include "btp/bttester.h"
#include "syscfg/syscfg.h"
#include <string.h>


#if MYNEWT_VAL(BLE_ISO_BROADCAST_SOURCE)

#include "btp/btp_bap.h"

#include "btp/btp.h"
#include "console/console.h"

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "math.h"

#include "audio/ble_audio_broadcast_source.h"
#include "audio/ble_audio_broadcast_sink.h"
#include "audio/ble_audio.h"
#include "host/ble_iso.h"

#define BROADCAST_ADV_INSTANCE                 1

static struct ble_audio_big_subgroup big_subgroup;

static uint8_t id_addr_type;
static uint8_t audio_data[155];
static uint16_t max_sdu;
static uint32_t sdu_interval;

static struct ble_audio_base tester_base;

static os_membuf_t bis_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_ISO_MAX_BISES),
                    sizeof(struct ble_audio_bis))
];
static struct os_mempool bis_pool;

static os_membuf_t codec_spec_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_ISO_MAX_BISES) * 2, 19)
];
static struct os_mempool codec_spec_pool;

static uint16_t bis_handles[MYNEWT_VAL(BLE_ISO_MAX_BISES)];
/* The timer callout */
static struct os_callout audio_broadcast_callout;

struct ble_iso_big_params big_params;

static int audio_data_offset;

static void
audio_broadcast_event_cb(struct os_event *ev)
{
    assert(ev != NULL);
    uint32_t ev_start_time = os_cputime_ticks_to_usecs(os_cputime_get32());

    if (audio_data_offset + 2 * max_sdu >= sizeof(audio_data)) {
        audio_data_offset = 0;
    }

    uint8_t lr_payload[max_sdu * 2];
    memcpy(lr_payload, audio_data + audio_data_offset, max_sdu);
    memcpy(lr_payload + max_sdu, audio_data + audio_data_offset,
           max_sdu);
    ble_iso_tx(bis_handles[0], (void *)(lr_payload),
               max_sdu * 2);

    audio_data_offset += max_sdu;

    /** Use cputime to time BROADCAST_SDU_INTVL, as these ticks are more
     *  accurate than os_time ones. This assures that we do not push
     *  LC3 data to ISO before interval, which could lead to
     *  controller running out of buffers. This is only needed because
     *  we already have data in an array - in real world application
     *  we usually wait for new audio to arrive, and lose time to code it too.
     */
    while (os_cputime_ticks_to_usecs(os_cputime_get32()) - ev_start_time <
           (sdu_interval));

    os_callout_reset(&audio_broadcast_callout, 0);
}

static int
iso_event(struct ble_iso_event *event, void *arg)
{
    int i;

    switch (event->type) {
    case BLE_ISO_EVENT_BIG_CREATE_COMPLETE:
        console_printf("BIG created\n");
        if (event->big_created.desc.num_bis >
            MYNEWT_VAL(BROADCASTER_CHAN_NUM)) {
            return BLE_HS_EINVAL;
        }
        for (i = 0; i < MYNEWT_VAL(BROADCASTER_CHAN_NUM); i++) {
            bis_handles[i] = event->big_created.desc.conn_handle[i];
        }
        return 0;
    case BLE_ISO_EVENT_BIG_TERMINATE_COMPLETE:
        console_printf("BIG terminated\n");
        return 0;
    default:
        return BLE_HS_ENOTSUP;
    }
}

static uint8_t
supported_commands(const void *cmd, uint16_t cmd_len,
                   void *rsp, uint16_t *rsp_len)
{
    struct btp_bap_read_supported_commands_rp *rp = rsp;

    /* octet 0 */
    tester_set_bit(rp->data, BTP_BAP_READ_SUPPORTED_COMMANDS);
    tester_set_bit(rp->data, BTP_BAP_BROADCAST_SOURCE_SETUP);
    tester_set_bit(rp->data, BTP_BAP_BROADCAST_SOURCE_RELEASE);
    tester_set_bit(rp->data, BTP_BAP_BROADCAST_ADV_START);
    tester_set_bit(rp->data, BTP_BAP_BROADCAST_ADV_STOP);

    /* octet 1 */
    tester_set_bit(rp->data, BTP_BAP_BROADCAST_SOURCE_START);
    tester_set_bit(rp->data, BTP_BAP_BROADCAST_SOURCE_STOP);

    *rsp_len = sizeof(*rp) + 2;

    return BTP_STATUS_SUCCESS;
}

static int
base_create(const struct bap_broadcast_source_setup_cmd *cmd)
{
    struct ble_audio_bis *bis;
    uint8_t sampling_freq = cmd->cc_ltvs[2];
    uint8_t frame_duration = cmd->cc_ltvs[5];
    uint16_t chan_loc = BLE_AUDIO_LOCATION_FRONT_LEFT |
                        BLE_AUDIO_LOCATION_FRONT_RIGHT;

    uint8_t codec_spec_config[] =
        BLE_AUDIO_BUILD_CODEC_CONFIG(sampling_freq, frame_duration, chan_loc,
                                     max_sdu, );

    tester_base.broadcast_id = sampling_freq;
    tester_base.presentation_delay = sampling_freq * 10000;

    big_subgroup.bis_cnt = MYNEWT_VAL(BLE_ISO_MAX_BISES);

    /** LC3 */
    big_subgroup.codec_id.format = 0x06;

    big_subgroup.codec_spec_config_len = 0;

    bis = os_memblock_get(&bis_pool);
    if (!bis) {
        return BLE_HS_ENOMEM;
    }

    bis->codec_spec_config = os_memblock_get(&codec_spec_pool);
    memcpy(bis->codec_spec_config,
           codec_spec_config,
           sizeof(codec_spec_config));
    bis->codec_spec_config_len = sizeof(codec_spec_config);
    bis->idx = 1;

    STAILQ_INSERT_HEAD(&big_subgroup.bises, bis, next);
    STAILQ_INSERT_HEAD(&tester_base.subs, &big_subgroup, next);

    tester_base.num_subgroups++;

    return 0;
}

static int
broadcast_destroy_fn(struct ble_audio_base *base, void *args)
{
    struct ble_audio_bis *bis;

    STAILQ_FOREACH(bis, &big_subgroup.bises, next) {
        os_memblock_put(&codec_spec_pool, bis->codec_spec_config);
        os_memblock_put(&bis_pool, bis);
    }

    memset(&big_subgroup, 0, sizeof(big_subgroup));

    return 0;
}

static uint8_t
broadcast_source_setup(const void *cmd, uint16_t cmd_len, void *rsp,
                       uint16_t *rsp_len)
{
    int rc;

    const struct bap_broadcast_source_setup_cmd *source_config = cmd;
    max_sdu = source_config->max_sdu;
    sdu_interval = get_le24(source_config->sdu_interval);

    base_create(source_config);

    big_params.sdu_interval = sdu_interval;
    big_params.max_sdu = max_sdu;
    big_params.max_transport_latency = 8;
    big_params.rtn = source_config->rtn;
    big_params.phy = BLE_HCI_LE_PHY_2M;
    big_params.packing = 0;
    big_params.framing = source_config->framing;
    big_params.encryption = 0;

    struct ble_gap_periodic_adv_params periodic_params = {
        .itvl_min = 30,
        .itvl_max = 30,
    };

    struct ble_gap_ext_adv_params extended_params = {
        .itvl_min = 50,
        .itvl_max = 50,
        .scannable = 0,
        .connectable = 0,
        .primary_phy = BLE_HCI_LE_PHY_1M,
        .secondary_phy = BLE_HCI_LE_PHY_2M,
        .own_addr_type = id_addr_type,
        .sid = BROADCAST_ADV_INSTANCE,
    };

    struct ble_broadcast_create_params create_params = {
        .base = &tester_base,
        .extended_params = &extended_params,
        .periodic_params = &periodic_params,
        .name = MYNEWT_VAL(BROADCASTER_BROADCAST_NAME),
        .adv_instance = BROADCAST_ADV_INSTANCE,
        .big_params = &big_params,
        .svc_data = NULL,
        .svc_data_len = 0,
    };

    rc = ble_audio_broadcast_create(&create_params,
                               broadcast_destroy_fn,
                               NULL,
                               NULL);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
broadcast_source_release(const void *cmd, uint16_t cmd_len, void *rsp,
                         uint16_t *rsp_len)
{
    int rc;

    rc = ble_audio_broadcast_destroy(BROADCAST_ADV_INSTANCE);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
broadcast_adv_start(const void *cmd, uint16_t cmd_len, void *rsp,
                    uint16_t *rsp_len)
{
    int rc;

    rc = ble_audio_broadcast_start(BROADCAST_ADV_INSTANCE, iso_event, NULL);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
broadcast_adv_stop(const void *cmd, uint16_t cmd_len, void *rsp,
                   uint16_t *rsp_len)
{
    int rc;

    rc = ble_audio_broadcast_stop(BROADCAST_ADV_INSTANCE);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
broadcast_source_start(const void *cmd, uint16_t cmd_len, void *rsp,
                       uint16_t *rsp_len)
{
    int rc;

    rc = os_callout_reset(&audio_broadcast_callout, 0);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t broadcast_code_set(const void *cmd, uint16_t cmd_len, void *rsp,
                                  uint16_t *rsp_len)
{
    return BTP_STATUS_SUCCESS;
}

static uint8_t broadcast_sink_setup(const void *cmd, uint16_t cmd_len, void *rsp,
                                    uint16_t *rsp_len)
{
        return BTP_STATUS_SUCCESS;
}

static uint8_t broadcast_sink_stop(const void *cmd, uint16_t cmd_len, void *rsp,
                                    uint16_t *rsp_len)
{
    int rc;
    //const struct btp_bap_broadcast_sink_stop_cmd *cp = cmd;

    rc = ble_audio_broadcast_sink_stop(0);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    return rc;
}

static uint8_t
broadcast_source_stop(const void *cmd, uint16_t cmd_len, void *rsp,
                      uint16_t *rsp_len)
{
    os_callout_stop(&audio_broadcast_callout);

    return BTP_STATUS_SUCCESS;
}

static const struct btp_handler handlers[] = {
    {
        .opcode = BTP_BAP_READ_SUPPORTED_COMMANDS,
        .index = BTP_INDEX_NONE,
        .expect_len = 0,
        .func = supported_commands,
    },
    {
        .opcode = BTP_BAP_BROADCAST_SOURCE_SETUP,
        .index = BTP_INDEX,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = broadcast_source_setup,
    },
    {
        .opcode = BTP_BAP_BROADCAST_SOURCE_RELEASE,
        .index = BTP_INDEX,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = broadcast_source_release,
    },
    {
        .opcode = BTP_BAP_BROADCAST_ADV_START,
        .index = BTP_INDEX,
        .expect_len = sizeof(struct bap_bap_broadcast_adv_start_cmd),
        .func = broadcast_adv_start,
    },
    {
        .opcode = BTP_BAP_BROADCAST_ADV_STOP,
        .index = BTP_INDEX,
        .expect_len = sizeof(struct bap_bap_broadcast_adv_stop_cmd),
        .func = broadcast_adv_stop,
    },
    {
        .opcode = BTP_BAP_BROADCAST_SOURCE_START,
        .index = BTP_INDEX,
        .expect_len = sizeof(struct bap_bap_broadcast_source_start_cmd),
        .func = broadcast_source_start,
    },
    {
        .opcode = BTP_BAP_BROADCAST_SOURCE_STOP,
        .index = BTP_INDEX,
        .expect_len = sizeof(struct bap_bap_broadcast_source_stop_cmd),
        .func = broadcast_source_stop,
    },
    {
        .opcode = BTP_BAP_SET_BROADCAST_CODE,
        .index = BTP_INDEX,
        .expect_len = sizeof(struct btp_bap_set_broadcast_code_cmd),
        .func = broadcast_code_set,
    },
    {
        .opcode = BTP_BAP_BROADCAST_SINK_SETUP,
        .index = BTP_INDEX,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = broadcast_sink_setup,
    },
    {
        .opcode = BTP_BAP_BROADCAST_SINK_STOP,
        .index = BTP_INDEX,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = broadcast_sink_stop,
    },
};

#define BROADCAST_SINK_PA_SYNC_TIMEOUT_DEFAULT  0x07D0

static int
broadcast_sink_pa_sync_params_get(struct ble_gap_periodic_sync_params *params)
{
    params->skip = 0;
    params->sync_timeout = BROADCAST_SINK_PA_SYNC_TIMEOUT_DEFAULT;
    params->reports_disabled = false;

    return 0;
}

static int
broadcast_sink_disc_start(const struct ble_gap_ext_disc_params *params)
{
    uint8_t own_addr_type;
    int rc;

    /* Figure out address to use while scanning. */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        console_printf("determining own address type failed (%d)", rc);
        assert(0);
    }

    rc = ble_gap_ext_disc(own_addr_type, 0, 0, 0, 0, 0, params, NULL, NULL, NULL);
    if (rc != 0) {
        console_printf("ext disc failed (%d)", rc);
    }

    return rc;
}

static int
broadcast_sink_disc_stop(void)
{
    int rc;

    rc = ble_gap_disc_cancel();
    if (rc != 0) {
        console_printf("disc cancel failed (%d)", rc);
    }

    return rc;
}

static int
broadcast_sink_action_fn(struct ble_audio_broadcast_sink_action *action, void *arg)
{
    switch (action->type) {
    case BLE_AUDIO_BROADCAST_SINK_ACTION_PA_SYNC:
        return broadcast_sink_pa_sync_params_get(action->pa_sync.out_params);
    case BLE_AUDIO_BROADCAST_SINK_ACTION_BIG_SYNC:
        break;
    case BLE_AUDIO_BROADCAST_SINK_ACTION_BIS_SYNC:
        return 0;
    case BLE_AUDIO_BROADCAST_SINK_ACTION_DISC_START:
        return broadcast_sink_disc_start(action->disc_start.params_preferred);
    case BLE_AUDIO_BROADCAST_SINK_ACTION_DISC_STOP:
        return broadcast_sink_disc_stop();
    default:
        assert(false);
        return BLE_HS_ENOTSUP;
    }

    return 0;
}

static int
broadcast_sink_audio_event_handler(struct ble_audio_event *event, void *arg)
{
    switch (event->type) {
    case BLE_AUDIO_EVENT_BROADCAST_SINK_PA_SYNC_STATE:
        console_printf("source_id=0x%02x PA sync: %s\n",
                       event->broadcast_sink_pa_sync_state.source_id,
                       ble_audio_broadcast_sink_sync_state_str(
                               event->broadcast_sink_pa_sync_state.state));
        break;
    case BLE_AUDIO_EVENT_BROADCAST_SINK_BIS_SYNC_STATE:
        console_printf("source_id=0x%02x bis_index=0x%02x BIS sync: %s\n",
                       event->broadcast_sink_bis_sync_state.source_id,
                       event->broadcast_sink_bis_sync_state.bis_index,
                       ble_audio_broadcast_sink_sync_state_str(
                               event->broadcast_sink_bis_sync_state.state));
        if (event->broadcast_sink_bis_sync_state.state ==
            BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_ESTABLISHED) {
            console_printf("conn_handle=0x%04x\n",
                           event->broadcast_sink_bis_sync_state.conn_handle);
        }
        break;
    default:
        break;
    }

    return 0;
}

static int
scan_delegator_pick_source_id_to_swap(uint8_t *out_source_id_to_swap)
{
    /* TODO: Add some logic here */
    *out_source_id_to_swap = 0;

    return 0;
}

static int
scan_delegator_action_fn(struct ble_audio_scan_delegator_action *action, void *arg)
{
    switch (action->type) {
    case BLE_AUDIO_SCAN_DELEGATOR_ACTION_SOURCE_ADD:
        console_printf("Source Add:\nsource_id=%u\n", action->source_add.source_id);
        if (action->source_add.out_source_id_to_swap == NULL) {
            return 0;
        } else {
            return scan_delegator_pick_source_id_to_swap(action->source_add.out_source_id_to_swap);
        }
    case BLE_AUDIO_SCAN_DELEGATOR_ACTION_SOURCE_MODIFY:
        console_printf("Source Modify:\nsource_id=%u\n", action->source_modify.source_id);
        break;
    case BLE_AUDIO_SCAN_DELEGATOR_ACTION_SOURCE_REMOVE:
        console_printf("Source Remove:\nsource_id=%u\n", action->source_remove.source_id);
        break;
    default:
        assert(false);
        return BLE_HS_ENOTSUP;
    }

    return 0;
}

static int
scan_delegator_audio_event_handler(struct ble_audio_event *event, void *arg)
{
    switch (event->type) {
    case BLE_AUDIO_EVENT_BROADCAST_ANNOUNCEMENT:
        console_printf("\n");
        break;
    default:
        break;
    }

    return 0;
}

uint8_t
tester_init_bap(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* configure global address */
    rc = ble_hs_id_infer_auto(0, &id_addr_type);
    assert(rc == 0);

    memset(audio_data, 36, sizeof(char)*155);

    os_callout_init(&audio_broadcast_callout, os_eventq_dflt_get(),
                    audio_broadcast_event_cb, NULL);

    rc = os_mempool_init(&bis_pool, MYNEWT_VAL(BLE_ISO_MAX_BISES),
                         sizeof(struct ble_audio_bis), bis_mem,
                         "bis_pool");
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    rc = os_mempool_init(&codec_spec_pool,
                         MYNEWT_VAL(BLE_ISO_MAX_BISES) * 2, 19,
                         codec_spec_mem, "codec_spec_pool");
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    static struct ble_audio_event_listener broadcast_sink_listener;

    rc = ble_audio_broadcast_sink_cb_set(broadcast_sink_action_fn, NULL);
    assert(rc == 0);

    rc = ble_audio_event_listener_register(&broadcast_sink_listener,
                                           broadcast_sink_audio_event_handler, NULL);

    static struct ble_audio_event_listener scan_delegator_listener;

    rc = ble_audio_scan_delegator_action_fn_set(scan_delegator_action_fn, NULL);
    assert(rc == 0);

    rc = ble_audio_event_listener_register(&scan_delegator_listener,
                                           scan_delegator_audio_event_handler, NULL);
    assert(rc == 0);

    tester_register_command_handlers(BTP_SERVICE_ID_BAP, handlers,
                                     ARRAY_SIZE(handlers));

    return BTP_STATUS_SUCCESS;
}

uint8_t
tester_unregister_bap(void)
{
    return BTP_STATUS_SUCCESS;
}

#endif /* MYNEWT_VAL(BLE_ISO_BROADCAST_SOURCE) */

