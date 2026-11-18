/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Interface between native simulator runner and Mynewt (embedded CPU SW).
 * Based on Zephyr nrf_bsim boards and inf_clock SoC.
 */

#include <stdbool.h>
#include "nsi_cpu_if.h"
#include "nce_if.h"
#include "irq_ctrl.h"
#include "phy_sync_ctrl.h"
#include "os/os.h"
#include "mcu/nrf52833_bsim.h"

extern int mynewt_main(int argc, char **argv);
extern void bsim_os_arch_init(void);
extern void bsim_os_arch_cleanup(void);

static void *g_nce_st;

void
bsim_halt_cpu(void)
{
    nce_halt_cpu(g_nce_st);
    bsim_irq_handler();
}

static void
bsim_cpu_start(void)
{
    os_init(mynewt_main);
    os_start();
}

NATIVE_SIMULATOR_IF void
nsif_cpu0_pre_cmdline_hooks(void)
{
}

NATIVE_SIMULATOR_IF void
nsif_cpu0_pre_hw_init_hooks(void)
{
    phy_sync_ctrl_connect_to_2G4_phy();
    phy_sync_ctrl_pre_boot2();
}

NATIVE_SIMULATOR_IF void
nsif_cpu0_boot(void)
{
    phy_sync_ctrl_pre_boot3();

    g_nce_st = nce_init();
    bsim_os_arch_init();

    /* Returns once CPU is halted for the first time */
    nce_boot_cpu(g_nce_st, bsim_cpu_start);
}

NATIVE_SIMULATOR_IF int
nsif_cpu0_cleanup(void)
{
    /* This may not return, but in such case it will be called again */
    nce_terminate(g_nce_st);
    bsim_os_arch_cleanup();

    return 0;
}

NATIVE_SIMULATOR_IF void
nsif_cpu0_irq_raised(void)
{
    nce_wake_cpu(g_nce_st);
}

NATIVE_SIMULATOR_IF void
nsif_cpu0_irq_raised_from_sw(void)
{
    if (hw_irq_ctrl_get_highest_prio_irq(BSIM_CPU_N) != -1) {
        bsim_irq_handler();
    }
}
