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

#ifndef H_BLE_SVC_HID_
#define H_BLE_SVC_HID_



/* 16 Bit Human Interface Device Service UUID */
#define BLE_SVC_HID_UUID16                                 0x1812

/* 16 Bit HID Service Characteristic UUIDs */
#define BLE_SVC_HID_CHR_UUID16_PROTOCOL_MODE               0x2A4E
#define BLE_SVC_HID_CHR_UUID16_REPORT                      0x2A4D
#define BLE_SVC_HID_CHR_UUID16_REPORT_MAP                  0x2A4B
#define BLE_SVC_HID_CHR_UUID16_BOOT_KEYBOARD_INPUT_REPORT  0x2A22
#define BLE_SVC_HID_CHR_UUID16_BOOT_KEYBOARD_OUTPUT_REPORT 0x2A32
#define BLE_SVC_HID_CHR_UUID16_BOOT_MOUSE_INPUT_REPORT     0x2A33
#define BLE_SVC_HID_CHR_UUID16_HID_INFORMATION             0x2A4A
#define BLE_SVC_HID_CHR_UUID16_HID_CONTROL_POINT           0x2A4C

/* HID Service Protocol Mode enum
 * The protocol mode of the HID Service
 * Format: uint8
 *   key="0" value="Boot Protocol Mode"
 *   key="1" value="Report Protocol Mode"
 *   key="2-255" ReservedForFutureUse
 */

typedef enum __attribute__((packed)) {
    BLE_SVC_HID_PROTOCOL_MODE_BOOT,
    BLE_SVC_HID_PROTOCOL_MODE_REPORT
} ble_svc_hid_protocol_mode_value_t ;

/* HID Service HID Information Flags
 * Format: 8-bit BitField
 * Note:The fields are in the order of LSO to MSO
 *   index="0" size="1" name="RemoteWake"
 *   index="1" size="1" name="NormallyConnectable"
 *   index="2" size="6" ReservedForFutureUse
 */
#define BLE_SVC_HID_HID_INFO_FLAG_REMOTE_WAKE  0x01
#define BLE_SVC_HID_HID_INFO_FLAG_NORMALLY_CONNECTABLE  0x02

/* HID Service HID Control Point Command enum
 * The HID Control Point characteristic is a control-point attribute that defines the following HID Commands when written: 
 * Format: uint8
 *   key="0" value="Suspend"
 *   key="1" value="Exit Suspend"
 *   key="2-255" ReservedForFutureUse
 */

typedef enum __attribute__((packed)) {
    BLE_SVC_HID_HID_CONTROL_POINT_SUSPEND,
    BLE_SVC_HID_HID_CONTROL_POINT_EXIT_SUSPEND
} ble_svc_hid_hid_control_point_value_t;

/* Functions */

void ble_svc_hid_init(void);

#endif
