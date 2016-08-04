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

#include <assert.h>
#include <string.h>
#include <errno.h>
#include "bsp/bsp.h"
#include "os/os.h"
#include "nimble/nimble_opt.h"
#include "host/host_hci.h"
#include "host/ble_hs_adv.h"
#include "ble_hs_priv.h"

/**
 * GAP - Generic Access Profile.
 *
 * Design overview:
 *
 * GAP procedures are initiated by the application via function calls.  Such
 * functions return when either of the following happens:
 *
 * (1) The procedure completes (success or failure).
 * (2) The procedure cannot proceed until a BLE peer responds.
 *
 * For (1), the result of the procedure if fully indicated by the function
 * return code.
 * For (2), the procedure result is indicated by an application-configured
 * callback.  The callback is executed when the procedure completes.
 *
 * Notes on thread-safety:
 * 1. The ble_hs mutex must always be unlocked when an application callback is
 *    executed.  The purpose of this requirement is to allow callbacks to
 *    initiate additional host procedures, which may require locking of the
 *    mutex.
 * 2. Functions called directly by the application never call callbacks.
 *    Generally, these functions lock the ble_hs mutex at the start, and only
 *    unlock it at return.
 * 3. Functions which do call callbacks (receive handlers and timer
 *    expirations) generally only lock the mutex long enough to modify
 *    affected state and make copies of data needed for the callback.  A copy
 *    of various pieces of data is called a "snapshot" (struct
 *    ble_gap_snapshot).  The sole purpose of snapshots is to allow callbacks
 *    to be executed after unlocking the mutex.
 */

/** GAP procedure op codes. */
#define BLE_GAP_OP_NULL                                 0
#define BLE_GAP_OP_M_DISC                               1
#define BLE_GAP_OP_M_CONN                               2
#define BLE_GAP_OP_S_ADV                                1

/**
 * If an attempt to cancel an active procedure fails, the attempt is retried
 * at this rate (ms).
 */
#define BLE_GAP_CANCEL_RETRY_RATE                              100 /* ms */

/**
 * The maximum amount of user data that can be put into the advertising data.
 * The stack will automatically insert the flags field on its own if requested
 * by the application, limiting the maximum amount of user data.
 */
#define BLE_GAP_ADV_DATA_LIMIT_FLAGS    (BLE_HCI_MAX_ADV_DATA_LEN - 3)
#define BLE_GAP_ADV_DATA_LIMIT_NO_FLAGS BLE_HCI_MAX_ADV_DATA_LEN

static const struct ble_gap_conn_params ble_gap_conn_params_dflt = {
    .scan_itvl = 0x0010,
    .scan_window = 0x0010,
    .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN,
    .itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX,
    .latency = BLE_GAP_INITIAL_CONN_LATENCY,
    .supervision_timeout = BLE_GAP_INITIAL_SUPERVISION_TIMEOUT,
    .min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN,
    .max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN,
};

/**
 * The state of the in-progress master connection.  If no master connection is
 * currently in progress, then the op field is set to BLE_GAP_OP_NULL.
 */
struct ble_gap_master_state {
    uint8_t op;

    uint8_t exp_set:1;
    os_time_t exp_os_ticks;

    ble_gap_event_fn *cb;
    void *cb_arg;

    union {

        struct {
            uint8_t using_wl:1;
            uint8_t our_addr_type:2;
            uint8_t cancel:1;
        } conn;

        struct {
            uint8_t limited:1;
        } disc;
    };
};
static bssnz_t struct ble_gap_master_state ble_gap_master;

/**
 * The state of the in-progress slave connection.  If no slave connection is
 * currently in progress, then the op field is set to BLE_GAP_OP_NULL.
 */
static bssnz_t struct {
    uint8_t op;

    unsigned exp_set:1;
    os_time_t exp_os_ticks;

    uint8_t conn_mode;
    uint8_t disc_mode;
    unsigned our_addr_type:2;
    ble_gap_event_fn *cb;
    void *cb_arg;

    uint8_t adv_data[BLE_HCI_MAX_ADV_DATA_LEN];
    uint8_t rsp_data[BLE_HCI_MAX_ADV_DATA_LEN];
    uint8_t adv_data_len;
    uint8_t rsp_data_len;

    unsigned adv_auto_flags:1;
} ble_gap_slave;

static int ble_gap_adv_enable_tx(int enable);
static int ble_gap_conn_cancel_tx(void);
static int ble_gap_disc_enable_tx(int enable, int filter_duplicates);

struct ble_gap_snapshot {
    struct ble_gap_conn_desc *desc;
    ble_gap_event_fn *cb;
    void *cb_arg;
};

STATS_SECT_DECL(ble_gap_stats) ble_gap_stats;
STATS_NAME_START(ble_gap_stats)
    STATS_NAME(ble_gap_stats, wl_set)
    STATS_NAME(ble_gap_stats, wl_set_fail)
    STATS_NAME(ble_gap_stats, adv_stop)
    STATS_NAME(ble_gap_stats, adv_stop_fail)
    STATS_NAME(ble_gap_stats, adv_start)
    STATS_NAME(ble_gap_stats, adv_start_fail)
    STATS_NAME(ble_gap_stats, adv_set_fields)
    STATS_NAME(ble_gap_stats, adv_set_fields_fail)
    STATS_NAME(ble_gap_stats, adv_rsp_set_fields)
    STATS_NAME(ble_gap_stats, adv_rsp_set_fields_fail)
    STATS_NAME(ble_gap_stats, discover)
    STATS_NAME(ble_gap_stats, discover_fail)
    STATS_NAME(ble_gap_stats, initiate)
    STATS_NAME(ble_gap_stats, initiate_fail)
    STATS_NAME(ble_gap_stats, terminate)
    STATS_NAME(ble_gap_stats, terminate_fail)
    STATS_NAME(ble_gap_stats, cancel)
    STATS_NAME(ble_gap_stats, cancel_fail)
    STATS_NAME(ble_gap_stats, update)
    STATS_NAME(ble_gap_stats, update_fail)
    STATS_NAME(ble_gap_stats, connect_mst)
    STATS_NAME(ble_gap_stats, connect_slv)
    STATS_NAME(ble_gap_stats, disconnect)
    STATS_NAME(ble_gap_stats, rx_disconnect)
    STATS_NAME(ble_gap_stats, rx_update_complete)
    STATS_NAME(ble_gap_stats, rx_adv_report)
    STATS_NAME(ble_gap_stats, rx_conn_complete)
    STATS_NAME(ble_gap_stats, discover_cancel)
    STATS_NAME(ble_gap_stats, discover_cancel_fail)
    STATS_NAME(ble_gap_stats, security_initiate)
    STATS_NAME(ble_gap_stats, security_initiate_fail)
STATS_NAME_END(ble_gap_stats)

/*****************************************************************************
 * $log                                                                      *
 *****************************************************************************/

static void
ble_gap_log_duration(int32_t duration_ms)
{
    if (duration_ms == BLE_HS_FOREVER) {
        BLE_HS_LOG(INFO, "duration=forever");
    } else {
        BLE_HS_LOG(INFO, "duration=%dms", duration_ms);
    }
}

static void
ble_gap_log_conn(uint8_t own_addr_type,
                 uint8_t peer_addr_type, const uint8_t *peer_addr,
                 const struct ble_gap_conn_params *params)
{
    BLE_HS_LOG(INFO, "peer_addr_type=%d peer_addr=", peer_addr_type);
    if (peer_addr == NULL) {
        BLE_HS_LOG(INFO, "N/A");
    } else {
        BLE_HS_LOG_ADDR(INFO, peer_addr);
    }

    BLE_HS_LOG(INFO, " scan_itvl=%d scan_window=%d itvl_min=%d itvl_max=%d "
                     "latency=%d supervision_timeout=%d min_ce_len=%d "
                     "max_ce_len=%d own_addr_type=%d",
               params->scan_itvl, params->scan_window, params->itvl_min,
               params->itvl_max, params->latency, params->supervision_timeout,
               params->min_ce_len, params->max_ce_len, own_addr_type);
}

static void
ble_gap_log_disc(uint8_t own_addr_type, int32_t duration_ms,
                 const struct ble_gap_disc_params *disc_params)
{
    BLE_HS_LOG(INFO, "own_addr_type=%d filter_policy=%d passive=%d limited=%d "
                     "filter_duplicates=%d ",
               own_addr_type, disc_params->filter_policy, disc_params->passive,
               disc_params->limited, disc_params->filter_duplicates);
    ble_gap_log_duration(duration_ms);
}

static void
ble_gap_log_update(uint16_t conn_handle,
                   const struct ble_gap_upd_params *params)
{
    BLE_HS_LOG(INFO, "connection parameter update; "
                     "conn_handle=%d itvl_min=%d itvl_max=%d latency=%d "
                     "supervision_timeout=%d min_ce_len=%d max_ce_len=%d",
               conn_handle, params->itvl_min, params->itvl_max,
               params->latency, params->supervision_timeout,
               params->min_ce_len, params->max_ce_len);
}

static void
ble_gap_log_wl(const struct ble_gap_white_entry *white_list,
               uint8_t white_list_count)
{
    const struct ble_gap_white_entry *entry;
    int i;

    BLE_HS_LOG(INFO, "count=%d ", white_list_count);

    for (i = 0; i < white_list_count; i++) {
        entry = white_list + i;

        BLE_HS_LOG(INFO, "entry-%d={addr_type=%d addr=", i, entry->addr_type);
        BLE_HS_LOG_ADDR(INFO, entry->addr);
        BLE_HS_LOG(INFO, "} ");
    }
}

static void
ble_gap_log_adv(uint8_t own_addr_type, uint8_t peer_addr_type,
                const uint8_t *peer_addr,
                const struct ble_gap_adv_params *adv_params)
{
    BLE_HS_LOG(INFO, "disc_mode=%d peer_addr_type=%d peer_addr=",
               ble_gap_slave.disc_mode, peer_addr_type);
    if(peer_addr) {
        BLE_HS_LOG_ADDR(INFO, peer_addr);
    } else {
        BLE_HS_LOG(INFO, "none");
    }
    BLE_HS_LOG(INFO, " adv_channel_map=%d own_addr_type=%d "
                     "adv_filter_policy=%d adv_itvl_min=%d adv_itvl_max=%d "
                     "adv_data_len=%d",
               adv_params->channel_map,
               own_addr_type,
               adv_params->filter_policy,
               adv_params->itvl_min,
               adv_params->itvl_max,
               ble_gap_slave.adv_data_len);
}

