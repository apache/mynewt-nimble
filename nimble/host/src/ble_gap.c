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
#include "host/host_hci.h"
#include "ble_hs_priv.h"

#define BLE_GAP_OP_NULL                                 0
#define BLE_GAP_STATE_NULL                              255

#define BLE_GAP_OP_M_DISC                               1
#define BLE_GAP_OP_M_CONN                               2

#define BLE_GAP_OP_S_NON                                1
#define BLE_GAP_OP_S_UND                                2
#define BLE_GAP_OP_S_DIR                                3

#define BLE_GAP_OP_W_SET                                1

/** Discovery master states. */
#define BLE_GAP_STATE_M_DISC_PARAMS                     0
#define BLE_GAP_STATE_M_DISC_ENABLE                     1
#define BLE_GAP_STATE_M_DISC_ACKED                      2
#define BLE_GAP_STATE_M_DISC_DISABLE                    3

/** Connect master states. */
#define BLE_GAP_STATE_M_PENDING                         0
#define BLE_GAP_STATE_M_UNACKED                         1
#define BLE_GAP_STATE_M_ACKED                           2

/** Undirected slave states. */
#define BLE_GAP_STATE_S_UND_PARAMS                      0
#define BLE_GAP_STATE_S_UND_POWER                       1
#define BLE_GAP_STATE_S_UND_ADV_DATA                    2
#define BLE_GAP_STATE_S_UND_RSP_DATA                    3
#define BLE_GAP_STATE_S_UND_ENABLE                      4
#define BLE_GAP_STATE_S_UND_ADV                         5

/** Directed slave states. */
#define BLE_GAP_STATE_S_DIR_PARAMS                      0
#define BLE_GAP_STATE_S_DIR_ENABLE                      1
#define BLE_GAP_STATE_S_DIR_ADV                         2

/** White list states. */
#define BLE_GAP_STATE_W_CLEAR                           0
#define BLE_GAP_STATE_W_ADD                             1

/** Connection update states. */
#define BLE_GAP_STATE_U_UPDATE                          0
#define BLE_GAP_STATE_U_UPDATE_ACKED                    1
#define BLE_GAP_STATE_U_REPLY                           2
#define BLE_GAP_STATE_U_REPLY_ACKED                     3
#define BLE_GAP_STATE_U_NEG_REPLY                       4

/**
 * The maximum amount of user data that can be put into the advertising data.
 * Six bytes are reserved at the end for the flags field and the transmit power
 * field.
 */
#define BLE_GAP_ADV_DATA_LIMIT          (BLE_HCI_MAX_ADV_DATA_LEN - 6)

static const struct ble_gap_crt_params ble_gap_params_dflt = {
    .scan_itvl = 0x0010,
    .scan_window = 0x0010,
    .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN,
    .itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX,
    .latency = BLE_GAP_INITIAL_CONN_LATENCY,
    .supervision_timeout = BLE_GAP_INITIAL_SUPERVISION_TIMEOUT,
    .min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN,
    .max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN,
};

static const struct hci_adv_params ble_gap_adv_params_dflt = {
    .adv_itvl_min = 0,
    .adv_itvl_max = 0,
    .adv_type = BLE_HCI_ADV_TYPE_ADV_IND,
    .own_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC,
    .peer_addr_type = BLE_HCI_ADV_PEER_ADDR_PUBLIC,
    .adv_channel_map = BLE_HCI_ADV_CHANMASK_DEF,
    .adv_filter_policy = BLE_HCI_ADV_FILT_DEF,
};

/**
 * The state of the in-progress master connection.  If no master connection is
 * currently in progress, then the op field is set to BLE_GAP_OP_NULL.
 */
static bssnz_t struct {
    uint8_t op;
    uint8_t state;
    uint8_t hci_handle;

    union {
        struct {
            uint8_t addr_type;
            uint8_t addr[6];
            struct ble_gap_crt_params params;
            ble_gap_conn_fn *cb;
            void *cb_arg;
        } conn;

        struct {
            uint8_t disc_mode;
            uint8_t filter_policy;
            uint8_t scan_type;
            ble_gap_disc_fn *cb;
            void *cb_arg;
        } disc;
    };
} ble_gap_master;

/**
 * The state of the in-progress slave connection.  If no slave connection is
 * currently in progress, then the op field is set to BLE_GAP_OP_NULL.
 */
static bssnz_t struct {
    uint8_t op;
    uint8_t state;
    uint8_t disc_mode;
    uint8_t hci_handle;
    ble_gap_conn_fn *cb;
    void *cb_arg;

    uint8_t dir_addr_type;
    uint8_t dir_addr[BLE_DEV_ADDR_LEN];

    struct hci_adv_params adv_params;
    int8_t tx_pwr_lvl;
    uint8_t adv_data_len;
    uint8_t adv_data[BLE_HCI_MAX_ADV_DATA_LEN];
} ble_gap_slave;

static bssnz_t struct {
    ble_gap_wl_fn *cb;
    void *cb_arg;

    struct ble_gap_white_entry *entries;
    uint8_t op;
    uint8_t state;
    uint8_t count;
    uint8_t cur;
    uint8_t hci_handle;
} ble_gap_wl;

struct ble_gap_update_entry {
    SLIST_ENTRY(ble_gap_update_entry) next;
    struct ble_gap_upd_params params;
    uint16_t conn_handle;
    uint8_t reject_reason;
    uint8_t state;
    uint8_t hci_handle;
};
static SLIST_HEAD(, ble_gap_update_entry) ble_gap_update_entries;

static void *ble_gap_update_mem;
static struct os_mempool ble_gap_update_pool;

static int ble_gap_adv_params_tx(void *arg);
static int ble_gap_adv_power_tx(void *arg);
static int ble_gap_adv_data_tx(void *arg);
static int ble_gap_adv_rsp_data_tx(void *arg);
static int ble_gap_adv_enable_tx(void *arg);
static int ble_gap_wl_tx_add(void *arg);
static int ble_gap_disc_tx_disable(void *arg);

static ble_hci_sched_tx_fn * const ble_gap_dispatch_adv_und[] = {
    [BLE_GAP_STATE_S_UND_PARAMS]   = ble_gap_adv_params_tx,
    [BLE_GAP_STATE_S_UND_POWER]    = ble_gap_adv_power_tx,
    [BLE_GAP_STATE_S_UND_ADV_DATA] = ble_gap_adv_data_tx,
    [BLE_GAP_STATE_S_UND_RSP_DATA] = ble_gap_adv_rsp_data_tx,
    [BLE_GAP_STATE_S_UND_ENABLE]   = ble_gap_adv_enable_tx,
    [BLE_GAP_STATE_S_UND_ADV]      = NULL,
};

static ble_hci_sched_tx_fn * const ble_gap_dispatch_adv_dir[] = {
    [BLE_GAP_STATE_S_DIR_PARAMS]   = ble_gap_adv_params_tx,
    [BLE_GAP_STATE_S_DIR_ENABLE]   = ble_gap_adv_enable_tx,
    [BLE_GAP_STATE_S_DIR_ADV]      = NULL,
};

static struct os_callout_func ble_gap_master_timer;
static struct os_callout_func ble_gap_slave_timer;

struct ble_gap_snapshot {
    struct ble_gap_conn_desc desc;
    ble_gap_conn_fn *cb;
    void *cb_arg;
};

static struct os_mutex ble_gap_mutex;

STATS_SECT_DECL(ble_gap_stats) ble_gap_stats;
STATS_NAME_START(ble_gap_stats)
    STATS_NAME(ble_gap_stats, disconnect)
    STATS_NAME(ble_gap_stats, wl_set)
    STATS_NAME(ble_gap_stats, adv_stop)
    STATS_NAME(ble_gap_stats, adv_start)
    STATS_NAME(ble_gap_stats, adv_set_field)
    STATS_NAME(ble_gap_stats, discover)
    STATS_NAME(ble_gap_stats, initiate)
    STATS_NAME(ble_gap_stats, terminate)
    STATS_NAME(ble_gap_stats, cancel)
    STATS_NAME(ble_gap_stats, update)
    STATS_NAME(ble_gap_stats, connect_slv)
    STATS_NAME(ble_gap_stats, connect_mst)
    STATS_NAME(ble_gap_stats, rx_disconn)
    STATS_NAME(ble_gap_stats, rx_update_complete)
    STATS_NAME(ble_gap_stats, rx_adv_report)
    STATS_NAME(ble_gap_stats, rx_conn_complete)
STATS_NAME_END(ble_gap_stats)

/*****************************************************************************
 * $mutex                                                                    *
 *****************************************************************************/

static void
ble_gap_lock(void)
{
    struct os_task *owner;
    int rc;

    owner = ble_gap_mutex.mu_owner;
    if (owner != NULL) {
        assert(owner != os_sched_get_current_task());
    }

    rc = os_mutex_pend(&ble_gap_mutex, 0xffffffff);
    assert(rc == 0 || rc == OS_NOT_STARTED);
}

static void
ble_gap_unlock(void)
{
    int rc;

    rc = os_mutex_release(&ble_gap_mutex);
    assert(rc == 0 || rc == OS_NOT_STARTED);
}

int
ble_gap_locked_by_cur_task(void)
{
    struct os_task *owner;

    owner = ble_gap_mutex.mu_owner;
    return owner != NULL && owner == os_sched_get_current_task();
}

/*****************************************************************************
 * $log                                                                      *
 *****************************************************************************/

static void
ble_gap_log_conn(void)
{
    BLE_HS_LOG(INFO, "addr_type=%d addr=", ble_gap_master.conn.addr_type);
    BLE_HS_LOG_ADDR(INFO, ble_gap_master.conn.addr);
    BLE_HS_LOG(INFO, " scan_itvl=%d scan_window=%d itvl_min=%d itvl_max=%d "
                     "latency=%d supervision_timeout=%d min_ce_len=%d "
                     "max_ce_len=%d",
               ble_gap_master.conn.params.scan_itvl,
               ble_gap_master.conn.params.scan_window,
               ble_gap_master.conn.params.itvl_min,
               ble_gap_master.conn.params.itvl_max,
               ble_gap_master.conn.params.latency,
               ble_gap_master.conn.params.supervision_timeout,
               ble_gap_master.conn.params.min_ce_len,
               ble_gap_master.conn.params.max_ce_len);
}

