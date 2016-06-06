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

#ifndef H_BLE_GATT_
#define H_BLE_GATT_

#include <inttypes.h>
#include "host/ble_att.h"
struct ble_hs_conn;
struct ble_att_error_rsp;

#define BLE_GATT_SVC_UUID16                             0x1801
#define BLE_GATT_CHR_SERVICE_CHANGED_UUID16             0x2a05

/*** @client. */
struct ble_gatt_error {
    uint16_t status;
    uint16_t att_handle;
};

struct ble_gatt_svc {
    uint16_t start_handle;
    uint16_t end_handle;
    uint8_t uuid128[16];
};

struct ble_gatt_attr {
    uint16_t handle;
    uint16_t offset;
    uint16_t value_len;
    void *value;
};

struct ble_gatt_chr {
    uint16_t def_handle;
    uint16_t val_handle;
    uint8_t properties;
    uint8_t uuid128[16];
};

struct ble_gatt_dsc {
    uint16_t handle;
    uint8_t uuid128[16];
};

typedef int ble_gatt_mtu_fn(uint16_t conn_handle, struct ble_gatt_error *error,
                            uint16_t mtu, void *arg);
typedef int ble_gatt_disc_svc_fn(uint16_t conn_handle,
                                 struct ble_gatt_error *error,
                                 struct ble_gatt_svc *service,
                                 void *arg);
typedef int ble_gatt_attr_fn(uint16_t conn_handle,
                             struct ble_gatt_error *error,
                             struct ble_gatt_attr *attr, void *arg);
typedef int ble_gatt_reliable_attr_fn(uint16_t conn_handle,
                                      struct ble_gatt_error *error,
                                      struct ble_gatt_attr *attrs,
                                      uint8_t num_attrs, void *arg);

typedef int ble_gatt_chr_fn(uint16_t conn_handle, struct ble_gatt_error *error,
                            struct ble_gatt_chr *chr, void *arg);

typedef int ble_gatt_dsc_fn(uint16_t conn_handle, struct ble_gatt_error *error,
                            uint16_t chr_def_handle, struct ble_gatt_dsc *dsc,
                            void *arg);

int ble_gattc_exchange_mtu(uint16_t conn_handle,
                           ble_gatt_mtu_fn *cb, void *cb_arg);
int ble_gattc_disc_all_svcs(uint16_t conn_handle,
                            ble_gatt_disc_svc_fn *cb, void *cb_arg);
int ble_gattc_disc_svc_by_uuid(uint16_t conn_handle, void *service_uuid128,
                               ble_gatt_disc_svc_fn *cb, void *cb_arg);
int ble_gattc_find_inc_svcs(uint16_t conn_handle, uint16_t start_handle,
                            uint16_t end_handle,
                            ble_gatt_disc_svc_fn *cb, void *cb_arg);
int ble_gattc_disc_all_chrs(uint16_t conn_handle, uint16_t start_handle,
                            uint16_t end_handle, ble_gatt_chr_fn *cb,
                            void *cb_arg);
int ble_gattc_disc_chrs_by_uuid(uint16_t conn_handle, uint16_t start_handle,
                               uint16_t end_handle, void *uuid128,
                               ble_gatt_chr_fn *cb, void *cb_arg);
int ble_gattc_disc_all_dscs(uint16_t conn_handle, uint16_t chr_val_handle,
                            uint16_t chr_end_handle,
                            ble_gatt_dsc_fn *cb, void *cb_arg);
