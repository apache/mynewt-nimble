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

#include "syscfg/syscfg.h"
#include "hal/hal_system.h"
#include "hal/hal_debug.h"
#include "nrf.h"
#include "cmsis.h"
#include "mcu/mcu_sim.h"
#include "hal/nrf_clock.h"

/**
 * Function called at startup. Called after BSS and .data initialized but
 * prior to the _start function.
 *
 * NOTE: this function is called by both the bootloader and the application.
 * If you add code here that you do not want executed in either case you need
 * to conditionally compile it using the config variable BOOT_LOADER (will
 * be set to 1 in case of bootloader build)
 *
 */
void
hal_system_init(void)
{
#if MYNEWT_VAL(MCU_DCDC_ENABLED)
    NRF_POWER->DCDCEN = 1;
#endif
}

void
hal_system_reset(void)
{

#if MYNEWT_VAL(HAL_SYSTEM_RESET_CB)
    hal_system_reset_cb();
#endif

    while (1) {
        HAL_DEBUG_BREAK();
        NVIC_SystemReset();
    }
}

int
hal_debugger_connected(void)
{
    return 0;
}

/**
 * hal system clock start
 *
 * Makes sure the LFCLK and/or HFCLK is started.
 */
void
hal_system_clock_start(void)
{
#if MYNEWT_VAL(MCU_LFCLK_SOURCE)
    uint32_t regmsk;
    uint32_t regval;
    uint32_t clksrc;

    regmsk = CLOCK_LFCLKSTAT_STATE_Msk | CLOCK_LFCLKSTAT_SRC_Msk;
    regval = CLOCK_LFCLKSTAT_STATE_Running << CLOCK_LFCLKSTAT_STATE_Pos;

#if MYNEWT_VAL_CHOICE(MCU_LFCLK_SOURCE, LFXO)
    regval |= CLOCK_LFCLKSTAT_SRC_Xtal << CLOCK_LFCLKSTAT_SRC_Pos;
    clksrc = CLOCK_LFCLKSRC_SRC_Xtal;
#elif MYNEWT_VAL_CHOICE(MCU_LFCLK_SOURCE, LFSYNTH)
    regval |= CLOCK_LFCLKSTAT_SRC_Synth << CLOCK_LFCLKSTAT_SRC_Pos;
    clksrc = CLOCK_LFCLKSRC_SRC_Synth;
#elif MYNEWT_VAL_CHOICE(MCU_LFCLK_SOURCE, LFRC)
    regval |= CLOCK_LFCLKSTAT_SRC_RC << CLOCK_LFCLKSTAT_SRC_Pos;
    clksrc = CLOCK_LFCLKSRC_SRC_RC;
#else
    #error Unknown LFCLK source selected
#endif

#if MYNEWT_VAL_CHOICE(MCU_LFCLK_SOURCE, LFSYNTH)
    /* Must turn on HFLCK for synthesized 32768 crystal */
    nrf52_clock_hfxo_request();
#else
    /* Make sure HFCLK is stopped */
    nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_HFCLKSTOP);
#endif

    /* Check if this clock source is already running */
    if ((NRF_CLOCK_regs.LFCLKSTAT & regmsk) != regval) {

        nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_LFCLKSTOP);
        NRF_CLOCK_regs.EVENTS_LFCLKSTARTED = 0;
        NRF_CLOCK_regs.LFCLKSRC = clksrc;
        nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_LFCLKSTART);

        /* Wait here till started! */
//        while (1) {
//            if (NRF_CLOCK_regs.EVENTS_LFCLKSTARTED) {
//                if ((NRF_CLOCK_regs.LFCLKSTAT & regmsk) == regval) {
//                    break;
//                }
//            }
//        }
    }
#endif
}


void*
NRF_RADIO_BASE_FUN(void)
{
    return NULL;
}