static void
ble_gap_log_disc(void)
{
    BLE_HS_LOG(INFO, "disc_mode=%d filter_policy=%d scan_type=%d",
               ble_gap_master.disc.disc_mode,
               ble_gap_master.disc.filter_policy,
               ble_gap_master.disc.scan_type);
}

static void
ble_gap_log_update(struct ble_gap_update_entry *entry)
{
    BLE_HS_LOG(INFO, "connection parameter update; "
                     "conn_handle=%d itvl_min=%d itvl_max=%d latency=%d "
                     "supervision_timeout=%d min_ce_len=%d max_ce_len=%d",
               entry->conn_handle, entry->params.itvl_min,
               entry->params.itvl_max, entry->params.latency,
               entry->params.supervision_timeout, entry->params.min_ce_len,
               entry->params.max_ce_len);
}

static void
ble_gap_log_wl(void)
{
    struct ble_gap_white_entry *entry;
    int i;

    BLE_HS_LOG(INFO, "count=%d ", ble_gap_wl.count);

    for (i = 0; i < ble_gap_wl.count; i++) {
        entry = ble_gap_wl.entries + i;

        BLE_HS_LOG(INFO, "entry-%d={addr_type=%d addr=", i, entry->addr_type);
        BLE_HS_LOG_ADDR(INFO, entry->addr);
        BLE_HS_LOG(INFO, "} ");
    }
}

static void
ble_gap_log_adv(void)
{
    BLE_HS_LOG(INFO, "disc_mode=%d addr_type=%d addr=",
               ble_gap_slave.disc_mode, ble_gap_slave.dir_addr_type);
    BLE_HS_LOG_ADDR(INFO, ble_gap_slave.dir_addr);
    BLE_HS_LOG(INFO, "adv_type=%d adv_channel_map=%d own_addr_type=%d "
                     "adv_filter_policy=%d adv_itvl_min=%d  adv_itvl_max=%d "
                     "adv_data_len=%d",
               ble_gap_slave.adv_params.adv_type,
               ble_gap_slave.adv_params.adv_channel_map,
               ble_gap_slave.adv_params.own_addr_type,
               ble_gap_slave.adv_params.adv_filter_policy,
               ble_gap_slave.adv_params.adv_itvl_min,
               ble_gap_slave.adv_params.adv_itvl_max,
               ble_gap_slave.adv_data_len);
}

/*****************************************************************************
 * $snapshot                                                                 *
 *****************************************************************************/

/**
 * Lock restrictions: None.
 */
static void
ble_gap_fill_conn_desc(struct ble_hs_conn *conn,
                       struct ble_gap_conn_desc *desc)
{
    desc->conn_handle = conn->bhc_handle;
    desc->peer_addr_type = conn->bhc_addr_type;
    memcpy(desc->peer_addr, conn->bhc_addr, sizeof desc->peer_addr);
    desc->conn_itvl = conn->bhc_itvl;
    desc->conn_latency = conn->bhc_latency;
    desc->supervision_timeout = conn->bhc_supervision_timeout;
}

/**
 * Lock restrictions: None.
 */
static void
ble_gap_conn_to_snapshot(struct ble_hs_conn *conn,
                         struct ble_gap_snapshot *snap)
{
    ble_gap_fill_conn_desc(conn, &snap->desc);
    snap->cb = conn->bhc_cb;
    snap->cb_arg = conn->bhc_cb_arg;
}

/**
 * Lock restrictions:
 *     o Caller unlocks ble_hs_conn.
 */
static int
ble_gap_find_snapshot(uint16_t handle, struct ble_gap_snapshot *snap)
{
    struct ble_hs_conn *conn;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(handle);
    if (conn != NULL) {
        ble_gap_conn_to_snapshot(conn, snap);
    }

    ble_hs_conn_unlock();

    if (conn == NULL) {
        return BLE_HS_ENOENT;
    } else {
        return 0;
    }
}

/*****************************************************************************
 * $misc                                                                     *
 *****************************************************************************/

/**
 * Lock restrictions:
 *     o Caller locks gap.
 */
static void
ble_gap_master_reset_state(void)
{
    os_callout_stop(&ble_gap_master_timer.cf_c);
    ble_gap_master.op = BLE_GAP_OP_NULL;
}

/**
 * Lock restrictions:
 *     o Caller locks gap.
 */
