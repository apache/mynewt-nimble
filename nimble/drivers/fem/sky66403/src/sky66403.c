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
#include <sky66403/sky66403.h>
#include <syscfg/syscfg.h>
#include <hal/hal_gpio.h>
#include <controller/ble_fem.h>

static struct {
    uint8_t rx_bypass : 1;
    uint8_t tx_bypass : 1;
} sky66403_config = {
    .rx_bypass = MYNEWT_VAL(SKY66403_RX_BYPASS),
    .tx_bypass = MYNEWT_VAL(SKY66403_TX_BYPASS),
};

#if !MYNEWT_VAL_CHOICE(SKY66403_ANTENNA_PORT, runtime)
static uint8_t
sky66403_default_antenna_get(void)
{
    if (MYNEWT_VAL_CHOICE(SKY66403_ANTENNA_PORT, ANT2)) {
        return 2;
    } else {
        return 1;
    }
}
#endif

static void
sky66403_bypass(uint8_t enabled)
{
    /* this is called only if bypass is enabled which means CPS PIN is
     * correctly set and there is no need to check it here.
     */
    hal_gpio_write(MYNEWT_VAL(SKY66403_PIN_CPS), enabled);
}

void
ble_fem_pa_init(void)
{
    sky66403_tx_linear_mode(MYNEWT_VAL(SKY66403_TX_LINEAR_MODE));
    sky66403_tx_bypass(0);
#if MYNEWT_VAL(BLE_FEM_ANTENNA)
    ble_fem_antenna(0);
#endif
}

void
ble_fem_pa_enable(void)
{
    if (sky66403_config.tx_bypass) {
        sky66403_bypass(1);
    }
}

void
ble_fem_pa_disable(void)
{
    if (sky66403_config.tx_bypass) {
        sky66403_bypass(0);
    }
}

void
ble_fem_lna_init(void)
{
    sky66403_rx_bypass(0);
#if MYNEWT_VAL(BLE_FEM_ANTENNA)
    ble_fem_antenna(0);
#endif
}

void
ble_fem_lna_enable(void)
{
    if (sky66403_config.rx_bypass) {
        sky66403_bypass(1);
    }
}

void
ble_fem_lna_disable(void)
{
    if (sky66403_config.rx_bypass) {
        sky66403_bypass(0);
    }
}

void
sky66403_tx_linear_mode(uint8_t enabled)
{
    int pin = MYNEWT_VAL(SKY66403_PIN_CHL);

    if (pin >= 0) {
        hal_gpio_write(pin, enabled);
    }
}

int
ble_fem_antenna(uint8_t port)
{
    int pin = MYNEWT_VAL(SKY66403_PIN_SEL);
    uint8_t ant;

    if (pin >= 0) {
        switch (port) {
        case 0:
            ant = sky66403_default_antenna_get();
            assert(ant == 1 || ant == 2);
            return ble_fem_antenna(ant);
        case 1:
            hal_gpio_write(pin, 0);
            break;
        case 2:
            hal_gpio_write(pin, 1);
            break;
        default:
            return -1;
        }
    }

    return 0;
}

void
sky66403_rx_bypass(uint8_t enabled)
{
    int pin = MYNEWT_VAL(SKY66403_PIN_CPS);

    if (pin >= 0) {
        sky66403_config.rx_bypass = enabled;
        sky66403_bypass(enabled);
    }
}

void
sky66403_tx_bypass(uint8_t enabled)
{
    int pin = MYNEWT_VAL(SKY66403_PIN_CPS);

    if (pin >= 0) {
        sky66403_config.tx_bypass = enabled;
        sky66403_bypass(enabled);
    }
}

void
sky66403_init(void)
{
    int pin;

    /* Use CRX and CTX to enable sleep mode */
    pin = MYNEWT_VAL(SKY66403_PIN_CSD);
    if (pin >= 0) {
        hal_gpio_init_out(pin, 1);
    }

    /* Set default tx power mode */
    pin = MYNEWT_VAL(SKY66403_PIN_CHL);
    if (pin >= 0) {
        hal_gpio_init_out(pin, MYNEWT_VAL(SKY66403_TX_LINEAR_MODE));
    }

    /* Disable bypass, we'll enable it when needed */
    pin = MYNEWT_VAL(SKY66403_PIN_CPS);
    if (pin >= 0) {
        hal_gpio_init_out(pin, 0);
    }

    /* configure default antenna */
    pin = MYNEWT_VAL(SKY66403_PIN_SEL);
    if (pin >= 0) {
        switch (sky66403_default_antenna_get()) {
        case 1:
            hal_gpio_init_out(pin, 0);
            break;
        case 2:
            hal_gpio_init_out(pin, 1);
            break;
        default:
            assert(0);
        }
    }
}
