/*
 * Copyright (c) 2017 Oticon A/S
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * CMSIS replacement for BabbleSim nRF HW models. This is included by
 * core_cm4.h provided by HW models when INTEGRATOR_PROVIDES_CMSIS is defined.
 * Based on Zephyr nrf_bsim boards.
 */

#ifndef H_BSIM_CMSIS_
#define H_BSIM_CMSIS_

#include <stdint.h>
#include <mdk/nrf.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __DMB
#define __DMB()
#endif
#ifndef __DSB
#define __DSB()
#endif
#ifndef __ISB
#define __ISB()
#endif
#ifndef __NOP
#define __NOP()
#endif

void __WFE(void);
void __WFI(void);
void __SEV(void);

uint32_t __STREXB(uint8_t value, volatile uint8_t *ptr);
uint32_t __STREXH(uint16_t value, volatile uint16_t *ptr);
uint32_t __STREXW(uint32_t value, volatile uint32_t *ptr);
uint8_t __LDREXB(volatile uint8_t *ptr);
uint16_t __LDREXH(volatile uint16_t *ptr);
uint32_t __LDREXW(volatile uint32_t *ptr);
void __CLREX(void);

static inline unsigned char
__CLZ(uint32_t value)
{
    if (value == 0) {
        return 32;
    }

    return __builtin_clz(value);
}

void __enable_irq(void);
void __disable_irq(void);
uint32_t __get_PRIMASK(void);
void __set_PRIMASK(uint32_t primask);

void NVIC_SetPendingIRQ(IRQn_Type IRQn);
void NVIC_ClearPendingIRQ(IRQn_Type IRQn);
void NVIC_DisableIRQ(IRQn_Type IRQn);
uint32_t NVIC_GetPendingIRQ(IRQn_Type IRQn);
void NVIC_EnableIRQ(IRQn_Type IRQn);
uint32_t NVIC_GetEnableIRQ(IRQn_Type IRQn);
void NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority);
uint32_t NVIC_GetPriority(IRQn_Type IRQn);
void NVIC_SystemReset(void);

void nrfbsim_clear_excl_access(void);

#ifdef __cplusplus
}
#endif

#endif /* H_BSIM_CMSIS_ */