/*****************************************************************************
 * $snapshot                                                                 *
 *****************************************************************************/

static void
ble_gap_fill_conn_desc(struct ble_hs_conn *conn,
                       struct ble_gap_conn_desc *desc)
{
    struct ble_hs_conn_addrs addrs;

    ble_hs_conn_addrs(conn, &addrs);

    desc->our_ota_addr_type = addrs.our_ota_addr_type;
    memcpy(desc->our_ota_addr, addrs.our_ota_addr, 6);
    desc->our_id_addr_type = addrs.our_id_addr_type;
    memcpy(desc->our_id_addr, addrs.our_id_addr, 6);
    desc->peer_ota_addr_type = addrs.peer_ota_addr_type;
    memcpy(desc->peer_ota_addr, addrs.peer_ota_addr, 6);
    desc->peer_id_addr_type = addrs.peer_id_addr_type;
    memcpy(desc->peer_id_addr, addrs.peer_id_addr, 6);

    desc->conn_handle = conn->bhc_handle;
    desc->conn_itvl = conn->bhc_itvl;
    desc->conn_latency = conn->bhc_latency;
    desc->supervision_timeout = conn->bhc_supervision_timeout;
    desc->master_clock_accuracy = conn->bhc_master_clock_accuracy;
    desc->sec_state = conn->bhc_sec_state;

    if (conn->bhc_flags & BLE_HS_CONN_F_MASTER) {
        desc->role = BLE_GAP_ROLE_MASTER;
    } else {
        desc->role = BLE_GAP_ROLE_SLAVE;
    }
}

static void
ble_gap_conn_to_snapshot(struct ble_hs_conn *conn,
                         struct ble_gap_snapshot *snap)
{
    ble_gap_fill_conn_desc(conn, snap->desc);
    snap->cb = conn->bhc_cb;
    snap->cb_arg = conn->bhc_cb_arg;
}

static int
ble_gap_find_snapshot(uint16_t handle, struct ble_gap_snapshot *snap)
{
    struct ble_hs_conn *conn;

    ble_hs_lock();

    conn = ble_hs_conn_find(handle);
    if (conn != NULL) {
        ble_gap_conn_to_snapshot(conn, snap);
    }

    ble_hs_unlock();

    if (conn == NULL) {
        return BLE_HS_ENOTCONN;
    } else {
        return 0;
    }
}

/**
 * Searches for a connection with the specified handle.  If a matching
 * connection is found, the supplied connection descriptor is filled
 * correspondingly.
 *
 * @param handle                The connection handle to search for.
 * @param out_desc              On success, this is populated with information
 *                                  relating to the matching connection.  Pass
 *                                  NULL if you don't need this information.
 *
 * @return                      0 on success; BLE_HS_ENOTCONN if no matching
 *                                  connection was found.
 */
int
ble_gap_conn_find(uint16_t handle, struct ble_gap_conn_desc *out_desc)
{
    struct ble_hs_conn *conn;

    ble_hs_lock();

    conn = ble_hs_conn_find(handle);
    if (conn != NULL && out_desc != NULL) {
        ble_gap_fill_conn_desc(conn, out_desc);
    }

    ble_hs_unlock();

    if (conn == NULL) {
        return BLE_HS_ENOTCONN;
    } else {
        return 0;
    }
}

static int
ble_gap_extract_conn_cb(uint16_t conn_handle,
                        ble_gap_event_fn **out_cb, void **out_cb_arg)
{
    const struct ble_hs_conn *conn;

    BLE_HS_DBG_ASSERT(conn_handle != 0);

    ble_hs_lock();

    conn = ble_hs_conn_find(conn_handle);
    if (conn != NULL) {
        *out_cb = conn->bhc_cb;
        *out_cb_arg = conn->bhc_cb_arg;
    } else {
        *out_cb = NULL;
        *out_cb_arg = NULL;
    }

    ble_hs_unlock();

    if (conn == NULL) {
        return BLE_HS_ENOTCONN;
    } else {
        return 0;
    }
}

/*****************************************************************************
 * $misc                                                                     *
 *****************************************************************************/

static int
ble_gap_call_event_cb(struct ble_gap_event *event,
                      ble_gap_event_fn *cb, void *cb_arg)
{
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());

    if (cb != NULL) {
        rc = cb(event, cb_arg);
    } else {
        if (event->type == BLE_GAP_EVENT_CONN_UPDATE_REQ) {
            /* Just copy peer parameters back into the reply. */
            *event->conn_update_req.self_params =
                *event->conn_update_req.peer_params;
        }
        rc = 0;
    }

    return rc;
}


