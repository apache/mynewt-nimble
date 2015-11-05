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

#ifndef _BLE_HOST_H 
#define _BLE_HOST_H 

#include <inttypes.h>

extern int ble_host_listen_enabled;

/** --- */

void ble_host_task_handler(void *arg);
int ble_host_send_data_connectionless(uint16_t con_handle, uint16_t cid,
                                      uint8_t *data, uint16_t len);
int ble_host_poll(void);
int host_init(void);

#endif /* _BLE_HOST_H */
