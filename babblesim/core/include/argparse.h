/*
 * Copyright (c) 2017 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BSIM_NRF_ARGS_H
#define BSIM_NRF_ARGS_H

#include <stdint.h>
#include "NRF_hw_args.h"
#include "bs_cmd_line.h"
#include "bs_cmd_line_typical.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nrf52_bsim_args_t {
    BS_BASIC_DEVICE_OPTIONS_FIELDS
    nrf_hw_sub_args_t nrf_hw;
};

struct nrf52_bsim_args_t *nrfbsim_argsparse(int argc, char *argv[]);
void nrfbsim_register_args(void);

#ifdef __cplusplus
}
#endif

#endif