static void
ble_gap_slave_reset_state(void)
{
    os_callout_stop(&ble_gap_slave_timer.cf_c);
    ble_gap_slave.op = BLE_GAP_OP_NULL;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_call_conn_cb(int event, int status,
                     struct ble_gap_snapshot *snap,
                     struct ble_gap_upd_params *self_params,
                     struct ble_gap_upd_params *peer_params)
{
    struct ble_gap_conn_ctxt ctxt;
    int rc;

    ble_hs_misc_assert_no_locks();

    memset(&ctxt, 0, sizeof ctxt);
    ctxt.desc = &snap->desc;
    ctxt.self_params = self_params;
    ctxt.peer_params = peer_params;

    if (snap->cb != NULL) {
        rc = snap->cb(event, status, &ctxt, snap->cb_arg);
    } else {
        if (event == BLE_GAP_EVENT_CONN_UPDATE_REQ) {
            /* Just copy peer parameters back into reply. */
            *self_params = *peer_params;
        }
        rc = 0;
    }

    return rc;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_call_slave_cb(int event, int status, int reset_state)
{
    struct ble_gap_conn_ctxt ctxt;
    struct ble_gap_conn_desc desc;
    ble_gap_conn_fn *cb;
    void *cb_arg;

    ble_hs_misc_assert_no_locks();

    ble_gap_lock();

    desc.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    desc.peer_addr_type = ble_gap_slave.dir_addr_type;
    memcpy(desc.peer_addr, ble_gap_slave.dir_addr, sizeof desc.peer_addr);

    cb = ble_gap_slave.cb;
    cb_arg = ble_gap_slave.cb_arg;

    if (reset_state) {
        ble_gap_slave_reset_state();
    }

    ble_gap_unlock();

    if (cb != NULL) {
        ctxt.desc = &desc;
        ctxt.peer_params = NULL;
        ctxt.self_params = NULL;

        cb(event, status, &ctxt, cb_arg);
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_call_master_conn_cb(int event, int status, int reset_state)
{
    struct ble_gap_conn_ctxt ctxt;
    struct ble_gap_conn_desc desc;
    ble_gap_conn_fn *cb;
    void *cb_arg;
    int rc;

    ble_hs_misc_assert_no_locks();

    ble_gap_lock();

    memset(&desc, 0, sizeof ctxt);

    desc.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    desc.peer_addr_type = ble_gap_master.conn.addr_type;
    memcpy(desc.peer_addr, ble_gap_master.conn.addr,
           sizeof desc.peer_addr);

    cb = ble_gap_master.conn.cb;
    cb_arg = ble_gap_master.conn.cb_arg;

    if (reset_state) {
        ble_gap_master_reset_state();
    }

    ble_gap_unlock();

    if (cb != NULL) {
        ctxt.desc = &desc;
        ctxt.peer_params = NULL;
        ctxt.self_params = NULL;

        rc = cb(event, status, &ctxt, cb_arg);
    } else {
        rc = 0;
    }

    return rc;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_call_master_disc_cb(int event, int status, struct ble_hs_adv *adv,
                            struct ble_hs_adv_fields *fields, int reset_state)
{
    struct ble_gap_disc_desc desc;
    ble_gap_disc_fn *cb;
    void *cb_arg;

    ble_hs_misc_assert_no_locks();

    ble_gap_lock();

    if (adv != NULL) {
        desc.event_type = adv->event_type;
        desc.addr_type = adv->addr_type;
        desc.length_data = adv->length_data;
        desc.rssi = adv->rssi;
        memcpy(desc.addr, adv->addr, sizeof adv->addr);
        desc.data = adv->data;
        desc.fields = fields;
    } else {
        memset(&desc, 0, sizeof desc);
    }

    cb = ble_gap_master.disc.cb;
    cb_arg = ble_gap_master.disc.cb_arg;

    if (reset_state) {
        ble_gap_master_reset_state();
    }

    ble_gap_unlock();

    if (cb != NULL) {
        cb(event, status, &desc, cb_arg);
    }
}

/**
 * Lock restrictions: Caller must NOT lock ble_hs_conn mutex.
 */
static void
ble_gap_call_wl_cb(int status, int reset_state)
{
    ble_gap_wl_fn *cb;
    void *cb_arg;

    ble_gap_lock();

    cb = ble_gap_wl.cb;
    cb_arg = ble_gap_wl.cb_arg;

    if (reset_state) {
        ble_gap_wl.op = BLE_GAP_OP_NULL;
    }

    ble_gap_unlock();

    if (cb != NULL) {
        cb(status, cb_arg);
    }
}

/**
 * Lock restrictions:
 *     o Caller locks gap.
 */
static struct ble_gap_update_entry *
ble_gap_update_find(uint16_t conn_handle)
{
    struct ble_gap_update_entry *entry;

    SLIST_FOREACH(entry, &ble_gap_update_entries, next) {
        if (entry->conn_handle == conn_handle) {
            return entry;
        }
    }

    return NULL;
}

/**
 * Lock restrictions: None.
 */
static struct ble_gap_update_entry *
ble_gap_update_entry_alloc(uint16_t conn_handle,
                           struct ble_gap_upd_params *params, int state)
{
    struct ble_gap_update_entry *entry;

#ifdef BLE_HS_DEBUG
    ble_gap_lock();
    assert(ble_gap_update_find(conn_handle) == NULL);
    ble_gap_unlock();
#endif

    entry = os_memblock_get(&ble_gap_update_pool);
    if (entry == NULL) {
        return NULL;
    }

    memset(entry, 0, sizeof *entry);
    entry->conn_handle = conn_handle;
    entry->params = *params;
    entry->state = state;

    return entry;
}

/**
 * Lock restrictions: None.
 */
static void
ble_gap_update_entry_free(struct ble_gap_update_entry *entry)
{
    int rc;

    rc = os_memblock_put(&ble_gap_update_pool, entry);
    assert(rc == 0);
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_update_notify(struct ble_gap_update_entry *entry, int status)
{
    struct ble_gap_snapshot snap;
    int rc;

    rc = ble_gap_find_snapshot(entry->conn_handle, &snap);
    if (rc != 0) {
        return;
    }

    ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN_UPDATED, status, &snap, NULL,
                         NULL);
}

/**
 * Called when an error is encountered while the master-connection-fsm is
 * active.  Resets the state machine, clears the HCI ack callback, and notifies
 * the host task that the next hci_batch item can be processed.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_master_failed(int status)
{
    switch (ble_gap_master.op) {
    case BLE_GAP_OP_M_DISC:
        ble_gap_call_master_disc_cb(BLE_GAP_EVENT_DISC_FINISHED, status,
                                    NULL, NULL, 1);
        break;

    case BLE_GAP_OP_M_CONN:
        ble_gap_call_master_conn_cb(BLE_GAP_EVENT_CONN, status, 1);
        break;

    default:
        break;
    }
}

/**
 * Lock restrictions: None.
 */
static void
ble_gap_update_entry_remove_free(struct ble_gap_update_entry *entry)
{
    SLIST_REMOVE(&ble_gap_update_entries, entry, ble_gap_update_entry, next);
    ble_gap_update_entry_free(entry);
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_update_failed(struct ble_gap_update_entry *entry, int status)
{
    ble_gap_update_notify(entry, status);
    ble_gap_update_entry_free(entry);
}

/**
 * Lock restrictions:
 *     o Caller unlocks gap.
 */
static void
ble_gap_conn_broken(uint16_t conn_handle)
{
    struct ble_gap_update_entry *entry;

    ble_gap_lock();

    entry = ble_gap_update_find(conn_handle);
    if (entry != NULL) {
        if (entry->hci_handle != BLE_HCI_SCHED_HANDLE_NONE) {
            ble_hci_sched_cancel(entry->hci_handle);
        }
        ble_gap_update_entry_remove_free(entry);
    }

    ble_gap_unlock();

    ble_gattc_connection_broken(conn_handle);

    STATS_INC(ble_gap_stats, disconnect);
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gap_rx_disconn_complete(struct hci_disconn_complete *evt)
{
#if !NIMBLE_OPT_CONNECT
    return;
#endif

    struct ble_gap_snapshot snap;
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_misc_assert_no_locks();

    STATS_INC(ble_gap_stats, rx_disconn);

    if (evt->status == 0) {
        /* Find the connection that this event refers to. */
        ble_hs_conn_lock();
        conn = ble_hs_conn_find(evt->connection_handle);
        if (conn != NULL) {
            ble_gap_conn_to_snapshot(conn, &snap);

            ble_gap_conn_broken(evt->connection_handle);
            ble_hs_conn_remove(conn);
            ble_hs_conn_free(conn);
        }
        ble_hs_conn_unlock();

        if (conn != NULL) {
            ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN, BLE_HS_ENOTCONN,
                                      &snap, NULL, NULL);
        }
    } else {
        rc = ble_gap_find_snapshot(evt->connection_handle, &snap);
        if (rc == 0) {
            ble_gap_call_conn_cb(BLE_GAP_EVENT_TERM_FAILURE,
                                      BLE_HS_HCI_ERR(evt->status), &snap,
                                      NULL, NULL);
        }
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gap_rx_update_complete(struct hci_le_conn_upd_complete *evt)
{
#if !NIMBLE_OPT_CONNECT
    return;
#endif

    struct ble_gap_update_entry *entry;
    struct ble_gap_snapshot snap;
    struct ble_hs_conn *conn;

    ble_hs_misc_assert_no_locks();

    STATS_INC(ble_gap_stats, rx_update_complete);

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(evt->connection_handle);
    if (conn != NULL) {
        ble_gap_lock();

        entry = ble_gap_update_find(evt->connection_handle);
        if (entry != NULL) {
            ble_gap_update_entry_remove_free(entry);
        }

        ble_gap_unlock();

        if (evt->status == 0) {
            conn->bhc_itvl = evt->conn_itvl;
            conn->bhc_latency = evt->conn_latency;
            conn->bhc_supervision_timeout = evt->supervision_timeout;
        }

        ble_gap_conn_to_snapshot(conn, &snap);
    }

    ble_hs_conn_unlock();

    if (conn != NULL) {
        ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN_UPDATED,
                             BLE_HS_HCI_ERR(evt->status), &snap, NULL, NULL);
    }
}

/**
 * Tells you if the BLE host is in the process of creating a master connection.
 *
 * Lock restrictions: None.
 */
int
ble_gap_master_in_progress(void)
{
    return ble_gap_master.op != BLE_GAP_OP_NULL;
}

/**
 * Tells you if the BLE host is in the process of creating a slave connection.
 *
 * Lock restrictions: None.
 */
int
ble_gap_slave_in_progress(void)
{
    return ble_gap_slave.op != BLE_GAP_OP_NULL;
}

/**
 * Tells you if the BLE host is in the process of updating a connection.
 *
 * Lock restrictions:
 *     o Caller unlocks gap.
 *
 * @param conn_handle           The connection to test, or
 *                                  BLE_HS_CONN_HANDLE_NONE to check all
 *                                  connections.
 *
 * @return                      0=connection not being updated;
 *                              1=connection being updated.
 */
int
ble_gap_update_in_progress(uint16_t conn_handle)
{
#if !NIMBLE_OPT_CONNECT
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gap_update_entry *entry;

    ble_gap_lock();

    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        entry = ble_gap_update_find(conn_handle);
    } else {
        entry = SLIST_FIRST(&ble_gap_update_entries);
    }

    ble_gap_unlock();

    return entry != NULL;
}

/**
 * Lock restrictions:
 *     o Caller unlocks gap.
 */
static int
ble_gap_gen_set_op(uint8_t *slot, uint8_t op)
{
    int rc;

    ble_gap_lock();

    if (*slot != BLE_GAP_OP_NULL) {
        rc = BLE_HS_EALREADY;
    } else {
        *slot = op;
        rc = 0;
    }

    ble_gap_unlock();

    return rc;
}

/**
 * Lock restrictions:
 *     o Caller unlocks gap.
 */
static int
ble_gap_master_set_op(uint8_t op)
{
    return ble_gap_gen_set_op(&ble_gap_master.op, op);
}

/**
 * Lock restrictions:
 *     o Caller unlocks gap.
 */
static int
ble_gap_slave_set_op(uint8_t op)
{
    return ble_gap_gen_set_op(&ble_gap_slave.op, op);
}

/**
 * Lock restrictions:
 *     o Caller unlocks gap.
 */
static int
ble_gap_wl_set_op(uint8_t op)
{
    int rc;

    ble_gap_lock();

    if (ble_gap_wl_busy()) {
        rc = BLE_HS_EBUSY;
    } else {
        ble_gap_wl.op = op;
        rc = 0;
    }

    ble_gap_unlock();

    return rc;
}

/**
 * Lock restrictions: None.
 */
static int
ble_gap_currently_advertising(void)
{
    switch (ble_gap_slave.op) {
    case BLE_GAP_OP_NULL:
        return 0;

    case BLE_GAP_OP_S_NON:
        return ble_gap_slave.state == BLE_GAP_STATE_S_UND_ADV;

    case BLE_GAP_OP_S_UND:
        return ble_gap_slave.state == BLE_GAP_STATE_S_UND_ADV;

    case BLE_GAP_OP_S_DIR:
        return ble_gap_slave.state == BLE_GAP_STATE_S_DIR_ADV;

    default:
        assert(0);
        return 0;
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_master_enqueue(uint8_t state, int in_progress,
                       ble_hci_sched_tx_fn *hci_tx_cb, void *cb_arg)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    ble_gap_master.state = state;
    rc = ble_hci_sched_enqueue(hci_tx_cb, cb_arg, &ble_gap_master.hci_handle);
    if (rc != 0) {
        if (in_progress) {
            ble_gap_master_failed(rc);
        } else {
            ble_gap_master_reset_state();
        }
    }

    return rc;
}

/**
 * Attempts to complete the master connection process in response to a
 * "connection complete" event from the controller.  If the master connection
 * FSM is in a state that can accept this event, and the peer device address is
 * valid, the master FSM is reset and success is returned.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @param addr_type             The address type of the peer; one of the
 *                                  following values:
 *                                  o    BLE_HCI_ADV_PEER_ADDR_PUBLIC
 *                                  o    BLE_HCI_ADV_PEER_ADDR_RANDOM
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
        if (ble_gap_master.state != BLE_GAP_STATE_M_ACKED) {
            rc = BLE_HS_ENOENT;
        } else if (ble_gap_master.conn.addr_type == BLE_GAP_ADDR_TYPE_WL ||
                   (addr_type == ble_gap_master.conn.addr_type &&
                    memcmp(addr, ble_gap_master.conn.addr,
                           BLE_DEV_ADDR_LEN) == 0)) {
            rc = 0;
        } else {
            ble_gap_master_failed(BLE_HS_ECONTROLLER);
            rc = BLE_HS_ECONTROLLER;
        }
        break;

    default:
        assert(0);
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
 * Lock restrictions: None.
 *
 * @param addr_type             The address type of the peer; one of the
 *                                  following values:
 *                                  o    BLE_HCI_ADV_PEER_ADDR_PUBLIC
 *                                  o    BLE_HCI_ADV_PEER_ADDR_RANDOM
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

    if (!ble_gap_currently_advertising()) {
        rc = BLE_HS_ENOENT;
    } else {
        switch (ble_gap_slave.op) {
        case BLE_GAP_OP_NULL:
        case BLE_GAP_OP_S_NON:
            rc = BLE_HS_ENOENT;
            break;

        case BLE_GAP_OP_S_UND:
            rc = 0;
            break;

        case BLE_GAP_OP_S_DIR:
            if (ble_gap_slave.dir_addr_type != addr_type ||
                memcmp(ble_gap_slave.dir_addr, addr, BLE_DEV_ADDR_LEN) != 0) {

                rc = BLE_HS_ENOENT;
            } else {
                rc = 0;
            }
            break;

        default:
            assert(0);
            rc = BLE_HS_ENOENT;
            break;
        }
    }

    if (rc == 0) {
        STATS_INC(ble_gap_stats, connect_slv);
    }

    return rc;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gap_rx_adv_report(struct ble_hs_adv *adv)
{
#if !NIMBLE_OPT_ROLE_OBSERVER
    return;
#endif

    struct ble_hs_adv_fields fields;
    int rc;

    STATS_INC(ble_gap_stats, rx_adv_report);

    if (ble_gap_master.op      != BLE_GAP_OP_M_DISC ||
        ble_gap_master.state   != BLE_GAP_STATE_M_DISC_ACKED) {

        return;
    }

    rc = ble_hs_adv_parse_fields(&fields, adv->data, adv->length_data);
    if (rc != 0) {
        /* XXX: Increment stat. */
        return;
    }

    if (ble_gap_master.disc.disc_mode == BLE_GAP_DISC_MODE_LTD &&
        !(fields.flags & BLE_HS_ADV_F_DISC_LTD)) {

        return;
    }

    ble_gap_call_master_disc_cb(BLE_GAP_EVENT_DISC_SUCCESS, 0, adv,
                                     &fields, 0);
}

/**
 * Processes an incoming connection-complete HCI event.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
int
ble_gap_rx_conn_complete(struct hci_le_conn_complete *evt)
{
#if !NIMBLE_OPT_CONNECT
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gap_snapshot snap;
    struct ble_hs_conn *conn;
    int status;
    int rc;

    STATS_INC(ble_gap_stats, rx_conn_complete);

    /* Determine if this event refers to a completed connection or a connection
     * in progress.
     */
    ble_hs_conn_lock();
    conn = ble_hs_conn_find(evt->connection_handle);

    /* Apply the event to the existing connection if it exists. */
    if (conn != NULL) {
        /* XXX: Does this ever happen? */
        if (evt->status != 0) {
            ble_gap_conn_to_snapshot(conn, &snap);

            ble_gap_conn_broken(evt->connection_handle);

            ble_hs_conn_remove(conn);
            ble_hs_conn_free(conn);
        }
    }

    ble_hs_conn_unlock();

    if (conn != NULL) {
        if (evt->status != 0) {
            ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN, evt->status, &snap,
                                 NULL, NULL);
        }
        return 0;
    }

    /* This event refers to a new connection. */

    if (evt->status != BLE_ERR_SUCCESS) {
        status = BLE_HS_HCI_ERR(evt->status);

        /* Determine the role from the status code. */
        switch (evt->status) {
        case BLE_ERR_DIR_ADV_TMO:
            if (ble_gap_slave_in_progress()) {
                ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_FINISHED, 0, 1);
            }
            break;

        default:
            if (ble_gap_master_in_progress()) {
                ble_gap_master_failed(status);
            }
            break;
        }

        return 0;
    }

    switch (evt->role) {
    case BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER:
        rc = ble_gap_accept_master_conn(evt->peer_addr_type,
                                             evt->peer_addr);
        if (rc != 0) {
            return rc;
        }
        break;

    case BLE_HCI_LE_CONN_COMPLETE_ROLE_SLAVE:
        rc = ble_gap_accept_slave_conn(evt->peer_addr_type,
                                            evt->peer_addr);
        if (rc != 0) {
            return rc;
        }
        break;

    default:
        assert(0);
        break;
    }

    /* We verified that there is a free connection when the procedure began. */
    conn = ble_hs_conn_alloc();
    assert(conn != NULL);

    conn->bhc_handle = evt->connection_handle;
    memcpy(conn->bhc_addr, evt->peer_addr, sizeof conn->bhc_addr);
    conn->bhc_itvl = evt->conn_itvl;
    conn->bhc_latency = evt->conn_latency;
    conn->bhc_supervision_timeout = evt->supervision_timeout;
    if (evt->role == BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER) {
        conn->bhc_flags |= BLE_HS_CONN_F_MASTER;
        conn->bhc_cb = ble_gap_master.conn.cb;
        conn->bhc_cb_arg = ble_gap_master.conn.cb_arg;
        ble_gap_master_reset_state();
    } else {
        conn->bhc_cb = ble_gap_slave.cb;
        conn->bhc_cb_arg = ble_gap_slave.cb_arg;
        ble_gap_slave_reset_state();
    }

    ble_gap_conn_to_snapshot(conn, &snap);

    ble_hs_conn_insert(conn);
    ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN, 0, &snap, NULL, NULL);

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
int
ble_gap_rx_l2cap_update_req(uint16_t conn_handle,
                            struct ble_gap_upd_params *params)
{
    struct ble_gap_conn_ctxt ctxt;
    struct ble_gap_snapshot snap;
    int rc;

    ble_hs_misc_assert_no_locks();

    rc = ble_gap_find_snapshot(conn_handle, &snap);
    if (rc != 0) {
        return rc;
    }

    if (snap.cb != NULL) {
        ctxt.desc = &snap.desc;
        ctxt.peer_params = params;
        ctxt.self_params = NULL;
        rc = snap.cb(BLE_GAP_EVENT_L2CAP_UPDATE_REQ, 0, &ctxt, snap.cb_arg);
    } else {
        rc = 0;
    }

    return rc;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_master_timer_exp(void *arg)
{
    ble_hs_misc_assert_no_locks();

    assert(ble_gap_master_in_progress());

    switch (ble_gap_master.op) {
    case BLE_GAP_OP_M_DISC:
        /* When a discovery procedure times out, it is not a failure. */
        ble_gap_master_enqueue(BLE_GAP_STATE_M_DISC_DISABLE, 1,
                               ble_gap_disc_tx_disable, NULL);
        break;

    default:
        ble_gap_master_failed(BLE_HS_ETIMEOUT);
        break;
    }

}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_slave_timer_exp(void *arg)
{
    ble_hs_misc_assert_no_locks();

    assert(ble_gap_slave_in_progress());
    ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_FINISHED, 0, 1);
}

/*****************************************************************************
 * $white list                                                               *
 *****************************************************************************/

/**
 * XXX: This should be static, as it requires the private gap mutex to be
 * locked.  Currently only called by gap code and test code.
 *
 * Lock restrictions:
 *     o Caller locks gap.
 */
int
ble_gap_wl_busy(void)
{
#if !NIMBLE_OPT_WHITELIST
    return BLE_HS_ENOTSUP;
#endif

    /* Check if application is currently setting the white list. */
    if (ble_gap_wl.op != BLE_GAP_OP_NULL) {
        return 1;
    }

    /* Check if an auto or selective connection establishment procedure is in
     * progress.
     */
    if (ble_gap_master.op == BLE_GAP_OP_M_CONN &&
        ble_gap_master.conn.addr_type == BLE_GAP_ADDR_TYPE_WL) {

        return 1;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_wl_enqueue(uint8_t state, int in_progress,
                   ble_hci_sched_tx_fn *hci_tx_cb, void *cb_arg)
{
    int rc;

    ble_gap_wl.state = state;
    rc = ble_hci_sched_enqueue(hci_tx_cb, cb_arg, &ble_gap_wl.hci_handle);
    if (rc != 0) {
        if (in_progress) {
            ble_gap_call_wl_cb(rc, 1);
        } else {
            ble_gap_wl.op = BLE_GAP_OP_NULL;
        }
    }

    return rc;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_wl_ack_add(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_wl.op == BLE_GAP_OP_W_SET);
    assert(ble_gap_wl.state == BLE_GAP_STATE_W_ADD);
    assert(ble_gap_wl.hci_handle == ack->bha_hci_handle);

    ble_gap_wl.hci_handle = BLE_HCI_SCHED_HANDLE_NONE;

    if (ack->bha_status != 0) {
        ble_gap_call_wl_cb(ack->bha_status, 1);
        return;
    }

    ble_gap_wl.cur++;
    if (ble_gap_wl.cur < ble_gap_wl.count) {
        ble_gap_wl_enqueue(BLE_GAP_STATE_W_ADD, 1, ble_gap_wl_tx_add, NULL);
    } else {
        /* Success. */
        ble_gap_call_wl_cb(0, 1);
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_wl_tx_add(void *arg)
{
    struct ble_gap_white_entry *white_entry;
    int rc;

    assert(ble_gap_wl.op == BLE_GAP_OP_W_SET);
    assert(ble_gap_wl.state == BLE_GAP_STATE_W_ADD);
    assert(ble_gap_wl.entries != NULL);
    assert(ble_gap_wl.cur < ble_gap_wl.count);

    ble_hci_sched_set_ack_cb(ble_gap_wl_ack_add, NULL);

    white_entry = ble_gap_wl.entries + ble_gap_wl.cur;
    rc = host_hci_cmd_le_add_to_whitelist(white_entry->addr,
                                          white_entry->addr_type);
    if (rc != 0) {
        ble_gap_call_wl_cb(rc, 1);
        return 1;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_wl_ack_clear(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_wl.op == BLE_GAP_OP_W_SET);
    assert(ble_gap_wl.state == BLE_GAP_STATE_W_CLEAR);
    assert(ble_gap_wl.hci_handle == ack->bha_hci_handle);

    ble_gap_wl.hci_handle = BLE_HCI_SCHED_HANDLE_NONE;

    if (ack->bha_status != 0) {
        ble_gap_call_wl_cb(ack->bha_status, 1);
        return;
    }

    ble_gap_wl_enqueue(BLE_GAP_STATE_W_ADD, 1, ble_gap_wl_tx_add, NULL);
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_wl_tx_clear(void *arg)
{
    int rc;

    assert(ble_gap_wl.op == BLE_GAP_OP_W_SET);
    assert(ble_gap_wl.state == BLE_GAP_STATE_W_CLEAR);

    ble_hci_sched_set_ack_cb(ble_gap_wl_ack_clear, NULL);

    rc = host_hci_cmd_le_clear_whitelist();
    if (rc != 0) {
        ble_gap_call_wl_cb(rc, 1);
        return 1;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
int
ble_gap_wl_set(struct ble_gap_white_entry *white_list,
               uint8_t white_list_count, ble_gap_wl_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT_WHITELIST
    return BLE_HS_ENOTSUP;
#endif

    int rc;
    int i;

    if (white_list_count <= 0) {
        return BLE_HS_EINVAL;
    }

    for (i = 0; i < white_list_count; i++) {
        if (white_list[i].addr_type != BLE_ADDR_TYPE_PUBLIC &&
            white_list[i].addr_type != BLE_ADDR_TYPE_RANDOM) {

            return BLE_HS_EINVAL;
        }
    }

    rc = ble_gap_wl_set_op(BLE_GAP_OP_W_SET);
    if (rc != 0) {
        return rc;
    }

    ble_gap_wl.cb = cb;
    ble_gap_wl.cb_arg = cb_arg;
    ble_gap_wl.entries = white_list;
    ble_gap_wl.count = white_list_count;
    ble_gap_wl.cur = 0;

    rc = ble_gap_wl_enqueue(BLE_GAP_STATE_W_CLEAR, 0,
                            ble_gap_wl_tx_clear, NULL);
    if (rc != 0) {
        return rc;
    }

    STATS_INC(ble_gap_stats, wl_set);
    BLE_HS_LOG(INFO, "GAP procedure initiated: set whitelist; ");
    ble_gap_log_wl();
    BLE_HS_LOG(INFO, "\n");

    return 0;
}

/*****************************************************************************
 * $stop advertise                                                           *
 *****************************************************************************/

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_adv_ack_disable(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_slave.hci_handle == ack->bha_hci_handle);

    ble_gap_slave.hci_handle = BLE_HCI_SCHED_HANDLE_NONE;

    if (ack->bha_status == 0) {
        /* Advertising should now be aborted. */
        ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_FINISHED, 0, 1);
    } else {

        ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_STOP_FAILURE,
                              ack->bha_status, 0);
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_adv_disable_tx(void *arg)
{
    int rc;

    ble_hci_sched_set_ack_cb(ble_gap_adv_ack_disable, NULL);
    rc = host_hci_cmd_le_set_adv_enable(0);
    if (rc != BLE_ERR_SUCCESS) {
        ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_STOP_FAILURE,
                                   BLE_HS_HCI_ERR(rc), 0);
        return 1;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
int
ble_gap_adv_stop(void)
{
#if !NIMBLE_OPT_ADVERTISE
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    /* Do nothing if advertising is already disabled. */
    if (!ble_gap_currently_advertising()) {
        return BLE_HS_EALREADY;
    }

    rc = ble_hci_sched_enqueue(ble_gap_adv_disable_tx, NULL,
                               &ble_gap_slave.hci_handle);
    if (rc != 0) {
        return rc;
    }

    STATS_INC(ble_gap_stats, adv_stop);
    BLE_HS_LOG(INFO, "GAP procedure initiated: stop advertising; ");
    ble_gap_log_adv();
    BLE_HS_LOG(INFO, "\n");

    return 0;
}

/*****************************************************************************
 * $advertise                                                                *
 *****************************************************************************/

/**
 * Lock restrictions: None.
 */
static void
ble_gap_adv_itvls(uint8_t disc_mode, uint8_t conn_mode,
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
        assert(0);
        *out_itvl_min = BLE_GAP_ADV_FAST_INTERVAL2_MIN;
        *out_itvl_max = BLE_GAP_ADV_FAST_INTERVAL2_MAX;
        break;
    }
}

/**
 * Lock restrictions: None.
 */
static ble_hci_sched_tx_fn *
ble_gap_adv_get_dispatch(void)
{
    switch (ble_gap_slave.op) {
    case BLE_GAP_OP_S_NON:
        return ble_gap_dispatch_adv_und[ble_gap_slave.state];

    case BLE_GAP_OP_S_UND:
        return ble_gap_dispatch_adv_und[ble_gap_slave.state];

    case BLE_GAP_OP_S_DIR:
        return ble_gap_dispatch_adv_dir[ble_gap_slave.state];

    default:
        assert(0);
        return NULL;
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_adv_next_state(void)
{
    ble_hci_sched_tx_fn *tx_fn;
    int rc;

    ble_gap_slave.state++;
    tx_fn = ble_gap_adv_get_dispatch();
    if (tx_fn != NULL) {
        rc = ble_hci_sched_enqueue(tx_fn, NULL, &ble_gap_slave.hci_handle);
        if (rc != 0) {
            ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_FAILURE, rc, 1);
        }
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_adv_ack(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_slave.hci_handle == ack->bha_hci_handle);

    ble_gap_slave.hci_handle = BLE_HCI_SCHED_HANDLE_NONE;

    if (ack->bha_status != 0) {
        ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_FAILURE, ack->bha_status, 1);
    } else {
        ble_gap_adv_next_state();
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_adv_enable_tx(void *arg)
{
    int rc;

    ble_hci_sched_set_ack_cb(ble_gap_adv_ack, NULL);
    rc = host_hci_cmd_le_set_adv_enable(1);
    if (rc != BLE_ERR_SUCCESS) {
        ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_FAILURE,
                                   BLE_HS_HCI_ERR(rc), 1);
        return 1;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_adv_rsp_data_tx(void *arg)
{
    uint8_t rsp_data[BLE_HCI_MAX_SCAN_RSP_DATA_LEN] = { 0 }; /* XXX */
    int rc;

    ble_hci_sched_set_ack_cb(ble_gap_adv_ack, NULL);
    rc = host_hci_cmd_le_set_scan_rsp_data(rsp_data, sizeof rsp_data);
    if (rc != 0) {
        ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_FAILURE,
                                   BLE_HS_HCI_ERR(rc), 1);
        return 1;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_adv_data_tx(void *arg)
{
    uint8_t adv_data_len;
    uint8_t flags;
    int rc;

    assert(ble_gap_slave.op != BLE_GAP_OP_NULL);

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
        assert(0);
        break;
    }

    flags |= BLE_HS_ADV_F_BREDR_UNSUP;

    /* Encode the flags AD field if it is nonzero. */
    adv_data_len = ble_gap_slave.adv_data_len;
    if (flags != 0) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_FLAGS, 1, &flags,
                                 ble_gap_slave.adv_data, &adv_data_len,
                                 BLE_HCI_MAX_ADV_DATA_LEN);
        assert(rc == 0);
    }

    /* Encode the transmit power AD field. */
    rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_TX_PWR_LVL, 1,
                             &ble_gap_slave.tx_pwr_lvl,
                             ble_gap_slave.adv_data,
                             &adv_data_len, BLE_HCI_MAX_ADV_DATA_LEN);
    assert(rc == 0);

    ble_hci_sched_set_ack_cb(ble_gap_adv_ack, NULL);
    rc = host_hci_cmd_le_set_adv_data(ble_gap_slave.adv_data,
                                      adv_data_len);
    if (rc != 0) {
        ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_FAILURE,
                                   BLE_HS_HCI_ERR(rc), 1);
        return 1;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_adv_power_ack(struct ble_hci_ack *ack, void *arg)
{
    int8_t power_level;

    assert(ble_gap_slave.hci_handle == ack->bha_hci_handle);

    ble_gap_slave.hci_handle = BLE_HCI_SCHED_HANDLE_NONE;

    if (ack->bha_status != 0) {
        ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_FAILURE, ack->bha_status, 1);
        return;
    }

    if (ack->bha_params_len != BLE_HCI_ADV_CHAN_TXPWR_ACK_PARAM_LEN) {
        ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_FAILURE,
                              BLE_HS_ECONTROLLER, 1);
        return;
    }

    power_level = ack->bha_params[1];
    if (power_level < BLE_HCI_ADV_CHAN_TXPWR_MIN ||
        power_level > BLE_HCI_ADV_CHAN_TXPWR_MAX) {

        ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_FAILURE,
                              BLE_HS_ECONTROLLER, 1);
        return;
    }

    /* Save power level value so it can be put in the advertising data. */
    ble_gap_slave.tx_pwr_lvl = power_level;

    ble_gap_adv_next_state();
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_adv_power_tx(void *arg)
{
    int rc;

    ble_hci_sched_set_ack_cb(ble_gap_adv_power_ack, NULL);
    rc = host_hci_cmd_read_adv_pwr();
    if (rc != 0) {
        ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_FAILURE,
                              BLE_HS_HCI_ERR(rc), 1);
        return 1;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_adv_params_tx(void *arg)
{
    struct hci_adv_params hap;
    int rc;

    hap = ble_gap_slave.adv_params;

    switch (ble_gap_slave.op) {
    case BLE_GAP_OP_S_NON:
        hap.adv_type = BLE_HCI_ADV_TYPE_ADV_NONCONN_IND;
        break;

    case BLE_GAP_OP_S_DIR:
        hap.adv_type = BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD;
        memcpy(hap.peer_addr, ble_gap_slave.dir_addr,
               sizeof hap.peer_addr);
        break;

    case BLE_GAP_OP_S_UND:
        hap.adv_type = BLE_HCI_ADV_TYPE_ADV_IND;
        break;

    default:
        assert(0);
        break;
    }

    ble_hci_sched_set_ack_cb(ble_gap_adv_ack, NULL);
    rc = host_hci_cmd_le_set_adv_params(&hap);
    if (rc != 0) {
        ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_FAILURE,
                              BLE_HS_HCI_ERR(rc), 1);
        return 1;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_adv_initiate(void)
{
    int rc;

    rc = ble_hci_sched_enqueue(ble_gap_adv_params_tx, NULL,
                               &ble_gap_slave.hci_handle);
    if (rc != 0) {
        ble_gap_slave_reset_state();
        return rc;
    }

    return 0;
}

/**
 * Enables the specified discoverable mode and connectable mode, and initiates
 * the advertising process.
 *
 * Lock restrictions:
 *     o Caller unlocks gap.
 *
 * @param discoverable_mode     One of the following constants:
 *                                  o BLE_GAP_DISC_MODE_NON
 *                                      (non-discoverable; 3.C.9.2.2).
 *                                  o BLE_GAP_DISC_MODE_LTD
 *                                      (limited-discoverable; 3.C.9.2.3).
 *                                  o BLE_GAP_DISC_MODE_GEN
 *                                      (general-discoverable; 3.C.9.2.4).
 * @param connectable_mode      One of the following constants:
 *                                  o BLE_GAP_CONN_MODE_NON
 *                                      (non-connectable; 3.C.9.3.2).
 *                                  o BLE_GAP_CONN_MODE_DIR
 *                                      (directed-connectable; 3.C.9.3.3).
 *                                  o BLE_GAP_CONN_MODE_UND
 *                                      (undirected-connectable; 3.C.9.3.4).
 * @param peer_addr             The address of the peer who is allowed to
 *                                  connect; only meaningful for directed
 *                                  connectable mode.  For other modes, specify
 *                                  NULL.
 * @param peer_addr_type        The type of address specified for the
 *                                  "peer_addr" parameter; only meaningful for
 *                                  directed connectable mode.  For other
 *                                  modes, specify 0.  For directed connectable
 *                                  mode, this should be one of the following
 *                                  constants:
 *                                      o BLE_HCI_ADV_PEER_ADDR_PUBLIC
 *                                      o BLE_HCI_ADV_PEER_ADDR_RANDOM
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gap_adv_start(uint8_t discoverable_mode, uint8_t connectable_mode,
                  uint8_t *peer_addr, uint8_t peer_addr_type,
                  struct hci_adv_params *adv_params,
                  ble_gap_conn_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT_ADVERTISE
    return BLE_HS_ENOTSUP;
#endif

    uint8_t op;
    int rc;

    if (discoverable_mode >= BLE_GAP_DISC_MODE_MAX ||
        connectable_mode >= BLE_GAP_CONN_MODE_MAX) {

        return BLE_HS_EINVAL;
    }

    /* Don't initiate a connection procedure if we won't be able to allocate a
     * connection object on completion.
     */
    if (connectable_mode != BLE_GAP_CONN_MODE_NON &&
        !ble_hs_conn_can_alloc()) {

        return BLE_HS_ENOMEM;
    }

    switch (connectable_mode) {
    case BLE_GAP_CONN_MODE_NON:
        op = BLE_GAP_OP_S_NON;
        break;

    case BLE_GAP_CONN_MODE_UND:
        op = BLE_GAP_OP_S_UND;
        break;

    case BLE_GAP_CONN_MODE_DIR:
        if (peer_addr_type != BLE_ADDR_TYPE_PUBLIC &&
            peer_addr_type != BLE_ADDR_TYPE_RANDOM) {

            return BLE_HS_EINVAL;
        }

        op = BLE_GAP_OP_S_DIR;
        break;

    default:
        assert(0);
        break;
    }

    rc = ble_gap_slave_set_op(op);
    if (rc != 0) {
        return rc;
    }

    if (connectable_mode == BLE_GAP_CONN_MODE_DIR) {
        ble_gap_slave.dir_addr_type = peer_addr_type;
        memcpy(ble_gap_slave.dir_addr, peer_addr, 6);
    }

    ble_gap_slave.cb = cb;
    ble_gap_slave.cb_arg = cb_arg;
    ble_gap_slave.state = 0;
    ble_gap_slave.disc_mode = discoverable_mode;

    if (adv_params != NULL) {
        ble_gap_slave.adv_params = *adv_params;
    } else {
        ble_gap_slave.adv_params = ble_gap_adv_params_dflt;
    }

    ble_gap_adv_itvls(discoverable_mode, connectable_mode,
                      &ble_gap_slave.adv_params.adv_itvl_min,
                      &ble_gap_slave.adv_params.adv_itvl_max);

    rc = ble_gap_adv_initiate();
    if (rc != 0) {
        return rc;
    }

    STATS_INC(ble_gap_stats, adv_start);
    BLE_HS_LOG(INFO, "GAP procedure initiated: advertise; ");
    ble_gap_log_adv();
    BLE_HS_LOG(INFO, "\n");

    return 0;
}

/**
 * Lock restrictions: None.
 */
int
ble_gap_adv_set_fields(struct ble_hs_adv_fields *adv_fields)
{
#if !NIMBLE_OPT_ADVERTISE
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    STATS_INC(ble_gap_stats, adv_set_field);

    rc = ble_hs_adv_set_fields(adv_fields, ble_gap_slave.adv_data,
                               &ble_gap_slave.adv_data_len,
                               BLE_GAP_ADV_DATA_LIMIT);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $discovery procedures                                                     *
 *****************************************************************************/

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_disc_ack_disable(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_master.op == BLE_GAP_OP_M_DISC);
    assert(ble_gap_master.state == BLE_GAP_STATE_M_DISC_DISABLE);
    assert(ble_gap_master.hci_handle == ack->bha_hci_handle);

    ble_gap_master.hci_handle = BLE_HCI_SCHED_HANDLE_NONE;

    if (ack->bha_status != 0) {
        ble_gap_master_failed(ack->bha_status);
    } else {
        ble_gap_master_failed(0);
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_disc_tx_disable(void *arg)
{
    int rc;

    assert(ble_gap_master.op == BLE_GAP_OP_M_DISC);
    assert(ble_gap_master.state == BLE_GAP_STATE_M_DISC_DISABLE);

    ble_hci_sched_set_ack_cb(ble_gap_disc_ack_disable, NULL);
    rc = host_hci_cmd_le_set_scan_enable(0, 0);
    if (rc != 0) {
        /* XXX: What can we do? */
        ble_gap_master_failed(rc);
        return rc;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_disc_ack_enable(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_master.op == BLE_GAP_OP_M_DISC);
    assert(ble_gap_master.state == BLE_GAP_STATE_M_DISC_ENABLE);
    assert(ble_gap_master.hci_handle == ack->bha_hci_handle);

    ble_gap_master.hci_handle = BLE_HCI_SCHED_HANDLE_NONE;

    if (ack->bha_status != 0) {
        ble_gap_master_failed(ack->bha_status);
    } else {
        ble_gap_master.state = BLE_GAP_STATE_M_DISC_ACKED;
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_disc_tx_enable(void *arg)
{
    int rc;

    assert(ble_gap_master.op == BLE_GAP_OP_M_DISC);
    assert(ble_gap_master.state == BLE_GAP_STATE_M_DISC_ENABLE);

    ble_hci_sched_set_ack_cb(ble_gap_disc_ack_enable, NULL);
    rc = host_hci_cmd_le_set_scan_enable(1, 0);
    if (rc != 0) {
        ble_gap_master_failed(rc);
        return rc;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_disc_ack_params(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_master.op == BLE_GAP_OP_M_DISC);
    assert(ble_gap_master.state == BLE_GAP_STATE_M_DISC_PARAMS);
    assert(ble_gap_master.hci_handle == ack->bha_hci_handle);

    ble_gap_master.hci_handle = BLE_HCI_SCHED_HANDLE_NONE;

    if (ack->bha_status != 0) {
        ble_gap_master_failed(ack->bha_status);
        return;
    }

    ble_gap_master_enqueue(BLE_GAP_STATE_M_DISC_ENABLE, 1,
                                ble_gap_disc_tx_enable, NULL);
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_disc_tx_params(void *arg)
{
    int rc;

    assert(ble_gap_master.op == BLE_GAP_OP_M_DISC);
    assert(ble_gap_master.state == BLE_GAP_STATE_M_DISC_PARAMS);

    ble_hci_sched_set_ack_cb(ble_gap_disc_ack_params, NULL);
    rc = host_hci_cmd_le_set_scan_params(
        ble_gap_master.disc.scan_type,
        BLE_GAP_SCAN_FAST_INTERVAL_MIN,
        BLE_GAP_SCAN_FAST_WINDOW,
        BLE_HCI_ADV_OWN_ADDR_PUBLIC,
        ble_gap_master.disc.filter_policy);
    if (rc != 0) {
        ble_gap_master_failed(rc);
        return rc;
    }

    return 0;
}

/**
 * Performs the Limited or General Discovery Procedures, as described in
 * vol. 3, part C, section 9.2.5 / 9.2.6.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gap_disc(uint32_t duration_ms, uint8_t discovery_mode,
             uint8_t scan_type, uint8_t filter_policy,
             ble_gap_disc_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT_ROLE_OBSERVER
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    if (discovery_mode != BLE_GAP_DISC_MODE_LTD &&
        discovery_mode != BLE_GAP_DISC_MODE_GEN) {

        return BLE_HS_EINVAL;
    }

    if (scan_type != BLE_HCI_SCAN_TYPE_PASSIVE &&
        scan_type != BLE_HCI_SCAN_TYPE_ACTIVE) {

        return BLE_HS_EINVAL;
    }

    if (filter_policy > BLE_HCI_SCAN_FILT_MAX) {
        return BLE_HS_EINVAL;
    }

    if (duration_ms == 0) {
        duration_ms = BLE_GAP_GEN_DISC_SCAN_MIN;
    }

    rc = ble_gap_master_set_op(BLE_GAP_OP_M_DISC);
    if (rc != 0) {
        return rc;
    }

    ble_gap_master.disc.disc_mode = discovery_mode;
    ble_gap_master.disc.scan_type = scan_type;
    ble_gap_master.disc.filter_policy = filter_policy;
    ble_gap_master.disc.cb = cb;
    ble_gap_master.disc.cb_arg = cb_arg;
    rc = ble_gap_master_enqueue(BLE_GAP_STATE_M_DISC_PARAMS, 0,
                                ble_gap_disc_tx_params, NULL);
    if (rc != 0) {
        return rc;
    }

    os_callout_reset(&ble_gap_master_timer.cf_c,
                     duration_ms * OS_TICKS_PER_SEC / 1000);

    STATS_INC(ble_gap_stats, discover);
    BLE_HS_LOG(INFO, "GAP procedure initiated: discovery; ");
    ble_gap_log_disc();
    BLE_HS_LOG(INFO, "\n");

    return 0;
}

/*****************************************************************************
 * $connection establishment procedures                                      *
 *****************************************************************************/

/**
 * Processes an HCI acknowledgement (either command status or command complete)
 * while a master connection is being established.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_conn_create_ack(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_master.op == BLE_GAP_OP_M_CONN);
    assert(ble_gap_master.state == BLE_GAP_STATE_M_UNACKED);
    assert(ble_gap_master.hci_handle == ack->bha_hci_handle);

    ble_gap_master.hci_handle = BLE_HCI_SCHED_HANDLE_NONE;

    if (ack->bha_status != 0) {
        ble_gap_master_failed(ack->bha_status);
        return;
    }

    ble_gap_master.state = BLE_GAP_STATE_M_ACKED;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_conn_create_tx(void *arg)
{
    struct hci_create_conn hcc;
    int rc;

    assert(ble_gap_master.op == BLE_GAP_OP_M_CONN);
    assert(ble_gap_master.state == BLE_GAP_STATE_M_PENDING);

    hcc.scan_itvl = ble_gap_master.conn.params.scan_itvl;
    hcc.scan_window = ble_gap_master.conn.params.scan_window;

    if (ble_gap_master.conn.addr_type == BLE_GAP_ADDR_TYPE_WL) {
        hcc.filter_policy = BLE_HCI_CONN_FILT_USE_WL;
        hcc.peer_addr_type = BLE_HCI_ADV_PEER_ADDR_PUBLIC;
        memset(hcc.peer_addr, 0, sizeof hcc.peer_addr);
    } else {
        hcc.filter_policy = BLE_HCI_CONN_FILT_NO_WL;
        hcc.peer_addr_type = ble_gap_master.conn.addr_type;
        memcpy(hcc.peer_addr, ble_gap_master.conn.addr,
               sizeof hcc.peer_addr);
    }
    hcc.own_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
    hcc.conn_itvl_min = ble_gap_master.conn.params.itvl_min;
    hcc.conn_itvl_max = ble_gap_master.conn.params.itvl_max;
    hcc.conn_latency = ble_gap_master.conn.params.latency;
    hcc.supervision_timeout =
        ble_gap_master.conn.params.supervision_timeout;
    hcc.min_ce_len = ble_gap_master.conn.params.min_ce_len;
    hcc.max_ce_len = ble_gap_master.conn.params.max_ce_len;

    ble_gap_master.state = BLE_GAP_STATE_M_UNACKED;
    ble_hci_sched_set_ack_cb(ble_gap_conn_create_ack, NULL);

    rc = host_hci_cmd_le_create_connection(&hcc);
    if (rc != 0) {
        ble_gap_master_failed(rc);
        return 1;
    }

    return 0;
}

/**
 * Performs the Direct Connection Establishment Procedure, as described in
 * vol. 3, part C, section 9.3.8.
 *
 * @param addr_type             The peer's address type; one of:
 *                                  o BLE_HCI_CONN_PEER_ADDR_PUBLIC
 *                                  o BLE_HCI_CONN_PEER_ADDR_RANDOM
 *                                  o BLE_HCI_CONN_PEER_ADDR_PUBLIC_IDENT
 *                                  o BLE_HCI_CONN_PEER_ADDR_RANDOM_IDENT
 *                                  o BLE_GAP_ADDR_TYPE_WL
 * @param addr                  The address of the peer to connect to.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gap_conn_initiate(int addr_type, uint8_t *addr,
                      struct ble_gap_crt_params *params,
                      ble_gap_conn_fn *cb, void *cb_arg)
{
#if !NIMBLE_OPT_ROLE_CENTRAL
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    if (addr_type != BLE_ADDR_TYPE_PUBLIC &&
        addr_type != BLE_ADDR_TYPE_RANDOM &&
        addr_type != BLE_GAP_ADDR_TYPE_WL) {

        return BLE_HS_EINVAL;
    }

    rc = ble_gap_master_set_op(BLE_GAP_OP_M_CONN);
    if (rc != 0) {
        return rc;
    }

    if (params == NULL) {
        ble_gap_master.conn.params = ble_gap_params_dflt;
    } else {
        /* XXX: Verify params. */
        ble_gap_master.conn.params = *params;
    }

    ble_gap_master.conn.addr_type = addr_type;
    ble_gap_master.conn.cb = cb;
    ble_gap_master.conn.cb_arg = cb_arg;

    if (addr_type != BLE_GAP_ADDR_TYPE_WL) {
        memcpy(ble_gap_master.conn.addr, addr, BLE_DEV_ADDR_LEN);
    }

    rc = ble_gap_master_enqueue(BLE_GAP_STATE_M_PENDING, 0,
                                ble_gap_conn_create_tx, NULL);
    if (rc != 0) {
        return rc;
    }

    STATS_INC(ble_gap_stats, initiate);
    BLE_HS_LOG(INFO, "GAP procedure initiated: connect; ");
    ble_gap_log_conn();
    BLE_HS_LOG(INFO, "\n");

    return 0;
}

/*****************************************************************************
 * $terminate connection procedure                                           *
 *****************************************************************************/

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_terminate_ack(struct ble_hci_ack *ack, void *arg)
{
    struct ble_gap_snapshot snap;
    uint16_t handle;
    int rc;

    if (ack->bha_status != 0) {
        handle = (uintptr_t)arg;
        rc = ble_gap_find_snapshot(handle, &snap);
        if (rc == 0) {
            ble_gap_call_conn_cb(BLE_GAP_EVENT_TERM_FAILURE, ack->bha_status,
                                 &snap, NULL, NULL);
        }
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_terminate_tx(void *arg)
{
    struct ble_gap_snapshot snap;
    uint16_t handle;
    int status;
    int rc;

    handle = (uintptr_t)arg;

    ble_hci_sched_set_ack_cb(ble_gap_terminate_ack, arg);

    status = host_hci_cmd_disconnect(handle, BLE_ERR_REM_USER_CONN_TERM);
    if (status != 0) {
        rc = ble_gap_find_snapshot(handle, &snap);
        if (rc == 0) {
            ble_gap_call_conn_cb(BLE_GAP_EVENT_TERM_FAILURE, status, &snap,
                                 NULL, NULL);
        }
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
int
ble_gap_terminate(uint16_t conn_handle)
{
    int rc;

    if (!ble_hs_conn_exists(conn_handle)) {
        return BLE_HS_ENOENT;
    }

    rc = ble_hci_sched_enqueue(ble_gap_terminate_tx,
                               (void *)(uintptr_t)conn_handle, NULL);
    if (rc != 0) {
        return rc;
    }

    STATS_INC(ble_gap_stats, terminate);
    BLE_HS_LOG(INFO, "GAP procedure initiated: terminate connection; "
                     "conn_handle=%d\n", conn_handle);

    return 0;
}

/*****************************************************************************
 * $cancel                                                                   *
 *****************************************************************************/

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_cancel_ack(struct ble_hci_ack *ack, void *arg)
{
    if (ack->bha_status != 0) {
        ble_gap_call_master_conn_cb(BLE_GAP_EVENT_CANCEL_FAILURE,
                                    ack->bha_status, 0);
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_cancel_tx(void *arg)
{
    int rc;

    ble_hci_sched_set_ack_cb(ble_gap_cancel_ack, NULL);

    rc = host_hci_cmd_le_create_conn_cancel();
    if (rc != 0) {
        ble_gap_call_master_conn_cb(BLE_GAP_EVENT_CANCEL_FAILURE, rc, 0);
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
int
ble_gap_cancel(void)
{
    int rc;

    STATS_INC(ble_gap_stats, cancel);

    ble_gap_lock();

    if (!ble_gap_master_in_progress()) {
        rc = BLE_HS_ENOENT;
    } else {
        if (ble_gap_master.hci_handle != BLE_HCI_SCHED_HANDLE_NONE) {
            ble_hci_sched_cancel(ble_gap_master.hci_handle);
            ble_gap_master.hci_handle = BLE_HCI_SCHED_HANDLE_NONE;
        }

        rc = ble_hci_sched_enqueue(ble_gap_cancel_tx, NULL, NULL);
    }

    ble_gap_unlock();

    if (rc == 0) {
        BLE_HS_LOG(INFO, "GAP procedure initiated: cancel connetion\n");
    }

    return rc;
}

/*****************************************************************************
 * $update connection parameters                                             *
 *****************************************************************************/

/**
 * Lock restrictions:
 *     o Caller unlocks gap.
 */
static void
ble_gap_param_neg_reply_ack(struct ble_hci_ack *ack, void *arg)
{
    struct ble_gap_update_entry *entry;

    ble_gap_lock();

    entry = arg;
    assert(entry->state == BLE_GAP_STATE_U_NEG_REPLY);
    assert(entry->hci_handle == ack->bha_hci_handle);

    SLIST_REMOVE(&ble_gap_update_entries, entry,
                 ble_gap_update_entry, next);

    ble_gap_unlock();

    ble_gap_update_entry_free(entry);
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_param_reply_ack(struct ble_hci_ack *ack, void *arg)
{
    struct ble_gap_update_entry *entry;

    ble_gap_lock();

    entry = arg;
    assert(entry->state == BLE_GAP_STATE_U_REPLY);
    assert(entry->hci_handle == ack->bha_hci_handle);

    entry->hci_handle = BLE_HCI_SCHED_HANDLE_NONE;

    if (ack->bha_status != 0) {
        SLIST_REMOVE(&ble_gap_update_entries, entry,
                     ble_gap_update_entry, next);
    } else {
        entry->state = BLE_GAP_STATE_U_REPLY_ACKED;
    }

    ble_gap_unlock();

    if (ack->bha_status != 0) {
        ble_gap_update_failed(entry, ack->bha_status);
    }
}

static int
ble_gap_tx_param_pos_reply(void *arg)
{
    struct hci_conn_param_reply pos_reply;
    struct ble_gap_update_entry *entry;
    struct ble_gap_snapshot snap;
    int snap_rc;
    int rc;

    entry = arg;

    pos_reply.handle = entry->conn_handle;
    pos_reply.conn_itvl_min = entry->params.itvl_min;
    pos_reply.conn_itvl_max = entry->params.itvl_max;
    pos_reply.conn_latency = entry->params.latency;
    pos_reply.supervision_timeout = entry->params.supervision_timeout;
    pos_reply.min_ce_len = entry->params.min_ce_len;
    pos_reply.max_ce_len = entry->params.max_ce_len;

    ble_hci_sched_set_ack_cb(ble_gap_param_reply_ack, entry);

    rc = host_hci_cmd_le_conn_param_reply(&pos_reply);
    if (rc != 0) {
        rc = BLE_HS_HCI_ERR(rc);
        snap_rc = ble_gap_find_snapshot(entry->conn_handle, &snap);
        if (snap_rc == 0) {
            ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN_UPDATE_REQ, rc, &snap,
                                 NULL, NULL);
        }
    }

    return rc;
}

static int
ble_gap_tx_param_neg_reply(void *arg)
{
    struct hci_conn_param_neg_reply neg_reply;
    struct ble_gap_update_entry *entry;
    struct ble_gap_snapshot snap;
    int snap_rc;
    int rc;

    entry = arg;

    neg_reply.handle = entry->conn_handle;
    neg_reply.reason = entry->reject_reason;

    ble_hci_sched_set_ack_cb(ble_gap_param_neg_reply_ack, entry);
    rc = host_hci_cmd_le_conn_param_neg_reply(&neg_reply);
    if (rc != 0) {
        rc = BLE_HS_HCI_ERR(rc);
        snap_rc = ble_gap_find_snapshot(entry->conn_handle, &snap);
        if (snap_rc == 0) {
            ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN_UPDATE_REQ, rc, &snap,
                                 NULL, NULL);
        }
    }

    return rc;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gap_rx_param_req(struct hci_le_conn_param_req *evt)
{
#if !NIMBLE_OPT_CONNECT
    return;
#endif

    struct ble_gap_upd_params peer_params;
    struct ble_gap_update_entry *entry;
    struct ble_gap_snapshot snap;
    int rc;

    rc = ble_gap_find_snapshot(evt->connection_handle, &snap);
    if (rc != 0) {
        /* We are not connected to the sender. */
        return;
    }

    ble_gap_lock();

    entry = ble_gap_update_find(evt->connection_handle);
    if (entry != NULL) {
        /* Parameter update already in progress; replace existing request with
         * new one.
         */
        ble_gap_update_entry_remove_free(entry);
    }

    ble_gap_unlock();

    peer_params.itvl_min = evt->itvl_min;
    peer_params.itvl_max = evt->itvl_max;
    peer_params.latency = evt->latency;
    peer_params.supervision_timeout = evt->timeout;
    peer_params.min_ce_len = 0;
    peer_params.max_ce_len = 0;

    entry = ble_gap_update_entry_alloc(evt->connection_handle, &peer_params,
                                       BLE_GAP_STATE_U_REPLY);
    if (entry == NULL) {
        /* Out of memory; reject. */
        /* XXX: Our negative response should indicate which connection handle
         * it applies to, but we don't have anywhere to store this information.
         * For now, just send a connection handle of 0 and hope the peer can
         * sort it out.
         */
        rc = BLE_ERR_MEM_CAPACITY;
    } else {
        rc = ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN_UPDATE_REQ, 0, &snap,
                                  &entry->params, &peer_params);
        if (rc != 0) {
            entry->reject_reason = rc;
        }
    }

    if (rc == 0) {
        ble_hci_sched_enqueue(ble_gap_tx_param_pos_reply, entry,
                              &entry->hci_handle);
    }

    if (rc != 0) {
        if (entry != NULL) {
            entry->state = BLE_GAP_STATE_U_NEG_REPLY;
            ble_hci_sched_enqueue(ble_gap_tx_param_neg_reply, entry,
                                  &entry->hci_handle);
        } else {
            ble_hci_sched_enqueue(ble_gap_tx_param_neg_reply, NULL, NULL);
        }
    }

    if (entry != NULL) {
        ble_gap_lock();
        SLIST_INSERT_HEAD(&ble_gap_update_entries, entry, next);
        ble_gap_unlock();
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_update_ack(struct ble_hci_ack *ack, void *arg)
{
    struct ble_gap_update_entry *entry;

    ble_gap_lock();

    entry = arg;
    assert(entry->state == BLE_GAP_STATE_U_UPDATE);
    assert(entry->hci_handle == ack->bha_hci_handle);

    ble_gap_master.hci_handle = BLE_HCI_SCHED_HANDLE_NONE;

    if (ack->bha_status != 0) {
        SLIST_REMOVE(&ble_gap_update_entries, entry,
                     ble_gap_update_entry, next);
    } else {
        entry->state = BLE_GAP_STATE_U_UPDATE_ACKED;
    }

    ble_gap_unlock();

    if (ack->bha_status != 0) {
        ble_gap_update_failed(entry, ack->bha_status);
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_update_tx(void *arg)
{
    struct ble_gap_update_entry *entry;
    struct hci_conn_update cmd;
    int rc;

    ble_gap_lock();

    entry = arg;
    assert(entry->state == BLE_GAP_STATE_U_UPDATE);

    cmd.handle = entry->conn_handle;
    cmd.conn_itvl_min = entry->params.itvl_min;
    cmd.conn_itvl_max = entry->params.itvl_max;
    cmd.conn_latency = entry->params.latency;
    cmd.supervision_timeout = entry->params.supervision_timeout;
    cmd.min_ce_len = entry->params.min_ce_len;
    cmd.max_ce_len = entry->params.max_ce_len;

    ble_hci_sched_set_ack_cb(ble_gap_update_ack, entry);

    rc = host_hci_cmd_le_conn_update(&cmd);
    if (rc != 0) {
        SLIST_REMOVE(&ble_gap_update_entries, entry,
                     ble_gap_update_entry, next);
    }

    ble_gap_unlock();

    if (rc != 0) {
        ble_gap_update_failed(entry, rc);
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks ble_hs_conn.
 *     o Caller unlocks gap.
 */
int
ble_gap_update_params(uint16_t conn_handle, struct ble_gap_upd_params *params)
{
#if !NIMBLE_OPT_CONNECT
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gap_update_entry *entry;
    int rc;

    STATS_INC(ble_gap_stats, update);

    if (!ble_hs_conn_exists(conn_handle)) {
        return BLE_HS_ENOTCONN;
    }

    ble_gap_lock();
    entry = ble_gap_update_find(conn_handle);
    ble_gap_unlock();

    if (entry != NULL) {
        return BLE_HS_EALREADY;
    }

    entry = ble_gap_update_entry_alloc(conn_handle, params,
                                       BLE_GAP_STATE_U_UPDATE);
    if (entry == NULL) {
        return BLE_HS_ENOMEM;
    }

    entry->conn_handle = conn_handle;
    entry->params = *params;
    entry->state = BLE_GAP_STATE_U_UPDATE;

    rc = ble_hci_sched_enqueue(ble_gap_update_tx, entry, &entry->hci_handle);
    if (rc == 0) {
        BLE_HS_LOG(INFO, "GAP procedure initiated: ");
        ble_gap_log_update(entry);
        BLE_HS_LOG(INFO, "\n");

        ble_gap_lock();
        SLIST_INSERT_HEAD(&ble_gap_update_entries, entry, next);
        ble_gap_unlock();
    }

    return rc;
}

/*****************************************************************************
 * $init                                                                     *
 *****************************************************************************/

/**
 * Lock restrictions: None.
 */
int
ble_gap_init(void)
{
    int rc;

    memset(&ble_gap_master, 0, sizeof ble_gap_master);
    memset(&ble_gap_slave, 0, sizeof ble_gap_slave);
    memset(&ble_gap_wl, 0, sizeof ble_gap_wl);

    free(ble_gap_update_mem);
    ble_gap_update_mem = malloc(
        OS_MEMPOOL_BYTES(ble_hs_cfg.max_conn_update_entries,
                         sizeof (struct ble_gap_update_entry)));

    os_callout_func_init(&ble_gap_master_timer, &ble_hs_evq,
                         ble_gap_master_timer_exp, NULL);
    os_callout_func_init(&ble_gap_slave_timer, &ble_hs_evq,
                         ble_gap_slave_timer_exp, NULL);

    if (ble_hs_cfg.max_conn_update_entries > 0) {
        rc = os_mempool_init(&ble_gap_update_pool,
                             ble_hs_cfg.max_conn_update_entries,
                             sizeof (struct ble_gap_update_entry),
                             ble_gap_update_mem, "ble_gap_update_pool");
        if (rc != 0) {
            rc = BLE_HS_EOS;
            goto err;
        }
    }

    SLIST_INIT(&ble_gap_update_entries);

    rc = stats_init_and_reg(
        STATS_HDR(ble_gap_stats), STATS_SIZE_INIT_PARMS(ble_gap_stats,
        STATS_SIZE_32), STATS_NAME_INIT_PARMS(ble_gap_stats), "ble_gap");
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    return 0;

err:
    free(ble_gap_update_mem);
    ble_gap_update_mem = NULL;
    return rc;
}
