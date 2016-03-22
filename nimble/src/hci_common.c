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
#include <stdint.h>
#include "os/os.h"
#include "nimble/ble.h"
#include "nimble/hci_common.h"

/* 
 * Lengths for commands. NOTE: 0xFF is a special case and means that there
 * is no fixed length for the command or is a command that we have not
 * implemented.
 */ 
const uint8_t g_ble_hci_le_cmd_len[BLE_HCI_NUM_LE_CMDS] = 
{
    0,                                  /* 0x0000: reserved */
    BLE_HCI_SET_LE_EVENT_MASK_LEN,      /* 0x0001: set event mask */
    BLE_HCI_RD_BUF_SIZE_LEN,            /* 0x0002: read buffer size */
    0,                                  /* 0x0003: read local supp features */
    0,                                  /* 0x0004: not defined */
    BLE_DEV_ADDR_LEN,                   /* 0x0005: set random address */
    BLE_HCI_SET_ADV_PARAM_LEN,          /* 0x0006: set advertising parameters */
    0,                                  /* 0x0007: read adv chan tx power */
    0xFF,                               /* 0x0008: set advertising data */
    0xFF,                               /* 0x0009: set scan rsp data */
    BLE_HCI_SET_ADV_ENABLE_LEN,         /* 0x000A: set advertising enable */
    BLE_HCI_SET_SCAN_PARAM_LEN,         /* 0x000B: set scan parameters */
    BLE_HCI_SET_SCAN_ENABLE_LEN,        /* 0x000C: set scan enable */
    BLE_HCI_CREATE_CONN_LEN,            /* 0x000D: create connection */
    0,                                  /* 0x000E: create connection cancel*/
    0,                                  /* 0x000F: read whitelist size */
    0,                                  /* 0x0010: clear white list */
    BLE_HCI_CHG_WHITE_LIST_LEN,         /* 0x0011: add to white list */
    BLE_HCI_CHG_WHITE_LIST_LEN,         /* 0x0012: remove from white list */
    BLE_HCI_CONN_UPDATE_LEN,            /* 0x0013: connection update */
    BLE_HCI_SET_HOST_CHAN_CLASS_LEN,    /* 0x0014: set host chan class */
    sizeof(uint16_t),                   /* 0x0015: read channel map */
    BLE_HCI_CONN_RD_REM_FEAT_LEN,       /* 0x0016: read remote features */
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0,                                  /* 0x001C: read supported states */
    0xFF,
    0xFF,
    0xFF,
    BLE_HCI_CONN_PARAM_REPLY_LEN,       /* 0x0020: conn param reply */
    BLE_HCI_CONN_PARAM_NEG_REPLY_LEN,   /* 0x0021: conn param neg reply */
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0,                                  /* 0x002F: Read max data length */
};

