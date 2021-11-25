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
#include "os/mynewt.h"
#include "hal/hal_os_tick.h"
#include "nrf.h"
#include "mcu/cmsis_nvic.h"
#include "mcu/mcu_sim.h"
#include <time_machine.h>
#include <NRF_HWLowL.h>
#include <NRF_HW_model_top.h>
#include <NRF_RTC.h>
#include <hal/nrf_rtc.h>

/* The OS scheduler requires a low-frequency timer. */
#if MYNEWT_VAL(OS_SCHEDULING) && !MYNEWT_VAL(MCU_LFCLK_SOURCE)
    #error The OS scheduler requires a low-frequency timer; configure MCU_LFCLK_SOURCE
#endif

#define RTC_FREQ            32768       /* in Hz */
#define OS_TICK_TIMER       NRF_RTC1
#define OS_TICK_IRQ         RTC1_IRQn
#define OS_TICK_CMPREG      3   /* generate timer interrupt */
#define RTC_COMPARE_INT_MASK(ccreg) (1UL << ((ccreg) + 16))

struct hal_os_tick
{
    int ticks_per_ostick;
    os_time_t max_idle_ticks;
    uint32_t lastocmp;
};

struct hal_os_tick g_hal_os_tick;

/*
 * Implement (x - y) where the range of both 'x' and 'y' is limited to 24-bits.
 *
 * For example:
 *
 * sub24(0, 0xffffff) = 1
 * sub24(0xffffff, 0xfffffe) = 1
 * sub24(0xffffff, 0) = -1
 * sub24(0x7fffff, 0) = 8388607
 * sub24(0x800000, 0) = -8388608
 */
static inline int
sub24(uint32_t x, uint32_t y)
{
    int result;

    assert(x <= 0xffffff);
    assert(y <= 0xffffff);

    result = x - y;
    if (result & 0x800000) {
        return (result | 0xff800000);
    } else {
        return (result & 0x007fffff);
    }
}

static inline uint32_t
nrf52_os_tick_counter(void)
{
    return nrf_rtc_counter_get(OS_TICK_TIMER);
}

static inline void
nrf52_os_tick_set_ocmp(uint32_t ocmp)
{
    int delta;
    uint32_t counter;

    OS_ASSERT_CRITICAL();
    while (1) {
        ocmp &= 0xffffff;
        nrf_rtc_cc_set(OS_TICK_TIMER, OS_TICK_CMPREG, ocmp);
        counter = nrf52_os_tick_counter();
        /*
         * From nRF52 Product specification
         *
         * - If Counter is 'N' writing (N) or (N + 1) to CC register
         *   may not trigger a compare event.
         *
         * - If Counter is 'N' writing (N + 2) to CC register is guaranteed
         *   to trigger a compare event at 'N + 2'.
         */
        delta = sub24(ocmp, counter);
        if (delta > 2) {
            break;
        }
        ocmp += g_hal_os_tick.ticks_per_ostick;
    }
}

static void
nrf52_timer_handler(void)
{
    int delta;
    int ticks;
    os_sr_t sr;
    uint32_t counter;

    os_trace_isr_enter();
    OS_ENTER_CRITICAL(sr);

    /* Calculate elapsed ticks and advance OS time. */

    counter = nrf52_os_tick_counter();
    delta = sub24(counter, g_hal_os_tick.lastocmp);
    ticks = delta / g_hal_os_tick.ticks_per_ostick;
    os_time_advance(ticks);

    /* Clear timer interrupt */
    OS_TICK_TIMER->EVENTS_COMPARE[OS_TICK_CMPREG] = 0;

    /* Update the time associated with the most recent tick */
    g_hal_os_tick.lastocmp = (g_hal_os_tick.lastocmp +
        (ticks * g_hal_os_tick.ticks_per_ostick)) & 0xffffff;

    /* Update the output compare to interrupt at the next tick */
    nrf52_os_tick_set_ocmp(g_hal_os_tick.lastocmp + g_hal_os_tick.ticks_per_ostick);

    OS_EXIT_CRITICAL(sr);
    os_trace_isr_exit();
}

/* Wait For Interrupt */
void
__WFI(void)
{
    while (hw_irq_ctrl_get_irq_status() == 0) {
        tm_tick();
    }
}

void
os_tick_idle(os_time_t ticks)
{
    uint32_t ocmp;

    OS_ASSERT_CRITICAL();

    if (ticks > 0) {
        /*
         * Enter tickless regime during long idle durations.
         */
        if (ticks > g_hal_os_tick.max_idle_ticks) {
            ticks = g_hal_os_tick.max_idle_ticks;
        }
        ocmp = g_hal_os_tick.lastocmp + (ticks*g_hal_os_tick.ticks_per_ostick);
        nrf52_os_tick_set_ocmp(ocmp);
    }

    __WFI();

    if (ticks > 0) {
        /*
         * Update OS time before anything else when coming out of
         * the tickless regime.
         */
        nrf52_timer_handler();
    }
}

extern void nrf_rtc_regw_sideeffects(int i);

void
os_tick_init(uint32_t os_ticks_per_sec, int prio)
{
    uint32_t sr;

    assert(RTC_FREQ % os_ticks_per_sec == 0);

    g_hal_os_tick.lastocmp = 0;
    g_hal_os_tick.ticks_per_ostick = RTC_FREQ / os_ticks_per_sec;

    /*
     * The maximum number of OS ticks allowed to elapse during idle is
     * limited to 1/4th the number of timer ticks before the 24-bit counter
     * rolls over.
     */
    g_hal_os_tick.max_idle_ticks = (1UL << 22) / g_hal_os_tick.ticks_per_ostick;

    /* disable interrupts */
    OS_ENTER_CRITICAL(sr);

    /* Set isr in vector table and enable interrupt */
    NVIC_SetPriority(OS_TICK_IRQ, prio);
    NVIC_SetVector(OS_TICK_IRQ, (uint32_t)nrf52_timer_handler);
    NVIC_EnableIRQ(OS_TICK_IRQ);

    /*
     * Program the OS_TICK_TIMER to operate at 32KHz and trigger an output
     * compare interrupt at a rate of 'os_ticks_per_sec'.
     */
    nrf_rtc_task_trigger(OS_TICK_TIMER, NRF_RTC_TASK_STOP);
    nrf_rtc_task_trigger(OS_TICK_TIMER, NRF_RTC_TASK_CLEAR);
    nrf_rtc_event_disable(OS_TICK_TIMER, 0xffffffff);
    nrf_rtc_int_disable(OS_TICK_TIMER, 0xffffffff);
    nrf_rtc_int_enable(OS_TICK_TIMER, RTC_COMPARE_INT_MASK(OS_TICK_CMPREG));

    OS_TICK_TIMER->EVENTS_COMPARE[OS_TICK_CMPREG] = 0;
    nrf_rtc_cc_set(OS_TICK_TIMER, OS_TICK_CMPREG, g_hal_os_tick.ticks_per_ostick);

    nrf_rtc_task_trigger(OS_TICK_TIMER, NRF_RTC_TASK_START);

    OS_EXIT_CRITICAL(sr);
}
