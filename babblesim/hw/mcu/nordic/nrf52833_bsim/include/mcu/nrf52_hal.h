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

#ifndef H_MCU_NRF52_HAL_
#define H_MCU_NRF52_HAL_

#include "cmsis.h"

#ifdef __cplusplus
extern "C" {
#endif

#define __HAL_DISABLE_INTERRUPTS(x)                                           \
    do {                                                                      \
        x = __get_PRIMASK();                                                  \
        __disable_irq();                                                      \
    } while (0)

#define __HAL_ENABLE_INTERRUPTS(x)                                            \
    do {                                                                      \
        if (!x) {                                                             \
            __enable_irq();                                                   \
        }                                                                     \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* H_MCU_NRF52_HAL_ */
