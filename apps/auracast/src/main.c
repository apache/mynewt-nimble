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

#include "audio_data.h"

#define BROADCAST_SID                       1
#define BROADCAST_MAX_SDU                   120
#define BROADCAST_SDU_INTVL                 10000
#define BROADCAST_SAMPLE_RATE               BLE_AUDIO_SAMPLING_RATE_48000_HZ

#define BROADCASTER_INTERRUPT_TASK_PRIO  4
#define BROADCASTER_INTERRUPT_TASK_STACK_SZ    512

static uint8_t id_addr_type;

static struct ble_audio_base auracast_base;
static struct ble_audio_big_subgroup big_subgroup;

static os_membuf_t bis_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BROADCASTER_CHAN_NUM),
                    sizeof(struct ble_audio_bis))
];
static struct os_mempool bis_pool;

static os_membuf_t codec_spec_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_MAX_BIS) * 2, 19)
];
static struct os_mempool codec_spec_pool;

static uint16_t bis_handles[MYNEWT_VAL(BROADCASTER_CHAN_NUM)];
/* The timer callout */
static struct os_callout audio_broadcast_callout;

static int audio_data_offset;
static struct os_task auracast_interrupt_task_str;
static struct os_eventq auracast_interrupt_eventq;
static os_stack_t auracast_interrupt_task_stack[BROADCASTER_INTERRUPT_TASK_STACK_SZ];

static uint8_t auracast_adv_instance;

static void
auracast_interrupt_task(void *arg)
{
    while (1) {
        os_eventq_run(&auracast_interrupt_eventq);
    }
}

static void
broadcast_stop_ev_cb(struct os_event *ev)
{
    ble_svc_auracast_stop(auracast_adv_instance);
    ble_svc_auracast_terminate(auracast_adv_instance);
}

static struct os_event broadcast_stop_ev = {
    .ev_cb = broadcast_stop_ev_cb,
};

static void
auracast_gpio_irq(void *arg)
{
    os_eventq_put(&auracast_interrupt_eventq, &broadcast_stop_ev);
}

static void
audio_broadcast_event_cb(struct os_event *ev)
{
    assert(ev != NULL);
    uint32_t ev_start_time = os_cputime_ticks_to_usecs(os_cputime_get32());

#if MYNEWT_VAL(BROADCASTER_CHAN_NUM) > 1
    if (audio_data_offset + BROADCAST_MAX_SDU >= sizeof(audio_data)) {
        audio_data_offset = 0;
    }

    ble_iso_tx(bis_handles[0], (void *)(audio_data + audio_data_offset),
               BROADCAST_MAX_SDU);
    ble_iso_tx(bis_handles[1], (void *)(audio_data + audio_data_offset),
               BROADCAST_MAX_SDU);
#else
    if (audio_data_offset + 2 * BROADCAST_MAX_SDU >= sizeof(audio_data)) {
        audio_data_offset = 0;
    }

    uint8_t lr_payload[BROADCAST_MAX_SDU * 2];
    memcpy(lr_payload, audio_data + audio_data_offset, BROADCAST_MAX_SDU);
    memcpy(lr_payload + BROADCAST_MAX_SDU, audio_data + audio_data_offset,
           BROADCAST_MAX_SDU);
    ble_iso_tx(bis_handles[0], (void *)(lr_payload),
               BROADCAST_MAX_SDU * 2);
#endif
    audio_data_offset += BROADCAST_MAX_SDU;

    /** Use cputime to time BROADCAST_SDU_INTVL, as these ticks are more
     *  accurate than os_time ones. This assures that we do not push
     *  LC3 data to ISO before interval, which could lead to
     *  controller running out of buffers. This is only needed because
     *  we already have coded data in an array - in real world application
     *  we usually wait for new audio to arrive, and lose time to code it too.
     */
    while (os_cputime_ticks_to_usecs(os_cputime_get32()) - ev_start_time <
           (BROADCAST_SDU_INTVL));

    os_callout_reset(&audio_broadcast_callout, 0);
}
static int
broadcast_audio()
{
    os_callout_reset(&audio_broadcast_callout, 0);

    return 0;
}

static void
auracast_init()
{
    int rc;

    os_callout_init(&audio_broadcast_callout, os_eventq_dflt_get(),
                    audio_broadcast_event_cb, NULL);

    assert(MYNEWT_VAL(BROADCASTER_CHAN_NUM) > 0);

    rc = os_mempool_init(&bis_pool, MYNEWT_VAL(BROADCASTER_CHAN_NUM),
                         sizeof(struct ble_audio_bis), bis_mem,
                         "bis_pool");
    assert(rc == 0);

    rc = os_mempool_init(&codec_spec_pool,
                         MYNEWT_VAL(BLE_MAX_BIS) * 2, 19,
                         codec_spec_mem, "codec_spec_pool");
    assert(rc == 0);
}

