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

#ifndef H_BLE_GAP_CONN_
#define H_BLE_GAP_CONN_

#include <inttypes.h>
#include "host/ble_gap.h"
struct hci_le_conn_complete;
struct hci_disconn_complete;
struct ble_hci_ack;

#define BLE_GAP_CONN_AD_TYPE_FLAGS                  0x01
#define BLE_GAP_CONN_AD_TYPE_INCOMP_16BIT_UUIDS     0x02
#define BLE_GAP_CONN_AD_TYPE_COMP_16BIT_UUIDS       0x03
#define BLE_GAP_CONN_AD_TYPE_INCOMP_32BIT_UUIDS     0x04
#define BLE_GAP_CONN_AD_TYPE_COMP_32BIT_UUIDS       0x05
#define BLE_GAP_CONN_AD_TYPE_INCOMP_128BIT_UUIDS    0x06
#define BLE_GAP_CONN_AD_TYPE_COMP_128BIT_UUIDS      0x07
#define BLE_GAP_CONN_AD_TYPE_INCOMP_NAME            0x08
#define BLE_GAP_CONN_AD_TYPE_COMP_NAME              0x09
#define BLE_GAP_CONN_AD_TYPE_TX_PWR_LEVEL           0x0a
#define BLE_GAP_CONN_AD_TYPE_DEVICE_CLASS           0x0b

#define BLE_GAP_CONN_AD_F_DISC_LTD                  0x01
#define BLE_GAP_CONN_AD_F_DISC_GEN                  0x02

void ble_gap_conn_rx_adv_report(struct ble_gap_conn_adv_rpt *rpt);
int ble_gap_conn_rx_conn_complete(struct hci_le_conn_complete *evt);
void ble_gap_conn_rx_disconn_complete(struct hci_disconn_complete *evt);
int ble_gap_conn_master_in_progress(void);
int ble_gap_conn_slave_in_progress(void);
int ble_gap_conn_init(void);

#endif
