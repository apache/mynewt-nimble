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

#ifndef H_BLE_HS_RESOLV_PRIV_
#define H_BLE_HS_RESOLV_PRIV_

#include <stdint.h>
#include "nimble/ble.h"
#include "nimble/nimble_opt.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ble_gap_conn_complete;

/* The resolver is also built for unit tests, which run with controller
 * privacy, so that it is tested in host's newt test.
 */
#define BLE_HS_RESOLV_ENABLED \
    (MYNEWT_VAL(BLE_HOST_BASED_PRIVACY) || (MYNEWT_VAL(SELFTEST) && NIMBLE_BLE_SM))

#if BLE_HS_RESOLV_ENABLED

/* Host resolving list: peers' identity addresses and IRKs, as they would
 * be programmed into a controller resolving list.
 */
int ble_hs_resolv_add(const ble_addr_t *id_addr, const uint8_t *irk);
int ble_hs_resolv_remove(const ble_addr_t *id_addr);
void ble_hs_resolv_clear(void);

/* If addr is an RPA of a peer on the list, replace it with the peer's
 * identity address (type BLE_ADDR_PUBLIC_ID or BLE_ADDR_RANDOM_ID, as a
 * controller reports resolved addresses) and remember the RPA.
 * Returns 1 if the address was resolved.
 */
int ble_hs_resolv_addr(ble_addr_t *addr);

/* Resolve the peer address of a connection complete event in place. */
void ble_hs_resolv_conn_complete(struct ble_gap_conn_complete *evt);

/* If peer_addr is the identity address of a peer on the list that was last
 * seen advertising with an RPA, copy that RPA to out_rpa and return 0.
 */
int ble_hs_resolv_peer_rpa(const ble_addr_t *peer_addr, ble_addr_t *out_rpa);

#endif

#ifdef __cplusplus
}
#endif

#endif