static int
base_create()
{
#if MYNEWT_VAL(BROADCASTER_CHAN_NUM) > 1
    struct ble_audio_bis *bis_left;
    struct ble_audio_bis *bis_right;
    uint8_t codec_spec_config_left_chan[] =
        BLE_AUDIO_BUILD_CODEC_CONFIG(BROADCAST_SAMPLE_RATE,
                                     BLE_AUDIO_SELECTED_FRAME_DURATION_10_MS,
                                     BLE_AUDIO_LOCATION_FRONT_LEFT,
                                     BROADCAST_MAX_SDU, );
    uint8_t codec_spec_config_right_chan[] =
        BLE_AUDIO_BUILD_CODEC_CONFIG(BROADCAST_SAMPLE_RATE,
                                     BLE_AUDIO_SELECTED_FRAME_DURATION_10_MS,
                                     BLE_AUDIO_LOCATION_FRONT_RIGHT,
                                     BROADCAST_MAX_SDU, );
#else
    uint16_t chan_loc = BLE_AUDIO_LOCATION_FRONT_LEFT |
                        BLE_AUDIO_LOCATION_FRONT_RIGHT;
    uint8_t codec_spec_config[] =
        BLE_AUDIO_BUILD_CODEC_CONFIG(BROADCAST_SAMPLE_RATE,
                                     BLE_AUDIO_SELECTED_FRAME_DURATION_10_MS,
                                     chan_loc,
                                     BROADCAST_MAX_SDU * 2, );

    struct ble_audio_bis *bis;
#endif
    auracast_base.broadcast_id = 0x42;
    auracast_base.presentation_delay = 20000;

    big_subgroup.bis_cnt = MYNEWT_VAL(BROADCASTER_CHAN_NUM);

    /** LC3 */
    big_subgroup.codec_id.format = 0x06;

    big_subgroup.codec_spec_config_len = 0;
#if MYNEWT_VAL(BROADCASTER_CHAN_NUM) > 1
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

    STAILQ_FOREACH(bis, &big_subgroup.bises, next) {
        os_memblock_put(&codec_spec_pool, bis->codec_spec_config);
        os_memblock_put(&bis_pool, bis);
    }

    memset(&big_subgroup, 0, sizeof(big_subgroup));

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
            MYNEWT_VAL(BROADCASTER_CHAN_NUM)) {
            return BLE_HS_EINVAL;
        }
        for (i = 0; i < MYNEWT_VAL(BROADCASTER_CHAN_NUM); i++) {
            bis_handles[i] = event->big_created.desc.conn_handle[i];
        }
        broadcast_audio();
        return 0;
    case BLE_ISO_EVENT_BIG_TERMINATE_COMPLETE:
        console_printf("BIG terminated\n");
        return 0;
    default:
        return BLE_HS_ENOTSUP;
    }
}

static int
auracast_create()
{
    const char *program_info = "NimBLE Auracast Test";
    static struct ble_iso_big_params big_params = {
        .sdu_interval = BROADCAST_SDU_INTVL,
        .max_sdu = MYNEWT_VAL(BROADCASTER_CHAN_NUM) > 1 ?
                   BROADCAST_MAX_SDU : BROADCAST_MAX_SDU * 2,
        .max_transport_latency = BROADCAST_SDU_INTVL / 1000,
        .rtn = 0,
        .phy = BLE_HCI_LE_PHY_2M,
        .packing = 0,
        .framing = 0,
        .encryption = 0,
    };

    struct ble_svc_auracast_create_params create_params = {
        .base = &auracast_base,
        .big_params = &big_params,
        .name = MYNEWT_VAL(BROADCASTER_BROADCAST_NAME),
        .program_info = program_info,
        .own_addr_type = id_addr_type,
        .secondary_phy = BLE_HCI_LE_PHY_2M,
        .sid = BROADCAST_SID,
        .frame_duration = 10000,
        .sampling_frequency = 48000,
        .bitrate = 48000,
    };

    return ble_svc_auracast_create(&create_params,
                                   &auracast_adv_instance,
                                   auracast_destroy_fn,
                                   NULL,
                                   NULL);
}

static int
auracast_start()
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

    broadcast_audio();
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

    os_eventq_init(&auracast_interrupt_eventq);
    os_task_init(&auracast_interrupt_task_str, "auracast_interrupt_task",
                 auracast_interrupt_task, NULL,
                 BROADCASTER_INTERRUPT_TASK_PRIO, OS_WAIT_FOREVER,
                 auracast_interrupt_task_stack,
                 BROADCASTER_INTERRUPT_TASK_STACK_SZ);

    hal_gpio_irq_init(BUTTON_3, auracast_gpio_irq, NULL,
                      HAL_GPIO_TRIG_RISING, HAL_GPIO_PULL_UP);
    hal_gpio_irq_enable(BUTTON_3);

    /* As the last thing, process events from default event queue */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
