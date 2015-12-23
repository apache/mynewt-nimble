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

#ifndef H_BLE_HS_
#define H_BLE_HS_

#include <inttypes.h>
#include "host/ble_att.h"

#define BLE_HS_EAGAIN                   1
#define BLE_HS_EALREADY                 2
#define BLE_HS_EINVAL                   3
#define BLE_HS_EMSGSIZE                 4
#define BLE_HS_ENOENT                   5
#define BLE_HS_ENOMEM                   6
#define BLE_HS_ENOTCONN                 7
#define BLE_HS_ENOTSUP                  8
#define BLE_HS_EAPP                     9
#define BLE_HS_EBADDATA                 10
#define BLE_HS_EOS                      11
#define BLE_HS_ECONGESTED               12
#define BLE_HS_ECONTROLLER              13

#define BLE_HS_ERR_ATT_BASE             0x100   /* 256 */
#define ATT_ERR(att_code)               (BLE_HS_ERR_ATT_BASE + (att_code))

#define BLE_HS_EATT_INVALID_HANDLE      ATT_ERR(BLE_ATT_ERR_INVALID_HANDLE)
#define BLE_HS_EATT_INVALID_PDU         ATT_ERR(BLE_ATT_ERR_INVALID_PDU)
#define BLE_HS_EATT_REQ_NOT_SUPPORTED   ATT_ERR(BLE_ATT_ERR_REQ_NOT_SUPPORTED)
#define BLE_HS_EATT_INVALID_OFFSET      ATT_ERR(BLE_ATT_ERR_INVALID_OFFSET)
#define BLE_HS_EATT_PREPARE_QUEUE_FULL  ATT_ERR(BLE_ATT_ERR_PREPARE_QUEUE_FULL)
#define BLE_HS_EATT_ATTR_NOT_FOUND      ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)
#define BLE_HS_EATT_ATTR_VALUE_LEN      ATT_ERR(BLE_ATT_ERR_ATTR_LEN)
#define BLE_HS_EATT_UNLIKELY            ATT_ERR(BLE_ATT_ERR_UNLIKELY)
#define BLE_HS_EATT_UNSUPPORTED_GROUP   ATT_ERR(BLE_ATT_ERR_UNSUPPORTED_GROUP)
#define BLE_HS_EATT_INSUFFICIENT_RES    ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_RES)

struct ble_hs_cfg {
    uint16_t max_outstanding_pkts_per_conn;
    uint8_t max_connections;
};
extern struct ble_hs_cfg ble_hs_cfg;


#define BLE_HS_ADV_LE_ROLE_PERIPH               0x00
#define BLE_HS_ADV_LE_ROLE_CENTRAL              0x01
#define BLE_HS_ADV_LE_ROLE_BOTH_PERIPH_PREF     0x02
#define BLE_HS_ADV_LE_ROLE_BOTH_CENTRAL_PREF    0x03

struct ble_hs_adv_fields {
    /*** 0x01 - Flags. */
    uint8_t flags;

    /*** 0x02,0x03 - 16-bit service class UUIDs. */
    uint16_t *uuids16;
    uint8_t num_uuids16;
    unsigned uuids16_is_complete:1;

    /*** 0x04,0x05 - 32-bit service class UUIDs. */
    uint32_t *uuids32;
    uint8_t num_uuids32;
    unsigned uuids32_is_complete:1;

    /*** 0x06,0x07 - 128-bit service class UUIDs. */
    void *uuids128;
    uint8_t num_uuids128;
    unsigned uuids128_is_complete:1;

    /*** 0x08,0x09 - Local name. */
    uint8_t *name;
    uint8_t name_len;
    unsigned name_is_complete:1;

    /*** 0x0a - Tx power level. */
    uint8_t tx_pwr_lvl;
    unsigned tx_pwr_lvl_is_present:1;

    /*** 0x1c - LE role. */
    uint8_t le_role;
    unsigned le_role_is_present:1;
};

int ble_hs_init(uint8_t prio);

#endif
