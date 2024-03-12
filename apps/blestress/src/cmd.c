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
#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include "os/mynewt.h"
#include "bsp/bsp.h"
#include "hal/hal_gpio.h"

#include "nimble/ble.h"
#include "nimble/nimble_opt.h"

#include "console/console.h"
#include "shell/shell.h"

#include "cmd.h"

struct os_sem stress_main_sem;
struct os_sem tx_stress_start_sem;

static int
device_role_rx(int argc, char **argv)
{
    hal_gpio_init_out(LED_2, 1);
    hal_gpio_toggle(LED_2);

    console_printf("RX device");
    shell_register_default_module("rx_cmd");

    console_printf("\033[1;36mRX device\033[0m\n");
    console_printf("Type start_test num=(1-15) to start a specific test "
                   "case\n");

    return 0;
}

static int
device_role_tx(int argc, char **argv)
{
    console_printf("\033[1;36mTX device\033[0m\n");

    os_sem_release(&tx_stress_start_sem);

    return 0;
}

#if MYNEWT_VAL(SHELL_CMD_HELP)
static const struct shell_cmd_help tx_stress_help = {
    .summary = "TX Device: advertiser",
    .usage = NULL,
    .params = NULL,
};

static const struct shell_cmd_help rx_stress_help = {
    .summary = "RX Device: scanner",
    .usage = NULL,
    .params = NULL,
};
#endif

static const struct shell_cmd stress_cmd[] = {
    {
        .sc_cmd = "rx",
        .sc_cmd_func = device_role_rx,
#if MYNEWT_VAL(SHELL_CMD_HELP)
        .help = &rx_stress_help,
#endif
    },
    {
        .sc_cmd = "tx",
        .sc_cmd_func = device_role_tx,
#if MYNEWT_VAL(SHELL_CMD_HELP)
        .help = &tx_stress_help,
#endif
    },
};

void
cmd_stress_init(void)
{
    shell_register("blestress", stress_cmd);
    shell_register_default_module("blestress");
}
