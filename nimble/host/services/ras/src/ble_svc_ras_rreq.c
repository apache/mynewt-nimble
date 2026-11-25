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

#include "syscfg/syscfg.h"

#if MYNEWT_VAL(BLE_CHANNEL_SOUNDING) && MYNEWT_VAL(BLE_SVC_RAS_CLIENT)

#include <string.h>
#include "os/endian.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_log.h"
#include "services/ras/ble_svc_ras.h"
#ifndef MYNEWT
#include "nimble/nimble_port.h"
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define RAS_RREQ_MAX_CONN         MYNEWT_VAL(BLE_SVC_RAS_MAX_CONNS)

/* RAP 4.5.4.1 mandatory timeout values for on-demand ranging data */
#define RREQ_GET_RD_TIMEOUT_MS    5000
#define RREQ_INTER_SEG_TIMEOUT_MS 1000

/* Per-Connection RREQ State */
struct ble_svc_ras_rreq {
    uint16_t conn_handle;
    uint8_t alloc_gen;
    struct ble_svc_ras_handles handles;

    /* Callbacks */
    ble_svc_ras_rreq_rd_ready_fn *rd_ready_cb;
    ble_svc_ras_rreq_rd_overwritten_fn *rd_ow_cb;
    ble_svc_ras_rreq_data_complete_fn *persistent_data_cb;

    /* Subscribe state */
    uint8_t subscribe_total;
    uint8_t subscribe_done;
    int subscribe_status;
    ble_svc_ras_rreq_subscribe_fn *subscribe_cb;

    /* Features read */
    uint32_t server_features;
    ble_svc_ras_rreq_features_fn *features_cb;

    /* Segment reassembly of the peer's data arriving over RAS
     * notifications. */
    uint16_t get_counter;
    uint8_t next_expected_seg;
    uint8_t reassembly_buf[BLE_SVC_RAS_PROCEDURE_MEM];
    uint16_t reassembly_len;
    ble_svc_ras_rreq_data_complete_fn *get_data_cb;

    /* Gap tracking (best-effort): if a segment is lost we zero-pad its
     * slot so later segments still land at deterministic offsets.  The
     * buffer is then delivered to the app with has_gaps=1 surfaced via
     * the error status, because this build does not implement Retrieve
     * Lost.  seg_data_len_hint holds the data length of a normal
     * (non-last) segment observed for the current procedure. */
    uint16_t seg_data_len_hint;

    /* Real-time reassembly state (RAP 4.4.1).  Shares reassembly_buf[]
     * with on-demand; only one mode is active at a time (RAP 4.4 SHALL
     * enable ONE of OD or RT). */
    uint16_t rt_reassembly_len;
    uint16_t rt_ranging_counter;
    uint8_t rt_next_expected_seg;

    /* Packed boolean flags - one bit each.  Total: 16 bits (15 flags
     * plus one pad bit), fits in a single uint16_t.  Grouped by
     * subsystem; comments kept where the flag's semantic isn't obvious
     * from the name. */
    uint16_t features_valid : 1;
    uint16_t get_in_progress : 1;
    uint16_t first_received : 1;
    uint16_t last_received : 1;
    /* ACK_RD sent (on last segment or on Complete_RD_RSP) */
    uint16_t ack_sent : 1;
    /* Complete_RD_RSP may arrive before last segment (RAP 4.5.2.1) */
    uint16_t complete_rd_rsp_received : 1;
    uint16_t has_gaps : 1;
    /* Ranging data mode (RAP 4.4: shall enable ONE of OD or RT) */
    uint16_t use_realtime : 1;
    /* Use indicate (instead of notify) for the data RD CCCD (OD or RT) */
    uint16_t use_indicate : 1;
    /* Data mode CCCDs currently enabled on the server */
    uint16_t od_rd_ccc_enabled : 1;
    uint16_t rt_rd_ccc_enabled : 1;
    /* What the two above held when the current subscribe round started, so a
     * write that fails restores the server's actual state instead of the state
     * the round assumed. */
    uint16_t od_rd_ccc_prev : 1;
    uint16_t rt_rd_ccc_prev : 1;
    uint16_t rt_in_progress : 1;
    uint16_t timers_inited : 1;
    uint16_t _rreq_pad : 1;

    /* Timers (RAP 4.5.4.1 on-demand / RAP 4.4.1.1 real-time) */
    struct ble_npl_callout get_rd_timer;
    struct ble_npl_callout inter_seg_timer;
    struct ble_npl_callout rt_first_seg_timer;
    struct ble_npl_callout rt_inter_seg_timer;

    /* Local CS event pass-through (no buffering) */
    ble_svc_ras_rreq_local_cs_fn *local_cs_cb;
};

/* Per-connection RREQ state pool. */
static struct ble_svc_ras_rreq ble_svc_ras_rreq_pool[RAS_RREQ_MAX_CONN];
static bool ble_svc_ras_rreq_pool_inited = false;

