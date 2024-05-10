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

#include "console/console.h"
#include "config/config.h"

#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

#include "services/auracast/ble_svc_auracast.h"

#include "hal/hal_gpio.h"
#include "bsp/bsp.h"
#include "app_priv.h"

#define BROADCAST_SID                       1
#define BROADCAST_SDU_INTVL                 MYNEWT_VAL(LC3_FRAME_DURATION)

#if (MYNEWT_VAL(LC3_SAMPLING_FREQ) == 8000)
#define BROADCAST_SAMPLE_RATE               BLE_AUDIO_SAMPLING_RATE_8000_HZ
#elif (MYNEWT_VAL(LC3_SAMPLING_FREQ) == 16000)
#define BROADCAST_SAMPLE_RATE               BLE_AUDIO_SAMPLING_RATE_16000_HZ
#elif (MYNEWT_VAL(LC3_SAMPLING_FREQ) == 24000)
#define BROADCAST_SAMPLE_RATE               BLE_AUDIO_SAMPLING_RATE_24000_HZ
#elif (MYNEWT_VAL(LC3_SAMPLING_FREQ) == 32000)
#define BROADCAST_SAMPLE_RATE               BLE_AUDIO_SAMPLING_RATE_32000_HZ
#elif (MYNEWT_VAL(LC3_SAMPLING_FREQ) == 48000)
#define BROADCAST_SAMPLE_RATE               BLE_AUDIO_SAMPLING_RATE_48000_HZ
#else
BUILD_ASSERT(0, "Sample frequency not supported");
#endif

#define BROADCAST_MAX_SDU                   (BROADCAST_SDU_INTVL * \
                                             MYNEWT_VAL(LC3_BITRATE) / \
                                             (1000 * 1000 * 8) *   \
                                             MYNEWT_VAL(AURACAST_CHAN_NUM) / \
                                             MYNEWT_VAL(BIG_NUM_BIS))

#define BROADCASTER_INTERRUPT_TASK_PRIO  4
#define BROADCASTER_INTERRUPT_TASK_STACK_SZ    512

static uint8_t id_addr_type;

static struct ble_audio_base auracast_base;
static struct ble_audio_big_subgroup big_subgroup;

static os_membuf_t bis_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BIG_NUM_BIS),
                    sizeof(struct ble_audio_bis))
];
static struct os_mempool bis_pool;

static os_membuf_t codec_spec_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BIG_NUM_BIS) * 2, 19)
];
static struct os_mempool codec_spec_pool;

static uint8_t auracast_adv_instance;

static void
auracast_init(void)
{
    int rc;

    assert(MYNEWT_VAL(AURACAST_CHAN_NUM) > 0);

    rc = os_mempool_init(&bis_pool, MYNEWT_VAL(BIG_NUM_BIS),
                         sizeof(struct ble_audio_bis), bis_mem,
                         "bis_pool");
    assert(rc == 0);

    rc = os_mempool_init(&codec_spec_pool,
                         MYNEWT_VAL(BIG_NUM_BIS) * 2, 19,
                         codec_spec_mem, "codec_spec_pool");
    assert(rc == 0);
}

