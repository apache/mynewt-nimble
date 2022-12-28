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
#include <syscfg/syscfg.h>
#include <nrf21540/nrf21540.h>
#include <hal/hal_gpio.h>
#include <controller/ble_fem.h>

static void
nrf21540_pdn_set(int state)
{
    int pin;

    pin = MYNEWT_VAL(NRF21540_PIN_PDN);
    hal_gpio_write(pin, state);
}

int
nrf21540_mode_set(uint8_t mode)
{
#ifdef MYNEWT_VAL_NRF21540_PIN_MODE
    int pin;

    pin = MYNEWT_VAL(NRF21540_PIN_MODE);
    hal_gpio_init_out(pin, mode);

    return 0;
#else
    return -1;
#endif
}

void
ble_fem_pa_init(void)
{
    nrf21540_pdn_set(0);
}

void
ble_fem_pa_enable(void)
{
    nrf21540_pdn_set(1);
}

void
ble_fem_pa_disable(void)
{
    nrf21540_pdn_set(0);
}

void
ble_fem_lna_init(void)
{
}

void
ble_fem_lna_enable(void)
{
    nrf21540_pdn_set(1);
}

void
ble_fem_lna_disable(void)
{
    nrf21540_pdn_set(0);
}

int
ble_fem_antenna(uint8_t port)
{
#ifdef MYNEWT_VAL_NRF21540_PIN_ANT_SEL
    int pin;

    pin = MYNEWT_VAL(NRF21540_PIN_ANT_SEL);
    switch (port) {
    case 0:
    case 1:
        hal_gpio_write(pin, 0);
        break;
    case 2:
        hal_gpio_write(pin, 1);
        break;
    default:
        return -1;
    }

    return 0;
#else
    return -1;
#endif
}

void
nrf21540_init(void)
{
    int pin;

    pin = MYNEWT_VAL(NRF21540_PIN_PDN);
    assert(pin >= 0);
    hal_gpio_init_out(pin, 0);

#ifdef MYNEWT_VAL_NRF21540_PIN_ANT_SEL
    pin = MYNEWT_VAL(NRF21540_PIN_ANT_SEL);
    assert(pin >= 0);
    switch (MYNEWT_VAL(NRF21540_ANTENNA_PORT)) {
    case 1:
        hal_gpio_init_out(pin, 0);
        break;
    case 2:
        hal_gpio_init_out(pin, 1);
        break;
    default:
        assert(0);
    }
#endif

#ifdef MYNEWT_VAL_NRF21540_PIN_MODE
    pin = MYNEWT_VAL(NRF21540_PIN_MODE);
    assert(pin >= 0);
    if (MYNEWT_VAL_CHOICE(NRF21540_TX_GAIN_PRESET, POUTB)) {
        hal_gpio_init_out(pin, 1);
    } else {
        hal_gpio_init_out(pin, 0);
    }
#endif
}