static int
ble_gap_call_conn_event_cb(struct ble_gap_event *event, uint16_t conn_handle)
{
    ble_gap_event_fn *cb;
    void *cb_arg;
    int rc;

    rc = ble_gap_extract_conn_cb(conn_handle, &cb, &cb_arg);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gap_call_event_cb(event, cb, cb_arg);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
ble_gap_master_reset_state(void)
{
    ble_gap_master.op = BLE_GAP_OP_NULL;
    ble_gap_master.exp_set = 0;
    ble_gap_master.conn.cancel = 0;
}

static void
ble_gap_slave_reset_state(void)
{
    ble_gap_slave.op = BLE_GAP_OP_NULL;
    ble_gap_slave.exp_set = 0;
}

static void
ble_gap_master_extract_state(struct ble_gap_master_state *out_state,
                             int reset_state)
{
    ble_hs_lock();

    *out_state = ble_gap_master;

    if (reset_state) {
        ble_gap_master_reset_state();
    }

    ble_hs_unlock();
}

static void
ble_gap_slave_extract_cb(ble_gap_event_fn **out_cb, void **out_cb_arg)
{
    ble_hs_lock();

    *out_cb = ble_gap_slave.cb;
    *out_cb_arg = ble_gap_slave.cb_arg;
    ble_gap_slave_reset_state();

    ble_hs_unlock();
}

static void
ble_gap_adv_finished(void)
{
    struct ble_gap_event event;
    ble_gap_event_fn *cb;
    void *cb_arg;

    ble_gap_slave_extract_cb(&cb, &cb_arg);
    if (cb != NULL) {
        memset(&event, 0, sizeof event);
        event.type = BLE_GAP_EVENT_ADV_COMPLETE;

        cb(&event, cb_arg);
    }
}

static int
ble_gap_master_connect_failure(int status)
{
    struct ble_gap_master_state state;
    struct ble_gap_event event;
    int rc;

    ble_gap_master_extract_state(&state, 1);
    if (state.cb != NULL) {
        memset(&event, 0, sizeof event);
        event.type = BLE_GAP_EVENT_CONNECT;
        event.connect.status = status;

        rc = state.cb(&event, state.cb_arg);
    } else {
        rc = 0;
    }

    return rc;
}

static void
ble_gap_master_connect_cancelled(void)
{
    struct ble_gap_master_state state;
    struct ble_gap_event event;

    ble_gap_master_extract_state(&state, 1);
    if (state.cb != NULL) {
        /* The GAP event type depends on whether 1) the application manually
         * cancelled the connect procedure or 2) the connect procedure timed
         * out.
         */
        memset(&event, 0, sizeof event);
        if (state.conn.cancel) {
            event.type = BLE_GAP_EVENT_CONN_CANCEL;
        } else {
            event.type = BLE_GAP_EVENT_CONNECT;
            event.connect.status = BLE_HS_ETIMEOUT;
            event.connect.conn_handle = BLE_HS_CONN_HANDLE_NONE;

        }
        state.cb(&event, state.cb_arg);
    }
}

static void
ble_gap_disc_report(struct ble_gap_disc_desc *desc)
{
    struct ble_gap_master_state state;
    struct ble_gap_event event;

    ble_gap_master_extract_state(&state, 0);

    if (state.cb != NULL) {
        memset(&event, 0, sizeof event);
        event.type = BLE_GAP_EVENT_DISC;
        event.disc = *desc;

        state.cb(&event, state.cb_arg);
    }
}

static void
ble_gap_disc_complete(void)
{
    struct ble_gap_master_state state;
    struct ble_gap_event event;

    ble_gap_master_extract_state(&state, 1);

    if (state.cb != NULL) {
        memset(&event, 0, sizeof event);
        event.type = BLE_GAP_EVENT_DISC_COMPLETE;

        ble_gap_call_event_cb(&event, state.cb, state.cb_arg);
    }
}

static void
ble_gap_update_notify(uint16_t conn_handle, int status)
{
    struct ble_gap_event event;

    memset(&event, 0, sizeof event);
    event.type = BLE_GAP_EVENT_CONN_UPDATE;
    event.conn_update.conn_handle = conn_handle;
    event.conn_update.status = status;

    ble_gap_call_conn_event_cb(&event, conn_handle);
}

static uint32_t
ble_gap_master_ticks_until_exp(void)
{
    int32_t ticks;

    if (ble_gap_master.op == BLE_GAP_OP_NULL || !ble_gap_master.exp_set) {
        /* Timer not set; infinity ticks until next event. */
        return BLE_HS_FOREVER;
    }

    ticks = ble_gap_master.exp_os_ticks - os_time_get();
    if (ticks > 0) {
        /* Timer not expired yet. */
        return ticks;
    }

    /* Timer just expired. */
    return 0;
}

static uint32_t
ble_gap_slave_ticks_until_exp(void)
{
    int32_t ticks;

    if (ble_gap_slave.op == BLE_GAP_OP_NULL || !ble_gap_slave.exp_set) {
        /* Timer not set; infinity ticks until next event. */
        return BLE_HS_FOREVER;
    }

    ticks = ble_gap_slave.exp_os_ticks - os_time_get();
    if (ticks > 0) {
        /* Timer not expired yet. */
        return ticks;
    }

    /* Timer just expired. */
    return 0;
}

static void
ble_gap_heartbeat_sched(void)
{
    int32_t mst_ticks;
    int32_t slv_ticks;
    int32_t ticks;

    mst_ticks = ble_gap_master_ticks_until_exp();
    slv_ticks = ble_gap_slave_ticks_until_exp();
    ticks = min(mst_ticks, slv_ticks);

    ble_hs_heartbeat_sched(ticks);
}

static void
ble_gap_master_set_timer(uint32_t ticks_from_now)
{
    ble_gap_master.exp_os_ticks = os_time_get() + ticks_from_now;
    ble_gap_master.exp_set = 1;

    ble_gap_heartbeat_sched();
}

static void
ble_gap_slave_set_timer(uint32_t ticks_from_now)
{
    ble_gap_slave.exp_os_ticks = os_time_get() + ticks_from_now;
    ble_gap_slave.exp_set = 1;

    ble_gap_heartbeat_sched();
}

/**
 * Called when an error is encountered while the master-connection-fsm is
 * active.
 */
static void
ble_gap_master_failed(int status)
{
    switch (ble_gap_master.op) {
    case BLE_GAP_OP_M_CONN:
        STATS_INC(ble_gap_stats, initiate_fail);
        ble_gap_master_connect_failure(status);
        break;

    default:
        BLE_HS_DBG_ASSERT(0);
        break;
    }
}

static void
ble_gap_update_failed(uint16_t conn_handle, int status)
{
    STATS_INC(ble_gap_stats, update_fail);
    ble_hs_atomic_conn_set_flags(conn_handle, BLE_HS_CONN_F_UPDATE, 0);
    ble_gap_update_notify(conn_handle, status);
}

void
ble_gap_conn_broken(uint16_t conn_handle, int reason)
{
    struct ble_gap_snapshot snap;
    struct ble_gap_event event;
    int rc;

    memset(&event, 0, sizeof event);
    snap.desc = &event.disconnect.conn;

    rc = ble_gap_find_snapshot(conn_handle, &snap);
    if (rc != 0) {
        /* No longer connected. */
        return;
    }

    /* Indicate the connection termination to each module.  The order matters
     * here: gatts must come before gattc to ensure the application does not
     * get informed of spurious notify-tx events.
     */
    ble_sm_connection_broken(conn_handle);
    ble_gatts_connection_broken(conn_handle);
    ble_gattc_connection_broken(conn_handle);

    ble_hs_atomic_conn_delete(conn_handle);

    event.type = BLE_GAP_EVENT_DISCONNECT;
    event.disconnect.reason = reason;
    ble_gap_call_event_cb(&event, snap.cb, snap.cb_arg);

    STATS_INC(ble_gap_stats, disconnect);
}

void
ble_gap_rx_disconn_complete(struct hci_disconn_complete *evt)
{
#if !NIMBLE_OPT(CONNECT)
    return;
#endif

    struct ble_gap_event event;

    STATS_INC(ble_gap_stats, rx_disconnect);

    if (evt->status == 0) {
        ble_gap_conn_broken(evt->connection_handle,
                            BLE_HS_HCI_ERR(evt->reason));
    } else {
        memset(&event, 0, sizeof event);
        event.type = BLE_GAP_EVENT_TERM_FAILURE;
        event.term_failure.conn_handle = evt->connection_handle;
        event.term_failure.status = BLE_HS_HCI_ERR(evt->status);
        ble_gap_call_conn_event_cb(&event, evt->connection_handle);
    }
}

void
ble_gap_rx_update_complete(struct hci_le_conn_upd_complete *evt)
{
#if !NIMBLE_OPT(CONNECT)
    return;
#endif

    struct ble_gap_event event;
    struct ble_hs_conn *conn;

    STATS_INC(ble_gap_stats, rx_update_complete);

    memset(&event, 0, sizeof event);

    ble_hs_lock();

    conn = ble_hs_conn_find(evt->connection_handle);
    if (conn != NULL) {
        if (evt->status == 0) {
            conn->bhc_itvl = evt->conn_itvl;
            conn->bhc_latency = evt->conn_latency;
            conn->bhc_supervision_timeout = evt->supervision_timeout;
        }
    }

    conn->bhc_flags &= ~BLE_HS_CONN_F_UPDATE;

    ble_hs_unlock();

    event.type = BLE_GAP_EVENT_CONN_UPDATE;
    event.conn_update.conn_handle = evt->connection_handle;
    event.conn_update.status = BLE_HS_HCI_ERR(evt->status);
    ble_gap_call_conn_event_cb(&event, evt->connection_handle);
}

/**
 * Tells you if there is an active central GAP procedure (connect or discover).
 */
int
ble_gap_master_in_progress(void)
{
    return ble_gap_master.op != BLE_GAP_OP_NULL;
}

/**
 * Attempts to complete the master connection process in response to a
 * "connection complete" event from the controller.  If the master connection
 * FSM is in a state that can accept this event, and the peer device address is
 * valid, the master FSM is reset and success is returned.
 *
 * @param addr_type             The address type of the peer; one of the
 *                                  following values:
 *                                  o    BLE_ADDR_TYPE_PUBLIC
 *                                  o    BLE_ADDR_TYPE_RANDOM
 * @param addr                  The six-byte address of the connection peer.
 *
 * @return                      0 if the connection complete event was
 *                                  accepted;
 *                              BLE_HS_ENOENT if the event does not apply.
 */
static int
ble_gap_accept_master_conn(uint8_t addr_type, uint8_t *addr)
{
    int rc;

    switch (ble_gap_master.op) {
    case BLE_GAP_OP_NULL:
    case BLE_GAP_OP_M_DISC:
        rc = BLE_HS_ENOENT;
        break;

    case BLE_GAP_OP_M_CONN:
        rc = 0;
        break;

    default:
        BLE_HS_DBG_ASSERT(0);
        rc = BLE_HS_ENOENT;
        break;
    }

    if (rc == 0) {
        STATS_INC(ble_gap_stats, connect_mst);
    }

    return rc;
}

/**
 * Attempts to complete the slave connection process in response to a
 * "connection complete" event from the controller.  If the slave connection
 * FSM is in a state that can accept this event, and the peer device address is
 * valid, the master FSM is reset and success is returned.
 *
 * @param addr_type             The address type of the peer; one of the
 *                                  following values:
 *                                  o    BLE_ADDR_TYPE_PUBLIC
 *                                  o    BLE_ADDR_TYPE_RANDOM
 * @param addr                  The six-byte address of the connection peer.
 *
 * @return                      0 if the connection complete event was
 *                                  accepted;
 *                              BLE_HS_ENOENT if the event does not apply.
 */
static int
ble_gap_accept_slave_conn(uint8_t addr_type, uint8_t *addr)
{
    int rc;

    if (!ble_gap_adv_active()) {
        rc = BLE_HS_ENOENT;
    } else {
        switch (ble_gap_slave.conn_mode) {
        case BLE_GAP_CONN_MODE_NON:
            rc = BLE_HS_ENOENT;
            break;

        case BLE_GAP_CONN_MODE_UND:
            rc = 0;
            break;

        case BLE_GAP_CONN_MODE_DIR:
            rc = 0;
            break;

        default:
            BLE_HS_DBG_ASSERT(0);
            rc = BLE_HS_ENOENT;
            break;
        }
    }

    if (rc == 0) {
        STATS_INC(ble_gap_stats, connect_slv);
    }

    return rc;
}

void
ble_gap_rx_adv_report(struct ble_gap_disc_desc *desc)
{
#if !NIMBLE_OPT(ROLE_OBSERVER)
    return;
#endif

    struct ble_hs_adv_fields fields;
    int rc;

    STATS_INC(ble_gap_stats, rx_adv_report);

    if (ble_gap_master.op != BLE_GAP_OP_M_DISC) {
        return;
    }

    rc = ble_hs_adv_parse_fields(&fields, desc->data, desc->length_data);
    if (rc != 0) {
        /* XXX: Increment stat. */
        return;
    }

    /* If a limited discovery procedure is active, discard non-limited
     * advertisements.
     */
    if (ble_gap_master.disc.limited &&
        !(fields.flags & BLE_HS_ADV_F_DISC_LTD)) {

        return;
    }

    desc->fields = &fields;
    ble_gap_disc_report(desc);
}

/**
 * Processes an incoming connection-complete HCI event.
 */
int
ble_gap_rx_conn_complete(struct hci_le_conn_complete *evt)
{
#if !NIMBLE_OPT(CONNECT)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gap_event event;
    struct ble_hs_conn *conn;
    int rc;

    STATS_INC(ble_gap_stats, rx_conn_complete);

    /* Apply the event to the existing connection if it exists. */
    if (evt->status != BLE_ERR_UNK_CONN_ID &&
        ble_hs_atomic_conn_flags(evt->connection_handle, NULL) == 0) {

        /* XXX: Does this ever happen? */

        if (evt->status != 0) {
            ble_gap_conn_broken(evt->connection_handle,
                                BLE_HS_HCI_ERR(evt->status));
        }
        return 0;
    }

    /* This event refers to a new connection. */

    if (evt->status != BLE_ERR_SUCCESS) {
        /* Determine the role from the status code. */
        switch (evt->status) {
        case BLE_ERR_DIR_ADV_TMO:
            if (ble_gap_adv_active()) {
                ble_gap_adv_finished();
            }
            break;

        default:
            if (ble_gap_master_in_progress()) {
                if (evt->status == BLE_ERR_UNK_CONN_ID) {
                    /* Connect procedure successfully cancelled. */
                    ble_gap_master_connect_cancelled();
                } else {
                    ble_gap_master_failed(BLE_HS_HCI_ERR(evt->status));
                }
            }
            break;
        }

        return 0;
    }

    switch (evt->role) {
    case BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER:
        rc = ble_gap_accept_master_conn(evt->peer_addr_type, evt->peer_addr);
        if (rc != 0) {
            return rc;
        }
        break;

    case BLE_HCI_LE_CONN_COMPLETE_ROLE_SLAVE:
        rc = ble_gap_accept_slave_conn(evt->peer_addr_type, evt->peer_addr);
        if (rc != 0) {
            return rc;
        }
        break;

    default:
        BLE_HS_DBG_ASSERT(0);
        break;
    }

    /* We verified that there is a free connection when the procedure began. */
    conn = ble_hs_conn_alloc();
    BLE_HS_DBG_ASSERT(conn != NULL);

    conn->bhc_handle = evt->connection_handle;
    memcpy(conn->bhc_peer_addr, evt->peer_addr, sizeof conn->bhc_peer_addr);
    conn->bhc_peer_addr_type = evt->peer_addr_type;
    memcpy(conn->bhc_our_rpa_addr, evt->local_rpa,
           sizeof conn->bhc_our_rpa_addr);
    memcpy(conn->bhc_peer_rpa_addr, evt->peer_rpa,
           sizeof conn->bhc_peer_rpa_addr);
    conn->bhc_itvl = evt->conn_itvl;
    conn->bhc_latency = evt->conn_latency;
    conn->bhc_supervision_timeout = evt->supervision_timeout;
    conn->bhc_master_clock_accuracy = evt->master_clk_acc;
    if (evt->role == BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER) {
        conn->bhc_cb = ble_gap_master.cb;
        conn->bhc_cb_arg = ble_gap_master.cb_arg;
        conn->bhc_flags |= BLE_HS_CONN_F_MASTER;
        conn->bhc_our_addr_type = ble_gap_master.conn.our_addr_type;
        ble_gap_master_reset_state();
    } else {
        conn->bhc_cb = ble_gap_slave.cb;
        conn->bhc_cb_arg = ble_gap_slave.cb_arg;
        conn->bhc_our_addr_type = ble_gap_slave.our_addr_type;
        ble_gap_slave_reset_state();
    }

    memcpy(conn->bhc_our_rpa_addr, evt->local_rpa, 6);
    memcpy(conn->bhc_peer_rpa_addr, evt->peer_rpa, 6);

    ble_hs_lock();

    memset(&event, 0, sizeof event);
    ble_hs_conn_insert(conn);

    ble_hs_unlock();

    event.type = BLE_GAP_EVENT_CONNECT;
    event.connect.conn_handle = evt->connection_handle;
    event.connect.status = 0;
    ble_gap_call_conn_event_cb(&event, evt->connection_handle);

    return 0;
}

int
ble_gap_rx_l2cap_update_req(uint16_t conn_handle,
                            struct ble_gap_upd_params *params)
{
    struct ble_gap_event event;
    int rc;

    memset(&event, 0, sizeof event);
    event.type = BLE_GAP_EVENT_L2CAP_UPDATE_REQ;
    event.conn_update_req.conn_handle = conn_handle;
    event.conn_update_req.peer_params = params;

    rc = ble_gap_call_conn_event_cb(&event, conn_handle);
    return rc;
}

static int32_t
ble_gap_master_heartbeat(void)
{
    uint32_t ticks_until_exp;
    int rc;

    ticks_until_exp = ble_gap_master_ticks_until_exp();
    if (ticks_until_exp != 0) {
        /* Timer not expired yet. */
        return ticks_until_exp;
    }

    /*** Timer expired; process event. */

    switch (ble_gap_master.op) {
    case BLE_GAP_OP_M_CONN:
        rc = ble_gap_conn_cancel_tx();
        if (rc != 0) {
            /* Failed to stop connecting; try again in 100 ms. */
            return BLE_GAP_CANCEL_RETRY_RATE;
        } else {
            /* Stop the timer now that the cancel command has been acked. */
            ble_gap_master.exp_set = 0;

            /* Timeout gets reported when we receive a connection complete
             * event indicating the connect procedure has been cancelled.
             */
            /* XXX: Set a timer to reset the controller if a connection
             * complete event isn't received within a reasonable interval.
             */
        }
        break;

    case BLE_GAP_OP_M_DISC:
        /* When a discovery procedure times out, it is not a failure. */
        rc = ble_gap_disc_enable_tx(0, 0);
        if (rc != 0) {
            /* Failed to stop discovery; try again in 100 ms. */
            return BLE_GAP_CANCEL_RETRY_RATE;
        }

        ble_gap_disc_complete();
        break;

    default:
        BLE_HS_DBG_ASSERT(0);
        break;
    }

    return BLE_HS_FOREVER;
}

static int32_t
ble_gap_slave_heartbeat(void)
{
    uint32_t ticks_until_exp;
    int rc;

    ticks_until_exp = ble_gap_slave_ticks_until_exp();
    if (ticks_until_exp != 0) {
        /* Timer not expired yet. */
        return ticks_until_exp;
    }

    /*** Timer expired; process event. */

    /* Stop advertising. */
    rc = ble_gap_adv_enable_tx(0);
    if (rc != 0) {
        /* Failed to stop advertising; try again in 100 ms. */
        return 100;
    }

    /* Clear the timer and cancel the current procedure. */
    ble_gap_slave_reset_state();

    /* Indicate to application that advertising has stopped. */
    ble_gap_adv_finished();

    return BLE_HS_FOREVER;
}

/**
 * Handles timed-out master procedures.
 *
 * Called by the heartbeat timer; executed at least once a second.
 *
 * @return                      The number of ticks until this function should
 *                                  be called again.
 */
int32_t
ble_gap_heartbeat(void)
{
    int32_t master_ticks;
    int32_t slave_ticks;

    master_ticks = ble_gap_master_heartbeat();
    slave_ticks = ble_gap_slave_heartbeat();

    return min(master_ticks, slave_ticks);
}

/*****************************************************************************
 * $white list                                                               *
 *****************************************************************************/

static int
ble_gap_wl_busy(void)
{
#if !NIMBLE_OPT(WHITELIST)
    return BLE_HS_ENOTSUP;
#endif

    /* Check if an auto or selective connection establishment procedure is in
     * progress.
     */
    return ble_gap_master.op == BLE_GAP_OP_M_CONN &&
           ble_gap_master.conn.using_wl;
}

static int
ble_gap_wl_tx_add(const struct ble_gap_white_entry *entry)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_CHG_WHITE_LIST_LEN];
    int rc;

    rc = host_hci_cmd_build_le_add_to_whitelist(entry->addr, entry->addr_type,
                                                buf, sizeof buf);
    if (rc != 0) {
        return rc;
    }

    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_gap_wl_tx_clear(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN];
    int rc;

    host_hci_cmd_build_le_clear_whitelist(buf, sizeof buf);
    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Overwrites the controller's white list with the specified contents.
 *
 * @param white_list            The entries to write to the white list.
 * @param white_list_count      The number of entries in the white list.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gap_wl_set(const struct ble_gap_white_entry *white_list,
               uint8_t white_list_count)
{
#if !NIMBLE_OPT(WHITELIST)
    return BLE_HS_ENOTSUP;
#endif

    int rc;
    int i;

    STATS_INC(ble_gap_stats, wl_set);

    ble_hs_lock();

    if (white_list_count == 0) {
        rc = BLE_HS_EINVAL;
        goto done;
    }

    for (i = 0; i < white_list_count; i++) {
        if (white_list[i].addr_type != BLE_ADDR_TYPE_PUBLIC &&
            white_list[i].addr_type != BLE_ADDR_TYPE_RANDOM) {

            rc = BLE_HS_EINVAL;
            goto done;
        }
    }

    if (ble_gap_wl_busy()) {
        rc = BLE_HS_EBUSY;
        goto done;
    }

    BLE_HS_LOG(INFO, "GAP procedure initiated: set whitelist; ");
    ble_gap_log_wl(white_list, white_list_count);
    BLE_HS_LOG(INFO, "\n");

    rc = ble_gap_wl_tx_clear();
    if (rc != 0) {
        goto done;
    }

    for (i = 0; i < white_list_count; i++) {
        rc = ble_gap_wl_tx_add(white_list + i);
        if (rc != 0) {
            goto done;
        }
    }

    rc = 0;

done:
    ble_hs_unlock();

    if (rc != 0) {
        STATS_INC(ble_gap_stats, wl_set_fail);
    }
    return rc;
}

