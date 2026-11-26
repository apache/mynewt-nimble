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

#if MYNEWT_VAL(BLE_SVC_RAS_CLIENT)
/* RREQ (Client) Public API */

/**
 * Discovered RAS handle set, filled by the application after GATT discovery.
 * Value handles (features, od_rd, etc.) are mandatory.
 * CCC handles (*_ccc) are needed by ble_svc_ras_rreq_subscribe().
 */
struct ble_svc_ras_handles {
    uint16_t features;
    uint16_t od_rd;
    uint16_t od_rd_ccc;
    uint16_t rt_rd;
    uint16_t rt_rd_ccc;
    uint16_t cp;
    uint16_t cp_ccc;
    uint16_t rd_ready;
    uint16_t rd_ready_ccc;
    uint16_t rd_overwritten;
    uint16_t rd_overwritten_ccc;
};

/** Callback: Ranging Data Ready indication received. */
typedef void ble_svc_ras_rreq_rd_ready_fn(uint16_t conn_handle,
                                          uint16_t ranging_counter);
/** Callback: Ranging Data Overwritten indication received. */
typedef void ble_svc_ras_rreq_rd_overwritten_fn(uint16_t conn_handle,
                                                uint16_t ranging_counter);
/** Callback: GET_RD / Real-time RD procedure completed.
 *  status: 0 on success, non-zero on error; check status != 0 (not
 *  status < 0) to detect all error conditions.  Local NimBLE errors use
 *  BLE_HS_E* (positive integers, e.g. BLE_HS_ENOMEM); remote ATT errors
 *  use BLE_HS_ATT_ERR(att_err), i.e. BLE_HS_ERR_ATT_BASE + att_err.
 *  data is valid when status == 0, and in two delivery-with-error cases:
 *  with BLE_HS_EBADDATA the transfer ended with lost segments and the
 *  gaps are zero-filled, so the buffer is complete in size but not in
 *  content; with a failed final acknowledgement the fully reassembled
 *  data is delivered with the ACK write's error code.  Otherwise data is
 *  NULL or must not be used. */
typedef void ble_svc_ras_rreq_data_complete_fn(uint16_t conn_handle,
                                               uint16_t ranging_counter,
                                               const uint8_t *data,
                                               uint16_t data_len, int status);
/** Callback: all CCCD subscriptions completed.
 *  status: 0 if all succeeded, first non-zero ATT error otherwise. */
typedef void ble_svc_ras_rreq_subscribe_fn(uint16_t conn_handle, int status);

/** Callback: Features read completed. */
typedef void ble_svc_ras_rreq_features_fn(uint16_t conn_handle,
                                          uint32_t features, int status);

/** Allocate RREQ state for a connection.  Call after GATT discovery.
 *  The handles are not validated here: a missing mandatory handle is
 *  reported by the call that needs it (BLE_HS_EINVAL from
 *  ble_svc_ras_rreq_read_features(), get_ranging_data() and friends). */
int ble_svc_ras_rreq_alloc(uint16_t conn_handle,
                           const struct ble_svc_ras_handles *handles);

/** Set the ranging data mode (RAP 4.4: shall enable ONE of on-demand or
 *  real-time).  Call between alloc and subscribe.  Default after alloc:
 *  on-demand (false). */
int ble_svc_ras_rreq_set_ranging_mode(uint16_t conn_handle, bool use_realtime);

/** Select indicate (true) or notify (false) for the data RD CCCD.
 *  Applies to both on-demand and real-time modes.
 *  Call between alloc and subscribe. Default: notify (false). */
int ble_svc_ras_rreq_set_indicate(uint16_t conn_handle, bool use_indicate);

/** Free RREQ state for a connection.  Unlike the server side, which releases
 *  its state from the host's disconnect handling, this is the application's
 *  responsibility: call it from the BLE_GAP_EVENT_DISCONNECT handler.  If the
 *  entry is left behind and the controller later hands out the same connection
 *  handle again, ble_svc_ras_rreq_alloc() succeeds on the stale entry instead
 *  of resetting it, leaving the previous session's subscription and streaming
 *  state in place. */
void ble_svc_ras_rreq_free(uint16_t conn_handle);

/** Prepare the RAS client (RREQ) side for a new host incarnation.
 *
 *  sysinit runs this when the package is enabled, so an application that
 *  brings the host up the normal way has nothing to do.  It is declared here
 *  because it has to run before any other RREQ call in a process that takes
 *  the host down and brings it back up: the RREQ pool and its per-entry
 *  latches outlive the host stack, while the NPL callout blocks those entries
 *  point at do not, and this is what drops the dead handles so the next
 *  ble_svc_ras_rreq_alloc() takes fresh ones.
 *
 *  Discards whatever RREQ state is present, so it must not be called while
 *  ranging is in progress.  Idempotent, and does nothing on a first init. */
void ble_svc_ras_rreq_init(void);

/** Tear down the RAS client (RREQ) side.
 *  Drops any in-progress reassembly state, then stops every per-entry timer.
 *  The pool memory itself is deliberately kept so that in-flight GATT
 *  callbacks stay safe; it is reused by the next ble_svc_ras_rreq_alloc(),
 *  which reinitialises the callouts.
 *
 *  Deliberately has no in-tree caller: the host never tears services down on
 *  its own, so this exists for an application that wants to reclaim the RREQ
 *  pool after it is done with ranging.  Nothing leaks if it is never called,
 *  and taking the host down without it stays safe because
 *  ble_svc_ras_rreq_init() discards what the previous host left behind.
 *  Safe to call multiple times and when no alloc was ever performed. */
