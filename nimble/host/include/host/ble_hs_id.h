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

#ifndef H_BLE_HS_ID_
#define H_BLE_HS_ID_

#include <inttypes.h>
#include "nimble/ble.h"

#ifdef __cplusplus
extern "C" {
#endif

int ble_hs_id_gen_rnd(int nrpa, ble_addr_t *out_addr);
int ble_hs_id_set_rnd(const uint8_t *rnd_addr);
int ble_hs_id_copy_addr(uint8_t id_addr_type, uint8_t *out_id_addr,
                        int *out_is_nrpa);
int ble_hs_id_infer_auto(int privacy, uint8_t *out_addr_type);

#ifdef __cplusplus
}
#endif

#endif
