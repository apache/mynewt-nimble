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

#ifndef H_BLEHR_SENSOR_
#define H_BLEHR_SENSOR_

#include "log/log.h"
#include "nimble/ble.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct log blehr_log;

/* blehr uses the first "peruser" log module */
#define BLEHR_LOG_MODULE (LOG_MODULE_PERUSER + 0)

/* Convenience macro for logging to the blerh module */
#define BLEHR_LOG(lvl, ...) \
    LOG_ ## lvl(&blehr_log, BLEHR_LOG_MODULE, __VA_ARGS__)

/* Heart-rate configuration */
#define GATT_HRS_UUID                           0x180D
#define GATT_HRS_MEASUREMENT_UUID               0x2A37
#define GATT_HRS_BODY_SENSOR_LOC_UUID           0x2A38
#define GATT_DEVICE_INFO_UUID                   0x180A
#define GATT_MANUFACTURER_NAME_UUID             0x2A29
#define GATT_MODEL_NUMBER_UUID                  0x2A24

extern uint16_t hrs_hrm_handle;

int gatt_svr_init(void);

#ifdef __cplusplus
}
#endif

#endif