static int
base_create(void)
{
#if MYNEWT_VAL(BIG_NUM_BIS) > 1
    struct ble_audio_bis *bis_left;
    struct ble_audio_bis *bis_right;
    uint8_t codec_spec_config_left_chan[] =
        BLE_AUDIO_BUILD_CODEC_CONFIG(BROADCAST_SAMPLE_RATE,
                                     MYNEWT_VAL(LC3_FRAME_DURATION) == 10000 ?
                                     BLE_AUDIO_SELECTED_FRAME_DURATION_10_MS :
                                     BLE_AUDIO_SELECTED_FRAME_DURATION_7_5_MS,
                                     BLE_AUDIO_LOCATION_FRONT_LEFT,
                                     BROADCAST_MAX_SDU, );
    uint8_t codec_spec_config_right_chan[] =
        BLE_AUDIO_BUILD_CODEC_CONFIG(BROADCAST_SAMPLE_RATE,
                                     MYNEWT_VAL(LC3_FRAME_DURATION) == 10000 ?
                                     BLE_AUDIO_SELECTED_FRAME_DURATION_10_MS :
                                     BLE_AUDIO_SELECTED_FRAME_DURATION_7_5_MS,
                                     BLE_AUDIO_LOCATION_FRONT_RIGHT,
                                     BROADCAST_MAX_SDU, );
#else
    uint16_t chan_loc = BLE_AUDIO_LOCATION_FRONT_LEFT |
                        BLE_AUDIO_LOCATION_FRONT_RIGHT;
    uint8_t codec_spec_config[] =
        BLE_AUDIO_BUILD_CODEC_CONFIG(BROADCAST_SAMPLE_RATE,
                                     MYNEWT_VAL(LC3_FRAME_DURATION) == 10000 ?
                                     BLE_AUDIO_SELECTED_FRAME_DURATION_10_MS :
                                     BLE_AUDIO_SELECTED_FRAME_DURATION_7_5_MS,
                                     chan_loc,
                                     BROADCAST_MAX_SDU * 2, );

    struct ble_audio_bis *bis;
#endif
    if (MYNEWT_VAL(BROADCAST_ID) != 0) {
        auracast_base.broadcast_id = MYNEWT_VAL(BROADCAST_ID);
    } else {
        ble_hs_hci_rand(&auracast_base.broadcast_id, 3);
    }
    auracast_base.presentation_delay = 20000;

    big_subgroup.bis_cnt = MYNEWT_VAL(BIG_NUM_BIS);

    /** LC3 */
    big_subgroup.codec_id.format = 0x06;

    big_subgroup.codec_spec_config_len = 0;
#if MYNEWT_VAL(BIG_NUM_BIS) > 1
    bis_left = os_memblock_get(&bis_pool);
    if (!bis_left) {
        return BLE_HS_ENOMEM;
    }

    bis_left->codec_spec_config = os_memblock_get(&codec_spec_pool);
    memcpy(bis_left->codec_spec_config,
           codec_spec_config_left_chan,
           sizeof(codec_spec_config_left_chan));
    bis_left->codec_spec_config_len = sizeof(codec_spec_config_left_chan);
    bis_left->idx = 1;

    bis_right = os_memblock_get(&bis_pool);
    if (!bis_right) {
        return BLE_HS_ENOMEM;
    }

    bis_right->codec_spec_config = os_memblock_get(&codec_spec_pool);
    memcpy(bis_right->codec_spec_config,
           codec_spec_config_right_chan,
           sizeof(codec_spec_config_right_chan));
    bis_right->codec_spec_config_len = sizeof(codec_spec_config_right_chan);
    bis_right->idx = 2;

    STAILQ_INSERT_HEAD(&big_subgroup.bises, bis_left, next);
    STAILQ_INSERT_TAIL(&big_subgroup.bises, bis_right, next);
#else
    bis = os_memblock_get(&bis_pool);
    if (!bis) {
        return BLE_HS_ENOMEM;
    }

    bis->codec_spec_config = os_memblock_get(&codec_spec_pool);
    memcpy(bis->codec_spec_config,
           codec_spec_config,
           sizeof(codec_spec_config));
    bis->codec_spec_config_len = sizeof(codec_spec_config);
    STAILQ_INSERT_HEAD(&big_subgroup.bises, bis, next);
#endif

    STAILQ_INSERT_HEAD(&auracast_base.subs, &big_subgroup, next);
    auracast_base.num_subgroups++;
    return 0;
}

static int
auracast_destroy_fn(struct ble_audio_base *base, void *args)
{
    struct ble_audio_bis *bis;
    int i;

    STAILQ_FOREACH(bis, &big_subgroup.bises, next) {
        os_memblock_put(&codec_spec_pool, bis->codec_spec_config);
        os_memblock_put(&bis_pool, bis);
    }

    memset(&big_subgroup, 0, sizeof(big_subgroup));

    for (i = 0; i < MYNEWT_VAL(AURACAST_CHAN_NUM); i++) {
        chans[i].handle = 0;
    }

    return 0;
}

