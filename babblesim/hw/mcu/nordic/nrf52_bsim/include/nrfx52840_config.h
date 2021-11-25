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

#ifndef NRFX52840_CONFIG_H__
#define NRFX52840_CONFIG_H__

#include "syscfg/syscfg.h"

#ifndef NRFX_CLOCK_ENABLED
#define NRFX_CLOCK_ENABLED 0
#endif

#ifndef NRFX_COMP_ENABLED
#define NRFX_COMP_ENABLED 0
#endif

#ifndef NRFX_GPIOTE_ENABLED
#define NRFX_GPIOTE_ENABLED 0
#endif

#ifndef NRFX_I2S_ENABLED
#define NRFX_I2S_ENABLED 0
#endif

#ifndef NRFX_LPCOMP_ENABLED
#define NRFX_LPCOMP_ENABLED 0
#endif

#ifndef NRFX_PDM_ENABLED
#define NRFX_PDM_ENABLED 0
#endif

#ifndef NRFX_POWER_ENABLED
#define NRFX_POWER_ENABLED 0
#endif

#ifndef NRFX_PPI_ENABLED
#define NRFX_PPI_ENABLED 0
#endif

#ifndef NRFX_PRS_ENABLDE
#define NRFX_PRS_ENABLDE 0
#endif

#ifndef NRFX_QDEC_ENABLED
#define NRFX_QDEC_ENABLED 0
#endif

#ifndef NRFX_RNG_ENABLED
#define NRFX_RNG_ENABLED 0
#endif

#ifndef NRFX_RTC_ENABLED
#define NRFX_RTC_ENABLED 0
#endif

#ifndef NRFX_SPIM_ENABLED
#define NRFX_SPIM_ENABLED 0
#endif

#ifndef NRFX_SPIS_ENABLED
#define NRFX_SPIS_ENABLED 0
#endif

#ifndef NRFX_SPI_ENABLED
#define NRFX_SPI_ENABLED 0
#endif

#ifndef NRFX_SWI_ENABLED
#define NRFX_SWI_ENABLED 0
#endif

#ifndef NRFX_SYSTICK_ENABLED
#define NRFX_SYSTICK_ENABLED 0
#endif

#ifndef NRFX_TIMER_ENABLED
#define NRFX_TIMER_ENABLED 0
#endif

#ifndef NRFX_TWIM_ENABLED
#define NRFX_TWIM_ENABLED 0
#endif

#ifndef NRFX_TWIS_ENABLED
#define NRFX_TWIS_ENABLED 0
#endif

#ifndef NRFX_TWI_ENABLED
#define NRFX_TWI_ENABLED 0
#endif

#ifndef NRFX_UARTE_ENABLED
#define NRFX_UARTE_ENABLED 0
#endif

#ifndef NRFX_UART_ENABLED
#define NRFX_UART_ENABLED 0
#endif

#ifndef NRFX_WDT_ENABLED
#define NRFX_WDT_ENABLED 0
#endif

#if MYNEWT_VAL(ADC_0)
#define NRFX_SAADC_ENABLED 1
#define NRFX_SAADC_CONFIG_RESOLUTION 1
#define NRFX_SAADC_CONFIG_OVERSAMPLE 0
#define NRFX_SAADC_CONFIG_LP_MODE 0
#define NRFX_SAADC_CONFIG_IRQ_PRIORITY 7
#endif

#if (MYNEWT_VAL(PWM_0) || MYNEWT_VAL(PWM_1) || MYNEWT_VAL(PWM_2) || MYNEWT_VAL(PWM_3))
#define NRFX_PWM_ENABLED 1
#define NRFX_PWM_DEFAULT_CONFIG_OUT0_PIN 31
#define NRFX_PWM_DEFAULT_CONFIG_OUT1_PIN 31
#define NRFX_PWM_DEFAULT_CONFIG_OUT2_PIN 31
#define NRFX_PWM_DEFAULT_CONFIG_OUT3_PIN 31
#define NRFX_PWM_DEFAULT_CONFIG_BASE_CLOCK 4
#define NRFX_PWM_DEFAULT_CONFIG_COUNT_MODE 0
#define NRFX_PWM_DEFAULT_CONFIG_TOP_VALUE 1000
#define NRFX_PWM_DEFAULT_CONFIG_LOAD_MODE 0
#define NRFX_PWM_DEFAULT_CONFIG_STEP_MODE 0
#define NRFX_PWM_DEFAULT_CONFIG_IRQ_PRIORITY 7
#endif

#if MYNEWT_VAL(PWM_0)
#define NRFX_PWM0_ENABLED 1
#endif

#if MYNEWT_VAL(PWM_1)
#define NRFX_PWM1_ENABLED 1
#endif

#if MYNEWT_VAL(PWM_2)
#define NRFX_PWM2_ENABLED 1
#endif

#if MYNEWT_VAL(PWM_3)
#define NRFX_PWM3_ENABLED 1
#endif

#endif // NRFX52840_CONFIG_H__
