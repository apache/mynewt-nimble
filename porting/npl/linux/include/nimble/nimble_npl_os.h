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

#ifndef _NIMBLE_NPL_OS_H_
#define _NIMBLE_NPL_OS_H_

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include "os_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_NPL_OS_ALIGNMENT    (__WORDSIZE / 8)

#define BLE_NPL_TIME_FOREVER    INT32_MAX

struct ble_npl_eventq * ble_npl_eventq_dflt_get(void);
void ble_npl_eventq_run(struct ble_npl_eventq *evq);

#ifdef __cplusplus
}
#endif

#endif  /* _NPL_H_ */
