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

#include <inttypes.h>
struct ble_l2cap_chan;

#define BLE_ATT_OP_ERROR_RSP             0x01
#define BLE_ATT_OP_MTU_REQ               0x02
#define BLE_ATT_OP_MTU_RSP               0x03
#define BLE_ATT_OP_FIND_INFO_REQ         0x04
#define BLE_ATT_OP_FIND_INFO_RSP         0x05
#define BLE_ATT_OP_FIND_TYPE_VALUE_REQ   0x06
#define BLE_ATT_OP_FIND_TYPE_VALUE_RSP   0x07
#define BLE_ATT_OP_READ_TYPE_REQ         0x08
#define BLE_ATT_OP_READ_TYPE_RSP         0x09
#define BLE_ATT_OP_READ_REQ              0x0a
#define BLE_ATT_OP_READ_RSP              0x0b
#define BLE_ATT_OP_READ_GROUP_TYPE_REQ   0x10
#define BLE_ATT_OP_READ_GROUP_TYPE_RSP   0x11
#define BLE_ATT_OP_WRITE_REQ             0x12
#define BLE_ATT_OP_WRITE_RSP             0x13

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Request Opcode In Error            | 1                 |
 * | Attribute Handle In Error          | 2                 |
 * | Error Code                         | 1                 |
 */
