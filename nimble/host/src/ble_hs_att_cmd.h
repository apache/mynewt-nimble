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

#define BLE_HS_ATT_OP_ERROR_RSP             0x01
#define BLE_HS_ATT_OP_MTU_REQ               0x02
#define BLE_HS_ATT_OP_MTU_RSP               0x03
#define BLE_HS_ATT_OP_FIND_INFO_REQ         0x04
#define BLE_HS_ATT_OP_FIND_INFO_RSP         0x05
#define BLE_HS_ATT_OP_READ_REQ              0x0a
#define BLE_HS_ATT_OP_READ_RSP              0x0b
#define BLE_HS_ATT_OP_WRITE_REQ             0x12
#define BLE_HS_ATT_OP_WRITE_RSP             0x13

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Request Opcode In Error            | 1                 |
 * | Attribute Handle In Error          | 2                 |
 * | Error Code                         | 1                 |
 */
#define BLE_HS_ATT_ERROR_RSP_SZ             5
struct ble_hs_att_error_rsp {
    uint8_t bhaep_op;
    uint8_t bhaep_req_op;
    uint16_t bhaep_handle;
    uint8_t bhaep_error_code;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Client Rx MTU                      | 2                 |
 */
#define BLE_HS_ATT_MTU_REQ_SZ               3
struct ble_hs_att_mtu_req {
    uint8_t bhamq_op;
    uint16_t bhamq_mtu;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Server Rx MTU                      | 2                 |
 */
#define BLE_HS_ATT_MTU_RSP_SZ               3
struct ble_hs_att_mtu_rsp {
    uint8_t bhamp_op;
    uint16_t bhamp_mtu;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Starting Handle                    | 2                 |
 * | Ending Handle                      | 2                 |
 */
#define BLE_HS_ATT_FIND_INFO_REQ_SZ         5
struct ble_hs_att_find_info_req {
    uint8_t bhafq_op;
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
#define BLE_HS_ATT_FIND_INFO_RSP_MIN_SZ     2
struct ble_hs_att_find_info_rsp {
    uint8_t bhafp_op;
    uint8_t bhafp_format;
    /* Followed by information data. */
};

#define BLE_HS_ATT_FIND_INFO_RSP_FORMAT_16BIT   1
#define BLE_HS_ATT_FIND_INFO_RSP_FORMAT_128BIT  2

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Attribute Handle                   | 2                 |
 */
#define BLE_HS_ATT_READ_REQ_SZ              3
struct ble_hs_att_read_req {
    uint8_t bharq_op;
    uint16_t bharq_handle;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Attribute Value                    | 0 to (ATT_MTU-1)  |
 */
#define BLE_HS_ATT_READ_RSP_MIN_SZ          1

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | Attribute Opcode                   | 1                 |
 * | Attribute Handle                   | 2                 |
 * | Attribute Value                    | 0 to (ATT_MTU-3)  |
 */
#define BLE_HS_ATT_WRITE_REQ_MIN_SZ         3
struct ble_hs_att_write_req {
    uint8_t bhawq_op;
    uint16_t bhawq_handle;
};

#define BLE_HS_ATT_WRITE_RSP_SZ             1

int ble_hs_att_error_rsp_parse(void *payload, int len,
                               struct ble_hs_att_error_rsp *rsp);
int ble_hs_att_error_rsp_write(void *payload, int len,
                               struct ble_hs_att_error_rsp *rsp);
int ble_hs_att_mtu_req_parse(void *payload, int len,
                             struct ble_hs_att_mtu_req *req);
int ble_hs_att_mtu_rsp_parse(void *payload, int len,
                             struct ble_hs_att_mtu_rsp *rsp);
int ble_hs_att_find_info_req_parse(void *payload, int len,
                                   struct ble_hs_att_find_info_req *req);
int ble_hs_att_find_info_req_write(void *payload, int len,
                                   struct ble_hs_att_find_info_req *req);
int ble_hs_att_find_info_rsp_parse(void *payload, int len,
                                   struct ble_hs_att_find_info_rsp *rsp);
int ble_hs_att_find_info_rsp_write(void *payload, int len,
                                   struct ble_hs_att_find_info_rsp *rsp);
int ble_hs_att_read_req_parse(void *payload, int len,
                              struct ble_hs_att_read_req *req);
int ble_hs_att_read_req_write(void *payload, int len,
                              struct ble_hs_att_read_req *req);
int ble_hs_att_write_req_parse(void *payload, int len,
                               struct ble_hs_att_write_req *req);
int ble_hs_att_write_req_write(void *payload, int len,
                               struct ble_hs_att_write_req *req);
