/*
 * Copyright (c) 2017 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SW side of the IRQ handling
 */

#include <stdint.h>
#include "irq_ctrl.h"
#include "irq_sources.h"
#include "os/sim.h"

static int currently_running_irq = -1;
extern void (* const systemVectors[256])(void);

/**
 * When an interrupt is raised, this function is called to handle it and, if
 * needed, swap to a re-enabled thread
 *
 * Note that even that this function is executing in a Zephyr thread,  it is
 * effectively the model of the interrupt controller passing context to the IRQ
 * handler and therefore its priority handling
 */

void posix_interrupt_raised(void)
{
    uint64_t irq_lock;
    int irq_nbr;

    irq_lock = hw_irq_ctrl_get_current_lock();

    if (irq_lock) {
        /* "spurious" wakes can happen with interrupts locked */
        return;
    }

    while ((irq_nbr = hw_irq_ctrl_get_highest_prio_irq()) != -1) {
        int last_current_running_prio = hw_irq_ctrl_get_cur_prio();
        int last_running_irq = currently_running_irq;

        hw_irq_ctrl_set_cur_prio(hw_irq_ctrl_get_prio(irq_nbr));
        hw_irq_ctrl_clear_irq(irq_nbr);

        currently_running_irq = irq_nbr;
        systemVectors[irq_nbr + 16]();
        currently_running_irq = last_running_irq;

        hw_irq_ctrl_set_cur_prio(last_current_running_prio);
    }
}

/**
 * Thru this function the IRQ controller can raise an immediate  interrupt which
 * will interrupt the SW itself
 * (this function should only be called from the HW model code, from SW threads)
 */
void posix_irq_handler_im_from_sw(void)
{
	/*
	 * if a higher priority interrupt than the possibly currently running is
	 * pending we go immediately into irq_handler() to vector into its
	 * handler
	 */
	if (hw_irq_ctrl_get_highest_prio_irq() != -1) {
	    posix_interrupt_raised();
	}
}
