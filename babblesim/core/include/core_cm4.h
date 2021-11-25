/*
 * Copyright (c) 2020 Oticon A/S
 * Copyright (c) 2021 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _BSIM_CORE_CM4_H
#define _BSIM_CORE_CM4_H

#include <stdint.h>

/* Include the original ext_NRF52_hw_models core_cm4.h */
#include <../HW_models/core_cm4.h>

/* Add missing function definitions */
extern void NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority);
extern void NVIC_EnableIRQ(IRQn_Type IRQn);
extern void NVIC_DisableIRQ(IRQn_Type IRQn);

void __WFI(void);

#ifndef __REV
#define __REV __builtin_bswap32
#endif

#endif
