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

#ifndef H_BLE_LL_ISO_BIG_
#define H_BLE_LL_ISO_BIG_

#ifdef __cplusplus
extern "C" {
#endif

#if MYNEWT_VAL(BLE_LL_ISO_BROADCASTER)

void ble_ll_iso_big_chan_map_update(void);

void ble_ll_iso_big_halt(void);

int ble_ll_iso_big_hci_create(const uint8_t *cmdbuf, uint8_t len);
int ble_ll_iso_big_hci_create_test(const uint8_t *cmdbuf, uint8_t len);
int ble_ll_iso_big_hci_terminate(const uint8_t *cmdbuf, uint8_t len);
void ble_ll_iso_big_hci_evt_complete(void);

void ble_ll_iso_big_init(void);
void ble_ll_iso_big_reset(void);

#endif /* BLE_LL_ISO_BROADCASTER */

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_LL_ISO_BIG_ */