static int
ble_svc_ras_rreq_pool_init(void)
{
    int i;

    if (ble_svc_ras_rreq_pool_inited) {
        return 0;
    }
    for (i = 0; i < RAS_RREQ_MAX_CONN; i++) {
        ble_svc_ras_rreq_pool[i].conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
    ble_svc_ras_rreq_pool_inited = true;
    return 0;
}

static struct ble_svc_ras_rreq *
ble_svc_ras_rreq_find(uint16_t conn_handle)
{
    int i;

    if (ble_svc_ras_rreq_pool_init() != 0) {
        return NULL;
    }
    for (i = 0; i < RAS_RREQ_MAX_CONN; i++) {
        if (ble_svc_ras_rreq_pool[i].conn_handle == conn_handle) {
            return &ble_svc_ras_rreq_pool[i];
        }
    }
    return NULL;
}

static void
ble_svc_ras_rreq_reset_reassembly(struct ble_svc_ras_rreq *rreq)
{
    rreq->get_in_progress = false;
    rreq->get_counter = 0;
    rreq->next_expected_seg = 0;
    rreq->first_received = false;
    rreq->last_received = false;
    rreq->ack_sent = false;
    rreq->reassembly_len = 0;
    rreq->get_data_cb = NULL;
    rreq->complete_rd_rsp_received = false;

    rreq->seg_data_len_hint = 0;
    rreq->has_gaps = false;

    if (rreq->timers_inited) {
        ble_npl_callout_stop(&rreq->get_rd_timer);
        ble_npl_callout_stop(&rreq->inter_seg_timer);
    }
}

/* Fire the data-complete callback.  override_cb, if non-NULL, is used instead
 * of rreq->get_data_cb.  Pass the saved get_data_cb when calling this after
 * ble_svc_ras_rreq_reset_reassembly() (which NULLs get_data_cb) so the
 * per-transfer callback is not silently dropped. */
static void
ble_svc_ras_rreq_fire_data_complete(
    struct ble_svc_ras_rreq *rreq,
    ble_svc_ras_rreq_data_complete_fn *override_cb, const uint8_t *data,
    uint16_t data_len, int status, uint16_t completed_counter)
{
    ble_svc_ras_rreq_data_complete_fn *cb =
        override_cb ? override_cb : rreq->get_data_cb;

    if (cb == NULL) {
        cb = rreq->persistent_data_cb;
    }
    if (cb) {
        cb(rreq->conn_handle, completed_counter, data, data_len, status);
    }
}

/* Hand the reassembled on-demand ranging data to the app and clear the
 * transfer state.  The buffer pointer is taken before the reset so the data
 * stays addressable across the callback even though the transfer state it
 * belonged to is already cleared. */
static void
ble_svc_ras_rreq_deliver_reassembled(struct ble_svc_ras_rreq *rreq, int status)
{
    uint16_t completed_len = rreq->reassembly_len;
    uint16_t completed_counter = rreq->get_counter;
    ble_svc_ras_rreq_data_complete_fn *saved_cb = rreq->get_data_cb;

    uint8_t *data_buf = rreq->reassembly_buf;

    ble_svc_ras_rreq_reset_reassembly(rreq);
    ble_svc_ras_rreq_fire_data_complete(rreq, saved_cb, data_buf,
                                        completed_len, status,
                                        completed_counter);
}

/*
 * Timeout Handlers (RAP 4.5.4.1 SHALL)
 *
 * 5 s after GET_RD with no on-demand PDU  -> abort + notify app
 * 1 s between successive on-demand PDUs   -> abort + notify app
 */
static void
ble_svc_ras_rreq_timeout_abort(struct ble_svc_ras_rreq *rreq)
{
    ble_svc_ras_rreq_data_complete_fn *saved_cb;
    uint8_t cmd[BLE_SVC_RAS_CP_CMD_OPCODE_LEN];
    uint16_t counter;
    int rc;

    if (!rreq->get_in_progress) {
        return;
    }

    BLE_HS_LOG(WARN,
               "[RREQ] Timeout ABORT triggered conn=%d counter=%d "
               "first_received=%d last_received=%d reassembly_len=%d\n",
               rreq->conn_handle, rreq->get_counter, rreq->first_received,
               rreq->last_received, rreq->reassembly_len);

    if (rreq->timers_inited) {
        ble_npl_callout_stop(&rreq->get_rd_timer);
        ble_npl_callout_stop(&rreq->inter_seg_timer);
    }

    counter = rreq->get_counter;

    /* RAP 4.5.4.1: shall write Abort command if supported by the RRSP */
    if (rreq->features_valid &&
        (rreq->server_features & BLE_SVC_RAS_FEAT_ABORT_OP_BIT)) {
        cmd[0] = BLE_SVC_RAS_CP_OPCODE_ABORT_OP;
        rc = ble_gattc_write_no_rsp_flat(rreq->conn_handle,
                                         rreq->handles.cp, cmd, sizeof(cmd));
        BLE_HS_LOG(DEBUG, "[RREQ] Timeout ABORT_OP sent rc=%d conn=%d\n", rc,
                   rreq->conn_handle);
    }

    saved_cb = rreq->get_data_cb;
    ble_svc_ras_rreq_reset_reassembly(rreq);
    ble_svc_ras_rreq_fire_data_complete(rreq, saved_cb, NULL, 0,
                                        BLE_HS_ETIMEOUT, counter);
}

/*
 * Stale-event guards, shared by every timeout callback below.
 *
 * ble_npl_callout_stop() does drain an expiry that is already queued on the
 * eventq, but it cannot cancel one that is in flight, so an event posted just
 * after the stop is still dequeued afterwards.  Each callback therefore
 * revalidates its own trigger:
 *   conn_handle == NONE  -> the slot was freed (disconnect, teardown)
 *   callout is active    -> the callout was re-armed for a newer transaction,
 *                           which supersedes this expiry
 * A genuine one-shot expiry leaves the callout inactive, so the second guard
 * never suppresses a real timeout.
 */
static void
ble_svc_ras_rreq_get_rd_timeout_cb(struct ble_npl_event *ev)
{
    struct ble_svc_ras_rreq *rreq = ble_npl_event_get_arg(ev);

    if (rreq->conn_handle == BLE_HS_CONN_HANDLE_NONE ||
        ble_npl_callout_is_active(&rreq->get_rd_timer)) {
        return;
    }
    /* If the first segment already arrived, data is flowing and the
     * inter-segment timer is the relevant watchdog. */
    if (rreq->first_received) {
        return;
    }
    BLE_HS_LOG(WARN,
               "[RREQ] GET_RD 5s timeout, no data PDU received conn=%d\n",
               rreq->conn_handle);
    ble_svc_ras_rreq_timeout_abort(rreq);
}

static void
ble_svc_ras_rreq_inter_seg_timeout_cb(struct ble_npl_event *ev)
{
    struct ble_svc_ras_rreq *rreq = ble_npl_event_get_arg(ev);

    /* Same stale-event guards as ble_svc_ras_rreq_get_rd_timeout_cb, without
     * the first_received check: that one belongs to the GET_RD watchdog,
     * which has to stand down once data starts flowing, whereas this timer is
     * armed only by the segment handler and so is already scoped to the phase
     * it watches.  is_active() covers the ordinary stop-then-rearm case.  It
     * cannot separate out an expiry already dispatched at the moment
     * ble_svc_ras_rreq_reset_reassembly() stopped the timer, so that one is
     * charged to whichever transfer is current; the effect is bounded to a
     * retryable BLE_HS_ETIMEOUT, since the abort path only resets state the
     * next GET_RD re-establishes. */
    if (rreq->conn_handle == BLE_HS_CONN_HANDLE_NONE ||
        ble_npl_callout_is_active(&rreq->inter_seg_timer)) {
        return;
    }
    BLE_HS_LOG(WARN, "[RREQ] Inter-segment 1s timeout conn=%d\n",
               rreq->conn_handle);
    ble_svc_ras_rreq_timeout_abort(rreq);
}

/*
 * Discard a partially received real-time procedure and report status to the
 * application.  Used by the timeout handlers (RAP 4.4.1.1: 5 s with no RT
 * data after a CS procedure starts, 1 s between successive RT data PDUs) and
 * by segment-gap detection.
 *
 * The Real-time RD CCCD is left enabled; the next procedure's first_seg
 * resynchronizes the stream.
 */
static void
ble_svc_ras_rreq_rt_drop(struct ble_svc_ras_rreq *rreq, int status)
{
    uint16_t counter;

    BLE_HS_LOG(WARN,
               "[RREQ] RT drop: discarding partial data status=%d conn=%d\n",
               status, rreq->conn_handle);

    if (rreq->timers_inited) {
        ble_npl_callout_stop(&rreq->rt_first_seg_timer);
        ble_npl_callout_stop(&rreq->rt_inter_seg_timer);
    }

    /* The first-segment timeout fires with rt_in_progress false, meaning no
     * first_seg ever arrived and rt_ranging_counter still holds the previous
     * procedure's value - reporting it would blame a procedure that actually
     * completed.  Fall back to 0 for "unknown", as the short-first_seg path
     * in ble_svc_ras_rreq_handle_realtime_rd already does. */
    counter = rreq->rt_in_progress ? rreq->rt_ranging_counter : 0;
    rreq->rt_in_progress = false;
    rreq->rt_reassembly_len = 0;

    ble_svc_ras_rreq_fire_data_complete(rreq, NULL, NULL, 0, status, counter);
}

static void
ble_svc_ras_rreq_rt_first_seg_timeout_cb(struct ble_npl_event *ev)
{
    struct ble_svc_ras_rreq *rreq = ble_npl_event_get_arg(ev);

    /* Same stale-event guards as ble_svc_ras_rreq_get_rd_timeout_cb. */
    if (rreq->conn_handle == BLE_HS_CONN_HANDLE_NONE ||
        ble_npl_callout_is_active(&rreq->rt_first_seg_timer)) {
        return;
    }
    /* rt_in_progress means data is flowing, so this timeout is stale;
     * use_realtime false means the slot was reused for OD mode. */
    if (rreq->rt_in_progress || !rreq->use_realtime) {
        return;
    }
    BLE_HS_LOG(WARN,
               "[RREQ] RT 5s timeout, no data after CS procedure conn=%d\n",
               rreq->conn_handle);
    ble_svc_ras_rreq_rt_drop(rreq, BLE_HS_ETIMEOUT);
}

static void
ble_svc_ras_rreq_rt_inter_seg_timeout_cb(struct ble_npl_event *ev)
{
    struct ble_svc_ras_rreq *rreq = ble_npl_event_get_arg(ev);

    /* Same stale-event guards as ble_svc_ras_rreq_get_rd_timeout_cb.  The
     * is_active guard matters most here: a new procedure's first segment
     * re-arms this callout with rt_in_progress set again, so the
     * !rt_in_progress guard below would not recognise the previous
     * procedure's expiry. */
    if (rreq->conn_handle == BLE_HS_CONN_HANDLE_NONE ||
        ble_npl_callout_is_active(&rreq->rt_inter_seg_timer)) {
        return;
    }
    if (!rreq->rt_in_progress) {
        return;
    }
    BLE_HS_LOG(WARN, "[RREQ] RT inter-segment 1s timeout conn=%d\n",
               rreq->conn_handle);
    ble_svc_ras_rreq_rt_drop(rreq, BLE_HS_ETIMEOUT);
}

/* Public API: Allocation / Teardown */
int
ble_svc_ras_rreq_alloc(uint16_t conn_handle,
                       const struct ble_svc_ras_handles *handles)
{
    struct ble_npl_callout saved_t0, saved_t1, saved_t2, saved_t3;
    struct ble_svc_ras_rreq *rreq;
    struct ble_npl_eventq *evq;
    bool had_timers;
    uint8_t gen;
    int rc;
    int i;

    rc = ble_svc_ras_rreq_pool_init();
    if (rc != 0) {
        return rc;
    }

    if (ble_svc_ras_rreq_find(conn_handle)) {
        return 0;
    }

    for (i = 0; i < RAS_RREQ_MAX_CONN; i++) {
        if (ble_svc_ras_rreq_pool[i].conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            rreq = &ble_svc_ras_rreq_pool[i];

            had_timers = rreq->timers_inited;
            if (had_timers) {
                ble_npl_callout_stop(&rreq->get_rd_timer);
                ble_npl_callout_stop(&rreq->inter_seg_timer);
                ble_npl_callout_stop(&rreq->rt_first_seg_timer);
                ble_npl_callout_stop(&rreq->rt_inter_seg_timer);
                saved_t0 = rreq->get_rd_timer;
                saved_t1 = rreq->inter_seg_timer;
                saved_t2 = rreq->rt_first_seg_timer;
                saved_t3 = rreq->rt_inter_seg_timer;
            }

            gen = rreq->alloc_gen;
            memset(rreq, 0, sizeof(*rreq));
            rreq->alloc_gen = gen + 1;
            rreq->conn_handle = conn_handle;
            rreq->handles = *handles;

            if (had_timers) {
                rreq->get_rd_timer = saved_t0;
                rreq->inter_seg_timer = saved_t1;
                rreq->rt_first_seg_timer = saved_t2;
                rreq->rt_inter_seg_timer = saved_t3;
                rreq->timers_inited = true;
            } else {
#ifdef MYNEWT
                evq = (struct ble_npl_eventq *)os_eventq_dflt_get();
#else
                evq = nimble_port_get_dflt_eventq();
#endif

                ble_npl_callout_init(&rreq->get_rd_timer, evq,
                                     ble_svc_ras_rreq_get_rd_timeout_cb,
                                     rreq);
                ble_npl_callout_init(&rreq->inter_seg_timer, evq,
                                     ble_svc_ras_rreq_inter_seg_timeout_cb,
                                     rreq);
                ble_npl_callout_init(&rreq->rt_first_seg_timer, evq,
                                     ble_svc_ras_rreq_rt_first_seg_timeout_cb,
                                     rreq);
                ble_npl_callout_init(&rreq->rt_inter_seg_timer, evq,
                                     ble_svc_ras_rreq_rt_inter_seg_timeout_cb,
                                     rreq);
                rreq->timers_inited = true;
            }
            BLE_HS_LOG(INFO, "[RREQ] alloc conn=%d\n", conn_handle);
            return 0;
        }
    }

    BLE_HS_LOG(ERROR, "[RREQ] pool full conn=%d\n", conn_handle);
    return BLE_HS_ENOMEM;
}

void
ble_svc_ras_rreq_free(uint16_t conn_handle)
{
    struct ble_svc_ras_rreq *rreq = ble_svc_ras_rreq_find(conn_handle);
    ble_svc_ras_rreq_data_complete_fn *saved_cb;
    uint8_t saved_gen;
    uint16_t counter;

    if (rreq) {
        if (rreq->timers_inited) {
            ble_npl_callout_stop(&rreq->get_rd_timer);
            ble_npl_callout_stop(&rreq->inter_seg_timer);
            ble_npl_callout_stop(&rreq->rt_first_seg_timer);
            ble_npl_callout_stop(&rreq->rt_inter_seg_timer);
        }
        saved_gen = rreq->alloc_gen;
        if (rreq->get_in_progress) {
            saved_cb = rreq->get_data_cb;
            counter = rreq->get_counter;
            rreq->get_in_progress = false;
            rreq->get_data_cb = NULL;
            ble_svc_ras_rreq_fire_data_complete(rreq, saved_cb, NULL, 0,
                                                BLE_HS_ENOTCONN, counter);
            if (rreq->alloc_gen != saved_gen ||
                rreq->conn_handle == BLE_HS_CONN_HANDLE_NONE) {
                return;
            }
        }
        if (rreq->rt_in_progress) {
            counter = rreq->rt_ranging_counter;
            rreq->rt_in_progress = false;
            ble_svc_ras_rreq_fire_data_complete(rreq, NULL, NULL, 0,
                                                BLE_HS_ENOTCONN, counter);
            if (rreq->alloc_gen != saved_gen ||
                rreq->conn_handle == BLE_HS_CONN_HANDLE_NONE) {
                return;
            }
        }
        /* Clear stale async callback pointers so a GATT response arriving
         * after free doesn't invoke an old application callback. */
        rreq->features_cb = NULL;
        /* subscribe_cb is not fired here: the host cancels pending GATT
         * write callbacks on disconnect with BLE_HS_ENOTCONN, so the
         * subscribe completion arrives through the normal callback path.
         * Clearing the pointer prevents a stale invocation on slot reuse. */
        rreq->subscribe_cb = NULL;
        /* Clear reassembly state before the entry becomes eligible for
         * reuse. */
        rreq->reassembly_len = 0;
        rreq->rt_reassembly_len = 0;
        BLE_HS_LOG(INFO, "[RREQ] free conn=%d\n", conn_handle);
        rreq->conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
}

/* Teardown counterpart to ble_svc_ras_rreq_pool_init().  Stops the
 * per-connection callouts and completes any in-flight transfer with an
 * error.  Safe to call more than once. */
static void
ble_svc_ras_rreq_pool_deinit(void)
{
    ble_svc_ras_rreq_data_complete_fn *saved_cb;
    struct ble_svc_ras_rreq *rreq;
    uint8_t saved_gen;
    uint16_t counter;
    int i;

    if (!ble_svc_ras_rreq_pool_inited) {
        return;
    }

    for (i = 0; i < RAS_RREQ_MAX_CONN; i++) {
        rreq = &ble_svc_ras_rreq_pool[i];
        if (rreq->timers_inited) {
            ble_npl_callout_stop(&rreq->get_rd_timer);
            ble_npl_callout_stop(&rreq->inter_seg_timer);
            ble_npl_callout_stop(&rreq->rt_first_seg_timer);
            ble_npl_callout_stop(&rreq->rt_inter_seg_timer);
        }
        if (rreq->conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            /* The continue on alloc_gen/NONE mismatch is intentional:
             * if the callback did free+alloc, free already cleaned up the
             * old state and alloc initialized a fresh slot.  Continuing
             * the loop prevents the deinit cleanup from corrupting the
             * re-allocated entry.  The re-allocated slot is invalidated
             * after the loop when ble_svc_ras_rreq_pool_inited is set to
             * false. */
            saved_gen = rreq->alloc_gen;
            if (rreq->get_in_progress) {
                saved_cb = rreq->get_data_cb;
                counter = rreq->get_counter;
                rreq->get_in_progress = false;
                rreq->get_data_cb = NULL;
                ble_svc_ras_rreq_fire_data_complete(rreq, saved_cb, NULL, 0,
                                                    BLE_HS_ENOTCONN, counter);
                if (rreq->alloc_gen != saved_gen ||
                    rreq->conn_handle == BLE_HS_CONN_HANDLE_NONE) {
                    continue;
                }
            }
            if (rreq->rt_in_progress) {
                counter = rreq->rt_ranging_counter;
                rreq->rt_in_progress = false;
                ble_svc_ras_rreq_fire_data_complete(rreq, NULL, NULL, 0,
                                                    BLE_HS_ENOTCONN, counter);
                if (rreq->alloc_gen != saved_gen ||
                    rreq->conn_handle == BLE_HS_CONN_HANDLE_NONE) {
                    continue;
                }
            }
            rreq->subscribe_cb = NULL;
        }
        rreq->reassembly_len = 0;
        rreq->rt_reassembly_len = 0;
        rreq->conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }

    /* Stop the callouts in a second pass: the data-complete callbacks fired
     * above may restart a timer, and the loop's continue paths skip their own
     * tail. */
    for (i = 0; i < RAS_RREQ_MAX_CONN; i++) {
        rreq = &ble_svc_ras_rreq_pool[i];
        if (rreq->timers_inited) {
            ble_npl_callout_stop(&rreq->get_rd_timer);
            ble_npl_callout_stop(&rreq->inter_seg_timer);
            ble_npl_callout_stop(&rreq->rt_first_seg_timer);
            ble_npl_callout_stop(&rreq->rt_inter_seg_timer);
            rreq->timers_inited = false;
        }
    }

    ble_svc_ras_rreq_pool_inited = false;

    /* The pool itself stays where it is: a pending GATT client callback,
     * ble_svc_ras_rreq_features_read_cb() for instance, may still hold an arg
     * pointer into an entry.  Every conn_handle was set to NONE above, so such
     * a callback hits the conn_handle guard and bails out. */
}

/* Drops everything this module cached in an earlier host incarnation.
 *
 * nimble_port_deinit() releases the NPL callout blocks without telling the
 * services, so on the next nimble_port_init() the entries here would still
 * carry the conn_handles, reassembly buffers and callout handles of a host
 * that no longer exists.  Every entry is put back into its never-used state
 * and the pool latch is cleared, so ble_svc_ras_rreq_pool_init() rebuilds the
 * rest.
 *
 * Clearing conn_handle is what makes the order safe: the pool is allocated on
 * first use, so the window between here and the first alloc() is reachable by
 * ble_svc_ras_rreq_find(), and an entry left carrying an old handle would be
 * handed to a caller that then arms a zeroed callout.
 *
 * alloc_gen is carried across so it keeps counting up: a GATT callback left
 * over from the previous host compares it against its own snapshot, and a
 * value that went backwards could match one of those.
 *
 * On a first init the pool is untouched and every entry is already zero, so
 * this does nothing. */
void
ble_svc_ras_rreq_init(void)
{
    struct ble_svc_ras_rreq *rreq;
    uint8_t gen;
    int i;

    for (i = 0; i < RAS_RREQ_MAX_CONN; i++) {
        rreq = &ble_svc_ras_rreq_pool[i];
        gen = rreq->alloc_gen;

        memset(rreq, 0, sizeof(*rreq));
        rreq->alloc_gen = gen;
        rreq->conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }

    ble_svc_ras_rreq_pool_inited = false;
}

void
ble_svc_ras_rreq_deinit(void)
{
    ble_svc_ras_rreq_pool_deinit();
}

/* Public API: Callback Registration */
int
ble_svc_ras_rreq_set_rd_ready_cb(uint16_t conn_handle,
                                 ble_svc_ras_rreq_rd_ready_fn *cb)
{
    struct ble_svc_ras_rreq *rreq = ble_svc_ras_rreq_find(conn_handle);

    if (rreq == NULL) {
        return BLE_HS_ENOENT;
    }
    rreq->rd_ready_cb = cb;
    return 0;
}

int
ble_svc_ras_rreq_set_rd_overwritten_cb(uint16_t conn_handle,
                                       ble_svc_ras_rreq_rd_overwritten_fn *cb)
{
    struct ble_svc_ras_rreq *rreq = ble_svc_ras_rreq_find(conn_handle);

    if (rreq == NULL) {
        return BLE_HS_ENOENT;
    }
    rreq->rd_ow_cb = cb;
    return 0;
}

int
ble_svc_ras_rreq_set_data_complete_cb(uint16_t conn_handle,
                                      ble_svc_ras_rreq_data_complete_fn *cb)
{
    struct ble_svc_ras_rreq *rreq = ble_svc_ras_rreq_find(conn_handle);

    if (rreq == NULL) {
        return BLE_HS_ENOENT;
    }
    rreq->persistent_data_cb = cb;
    return 0;
}

/* Public API: Ranging Mode (RAP 4.4) */
int
ble_svc_ras_rreq_set_ranging_mode(uint16_t conn_handle, bool use_realtime)
{
    struct ble_svc_ras_rreq *rreq = ble_svc_ras_rreq_find(conn_handle);

    if (rreq == NULL) {
        return BLE_HS_ENOENT;
    }
    if (rreq->get_in_progress || rreq->rt_in_progress) {
        BLE_HS_LOG(ERROR,
                   "[RREQ] set_ranging_mode rejected: transfer in progress "
                   "conn=%d\n",
                   conn_handle);
        return BLE_HS_EBUSY;
    }
    rreq->use_realtime = use_realtime;
    BLE_HS_LOG(INFO, "[RREQ] Ranging mode set to %s conn=%d\n",
               use_realtime ? "REAL-TIME" : "ON-DEMAND", conn_handle);
    return 0;
}

/* Public API: data RD CCCD mode (notify vs indicate, applies to both OD
 * and RT) */
int
ble_svc_ras_rreq_set_indicate(uint16_t conn_handle, bool use_indicate)
{
    struct ble_svc_ras_rreq *rreq = ble_svc_ras_rreq_find(conn_handle);

    if (rreq == NULL) {
        return BLE_HS_ENOENT;
    }
    rreq->use_indicate = use_indicate;
    return 0;
}

/* Public API: Real-time CS Started notification */
int
ble_svc_ras_rreq_rt_cs_started(uint16_t conn_handle)
{
    struct ble_svc_ras_rreq *rreq = ble_svc_ras_rreq_find(conn_handle);
    uint16_t old_counter;
    uint8_t saved_gen;

    if (rreq == NULL) {
        return BLE_HS_ENOENT;
    }
    if (!rreq->use_realtime) {
        return BLE_HS_EINVAL;
    }

    if (rreq->rt_in_progress) {
        BLE_HS_LOG(WARN,
                   "[RREQ] RT CS started while previous RT in progress, "
                   "discarding incomplete data conn=%d\n",
                   conn_handle);
        old_counter = rreq->rt_ranging_counter;
        saved_gen = rreq->alloc_gen;
        rreq->rt_in_progress = false;
        rreq->rt_reassembly_len = 0;
        ble_svc_ras_rreq_fire_data_complete(rreq, NULL, NULL, 0,
                                            BLE_HS_ETIMEOUT, old_counter);
        if (rreq->alloc_gen != saved_gen ||
            rreq->conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            return BLE_HS_ENOTCONN;
        }
    }

    BLE_HS_LOG(DEBUG,
               "[RREQ] RT CS started, arming 5s no-data timer conn=%d\n",
               conn_handle);
    if (rreq->timers_inited) {
        ble_npl_callout_stop(&rreq->rt_inter_seg_timer);
        ble_npl_callout_reset(
            &rreq->rt_first_seg_timer,
            ble_npl_time_ms_to_ticks32(RREQ_GET_RD_TIMEOUT_MS));
    }
    return 0;
}

/* Public API: Subscribe to RAS CCCDs */
static int
ble_svc_ras_rreq_subscribe_cb(uint16_t conn_handle,
                              const struct ble_gatt_error *error,
                              struct ble_gatt_attr *attr, void *arg)
{
    struct ble_svc_ras_rreq *rreq = (struct ble_svc_ras_rreq *)arg;
    ble_svc_ras_rreq_subscribe_fn *cb;

    if (rreq->conn_handle != conn_handle) {
        return 0;
    }

    BLE_HS_LOG(DEBUG,
               "[RREQ] subscribe cb conn=%d attr=%d status=%d done=%d/%d\n",
               conn_handle, attr->handle, error->status,
               rreq->subscribe_done + 1, rreq->subscribe_total);

    if (error->status != 0) {
        /* Same rollback as the synchronous path in
         * ble_svc_ras_rreq_subscribe(): the CCCD the server rejected still
         * holds whatever it held before this round. */
        if (attr != NULL && attr->handle != 0) {
            if (attr->handle == rreq->handles.od_rd_ccc) {
                rreq->od_rd_ccc_enabled = rreq->od_rd_ccc_prev;
            } else if (attr->handle == rreq->handles.rt_rd_ccc) {
                rreq->rt_rd_ccc_enabled = rreq->rt_rd_ccc_prev;
            }
        }
        if (rreq->subscribe_status == 0) {
            rreq->subscribe_status = error->status;
        }
    }

    rreq->subscribe_done++;

    if (rreq->subscribe_done >= rreq->subscribe_total) {
        BLE_HS_LOG(INFO, "[RREQ] subscribe complete conn=%d status=%d\n",
                   rreq->conn_handle, rreq->subscribe_status);
        if (rreq->subscribe_cb) {
            cb = rreq->subscribe_cb;
            rreq->subscribe_cb = NULL;
            cb(rreq->conn_handle, rreq->subscribe_status);
        }
    }
    return 0;
}

static int
ble_svc_ras_rreq_write_ccc(uint16_t conn_handle, uint16_t ccc_handle,
                           uint16_t ccc_val, struct ble_svc_ras_rreq *rreq)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(ccc_val & 0xFF);
    buf[1] = (uint8_t)(ccc_val >> 8);
    return ble_gattc_write_flat(conn_handle, ccc_handle, buf, sizeof(buf),
                                ble_svc_ras_rreq_subscribe_cb, rreq);
}

int
ble_svc_ras_rreq_subscribe(uint16_t conn_handle,
                           ble_svc_ras_rreq_subscribe_fn *cb)
{
    struct ble_svc_ras_rreq *rreq = ble_svc_ras_rreq_find(conn_handle);
    ble_svc_ras_rreq_subscribe_fn *scb;
    struct {
        uint16_t ccc_handle;
        uint16_t ccc_val;
    } subs[6];
    int n;
    int rc;
    int i;

    if (rreq == NULL) {
        return BLE_HS_ENOENT;
    }

    /* A round in flight owns subscribe_done/total/status and subscribe_cb,
     * and the GATT write callback cannot tell which round a completion
     * belongs to.  Resetting the counters under it would let leftovers from
     * the old round complete the new one early, reporting success before its
     * own writes are acknowledged.  Refuse instead, as get_ranging_data does.
     * Every round ends with subscribe_done == subscribe_total: the write loop
     * below counts its own synchronous failures, so the guard cannot latch. */
    if (rreq->subscribe_total != 0 &&
        rreq->subscribe_done < rreq->subscribe_total) {
        BLE_HS_LOG(ERROR,
                   "[RREQ] subscribe already in progress conn=%d done=%d/%d\n",
                   conn_handle, rreq->subscribe_done, rreq->subscribe_total);
        return BLE_HS_EBUSY;
    }

    rreq->subscribe_total = 0;
    rreq->subscribe_done = 0;
    rreq->subscribe_status = 0;

    rreq->od_rd_ccc_prev = rreq->od_rd_ccc_enabled;
    rreq->rt_rd_ccc_prev = rreq->rt_rd_ccc_enabled;

    /*
     * RAP-specified subscription order:
     *  1. RD Overwritten (indicate)
     *  2. RD Ready       (indicate)
     *  3. Data: On-demand RD and/or Real-time RD (notify or indicate per
     *     use_indicate)
     *  4. CP             (indicate)
     */
    n = 0;

    /* Switching data mode on a live connection: disable the previous mode's
     * CCCD before enabling the new one, otherwise the server sees a write
     * that would leave both enabled and rejects it.  Queued first, and GATT
     * write procedures on one connection are served in order. */
    if (rreq->use_realtime && rreq->od_rd_ccc_enabled) {
        subs[n].ccc_handle = rreq->handles.od_rd_ccc;
        subs[n].ccc_val = 0;
        n++;
        rreq->od_rd_ccc_enabled = 0;
    } else if (!rreq->use_realtime && rreq->rt_rd_ccc_enabled) {
        subs[n].ccc_handle = rreq->handles.rt_rd_ccc;
        subs[n].ccc_val = 0;
        n++;
        rreq->rt_rd_ccc_enabled = 0;
    }

    /* RD Overwritten and RD Ready are On-demand only (RAS 2.7.1 / 3.4):
     * "The RAS Server shall not indicate the Ranging Data Ready indication
     *  in Real-time Ranging Data mode." */
    if (!rreq->use_realtime && rreq->handles.rd_overwritten_ccc) {
        subs[n].ccc_handle = rreq->handles.rd_overwritten_ccc;
        subs[n].ccc_val = BLE_GATT_CCCD_INDICATE;
        n++;
    }
    if (!rreq->use_realtime && rreq->handles.rd_ready_ccc) {
        subs[n].ccc_handle = rreq->handles.rd_ready_ccc;
        subs[n].ccc_val = BLE_GATT_CCCD_INDICATE;
        n++;
    }
    /* RAP 4.4: shall enable ONE of on-demand or real-time, never both.
     * Selection driven by ble_svc_ras_rreq_set_ranging_mode() (default:
     * on-demand). */
    if (rreq->use_realtime && rreq->handles.rt_rd_ccc) {
        subs[n].ccc_handle = rreq->handles.rt_rd_ccc;
        subs[n].ccc_val = rreq->use_indicate ? BLE_GATT_CCCD_INDICATE :
                                               BLE_GATT_CCCD_NOTIFY;
        n++;
        rreq->rt_rd_ccc_enabled = 1;
    } else if (!rreq->use_realtime && rreq->handles.od_rd_ccc) {
        subs[n].ccc_handle = rreq->handles.od_rd_ccc;
        subs[n].ccc_val = rreq->use_indicate ? BLE_GATT_CCCD_INDICATE :
                                               BLE_GATT_CCCD_NOTIFY;
        n++;
        rreq->od_rd_ccc_enabled = 1;
    }
    if (rreq->handles.cp_ccc) {
        subs[n].ccc_handle = rreq->handles.cp_ccc;
        subs[n].ccc_val = BLE_GATT_CCCD_INDICATE;
        n++;
    }

    if (n == 0) {
        BLE_HS_LOG(ERROR, "[RREQ] subscribe: no CCC handles set conn=%d\n",
                   conn_handle);
        return BLE_HS_EINVAL;
    }

    rreq->subscribe_cb = cb;
    rreq->subscribe_total = n;

    for (i = 0; i < n; i++) {
        rc = ble_svc_ras_rreq_write_ccc(conn_handle, subs[i].ccc_handle,
                                        subs[i].ccc_val, rreq);
        if (rc != 0) {
            BLE_HS_LOG(ERROR,
                       "[RREQ] subscribe write ccc failed handle=0x%04x "
                       "rc=%d\n",
                       subs[i].ccc_handle, rc);
            /* The data-mode flags were set above assuming the write lands.
             * Put back what the server actually has, or a retry skips the
             * "disable the previous mode" write and the server rejects the
             * enable for good.  The pre-round value is the answer for both
             * directions, including a re-enable of a CCCD this connection had
             * already enabled in an earlier round. */
            if (subs[i].ccc_handle == rreq->handles.od_rd_ccc) {
                rreq->od_rd_ccc_enabled = rreq->od_rd_ccc_prev;
            } else if (subs[i].ccc_handle == rreq->handles.rt_rd_ccc) {
                rreq->rt_rd_ccc_enabled = rreq->rt_rd_ccc_prev;
            }
            if (rreq->subscribe_status == 0) {
                rreq->subscribe_status = rc;
            }
            rreq->subscribe_done++;
        }
    }

    if (rreq->subscribe_done >= rreq->subscribe_total) {
        BLE_HS_LOG(INFO,
                   "[RREQ] subscribe immediate complete conn=%d status=%d\n",
                   conn_handle, rreq->subscribe_status);
        if (rreq->subscribe_cb) {
            scb = rreq->subscribe_cb;
            rreq->subscribe_cb = NULL;
            scb(conn_handle, rreq->subscribe_status);
        }
    }

    return 0;
}

/* Public API: Read Features */
static int
ble_svc_ras_rreq_features_read_cb(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  struct ble_gatt_attr *attr, void *arg)
{
    struct ble_svc_ras_rreq *rreq = (struct ble_svc_ras_rreq *)arg;
    uint8_t feat_buf[sizeof(uint32_t)];
    ble_svc_ras_rreq_features_fn *cb;
    uint32_t features;
    uint16_t pktlen;
    uint16_t copy_len;
    int cb_status;

    if (rreq->conn_handle != conn_handle) {
        return 0;
    }

    if (error->status == 0 && attr->om) {
        pktlen = OS_MBUF_PKTLEN(attr->om);
        if (pktlen == 0) {
            BLE_HS_LOG(ERROR, "[RREQ] Features read empty response conn=%d\n",
                       rreq->conn_handle);
            if (rreq->features_cb) {
                cb = rreq->features_cb;
                rreq->features_cb = NULL;
                cb(rreq->conn_handle, 0, BLE_HS_EBADDATA);
            }
            return 0;
        }
        copy_len = pktlen <= sizeof(feat_buf) ? pktlen : sizeof(feat_buf);
        memset(feat_buf, 0, sizeof(feat_buf));
        os_mbuf_copydata(attr->om, 0, copy_len, feat_buf);
        features = get_le32(feat_buf);
        rreq->server_features = features;
        rreq->features_valid = true;
        BLE_HS_LOG(INFO,
                   "[RREQ] Features read conn=%d features=0x%08" PRIx32 "\n",
                   rreq->conn_handle, features);

        if (rreq->features_cb) {
            cb = rreq->features_cb;
            rreq->features_cb = NULL;
            cb(rreq->conn_handle, features, 0);
        }
    } else {
        cb_status = (error->status != 0) ? error->status : BLE_HS_EBADDATA;
        BLE_HS_LOG(ERROR, "[RREQ] Features read failed conn=%d status=%d\n",
                   rreq->conn_handle, cb_status);
        if (rreq->features_cb) {
            cb = rreq->features_cb;
            rreq->features_cb = NULL;
            cb(rreq->conn_handle, 0, cb_status);
        }
    }
    return 0;
}

int
ble_svc_ras_rreq_read_features(uint16_t conn_handle,
                               ble_svc_ras_rreq_features_fn *cb)
{
    struct ble_svc_ras_rreq *rreq = ble_svc_ras_rreq_find(conn_handle);
    int rc;

    if (rreq == NULL) {
        return BLE_HS_ENOENT;
    }

    if (!rreq->handles.features) {
        return BLE_HS_EINVAL;
    }

    /* No EBUSY guard, unlike get_ranging_data.  That one owns a multi-event
     * transfer with reassembly buffers and watchdogs, so a second start would
     * corrupt state already in flight.  This owns nothing: RAS Features is a
     * single static value for the life of the connection (RAS 3.1), so
     * overlapping reads are idempotent and the callback slot is simply
     * last-writer-wins.  The result is cached in server_features on every
     * completion independently of the callback, so a caller can always take it
     * from ble_svc_ras_rreq_get_server_features(). */
    rreq->features_cb = cb;

    rc = ble_gattc_read(conn_handle, rreq->handles.features,
                        ble_svc_ras_rreq_features_read_cb, rreq);
    if (rc != 0) {
        rreq->features_cb = NULL;
        BLE_HS_LOG(ERROR, "[RREQ] Features read start failed conn=%d rc=%d\n",
                   conn_handle, rc);
    }
    return rc;
}

uint32_t
ble_svc_ras_rreq_get_server_features(uint16_t conn_handle)
{
    struct ble_svc_ras_rreq *rreq = ble_svc_ras_rreq_find(conn_handle);

    if (rreq == NULL || !rreq->features_valid) {
        return 0;
    }
    return rreq->server_features;
}

/* Public API: Get Ranging Data (GET_RD) */
int
ble_svc_ras_rreq_get_ranging_data(uint16_t conn_handle,
                                  uint16_t ranging_counter,
                                  ble_svc_ras_rreq_data_complete_fn *cb)
{
    struct ble_svc_ras_rreq *rreq = ble_svc_ras_rreq_find(conn_handle);
    uint8_t cmd[BLE_SVC_RAS_CP_CMD_OPCODE_LEN + sizeof(uint16_t)];
    int rc;

    if (rreq == NULL) {
        return BLE_HS_ENOENT;
    }

    if (rreq->use_realtime) {
        BLE_HS_LOG(ERROR,
                   "[RREQ] GET_RD rejected: real-time mode active conn=%d\n",
                   conn_handle);
        return BLE_HS_EINVAL;
    }

    if (rreq->get_in_progress) {
        BLE_HS_LOG(ERROR, "[RREQ] GET already in progress conn=%d\n",
                   conn_handle);
        return BLE_HS_EBUSY;
    }

    if (!rreq->handles.cp) {
        BLE_HS_LOG(ERROR,
                   "[RREQ] GET_RD rejected: CP handle not discovered "
                   "conn=%d\n",
                   conn_handle);
        return BLE_HS_EINVAL;
    }

    rreq->get_in_progress = true;
    rreq->get_counter = BLE_SVC_RAS_RANGING_COUNTER(ranging_counter);
    rreq->next_expected_seg = 0;
    rreq->first_received = false;
    rreq->last_received = false;
    rreq->ack_sent = false;
    rreq->reassembly_len = 0;
    rreq->get_data_cb = cb;
    rreq->complete_rd_rsp_received = false;
    rreq->seg_data_len_hint = 0;
    rreq->has_gaps = false;
    /* RAP 4.5.4.1: start 5 s timeout - shall abort if no data PDU arrives */
    if (rreq->timers_inited) {
        ble_npl_callout_reset(
            &rreq->get_rd_timer,
            ble_npl_time_ms_to_ticks32(RREQ_GET_RD_TIMEOUT_MS));
    }

    /* Step 2: Send GET_RD to reflector (CP write); reflector will send
     * segments (step 3). */
    cmd[0] = BLE_SVC_RAS_CP_OPCODE_GET_RD;
    put_le16(&cmd[1], rreq->get_counter);

    rc = ble_gattc_write_no_rsp_flat(conn_handle, rreq->handles.cp, cmd,
                                     sizeof(cmd));
    BLE_HS_LOG(INFO, "[RREQ] Sent GET_RD counter=%d rc=%d conn=%d\n",
               rreq->get_counter, rc, conn_handle);
    if (rc != 0) {
        ble_svc_ras_rreq_reset_reassembly(rreq);
    }
    return rc;
}

/* Public API: Abort */
int
ble_svc_ras_rreq_abort(uint16_t conn_handle)
{
    struct ble_svc_ras_rreq *rreq = ble_svc_ras_rreq_find(conn_handle);
    ble_svc_ras_rreq_data_complete_fn *saved_cb;
    uint8_t cmd[BLE_SVC_RAS_CP_CMD_OPCODE_LEN];
    uint8_t saved_gen;
    uint16_t counter;
    int status;
    int rc;

    if (rreq == NULL) {
        return BLE_HS_ENOENT;
    }

    rc = 0;
    /* handles.cp is not validated here (unlike get_ranging_data / set_filter)
     * because features_valid && BLE_SVC_RAS_FEAT_ABORT_OP_BIT already implies
     * successful feature discovery, which requires a valid CP handle.  If
     * handle is 0 the write-no-rsp is harmlessly dropped by the remote - no
     * crash or state corruption occurs. */
    if (rreq->features_valid &&
        (rreq->server_features & BLE_SVC_RAS_FEAT_ABORT_OP_BIT)) {
        cmd[0] = BLE_SVC_RAS_CP_OPCODE_ABORT_OP;

        rc = ble_gattc_write_no_rsp_flat(conn_handle, rreq->handles.cp, cmd,
                                         sizeof(cmd));
        BLE_HS_LOG(INFO, "[RREQ] Sent ABORT rc=%d conn=%d\n", rc, conn_handle);
    } else {
        BLE_HS_LOG(DEBUG,
                   "[RREQ] ABORT_OP skipped, not supported by server "
                   "conn=%d\n",
                   conn_handle);
    }

    /* Only notify the application when a transfer was actually in progress.
     * Always fire (success or failure) so the app is never left waiting for
     * a callback that will never arrive - mirrors the pattern used by
     * ble_svc_ras_rreq_timeout_abort() and
     * ble_svc_ras_rreq_handle_rd_overwritten(). */
    saved_gen = rreq->alloc_gen;
    if (rreq->get_in_progress) {
        counter = rreq->get_counter;
        saved_cb = rreq->get_data_cb;
        ble_svc_ras_rreq_reset_reassembly(rreq);
        /* Use BLE_HS_EAPP for a clean user-initiated abort so the app can
         * distinguish it from a timeout (BLE_HS_ETIMEOUT) or transport
         * error.  On write failure pass the actual error code. */
        status = (rc == 0) ? BLE_HS_EAPP : rc;
        ble_svc_ras_rreq_fire_data_complete(rreq, saved_cb, NULL, 0, status,
                                            counter);
        if (rreq->alloc_gen != saved_gen ||
            rreq->conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            return rc;
        }
    }
    /* Stop RT timers on abort: rt_first_seg_timer may be armed by
     * rt_cs_started while rt_in_progress is still false, and would fire
     * a spurious ETIMEOUT after the app already saw a clean abort. */
    if (rreq->use_realtime && rreq->timers_inited) {
        ble_npl_callout_stop(&rreq->rt_first_seg_timer);
        ble_npl_callout_stop(&rreq->rt_inter_seg_timer);
    }
    if (rreq->rt_in_progress) {
        counter = rreq->rt_ranging_counter;
        rreq->rt_in_progress = false;
        rreq->rt_reassembly_len = 0;
        status = (rc == 0) ? BLE_HS_EAPP : rc;
        ble_svc_ras_rreq_fire_data_complete(rreq, NULL, NULL, 0, status,
                                            counter);
    }
    return rc;
}

/* Public API: Set Filter */
int
ble_svc_ras_rreq_set_filter(uint16_t conn_handle, uint8_t mode, uint16_t mask)
{
    struct ble_svc_ras_rreq *rreq = ble_svc_ras_rreq_find(conn_handle);
    uint8_t cmd[BLE_SVC_RAS_CP_CMD_OPCODE_LEN + sizeof(uint16_t)];
    uint16_t filter_config;
    int rc;

    if (rreq == NULL) {
        return BLE_HS_ENOENT;
    }

    if (mode > 3) {
        return BLE_HS_EINVAL;
    }

    if (!rreq->handles.cp) {
        return BLE_HS_EINVAL;
    }

    /* RAP 4.3.2: the client adjusts its RAS operation to the advertised
     * features, so do not issue an Op Code the server would only reject.
     */
    if (!rreq->features_valid ||
        !(rreq->server_features & BLE_SVC_RAS_FEAT_FILTER_RD_BIT)) {
        BLE_HS_LOG(DEBUG,
                   "[RREQ] SET_FILTER skipped, not supported by server "
                   "conn=%d\n",
                   conn_handle);
        return BLE_HS_ENOTSUP;
    }

    filter_config =
        (uint16_t)(mode & 0x03) |
        (uint16_t)((mask & BLE_SVC_RAS_FILTER_DEFAULT_MASK) << 2);

    cmd[0] = BLE_SVC_RAS_CP_OPCODE_SET_FILTER;
    put_le16(&cmd[1], filter_config);

    rc = ble_gattc_write_no_rsp_flat(conn_handle, rreq->handles.cp, cmd,
                                     sizeof(cmd));
    BLE_HS_LOG(INFO,
               "[RREQ] Sent SET_FILTER mode=%d mask=0x%04x rc=%d conn=%d\n",
               mode, mask, rc, conn_handle);
    return rc;
}

/* Internal: Notification Handlers */
static void
ble_svc_ras_rreq_handle_rd_ready(struct ble_svc_ras_rreq *rreq,
                                 struct os_mbuf *om)
{
    uint8_t counter_buf[sizeof(uint16_t)];
    uint16_t ranging_counter;
    int len = OS_MBUF_PKTLEN(om);

    if (len < (int)sizeof(uint16_t)) {
        BLE_HS_LOG(ERROR, "[RREQ] RD Ready too short len=%d\n", len);
        return;
    }

    os_mbuf_copydata(om, 0, sizeof(counter_buf), counter_buf);
    ranging_counter = BLE_SVC_RAS_RANGING_COUNTER(get_le16(counter_buf));

    BLE_HS_LOG(INFO, "[RREQ] RD Ready counter=%d conn=%d\n", ranging_counter,
               rreq->conn_handle);

    if (rreq->rd_ready_cb) {
        rreq->rd_ready_cb(rreq->conn_handle, ranging_counter);
    }
}

/**
 * Step 5 (initiator): send ACK_RD for the current get_counter, then deliver
 * the data.  Called once Complete_RD_RSP and every segment have arrived,
 * which per RAP 4.5.2.1 is what completes the transfer - the trailing
 * RSP_CODE only acknowledges this ACK_RD write.  Sends only once per
 * transfer (ack_sent).  A write failure still delivers, so the transfer
 * does not hang with get_in_progress set.
 */
static void
ble_svc_ras_rreq_send_ack_rd(struct ble_svc_ras_rreq *rreq)
{
    uint8_t ack_buf[BLE_SVC_RAS_CP_CMD_OPCODE_LEN + sizeof(uint16_t)];
    int rc;

    if (rreq->ack_sent) {
        return;
    }
    ack_buf[0] = BLE_SVC_RAS_CP_OPCODE_ACK_RD;
    put_le16(&ack_buf[1], rreq->get_counter);

    rc = ble_gattc_write_no_rsp_flat(rreq->conn_handle, rreq->handles.cp,
                                     ack_buf, sizeof(ack_buf));
    if (rc != 0) {
        BLE_HS_LOG(ERROR,
                   "[RREQ] ACK_RD write failed rc=%d counter=%d conn=%d, "
                   "delivering reassembled data anyway\n",
                   rc, rreq->get_counter, rreq->conn_handle);
        /* Data is fully reassembled at this point, so deliver it despite the
         * ACK failure rather than stalling until the timeout. */
        ble_svc_ras_rreq_deliver_reassembled(rreq, rc);
        return;
    }
    rreq->ack_sent = true;
    BLE_HS_LOG(INFO,
               "[RREQ] Sent ACK_RD counter=%d rc=%d conn=%d "
               "(reassembly_len=%d)\n",
               rreq->get_counter, rc, rreq->conn_handle, rreq->reassembly_len);
    BLE_HS_LOG(INFO, "[RREQ] Procedure complete, peer data len=%d conn=%d\n",
               rreq->reassembly_len, rreq->conn_handle);
    ble_svc_ras_rreq_deliver_reassembled(rreq,
                                         rreq->has_gaps ? BLE_HS_EBADDATA : 0);
}

/*
 * Real-time Ranging Data Handler (RAP 4.4.1)
 *
 * Unlike on-demand, there is no GET_RD / ACK_RD / Complete_RD_RSP.
 * The server pushes segments automatically.  The first segment's
 * payload begins with the 2-byte ranging counter.  Completion is
 * signaled by the last_seg bit in the segment header.
 */
static void
ble_svc_ras_rreq_handle_realtime_rd(struct ble_svc_ras_rreq *rreq,
                                    struct os_mbuf *om)
{
    int notif_len = OS_MBUF_PKTLEN(om);
    uint8_t counter_buf[sizeof(uint16_t)];
    uint8_t seg_hdr_byte;
    uint8_t seg_counter;
    uint8_t saved_gen;
    uint16_t old_counter;
    uint16_t err_counter;
    uint16_t data_len;
    uint16_t cb_len;
    uint16_t cb_ctr;
    bool first_seg;
    bool last_seg;

    if (notif_len < (int)sizeof(struct ble_svc_ras_segment_header)) {
        BLE_HS_LOG(ERROR, "[RREQ] [RT RD] too short len=%d\n", notif_len);
        return;
    }

    /* reassembly_buf is shared between the two modes, and on-demand owns it
     * whenever a GET_RD is outstanding or bytes are already staged in it.  RAP
     * 4.4 keeps the modes apart - only one Ranging Data CCCD is enabled at a
     * time - so in real-time operation both tests below are false and this
     * costs nothing.  Enforcing it locally rather than trusting the peer is
     * what stops a notification on the real-time handle from restarting
     * reassembly at offset 0 over on-demand bytes, and from reaching the
     * last_seg path that hands the buffer to the app while on-demand is
     * still filling it. */
    if (rreq->get_in_progress || rreq->reassembly_len != 0) {
        BLE_HS_LOG(WARN,
                   "[RREQ] [RT RD] ignored, on-demand owns the reassembly "
                   "buffer conn=%d len=%d\n",
                   rreq->conn_handle, rreq->reassembly_len);
        return;
    }

    /* Segment header byte: bit 0 first_seg, bit 1 last_seg, bits 2-7 the
     * rolling segment counter.  Unpacked by hand rather than through struct
     * segment_header so the layout does not depend on bitfield ordering.
     */
    os_mbuf_copydata(om, 0, 1, &seg_hdr_byte);
    first_seg = (seg_hdr_byte & 0x01);
    last_seg = (seg_hdr_byte >> 1) & 0x01;
    seg_counter = (seg_hdr_byte >> 2) & BLE_SVC_RAS_SEG_COUNTER_MASK;

    data_len = (uint16_t)(notif_len -
                          (int)sizeof(struct ble_svc_ras_segment_header));

    BLE_HS_LOG(DEBUG,
               "[RREQ] [RT RD] seg=%d first=%d last=%d data_len=%d conn=%d\n",
               seg_counter, first_seg, last_seg, data_len, rreq->conn_handle);

    /* First segment of a new procedure - extract ranging counter from
     * payload */
    if (first_seg) {
        if (rreq->rt_in_progress) {
            BLE_HS_LOG(WARN,
                       "[RREQ] [RT RD] new first_seg while previous RT in "
                       "progress, discarding incomplete data conn=%d\n",
                       rreq->conn_handle);
            /* Stop the old procedure's inter-segment timer; the re-arm below
             * is what makes an expiry that slipped past this stop detectable
             * as stale in ble_svc_ras_rreq_rt_inter_seg_timeout_cb. */
            if (rreq->timers_inited) {
                ble_npl_callout_stop(&rreq->rt_inter_seg_timer);
            }
            old_counter = rreq->rt_ranging_counter;
            rreq->rt_in_progress = false;
            rreq->rt_reassembly_len = 0;
            /* BLE_HS_EPREEMPTED is used here (not BLE_HS_ETIMEOUT) to
             * distinguish "preempted by new data arriving" from "no data at
             * all" - the old procedure was actively receiving segments when
             * interrupted.  rt_cs_started uses BLE_HS_ETIMEOUT because there
             * the old procedure may have stalled (no new data). */
            saved_gen = rreq->alloc_gen;
            ble_svc_ras_rreq_fire_data_complete(rreq, NULL, NULL, 0,
                                                BLE_HS_EPREEMPTED,
                                                old_counter);
            if (rreq->alloc_gen != saved_gen ||
                rreq->conn_handle == BLE_HS_CONN_HANDLE_NONE) {
                return;
            }
        }
        rreq->rt_in_progress = true;
        rreq->rt_reassembly_len = 0;

        if (data_len < sizeof(uint16_t)) {
            BLE_HS_LOG(ERROR,
                       "[RREQ] [RT RD] first_seg too short for ranging "
                       "counter data_len=%d conn=%d\n",
                       data_len, rreq->conn_handle);
            rreq->rt_in_progress = false;
            if (rreq->timers_inited) {
                ble_npl_callout_stop(&rreq->rt_first_seg_timer);
                ble_npl_callout_stop(&rreq->rt_inter_seg_timer);
            }
            ble_svc_ras_rreq_fire_data_complete(rreq, NULL, NULL, 0,
                                                BLE_HS_EBADDATA, 0);
            return;
        }

        os_mbuf_copydata(om, sizeof(struct ble_svc_ras_segment_header),
                         sizeof(counter_buf), counter_buf);
        rreq->rt_ranging_counter = get_le16(counter_buf) & 0x0FFF;
        BLE_HS_LOG(DEBUG, "[RREQ] [RT RD] ranging_counter=%d conn=%d\n",
                   rreq->rt_ranging_counter, rreq->conn_handle);

        /* Stop the 5s "no data" timer - first segment arrived */
        if (rreq->timers_inited) {
            ble_npl_callout_stop(&rreq->rt_first_seg_timer);
        }
    }

    if (!rreq->rt_in_progress) {
        BLE_HS_LOG(ERROR,
                   "[RREQ] [RT RD] segment received with no first_seg yet "
                   "conn=%d\n",
                   rreq->conn_handle);
        return;
    }

    /* RAS 3.2.2: the Rolling Segment Counter is incremented by 1 per segment.
     * Real-time mode has no lost-segment retrieval (the control point is
     * limited to filtering in this mode), so a gap leaves the remainder of
     * the body misaligned and unparsable - drop the procedure rather than
     * deliver corrupt data as success.  The stream resynchronizes on the
     * next procedure's first segment. */
    if (!first_seg && seg_counter != rreq->rt_next_expected_seg) {
        BLE_HS_LOG(ERROR,
                   "[RREQ] [RT RD] segment gap: got=%d expected=%d conn=%d\n",
                   seg_counter, rreq->rt_next_expected_seg,
                   rreq->conn_handle);
        ble_svc_ras_rreq_rt_drop(rreq, BLE_HS_EBADDATA);
        return;
    }
    rreq->rt_next_expected_seg =
        (seg_counter + 1) & BLE_SVC_RAS_SEG_COUNTER_MASK;

    /* Append segment data to RT reassembly buffer */
    if (rreq->rt_reassembly_len + data_len > BLE_SVC_RAS_PROCEDURE_MEM) {
        BLE_HS_LOG(ERROR,
                   "[RREQ] [RT RD] buffer overflow reassembly_len=%d "
                   "+ data_len=%d conn=%d\n",
                   rreq->rt_reassembly_len, data_len, rreq->conn_handle);
        err_counter = rreq->rt_ranging_counter;
        rreq->rt_in_progress = false;
        rreq->rt_reassembly_len = 0;
        if (rreq->timers_inited) {
            ble_npl_callout_stop(&rreq->rt_inter_seg_timer);
        }
        ble_svc_ras_rreq_fire_data_complete(rreq, NULL, NULL, 0, BLE_HS_ENOMEM,
                                            err_counter);
        return;
    }

    os_mbuf_copydata(om, sizeof(struct ble_svc_ras_segment_header), data_len,
                     &rreq->reassembly_buf[rreq->rt_reassembly_len]);

    /* ranging_counter was already extracted directly from the mbuf in the
     * first_seg pre-extraction block above; no re-extraction needed here. */

    rreq->rt_reassembly_len += data_len;

    /* Reset 1s inter-segment timer */
    if (rreq->timers_inited) {
        if (last_seg) {
            ble_npl_callout_stop(&rreq->rt_inter_seg_timer);
        } else {
            ble_npl_callout_reset(
                &rreq->rt_inter_seg_timer,
                ble_npl_time_ms_to_ticks32(RREQ_INTER_SEG_TIMEOUT_MS));
        }
    }

    /* last_seg -> data complete - deliver to application */
    if (last_seg) {
        BLE_HS_LOG(INFO,
                   "[RREQ] [RT RD] complete: counter=%d total_len=%d "
                   "conn=%d\n",
                   rreq->rt_ranging_counter, rreq->rt_reassembly_len,
                   rreq->conn_handle);

        rreq->rt_in_progress = false;

        /* The reassembly buffer is shared with on-demand reassembly, and it
         * is safe to hand to the callback here because the guard at the top
         * of this function already rejected the notification if on-demand
         * held the buffer: reaching this point means get_in_progress is
         * false and reassembly_len is 0. */
        cb_len = rreq->rt_reassembly_len;
        cb_ctr = rreq->rt_ranging_counter;
        rreq->rt_reassembly_len = 0;
        ble_svc_ras_rreq_fire_data_complete(rreq, NULL, rreq->reassembly_buf,
                                            cb_len, 0, cb_ctr);
    }
}

/*
 * Step 3 (initiator): receive on-demand RD segments.  Segments arrive in
 * rolling-counter order; a counter gap is zero-padded so later segments
 * still land at deterministic offsets.  ACK is deferred until both the
 * last segment and Complete_RD_RSP have been seen (RAP 4.4.2 / 4.5.2.1).
 */
static void
ble_svc_ras_rreq_handle_ondemand_rd(struct ble_svc_ras_rreq *rreq,
                                    struct os_mbuf *om)
{
    int notif_len = OS_MBUF_PKTLEN(om);
    ble_svc_ras_rreq_data_complete_fn *saved_cb;
    uint8_t seg_hdr_byte;
    uint8_t seg_counter;
    uint8_t expected;
    uint8_t fwd_dist;
    uint16_t err_counter;
    uint16_t data_len;
    uint16_t reserve;
    bool first_seg;
    bool last_seg;

    BLE_HS_LOG(DEBUG, "[RREQ] [OD RD] rx len=%d conn=%d\n", notif_len,
               rreq->conn_handle);

    if (notif_len < (int)sizeof(struct ble_svc_ras_segment_header)) {
        BLE_HS_LOG(ERROR, "[RREQ] On-demand RD too short len=%d\n", notif_len);
        return;
    }

    if (!rreq->get_in_progress) {
        BLE_HS_LOG(ERROR,
                   "[RREQ] On-demand RD received but no GET in progress "
                   "conn=%d\n",
                   rreq->conn_handle);
        return;
    }

    /* Segment header layout as in ble_svc_ras_rreq_handle_realtime_rd(). */
    os_mbuf_copydata(om, 0, 1, &seg_hdr_byte);

    first_seg = (seg_hdr_byte & 0x01) != 0;
    last_seg = (seg_hdr_byte & 0x02) != 0;
    seg_counter = (seg_hdr_byte >> 2) & BLE_SVC_RAS_SEG_COUNTER_MASK;

    data_len = (uint16_t)(notif_len -
                          (int)sizeof(struct ble_svc_ras_segment_header));

    BLE_HS_LOG(DEBUG,
               "[RREQ] [OD RD] seg=%d first=%d last=%d data_len=%d conn=%d\n",
               seg_counter, first_seg, last_seg, data_len, rreq->conn_handle);

    /* RAP 4.5.4.1: cancel 5s GET_RD timer on first PDU ever */
    if (!rreq->first_received && rreq->timers_inited) {
        ble_npl_callout_stop(&rreq->get_rd_timer);
    }

    /* RAS 3.2.4: the starting Segment_Counter value is implementation-
     * defined; the first segment only guarantees first_seg == 1.  Align
     * next_expected_seg here so the gap-fill loop below does not treat
     * counters [0, seg_counter) as lost segments. */
    if (first_seg || !rreq->first_received) {
        if (!first_seg) {
            rreq->has_gaps = true;
        }
        /* A first segment arriving mid-transfer means the server restarted
         * the stream; discard the partial body so the new one is not
         * appended to it, and re-learn the segment length from the new
         * stream. */
        if (first_seg && rreq->reassembly_len > 0) {
            BLE_HS_LOG(WARN,
                       "[RREQ] [OD RD] restart: discarding %d partial bytes "
                       "conn=%d\n",
                       rreq->reassembly_len, rreq->conn_handle);
            rreq->reassembly_len = 0;
            rreq->has_gaps = false;
            rreq->seg_data_len_hint = 0;
        }
        rreq->first_received = true;
        rreq->next_expected_seg = seg_counter;
        if (rreq->seg_data_len_hint == 0) {
            rreq->seg_data_len_hint = data_len;
        }
    }

    /* Zero-pad any segments missing between next_expected_seg and the
     * received counter - gaps become permanent holes in the delivered
     * buffer. */
    expected = rreq->next_expected_seg;
    fwd_dist = (seg_counter - expected) & BLE_SVC_RAS_SEG_COUNTER_MASK;
    if (fwd_dist > (BLE_SVC_RAS_SEG_COUNTER_MASK / 2)) {
        BLE_HS_LOG(WARN,
                   "[RREQ] [OD RD] Backward/duplicate seg=%d expected=%d, "
                   "discarding conn=%d\n",
                   seg_counter, expected, rreq->conn_handle);
        return;
    }

    /* RAP 4.5.4.1: reset 1s inter-segment timer only on valid (forward)
     * segments.  Backward/duplicate segments are discarded above and must
     * not extend the timeout - otherwise a misbehaving peer could keep
     * the timer alive indefinitely without advancing reassembly. */
    if (rreq->timers_inited) {
        ble_npl_callout_reset(
            &rreq->inter_seg_timer,
            ble_npl_time_ms_to_ticks32(RREQ_INTER_SEG_TIMEOUT_MS));
    }

    while (expected != seg_counter) {
        reserve =
            (rreq->seg_data_len_hint > 0) ? rreq->seg_data_len_hint : data_len;
        if (rreq->reassembly_len + reserve > BLE_SVC_RAS_PROCEDURE_MEM) {
            BLE_HS_LOG(ERROR,
                       "[RREQ] Gap-fill overflow: missing seg=%d conn=%d\n",
                       expected, rreq->conn_handle);
            err_counter = rreq->get_counter;
            saved_cb = rreq->get_data_cb;
            ble_svc_ras_rreq_reset_reassembly(rreq);
            ble_svc_ras_rreq_fire_data_complete(rreq, saved_cb, NULL, 0,
                                                BLE_HS_ENOMEM, err_counter);
            return;
        }
        memset(&rreq->reassembly_buf[rreq->reassembly_len], 0, reserve);
        rreq->reassembly_len += reserve;
        rreq->has_gaps = true;
        BLE_HS_LOG(WARN,
                   "[RREQ] [OD RD] Gap: seg=%d missing, zero-padded %d "
                   "bytes conn=%d\n",
                   expected, reserve, rreq->conn_handle);
        expected = (expected + 1) & BLE_SVC_RAS_SEG_COUNTER_MASK;
    }

    if (rreq->reassembly_len + data_len > BLE_SVC_RAS_PROCEDURE_MEM) {
        BLE_HS_LOG(ERROR, "[RREQ] Reassembly overflow: %d + %d > %d conn=%d\n",
                   rreq->reassembly_len, data_len, BLE_SVC_RAS_PROCEDURE_MEM,
                   rreq->conn_handle);
        err_counter = rreq->get_counter;
        saved_cb = rreq->get_data_cb;
        ble_svc_ras_rreq_reset_reassembly(rreq);
        ble_svc_ras_rreq_fire_data_complete(rreq, saved_cb, NULL, 0,
                                            BLE_HS_ENOMEM, err_counter);
        return;
    }

    os_mbuf_copydata(om, sizeof(struct ble_svc_ras_segment_header), data_len,
                     &rreq->reassembly_buf[rreq->reassembly_len]);
    rreq->reassembly_len += data_len;
    rreq->next_expected_seg = (seg_counter + 1) & BLE_SVC_RAS_SEG_COUNTER_MASK;

    BLE_HS_LOG(DEBUG,
               "[RREQ] [OD RD] reassembly_len=%d (after seg %d) conn=%d\n",
               rreq->reassembly_len, seg_counter, rreq->conn_handle);

    if (last_seg) {
        rreq->last_received = true;
        /* inter_seg_timer deliberately keeps running past the last segment: it
         * is the only guard against a Complete_RD_RSP that never arrives. */

        BLE_HS_LOG(INFO,
                   "[RREQ] [OD RD] Last seg arrived: total_bytes=%d gaps=%d "
                   "conn=%d\n",
                   rreq->reassembly_len, rreq->has_gaps, rreq->conn_handle);

        /* ACK after Complete_RD_RSP (RAP 4.4.2 / 4.5.2.1).
         * Handle EATT reorder where Complete_RD_RSP arrived first. */
        if (rreq->complete_rd_rsp_received) {
            if (rreq->timers_inited) {
                ble_npl_callout_stop(&rreq->inter_seg_timer);
            }
            ble_svc_ras_rreq_send_ack_rd(rreq);
        }
        /* else: ACK is sent from the Complete_RD_RSP handler. */
    }
}

static void
ble_svc_ras_rreq_handle_cp_indication(struct ble_svc_ras_rreq *rreq,
                                      struct os_mbuf *om)
{
    int len = OS_MBUF_PKTLEN(om);
    ble_svc_ras_rreq_data_complete_fn *saved_cb;
    uint8_t buf[BLE_SVC_RAS_CP_WRITE_MAX_LEN];
    uint16_t resp_counter;
    uint16_t err_counter;
    uint16_t copy_len;
    uint8_t rsp_code;
    uint8_t opcode;
    int status;

    if (len < BLE_SVC_RAS_CP_CMD_OPCODE_LEN) {
        BLE_HS_LOG(ERROR, "[RREQ] CP indication too short len=%d\n", len);
        return;
    }

    copy_len = MIN((uint16_t)len, (uint16_t)sizeof(buf));
    os_mbuf_copydata(om, 0, copy_len, buf);

    opcode = buf[0];

    BLE_HS_LOG(DEBUG, "[RREQ] CP indication opcode=0x%02x len=%d conn=%d\n",
               opcode, len, rreq->conn_handle);

    /* Step 4: Complete_RD_RSP - all segments have been sent by the server.
     * ACK once the last segment has also been received (RAP 4.4.2 / 4.5.2.1);
     * if a segment was lost we ACK anyway with the zero-padded buffer. */
    if (opcode == BLE_SVC_RAS_CP_RSP_OPCODE_COMPLETE_RD_RSP) {
        if (copy_len < 3) {
            BLE_HS_LOG(ERROR, "[RREQ] Complete_RD_RSP too short len=%d\n",
                       copy_len);
            return;
        }

        resp_counter = BLE_SVC_RAS_RANGING_COUNTER(get_le16(&buf[1]));

        BLE_HS_LOG(DEBUG, "[RREQ] Complete_RD_RSP counter=%d conn=%d\n",
                   resp_counter, rreq->conn_handle);

        if (!rreq->get_in_progress) {
            BLE_HS_LOG(WARN,
                       "[RREQ] Complete_RD_RSP ignored: no transfer in "
                       "progress conn=%d\n",
                       rreq->conn_handle);
            return;
        }
        if (resp_counter != rreq->get_counter) {
            BLE_HS_LOG(ERROR,
                       "[RREQ] Complete_RD_RSP counter mismatch: "
                       "expected=%d got=%d conn=%d\n",
                       rreq->get_counter, resp_counter, rreq->conn_handle);
            return;
        }

        rreq->complete_rd_rsp_received = true;

        /* Stop the inter-segment timer only if the last segment has already
         * arrived.  Complete_RD_RSP can arrive before the last segment
         * (EATT reorder); the running timer still catches a lost last
         * segment, since notifications have no ATT-level ACK. */
        if (rreq->timers_inited && rreq->last_received) {
            ble_npl_callout_stop(&rreq->inter_seg_timer);
        }

        if (rreq->has_gaps) {
            BLE_HS_LOG(WARN,
                       "[RREQ] Gaps detected, ACK-ing with zero-padded "
                       "partial data conn=%d\n",
                       rreq->conn_handle);
        }

        if (rreq->last_received) {
            ble_svc_ras_rreq_send_ack_rd(rreq);
        }
        /* else: last segment not yet arrived (EATT reorder) - segment handler
         * will send ACK once the last PDU is in. */

    } else if (opcode == BLE_SVC_RAS_CP_RSP_OPCODE_COMPLETE_LOST_RD_SEG_RSP) {
        BLE_HS_LOG(WARN,
                   "[RREQ] unexpected Complete_Lost_RD_Seg_RSP ignored "
                   "conn=%d\n",
                   rreq->conn_handle);

        /* Step 5-6: RSP_CODE.  Success only acknowledges the preceding RAS-CP
         * write (RAS 3.3.2.2); the data was already delivered when ACK_RD went
         * out.  Any other code aborts the transfer (RAS 3.3.2.1). */
    } else if (opcode == BLE_SVC_RAS_CP_RSP_OPCODE_RSP_CODE) {
        if (copy_len < 2) {
            BLE_HS_LOG(ERROR, "[RREQ] RSP_CODE too short len=%d\n", copy_len);
            return;
        }
        rsp_code = buf[1];
        BLE_HS_LOG(DEBUG, "[RREQ] RSP_CODE code=0x%02x conn=%d\n", rsp_code,
                   rreq->conn_handle);

        if (rsp_code == BLE_SVC_RAS_CP_RSP_SUCCESS ||
            rsp_code == BLE_SVC_RAS_CP_RSP_SUCCESS_PERSISTED) {
            return;
        }

        /* RSP_CODE carries no originating opcode, so the transfer in progress
         * is what an error is charged to.  That is the right target: RAS-CP is
         * a one-command-at-a-time channel (RAS 3.3.2), so the only command
         * that can be answered while get_in_progress holds is one the client
         * chose to issue mid-transfer, and an error on it says the server is
         * no longer serving the request the transfer belongs to.  Failing the
         * transfer is also the recoverable direction - the counter is returned
         * with the status and the caller can re-issue GET_RD - whereas leaving
         * it open would park the client on a transfer the server has stopped
         * feeding until the inter-segment timer expires.
         *
         * ABORT_OP and ACK_RD never reach here with stale state of their own:
         * ABORT_OP resets reassembly before writing, and ACK_RD runs after the
         * data is delivered, which is why get_in_progress alone distinguishes
         * a live transfer from an answer that arrives once one has ended. */
        if (!rreq->get_in_progress) {
            BLE_HS_LOG(DEBUG,
                       "[RREQ] RSP_CODE 0x%02x ignored: no transfer in "
                       "progress conn=%d\n",
                       rsp_code, rreq->conn_handle);
            return;
        }

        BLE_HS_LOG(ERROR, "[RREQ] Server error code=0x%02x conn=%d\n",
                   rsp_code, rreq->conn_handle);
        err_counter = rreq->get_counter;
        saved_cb = rreq->get_data_cb;
        ble_svc_ras_rreq_reset_reassembly(rreq);
        /* BLE_HS_ATT_ERR maps RASCP codes into the ATT error namespace
         * (0x100+code).  While RASCP codes (Table 3.12) technically
         * occupy a different semantic space than ATT error codes, a
         * dedicated BLE_HS_ERR_RAS_BASE would be an API-level change.
         * The collision is cosmetic: apps should treat any non-zero
         * status as failure and log rsp_code for diagnostics. */
        status = BLE_HS_ATT_ERR(rsp_code);
        if (status == 0) {
            status = BLE_HS_EUNKNOWN;
        }
        ble_svc_ras_rreq_fire_data_complete(rreq, saved_cb, NULL, 0, status,
                                            err_counter);
    }
}

static void
ble_svc_ras_rreq_handle_rd_overwritten(struct ble_svc_ras_rreq *rreq,
                                       struct os_mbuf *om)
{
    ble_svc_ras_rreq_data_complete_fn *saved_cb;
    uint8_t counter_buf[sizeof(uint16_t)];
    uint8_t saved_gen;
    uint16_t counter;
    int len = OS_MBUF_PKTLEN(om);

    if (len < (int)sizeof(uint16_t)) {
        BLE_HS_LOG(ERROR, "[RREQ] RD Overwritten too short len=%d\n", len);
        return;
    }

    os_mbuf_copydata(om, 0, sizeof(counter_buf), counter_buf);
    counter = BLE_SVC_RAS_RANGING_COUNTER(get_le16(counter_buf));

    BLE_HS_LOG(INFO, "[RREQ] RD Overwritten counter=%d conn=%d\n", counter,
               rreq->conn_handle);

    saved_gen = rreq->alloc_gen;

    if (rreq->get_in_progress && rreq->get_counter == counter) {
        BLE_HS_LOG(WARN,
                   "[RREQ] Active transfer overwritten, aborting conn=%d\n",
                   rreq->conn_handle);
        saved_cb = rreq->get_data_cb;
        ble_svc_ras_rreq_reset_reassembly(rreq);
        ble_svc_ras_rreq_fire_data_complete(rreq, saved_cb, NULL, 0,
                                            BLE_HS_ETIMEOUT, counter);
    }

    if (rreq->alloc_gen != saved_gen ||
        rreq->conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    if (rreq->rd_ow_cb) {
        rreq->rd_ow_cb(rreq->conn_handle, counter);
    }
}

/* Public API: Notification Dispatch */
int
ble_svc_ras_rreq_on_notify(uint16_t conn_handle, uint16_t attr_handle,
                           struct os_mbuf *om)
{
    struct ble_svc_ras_rreq *rreq = ble_svc_ras_rreq_find(conn_handle);

    if (rreq == NULL) {
        return BLE_HS_ENOENT;
    }

    if (rreq->handles.rd_ready != 0 && attr_handle == rreq->handles.rd_ready) {
        ble_svc_ras_rreq_handle_rd_ready(rreq, om);
        return 0;
    }
    if (rreq->handles.od_rd != 0 && attr_handle == rreq->handles.od_rd) {
        ble_svc_ras_rreq_handle_ondemand_rd(rreq, om);
        return 0;
    }
    if (rreq->handles.rt_rd != 0 && attr_handle == rreq->handles.rt_rd) {
        ble_svc_ras_rreq_handle_realtime_rd(rreq, om);
        return 0;
    }
    if (rreq->handles.cp != 0 && attr_handle == rreq->handles.cp) {
        ble_svc_ras_rreq_handle_cp_indication(rreq, om);
        return 0;
    }
    if (rreq->handles.rd_overwritten != 0 &&
        attr_handle == rreq->handles.rd_overwritten) {
        ble_svc_ras_rreq_handle_rd_overwritten(rreq, om);
        return 0;
    }

    return BLE_HS_ENOENT;
}

/* Public API: Local CS Result Collection */
int
ble_svc_ras_rreq_set_local_cs_cb(uint16_t conn_handle,
                                 ble_svc_ras_rreq_local_cs_fn *cb)
{
    struct ble_svc_ras_rreq *rreq = ble_svc_ras_rreq_find(conn_handle);

    if (rreq == NULL) {
        return BLE_HS_ENOENT;
    }
    rreq->local_cs_cb = cb;
    return 0;
}

int
ble_svc_ras_rreq_cs_event_handler(struct ble_cs_event *event, void *arg)
{
    struct ble_svc_ras_rreq *rreq;
    uint16_t conn_handle;

    switch (event->type) {
    case BLE_CS_EVENT_SUBEVENT_RESULT:
        conn_handle = event->subev_result.conn_handle;
        rreq = ble_svc_ras_rreq_find(conn_handle);
        if (rreq && rreq->local_cs_cb) {
            rreq->local_cs_cb(event);
        }
        break;

    case BLE_CS_EVENT_SUBEVENT_RESULT_CONTINUE:
        conn_handle = event->subev_result_continue.conn_handle;
        rreq = ble_svc_ras_rreq_find(conn_handle);
        if (rreq && rreq->local_cs_cb) {
            rreq->local_cs_cb(event);
        }
        break;

    case BLE_CS_EVENT_PROCEDURE_COMPLETE:
        conn_handle = event->procedure_complete.conn_handle;
        rreq = ble_svc_ras_rreq_find(conn_handle);
        if (rreq && rreq->local_cs_cb) {
            rreq->local_cs_cb(event);
        }
        break;

    default:
        break;
    }

    return 0;
}

#endif /* BLE_CHANNEL_SOUNDING && BLE_SVC_RAS_CLIENT */
