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

#ifndef H_BLE_CS_PRIV_
#define H_BLE_CS_PRIV_

#define BLE_HS_CS_RTT_AA_ONLY                  (0x00)
#define BLE_HS_CS_RTT_32_BIT_SOUNDING_SEQUENCE (0x01)
#define BLE_HS_CS_RTT_96_BIT_SOUNDING_SEQUENCE (0x02)
#define BLE_HS_CS_RTT_32_BIT_RANDOM_SEQUENCE   (0x03)
#define BLE_HS_CS_RTT_64_BIT_RANDOM_SEQUENCE   (0x04)
#define BLE_HS_CS_RTT_96_BIT_RANDOM_SEQUENCE   (0x05)
#define BLE_HS_CS_RTT_128_BIT_RANDOM_SEQUENCE  (0x06)

#define BLE_HS_CS_SUBEVENT_DONE_STATUS_COMPLETED (0x0)
#define BLE_HS_CS_SUBEVENT_DONE_STATUS_PARTIAL   (0x1)
#define BLE_HS_CS_SUBEVENT_DONE_STATUS_ABORTED   (0xF)

#define BLE_HS_CS_PROC_DONE_STATUS_COMPLETED (0x0)
#define BLE_HS_CS_PROC_DONE_STATUS_PARTIAL   (0x1)
#define BLE_HS_CS_PROC_DONE_STATUS_ABORTED   (0xF)

int ble_hs_hci_evt_le_cs_rd_rem_supp_cap_complete(uint8_t subevent, const void *data, unsigned int len);
int ble_hs_hci_evt_le_cs_rd_rem_fae_complete(uint8_t subevent, const void *data, unsigned int len);
int ble_hs_hci_evt_le_cs_sec_enable_complete(uint8_t subevent, const void *data, unsigned int len);
int ble_hs_hci_evt_le_cs_config_complete(uint8_t subevent, const void *data, unsigned int len);
int ble_hs_hci_evt_le_cs_proc_enable_complete(uint8_t subevent, const void *data, unsigned int len);
int ble_hs_hci_evt_le_cs_subevent_result(uint8_t subevent, const void *data, unsigned int len);
int ble_hs_hci_evt_le_cs_subevent_result_continue(uint8_t subevent, const void *data, unsigned int len);
int ble_hs_hci_evt_le_cs_test_end_complete(uint8_t subevent, const void *data, unsigned int len);
#endif
