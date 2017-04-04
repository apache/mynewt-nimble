/*
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
 * Storage for the length of each HCI LE command. This is the length of the
 * command parameters in the command; not the response length.
 */
const uint8_t g_ble_hci_le_cmd_len[BLE_HCI_NUM_LE_CMDS] =
{
    0,                                  /* 0x0000: reserved */
    BLE_HCI_SET_LE_EVENT_MASK_LEN,      /* 0x0001: set event mask */
    BLE_HCI_RD_BUF_SIZE_LEN,            /* 0x0002: read buffer size */
    0,                                  /* 0x0003: read local supp features */
    0,                                  /* 0x0004: not defined */
    BLE_HCI_SET_RAND_ADDR_LEN,          /* 0x0005: set random address */
    BLE_HCI_SET_ADV_PARAM_LEN,          /* 0x0006: set advertising parameters */
    0,                                  /* 0x0007: read adv chan tx power */
    BLE_HCI_SET_ADV_DATA_LEN,           /* 0x0008: set advertising data */
    BLE_HCI_SET_SCAN_RSP_DATA_LEN,      /* 0x0009: set scan rsp data */
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
    BLE_HCI_LE_ENCRYPT_LEN,             /* 0x0017: encrypt */
    0,                                  /* 0x0018: rand */
    BLE_HCI_LE_START_ENCRYPT_LEN,       /* 0x0019: start encryption */
    BLE_HCI_LT_KEY_REQ_REPLY_LEN,       /* 0x001A: LTK request reply */
    sizeof(uint16_t),                   /* 0x001B: LTK request negative reply */
    0,                                  /* 0x001C: read supported states */
    sizeof(uint8_t),                    /* 0x001D: receiver test */
    BLE_HCI_TX_TEST_LEN,                /* 0x001E: transmitter test */
    0,                                  /* 0x001F: test end */
    BLE_HCI_CONN_PARAM_REPLY_LEN,       /* 0x0020: conn param reply */
    BLE_HCI_CONN_PARAM_NEG_REPLY_LEN,   /* 0x0021: conn param neg reply */
    BLE_HCI_SET_DATALEN_LEN,            /* 0x0022: set data length */
    0,                                  /* 0x0023: read sugg data len */
    BLE_HCI_WR_SUGG_DATALEN_LEN,        /* 0x0024: write suggested data len */
    0,                                  /* 0x0025: rd local P256 pub key */
    BLE_HCI_GEN_DHKEY_LEN,              /* 0x0026: generate DHKEY */
    BLE_HCI_ADD_TO_RESOLV_LIST_LEN,     /* 0x0027: add to resolving list */
    BLE_HCI_RMV_FROM_RESOLV_LIST_LEN,   /* 0x0028: rmv from resolving list */
    0,                                  /* 0x0029: clear resolving list */
    0,                                  /* 0x002A: read resolving list size */
    BLE_HCI_RD_PEER_RESOLV_ADDR_LEN,    /* 0x002B: read peer resolvable addr */
    BLE_HCI_RD_LOC_RESOLV_ADDR_LEN,     /* 0x002C: read local resolvable addr */
    sizeof(uint8_t),                    /* 0x002D: set addr resolution enable */
    sizeof(uint16_t),                   /* 0x002E: set resolv priv addr tmo */
    0,                                  /* 0x002F: read max data length */
    BLE_HCI_LE_RD_PHY_LEN,              /* 0x0030: read maximum default PHY */
    BLE_HCI_LE_SET_DEFAULT_PHY_LEN,     /* 0x0031: set default PHY */
    BLE_HCI_LE_SET_PHY_LEN,             /* 0x0032: set PHY */
    BLE_HCI_LE_ENH_RCVR_TEST_LEN,       /* 0x0033: enhanced receiver test */
    BLE_HCI_LE_ENH_TRANS_TEST_LEN,      /* 0x0034: enhanced transmitter test */
    BLE_HCI_LE_SET_ADV_SET_RND_ADDR_LEN,/* 0x0035: set adv. set random address */
    BLE_HCI_LE_SET_EXT_ADV_PARAM_LEN,   /* 0x0036: set ext. adv params */
    BLE_HCI_LE_SET_EXT_ADV_DATA_LEN,    /* 0x0037: set ext. adv. data */
    BLE_HCI_LE_SET_EXT_SCAN_RSP_DATA_LEN,/* 0x0038: set ext. scan resp. data */
    BLE_HCI_LE_SET_EXT_ADV_ENABLE_LEN,  /* 0x0039: set ext. adv. enable */
    0,                                  /* 0x003A: read max adv. data len */
    0,                                  /* 0x003B: read number of sup. adv. sets */
    BLE_HCI_LE_REMOVE_ADV_SET_LEN,      /* 0x003C: remove adv. set */
    0,                                  /* 0x003D: clear advertising sets */
    BLE_HCI_LE_SET_PER_ADV_PARAMS_LEN,  /* 0x003E: set periodic adv. param. */
    BLE_HCI_LE_SET_PER_ADV_DATA_LEN,    /* 0x003F: set periodic adv. data */
    BLE_HCI_LE_SET_PER_ADV_ENABLE_LEN,  /* 0x0040: periodic adv. enable */
    BLE_HCI_LE_SET_EXT_SCAN_PARAM_LEN,  /* 0x0041: set ext. scan param. */
    BLE_HCI_LE_SET_EXT_SCAN_ENABLE_LEN, /* 0x0042: set ext. scan enable */
    BLE_HCI_LE_EXT_CREATE_CONN_LEN,     /* 0x0043: ext. create connection */
    BLE_HCI_LE_PER_ADV_CREATE_SYNC_LEN, /* 0x0044: periodic adv. create sync */
    0,                                  /* 0x0045: periodic adv. create sync cancel */
    0,                                  /* 0x0046: periodic adv. terminate sync */
    BLE_HCI_LE_ADD_DEV_TO_PER_ADV_LIST_LEN,  /* 0x0047: add dev to per. adv. list */
    BLE_HCI_LE_REM_DEV_FROM_PER_ADV_LIST_LEN,/* 0x0048: remove dev from per. adv. list */
    0,                                  /* 0x0049: clear periodic adv. list */
    0,                                  /* 0x004A: read periodic list size */
    0,                                  /* 0x004B: read transmit power */
    0,                                  /* 0x004C: read RF path */
    BLE_HCI_LE_WR_RF_PATH_COMPENSATION_LEN, /* 0x004D: write RF path */
    BLE_HCI_LE_SET_PRIVACY_MODE_LEN,    /* 0x004E: set privacy mode */
};
