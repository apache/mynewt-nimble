/*
 * Copyright (c) 2017 Oticon A/S
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * CMSIS replacement functions for BabbleSim nRF HW models.
 * Based on Zephyr nrf_bsim boards.
 */

#include <stdbool.h>
#include <stdint.h>
#include "irq_ctrl.h"
#include "NHW_config.h"
#include "bs_tracing.h"
#include "cmsis.h"
#include "mcu/nrf52833_bsim.h"

void
NVIC_SetPendingIRQ(IRQn_Type IRQn)
{
    hw_irq_ctrl_raise_im_from_sw(BSIM_CPU_N, IRQn);
}

void
NVIC_ClearPendingIRQ(IRQn_Type IRQn)
{
    hw_irq_ctrl_clear_irq(BSIM_CPU_N, IRQn);
}

void
NVIC_DisableIRQ(IRQn_Type IRQn)
{
    hw_irq_ctrl_disable_irq(BSIM_CPU_N, IRQn);
}

uint32_t
NVIC_GetPendingIRQ(IRQn_Type IRQn)
{
    return hw_irq_ctrl_is_irq_pending(BSIM_CPU_N, IRQn);
}

void
NVIC_EnableIRQ(IRQn_Type IRQn)
{
    hw_irq_ctrl_enable_irq(BSIM_CPU_N, IRQn);
}

uint32_t
NVIC_GetEnableIRQ(IRQn_Type IRQn)
{
    return hw_irq_ctrl_is_irq_enabled(BSIM_CPU_N, IRQn);
}

void
NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority)
{
    hw_irq_ctrl_prio_set(BSIM_CPU_N, IRQn, priority);
}

uint32_t
NVIC_GetPriority(IRQn_Type IRQn)
{
    return hw_irq_ctrl_get_prio(BSIM_CPU_N, IRQn);
}

void
NVIC_SystemReset(void)
{
    bs_trace_error_time_line("%s called. Exiting\n", __func__);
}

/*
 * Unlocking interrupts shall also do pending context switch (like PendSV on
 * Cortex-M), see os_arch.c.
 */
void
__enable_irq(void)
{
    hw_irq_ctrl_change_lock(BSIM_CPU_N, false);
    bsim_os_arch_ctx_sw_check();
}

void
__disable_irq(void)
{
    hw_irq_ctrl_change_lock(BSIM_CPU_N, true);
}

uint32_t
__get_PRIMASK(void)
{
    return hw_irq_ctrl_get_current_lock(BSIM_CPU_N);
}

void
__set_PRIMASK(uint32_t primask)
{
    hw_irq_ctrl_change_lock(BSIM_CPU_N, primask != 0);
    if (primask == 0) {
        bsim_os_arch_ctx_sw_check();
    }
}

/*
 * Very simple model of the WFE and SEV ARM instructions which seems good
 * enough for the Nordic controller
 */
static bool g_cpu_event_set;

void
__WFE(void)
{
    if (!g_cpu_event_set) {
        bsim_halt_cpu();
    }
    g_cpu_event_set = false;
}

void
__SEV(void)
{
    g_cpu_event_set = true;
}

static bool
irq_pending(void)
{
    int i;

    for (i = 0; i < NHW_INTCTRL_MAX_INTLINES; i++) {
        if (hw_irq_ctrl_is_irq_pending(BSIM_CPU_N, i) &&
            hw_irq_ctrl_is_irq_enabled(BSIM_CPU_N, i)) {
            return true;
        }
    }

    return false;
}

/*
 * WFI wakes up CPU on pending interrupt even if interrupts are locked (e.g.
 * idle task waits for interrupt in critical section) and does not sleep if
 * interrupt is already pending.
 */
void
__WFI(void)
{
    int prev;

    if (irq_pending()) {
        return;
    }

    prev = hw_irq_ctrl_set_wake_even_if_lock(BSIM_CPU_N, true);
    bsim_halt_cpu();
    hw_irq_ctrl_set_wake_even_if_lock(BSIM_CPU_N, prev);
}

/* LDREX/STREX/CLREX lock state */
static bool g_ex_lock;

static bool
strex_lock_model(void)
{
    if (!g_ex_lock) {
        return true;
    }

    g_ex_lock = false;
    return false;
}

void
nrfbsim_clear_excl_access(void)
{
    g_ex_lock = false;
}

uint32_t
__STREXB(uint8_t value, volatile uint8_t *ptr)
{
    if (strex_lock_model()) {
        return 1;
    }

    *ptr = value;
    return 0;
}

uint32_t
__STREXH(uint16_t value, volatile uint16_t *ptr)
{
    if (strex_lock_model()) {
        return 1;
    }

    *ptr = value;
    return 0;
}

uint32_t
__STREXW(uint32_t value, volatile uint32_t *ptr)
{
    if (strex_lock_model()) {
        return 1;
    }

    *ptr = value;
    return 0;
}

uint8_t
__LDREXB(volatile uint8_t *ptr)
{
    g_ex_lock = true;
    return *ptr;
}

uint16_t
__LDREXH(volatile uint16_t *ptr)
{
    g_ex_lock = true;
    return *ptr;
}

uint32_t
__LDREXW(volatile uint32_t *ptr)
{
    g_ex_lock = true;
    return *ptr;
}

void
__CLREX(void)
{
    g_ex_lock = false;
}
