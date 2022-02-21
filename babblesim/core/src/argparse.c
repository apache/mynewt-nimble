/*
 * Copyright (c) 2017 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h>
#include <stdlib.h>
#include <limits.h>
#include "bs_tracing.h"
#include "bs_oswrap.h"
#include "bs_dump_files.h"
#include "argparse.h"
#include "NRF_hw_args.h"
#include "bs_cmd_line.h"
#include "bs_dynargs.h"
#include "bs_cmd_line_typical.h"
#include "NRF_HWLowL.h"
#include "controller/ble_ll.h"

static bs_args_struct_t *args_struct;
static struct nrf52_bsim_args_t arg;
const char *bogus_sim_id = "bogus";

static void cmd_trace_lvl_found(char *argv, int offset)
{
	bs_trace_set_level(arg.verb);
}

static void cmd_gdev_nbr_found(char *argv, int offset)
{
	bs_trace_set_prefix_dev(arg.global_device_nbr);
}

static bool nosim;
static void cmd_nosim_found(char *argv, int offset)
{
	hwll_set_nosim(true);
}

static void cmd_bdaddr_found(char *argv, int offset)
{
    union {
        uint64_t u64;
        uint8_t u8[8];
    } bdaddr;
    char *endptr;

    if (sscanf(&argv[offset], "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
               &bdaddr.u8[5], &bdaddr.u8[4], &bdaddr.u8[3], &bdaddr.u8[2],
               &bdaddr.u8[1], &bdaddr.u8[0]) < 6) {
        bdaddr.u64 = strtoull(&argv[offset], &endptr, 0);
        if (*endptr) {
            return;
        }

        bdaddr.u64 = htole64(bdaddr.u64);
    }

    ble_ll_set_public_addr(bdaddr.u8);
}

static void print_no_sim_warning(void)
{
	bs_trace_warning("Neither simulation id or the device number "
			"have been set. I assume you want to run "
			"without a BabbleSim phy (-nosim)\n");
	bs_trace_warning("If this is not what you wanted, check with "
			"--help how to set them\n");
	bs_trace_raw(3, "setting sim_id to 'bogus', device number to 0 "
			"and nosim\n");
}

void nrfbsim_register_args(void)
{
#define args (&arg)
	/* This define is quite ugly, but allows reusing the definitions
	 * provided by the utils library */
	static bs_args_struct_t args_struct_toadd[] = {
		ARG_TABLE_S_ID,
		ARG_TABLE_P_ID_2G4,
		ARG_TABLE_DEV_NBR,
		ARG_TABLE_GDEV_NBR,
		ARG_TABLE_VERB,
		ARG_TABLE_SEED,
		ARG_TABLE_COLOR,
		ARG_TABLE_NOCOLOR,
		ARG_TABLE_FORCECOLOR,
		_NRF_HW_SUB_CMD_ARG_STRUCT,
		/*
		 * Fields:
		 * manual, mandatory, switch,
		 * option_name, var_name, type,
		 * destination, callback,
		 * description
		 */
		{ false, false , false,
		  "A", "bdaddr", 's',
		  NULL, cmd_bdaddr_found, "Device public address"},
		{false, false, true,
		"nosim", "", 'b',
		(void *)&nosim, cmd_nosim_found,
		"(debug feature) Do not connect to the phy"},
		BS_DUMP_FILES_ARGS,
		{true, false, false,
		"argsmain", "arg", 'l',
		NULL, NULL,
		"The arguments that follow will be passed to main (default)"},
		ARG_TABLE_ENDMARKER
	};
#undef args

	bs_add_dynargs(&args_struct, args_struct_toadd);
}

/**
 * Check the arguments provided in the command line: set args based on it or
 * defaults, and check they are correct
 */
struct nrf52_bsim_args_t *nrfbsim_argsparse(int argc, char *argv[])
{
	bs_args_set_defaults(args_struct);
	arg.verb = 2;
	bs_trace_set_level(arg.verb);
	nrf_hw_sub_cmline_set_defaults(&arg.nrf_hw);
	static const char default_phy[] = "2G4";

    for (int i = 1; i < argc; i++) {
        if (bs_is_option(argv[i], "argsmain", 0)) {
            continue;
        }

        if (!bs_args_parse_one_arg(argv[i], args_struct)) {
            bs_args_print_switches_help(args_struct);
            bs_trace_error_line("Incorrect option %s\n",
                                argv[i]);
        }
    }

	/**
	 * If the user did not set the simulation id or device number
	 * we assume he wanted to run with nosim (but warn him)
	 */
	if ((!nosim) && (arg.s_id == NULL) && (arg.device_nbr == UINT_MAX)) {
		print_no_sim_warning();
		nosim = true;
		hwll_set_nosim(true);
	}
	if (nosim) {
		if (arg.s_id == NULL) {
			arg.s_id = (char *)bogus_sim_id;
		}
		if (arg.device_nbr == UINT_MAX) {
			arg.device_nbr = 0;
		}
	}

	if (arg.device_nbr == UINT_MAX) {
		bs_args_print_switches_help(args_struct);
		bs_trace_error_line("The command line option <device number> "
				    "needs to be set\n");
	}
	if (arg.global_device_nbr == UINT_MAX) {
		arg.global_device_nbr = arg.device_nbr;
		bs_trace_set_prefix_dev(arg.global_device_nbr);
	}
	if (!arg.s_id) {
		bs_args_print_switches_help(args_struct);
		bs_trace_error_line("The command line option <simulation ID> "
				    "needs to be set\n");
	}
	if (!arg.p_id) {
		arg.p_id = (char *)default_phy;
	}

	if (arg.rseed == UINT_MAX) {
		arg.rseed = 0x1000 + arg.device_nbr;
	}
	return &arg;
}
