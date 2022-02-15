/*
 * Copyright (c) 2017-2018 Oticon A/S
 * Copyright (c) 2021 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <unistd.h>
#include "NRF_HW_model_top.h"
#include "NRF_HWLowL.h"
#include "bs_tracing.h"
#include "bs_symbols.h"
#include "bs_types.h"
#include "bs_rand_main.h"
#include "bs_pc_backchannel.h"
#include "bs_dump_files.h"
#include "argparse.h"
#include "time_machine.h"
#include "os/mynewt.h"
#include <stdio.h>
#include "os/sim.h"

uint global_device_nbr;
struct nrf52_bsim_args_t *args;

void
bst_tick(bs_time_t time)
{
    return;
}

uint8_t
inner_main_clean_up(int exit_code)
{
    hwll_terminate_simulation();
    nrf_hw_models_free_all();
    bs_dump_files_close_all();

    bs_clean_back_channels();
    return 0;
}

uint8_t
main_clean_up_trace_wrap(void)
{
    return inner_main_clean_up(0);
}

void
bsim_init(int argc, char** argv, int (*main_fn)(int argc, char **arg))
{
        setvbuf(stdout, NULL, _IOLBF, 512);
        setvbuf(stderr, NULL, _IOLBF, 512);

        bs_trace_register_cleanup_function(main_clean_up_trace_wrap);
        bs_trace_register_time_function(tm_get_abs_time);

        nrf_hw_pre_init();
        nrfbsim_register_args();

        args = nrfbsim_argsparse(argc, argv);
        global_device_nbr = args->global_device_nbr;

        bs_read_function_names_from_Tsymbols(argv[0]);

        nrf_hw_initialize(&args->nrf_hw);
        os_init(main_fn);
        os_start();

        while (1) {
            sleep(1);
        }
}

void
bsim_start(void)
{
    bs_trace_raw(9, "%s: Connecting to phy...\n", __func__);
    hwll_connect_to_phy(args->device_nbr, args->s_id, args->p_id);
    bs_trace_raw(9, "%s: Connected\n", __func__);

    bs_random_init(args->rseed);
    bs_dump_files_open(args->s_id, args->global_device_nbr);
}
