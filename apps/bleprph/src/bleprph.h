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

#ifndef H_BLEPRPH_
#define H_BLEPRPH_

#include <stdbool.h>
#include "log/log.h"
#include "nimble/ble.h"
#ifdef __cplusplus
extern "C" {
#endif

struct ble_hs_cfg;
struct ble_gatt_register_ctxt;

extern struct log bleprph_log;

/* bleprph uses the first "peruser" log module. */
#define BLEPRPH_LOG_MODULE  (LOG_MODULE_PERUSER + 0)

/* Convenience macro for logging to the bleprph module. */
#define BLEPRPH_LOG(lvl, ...) \
    LOG_ ## lvl(&bleprph_log, BLEPRPH_LOG_MODULE, __VA_ARGS__)

/** GATT server. */
#define GATT_SVR_SVC_ALERT_UUID               0x1811
#define GATT_SVR_CHR_SUP_NEW_ALERT_CAT_UUID   0x2A47
#define GATT_SVR_CHR_NEW_ALERT                0x2A46
#define GATT_SVR_CHR_SUP_UNR_ALERT_CAT_UUID   0x2A48
#define GATT_SVR_CHR_UNR_ALERT_STAT_UUID      0x2A45
#define GATT_SVR_CHR_ALERT_NOT_CTRL_PT        0x2A44

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
int gatt_svr_init(void);

/* PHY support */
#if MYNEWT_VAL(BLEPRPH_LE_PHY_SUPPORT)
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
