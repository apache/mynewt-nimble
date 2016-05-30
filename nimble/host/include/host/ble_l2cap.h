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

#define BLE_SM_OP_PAIR_REQ                      0x01
#define BLE_SM_OP_PAIR_RSP                      0x02
#define BLE_SM_OP_PAIR_CONFIRM                  0x03
#define BLE_SM_OP_PAIR_RANDOM                   0x04
#define BLE_SM_OP_PAIR_FAIL                     0x05
#define BLE_SM_OP_ENC_INFO                      0x06
#define BLE_SM_OP_MASTER_ID                     0x07
#define BLE_SM_OP_IDENTITY_INFO                 0x08
#define BLE_SM_OP_IDENTITY_ADDR_INFO            0x09
#define BLE_SM_OP_SIGN_INFO                     0x0a
#define BLE_SM_OP_SEC_REQ                       0x0b
#define BLE_SM_OP_PAIR_PUBLIC_KEY               0x0c
#define BLE_SM_OP_PAIR_DHKEY_CHECK              0x0d
#define BLE_SM_OP_PAIR_KEYPRESS_NOTIFY          0x0e

#define BLE_SM_ERR_PASSKEY                      0x01
#define BLE_SM_ERR_OOB                          0x02
#define BLE_SM_ERR_AUTHREQ                      0x03
#define BLE_SM_ERR_CONFIRM_MISMATCH             0x04
#define BLE_SM_ERR_PAIR_NOT_SUPP                0x05
#define BLE_SM_ERR_ENC_KEY_SZ                   0x06
#define BLE_SM_ERR_CMD_NOT_SUPP                 0x07
#define BLE_SM_ERR_UNSPECIFIED                  0x08
#define BLE_SM_ERR_REPEATED                     0x09
#define BLE_SM_ERR_INVAL                        0x0a
#define BLE_SM_ERR_DHKEY                        0x0b
#define BLE_SM_ERR_NUM_CMP                      0x0c
#define BLE_SM_ERR_ALREADY                      0x0d
#define BLE_SM_ERR_CROSS_TRANS                  0x0e
#define BLE_SM_ERR_MAX_PLUS_1                   0x0f

#define BLE_SM_PAIR_ALG_JW                      0
#define BLE_SM_PAIR_ALG_PASSKEY                 1
#define BLE_SM_PAIR_ALG_OOB                     2
#define BLE_SM_PAIR_ALG_NUM_CMP                 3

#define BLE_SM_PAIR_KEY_DIST_ENC                0x01
#define BLE_SM_PAIR_KEY_DIST_ID                 0x02
#define BLE_SM_PAIR_KEY_DIST_SIGN               0x04
#define BLE_SM_PAIR_KEY_DIST_LINK               0x08
#define BLE_SM_PAIR_KEY_DIST_RESERVED           0xf0

#define BLE_SM_IO_CAP_DISP_ONLY                 0x00
#define BLE_SM_IO_CAP_DISP_YES_NO               0x01
#define BLE_SM_IO_CAP_KEYBOARD_ONLY             0x02
#define BLE_SM_IO_CAP_NO_IO                     0x03
#define BLE_SM_IO_CAP_KEYBOARD_DISP             0x04
#define BLE_SM_IO_CAP_RESERVED                  0x05

#define BLE_SM_PAIR_OOB_NO                      0x00
#define BLE_SM_PAIR_OOB_YES                     0x01
#define BLE_SM_PAIR_OOB_RESERVED                0x02

#define BLE_SM_PAIR_AUTHREQ_BOND                0x01
#define BLE_SM_PAIR_AUTHREQ_MITM                0x04
#define BLE_SM_PAIR_AUTHREQ_SC                  0x08
#define BLE_SM_PAIR_AUTHREQ_KEYPRESS            0x10
#define BLE_SM_PAIR_AUTHREQ_RESERVED            0xe2

#define BLE_SM_PAIR_KEY_SZ_MIN                  7
#define BLE_SM_PAIR_KEY_SZ_MAX                  16

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

/* Strucure to pass the passkey info back to the l2cap */
struct ble_sm_passkey
{
    uint8_t action;
    union
    {
        uint32_t passkey;
        uint8_t  oob[16];
    };
};

#if NIMBLE_OPT(SM)
/* If the application gets a passkey action, it must peform the action
 * and then call this function to notify the host that the action is
 * complete
 */
int ble_sm_set_tk(uint16_t conn_handle, struct ble_sm_passkey *pkey);
void ble_sm_connection_broken(uint16_t conn_handle);
#else
#define ble_sm_set_tk(conn_handle, pkey) (BLE_HS_ENOTSUP)
#define ble_sm_connection_broken(conn_handle)
#endif

#endif
