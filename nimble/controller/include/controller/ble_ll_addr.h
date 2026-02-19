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

#ifndef H_BLE_LL_ADDR_
#define H_BLE_LL_ADDR_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int ble_ll_addr_init(void);
int ble_ll_addr_public_set(const uint8_t *addr);

const uint8_t *ble_ll_addr_get(uint8_t addr_type);
const uint8_t *ble_ll_addr_public_get(void);
const uint8_t *ble_ll_addr_random_get(void);
int ble_ll_addr_random_set(const uint8_t *addr);

bool ble_ll_addr_is_our(int addr_type, const uint8_t *addr);
bool ble_ll_addr_is_valid_own_addr_type(uint8_t addr_type,
                                        const uint8_t *random_addr);

/* Address provider APIs - should be implemented by packages supporting
 * relevant APIs
 */
int ble_ll_addr_provide_public(uint8_t *addr);
int ble_ll_addr_provide_static(uint8_t *addr);

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_LL_ADDR_ */
