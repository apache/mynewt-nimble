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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "nimble_port.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/ias/ble_svc_ias.h"

static const char gap_name[] = "nimble test";

static struct os_task task_h;

static void start_advertise(void);

static int
ias_event_cb(uint8_t alert_level)
{
  /*
    switch (alert_level) {
    case BLE_SVC_IAS_ALERT_LEVEL_NO_ALERT:
        xTimerStop(led_tmr_h, portMAX_DELAY);
        bsp_board_leds_off();
        break;
    case BLE_SVC_IAS_ALERT_LEVEL_MILD_ALERT:
        bsp_board_led_on(BSP_BOARD_LED_0);
        bsp_board_led_off(BSP_BOARD_LED_1);
        bsp_board_led_off(BSP_BOARD_LED_2);
        bsp_board_led_on(BSP_BOARD_LED_3);
        xTimerStart(led_tmr_h, portMAX_DELAY);
        break;
    case BLE_SVC_IAS_ALERT_LEVEL_HIGH_ALERT:
        bsp_board_leds_on();
        xTimerStart(led_tmr_h, portMAX_DELAY);
        break;
    }
  */

    return 0;
}

static void
put_ad(uint8_t ad_type, uint8_t ad_len, const void *ad, uint8_t *buf,
       uint8_t *len)
{
    buf[(*len)++] = ad_len + 1;
    buf[(*len)++] = ad_type;

    memcpy(&buf[*len], ad, ad_len);

    *len += ad_len;
}

static void
update_ad(void)
{
    uint8_t ad[BLE_HS_ADV_MAX_SZ];
    uint8_t ad_len = 0;
    uint8_t ad_flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    uint16_t ad_uuid = htole16(BLE_SVC_IAS_UUID16);

    put_ad(BLE_HS_ADV_TYPE_FLAGS, 1, &ad_flags, ad, &ad_len);
    put_ad(BLE_HS_ADV_TYPE_COMP_NAME, sizeof(gap_name), gap_name, ad, &ad_len);
    put_ad(BLE_HS_ADV_TYPE_COMP_UUIDS16, sizeof(ad_uuid), &ad_uuid, ad, &ad_len);

    ble_gap_adv_set_data(ad, ad_len);
}

static int
gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status) {
            start_advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        start_advertise();
        break;
    }

    return 0;
}

static void
start_advertise(void)
{
    struct ble_gap_adv_params advp;
    int rc;

    update_ad();

    memset(&advp, 0, sizeof advp);
    advp.conn_mode = BLE_GAP_CONN_MODE_UND;
    advp.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &advp, gap_event_cb, NULL);
    assert(rc == 0);
}

static void
on_sync_cb(void)
{
    start_advertise();
}

static void
dflt_task(void *param)
{
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }
}

void start_nimble(void)
{
    /* Execute sysinit port */
    nimble_port_sysinit();

    /* Configure Nimble host */
    ble_hs_cfg.sync_cb = on_sync_cb;

    ble_svc_gap_device_name_set(gap_name);
    ble_svc_ias_set_cb(ias_event_cb);

    /* Create task which handles default event queue */
    os_task_init(&task_h, "dflt", dflt_task,
		 NULL, 1, 0, NULL, 400);
}
