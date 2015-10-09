/**
 * Copyright (c) 2015 Stack Inc.
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

#ifndef H_LL_
#define H_LL_

/* XXX: Not sure this should go here, but whatever */
/* 
 * ble ll global parameters
 * 
 * NOTES:
 *  1) Controller should not change value of supported max tx and rx time and
 *  octets.
 */
struct ll_global_parms
{
    int conn_init_max_tx_octets;
    int conn_init_max_tx_time;
    int supp_max_tx_octets;
    int supp_max_tx_time;
    int supp_max_rx_octets;
    int supp_max_rx_time;
};

struct ll_obj
{
    struct os_eventq ll_evq;
    struct ll_global_parms ll_parms;
};

extern struct ll_obj g_ll_data;

/* BLE LL Task Events */
#define BLE_LL_EVENT_HCI_CMD        (OS_EVENT_T_PERUSER)
#define BLE_LL_EVENT_ADV_TXDONE     (OS_EVENT_T_PERUSER + 1)

/* LL Features */
#define BLE_LL_FEAT_LE_ENCRYPTION   (0x01)
#define BLE_LL_FEAT_CONN_PARM_REQ   (0x02)
#define BLE_LL_FEAT_EXTENDED_REJ    (0x04)
#define BLE_LL_FEAT_SLAVE_INIT      (0x08)
#define BLE_LL_FEAT_LE_PING         (0x10)
#define BLE_LL_FEAT_DATA_LEN_EXT    (0x20)
#define BLE_LL_FEAT_LL_PRIVACY      (0x40)
#define BLE_LL_FEAT_EXT_SCAN_FILT   (0x80)

/* LL timing */
#define BLE_LL_IFS              (150)       /* usecs */
#define BLE_CLOCK_DRIFT_ACTIVE  (50)        /* +/- ppm */
#define BLE_CLOCK_DRIFT_SLEEP   (500)       /* +/- ppm */

/* 
 * BLE LL device address. Note that element 0 of the array is the LSB and
 * is sent over the air first. Byte 5 is the MSB and is the last one sent over
 * the air.
 */
#define BLE_DEV_ADDR_LEN    (6)     /* bytes */

struct ll_dev_addr
{
    uint8_t u8[BLE_DEV_ADDR_LEN];
};

#define BLE_IS_DEV_ADDR_STATIC(dev_addr)       ((dev_addr.u8[5] & 0xc0) == 0xc0)
#define BLE_IS_DEV_ADDR_RESOLVABLE(dev_addr)   ((dev_addr.u8[5] & 0xc0) == 0x40)
#define BLE_IS_DEV_ADDR_UNRESOLVABLE(dev_addr) ((dev_addr.u8[5] & 0xc0) == 0x00)

/* 
 * LL packet format
 * 
 *  -> Premable         (1 byte)
 *  -> Access Address   (4 bytes)
 *  -> PDU              (2 to 257 octets)
 *  -> CRC              (3 bytes)
 */
#define BLE_LL_PREAMBLE_LEN     (1)
#define BLE_LL_ACC_ADDR_LEN     (4)
#define BLE_LL_CRC_LEN          (3)
#define BLE_LL_PDU_HDR_LEN      (2)
#define BLE_LL_MIN_PDU_LEN      (BLE_LL_PDU_HDR_LEN)
#define BLE_LL_MAX_PDU_LEN      (257)
#define BLE_LL_CRCINIT_ADV      (0x555555)

/* Access address for advertising channels */
#define BLE_ACCESS_ADDR_ADV             (0x8E89BED6)

/*
 * Data Channel format
 * 
 *  -> Header (2 bytes.
 *      -> LSB contains llid, nesn, sn and md
 *      -> MSB contains length (8 bits)
 *  -> Payload (0 to 251)
 *  -> MIC (0 or 4 bytes)
 */
#define BLE_LL_DATA_HDR_LLID_MASK       (0x03)
#define BLE_LL_DATA_HDR_NESN_MASK       (0x04)
#define BLE_LL_DATA_HDR_SN_MASK         (0x08)
#define BLE_LL_DATA_HDR_MD_MASK         (0x10)
#define BLE_LL_DATA_HDR_RSRVD_MASK      (0xE0)

/* LLID definitions */
#define BLE_LL_LLID_RSRVD               (0)
#define BLE_LL_LLID_DATA_FRAG           (1)
#define BLE_LL_LLID_DATA_START          (2)
#define BLE_LL_LLID_CTRL                (3)

/* 
 * LL CTRL PDU format
 *  -> Opcode (1 byte)
 *  -> Ctrl data (0 - 26 bytes)
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
struct ll_conn_upd_req
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
struct ll_chan_map_req
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
struct ll_enc_req
{
    uint8_t rand[8];
    uint16_t ediv;
    uint8_t skdm[8];
    uint32_t ivm;
};

#define BLE_LL_CTRL_ENC_REQ_LEN             (22)

/* LL control enc rsp */
struct ll_enc_rsp
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
struct ll_version_ind
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
struct ll_conn_params
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
struct ll_reject_ind_ext
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
struct ll_len_req
{
    uint16_t max_rx_bytes;
    uint16_t max_rx_time;
    uint16_t max_tx_bytes;
    uint16_t max_tx_time;
};

