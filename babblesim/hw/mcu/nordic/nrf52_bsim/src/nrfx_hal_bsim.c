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

#include <hal/nrf_timer.h>

/*
 * nrfx HAL functions used by NimBLE that are not replaced by BabbleSim HW
 * models. Original implementations are in nrf_hal_originals.o which is
 * removed from HW models library (see link_babblesim.sh).
 */

nrf_timer_task_t
nrf_timer_capture_task_get(uint8_t channel)
{
    return (nrf_timer_task_t)NRFX_OFFSETOF(NRF_TIMER_Type, TASKS_CAPTURE[channel]);
}
