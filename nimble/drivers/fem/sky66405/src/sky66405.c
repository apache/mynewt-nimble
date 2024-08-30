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

#include <syscfg/syscfg.h>
#include <hal/hal_gpio.h>
#include <controller/ble_fem.h>
#include <sky66405/sky66405.h>

static struct {
    uint8_t rx_bypass : 1;
    uint8_t tx_bypass : 1;
} sky66405_config = {
    .rx_bypass = MYNEWT_VAL(SKY66405_RX_BYPASS),
    .tx_bypass = MYNEWT_VAL(SKY66405_TX_BYPASS),
};

static void
sky66405_bypass(uint8_t enabled)
{
    /* this is called only if bypass is enabled which means CPS PIN is
     * correctly set and there is no need to check it here.
     */
    hal_gpio_write(MYNEWT_VAL(SKY66405_PIN_CPS), enabled);
}

void
ble_fem_pa_init(void)
{
    sky66405_tx_bypass(0);
}

void
ble_fem_pa_enable(void)
{
    if (sky66405_config.tx_bypass) {
        sky66405_bypass(1);
    }
}

void
ble_fem_pa_disable(void)
{
    if (sky66405_config.tx_bypass) {
        sky66405_bypass(0);
    }
}

void
ble_fem_lna_init(void)
{
    sky66405_rx_bypass(0);
}

void
ble_fem_lna_enable(void)
{
    if (sky66405_config.rx_bypass) {
        sky66405_bypass(1);
    }
}

void
ble_fem_lna_disable(void)
{
    if (sky66405_config.rx_bypass) {
        sky66405_bypass(0);
    }
}

void
sky66405_rx_bypass(uint8_t enabled)
{
    int pin = MYNEWT_VAL(SKY66405_PIN_CPS);

    if (pin >= 0) {
        sky66405_config.rx_bypass = enabled;
        sky66405_bypass(enabled);
    }
}

void
sky66405_tx_bypass(uint8_t enabled)
{
    int pin = MYNEWT_VAL(SKY66405_PIN_CPS);

    if (pin >= 0) {
        sky66405_config.tx_bypass = enabled;
        sky66405_bypass(enabled);
    }
}

void
sky66405_init(void)
{
    int pin;

    /* Disable bypass, we'll enable it when needed */
    pin = MYNEWT_VAL(SKY66405_PIN_CPS);
    if (pin >= 0) {
        hal_gpio_init_out(pin, 0);
    }
}
