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

#ifndef H_BLE_ATT_
#define H_BLE_ATT_

#define BLE_ATT_UUID_PRIMARY_SERVICE        0x2800
#define BLE_ATT_UUID_SECONDARY_SERVICE      0x2801
#define BLE_ATT_UUID_INCLUDE                0x2802
#define BLE_ATT_UUID_CHARACTERISTIC         0x2803

#define BLE_ATT_ERR_INVALID_HANDLE          0x01
#define BLE_ATT_ERR_INVALID_PDU             0x04
#define BLE_ATT_ERR_REQ_NOT_SUPPORTED       0x06
#define BLE_ATT_ERR_INVALID_OFFSET          0x07
#define BLE_ATT_ERR_PREPARE_QUEUE_FULL      0x09
#define BLE_ATT_ERR_ATTR_NOT_FOUND          0x0a
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
#define BLE_ATT_OP_READ_GROUP_TYPE_REQ      0x10
#define BLE_ATT_OP_READ_GROUP_TYPE_RSP      0x11
#define BLE_ATT_OP_WRITE_REQ                0x12
#define BLE_ATT_OP_WRITE_RSP                0x13
#define BLE_ATT_OP_PREP_WRITE_REQ           0x16
#define BLE_ATT_OP_PREP_WRITE_RSP           0x17
#define BLE_ATT_OP_EXEC_WRITE_REQ           0x18
#define BLE_ATT_OP_EXEC_WRITE_RSP           0x19
#define BLE_ATT_OP_WRITE_CMD                0x52

#define BLE_ATT_ATTR_MAX_LEN                512

union ble_att_svr_handle_arg {
    struct {
        void *attr_data;
        int attr_len;
    } aha_read;

    struct {
        void *attr_data;
        int attr_len;
    } aha_write;
};

#define HA_FLAG_PERM_READ                   (1 << 0)
#define HA_FLAG_PERM_WRITE                  (1 << 1)
#define HA_FLAG_PERM_RW                     (1 << 2)
#define HA_FLAG_ENC_REQ                     (1 << 3)
#define HA_FLAG_AUTHENTICATION_REQ          (1 << 4)
#define HA_FLAG_AUTHORIZATION_REQ           (1 << 5)

struct ble_att_svr_entry;

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
typedef int ble_att_svr_handle_func(struct ble_att_svr_entry *entry,
                                    uint8_t op,
                                    union ble_att_svr_handle_arg *arg);

struct ble_att_svr_entry {
    STAILQ_ENTRY(ble_att_svr_entry) ha_next;

    uint8_t ha_uuid[16];
    uint8_t ha_flags;
    uint8_t ha_pad1;
    uint16_t ha_handle_id;
    ble_att_svr_handle_func *ha_fn;
};

int ble_att_svr_register(uint8_t *uuid, uint8_t flags, uint16_t *handle_id,
                         ble_att_svr_handle_func *fn);

#endif
