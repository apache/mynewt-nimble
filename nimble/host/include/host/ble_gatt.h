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

#ifndef H_BLE_GATT_
#define H_BLE_GATT_

#include <inttypes.h>
struct ble_hs_conn;
struct ble_att_error_rsp;
struct ble_att_clt_adata;

struct ble_gatt_service {
    uint16_t start_handle;
    uint16_t end_handle;
    uint8_t uuid128[16];
};

struct ble_gatt_attr {
    uint16_t handle;
    uint8_t value_len;
    void *value;
};

struct ble_gatt_chr {
    uint16_t decl_handle;
    uint16_t value_handle;
    uint8_t properties;
    uint8_t uuid128[16];
};

typedef int ble_gatt_disc_service_fn(uint16_t conn_handle,
                                     uint8_t ble_hs_status, uint8_t att_status,
                                     struct ble_gatt_service *service,
                                     void *arg);
typedef int ble_gatt_attr_fn(uint16_t conn_handle, uint8_t ble_hs_status,
                             uint8_t att_status, struct ble_gatt_attr *attr,
                             void *arg);

typedef int ble_gatt_chr_fn(uint16_t conn_handle, uint8_t ble_hs_status,
                            uint8_t att_status, struct ble_gatt_chr *chr,
                            void *arg);

typedef int ble_gatt_write_fn(uint16_t conn_handle, uint8_t ble_hs_status,
                              uint8_t att_status, uint16_t attr_handle,
                              void *arg);

int ble_gatt_disc_all_services(uint16_t conn_handle,
                               ble_gatt_disc_service_fn *cb,
                               void *cb_arg);
int ble_gatt_disc_service_by_uuid(uint16_t conn_handle, void *service_uuid128,
                                  ble_gatt_disc_service_fn *cb, void *cb_arg);
int ble_gatt_disc_all_chars(uint16_t conn_handle, uint16_t start_handle,
                            uint16_t end_handle, ble_gatt_chr_fn *cb,
                            void *cb_arg);
int ble_gatt_read(uint16_t conn_handle, uint16_t attr_handle,
                  ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gatt_write_no_rsp(uint16_t conn_handle, uint16_t attr_handle,
                          void *value, uint16_t value_len,
                          ble_gatt_write_fn *cb, void *cb_arg);
int ble_gatt_write(uint16_t conn_handle, uint16_t attr_handle, void *value,
                   uint16_t value_len, ble_gatt_write_fn *cb, void *cb_arg);

int ble_gatt_mtu(uint16_t conn_handle);
int ble_gatt_init(void);

#endif
