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

#ifndef H_ITF_
#define H_ITF_

struct os_eventq;

int ble_host_sim_send_data_connectionless(uint16_t con_handle, uint16_t cid,
                                          uint8_t *data, uint16_t len);
int ble_sim_listen(uint16_t con_handle);
int ble_host_sim_poll(void);

#endif