/*****************************************************************************
 * $stop advertise                                                           *
 *****************************************************************************/

static int
ble_gap_adv_enable_tx(int enable)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_ADV_ENABLE_LEN];
    int rc;

    host_hci_cmd_build_le_set_adv_enable(!!enable, buf, sizeof buf);
    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Stops the currently-active advertising procedure.  A success return
 * code indicates that advertising has been fully aborted; a new advertising
 * procedure can be initiated immediately.
 *
 * @return                      0 on success;
 *                              BLE_HS_EALREADY if there is no active
 *                                  advertising procedure;
 *                              Other nonzero on error.
 */
int
ble_gap_adv_stop(void)
{
#if !NIMBLE_OPT(ADVERTISE)
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    STATS_INC(ble_gap_stats, adv_stop);

    ble_hs_lock();

    /* Do nothing if advertising is already disabled. */
    if (!ble_gap_adv_active()) {
        rc = BLE_HS_EALREADY;
        goto done;
    }

    BLE_HS_LOG(INFO, "GAP procedure initiated: stop advertising.\n");

    rc = ble_gap_adv_enable_tx(0);
    if (rc != 0) {
        goto done;
    }

    ble_gap_slave_reset_state();

    rc = 0;

done:
    ble_hs_unlock();

    if (rc != 0) {
        STATS_INC(ble_gap_stats, adv_set_fields_fail);
    }
    return rc;
}

/*****************************************************************************
 * $advertise                                                                *
 *****************************************************************************/

static int
ble_gap_adv_rsp_data_tx(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_SCAN_RSP_DATA_LEN];
    int rc;

    rc = host_hci_cmd_build_le_set_scan_rsp_data(ble_gap_slave.rsp_data,
                                                 ble_gap_slave.rsp_data_len,
                                                 buf, sizeof buf);
    if (rc != 0) {
        return rc;
    }

    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
ble_gap_adv_data_set_flags(void)
{
    uint8_t flags;
    int rc;

    /* Calculate the value of the flags field from the discoverable mode. */
    flags = 0;
    switch (ble_gap_slave.disc_mode) {
    case BLE_GAP_DISC_MODE_NON:
        break;

    case BLE_GAP_DISC_MODE_LTD:
        flags |= BLE_HS_ADV_F_DISC_LTD;
        break;

    case BLE_GAP_DISC_MODE_GEN:
        flags |= BLE_HS_ADV_F_DISC_GEN;
        break;

    default:
        BLE_HS_DBG_ASSERT(0);
        break;
    }

    flags |= BLE_HS_ADV_F_BREDR_UNSUP;

    if (flags != 0) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_FLAGS, 1, &flags,
                                 ble_gap_slave.adv_data,
                                 &ble_gap_slave.adv_data_len,
                                 BLE_HCI_MAX_ADV_DATA_LEN);
        BLE_HS_DBG_ASSERT_EVAL(rc == 0);
    }
}

static int
ble_gap_adv_data_tx(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_ADV_DATA_LEN];
    int rc;

    /* Calculate the flags AD field if requested by application.  Clear the
     * auto flag after encoding the flags so that we don't get repeated flags
     * fields on subsequent advertising procedures.
     */
    if (ble_gap_slave.adv_auto_flags) {
        ble_gap_adv_data_set_flags();
        ble_gap_slave.adv_auto_flags = 0;
    }

    rc = host_hci_cmd_build_le_set_adv_data(ble_gap_slave.adv_data,
                                            ble_gap_slave.adv_data_len,
                                            buf, sizeof buf);
    if (rc != 0) {
        return rc;
    }

    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_gap_adv_type(const struct ble_gap_adv_params *adv_params)
{
    switch (adv_params->conn_mode) {
    case BLE_GAP_CONN_MODE_NON:
        if (adv_params->disc_mode == BLE_GAP_DISC_MODE_NON) {
            return BLE_HCI_ADV_TYPE_ADV_NONCONN_IND;
        } else {
            return BLE_HCI_ADV_TYPE_ADV_SCAN_IND;
        }

    case BLE_GAP_CONN_MODE_UND:
        return BLE_HCI_ADV_TYPE_ADV_IND;

    case BLE_GAP_CONN_MODE_DIR:
        if (adv_params->high_duty_cycle) {
            return BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD;
        } else {
            return BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_LD;
        }

    default:
        BLE_HS_DBG_ASSERT(0);
        return BLE_HCI_ADV_TYPE_ADV_IND;
    }
}

