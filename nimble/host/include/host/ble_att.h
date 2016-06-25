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

#ifndef H_BLE_ATT_
#define H_BLE_ATT_

#include "os/queue.h"

#define BLE_ATT_UUID_PRIMARY_SERVICE        0x2800
#define BLE_ATT_UUID_SECONDARY_SERVICE      0x2801
#define BLE_ATT_UUID_INCLUDE                0x2802
#define BLE_ATT_UUID_CHARACTERISTIC         0x2803

#define BLE_ATT_ERR_INVALID_HANDLE          0x01
#define BLE_ATT_ERR_READ_NOT_PERMITTED      0x02
#define BLE_ATT_ERR_WRITE_NOT_PERMITTED     0x03
#define BLE_ATT_ERR_INVALID_PDU             0x04
#define BLE_ATT_ERR_INSUFFICIENT_AUTHENT    0x05
#define BLE_ATT_ERR_REQ_NOT_SUPPORTED       0x06
#define BLE_ATT_ERR_INVALID_OFFSET          0x07
#define BLE_ATT_ERR_PREPARE_QUEUE_FULL      0x09
#define BLE_ATT_ERR_ATTR_NOT_FOUND          0x0a
#define BLE_ATT_ERR_ATTR_NOT_LONG           0x0b
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN  0x0d
#define BLE_ATT_ERR_UNLIKELY                0x0e
#define BLE_ATT_ERR_UNSUPPORTED_GROUP       0x10
#define BLE_ATT_ERR_INSUFFICIENT_RES        0x11

#define BLE_ATT_OP_ERROR_RSP                0x01
#define BLE_ATT_OP_MTU_REQ                  0x02
#define BLE_ATT_OP_MTU_RSP                  0x03
#define BLE_ATT_OP_FIND_INFO_REQ            0x04
#define BLE_ATT_OP_FIND_INFO_RSP            0x05
#define BLE_ATT_OP_FIND_TYPE_VALUE_REQ      0x06
#define BLE_ATT_OP_FIND_TYPE_VALUE_RSP      0x07
#define BLE_ATT_OP_READ_TYPE_REQ            0x08
#define BLE_ATT_OP_READ_TYPE_RSP            0x09
#define BLE_ATT_OP_READ_REQ                 0x0a
#define BLE_ATT_OP_READ_RSP                 0x0b
#define BLE_ATT_OP_READ_BLOB_REQ            0x0c
#define BLE_ATT_OP_READ_BLOB_RSP            0x0d
#define BLE_ATT_OP_READ_MULT_REQ            0x0e
#define BLE_ATT_OP_READ_MULT_RSP            0x0f
#define BLE_ATT_OP_READ_GROUP_TYPE_REQ      0x10
#define BLE_ATT_OP_READ_GROUP_TYPE_RSP      0x11
#define BLE_ATT_OP_WRITE_REQ                0x12
#define BLE_ATT_OP_WRITE_RSP                0x13
#define BLE_ATT_OP_PREP_WRITE_REQ           0x16
#define BLE_ATT_OP_PREP_WRITE_RSP           0x17
#define BLE_ATT_OP_EXEC_WRITE_REQ           0x18
#define BLE_ATT_OP_EXEC_WRITE_RSP           0x19
#define BLE_ATT_OP_NOTIFY_REQ               0x1b
#define BLE_ATT_OP_INDICATE_REQ             0x1d
#define BLE_ATT_OP_INDICATE_RSP             0x1e
#define BLE_ATT_OP_WRITE_CMD                0x52

#define BLE_ATT_ATTR_MAX_LEN                512

#define BLE_ATT_F_READ                      0x01
#define BLE_ATT_F_WRITE                     0x02
#define BLE_ATT_F_READ_ENC                  0x04
#define BLE_ATT_F_READ_AUTHEN               0x08
#define BLE_ATT_F_READ_AUTHOR               0x10
#define BLE_ATT_F_WRITE_ENC                 0x20
#define BLE_ATT_F_WRITE_AUTHEN              0x40
#define BLE_ATT_F_WRITE_AUTHOR              0x80

#define HA_FLAG_PERM_RW                     (BLE_ATT_F_READ | BLE_ATT_F_WRITE)

#define BLE_ATT_ACCESS_OP_READ              1
#define BLE_ATT_ACCESS_OP_WRITE             2

/**
 * Context for reads of characteristics and descriptors.  An instance of this
 * gets passed to an application callback whenever a local characteristic or
 * descriptor is read.
 */
struct ble_att_read_ctxt {
    /**
     * (stack --> app)
     * Maximum number of data bytes the stack can send in the read response.
     * This is based on the connection's ATT MTU.
     */
    uint16_t max_data_len;

    /**
     * (stack --> app)
     * A buffer that the app can use to write the characteristic response value
     * to.  This buffer can hold up to max_data_len bytes.
     */
    uint8_t *buf;

    /**
     * (app --> stack)
     * App points this at the characteristic data to respond with.  Initially
     * this points to "buf".
     */
    const void *data;

    /**
     * (app --> stack)
     * App fills this with the number of data bytes contained in characteristic
     * response.
     */
    uint16_t len;

    uint16_t offset;
};

/**
 * Context for writes of characteristics and descriptors.  An instance of this
 * gets passed to an application callback whenever a local characteristic or
 * descriptor is written.
 */
struct ble_att_write_ctxt {
    /**
     * (stack --> app)
     * The data that the peer is writing to the characteristic.
     */
    const void *data;

    /**
     * (stack --> app)
     * The number of bytes of characteristic data being written.
     */
    int len;

    uint16_t offset;
};

struct ble_att_svr_access_ctxt {
    union {
        struct ble_att_read_ctxt read;
        struct ble_att_write_ctxt write;
    };
};

/**
 * Handles a host attribute request.
 *
 * @param entry                 The host attribute being requested.
 * @param op                    The operation being performed on the attribute.
 * @param arg                   The request data associated with that host
 *                                  attribute.
 *
 * @return                      0 on success;
 *                              One of the BLE_ATT_ERR_[...] codes on
 *                                  failure.
 */
typedef int ble_att_svr_access_fn(uint16_t conn_handle, uint16_t attr_handle,
                                  uint8_t *uuid128, uint8_t op,
                                  struct ble_att_svr_access_ctxt *ctxt,
                                  void *arg);

int ble_att_svr_register(const uint8_t *uuid, uint8_t flags,
                         uint16_t *handle_id,
                         ble_att_svr_access_fn *cb, void *cb_arg);
int ble_att_svr_register_uuid16(uint16_t uuid16, uint8_t flags,
                                uint16_t *handle_id, ble_att_svr_access_fn *cb,
                                void *cb_arg);

typedef int ble_att_svr_notify_fn(uint16_t conn_handle, uint16_t attr_handle,
                                  uint8_t *attr_val, uint16_t attr_len,
                                  void *arg);

int ble_att_svr_read_local(uint16_t attr_handle, const void **out_data,
                           uint16_t *out_attr_len);
int ble_att_svr_write_local(uint16_t attr_handle, const void *data,
                            uint16_t data_len);

int ble_att_set_preferred_mtu(uint16_t mtu);

#endif
