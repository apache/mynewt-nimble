/**
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

#ifndef H_BLE_HS_MBUF_
#define H_BLE_HS_MBUF_

#include <inttypes.h>
struct os_mbuf;

struct os_mbuf *ble_hs_mbuf_att_pkt(void);
struct os_mbuf *ble_hs_mbuf_from_flat(const void *buf, uint16_t len);
int ble_hs_mbuf_to_flat(const struct os_mbuf *om, void *flat, uint16_t max_len,
                        uint16_t *out_copy_len);

#endif
