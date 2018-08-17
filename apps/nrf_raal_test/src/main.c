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
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "os/mynewt.h"
#include "hal/hal_gpio.h"
#include "console/console.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "shell/shell.h"

#define NNRF_RAAL_TIMESLOT_GPIO         (28)

extern void nrf_raal_continuous_mode_enter(void);
extern void nrf_raal_continuous_mode_exit(void);

void my_radio_isr_handler(void)
{

}

void nrf_raal_timeslot_started(void)
{
    hal_gpio_write(NNRF_RAAL_TIMESLOT_GPIO, 1);

    /*
     * Break radio configuration to make sure PHY will reconfigure when exiting
     * from slot
     */
    NRF_RADIO->POWER = 0;
}

void nrf_raal_timeslot_ended(void)
{
    hal_gpio_write(NNRF_RAAL_TIMESLOT_GPIO, 0);
}

static void start_advertise(void);

static int
cmd_cont_enter(int argc, char **argv)
{
    nrf_raal_continuous_mode_enter();

    return 0;
}

static int
cmd_cont_exit(int argc, char **argv)
{
    nrf_raal_continuous_mode_exit();

    return 0;
}

static const struct shell_cmd raal_commands[] = {
    {
        .sc_cmd = "cont-enter",
        .sc_cmd_func = cmd_cont_enter,
    },
    {
        .sc_cmd = "cont-exit",
        .sc_cmd_func = cmd_cont_exit,
    },
    { },
};

static int
gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
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
    uint8_t own_addr_type;
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    assert(rc == 0);

    name = ble_svc_gap_device_name();

    memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    assert(rc == 0);

    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    assert(rc == 0);
}

static void
on_sync(void)
{
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    start_advertise();
}

int
main(void)
{
    sysinit();

    hal_gpio_init_out(NNRF_RAAL_TIMESLOT_GPIO, 0);

    ble_hs_cfg.sync_cb = on_sync;

    shell_register("raal", raal_commands);
    shell_register_default_module("raal");

    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }
    return 0;
}
