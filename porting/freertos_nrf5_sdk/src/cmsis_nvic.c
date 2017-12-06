/* mbed Microcontroller Library - cmsis_nvic for XXXX
 * Copyright (c) 2009-2011 ARM Limited. All rights reserved.
 *
 * CMSIS-style functionality to support dynamic vectors
 */
#include "bsp/cmsis_nvic.h"

#ifndef __CORTEX_M
    #error "Macro __CORTEX_M not defined; must define cortex-m type!"
#endif

#if (__CORTEX_M == 0) 
    #ifndef __VTOR_PRESENT
    /* no VTOR is defined so we assume we can't relocate the vector table */
    #define __VTOR_PRESENT  (0)
#endif
#endif

extern char __isr_vector[];
extern char __vector_tbl_reloc__[];

void
NVIC_Relocate(void)
{
    uint32_t *current_location;
    uint32_t *new_location;
    int i;

    /*
     * Relocate the vector table from its current position to the position
     * designated in the linker script.
     */
    current_location = (uint32_t *)&__isr_vector;
    new_location = (uint32_t *)&__vector_tbl_reloc__;

    if (new_location != current_location) {
        for (i = 0; i < NVIC_NUM_VECTORS; i++) {
            new_location[i] = current_location[i];
        }
    }

    /* Set VTOR except for M0 */
#if ((__CORTEX_M == 0) && (__VTOR_PRESENT == 0))
#else
    SCB->VTOR = (uint32_t)&__vector_tbl_reloc__;
#endif
}

void
NVIC_SetVector(IRQn_Type IRQn, uint32_t vector)
{
    uint32_t *vectors;
#if ((__CORTEX_M == 0) && (__VTOR_PRESENT == 0))
    vectors = (uint32_t *)&__vector_tbl_reloc__;
#else
    vectors = (uint32_t *)SCB->VTOR;
#endif
    vectors[IRQn + NVIC_USER_IRQ_OFFSET] = vector;
    __DMB();
}

uint32_t
NVIC_GetVector(IRQn_Type IRQn)
{
    uint32_t *vectors;
#if (__CORTEX_M == 0)
    vectors = (uint32_t *)&__vector_tbl_reloc__;
#else
    vectors = (uint32_t*)SCB->VTOR;
#endif
    return vectors[IRQn + NVIC_USER_IRQ_OFFSET];
}

