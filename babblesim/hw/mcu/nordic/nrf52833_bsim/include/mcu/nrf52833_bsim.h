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

#ifndef H_MCU_NRF52833_BSIM_
#define H_MCU_NRF52833_BSIM_

#ifdef __cplusplus
extern "C" {
#endif

/* CPU (native simulator MCU) number of this image */
#define BSIM_CPU_N (0)

/* Halt CPU until woken up by HW models, then handle pending interrupts */
void bsim_halt_cpu(void);

/* Handle all pending interrupts (if not locked) */
void bsim_irq_handler(void);

/*
 * Do pending context switch if allowed (interrupts not locked and not in ISR)
 */
void bsim_os_arch_ctx_sw_check(void);

#ifdef __cplusplus
}
#endif

#endif /* H_MCU_NRF52833_BSIM_ */
