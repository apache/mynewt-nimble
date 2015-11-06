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

#define BLE_HS_ATT_OP_ERROR_RSP     0x01
#define BLE_HS_ATT_OP_READ_REQ      0x0a

/**
 * | Parameter                          | Size (octets) |
 * +------------------------------------+---------------+
 * | Attribute Opcode                   | 1             |
 * | Attribute Handle                   | 2             |
 */
#define BLE_HS_ATT_READ_REQ_SZ      3
struct ble_hs_att_read_req {
    uint8_t bharq_op;
    uint16_t bharq_handle;
};

/**
 * | Parameter                          | Size (octets) |
 * +------------------------------------+---------------+
 * | Attribute Opcode                   | 1             |
 * | Request Opcode In Error            | 1             |
 * | Attribute Handle In Error          | 2             |
 * | Error Code                         | 1             |
 */
#define BLE_HS_ATT_ERROR_RSP_SZ     5
struct ble_hs_att_error_rsp {
    uint8_t bhaep_op;
    uint8_t bhaep_req_op;
    uint16_t bhaep_handle;
    uint8_t bhaep_error_code;
};

int ble_hs_att_read_req_parse(void *payload, int len,
                              struct ble_hs_att_read_req *req);
int ble_hs_att_read_req_write(void *payload, int len,
                              struct ble_hs_att_read_req *req);
int ble_hs_att_error_rsp_parse(void *payload, int len,
                               struct ble_hs_att_error_rsp *rsp);
int ble_hs_att_error_rsp_write(void *payload, int len,
                               struct ble_hs_att_error_rsp *rsp);