#define BLE_ATT_ERROR_RSP_SZ             5
struct ble_att_error_rsp {
    uint8_t bhaep_req_op;
    uint16_t bhaep_handle;
    uint8_t bhaep_error_code;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Server Rx MTU                      | 2                 |
 */
#define BLE_ATT_MTU_CMD_SZ               3
struct ble_att_mtu_cmd {
    uint16_t bhamc_mtu;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Starting Handle                    | 2                 |
 * | Ending Handle                      | 2                 |
 */
#define BLE_ATT_FIND_INFO_REQ_SZ         5
struct ble_att_find_info_req {
    uint16_t bhafq_start_handle;
    uint16_t bhafq_end_handle;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Format                             | 1                 |
 * | Information Data                   | 4 to (ATT_MTU-2)  |
 */
#define BLE_ATT_FIND_INFO_RSP_BASE_SZ       2
struct ble_att_find_info_rsp {
    uint8_t bhafp_format;
    /* Followed by information data. */
};

#define BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT   1
#define BLE_ATT_FIND_INFO_RSP_FORMAT_128BIT  2

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Starting Handle                    | 2                 |
 * | Ending Handle                      | 2                 |
 * | Attribute Type                     | 2                 |
 * | Attribute Value                    | 0 to (ATT_MTU-7)  |
 */
#define BLE_ATT_FIND_TYPE_VALUE_REQ_BASE_SZ   7
struct ble_att_find_type_value_req {
    uint16_t bhavq_start_handle;
    uint16_t bhavq_end_handle;
    uint16_t bhavq_attr_type;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Information Data                   | 4 to (ATT_MTU-1)  |
 */
#define BLE_ATT_FIND_TYPE_VALUE_RSP_BASE_SZ     1
#define BLE_ATT_FIND_TYPE_VALUE_HINFO_BASE_SZ   4

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Starting Handle                    | 2                 |
 * | Ending Handle                      | 2                 |
 * | Attribute Type                     | 2 or 16           |
 */
#define BLE_ATT_READ_TYPE_REQ_BASE_SZ    5
#define BLE_ATT_READ_TYPE_REQ_SZ_16      7
#define BLE_ATT_READ_TYPE_REQ_SZ_128     21
struct ble_att_read_type_req {
    uint16_t bhatq_start_handle;
    uint16_t bhatq_end_handle;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Length                             | 1                 |
 * | Attribute Data List                | 2 to (ATT_MTU-2)  |
 */
#define BLE_ATT_READ_TYPE_RSP_MIN_SZ     2
struct ble_att_read_type_rsp {
    uint8_t bhatp_len;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Attribute Handle                   | 2                 |
 */
#define BLE_ATT_READ_REQ_SZ              3
struct ble_att_read_req {
    uint16_t bharq_handle;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Attribute Value                    | 0 to (ATT_MTU-1)  |
 */
#define BLE_ATT_READ_RSP_MIN_SZ          1

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Starting Handle                    | 2                 |
 * | Ending Handle                      | 2                 |
 * | Attribute Group Type               | 2 or 16           |
 */
#define BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ  5
#define BLE_ATT_READ_GROUP_TYPE_REQ_SZ_16    7
#define BLE_ATT_READ_GROUP_TYPE_REQ_SZ_128   21
struct ble_att_read_group_type_req {
    uint16_t bhagq_start_handle;
    uint16_t bhagq_end_handle;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Length                             | 1                 |
 * | Attribute Data List                | 2 to (ATT_MTU-2)  |
 */
#define BLE_ATT_READ_GROUP_TYPE_RSP_BASE_SZ  2
struct ble_att_read_group_type_rsp {
    uint8_t bhagp_length;
};

#define BLE_ATT_READ_GROUP_TYPE_ADATA_BASE_SZ   4

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Attribute Handle                   | 2                 |
 * | Attribute Value                    | 0 to (ATT_MTU-3)  |
 */
#define BLE_ATT_WRITE_REQ_MIN_SZ         3
struct ble_att_write_req {
    uint16_t bhawq_handle;
};

#define BLE_ATT_WRITE_RSP_SZ             1

int ble_att_error_rsp_parse(void *payload, int len,
                            struct ble_att_error_rsp *rsp);
int ble_att_error_rsp_write(void *payload, int len,
                            struct ble_att_error_rsp *rsp);
int ble_att_mtu_cmd_parse(void *payload, int len,
                          struct ble_att_mtu_cmd *cmd);
int ble_att_mtu_req_write(void *payload, int len,
                          struct ble_att_mtu_cmd *cmd);
int ble_att_mtu_rsp_write(void *payload, int len,
                          struct ble_att_mtu_cmd *cmd);
int ble_att_find_info_req_parse(void *payload, int len,
                                struct ble_att_find_info_req *req);
int ble_att_find_info_req_write(void *payload, int len,
                                struct ble_att_find_info_req *req);
int ble_att_find_info_rsp_parse(void *payload, int len,
                                struct ble_att_find_info_rsp *rsp);
int ble_att_find_info_rsp_write(void *payload, int len,
                                struct ble_att_find_info_rsp *rsp);
int ble_att_find_type_value_req_parse(
    void *payload, int len, struct ble_att_find_type_value_req *req);
int ble_att_find_type_value_req_write(
    void *payload, int len, struct ble_att_find_type_value_req *req);
int ble_att_read_req_parse(void *payload, int len,
                           struct ble_att_read_req *req);
int ble_att_read_req_write(void *payload, int len,
                           struct ble_att_read_req *req);
int ble_att_read_type_req_parse(void *payload, int len,
                                struct ble_att_read_type_req *req);
int ble_att_read_type_req_write(void *payload, int len,
                                struct ble_att_read_type_req *req);
int ble_att_read_type_rsp_parse(void *payload, int len,
                                struct ble_att_read_type_rsp *rsp);
int ble_att_read_type_rsp_write(void *payload, int len,
                                struct ble_att_read_type_rsp *rsp);
int ble_att_read_group_type_req_parse(
    void *payload, int len, struct ble_att_read_group_type_req *req);
int ble_att_read_group_type_req_write(
    void *payload, int len, struct ble_att_read_group_type_req *req);
int ble_att_read_group_type_rsp_parse(
    void *payload, int len, struct ble_att_read_group_type_rsp *rsp);
int ble_att_read_group_type_rsp_write(
    void *payload, int len, struct ble_att_read_group_type_rsp *rsp);
int ble_att_write_req_parse(void *payload, int len,
                            struct ble_att_write_req *req);
int ble_att_write_req_write(void *payload, int len,
                            struct ble_att_write_req *req);
