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

#ifndef H_BLE_SVC_RAS_
#define H_BLE_SVC_RAS_

#include <stdint.h>
#include <stdbool.h>
#include "syscfg/syscfg.h"
#include "host/ble_hs.h"
#include "host/ble_cs.h"

#if MYNEWT_VAL(BLE_CHANNEL_SOUNDING)

#ifdef __cplusplus
extern "C" {
#endif

/* RAS Service UUID */
#define BLE_SVC_RAS_RANGING_SERVICE_VAL         (0x185B)

/* RAS Characteristic UUIDs */
#define BLE_SVC_RAS_CHR_UUID_FEATURES_VAL       (0x2C14)
#define BLE_SVC_RAS_CHR_UUID_REALTIME_RD_VAL    (0x2C15)
#define BLE_SVC_RAS_CHR_UUID_ONDEMAND_RD_VAL    (0x2C16)
#define BLE_SVC_RAS_CHR_UUID_CP_VAL             (0x2C17)
#define BLE_SVC_RAS_CHR_UUID_RD_READY_VAL       (0x2C18)
#define BLE_SVC_RAS_CHR_UUID_RD_OVERWRITTEN_VAL (0x2C19)

/* RAS Feature Bits (Spec Table 3.3) */
#define BLE_SVC_RAS_FEAT_REALTIME_RD_BIT        (1 << 0)
#define BLE_SVC_RAS_FEAT_RETRIEVE_LOST_RD_BIT   (1 << 1)
#define BLE_SVC_RAS_FEAT_ABORT_OP_BIT           (1 << 2)
#define BLE_SVC_RAS_FEAT_FILTER_RD_BIT          (1 << 3)

/* The BLE_SVC_RAS_MAX_* constants below size every procedure-sized buffer
 * on both sides: the server's ranging buffers and the client's reassembly
 * buffer.  A procedure larger than this is stored truncated and reported as
 * aborted, so they are sized for the largest procedure a peer may produce
 * rather than for the configuration the local host creates. */
#define BLE_SVC_RAS_RANGING_HEADER_LEN          4
#define BLE_SVC_RAS_SUBEVENT_HEADER_LEN         8
#define BLE_SVC_RAS_STEP_MODE_LEN               1

#define BLE_SVC_RAS_MAX_SUBEVENTS                                       \
    MYNEWT_VAL(BLE_SVC_RAS_MAX_SUBEVENTS_PER_PROCEDURE)
#define BLE_SVC_RAS_MAX_STEPS                                           \
    MYNEWT_VAL(BLE_SVC_RAS_MAX_STEPS_PER_PROCEDURE)
#define BLE_SVC_RAS_MAX_STEP_DATA_LEN                                   \
    MYNEWT_VAL(BLE_SVC_RAS_MAX_STEP_DATA_LEN)

#define BLE_SVC_RAS_PROCEDURE_MEM                                       \
    (BLE_SVC_RAS_RANGING_HEADER_LEN +                                   \
     (BLE_SVC_RAS_MAX_SUBEVENTS * BLE_SVC_RAS_SUBEVENT_HEADER_LEN) +    \
     (BLE_SVC_RAS_MAX_STEPS * BLE_SVC_RAS_STEP_MODE_LEN) +              \
     (BLE_SVC_RAS_MAX_STEPS * BLE_SVC_RAS_MAX_STEP_DATA_LEN))

/* RASCP Wire Format Constants */
#define BLE_SVC_RAS_CP_CMD_OPCODE_LEN     1
#define BLE_SVC_RAS_CP_CMD_PARAMS_MAX_LEN 4
#define BLE_SVC_RAS_CP_WRITE_MAX_LEN                                          \
    (BLE_SVC_RAS_CP_CMD_OPCODE_LEN + BLE_SVC_RAS_CP_CMD_PARAMS_MAX_LEN)

/* RASCP Command Opcodes (RAS Spec Table 3.10) */
enum ble_svc_ras_cp_opcode {
    BLE_SVC_RAS_CP_OPCODE_GET_RD = 0x00,
    BLE_SVC_RAS_CP_OPCODE_ACK_RD = 0x01,
    BLE_SVC_RAS_CP_OPCODE_RETRIEVE_LOST_RD_SEGMENTS = 0x02,
    BLE_SVC_RAS_CP_OPCODE_ABORT_OP = 0x03,
    BLE_SVC_RAS_CP_OPCODE_SET_FILTER = 0x04,
};

/* RASCP Response Opcodes (RAS Spec Table 3.11) */
enum ble_svc_ras_cp_rsp_opcode {
    BLE_SVC_RAS_CP_RSP_OPCODE_COMPLETE_RD_RSP = 0x00,
    BLE_SVC_RAS_CP_RSP_OPCODE_COMPLETE_LOST_RD_SEG_RSP = 0x01,
    BLE_SVC_RAS_CP_RSP_OPCODE_RSP_CODE = 0x02,
};

/* RASCP Response Code Values (RAS Spec Table 3.12) */
#define BLE_SVC_RAS_CP_RSP_SUCCESS                 0x01
#define BLE_SVC_RAS_CP_RSP_OP_CODE_NOT_SUPPORTED   0x02
#define BLE_SVC_RAS_CP_RSP_INVALID_PARAMETER       0x03
#define BLE_SVC_RAS_CP_RSP_SUCCESS_PERSISTED       0x04
#define BLE_SVC_RAS_CP_RSP_ABORT_UNSUCCESSFUL      0x05
#define BLE_SVC_RAS_CP_RSP_PROCEDURE_NOT_COMPLETED 0x06
#define BLE_SVC_RAS_CP_RSP_SERVER_BUSY             0x07
#define BLE_SVC_RAS_CP_RSP_NO_RECORDS_FOUND        0x08

/* 12-bit Ranging Counter mask (BT 6.0 spec) */
#define BLE_SVC_RAS_RANGING_COUNTER_MASK           0x0FFF
#define BLE_SVC_RAS_RANGING_COUNTER(x)                                  \
    ((x) & BLE_SVC_RAS_RANGING_COUNTER_MASK)

/* 6-bit Rolling Segment Counter mask */
#define BLE_SVC_RAS_SEG_COUNTER_MASK               0x3F

/* Filter: 14 usable bits per mode (bits 2-15 of the uint16 param) */
#define BLE_SVC_RAS_FILTER_DEFAULT_MASK            0x3FFF

/* Spec maximum number of antenna paths; also the widest Antenna Paths Mask
 * the ranging header can express. */
#define BLE_SVC_RAS_MAX_ANTENNA_PATHS              4

/* Wire-format packed structures (RAS Spec Section 3.2).  The bit-field
 * layout assumes a little-endian target with low-bit-first allocation, which
 * holds for every toolchain NimBLE supports (GCC and Clang on little-endian
 * cores); multi-byte scalar fields are serialized with the explicit
 * little-endian accessors in the implementation. */

/* Ranging Data Header - RAS Table 3.7, 4 bytes */
struct ble_svc_ras_ranging_header {
    uint16_t ranging_counter
        : 12; /* Lower 12 bits of CS Procedure_Counter (0x000-0xFFF) */
    uint8_t config_id : 4;    /* CS Config_ID (0x00-0x03) */
    int8_t selected_tx_power; /* dBm, range: -127 to +20 */
    /* Bit N: Antenna Path_(N+1) reported (Table 3.7).  RAS Erratum 26610
     * retires these bits ("previously used"): clients derive the antenna
     * paths from the negotiated CS config instead and ignore this field. */
    uint8_t antenna_paths_mask : 4;
    uint8_t rfu : 4;
} __attribute__((packed));

/* Subevent Header - RAS Table 3.8, 8 bytes */
struct ble_svc_ras_subevent_header {
    uint16_t start_acl_conn_event; /* Start_ACL_Conn_Event counter */
    uint16_t freq_compensation;    /* 0.01 ppm units, 0xFFFF = not available */
    /* Done status: 0x0=Complete, 0x1=Partial, 0xF=Aborted */
    uint8_t ranging_done_status : 4;
    uint8_t subevent_done_status : 4;
    /* Abort reason: 0x0=no abort, 0x1=local/remote request,
     * 0x2=insufficient channels, 0x3=instant missed, 0xF=unspecified */
    uint8_t ranging_abort_reason : 4;
    uint8_t subevent_abort_reason : 4;
    int8_t ref_power_level;     /* dBm, range: -127 to +20 */
    uint8_t num_steps_reported; /* Steps in this subevent (0-160) */
} __attribute__((packed));

/* Segmentation Header - RAS Table 3.5, 1 byte */
struct ble_svc_ras_segment_header {
    bool first_seg : 1;      /* 1 = first segment of Ranging Data Body */
    bool last_seg : 1;       /* 1 = last segment of Ranging Data Body */
    /* Rolling counter 0-63, reset per new procedure */
    uint8_t seg_counter : 6;
} __attribute__((packed));

#if MYNEWT_VAL(BLE_SVC_RAS_SERVER)
/* RRSP (Server) Public API */

/** Initialize the RAS GATT service (RRSP / server side).
 *
 *  Call once per host incarnation, after the host stack is initialised;
 *  sysinit does this when the package is enabled.  Server state left behind
 *  by an earlier incarnation is discarded here, so a host that is taken down
 *  and brought back up starts from empty pools and fresh timers. */
void ble_svc_ras_init(void);

/** Tear down the RAS server (RRSP) side.
 *  Releases the ranging-buffer and RRSP pools, stops all per-entry timers,
 *  and unregisters the GAP event listener.
 *
 *  Optional, and meant for an application reclaiming RAS memory once it is
 *  finished with ranging; the GATT service entry itself belongs to the GATT
 *  server and is released with it.  Per-connection cleanup runs from the GAP
 *  listener this removes, so call it when the server is done rather than
 *  between sessions.
 *
 *  Runs while the host is still up, i.e. after ble_svc_ras_init() and before
 *  the host stack is torn down, because stopping the per-entry timers touches
 *  NPL callouts the stack owns.  Bringing the host down first needs no call
 *  at all: the next ble_svc_ras_init() discards whatever the previous
 *  incarnation left behind.  Repeat calls inside one host lifetime, and calls
 *  made without a preceding init, do nothing. */
void ble_svc_ras_deinit(void);

/** Auto-store CS subevent results into RAS buffers.
 *  Use as the CS event callback on the reflector (RRSP side). */
int ble_svc_ras_cs_event_handler(struct ble_cs_event *event, void *arg);

/** Clean up all RAS server state for a disconnecting connection.
 *  Invoked automatically by the RAS GAP event listener registered in
 *  ble_svc_ras_init(); calling it again from the application's disconnect
 *  handler is harmless. */
void ble_svc_ras_on_disconnect(uint16_t conn_handle);

/** GATT registration callback for debug logging. */
void ble_svc_ras_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);

#endif /* MYNEWT_VAL(BLE_SVC_RAS_SERVER) */

#ifdef __cplusplus
}
#endif

#endif /* MYNEWT_VAL(BLE_CHANNEL_SOUNDING) */
#endif /* H_BLE_SVC_RAS_ */
