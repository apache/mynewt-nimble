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

#ifndef H_CS_REFLECTOR_
#define H_CS_REFLECTOR_

#include <stdbool.h>
#include "nimble/ble.h"
#include "modlog/modlog.h"
#ifdef __cplusplus
extern "C" {
#endif

struct ble_hs_cfg;
struct ble_gatt_register_ctxt;

/** GATT server. */
#define GATT_SVR_SVC_CHANNEL_SOUNDING_UUID 0xffff
#define GATT_SVR_CHR_TOD_TOA_UUID          0xfffe

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
int gatt_svr_init(void);
int gatt_svr_indicate_toa_tod(uint16_t conn_handle, uint32_t toa_tod_val);

/* PHY support */
#if MYNEWT_VAL(CS_REFLECTOR_LE_PHY_SUPPORT)
#define CONN_HANDLE_INVALID     0xffff

void phy_init(void);
void phy_conn_changed(uint16_t handle);
void phy_update(uint8_t phy);
#endif

/** Misc. */
void print_bytes(const uint8_t *bytes, int len);
void print_addr(const void *addr);

#ifdef __cplusplus
}
#endif

#endif
