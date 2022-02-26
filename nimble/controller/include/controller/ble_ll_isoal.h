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

#ifndef H_BLE_LL_ISOAL_
#define H_BLE_LL_ISOAL_

#ifdef __cplusplus
extern "C" {
#endif

#if MYNEWT_VAL(BLE_LL_ISO)

/* HCI command handlers */
int ble_ll_isoal_hci_setup_data_path(const uint8_t *cmdbuf, uint8_t cmdlen,
                                     uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_isoal_hci_remove_data_path(const uint8_t *cmdbuf, uint8_t cmdlen,
                                      uint8_t *rspbuf, uint8_t *rsplen);

void ble_ll_isoal_init(void);

#endif /* BLE_LL_ISO */

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_LL_ISOAL_ */
