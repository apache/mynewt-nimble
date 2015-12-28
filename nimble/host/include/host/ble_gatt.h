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
#include "host/ble_att.h"
struct ble_hs_conn;
struct ble_att_error_rsp;
struct ble_att_clt_adata;

/*** @client. */
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

typedef int ble_gatt_disc_service_fn(uint16_t conn_handle, int status,
                                     struct ble_gatt_service *service,
                                     void *arg);
typedef int ble_gatt_attr_fn(uint16_t conn_handle, int status,
                             struct ble_gatt_attr *attr, void *arg);

typedef int ble_gatt_chr_fn(uint16_t conn_handle, int status,
                            struct ble_gatt_chr *chr, void *arg);

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
                          ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gatt_write(uint16_t conn_handle, uint16_t attr_handle, void *value,
                   uint16_t value_len, ble_gatt_attr_fn *cb, void *cb_arg);

int ble_gatt_exchange_mtu(uint16_t conn_handle);
int ble_gatt_init(void);

/*** @server. */

#define BLE_GATT_ACCESS_OP_READ_CHR         0
#define BLE_GATT_ACCESS_OP_WRITE_CHR        1
/* XXX: Notify, listen. */

union ble_gatt_access_ctxt;
typedef int ble_gatt_access_fn(uint16_t handle_id, uint8_t op,
                               union ble_gatt_access_ctxt *ctxt, void *arg);

struct ble_gatt_chr_def {
    uint8_t *uuid128;   /* NULL if no more characteristics. */
    ble_gatt_access_fn *access_cb;
    void *arg;
    struct ble_gatt_dsc_def *descriptors;
    uint8_t properties;
};

#define BLE_GATT_SVC_TYPE_END       0
#define BLE_GATT_SVC_TYPE_PRIMARY   1
#define BLE_GATT_SVC_TYPE_SECONDARY 2

struct ble_gatt_svc_def {
    uint8_t type;
    uint8_t *uuid128;
    struct ble_gatt_svc_def **includes; /* Terminated with null. */
    struct ble_gatt_chr_def *characteristics;
};

union ble_gatt_access_ctxt {
    struct {
        const struct ble_gatt_chr_def *chr;
        void *chr_data;
        int chr_len;
    } bgc_read;

    struct {
        const struct ble_gatt_chr_def *chr;
        void *chr_data;
        int chr_len;
    } bgc_write;
};

struct ble_gatt_dsc_def {
    uint8_t *uuid128;
    uint8_t att_flags;
    ble_att_svr_access_fn *access_cb;
    void *arg;
};

#define BLE_GATT_REGISTER_OP_SVC    0
#define BLE_GATT_REGISTER_OP_CHR    1
#define BLE_GATT_REGISTER_OP_DSC    2

union ble_gatt_register_ctxt;
typedef void ble_gatt_register_fn(uint8_t op,
                                  union ble_gatt_register_ctxt *ctxt);

int ble_gatt_register_services(const struct ble_gatt_svc_def *svcs,
                               ble_gatt_register_fn *register_cb);

union ble_gatt_register_ctxt {
    struct {
        uint16_t handle;
        const struct ble_gatt_svc_def *svc;
    } bgr_svc;

    struct {
        uint16_t def_handle;
        uint16_t val_handle;
        const struct ble_gatt_chr_def *chr;
    } bgr_chr;

    struct {
        uint16_t dsc_handle;
        const struct ble_gatt_dsc_def *dsc;
        uint16_t chr_def_handle;
        const struct ble_gatt_chr_def *chr;
    } bgr_dsc;
};

#endif
