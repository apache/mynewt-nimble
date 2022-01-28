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
#include <stdint.h>
#include "syscfg/syscfg.h"
#include "hal/hal_gpio.h"
#include "controller/ble_ll_plna.h"

#define NO_BYPASS \
        ((MYNEWT_VAL(SKY66112_TX_BYPASS) == 0) && \
         (MYNEWT_VAL(SKY66112_RX_BYPASS) == 0))

static void
sky66112_bypass(uint8_t enabled)
{
    if (NO_BYPASS) {
        return;
    }

    hal_gpio_write(MYNEWT_VAL(SKY66112_PIN_CPS), enabled);
}

void
ble_ll_plna_pa_init(void)
{
    /* Nothing to do here */
}

void
ble_ll_plna_pa_enable(void)
{
    if (!MYNEWT_VAL(SKY66112_TX_BYPASS)) {
        sky66112_bypass(0);
    }
}

void
ble_ll_plna_pa_disable(void)
{
    if (!MYNEWT_VAL(SKY66112_TX_BYPASS)) {
        sky66112_bypass(1);
    }
}

void
ble_ll_plna_lna_init(void)
{
    /* Nothing to do here */
}

void
ble_ll_plna_lna_enable(void)
{
    if (!MYNEWT_VAL(SKY66112_RX_BYPASS)) {
        sky66112_bypass(0);
    }
}

void
ble_ll_plna_lna_disable(void)
{
    if (!MYNEWT_VAL(SKY66112_RX_BYPASS)) {
        sky66112_bypass(1);
    }
}

void
sky66112_init(void)
{
    int pin;

    /* Use CRX and CTX to enable sleep mode */
    pin = MYNEWT_VAL(SKY66112_PIN_CSD);
    if (pin >= 0) {
        hal_gpio_init_out(pin, 1);
    }

    pin = MYNEWT_VAL(SKY66112_PIN_CPS);
    if (NO_BYPASS) {
        /* Disable bypass */
        if (pin >= 0) {
            hal_gpio_init_out(pin, 0);
        }
    } else {
        /* Enable bypass, we'll disable it when needed */
        assert(pin >= 0);
        hal_gpio_init_out(pin, 1);
    }

    pin = MYNEWT_VAL(SKY66112_PIN_CHL);
    if (pin >= 0) {
        hal_gpio_init_out(pin, MYNEWT_VAL(SKY66112_TX_HP_MODE));
    }

    /* Select ANT1 */
    pin = MYNEWT_VAL(SKY66112_PIN_SEL);
    if (pin >= 0) {
        hal_gpio_init_out(pin, 0);
    }
}
