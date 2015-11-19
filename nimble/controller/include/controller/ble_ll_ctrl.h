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

#ifndef H_BLE_LL_CTRL_
#define H_BLE_LL_CTRL_

/* 
 * LL CTRL PDU format
 *  -> Opcode   (1 byte)
 *  -> Data     (0 - 26 bytes)
 */
#define BLE_LL_CTRL_CONN_UPDATE_REQ     (0)
#define BLE_LL_CTRL_CHANNEL_MAP_REQ     (1)
#define BLE_LL_CTRL_TERMINATE_IND       (2)
#define BLE_LL_CTRL_ENC_REQ             (3)
#define BLE_LL_CTRL_ENC_RSP             (4)
#define BLE_LL_CTRL_START_ENC_REQ       (5)
#define BLE_LL_CTRL_START_ENC_RSP       (6)
#define BLE_LL_CTRL_UNKNOWN_RSP         (7)
#define BLE_LL_CTRL_FEATURE_REQ         (8)
#define BLE_LL_CTRL_FEATURE_RSP         (9)
#define BLE_LL_CTRL_PAUSE_ENC_REQ       (10)
#define BLE_LL_CTRL_PAUSE_ENC_RSP       (11)
#define BLE_LL_CTRL_VERSION_IND         (12)
#define BLE_LL_CTRL_REJECT_IND          (13)
#define BLE_LL_CTRL_SLAVE_FEATURE_REQ   (14)
#define BLE_LL_CTRL_CONN_PARM_REQ       (15)
#define BLE_LL_CTRL_CONN_PARM_RSP       (16)
#define BLE_LL_CTRL_REJECT_IND_EXT      (17)
#define BLE_LL_CTRL_PING_REQ            (18)
#define BLE_LL_CTRL_PING_RSP            (19)
#define BLE_LL_CTRL_LENGTH_REQ          (20)
#define BLE_LL_CTRL_LENGTH_RSP          (21)

/* LL control connection update request */
struct ble_ll_conn_upd_req
{
    uint8_t winsize;
    uint16_t winoffset;
    uint16_t interval;
    uint16_t latency;
    uint16_t timeout;
    uint16_t instant;
};
#define BLE_LL_CTRL_CONN_UPD_REQ_LEN        (11)

/* LL control channel map request */
struct ble_ll_chan_map_req
{
    uint8_t chmap[5];
    uint16_t instant;
};
#define BLE_LL_CTRL_CHAN_MAP_LEN            (7)

/* 
 * LL control terminate ind
 *  -> error code (1 byte)                         
 */
#define BLE_LL_CTRL_TERMINATE_IND_LEN      (1)

/* LL control enc req */
struct ble_ll_enc_req
{
    uint8_t rand[8];
    uint16_t ediv;
    uint8_t skdm[8];
    uint32_t ivm;
};

#define BLE_LL_CTRL_ENC_REQ_LEN             (22)

/* LL control enc rsp */
struct ble_ll_enc_rsp
{
    uint8_t skds[8];
    uint32_t ivs;
};

#define BLE_LL_CTRL_ENC_RSP_LEN             (12)

/* LL control start enc req and start enc rsp have no data */ 
#define BLE_LL_CTRL_START_ENC_LEN           (0)

/* 
 * LL control unknown response
 *  -> 1 byte which contains the unknown or un-supported opcode.
 */
#define BLE_LL_CTRL_UNK_RSP_LEN             (1)

/*
 * LL control feature req and LL control feature rsp 
 *  -> 8 bytes of data containing features supported by device.
 */
#define BLE_LL_CTRL_FEATURE_LEN             (8)

/* LL control pause enc req and pause enc rsp have no data */
#define BLE_LL_CTRL_PAUSE_ENC_LEN           (0)

/* 
 * LL control version ind 
 *  -> version (1 byte):
 *      Contains the version number of the bluetooth controller specification.
 *  -> comp_id (2 bytes)
 *      Contains the company identifier of the manufacturer of the controller.
 *  -> sub_ver_num: Contains a unique value for implementation or revision of
 *      the bluetooth controller.
 */
struct ble_ll_version_ind
{
    uint8_t ble_ctrlr_ver;
    uint16_t company_id;
    uint16_t sub_ver_num;
};

#define BLE_LL_CTRL_VERSION_IND_LEN         (5)

/* 
 * LL control reject ind
 *  -> error code (1 byte): contains reason why request was rejected.
 */
#define BLE_LL_CTRL_REJ_IND_LEN             (1)

/*
 * LL control slave feature req
 *  -> 8 bytes of data containing features supported by device.
 */
#define BLE_LL_CTRL_SLAVE_FEATURE_REQ_LEN   (8)

/* LL control connection param req and connection param rsp */
struct ble_ll_conn_params
{
    uint16_t interval_min;
    uint16_t interval_max;
    uint16_t latency;
    uint16_t timeout;
    uint16_t pref_periodicity;
    uint16_t ref_conn_event_cnt;
    uint16_t offset0;
    uint16_t offset1;
    uint16_t offset2;
    uint16_t offset3;
    uint16_t offset4;
    uint16_t offset5;
};

#define BLE_LL_CTRL_CONN_PARAMS_LEN     (24)

/* LL control reject ind ext */
struct ble_ll_reject_ind_ext
{
    uint8_t reject_opcode;
    uint8_t err_code;
};

#define BLE_LL_CTRL_REJECT_IND_EXT_LEN  (2)

/* LL control ping req and ping rsp (contain no data) */
#define BLE_LL_CTRL_PING_LEN            (0)

/* 
 * LL control length req and length rsp
 *  -> max_rx_bytes (2 bytes): defines connMaxRxOctets. Range 27 to 251
 *  -> max_rx_time (2 bytes): defines connMaxRxTime. Range 328 to 2120 usecs.
 *  -> max_tx_bytes (2 bytes): defines connMaxTxOctets. Range 27 to 251
 *  -> max_tx_time (2 bytes): defines connMaxTxTime. Range 328 to 2120 usecs.
 */
struct ble_ll_len_req
{
    uint16_t max_rx_bytes;
    uint16_t max_rx_time;
    uint16_t max_tx_bytes;
    uint16_t max_tx_time;
};

#define BLE_LL_CTRL_LENGTH_REQ_LEN      (8)

#endif /* H_BLE_LL_CTRL_ */