static void
ble_gap_adv_dflt_itvls(uint8_t conn_mode,
                       uint16_t *out_itvl_min, uint16_t *out_itvl_max)
{
    switch (conn_mode) {
    case BLE_GAP_CONN_MODE_NON:
        *out_itvl_min = BLE_GAP_ADV_FAST_INTERVAL2_MIN;
        *out_itvl_max = BLE_GAP_ADV_FAST_INTERVAL2_MAX;
        break;

    case BLE_GAP_CONN_MODE_UND:
        *out_itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
        *out_itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;
        break;

    case BLE_GAP_CONN_MODE_DIR:
        *out_itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
        *out_itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;
        break;

    default:
        BLE_HS_DBG_ASSERT(0);
        break;
    }
}

static int
ble_gap_adv_params_tx(uint8_t own_addr_type,
                      uint8_t peer_addr_type, const uint8_t *peer_addr,
                      const struct ble_gap_adv_params *adv_params)

{
    struct hci_adv_params hci_adv_params;
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_ADV_PARAM_LEN];
    int rc;

    if (peer_addr == NULL) {
        peer_addr = ble_hs_misc_null_addr;
    }

    hci_adv_params.own_addr_type = own_addr_type;
    hci_adv_params.peer_addr_type = peer_addr_type;
    memcpy(hci_adv_params.peer_addr, peer_addr,
           sizeof hci_adv_params.peer_addr);

    /* Fill optional fields if application did not specify them. */
    if (adv_params->itvl_min == 0 && adv_params->itvl_max == 0) {
        ble_gap_adv_dflt_itvls(adv_params->conn_mode,
                               &hci_adv_params.adv_itvl_min,
                               &hci_adv_params.adv_itvl_max);
    } else {
        hci_adv_params.adv_itvl_min = adv_params->itvl_min;
        hci_adv_params.adv_itvl_max = adv_params->itvl_max;
    }
    if (adv_params->channel_map == 0) {
        hci_adv_params.adv_channel_map = BLE_GAP_ADV_DFLT_CHANNEL_MAP;
    } else {
        hci_adv_params.adv_channel_map = adv_params->channel_map;
    }

    /* Zero is the default value for filter policy and high duty cycle */
    hci_adv_params.adv_filter_policy = adv_params->filter_policy;

    hci_adv_params.adv_type = ble_gap_adv_type(adv_params);
    rc = host_hci_cmd_build_le_set_adv_params(&hci_adv_params,
                                              buf, sizeof buf);
    if (rc != 0) {
        return BLE_HS_EINVAL;
    }

    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_gap_adv_validate(uint8_t own_addr_type, uint8_t peer_addr_type,
                     const uint8_t *peer_addr,
                     const struct ble_gap_adv_params *adv_params)
{
    if (adv_params == NULL) {
        return BLE_HS_EINVAL;
    }

    if (own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) {
        return BLE_HS_EINVAL;
    }

    if (adv_params->disc_mode >= BLE_GAP_DISC_MODE_MAX) {
        return BLE_HS_EINVAL;
    }

    if (ble_gap_slave.op != BLE_GAP_OP_NULL) {
        return BLE_HS_EALREADY;
    }

    switch (adv_params->conn_mode) {
    case BLE_GAP_CONN_MODE_NON:
        /* High duty cycle only allowed for directed advertising. */
        if (adv_params->high_duty_cycle) {
            return BLE_HS_EINVAL;
        }
        break;

    case BLE_GAP_CONN_MODE_UND:
        /* High duty cycle only allowed for directed advertising. */
        if (adv_params->high_duty_cycle) {
            return BLE_HS_EINVAL;
        }

        /* Don't allow connectable advertising if we won't be able to allocate
         * a new connection.
         */
        if (!ble_hs_conn_can_alloc()) {
            return BLE_HS_ENOMEM;
        }
        break;

    case BLE_GAP_CONN_MODE_DIR:
        if (peer_addr_type != BLE_ADDR_TYPE_PUBLIC &&
            peer_addr_type != BLE_ADDR_TYPE_RANDOM &&
            peer_addr_type != BLE_ADDR_TYPE_RPA_PUB_DEFAULT &&
            peer_addr_type != BLE_ADDR_TYPE_RPA_RND_DEFAULT) {

            return BLE_HS_EINVAL;
        }
        if (peer_addr == NULL) {
            return BLE_HS_EINVAL;
        }

        /* Don't allow connectable advertising if we won't be able to allocate
         * a new connection.
         */
        if (!ble_hs_conn_can_alloc()) {
            return BLE_HS_ENOMEM;
        }
        break;

    default:
        return BLE_HS_EINVAL;
    }

    return 0;
}

/**
 * Initiates advertising.
 *
 * @param own_addr_type         The type of address the stack should use for
 *                                  itself.  Valid values are:
 *                                      o BLE_ADDR_TYPE_PUBLIC
 *                                      o BLE_ADDR_TYPE_RANDOM
 *                                      o BLE_ADDR_TYPE_RPA_PUB_DEFAULT
 *                                      o BLE_ADDR_TYPE_RPA_RND_DEFAULT
 * @param peer_addr_type        Address type of the peer's identity address.
 *                                  Valid values are:
 *                                      o BLE_ADDR_TYPE_PUBLIC
 *                                      o BLE_ADDR_TYPE_RANDOM
 *                                  This parameter is ignored unless directed
 *                                  advertising is being used.
 * @param peer_addr             The peer's six-byte identity address.
 *                                  This parameter is ignored unless directed
 *                                  advertising is being used.
 * @param duration_ms           The duration of the advertisement procedure.
 *                                  On expiration, the procedure ends and a
 *                                  BLE_GAP_EVENT_ADV_COMPLETE event is
 *                                  reported.  Units are milliseconds.  Specify
 *                                  BLE_HS_FOREVER for no expiration.
 * @param adv_params            Additional arguments specifying the particulars
 *                                  of the advertising procedure.
 * @param cb                    The callback to associate with this advertising
 *                                  procedure.  If advertising ends, the event
 *                                  is reported through this callback.  If
 *                                  advertising results in a connection, the
 *                                  connection inherits this callback as its
 *                                  event-reporting mechanism.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gap_adv_start(uint8_t own_addr_type, uint8_t peer_addr_type,
                  const uint8_t *peer_addr, int32_t duration_ms,
                  const struct ble_gap_adv_params *adv_params,
                  ble_gap_event_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT(ADVERTISE)
    return BLE_HS_ENOTSUP;
#endif

    uint32_t duration_ticks;
    int rc;

    STATS_INC(ble_gap_stats, adv_start);

    ble_hs_lock();

    rc = ble_gap_adv_validate(own_addr_type, peer_addr_type, peer_addr,
                              adv_params);
    if (rc != 0) {
        goto done;
    }

    if (duration_ms != BLE_HS_FOREVER) {
        rc = os_time_ms_to_ticks(duration_ms, &duration_ticks);
        if (rc != 0) {
            /* Duration too great. */
            rc = BLE_HS_EINVAL;
            goto done;
        }
    }

    rc = ble_hs_id_use_addr(own_addr_type);
    if (rc != 0) {
        return rc;
    }

    BLE_HS_LOG(INFO, "GAP procedure initiated: advertise; ");
    ble_gap_log_adv(own_addr_type, peer_addr_type, peer_addr, adv_params);
    BLE_HS_LOG(INFO, "\n");

    ble_gap_slave.cb = cb;
    ble_gap_slave.cb_arg = cb_arg;
    ble_gap_slave.conn_mode = adv_params->conn_mode;
    ble_gap_slave.disc_mode = adv_params->disc_mode;
    ble_gap_slave.our_addr_type = own_addr_type;

    rc = ble_gap_adv_params_tx(own_addr_type, peer_addr_type, peer_addr,
                               adv_params);
    if (rc != 0) {
        goto done;
    }

    if (adv_params->conn_mode != BLE_GAP_CONN_MODE_DIR) {
        rc = ble_gap_adv_data_tx();
        if (rc != 0) {
            goto done;
        }

        rc = ble_gap_adv_rsp_data_tx();
        if (rc != 0) {
            goto done;
        }
    }

    rc = ble_gap_adv_enable_tx(1);
    if (rc != 0) {
        goto done;
    }

    if (duration_ms != BLE_HS_FOREVER) {
        ble_gap_slave_set_timer(duration_ticks);
    }

    ble_gap_slave.op = BLE_GAP_OP_S_ADV;
    rc = 0;

done:
    ble_hs_unlock();

    if (rc != 0) {
        STATS_INC(ble_gap_stats, adv_start_fail);
    }
    return rc;
}

/**
 * Configures the data to include in subsequent advertisements.
 *
 * @param adv_fields            Specifies the advertisement data.
 *
 * @return                      0 on success;
 *                              BLE_HS_EBUSY if advertising is in progress;
 *                              BLE_HS_EMSGSIZE if the specified data is too
 *                                  large to fit in an advertisement;
 *                              Other nonzero on failure.
 */
int
ble_gap_adv_set_fields(const struct ble_hs_adv_fields *adv_fields)
{
#if !NIMBLE_OPT(ADVERTISE)
    return BLE_HS_ENOTSUP;
#endif

    int max_sz;
    int rc;

    STATS_INC(ble_gap_stats, adv_set_fields);

    ble_hs_lock();

    /* Don't allow advertising fields to be set while advertising is active. */
    if (ble_gap_slave.op != BLE_GAP_OP_NULL) {
        rc = BLE_HS_EBUSY;
        goto done;
    }

    /* If application has requested the stack to calculate the flags field
     * automatically (flags == 0), there is less room for user data.
     */
    if (adv_fields->flags_is_present && adv_fields->flags == 0) {
        max_sz = BLE_GAP_ADV_DATA_LIMIT_FLAGS;
        ble_gap_slave.adv_auto_flags = 1;
    } else {
        max_sz = BLE_GAP_ADV_DATA_LIMIT_NO_FLAGS;
        ble_gap_slave.adv_auto_flags = 0;
    }

    rc = ble_hs_adv_set_fields(adv_fields, ble_gap_slave.adv_data,
                               &ble_gap_slave.adv_data_len, max_sz);
    if (rc != 0) {
        goto done;
    }

done:
    ble_hs_unlock();

    if (rc != 0) {
        STATS_INC(ble_gap_stats, adv_set_fields_fail);
    }
    return rc;
}

