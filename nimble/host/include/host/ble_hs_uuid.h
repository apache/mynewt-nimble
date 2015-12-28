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

#ifndef H_BLE_HS_UUID_
#define H_BLE_HS_UUID_

struct os_mbuf;

uint16_t ble_hs_uuid_16bit(void *uuid128);
int ble_hs_uuid_from_16bit(uint16_t uuid16, void *dst);
int ble_hs_uuid_append(struct os_mbuf *om, void *uuid128);
int ble_hs_uuid_extract(struct os_mbuf *om, int off, void *uuid128);

/**
 * Expands to a designated initializer byte array containing the 128-bit
 * representation of the specified 16-bit UUID.
 */
#define BLE_UUID16(uuid16) ((uint8_t[16]) {                                 \
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,                         \
    0x00, 0x10, 0x00, 0x00, (uuid16) & 0xff, (((uuid16) & 0xff00) >> 8),    \
    0x00, 0x00                                                              \
})

#endif /* _BLE_HOST_UUID_H */