static int
iso_event(struct ble_iso_event *event, void *arg)
{
    int i;

    switch (event->type) {
    case BLE_ISO_EVENT_BIG_CREATE_COMPLETE:
        console_printf("BIG created\n");
        if (event->big_created.desc.num_bis >
            MYNEWT_VAL(AURACAST_CHAN_NUM)) {
            return BLE_HS_EINVAL;
        }
        if (MYNEWT_VAL(AURACAST_CHAN_NUM) == event->big_created.desc.num_bis) {
            for (i = 0; i < MYNEWT_VAL(AURACAST_CHAN_NUM); i++) {
                chans[i].handle = event->big_created.desc.conn_handle[i];
            }
        } else {
            for (i = 0; i < MYNEWT_VAL(AURACAST_CHAN_NUM); i++) {
                chans[i].handle = event->big_created.desc.conn_handle[0];
            }
        }
        return 0;
    case BLE_ISO_EVENT_BIG_TERMINATE_COMPLETE:
        console_printf("BIG terminated\n");
        return 0;
    default:
        return BLE_HS_ENOTSUP;
    }
}

static int
auracast_create(void)
{
    const char *program_info = "NimBLE Auracast Test";
    static struct ble_iso_big_params big_params = {
        .sdu_interval = MYNEWT_VAL(LC3_FRAME_DURATION),
        .max_sdu = BROADCAST_MAX_SDU,
        .max_transport_latency = MYNEWT_VAL(LC3_FRAME_DURATION) / 1000,
        .rtn = MYNEWT_VAL(BIG_RTN),
        .phy = MYNEWT_VAL(BIG_PHY),
        .packing = MYNEWT_VAL(BIG_PACKING),
        .framing = MYNEWT_VAL(BIG_FRAMING),
        .encryption = MYNEWT_VAL(BIG_ENCRYPTION),
        .broadcast_code = MYNEWT_VAL(BROADCAST_CODE),
    };

    struct ble_svc_auracast_create_params create_params = {
        .base = &auracast_base,
        .big_params = &big_params,
        .name = MYNEWT_VAL(BROADCAST_NAME),
        .program_info = program_info,
        .own_addr_type = id_addr_type,
        .secondary_phy = BLE_HCI_LE_PHY_2M,
        .sid = BROADCAST_SID,
        .frame_duration = MYNEWT_VAL(LC3_FRAME_DURATION),
        .sampling_frequency = MYNEWT_VAL(LC3_SAMPLING_FREQ),
        .bitrate = MYNEWT_VAL(LC3_BITRATE),
    };

    return ble_svc_auracast_create(&create_params,
                                   &auracast_adv_instance,
                                   auracast_destroy_fn,
                                   NULL,
                                   NULL);
}

static int
auracast_start(void)
{
    return ble_svc_auracast_start(auracast_adv_instance, iso_event, NULL);
}

static void
on_sync(void)
{
    int rc;

    console_printf("Bluetooth initialized\n");

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* configure global address */
    rc = ble_hs_id_infer_auto(0, &id_addr_type);
    assert(rc == 0);

    auracast_init();

    rc = base_create();
    assert(rc == 0);

    rc = auracast_create();
    assert(rc == 0);

    rc = auracast_start();
    assert(rc == 0);
}

/*
 * main
 *
 * The main task for the project. This function initializes the packages,
 * then starts serving events from default event queue.
 *
 * @return int NOTE: this function should never return!
 */
int
mynewt_main(int argc, char **argv)
{
    /* Initialize OS */
    sysinit();

    console_printf("LE Audio Broadcast sample application\n");

    /* Set sync callback */
    ble_hs_cfg.sync_cb = on_sync;

    /* As the last thing, process events from default event queue */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
