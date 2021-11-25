/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef _OS_ARCH_ARM_H
#define _OS_ARCH_ARM_H

#include <stdint.h>
#include "syscfg/syscfg.h"
#include "mcu/cmsis_nvic.h"
#include "mcu/cortex_m4.h"
#include <irq_ctrl.h>
#include "mcu/mcu_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CPU status register */
typedef uint32_t os_sr_t;

/* Stack element */
typedef uint32_t os_stack_t;

struct stack_frame;
void os_arch_frame_init(struct stack_frame *sf);

/* Stack sizes for common OS tasks */
#define OS_SANITY_STACK_SIZE (2000)
#if MYNEWT_VAL(OS_SYSVIEW)
#define OS_IDLE_STACK_SIZE (80)
#else
#define OS_IDLE_STACK_SIZE (4000)
#endif

static inline int
os_arch_in_isr(void)
{
	return hw_irq_ctrl_get_irq_status();
}

/* Include common arch definitions and APIs */
#include "os/arch/common.h"

#ifdef __cplusplus
}
#endif

#endif /* _OS_ARCH_ARM_H */
