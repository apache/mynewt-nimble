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

#ifndef H_BLE_HS_PERIODIC_DISC_
#define H_BLE_HS_PERIODIC_DISC_

#include <inttypes.h>
#include "os/queue.h"
#ifdef __cplusplus
extern "C" {
#endif

struct ble_hs_periodic_sync {
    SLIST_ENTRY(ble_hs_periodic_sync) bhc_next;
    uint16_t   sync_handle;
    uint8_t    adv_sid;
    uint8_t    advertiser_addr_type;
    ble_addr_t advertiser_addr;
    uint8_t    advertiser_phy;
    uint16_t   periodic_adv_itvl;
    uint8_t    advertiser_clock_accuracy;
};

int ble_hs_periodic_sync_can_alloc(void);
struct ble_hs_periodic_sync *ble_hs_periodic_sync_alloc(void);
void ble_hs_periodic_sync_free(struct ble_hs_periodic_sync *psync);
void ble_hs_periodic_sync_insert(struct ble_hs_periodic_sync *psync);
void ble_hs_periodic_sync_remove(struct ble_hs_periodic_sync *psync);
struct ble_hs_periodic_sync *ble_hs_periodic_sync_find(
                                                         uint16_t sync_handle);
struct ble_hs_periodic_sync *ble_hs_periodic_sync_find_assert(
                                                         uint16_t sync_handle);
struct ble_hs_periodic_sync *ble_hs_periodic_sync_find_by_adv_addr(
                                                      const ble_addr_t *addr);
struct ble_hs_periodic_sync *ble_hs_periodic_sync_find_by_adv_sid(
                                                                 uint16_t sid);
int ble_hs_periodic_sync_exists(uint16_t sync_handle);
struct ble_hs_periodic_sync *ble_hs_periodic_sync_first(void);
int ble_hs_periodic_sync_init(void);

#ifdef __cplusplus
}
#endif

#endif