void ble_svc_ras_rreq_deinit(void);

/** Set the RD Ready callback. */
int ble_svc_ras_rreq_set_rd_ready_cb(uint16_t conn_handle,
                                     ble_svc_ras_rreq_rd_ready_fn *cb);

/** Set the RD Overwritten callback. */
int ble_svc_ras_rreq_set_rd_overwritten_cb(
    uint16_t conn_handle, ble_svc_ras_rreq_rd_overwritten_fn *cb);

/** Set a persistent data-complete callback.
 *  Called when any GET_RD procedure completes.
 *  A per-call callback passed to get_ranging_data() takes priority. */
int ble_svc_ras_rreq_set_data_complete_cb(
    uint16_t conn_handle, ble_svc_ras_rreq_data_complete_fn *cb);

/**
 * Subscribe to the RAS CCCDs required by the selected ranging mode, using
 * the CCC handles in the handles struct.  On-demand mode writes:
 *   RD Overwritten CCC  (indicate)
 *   RD Ready CCC        (indicate)
 *   On-demand RD CCC    (notify)
 *   CP CCC              (indicate)
 * Real-time mode writes only Real-time RD CCC (notify) and CP CCC
 * (indicate); RD Overwritten and RD Ready are on-demand only.  A mode
 * switch on a live connection first disables the previous mode's CCC.
 * Calls cb when all writes have completed.  One round at a time: returns
 * BLE_HS_EBUSY while an earlier round is still outstanding, so wait for cb
 * before switching mode and subscribing again. */
int ble_svc_ras_rreq_subscribe(uint16_t conn_handle,
                               ble_svc_ras_rreq_subscribe_fn *cb);

/** Read the RAS Features characteristic from the server.
 *  The result and cached value are delivered via the callback. */
int ble_svc_ras_rreq_read_features(uint16_t conn_handle,
                                   ble_svc_ras_rreq_features_fn *cb);

/** Return the cached server Features value (valid after a successful read). */
uint32_t ble_svc_ras_rreq_get_server_features(uint16_t conn_handle);

/** Initiate a GET_RD procedure. When all data is received and ACK'd,
 *  the completion callback is called with the reassembled data.
 *  If cb is NULL the persistent data-complete callback is used. */
int ble_svc_ras_rreq_get_ranging_data(uint16_t conn_handle,
                                      uint16_t ranging_counter,
                                      ble_svc_ras_rreq_data_complete_fn *cb);

/** Notify the RREQ that a CS procedure has started (real-time mode).
 *  Starts the 5s "no data" timeout (RAP 4.4.1.1).
 *  Call from the initiator when use_realtime==true. */
int ble_svc_ras_rreq_rt_cs_started(uint16_t conn_handle);

/** Send ABORT_OP command to the server. */
int ble_svc_ras_rreq_abort(uint16_t conn_handle);

/** Send SET_FILTER command to the server.
 *  mode: 0-3 (CS mode), mask: 14-bit filter mask. */
int ble_svc_ras_rreq_set_filter(uint16_t conn_handle, uint8_t mode,
                                uint16_t mask);

/** Process an incoming notification or indication from the RAS server.
 *  Call from the application's BLE_GAP_EVENT_NOTIFY_RX handler.
 *  Returns 0 if the notification was handled, BLE_HS_ENOENT if it was
 *  not recognized. */
int ble_svc_ras_rreq_on_notify(uint16_t conn_handle, uint16_t attr_handle,
                               struct os_mbuf *om);

/*
 * RREQ: Local CS Event Pass-through
 *
 * Lightweight wrapper for the initiator's own CS HCI events.
 * No data is buffered in the library; each RESULT, RESULT_CONTINUE,
 * and PROCEDURE_COMPLETE event is forwarded directly to the app
 * callback.  The app decides what to keep (e.g. only the latest
 * subevent's step data).
 */

/** Callback: local CS event received from the controller.
 *  Called once for each RESULT, RESULT_CONTINUE, and
 *  PROCEDURE_COMPLETE HCI event.  Step data pointers inside the
 *  event are only valid for the duration of this call. */
typedef void ble_svc_ras_rreq_local_cs_fn(struct ble_cs_event *event);

/** Register / replace the local CS event callback. */
int ble_svc_ras_rreq_set_local_cs_cb(uint16_t conn_handle,
                                     ble_svc_ras_rreq_local_cs_fn *cb);

/** CS event handler for the initiator (RREQ) side.
 *  Pass as the 'cb' argument to ble_cs_initiator_procedure_start().
 *  Forwards each event to the registered local-CS callback with
 *  zero internal buffering.  Signature matches ble_cs_event_fn. */
int ble_svc_ras_rreq_cs_event_handler(struct ble_cs_event *event, void *arg);
#endif /* MYNEWT_VAL(BLE_SVC_RAS_CLIENT) */

#ifdef __cplusplus
}
#endif

#endif /* MYNEWT_VAL(BLE_CHANNEL_SOUNDING) */
#endif /* H_BLE_SVC_RAS_ */
