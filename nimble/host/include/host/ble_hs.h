/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef H_BLE_HS_
#define H_BLE_HS_

#include <inttypes.h>

#define BLE_HS_EAGAIN                   1
#define BLE_HS_EALREADY                 2
#define BLE_HS_EINVAL                   3
#define BLE_HS_EMSGSIZE                 4
#define BLE_HS_ENOENT                   5
#define BLE_HS_ENOMEM                   6
#define BLE_HS_ENOTCONN                 7
#define BLE_HS_ENOTSUP                  8
#define BLE_HS_EATT                     9
#define BLE_HS_EBADDATA                 10
#define BLE_HS_EOS                      11

int ble_hs_init(uint8_t prio);

#endif
