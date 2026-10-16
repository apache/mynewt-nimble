/*
 * Copyright (c) 2017 Oticon A/S
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Interrupt handling for BabbleSim nRF HW models.
 * Based on Zephyr nrf_bsim boards.
 */

#include <stdint.h>
#include "irq_ctrl.h"
#include "bs_tracing.h"
#include "NHW_config.h"
#include "cmsis.h"
#include "mcu/cmsis_nvic.h"
#include "mcu/nrf52833_bsim.h"

static uint32_t g_vectors[NHW_INTCTRL_MAX_INTLINES];
static int g_currently_running_irq = -1;

void
NVIC_SetVector(IRQn_Type IRQn, uint32_t vector)
{
    if ((IRQn < 0) || (IRQn >= NHW_INTCTRL_MAX_INTLINES)) {
        bs_trace_error_time_line("Attempted to set vector for non existent "
                                 "interrupt %d\n",
                                 IRQn);
        return;
    }

    g_vectors[IRQn] = vector;
}

uint32_t
NVIC_GetVector(IRQn_Type IRQn)
{
    if ((IRQn < 0) || (IRQn >= NHW_INTCTRL_MAX_INTLINES)) {
        return 0;
    }

    return g_vectors[IRQn];
}

int
os_arch_in_isr(void)
{
    return g_currently_running_irq >= 0;
}

static void
vector_to_irq(int irq_nbr)
{
    bs_trace_raw_time(6, "Vectoring to irq %i (%s)\n", irq_nbr,
                      hw_irq_ctrl_get_name(BSIM_CPU_N, irq_nbr));

    if (g_vectors[irq_nbr] == 0) {
        bs_trace_error_time_line("Received irq %i without a registered "
                                 "handler\n",
                                 irq_nbr);
        return;
    }

    ((void (*)(void))g_vectors[irq_nbr])();

    bs_trace_raw_time(7, "Irq %i (%s) ended\n", irq_nbr,
                      hw_irq_ctrl_get_name(BSIM_CPU_N, irq_nbr));
}

void
bsim_irq_handler(void)
{
    int irq_nbr;
    int last_running_prio;
    int last_running_irq;

    /* "spurious" wakes can happen with interrupts locked */
    if (hw_irq_ctrl_get_current_lock(BSIM_CPU_N)) {
        return;
    }

    irq_nbr = hw_irq_ctrl_get_highest_prio_irq(BSIM_CPU_N);
    if (irq_nbr == -1) {
        /* Phony interrupt (e.g. during busy wait) */
        return;
    }

    do {
        last_running_prio = hw_irq_ctrl_get_cur_prio(BSIM_CPU_N);
        last_running_irq = g_currently_running_irq;

        hw_irq_ctrl_set_cur_prio(BSIM_CPU_N,
                                 hw_irq_ctrl_get_prio(BSIM_CPU_N, irq_nbr));
        hw_irq_ctrl_clear_irq(BSIM_CPU_N, irq_nbr);

        g_currently_running_irq = irq_nbr;
        vector_to_irq(irq_nbr);
        nrfbsim_clear_excl_access();
        g_currently_running_irq = last_running_irq;

        hw_irq_ctrl_reeval_level_irq(BSIM_CPU_N, irq_nbr);
        hw_irq_ctrl_set_cur_prio(BSIM_CPU_N, last_running_prio);
    } while ((irq_nbr = hw_irq_ctrl_get_highest_prio_irq(BSIM_CPU_N)) != -1);

    /* Context switch requested by ISR shall happen on exit from last ISR */
    bsim_os_arch_ctx_sw_check();
}
