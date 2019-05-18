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

#ifndef H_BLE_LL_NRF_RAAL_
#define H_BLE_LL_NRF_RAAL_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if MYNEWT_VAL(BLE_LL_NRF_RAAL_ENABLE)

void ble_ll_nrf_raal_init(void);
void ble_ll_nrf_raal_removed_from_sched(void);
void ble_ll_nrf_raal_halt(void);

void nrf_raal_init(void);
void nrf_raal_uninit(void);
bool nrf_raal_timeslot_is_granted(void);
void nrf_raal_continuous_mode_enter(void);
void nrf_raal_continuous_mode_exit(void);
void nrf_raal_critical_section_enter(void);
void nrf_raal_critical_section_exit(void);
bool nrf_raal_timeslot_request(uint32_t length_us);
uint32_t nrf_raal_timeslot_us_left_get(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_LL_NRF_RAAL_ */