int ble_gattc_read(uint16_t conn_handle, uint16_t attr_handle,
                   ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_read_by_uuid(uint16_t conn_handle, uint16_t start_handle,
                           uint16_t end_handle, void *uuid128,
                           ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_read_long(uint16_t conn_handle, uint16_t handle,
                        ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_read_mult(uint16_t conn_handle, uint16_t *handles,
                        uint8_t num_handles, ble_gatt_attr_fn *cb,
                        void *cb_arg);
int ble_gattc_write_no_rsp(uint16_t conn_handle, uint16_t attr_handle,
                           void *value, uint16_t value_len);
int ble_gattc_write(uint16_t conn_handle, uint16_t attr_handle, void *value,
                    uint16_t value_len, ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_write_long(uint16_t conn_handle, uint16_t attr_handle,
                         void *value, uint16_t value_len, ble_gatt_attr_fn *cb,
                         void *cb_arg);
int ble_gattc_write_reliable(uint16_t conn_handle, struct ble_gatt_attr *attrs,
                             int num_attrs, ble_gatt_reliable_attr_fn *cb,
                             void *cb_arg);
int ble_gattc_read_dsc(uint16_t conn_handle, uint16_t attr_handle,
                       ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_read_long_dsc(uint16_t conn_handle, uint16_t attr_handle,
                            ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_write_dsc(uint16_t conn_handle, uint16_t attr_handle,
                        void *value, uint16_t value_len,
                        ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_write_long_dsc(uint16_t conn_handle, uint16_t attr_handle,
                             void *value, uint16_t value_len,
                             ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_notify(uint16_t conn_handle, uint16_t chr_val_handle);
int ble_gattc_notify_custom(uint16_t conn_handle, uint16_t att_handle,
                            void *attr_data, uint16_t attr_data_len);

int ble_gattc_init(void);

/*** @server. */

#define BLE_GATT_CHR_PROP_BROADCAST         0x01
#define BLE_GATT_CHR_PROP_READ              0x02
#define BLE_GATT_CHR_PROP_WRITE_NO_RSP      0x04
#define BLE_GATT_CHR_PROP_WRITE             0x08
#define BLE_GATT_CHR_PROP_NOTIFY            0x10
#define BLE_GATT_CHR_PROP_INDICATE          0x20
#define BLE_GATT_CHR_PROP_AUTH_SIGN_WRITE   0x40
#define BLE_GATT_CHR_PROP_EXTENDED          0x80

#define BLE_GATT_ACCESS_OP_READ_CHR         0
#define BLE_GATT_ACCESS_OP_WRITE_CHR        1
#define BLE_GATT_ACCESS_OP_READ_DSC         2
#define BLE_GATT_ACCESS_OP_WRITE_DSC        3

union ble_gatt_access_ctxt;
typedef int ble_gatt_access_fn(uint16_t conn_handle, uint16_t attr_handle,
                               uint8_t op, union ble_gatt_access_ctxt *ctxt,
                               void *arg);

typedef uint16_t ble_gatt_chr_flags;

#define BLE_GATT_CHR_F_BROADCAST            0x0001
#define BLE_GATT_CHR_F_READ                 0x0002
#define BLE_GATT_CHR_F_WRITE_NO_RSP         0x0004
#define BLE_GATT_CHR_F_WRITE                0x0008
#define BLE_GATT_CHR_F_NOTIFY               0x0010
#define BLE_GATT_CHR_F_INDICATE             0x0020
#define BLE_GATT_CHR_F_AUTH_SIGN_WRITE      0x0040
#define BLE_GATT_CHR_F_RELIABLE_WRITE       0x0080
#define BLE_GATT_CHR_F_AUX_WRITE            0x0100
#define BLE_GATT_CHR_F_READ_ENC             0x0200
#define BLE_GATT_CHR_F_READ_AUTHEN          0x0400
#define BLE_GATT_CHR_F_READ_AUTHOR          0x0800
#define BLE_GATT_CHR_F_WRITE_ENC            0x1000
#define BLE_GATT_CHR_F_WRITE_AUTHEN         0x2000
#define BLE_GATT_CHR_F_WRITE_AUTHOR         0x4000

struct ble_gatt_chr_def {
    uint8_t *uuid128;   /* NULL if no more characteristics. */
    ble_gatt_access_fn *access_cb;
    void *arg;
    struct ble_gatt_dsc_def *descriptors;
    ble_gatt_chr_flags flags;
};

#define BLE_GATT_SVC_TYPE_END       0
#define BLE_GATT_SVC_TYPE_PRIMARY   1
#define BLE_GATT_SVC_TYPE_SECONDARY 2

struct ble_gatt_svc_def {
    uint8_t type;
    uint8_t *uuid128;
    const struct ble_gatt_svc_def **includes; /* Terminated with null. */
    struct ble_gatt_chr_def *characteristics;
};

union ble_gatt_access_ctxt {
    struct {
        const struct ble_gatt_chr_def *chr;
        void *data;
        int len;
    } chr_access;

    struct {
        const struct ble_gatt_dsc_def *dsc;
        void *data;
        int len;
    } dsc_access;
};

struct ble_gatt_dsc_def {
    uint8_t *uuid128;
    uint8_t att_flags;
    ble_gatt_access_fn *access_cb;
    void *arg;
};

#define BLE_GATT_REGISTER_OP_SVC    1
#define BLE_GATT_REGISTER_OP_CHR    2
#define BLE_GATT_REGISTER_OP_DSC    3

union ble_gatt_register_ctxt;
typedef void ble_gatt_register_fn(uint8_t op,
                                  union ble_gatt_register_ctxt *ctxt,
                                  void *arg);

int ble_gatts_register_svcs(const struct ble_gatt_svc_def *svcs,
                            ble_gatt_register_fn *register_cb,
                            void *cb_arg);
void ble_gatts_chr_updated(uint16_t chr_def_handle);

union ble_gatt_register_ctxt {
    struct {
        uint16_t handle;
        const struct ble_gatt_svc_def *svc;
    } svc_reg;

    struct {
        uint16_t def_handle;
        uint16_t val_handle;
        const struct ble_gatt_chr_def *chr;
    } chr_reg;

    struct {
        uint16_t dsc_handle;
        const struct ble_gatt_dsc_def *dsc;
        uint16_t chr_def_handle;
        const struct ble_gatt_chr_def *chr;
    } dsc_reg;
};

#endif
