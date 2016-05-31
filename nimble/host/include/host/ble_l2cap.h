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

#ifndef H_BLE_L2CAP_
#define H_BLE_L2CAP_

#include "nimble/nimble_opt.h"
struct ble_l2cap_sig_update_req;
struct ble_hs_conn;

#define BLE_L2CAP_SIG_OP_REJECT                 0x01
#define BLE_L2CAP_SIG_OP_CONNECT_REQ            0x02
#define BLE_L2CAP_SIG_OP_CONNECT_RSP            0x03
#define BLE_L2CAP_SIG_OP_CONFIG_REQ             0x04
#define BLE_L2CAP_SIG_OP_CONFIG_RSP             0x05
#define BLE_L2CAP_SIG_OP_DISCONN_REQ            0x06
#define BLE_L2CAP_SIG_OP_DISCONN_RSP            0x07
#define BLE_L2CAP_SIG_OP_ECHO_REQ               0x08
#define BLE_L2CAP_SIG_OP_ECHO_RSP               0x09
#define BLE_L2CAP_SIG_OP_INFO_REQ               0x0a
#define BLE_L2CAP_SIG_OP_INFO_RSP               0x0b
#define BLE_L2CAP_SIG_OP_CREATE_CHAN_REQ        0x0c
#define BLE_L2CAP_SIG_OP_CREATE_CHAN_RSP        0x0d
#define BLE_L2CAP_SIG_OP_MOVE_CHAN_REQ          0x0e
#define BLE_L2CAP_SIG_OP_MOVE_CHAN_RSP          0x0f
#define BLE_L2CAP_SIG_OP_MOVE_CHAN_CONF_REQ     0x10
#define BLE_L2CAP_SIG_OP_MOVE_CHAN_CONF_RSP     0x11
#define BLE_L2CAP_SIG_OP_UPDATE_REQ             0x12
#define BLE_L2CAP_SIG_OP_UPDATE_RSP             0x13
#define BLE_L2CAP_SIG_OP_CREDIT_CONNECT_REQ     0x14
#define BLE_L2CAP_SIG_OP_CREDIT_CONNECT_RSP     0x15
#define BLE_L2CAP_SIG_OP_FLOW_CTRL_CREDIT       0x16
#define BLE_L2CAP_SIG_OP_MAX                    0x17

#define BLE_L2CAP_SIG_ERR_CMD_NOT_UNDERSTOOD    0x0000
#define BLE_L2CAP_SIG_ERR_MTU_EXCEEDED          0x0001
#define BLE_L2CAP_SIG_ERR_INVALID_CID           0x0002

typedef void ble_l2cap_sig_update_fn(int status, void *arg);

struct ble_l2cap_sig_update_params {
    uint16_t itvl_min;
    uint16_t itvl_max;
    uint16_t slave_latency;
    uint16_t timeout_multiplier;
};

int ble_l2cap_sig_update(uint16_t conn_handle,
                         struct ble_l2cap_sig_update_params *params,
                         ble_l2cap_sig_update_fn *cb, void *cb_arg);

#endif
