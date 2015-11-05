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

#ifndef H_BLE_L2CAP_
#define H_BLE_L2CAP_

#include <inttypes.h>
struct ble_l2cap_chan;

int ble_l2cap_read_uint16(const struct ble_l2cap_chan *chan, int off,
                          uint16_t *u16);
void ble_l2cap_strip(struct ble_l2cap_chan *chan, int delta);

#endif
