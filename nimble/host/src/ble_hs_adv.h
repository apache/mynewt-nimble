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

#ifndef H_BLE_HS_ADV_
#define H_BLE_HS_ADV_

#include <inttypes.h>
struct ble_hs_adv_fields;

struct ble_hs_adv {
    uint8_t event_type;
    uint8_t addr_type;
    uint8_t length_data;
    int8_t rssi;
    uint8_t addr[6];
    uint8_t *data;
};

#define BLE_HS_ADV_TYPE_FLAGS                   0x01
#define BLE_HS_ADV_TYPE_INCOMP_UUIDS16          0x02
#define BLE_HS_ADV_TYPE_COMP_UUIDS16            0x03
#define BLE_HS_ADV_TYPE_INCOMP_UUIDS32          0x04
#define BLE_HS_ADV_TYPE_COMP_UUIDS32            0x05
#define BLE_HS_ADV_TYPE_INCOMP_UUIDS128         0x06
#define BLE_HS_ADV_TYPE_COMP_UUIDS128           0x07
#define BLE_HS_ADV_TYPE_INCOMP_NAME             0x08
#define BLE_HS_ADV_TYPE_COMP_NAME               0x09
#define BLE_HS_ADV_TYPE_TX_PWR_LEVEL            0x0a
#define BLE_HS_ADV_TYPE_DEVICE_CLASS            0x0b
#define BLE_HS_ADV_TYPE_LE_ROLE                 0x1c

#define BLE_HS_ADV_FLAGS_LEN                    1
#define BLE_HS_ADV_F_DISC_LTD                   0x01
#define BLE_HS_ADV_F_DISC_GEN                   0x02

int ble_hs_adv_set_flat(uint8_t type, int data_len, void *data,
                        uint8_t *dst, uint8_t *dst_len, uint8_t max_len);
int ble_hs_adv_set_fields(struct ble_hs_adv_fields *adv_fields,
                          uint8_t *dst, uint8_t *dst_len, uint8_t max_len);
int ble_hs_adv_parse_fields(struct ble_hs_adv_fields *adv_fields, uint8_t *src,
                            uint8_t src_len);
#endif
