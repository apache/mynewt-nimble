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

#ifndef H_BLE_SVC_CTS_
#define H_BLE_SVC_CTS_

/* 16 Bit Battery Service UUID */
#define BLE_SVC_CTS_UUID16                                   0x1805

/* 16 Bit Alert Notification Servivce Characteristic UUIDs */
#define BLE_SVC_CTS_CHR_UUID16_CURRENT_TIME                  0x2A2B
#define BLE_SVC_CTS_CHR_UUID16_LOCAL_TIME_INFORMATION        0x2A0F
#define BLE_SVC_CTS_CHR_UUID16_REFERENCE_TIME_INFORMATION    0x2A14

/* Time source used for reference time */
#define BLE_SVC_CTS_TIME_SOURCE_UNKNOWN 		     0
#define BLE_SVC_CTS_TIME_SOURCE_NETWORK_TIME_PROTOCOL        1
#define BLE_SVC_CTS_TIME_SOURCE_GPS                          2
#define BLE_SVC_CTS_TIME_SOURCE_RADIO_TIME_SIGNAL            3
#define BLE_SVC_CTS_TIME_SOURCE_MANUAL                       4
#define BLE_SVC_CTS_TIME_SOURCE_ATOMIC_CLOCK                 5
#define BLE_SVC_CTS_TIME_SOURCE_CELLULAR_NETWORK             6

/* Accuracy of source used for reference time */ 
#define BLE_SVC_CTS_TIME_ACCURACY_OUT_OF_RANGE               254
#define BLE_SVC_CTS_TIME_ACCURACY_UNKNOWN                    255

void ble_svc_cts_on_gap_connect(uint16_t conn_handle);
void ble_svc_cts_init(void);
int ble_svc_cts_reference_time_updated(uint8_t source, uint8_t accuracy, int8_t offset);

#endif