/**
 * Configures the data to include in subsequent scan responses.
 *
 * @param adv_fields            Specifies the scan response data.
 *
 * @return                      0 on success;
 *                              BLE_HS_EBUSY if advertising is in progress;
 *                              BLE_HS_EMSGSIZE if the specified data is too
 *                                  large to fit in an advertisement;
 *                              Other nonzero on failure.
 */
int
ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *rsp_fields)
{
#if !NIMBLE_OPT(ADVERTISE)
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    STATS_INC(ble_gap_stats, adv_rsp_set_fields);

    ble_hs_lock();

    /* Don't allow response fields to be set while advertising is active. */
    if (ble_gap_slave.op != BLE_GAP_OP_NULL) {
        rc = BLE_HS_EBUSY;
        goto done;
    }

    rc = ble_hs_adv_set_fields(rsp_fields, ble_gap_slave.rsp_data,
                               &ble_gap_slave.rsp_data_len,
                               BLE_HCI_MAX_ADV_DATA_LEN);
    if (rc != 0) {
        goto done;
    }

done:
    ble_hs_unlock();

    if (rc != 0) {
        STATS_INC(ble_gap_stats, adv_rsp_set_fields_fail);
    }
    return rc;
}

/**
 * Indicates whether an advertisement procedure is currently in progress.
 *
 * @return                      0: No advertisement procedure in progress;
 *                              1: Advertisement procedure in progress.
 */
int
ble_gap_adv_active(void)
{
    /* Assume read is atomic; mutex not necessary. */
    return ble_gap_slave.op == BLE_GAP_OP_S_ADV;
}

/*****************************************************************************
 * $discovery procedures                                                     *
 *****************************************************************************/

static int
ble_gap_disc_enable_tx(int enable, int filter_duplicates)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_SCAN_ENABLE_LEN];
    int rc;

    host_hci_cmd_build_le_set_scan_enable(!!enable, !!filter_duplicates,
                                          buf, sizeof buf);
    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_gap_disc_tx_params(uint8_t own_addr_type,
                       const struct ble_gap_disc_params *disc_params)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_SCAN_PARAM_LEN];
    uint8_t scan_type;
    int rc;

    if (disc_params->passive) {
        scan_type = BLE_HCI_SCAN_TYPE_PASSIVE;
    } else {
        scan_type = BLE_HCI_SCAN_TYPE_ACTIVE;
    }

    rc = host_hci_cmd_build_le_set_scan_params(scan_type,
                                               disc_params->itvl,
                                               disc_params->window,
                                               own_addr_type,
                                               disc_params->filter_policy,
                                               buf, sizeof buf);
    if (rc != 0) {
        return BLE_HS_EINVAL;
    }

    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Cancels the discovery procedure currently in progress.  A success return
 * code indicates that scanning has been fully aborted; a new discovery or
 * connect procedure can be initiated immediately.
 *
 * @return                      0 on success;
 *                              BLE_HS_EALREADY if there is no discovery
 *                                  procedure to cancel;
 *                              Other nonzero on unexpected error.
 */
int
ble_gap_disc_cancel(void)
{
    int rc;

    STATS_INC(ble_gap_stats, discover_cancel);

    ble_hs_lock();

    if (!ble_gap_disc_active()) {
        rc = BLE_HS_EALREADY;
        goto done;
    }

    rc = ble_gap_disc_enable_tx(0, 0);
    if (rc != 0) {
        goto done;
    }

    ble_gap_master_reset_state();

done:
    ble_hs_unlock();

    if (rc != 0) {
        STATS_INC(ble_gap_stats, discover_cancel_fail);
    }
    return rc;
}

static void
ble_gap_disc_fill_dflts(struct ble_gap_disc_params *disc_params)
{
   if (disc_params->itvl == 0) {
        if (disc_params->limited) {
            disc_params->itvl = BLE_GAP_LIM_DISC_SCAN_INT;
        } else {
            disc_params->itvl = BLE_GAP_SCAN_FAST_INTERVAL_MIN;
        }
    }

    if (disc_params->window == 0) {
        if (disc_params->limited) {
            disc_params->window = BLE_GAP_LIM_DISC_SCAN_WINDOW;
        } else {
            disc_params->window = BLE_GAP_SCAN_FAST_WINDOW;
        }
    }
}

static int
ble_gap_disc_validate(uint8_t own_addr_type,
                      const struct ble_gap_disc_params *disc_params)
{
    if (disc_params == NULL) {
        return BLE_HS_EINVAL;
    }

    if (own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) {
        return BLE_HS_EINVAL;
    }

    if (ble_gap_master.op != BLE_GAP_OP_NULL) {
        return BLE_HS_EALREADY;
    }

    return 0;
}

/**
 * Performs the Limited or General Discovery Procedures.
 *
 * @param own_addr_type         The type of address the stack should use for
 *                                  itself when sending scan requests.  Valid
 *                                  values are:
 *                                      o BLE_ADDR_TYPE_PUBLIC
 *                                      o BLE_ADDR_TYPE_RANDOM
 *                                      o BLE_ADDR_TYPE_RPA_PUB_DEFAULT
 *                                      o BLE_ADDR_TYPE_RPA_RND_DEFAULT
 *                                  This parameter is ignored unless active
 *                                  scanning is being used.
 * @param duration_ms           The duration of the discovery procedure.
 *                                  On expiration, the procedure ends and a
 *                                  BLE_GAP_EVENT_DISC_COMPLETE event is
 *                                  reported.  Units are milliseconds.  Specify
 *                                  BLE_HS_FOREVER for no expiration.
 * @param disc_params           Additional arguments specifying the particulars
 *                                  of the discovery procedure.
 * @param cb                    The callback to associate with this discovery
 *                                  procedure.  Advertising reports and
 *                                  discovery termination events are reported
 *                                  through this callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gap_disc(uint8_t own_addr_type, int32_t duration_ms,
             const struct ble_gap_disc_params *disc_params,
             ble_gap_event_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT(ROLE_OBSERVER)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gap_disc_params params;
    uint32_t duration_ticks;
    int rc;

    STATS_INC(ble_gap_stats, discover);

    ble_hs_lock();

    /* Make a copy of the parameter strcuture and fill unspecified values with
     * defaults.
     */
    params = *disc_params;
    ble_gap_disc_fill_dflts(&params);

    rc = ble_gap_disc_validate(own_addr_type, &params);
    if (rc != 0) {
        goto done;
    }

    if (duration_ms == 0) {
        duration_ms = BLE_GAP_DISC_DUR_DFLT;
    }

    if (duration_ms != BLE_HS_FOREVER) {
        rc = os_time_ms_to_ticks(duration_ms, &duration_ticks);
        if (rc != 0) {
            /* Duration too great. */
            rc = BLE_HS_EINVAL;
            goto done;
        }
    }

    if (!params.passive) {
        rc = ble_hs_id_use_addr(own_addr_type);
        if (rc != 0) {
            return rc;
        }
    }

    ble_gap_master.disc.limited = params.limited;
    ble_gap_master.cb = cb;
    ble_gap_master.cb_arg = cb_arg;

    BLE_HS_LOG(INFO, "GAP procedure initiated: discovery; ");
    ble_gap_log_disc(own_addr_type, duration_ms, &params);
    BLE_HS_LOG(INFO, "\n");

    rc = ble_gap_disc_tx_params(own_addr_type, &params);
    if (rc != 0) {
        goto done;
    }

    rc = ble_gap_disc_enable_tx(1, params.filter_duplicates);
    if (rc != 0) {
        goto done;
    }

    if (duration_ms != BLE_HS_FOREVER) {
        ble_gap_master_set_timer(duration_ticks);
    }

    ble_gap_master.op = BLE_GAP_OP_M_DISC;

    rc = 0;

done:
    ble_hs_unlock();

    if (rc != 0) {
        STATS_INC(ble_gap_stats, discover_fail);
    }
    return rc;
}

/**
 * Indicates whether a discovery procedure is currently in progress.
 *
 * @return                      0: No discovery procedure in progress;
 *                              1: Discovery procedure in progress.
 */
int
ble_gap_disc_active(void)
{
    /* Assume read is atomic; mutex not necessary. */
    return ble_gap_master.op == BLE_GAP_OP_M_DISC;
}

/*****************************************************************************
 * $connection establishment procedures                                      *
 *****************************************************************************/

