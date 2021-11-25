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

#ifndef H_NRF52_HAL_
#define H_NRF52_HAL_

#include "cmsis.h"

#ifdef __cplusplus
 extern "C" {
#endif

/* Helper functions to enable/disable interrupts. */
#define __HAL_DISABLE_INTERRUPTS(x)                     \
    do {                                                \
        x = __get_PRIMASK();                            \
        __disable_irq();                                \
    } while(0);

#define __HAL_ENABLE_INTERRUPTS(x)                      \
    do {                                                \
        if (!x) {                                       \
            __enable_irq();                             \
        }                                               \
    } while(0);

struct nrf52_uart_cfg {
    int8_t suc_pin_tx;                          /* pins for IO */
    int8_t suc_pin_rx;
    int8_t suc_pin_rts;
    int8_t suc_pin_cts;
};
const struct nrf52_uart_cfg *bsp_uart_config(void);

struct nrf52_hal_i2c_cfg {
    int scl_pin;
    int sda_pin;
    uint32_t i2c_frequency;
};
struct hal_flash;
extern const struct hal_flash nrf52k_flash_dev;
extern const struct hal_flash nrf52k_qspi_dev;

/* SPI configuration (used for both master and slave) */
struct nrf52_hal_spi_cfg {
    uint8_t sck_pin;
    uint8_t mosi_pin;
    uint8_t miso_pin;
    uint8_t ss_pin;
};

/*
 * GPIO pin mapping
 *
 * The logical GPIO pin numbers (0 to N) are mapped to ports in the following
 * manner:
 *  pins 0 - 31: Port 0
 *  pins 32 - 48: Port 1.
 *
 *  The nrf52832 has only one port with 32 pins. The nrf52840 has 48 pins and
 *  uses two ports.
 *
 *  NOTE: in order to save code space, there is no checking done to see if the
 *  user specifies a pin that is not used by the processor. If an invalid pin
 *  number is used unexpected and/or erroneous behavior will result.
 */
#if defined(NRF52832_XXAA) || defined(NRF52810_XXAA) || defined(NRF52811_XXAA)
#define HAL_GPIO_INDEX(pin)     (pin)
#define HAL_GPIO_PORT(pin)      (NRF_P0)
#define HAL_GPIO_MASK(pin)      (1 << pin)
#define HAL_GPIOTE_PIN_MASK     GPIOTE_CONFIG_PSEL_Msk
#endif

#ifdef NRF52840_XXAA
#define HAL_GPIO_INDEX(pin)     ((pin) & 0x1F)
#define HAL_GPIO_PORT(pin)      ((pin) > 31 ? NRF_P1 : NRF_P0)
#define HAL_GPIO_MASK(pin)      (1 << HAL_GPIO_INDEX(pin))
#define HAL_GPIOTE_PIN_MASK     (0x3FUL << GPIOTE_CONFIG_PSEL_Pos)
#endif

#ifdef __cplusplus
}
#endif

#endif  /* H_NRF52_HAL_ */
