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

#ifndef H_BLE_EDDYSTONE_
#define H_BLE_EDDYSTONE_

#include <inttypes.h>
struct ble_hs_adv_fields;

#define BLE_EDDYSTONE_MAX_UUIDS16           3
#define BLE_EDDYSTONE_URL_MAX_LEN           17

#define BLE_EDDYSTONE_URL_SCHEME_HTTP_WWW   0
#define BLE_EDDYSTONE_URL_SCHEME_HTTPS_WWW  1
#define BLE_EDDYSTONE_URL_SCHEME_HTTP       2
#define BLE_EDDYSTONE_URL_SCHEME_HTTPS      3

#define BLE_EDDYSTONE_URL_SUFFIX_COM_SLASH  0x00
#define BLE_EDDYSTONE_URL_SUFFIX_ORG_SLASH  0x01
#define BLE_EDDYSTONE_URL_SUFFIX_EDU_SLASH  0x02
#define BLE_EDDYSTONE_URL_SUFFIX_NET_SLASH  0x03
#define BLE_EDDYSTONE_URL_SUFFIX_INFO_SLASH 0x04
#define BLE_EDDYSTONE_URL_SUFFIX_BIZ_SLASH  0x05
#define BLE_EDDYSTONE_URL_SUFFIX_GOV_SLASH  0x06
#define BLE_EDDYSTONE_URL_SUFFIX_COM        0x07
#define BLE_EDDYSTONE_URL_SUFFIX_ORG        0x08
#define BLE_EDDYSTONE_URL_SUFFIX_EDU        0x09
#define BLE_EDDYSTONE_URL_SUFFIX_NET        0x0a
#define BLE_EDDYSTONE_URL_SUFFIX_INFO       0x0b
#define BLE_EDDYSTONE_URL_SUFFIX_BIZ        0x0c
#define BLE_EDDYSTONE_URL_SUFFIX_GOV        0x0d
#define BLE_EDDYSTONE_URL_SUFFIX_NONE       0xff

int ble_eddystone_set_adv_data_uid(struct ble_hs_adv_fields *adv_fields,
                                   void *uid);
int ble_eddystone_set_adv_data_url(struct ble_hs_adv_fields *adv_fields,
                                   uint8_t url_scheme, char *url_body,
                                   uint8_t url_body_len, uint8_t suffix);

#endif