static int
ble_gap_conn_create_tx(uint8_t own_addr_type,
                       uint8_t peer_addr_type, const uint8_t *peer_addr,
                       const struct ble_gap_conn_params *params)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_CREATE_CONN_LEN];
    struct hci_create_conn hcc;
    int rc;

    hcc.scan_itvl = params->scan_itvl;
    hcc.scan_window = params->scan_window;

    if (peer_addr_type == BLE_GAP_ADDR_TYPE_WL) {
        /* Application wants to connect to any device in the white list.  The
         * peer address type and peer address fields are ignored by the
         * controller; fill them with dummy values.
         */
        hcc.filter_policy = BLE_HCI_CONN_FILT_USE_WL;
        hcc.peer_addr_type = 0;
        memset(hcc.peer_addr, 0, sizeof hcc.peer_addr);
    } else {
        hcc.filter_policy = BLE_HCI_CONN_FILT_NO_WL;
        hcc.peer_addr_type = peer_addr_type;
        memcpy(hcc.peer_addr, peer_addr, sizeof hcc.peer_addr);
    }

    hcc.own_addr_type = own_addr_type;
    hcc.conn_itvl_min = params->itvl_min;
    hcc.conn_itvl_max = params->itvl_max;
    hcc.conn_latency = params->latency;
    hcc.supervision_timeout = params->supervision_timeout;
    hcc.min_ce_len = params->min_ce_len;
    hcc.max_ce_len = params->max_ce_len;

    rc = host_hci_cmd_build_le_create_connection(&hcc, buf, sizeof buf);
    if (rc != 0) {
        return BLE_HS_EUNKNOWN;
    }

    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Initiates a connect procedure.
 *
 * @param own_addr_type         The type of address the stack should use for
 *                                  itself during connection establishment.
 *                                      o BLE_ADDR_TYPE_PUBLIC
 *                                      o BLE_ADDR_TYPE_RANDOM
 *                                      o BLE_ADDR_TYPE_RPA_PUB_DEFAULT
 *                                      o BLE_ADDR_TYPE_RPA_RND_DEFAULT
 * @param peer_addr_type        The peer's address type.  One of:
 *                                      o BLE_HCI_CONN_PEER_ADDR_PUBLIC
 *                                      o BLE_HCI_CONN_PEER_ADDR_RANDOM
 *                                      o BLE_HCI_CONN_PEER_ADDR_PUBLIC_IDENT
 *                                      o BLE_HCI_CONN_PEER_ADDR_RANDOM_IDENT
 *                                      o BLE_GAP_ADDR_TYPE_WL
 * @param peer_addr             The identity address of the peer to connect to.
 *                                  This parameter is ignored when the white
 *                                  list is used.
 * @param duration_ms           The duration of the discovery procedure.
 *                                  On expiration, the procedure ends and a
 *                                  BLE_GAP_EVENT_DISC_COMPLETE event is
 *                                  reported.  Units are milliseconds.
 * @param conn_params           Additional arguments specifying the particulars
 *                                  of the connect procedure.  Specify null for
 *                                  default values.
 * @param cb                    The callback to associate with this connect
 *                                  procedure.  When the connect procedure
 *                                  completes, the result is reported through
 *                                  this callback.  If the connect procedure
 *                                  succeeds, the connection inherits this
 *                                  callback as its event-reporting mechanism.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gap_connect(uint8_t own_addr_type,
                uint8_t peer_addr_type, const uint8_t *peer_addr,
                int32_t duration_ms,
                const struct ble_gap_conn_params *conn_params,
                ble_gap_event_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT(ROLE_CENTRAL)
    return BLE_HS_ENOTSUP;
#endif

    uint32_t duration_ticks;
    int rc;

    STATS_INC(ble_gap_stats, initiate);

    ble_hs_lock();

    if (ble_gap_master.op != BLE_GAP_OP_NULL) {
        rc = BLE_HS_EALREADY;
        goto done;
    }

    if (!ble_hs_conn_can_alloc()) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    if (peer_addr_type != BLE_HCI_CONN_PEER_ADDR_PUBLIC &&
        peer_addr_type != BLE_HCI_CONN_PEER_ADDR_RANDOM &&
        peer_addr_type != BLE_HCI_CONN_PEER_ADDR_PUB_ID &&
        peer_addr_type != BLE_HCI_CONN_PEER_ADDR_RAND_ID &&
        peer_addr_type != BLE_GAP_ADDR_TYPE_WL) {

        rc = BLE_HS_EINVAL;
        goto done;
    }

    if (conn_params == NULL) {
        conn_params = &ble_gap_conn_params_dflt;
    }

    if (duration_ms == 0) {
        duration_ms = BLE_GAP_CONN_DUR_DFLT;
    }

    if (duration_ms != BLE_HS_FOREVER) {
        rc = os_time_ms_to_ticks(duration_ms, &duration_ticks);
        if (rc != 0) {
            /* Duration too great. */
            rc = BLE_HS_EINVAL;
            goto done;
        }
    }

    /* XXX: Verify conn_params. */

    rc = ble_hs_id_use_addr(own_addr_type);
    if (rc != 0) {
        return rc;
    }

    BLE_HS_LOG(INFO, "GAP procedure initiated: connect; ");
    ble_gap_log_conn(own_addr_type, peer_addr_type, peer_addr, conn_params);
    BLE_HS_LOG(INFO, "\n");

    ble_gap_master.cb = cb;
    ble_gap_master.cb_arg = cb_arg;
    ble_gap_master.conn.using_wl = peer_addr_type == BLE_GAP_ADDR_TYPE_WL;
    ble_gap_master.conn.our_addr_type = own_addr_type;

    rc = ble_gap_conn_create_tx(own_addr_type, peer_addr_type, peer_addr,
                                conn_params);
    if (rc != 0) {
        goto done;
    }

    if (duration_ms != BLE_HS_FOREVER) {
        ble_gap_master_set_timer(duration_ticks);
    }

    ble_gap_master.op = BLE_GAP_OP_M_CONN;

    rc = 0;

done:
    ble_hs_unlock();

    if (rc != 0) {
        STATS_INC(ble_gap_stats, initiate_fail);
    }
    return rc;
}

/**
 * Indicates whether a connect procedure is currently in progress.
 *
 * @return                      0: No connect procedure in progress;
 *                              1: Connect procedure in progress.
 */
int
ble_gap_conn_active(void)
{
    /* Assume read is atomic; mutex not necessary. */
    return ble_gap_master.op == BLE_GAP_OP_M_CONN;
}

/*****************************************************************************
 * $terminate connection procedure                                           *
 *****************************************************************************/

/**
 * Terminates an established connection.
 *
 * @param conn_handle           The handle corresponding to the connection to
 *                                  terminate.
 * @param hci_reason            The HCI error code to indicate as the reason
 *                                  for termination.
 *
 * @return                      0 on success;
 *                              BLE_HS_ENOTCONN if there is no connection with
 *                                  the specified handle;
 *                              Other nonzero on failure.
 */
int
ble_gap_terminate(uint16_t conn_handle, uint8_t hci_reason)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_DISCONNECT_CMD_LEN];
    int rc;

    STATS_INC(ble_gap_stats, terminate);

    ble_hs_lock();

    if (!ble_hs_conn_exists(conn_handle)) {
        rc = BLE_HS_ENOTCONN;
        goto done;
    }

    BLE_HS_LOG(INFO, "GAP procedure initiated: terminate connection; "
                     "conn_handle=%d hci_reason=%d\n",
               conn_handle, hci_reason);

    host_hci_cmd_build_disconnect(conn_handle, hci_reason,
                                  buf, sizeof buf);
    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        goto done;
    }

    rc = 0;

done:
    ble_hs_unlock();

    if (rc != 0) {
        STATS_INC(ble_gap_stats, terminate_fail);
    }
    return rc;
}

/*****************************************************************************
 * $cancel                                                                   *
 *****************************************************************************/

static int
ble_gap_conn_cancel_tx(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN];
    int rc;

    host_hci_cmd_build_le_create_conn_cancel(buf, sizeof buf);
    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Aborts a connect procedure in progress.
 *
 * @return                      0 on success;
 *                              BLE_HS_EALREADY if there is no active connect
 *                                  procedure.
 *                              Other nonzero on error.
 */
int
ble_gap_conn_cancel(void)
{
    int rc;

    STATS_INC(ble_gap_stats, cancel);

    ble_hs_lock();

    if (!ble_gap_conn_active()) {
        rc = BLE_HS_EALREADY;
        goto done;
    }

    BLE_HS_LOG(INFO, "GAP procedure initiated: cancel connection\n");

    rc = ble_gap_conn_cancel_tx();
    if (rc != 0) {
        goto done;
    }

    ble_gap_master.conn.cancel = 1;
    rc = 0;

done:
    ble_hs_unlock();

    if (rc != 0) {
        STATS_INC(ble_gap_stats, cancel_fail);
    }
    return rc;
}

/*****************************************************************************
 * $update connection parameters                                             *
 *****************************************************************************/

static int
ble_gap_tx_param_pos_reply(uint16_t conn_handle,
                           struct ble_gap_upd_params *params)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_CONN_PARAM_REPLY_LEN];
    struct hci_conn_param_reply pos_reply;
    int rc;

    pos_reply.handle = conn_handle;
    pos_reply.conn_itvl_min = params->itvl_min;
    pos_reply.conn_itvl_max = params->itvl_max;
    pos_reply.conn_latency = params->latency;
    pos_reply.supervision_timeout = params->supervision_timeout;
    pos_reply.min_ce_len = params->min_ce_len;
    pos_reply.max_ce_len = params->max_ce_len;

    host_hci_cmd_build_le_conn_param_reply(&pos_reply, buf, sizeof buf);
    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_gap_tx_param_neg_reply(uint16_t conn_handle, uint8_t reject_reason)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_CONN_PARAM_NEG_REPLY_LEN];
    struct hci_conn_param_neg_reply neg_reply;
    int rc;

    neg_reply.handle = conn_handle;
    neg_reply.reason = reject_reason;

    host_hci_cmd_build_le_conn_param_neg_reply(&neg_reply, buf, sizeof buf);
    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_gap_rx_param_req(struct hci_le_conn_param_req *evt)
{
#if !NIMBLE_OPT(CONNECT)
    return;
#endif

    struct ble_gap_upd_params peer_params;
    struct ble_gap_upd_params self_params;
    struct ble_gap_event event;
    uint8_t reject_reason;
    int rc;

    reject_reason = 0; /* Silence warning. */

    memset(&event, 0, sizeof event);

    peer_params.itvl_min = evt->itvl_min;
    peer_params.itvl_max = evt->itvl_max;
    peer_params.latency = evt->latency;
    peer_params.supervision_timeout = evt->timeout;
    peer_params.min_ce_len = 0;
    peer_params.max_ce_len = 0;

    /* Copy the peer params into the self params to make it easy on the
     * application.  The application callback will change only the fields which
     * it finds unsuitable.
     */
    self_params = peer_params;

    memset(&event, 0, sizeof event);
    event.type = BLE_GAP_EVENT_CONN_UPDATE_REQ;
    event.conn_update_req.conn_handle = evt->connection_handle;
    event.conn_update_req.self_params = &self_params;
    event.conn_update_req.peer_params = &peer_params;
    rc = ble_gap_call_conn_event_cb(&event, evt->connection_handle);
    if (rc != 0) {
        reject_reason = rc;
    }

    if (rc == 0) {
        rc = ble_gap_tx_param_pos_reply(evt->connection_handle, &self_params);
        if (rc != 0) {
            ble_gap_update_failed(evt->connection_handle, rc);
        } else {
            ble_hs_atomic_conn_set_flags(evt->connection_handle,
                                         BLE_HS_CONN_F_UPDATE, 1);
        }
    } else {
        ble_gap_tx_param_neg_reply(evt->connection_handle, reject_reason);
    }
}