#define BLE_LL_CTRL_LENGTH_REQ_LEN      (8)

/*
 * SCAN_REQ 
 *      -> ScanA    (6 bytes)
 *      -> AdvA     (6 bytes)
 * 
 *  ScanA is the scanners public (TxAdd=0) or random (TxAdd = 1) address
 *  AdvaA is the advertisers public (RxAdd=0) or random (RxAdd=1) address.
 * 
 * Sent by the LL in the Scanning state; received by the LL in the advertising
 * state. The advertising address is the intended recipient of this frame.
 */
#define BLE_SCAN_REQ_LEN                (12)

/*
 * SCAN_RSP
 *      -> AdvA         (6 bytes)
 *      -> ScanRspData  (0 - 31 bytes) 
 * 
 *  AdvaA is the advertisers public (TxAdd=0) or random (TxAdd=1) address.
 *  ScanRspData may contain any data from the advertisers host.
 * 
 * Sent by the LL in the advertising state; received by the LL in the
 * scanning state. 
 */
#define BLE_SCAN_RSP_MIN_LEN            (6)
#define BLE_SCAN_RSP_MAX_LEN            (37)

/*
 * CONNECT_REQ
 *      -> InitA        (6 bytes)
 *      -> AdvA         (6 bytes)
 *      -> LLData       (22 bytes)
 *          -> Access address (4 bytes)
 *          -> CRC init (3 bytes)
 *          -> WinSize (1 byte)
 *          -> WinOffset (2 bytes)
 *          -> Interval (2 bytes)
 *          -> Latency (2 bytes)
 *          -> Timeout (2 bytes)
 *          -> Channel Map (5 bytes)
 *          -> Hop Increment (5 bits)
 *          -> SCA (3 bits)
 * 
 *  InitA is the initiators public (TxAdd=0) or random (TxAdd=1) address.
 *  AdvaA is the advertisers public (RxAdd=0) or random (RxAdd=1) address.
 *  LLData contains connection request data.
 *      aa: Link Layer's access address
 *      crc_init: The CRC initialization value used for CRC calculation.
 *      winsize: The transmit window size = winsize * 1.25 msecs
 *      winoffset: The transmit window offset =  winoffset * 1.25 msecs
 *      interval: The connection interval = interval * 1.25 msecs.
 *      latency: connection slave latency = latency
 *      timeout: Connection supervision timeout = timeout * 10 msecs.
 *      chanmap: contains channel mapping indicating used and unused data
 *               channels. Only bits that are 1 are usable. LSB is channel 0.
 *      hop_inc: Hop increment used for frequency hopping. Random value in
 *               range of 5 to 16.
 */
#define BLE_CONNECT_REQ_LEN             (34)

struct ble_conn_req_data
{
    uint32_t    aa;
    uint8_t     crc_init[3];
    uint8_t     winsize;
    uint16_t    winoffset;
    uint16_t    interval;
    uint16_t    latency;
    uint16_t    timeout;
    uint8_t     chanmap[5];
    uint8_t     hop_inc;
    uint8_t     master_sca;
};

#define BLE_CONN_REQ_HOP_MASK           (0x1F)
#define BLE_CONN_REQ_SCA_MASK           (0xE0)

#define BLE_MASTER_SCA_251_500_PPM      (0)
#define BLE_MASTER_SCA_151_250_PPM      (1)
#define BLE_MASTER_SCA_101_150_PPM      (2)
#define BLE_MASTER_SCA_76_100_PPM       (3)
#define BLE_MASTER_SCA_51_75_PPM        (4)
#define BLE_MASTER_SCA_31_50_PPM        (5)
#define BLE_MASTER_SCA_21_30_PPM        (6)
#define BLE_MASTER_SCA_0_20_PPM         (7)

/*
 * Scanner filter policy 
 * 
 * Determines how the scanner's Link Layer processes advertisements.
 * 
 *  NONE: no filtering (default value)
 *  ADV: only advertising packets from devices in white list allowed. A
 *      connectable, directed advertisment is ignored if the scanner's device
 *      address is not in the packet.
 *  ADV_INITA: same as ADV but do not ignore connectable, directed
 *      advertisements if InitA address is resolvable.
 *  NONE_INITA: same as none (process all advertisements) but do not ignore
 *      connectable, directed advertisements if InitA address is resolvable.
 */
enum ll_scan_filt_policy
{
    BLE_LL_SCAN_FILT_NONE = 0,
    BLE_LL_SCAN_FILT_ADV,
    BLE_LL_SCAN_FILT_ADV_INITA,
    BLE_LL_SCAN_FILT_NONE_INITA,
};

/*
 * Initiator filter policy 
 * 
 * Determines how the initiator's Link Layer processes advertisements.
 * 
 *  LIST: process connectable advertisements only from devices in white list.
 *  SINGLE: do not use white list; process connectable advertisements from
 *      a single specific device specified by the host.
 */
enum ll_init_filt_policy
{
    BLE_LL_INIT_FILT_LIST = 0,
    BLE_LL_INIT_FILT_SINGLE,
};

/* External API */
void ll_event_adv_tx_done(void *arg);

#endif /* H_LL_ */