static int
ble_gap_update_tx(uint16_t conn_handle,
                  const struct ble_gap_upd_params *params)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_CONN_UPDATE_LEN];
    struct hci_conn_update cmd;
    int rc;

    cmd.handle = conn_handle;
    cmd.conn_itvl_min = params->itvl_min;
    cmd.conn_itvl_max = params->itvl_max;
    cmd.conn_latency = params->latency;
    cmd.supervision_timeout = params->supervision_timeout;
    cmd.min_ce_len = params->min_ce_len;
    cmd.max_ce_len = params->max_ce_len;

    rc = host_hci_cmd_build_le_conn_update(&cmd, buf, sizeof buf);
    if (rc != 0) {
        return rc;
    }

    rc = ble_hci_cmd_tx_empty_ack(buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Initiates a connection parameter update procedure.
 *
 * @param conn_handle           The handle corresponding to the connection to
 *                                  update.
 * @param params                The connection parameters to attempt to update
 *                                  to.
 *
 * @return                      0 on success;
 *                              BLE_HS_ENOTCONN if the there is no connection
 *                                  with the specified handle;
 *                              BLE_HS_EALREADY if a connection update
 *                                  procedure for this connection is already in
 *                                  progress;
 *                              Other nonzero on error.
 */
int
ble_gap_update_params(uint16_t conn_handle,
                      const struct ble_gap_upd_params *params)
{
#if !NIMBLE_OPT(CONNECT)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_hs_conn *conn;
    int rc;

    STATS_INC(ble_gap_stats, update);

    ble_hs_lock();

    conn = ble_hs_conn_find(conn_handle);
    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
        goto done;
    }

    if (conn->bhc_flags & BLE_HS_CONN_F_UPDATE) {
        rc = BLE_HS_EALREADY;
        goto done;
    }

    BLE_HS_LOG(INFO, "GAP procedure initiated: ");
    ble_gap_log_update(conn_handle, params);
    BLE_HS_LOG(INFO, "\n");

    rc = ble_gap_update_tx(conn_handle, params);
    if (rc != 0) {
        goto done;
    }

    conn->bhc_flags |= BLE_HS_CONN_F_UPDATE;

done:
    ble_hs_unlock();

    if (rc != 0) {
        STATS_INC(ble_gap_stats, update_fail);
    }
    return rc;
}

/*****************************************************************************
 * $security                                                                 *
 *****************************************************************************/

/**
 * Initiates the GAP encryption procedure.
 *
 * @param conn_handle           The handle corresponding to the connection to
 *                                  encrypt.
 *
 * @return                      0 on success;
 *                              BLE_HS_ENOTCONN if the there is no connection
 *                                  with the specified handle;
 *                              BLE_HS_EALREADY if an encrpytion procedure for
 *                                  this connection is already in progress;
 *                              Other nonzero on error.
 */
int
ble_gap_security_initiate(uint16_t conn_handle)
{
#if !NIMBLE_OPT(SM)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_store_value_sec value_sec;
    struct ble_store_key_sec key_sec;
    struct ble_hs_conn_addrs addrs;
    ble_hs_conn_flags_t conn_flags;
    struct ble_hs_conn *conn;
    int rc;

    STATS_INC(ble_gap_stats, security_initiate);

    ble_hs_lock();
    conn = ble_hs_conn_find(conn_handle);
    if (conn != NULL) {
        conn_flags = conn->bhc_flags;
        ble_hs_conn_addrs(conn, &addrs);

        memset(&key_sec, 0, sizeof key_sec);
        key_sec.peer_addr_type = addrs.peer_id_addr_type;
        memcpy(key_sec.peer_addr, addrs.peer_id_addr, 6);
    }
    ble_hs_unlock();

    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
        goto done;
    }

    if (conn_flags & BLE_HS_CONN_F_MASTER) {
        /* Search the security database for an LTK for this peer.  If one
         * is found, perform the encryption procedure rather than the pairing
         * procedure.
         */
        rc = ble_store_read_peer_sec(&key_sec, &value_sec);
        if (rc == 0 && value_sec.ltk_present) {
            rc = ble_sm_enc_initiate(conn_handle, value_sec.ltk,
                                     value_sec.ediv, value_sec.rand_num,
                                     value_sec.authenticated);
            if (rc != 0) {
                goto done;
            }
        } else {
            rc = ble_sm_pair_initiate(conn_handle);
            if (rc != 0) {
                goto done;
            }
        }
    } else {
        rc = ble_sm_slave_initiate(conn_handle);
        if (rc != 0) {
            goto done;
        }
    }

    rc = 0;

done:
    if (rc != 0) {
        STATS_INC(ble_gap_stats, security_initiate_fail);
    }

    return rc;
}

int
ble_gap_pair_initiate(uint16_t conn_handle)
{
    int rc;

    rc = ble_sm_pair_initiate(conn_handle);

    return rc;
}

int
ble_gap_encryption_initiate(uint16_t conn_handle,
                            const uint8_t *ltk,
                            uint16_t ediv,
                            uint64_t rand_val,
                            int auth)
{
#if !NIMBLE_OPT(SM)
    return BLE_HS_ENOTSUP;
#endif

    ble_hs_conn_flags_t conn_flags;
    int rc;

    rc = ble_hs_atomic_conn_flags(conn_handle, &conn_flags);
    if (rc != 0) {
        return rc;
    }

    if (!(conn_flags & BLE_HS_CONN_F_MASTER)) {
        return BLE_HS_EROLE;
    }

    rc = ble_sm_enc_initiate(conn_handle, ltk, ediv, rand_val, auth);
    return rc;
}

void
ble_gap_passkey_event(uint16_t conn_handle,
                      struct ble_gap_passkey_params *passkey_params)
{
#if !NIMBLE_OPT(SM)
    return;
#endif

    struct ble_gap_event event;

    BLE_HS_LOG(DEBUG, "send passkey action request %d\n",
               passkey_params->action);

    memset(&event, 0, sizeof event);
    event.type = BLE_GAP_EVENT_PASSKEY_ACTION;
    event.passkey.conn_handle = conn_handle;
    event.passkey.params = *passkey_params;
    ble_gap_call_conn_event_cb(&event, conn_handle);
}

void
ble_gap_enc_event(uint16_t conn_handle, int status, int security_restored)
{
#if !NIMBLE_OPT(SM)
    return;
#endif

    struct ble_gap_event event;

    memset(&event, 0, sizeof event);
    event.type = BLE_GAP_EVENT_ENC_CHANGE;
    event.enc_change.conn_handle = conn_handle;
    event.enc_change.status = status;
    ble_gap_call_conn_event_cb(&event, conn_handle);

    if (status == 0 && security_restored) {
        ble_gatts_bonding_restored(conn_handle);
    }
}

/*****************************************************************************
 * $rssi                                                                     *
 *****************************************************************************/

/**
 * Retrieves the most-recently measured RSSI for the specified connection.  A
 * connection's RSSI is updated whenever a data channel PDU is received.
 *
 * @param conn_handle           Specifies the connection to query.
 * @param out_rssi              On success, the retrieved RSSI is written here.
 *
 * @return                      0 on success;
 *                              A BLE host HCI return code if the controller
 *                                  rejected the request;
 *                              A BLE host core return code on unexpected
 *                                  error.
 */
int
ble_gap_conn_rssi(uint16_t conn_handle, int8_t *out_rssi)
{
    int rc;

    rc = ble_hci_util_read_rssi(conn_handle, out_rssi);
    return rc;
}

/*****************************************************************************
 * $notify                                                                   *
 *****************************************************************************/

void
ble_gap_notify_rx_event(uint16_t conn_handle, uint16_t attr_handle,
                        struct os_mbuf *om, int is_indication)
{
#if !NIMBLE_OPT(GATT_NOTIFY) && !NIMBLE_OPT(GATT_INDICATE)
    return;
#endif

    struct ble_gap_event event;

    memset(&event, 0, sizeof event);
    event.type = BLE_GAP_EVENT_NOTIFY_RX;
    event.notify_rx.conn_handle = conn_handle;
    event.notify_rx.attr_handle = attr_handle;
    event.notify_rx.om = om;
    event.notify_rx.indication = is_indication;
    ble_gap_call_conn_event_cb(&event, conn_handle);

    os_mbuf_free_chain(event.notify_rx.om);
}

void
ble_gap_notify_tx_event(int status, uint16_t conn_handle, uint16_t attr_handle,
                        int is_indication)
{
#if !NIMBLE_OPT(GATT_NOTIFY) && !NIMBLE_OPT(GATT_INDICATE)
    return;
#endif

    struct ble_gap_event event;

    memset(&event, 0, sizeof event);
    event.type = BLE_GAP_EVENT_NOTIFY_TX;
    event.notify_tx.conn_handle = conn_handle;
    event.notify_tx.status = status;
    event.notify_tx.attr_handle = attr_handle;
    event.notify_tx.indication = is_indication;
    ble_gap_call_conn_event_cb(&event, conn_handle);
}

/*****************************************************************************
 * $subscribe                                                                *
 *****************************************************************************/

void
ble_gap_subscribe_event(uint16_t conn_handle, uint16_t attr_handle,
                        uint8_t reason,
                        uint8_t prev_notify, uint8_t cur_notify,
                        uint8_t prev_indicate, uint8_t cur_indicate)
{
    struct ble_gap_event event;

    BLE_HS_DBG_ASSERT(prev_notify != cur_notify ||
                      prev_indicate != cur_indicate);
    BLE_HS_DBG_ASSERT(reason == BLE_GAP_SUBSCRIBE_REASON_WRITE ||
                      reason == BLE_GAP_SUBSCRIBE_REASON_TERM  ||
                      reason == BLE_GAP_SUBSCRIBE_REASON_RESTORE);

    memset(&event, 0, sizeof event);
    event.type = BLE_GAP_EVENT_SUBSCRIBE;
    event.subscribe.conn_handle = conn_handle;
    event.subscribe.attr_handle = attr_handle;
    event.subscribe.reason = reason;
    event.subscribe.prev_notify = !!prev_notify;
    event.subscribe.cur_notify = !!cur_notify;
    event.subscribe.prev_indicate = !!prev_indicate;
    event.subscribe.cur_indicate = !!cur_indicate;
    ble_gap_call_conn_event_cb(&event, conn_handle);
}

/*****************************************************************************
 * $init                                                                     *
 *****************************************************************************/

int
ble_gap_init(void)
{
    int rc;

    memset(&ble_gap_master, 0, sizeof ble_gap_master);
    memset(&ble_gap_slave, 0, sizeof ble_gap_slave);

    rc = stats_init_and_reg(
        STATS_HDR(ble_gap_stats), STATS_SIZE_INIT_PARMS(ble_gap_stats,
        STATS_SIZE_32), STATS_NAME_INIT_PARMS(ble_gap_stats), "ble_gap");
    if (rc != 0) {
        return BLE_HS_EOS;
    }

    return 0;
}
