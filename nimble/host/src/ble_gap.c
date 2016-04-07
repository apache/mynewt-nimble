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
#include "ble_hs_priv.h"

#define BLE_GAP_OP_NULL                                 0
#define BLE_GAP_STATE_NULL                              255

#define BLE_GAP_OP_M_DISC                               1
#define BLE_GAP_OP_M_CONN                               2

#define BLE_GAP_OP_S_ADV                                1

/** Discovery master states. */
#define BLE_GAP_STATE_M_DISC_PENDING                    0
#define BLE_GAP_STATE_M_DISC_ACTIVE                     1
#define BLE_GAP_STATE_M_DISC_DISABLE                    2

/** Connect master states. */
#define BLE_GAP_STATE_M_CONN_PENDING                    0
#define BLE_GAP_STATE_M_CONN_ACTIVE                     1

/** Advertise slave states. */
#define BLE_GAP_STATE_S_ADV_PENDING                     0
#define BLE_GAP_STATE_S_ADV_ACTIVE                      1

/** Connection update states. */
#define BLE_GAP_STATE_U_UPDATE                          0
#define BLE_GAP_STATE_U_REPLY                           1
#define BLE_GAP_STATE_U_REPLY_ACKED                     2
#define BLE_GAP_STATE_U_NEG_REPLY                       3

/**
 * The maximum amount of user data that can be put into the advertising data.
 * The stack may automatically insert some fields on its own, limiting the
 * maximum amount of user data.  The following fields are automatically
 * inserted:
 *     o Flags (3 bytes)
 *     o Tx-power-level (3 bytes) - Only if the application specified a
 *       tx_pwr_llvl_present value of 1 in a call to ble_gap_set_adv_data().
 */
#define BLE_GAP_ADV_DATA_LIMIT_PWR      (BLE_HCI_MAX_ADV_DATA_LEN - 6)
#define BLE_GAP_ADV_DATA_LIMIT_NO_PWR   (BLE_HCI_MAX_ADV_DATA_LEN - 3)

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

    unsigned exp_set:1;
    uint32_t exp_os_ticks;

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

    uint8_t conn_mode;
    uint8_t state;
    uint8_t disc_mode;
    ble_gap_conn_fn *cb;
    void *cb_arg;

    uint8_t dir_addr_type;
    uint8_t dir_addr[BLE_DEV_ADDR_LEN];

    struct hci_adv_params adv_params;
    struct hci_adv_params rsp_params;
    uint8_t adv_data[BLE_HCI_MAX_ADV_DATA_LEN];
    uint8_t rsp_data[BLE_HCI_MAX_ADV_DATA_LEN];
    uint8_t adv_data_len;
    uint8_t rsp_data_len;
    int8_t tx_pwr_lvl;

    unsigned adv_pwr_lvl:1;
} ble_gap_slave;

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

static int ble_gap_disc_tx_disable(void *arg);

struct ble_gap_snapshot {
    struct ble_gap_conn_desc desc;
    ble_gap_conn_fn *cb;
    void *cb_arg;
};

static struct os_mutex ble_gap_mutex;

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
        BLE_HS_DBG_ASSERT_EVAL(owner != os_sched_get_current_task());
    }

    rc = os_mutex_pend(&ble_gap_mutex, 0xffffffff);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0 || rc == OS_NOT_STARTED);
}

static void
ble_gap_unlock(void)
{
    int rc;

    rc = os_mutex_release(&ble_gap_mutex);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0 || rc == OS_NOT_STARTED);
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
ble_gap_log_wl(struct ble_gap_white_entry *white_list,
               uint8_t white_list_count)
{
    struct ble_gap_white_entry *entry;
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
ble_gap_log_adv(void)
{
    BLE_HS_LOG(INFO, "disc_mode=%d addr_type=%d addr=",
               ble_gap_slave.disc_mode, ble_gap_slave.dir_addr_type);
    BLE_HS_LOG_ADDR(INFO, ble_gap_slave.dir_addr);
    BLE_HS_LOG(INFO, " adv_type=%d adv_channel_map=%d own_addr_type=%d "
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
    ble_gap_master.state = BLE_GAP_STATE_NULL;
    ble_gap_master.op = BLE_GAP_OP_NULL;
}

/**
 * Lock restrictions:
 *     o Caller locks gap.
 */
static void
ble_gap_slave_reset_state(void)
{
    ble_gap_slave.state = BLE_GAP_STATE_NULL;
    ble_gap_slave.op = BLE_GAP_OP_NULL;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_call_conn_cb(int event, int status, struct ble_gap_conn_ctxt *ctxt,
                     ble_gap_conn_fn *cb, void *cb_arg)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    if (cb != NULL) {
        rc = cb(event, status, ctxt, cb_arg);
    } else {
        if (event == BLE_GAP_EVENT_CONN_UPDATE_REQ) {
            /* Just copy peer parameters back into reply. */
            *ctxt->update.self_params = *ctxt->update.peer_params;
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
        memset(&ctxt, 0, sizeof ctxt);
        ctxt.desc = &desc;

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
        memset(&ctxt, 0, sizeof ctxt);
        ctxt.desc = &desc;

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
ble_gap_update_entry_alloc(void)
{
    struct ble_gap_update_entry *entry;

    entry = os_memblock_get(&ble_gap_update_pool);
    if (entry == NULL) {
        return NULL;
    }

    memset(entry, 0, sizeof *entry);

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
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static void
ble_gap_update_notify(struct ble_gap_update_entry *entry, int status)
{
    struct ble_gap_conn_ctxt ctxt;
    struct ble_gap_snapshot snap;
    int rc;

    rc = ble_gap_find_snapshot(entry->conn_handle, &snap);
    if (rc != 0) {
        return;
    }

    memset(&ctxt, 0, sizeof ctxt);
    ctxt.desc = &snap.desc;
    ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN_UPDATED, status, &ctxt,
                         snap.cb, snap.cb_arg);
}

static void
ble_gap_master_set_timer(uint32_t ms_from_now)
{
    ble_gap_master.exp_os_ticks =
        os_time_get() + ms_from_now * OS_TICKS_PER_SEC / 1000;
    ble_gap_master.exp_set = 1;
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
        STATS_INC(ble_gap_stats, discover_fail);
        ble_gap_call_master_disc_cb(BLE_GAP_EVENT_DISC_FINISHED, status,
                                    NULL, NULL, 1);
        break;

    case BLE_GAP_OP_M_CONN:
        STATS_INC(ble_gap_stats, initiate_fail);
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
    STATS_INC(ble_gap_stats, update_fail);
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
    ble_l2cap_sm_connection_broken(conn_handle);

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

    struct ble_gap_conn_ctxt ctxt;
    struct ble_gap_snapshot snap;
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_misc_assert_no_locks();

    STATS_INC(ble_gap_stats, rx_disconnect);

    if (evt->status == 0) {
        /* Find the connection that this event refers to. */
        ble_hs_conn_lock();
        conn = ble_hs_conn_find(evt->connection_handle);
        if (conn != NULL) {
            ble_gap_conn_to_snapshot(conn, &snap);
            ble_hs_conn_remove(conn);
            ble_hs_conn_free(conn);
        }
        ble_hs_conn_unlock();

        if (conn != NULL) {
            ble_gap_conn_broken(evt->connection_handle);

            memset(&ctxt, 0, sizeof ctxt);
            ctxt.desc = &snap.desc;
            ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN, BLE_HS_ENOTCONN, &ctxt,
                                 snap.cb, snap.cb_arg);
        }
    } else {
        rc = ble_gap_find_snapshot(evt->connection_handle, &snap);
        if (rc == 0) {
            memset(&ctxt, 0, sizeof ctxt);
            ctxt.desc = &snap.desc;
            ble_gap_call_conn_cb(BLE_GAP_EVENT_TERM_FAILURE,
                                 BLE_HS_HCI_ERR(evt->status), &ctxt,
                                 snap.cb, snap.cb_arg);
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
    struct ble_gap_conn_ctxt ctxt;
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
        memset(&ctxt, 0, sizeof ctxt);
        ctxt.desc = &snap.desc;
        ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN_UPDATED,
                             BLE_HS_HCI_ERR(evt->status), &ctxt,
                             snap.cb, snap.cb_arg);
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
 * Lock restrictions: None.
 */
static int
ble_gap_currently_advertising(void)
{
    return ble_gap_slave.op == BLE_GAP_OP_S_ADV;
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
        if (ble_gap_master.state != BLE_GAP_STATE_M_CONN_ACTIVE) {
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
 * Lock restrictions: None.
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

    if (!ble_gap_currently_advertising()) {
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
            if (ble_gap_slave.dir_addr_type != addr_type ||
                memcmp(ble_gap_slave.dir_addr, addr, BLE_DEV_ADDR_LEN) != 0) {

                rc = BLE_HS_ENOENT;
            } else {
                rc = 0;
            }
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
        ble_gap_master.state   != BLE_GAP_STATE_M_DISC_ACTIVE) {

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

    struct ble_gap_conn_ctxt ctxt;
    struct ble_gap_snapshot snap;
    struct ble_hs_conn *conn;
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
            memset(&ctxt, 0, sizeof ctxt);
            ctxt.desc = &snap.desc;
            ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN, evt->status, &ctxt,
                                 snap.cb, snap.cb_arg);
        }
        return 0;
    }

    /* This event refers to a new connection. */

    if (evt->status != BLE_ERR_SUCCESS) {
        /* Determine the role from the status code. */
        switch (evt->status) {
        case BLE_ERR_DIR_ADV_TMO:
            if (ble_gap_slave_in_progress()) {
                ble_gap_call_slave_cb(BLE_GAP_EVENT_ADV_FINISHED, 0, 1);
            }
            break;

        default:
            if (ble_gap_master_in_progress()) {
                if (evt->status == BLE_ERR_UNK_CONN_ID) {
                    /* Connect procedure successfully cancelled. */
                    ble_gap_call_master_conn_cb(BLE_GAP_EVENT_CANCEL, 0, 1);
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

    memset(&ctxt, 0, sizeof ctxt);
    ctxt.desc = &snap.desc;
    ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN, 0, &ctxt, snap.cb, snap.cb_arg);

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
        memset(&ctxt, 0, sizeof ctxt);
        ctxt.desc = &snap.desc;
        ctxt.update.peer_params = params;
        rc = snap.cb(BLE_GAP_EVENT_L2CAP_UPDATE_REQ, 0, &ctxt, snap.cb_arg);
    } else {
        rc = 0;
    }

    return rc;
}

/**
 * Called by the ble_hs heartbeat timer.  Handles timed out master procedures.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_gap_heartbeat(void)
{
    int timer_expired;
    int rc;

    if (ble_gap_master.op != BLE_GAP_OP_NULL &&
        ble_gap_master.exp_set &&
        (int32_t)(os_time_get() - ble_gap_master.exp_os_ticks) >= 0) {

        timer_expired = 1;

        /* Clear the timer. */
        ble_gap_master.exp_set = 0;
    } else {
        timer_expired = 0;
    }

    if (timer_expired) {
        switch (ble_gap_master.op) {
        case BLE_GAP_OP_M_DISC:
            /* When a discovery procedure times out, it is not a failure. */
            ble_gap_master.state = BLE_GAP_STATE_M_DISC_DISABLE;
            rc = ble_hci_sched_enqueue(ble_gap_disc_tx_disable, NULL, NULL);
            if (rc != 0) {
                ble_gap_master_failed(rc);
            }
            break;

        default:
            ble_gap_master_failed(BLE_HS_ETIMEOUT);
            break;
        }
    }
}

/*****************************************************************************
 * $white list                                                               *
 *****************************************************************************/

/**
 * Lock restrictions: None.
 */
static int
ble_gap_wl_busy(void)
{
#if !NIMBLE_OPT_WHITELIST
    return BLE_HS_ENOTSUP;
#endif

    /* Check if an auto or selective connection establishment procedure is in
     * progress.
     */
    return ble_gap_master.op == BLE_GAP_OP_M_CONN &&
           ble_gap_master.conn.addr_type == BLE_GAP_ADDR_TYPE_WL;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_wl_tx_add(struct ble_gap_white_entry *entry)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_CHG_WHITE_LIST_LEN];
    int rc;

    rc = host_hci_cmd_build_le_add_to_whitelist(entry->addr, entry->addr_type,
                                                buf, sizeof buf);
    if (rc != 0) {
        return rc;
    }

    rc = ble_hci_block_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_wl_tx_clear(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN];
    int rc;

    host_hci_cmd_le_build_clear_whitelist(buf, sizeof buf);
    rc = ble_hci_block_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
int
ble_gap_wl_set(struct ble_gap_white_entry *white_list,
               uint8_t white_list_count)
{
#if !NIMBLE_OPT_WHITELIST
    return BLE_HS_ENOTSUP;
#endif

    int rc;
    int i;

    STATS_INC(ble_gap_stats, wl_set);

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

done:
    if (rc == 0) {
        BLE_HS_LOG(INFO, "GAP procedure initiated: set whitelist; ");
        ble_gap_log_wl(white_list, white_list_count);
        BLE_HS_LOG(INFO, "\n");
    } else {
        STATS_INC(ble_gap_stats, wl_set_fail);
    }

    return rc;
}

/*****************************************************************************
 * $stop advertise                                                           *
 *****************************************************************************/

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_adv_disable_tx(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_ADV_ENABLE_LEN];
    int rc;

    host_hci_cmd_build_le_set_adv_enable(0, buf, sizeof buf);
    rc = ble_hci_block_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
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

    STATS_INC(ble_gap_stats, adv_stop);

    /* Do nothing if advertising is already disabled. */
    if (!ble_gap_currently_advertising()) {
        rc = BLE_HS_EALREADY;
        goto done;
    }

    rc = ble_gap_adv_disable_tx();
    if (rc != 0) {
        goto done;
    }

    ble_gap_slave_reset_state();

done:
    if (rc == 0) {
        BLE_HS_LOG(INFO, "GAP procedure initiated: stop advertising; ");
        ble_gap_log_adv();
        BLE_HS_LOG(INFO, "\n");
    } else {
        STATS_INC(ble_gap_stats, adv_set_fields_fail);
    }

    return rc;
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
        BLE_HS_DBG_ASSERT(0);
        break;
    }
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_adv_enable_tx(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_ADV_PARAM_LEN];
    int rc;

    host_hci_cmd_build_le_set_adv_enable(1, buf, sizeof buf);

    rc = ble_hci_block_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
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

    rc = ble_hci_block_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_adv_data_tx(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_ADV_DATA_LEN];
    uint8_t adv_data_len;
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

    /* Encode the flags AD field if it is nonzero. */
    adv_data_len = ble_gap_slave.adv_data_len;
    if (flags != 0) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_FLAGS, 1, &flags,
                                 ble_gap_slave.adv_data, &adv_data_len,
                                 BLE_HCI_MAX_ADV_DATA_LEN);
        BLE_HS_DBG_ASSERT(rc == 0);
    }

    /* Encode the transmit power AD field. */
    if (ble_gap_slave.adv_pwr_lvl) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_TX_PWR_LVL, 1,
                                 &ble_gap_slave.tx_pwr_lvl,
                                 ble_gap_slave.adv_data,
                                 &adv_data_len, BLE_HCI_MAX_ADV_DATA_LEN);
        BLE_HS_DBG_ASSERT(rc == 0);
    }

    rc = host_hci_cmd_build_le_set_adv_data(ble_gap_slave.adv_data,
                                            adv_data_len, buf, sizeof buf);
    if (rc != 0) {
        return rc;
    }

    rc = ble_hci_block_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_adv_power_tx(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN];
    struct ble_hci_block_result result;
    uint8_t tx_pwr;
    int rc;

    host_hci_cmd_build_read_adv_pwr(buf, sizeof buf);
    rc = ble_hci_block_tx(buf, &tx_pwr, 1, &result);
    if (rc != 0) {
        return rc;
    }

    if (result.evt_total_len != 1           ||
        tx_pwr < BLE_HCI_ADV_CHAN_TXPWR_MIN ||
        tx_pwr > BLE_HCI_ADV_CHAN_TXPWR_MAX) {

        return BLE_HS_ECONTROLLER;
    }

    ble_gap_slave.tx_pwr_lvl = tx_pwr;

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_adv_params_tx(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_ADV_PARAM_LEN];
    struct hci_adv_params hap;
    int rc;

    hap = ble_gap_slave.adv_params;

    switch (ble_gap_slave.conn_mode) {
    case BLE_GAP_CONN_MODE_NON:
        hap.adv_type = BLE_HCI_ADV_TYPE_ADV_NONCONN_IND;
        break;

    case BLE_GAP_CONN_MODE_DIR:
        hap.adv_type = BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD;
        memcpy(hap.peer_addr, ble_gap_slave.dir_addr, sizeof hap.peer_addr);
        break;

    case BLE_GAP_CONN_MODE_UND:
        hap.adv_type = BLE_HCI_ADV_TYPE_ADV_IND;
        break;

    default:
        BLE_HS_DBG_ASSERT(0);
        break;
    }

    rc = host_hci_cmd_build_le_set_adv_params(&hap, buf, sizeof buf);
    if (rc != 0) {
        return rc;
    }

    rc = ble_hci_block_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
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
 *                                      o BLE_ADDR_TYPE_PUBLIC
 *                                      o BLE_ADDR_TYPE_RANDOM
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

    int rc;

    STATS_INC(ble_gap_stats, adv_start);

    if (discoverable_mode >= BLE_GAP_DISC_MODE_MAX) {
        rc = BLE_HS_EINVAL;
        goto done;
    }

    /* Don't initiate a connection procedure if we won't be able to allocate a
     * connection object on completion.
     */
    if (connectable_mode != BLE_GAP_CONN_MODE_NON &&
        !ble_hs_conn_can_alloc()) {

        rc = BLE_HS_ENOMEM;
        goto done;
    }

    switch (connectable_mode) {
    case BLE_GAP_CONN_MODE_NON:
    case BLE_GAP_CONN_MODE_UND:
        break;

    case BLE_GAP_CONN_MODE_DIR:
        if (peer_addr_type != BLE_ADDR_TYPE_PUBLIC &&
            peer_addr_type != BLE_ADDR_TYPE_RANDOM) {

            rc = BLE_HS_EINVAL;
            goto done;
        }
        break;

    default:
        rc = BLE_HS_EINVAL;
        goto done;
    }

    rc = ble_gap_slave_set_op(BLE_GAP_OP_S_ADV);
    if (rc != 0) {
        goto done;
    }
    ble_gap_slave.state = BLE_GAP_STATE_S_ADV_PENDING;

    if (connectable_mode == BLE_GAP_CONN_MODE_DIR) {
        ble_gap_slave.dir_addr_type = peer_addr_type;
        memcpy(ble_gap_slave.dir_addr, peer_addr, 6);
    }

    ble_gap_slave.cb = cb;
    ble_gap_slave.cb_arg = cb_arg;
    ble_gap_slave.state = 0;
    ble_gap_slave.conn_mode = connectable_mode;
    ble_gap_slave.disc_mode = discoverable_mode;

    if (adv_params != NULL) {
        ble_gap_slave.adv_params = *adv_params;
    } else {
        ble_gap_slave.adv_params = ble_gap_adv_params_dflt;
    }

    ble_gap_adv_itvls(discoverable_mode, connectable_mode,
                      &ble_gap_slave.adv_params.adv_itvl_min,
                      &ble_gap_slave.adv_params.adv_itvl_max);

    rc = ble_gap_adv_params_tx();
    if (rc != 0) {
        goto done;
    }

    if (ble_gap_slave.adv_pwr_lvl) {
        rc = ble_gap_adv_power_tx();
        if (rc != 0) {
            goto done;
        }
    }

    if (ble_gap_slave.conn_mode != BLE_GAP_CONN_MODE_DIR) {
        rc = ble_gap_adv_data_tx();
        if (rc != 0) {
            goto done;
        }

        rc = ble_gap_adv_rsp_data_tx();
        if (rc != 0) {
            goto done;
        }
    }

    rc = ble_gap_adv_enable_tx();
    if (rc != 0) {
        goto done;
    }

    ble_gap_slave.state = BLE_GAP_STATE_S_ADV_ACTIVE;

done:
    if (rc == 0) {
        BLE_HS_LOG(INFO, "GAP procedure initiated: advertise; ");
        ble_gap_log_adv();
        BLE_HS_LOG(INFO, "\n");
    } else {
        STATS_INC(ble_gap_stats, adv_start_fail);
        if (rc != BLE_HS_EALREADY) {
            ble_gap_slave_reset_state();
        }
    }

    return rc;
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

    int max_sz;
    int rc;

    STATS_INC(ble_gap_stats, adv_set_fields);

    if (adv_fields->tx_pwr_lvl_is_present) {
        max_sz = BLE_GAP_ADV_DATA_LIMIT_PWR;
    } else {
        max_sz = BLE_GAP_ADV_DATA_LIMIT_NO_PWR;
    }

    rc = ble_hs_adv_set_fields(adv_fields, ble_gap_slave.adv_data,
                               &ble_gap_slave.adv_data_len, max_sz);
    if (rc == 0) {
        ble_gap_slave.adv_pwr_lvl = adv_fields->tx_pwr_lvl_is_present;
    } else {
        STATS_INC(ble_gap_stats, adv_set_fields_fail);
    }

    return rc;
}

/**
 * Lock restrictions: None.
 */
int
ble_gap_adv_rsp_set_fields(struct ble_hs_adv_fields *rsp_fields)
{
#if !NIMBLE_OPT_ADVERTISE
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    STATS_INC(ble_gap_stats, adv_rsp_set_fields);

    rc = ble_hs_adv_set_fields(rsp_fields, ble_gap_slave.rsp_data,
                               &ble_gap_slave.rsp_data_len,
                               BLE_HCI_MAX_ADV_DATA_LEN);
    if (rc != 0) {
        STATS_INC(ble_gap_stats, adv_rsp_set_fields_fail);
    }

    return rc;
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
    BLE_HS_DBG_ASSERT(ble_gap_master.op == BLE_GAP_OP_M_DISC);
    BLE_HS_DBG_ASSERT(ble_gap_master.state == BLE_GAP_STATE_M_DISC_DISABLE);

    if (ack->bha_status != 0) {
        ble_gap_master_failed(ack->bha_status);
    } else {
        ble_gap_call_master_disc_cb(BLE_GAP_EVENT_DISC_FINISHED, 0,
                                    NULL, NULL, 1);
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

    BLE_HS_DBG_ASSERT(ble_gap_master.op == BLE_GAP_OP_M_DISC);
    BLE_HS_DBG_ASSERT(ble_gap_master.state == BLE_GAP_STATE_M_DISC_DISABLE);

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
static int
ble_gap_disc_tx_enable(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_SCAN_ENABLE_LEN];
    int rc;

    host_hci_cmd_build_le_set_scan_enable(1, 0, buf, sizeof buf);
    rc = ble_hci_block_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_disc_tx_params(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_SCAN_PARAM_LEN];
    int rc;

    BLE_HS_DBG_ASSERT(ble_gap_master.op == BLE_GAP_OP_M_DISC);
    BLE_HS_DBG_ASSERT(ble_gap_master.state == BLE_GAP_STATE_M_DISC_PENDING);

    rc = host_hci_cmd_build_le_set_scan_params(
        ble_gap_master.disc.scan_type,
        BLE_GAP_SCAN_FAST_INTERVAL_MIN,
        BLE_GAP_SCAN_FAST_WINDOW,
        BLE_HCI_ADV_OWN_ADDR_PUBLIC,
        ble_gap_master.disc.filter_policy,
        buf, sizeof buf);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

    rc = ble_hci_block_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
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

    STATS_INC(ble_gap_stats, discover);

    if (discovery_mode != BLE_GAP_DISC_MODE_LTD &&
        discovery_mode != BLE_GAP_DISC_MODE_GEN) {

        rc = BLE_HS_EINVAL;
        goto done;
    }

    if (scan_type != BLE_HCI_SCAN_TYPE_PASSIVE &&
        scan_type != BLE_HCI_SCAN_TYPE_ACTIVE) {

        rc = BLE_HS_EINVAL;
        goto done;
    }

    if (filter_policy > BLE_HCI_SCAN_FILT_MAX) {
        rc = BLE_HS_EINVAL;
        goto done;
    }

    if (duration_ms == 0) {
        duration_ms = BLE_GAP_GEN_DISC_SCAN_MIN;
    }

    rc = ble_gap_master_set_op(BLE_GAP_OP_M_DISC);
    if (rc != 0) {
        goto done;
    }

    ble_gap_master.disc.disc_mode = discovery_mode;
    ble_gap_master.disc.scan_type = scan_type;
    ble_gap_master.disc.filter_policy = filter_policy;
    ble_gap_master.disc.cb = cb;
    ble_gap_master.disc.cb_arg = cb_arg;

    rc = ble_gap_disc_tx_params();
    if (rc != 0) {
        goto done;
    }

    rc = ble_gap_disc_tx_enable();
    if (rc != 0) {
        goto done;
    }

    ble_gap_master_set_timer(duration_ms);

done:
    if (rc == 0) {
        BLE_HS_LOG(INFO, "GAP procedure initiated: discovery; ");
        ble_gap_log_disc();
        BLE_HS_LOG(INFO, "\n");
    } else {
        if (rc != BLE_HS_EALREADY) {
            ble_gap_master_reset_state();
        }
        STATS_INC(ble_gap_stats, discover_fail);
    }

    return rc;
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

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
static int
ble_gap_conn_create_tx(void *arg)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_CREATE_CONN_LEN];
    struct hci_create_conn hcc;
    int rc;

    BLE_HS_DBG_ASSERT(ble_gap_master.op == BLE_GAP_OP_M_CONN);

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

    rc = host_hci_cmd_build_le_create_connection(&hcc, buf, sizeof buf);
    if (rc != 0) {
        return BLE_HS_EUNKNOWN;
    }

    rc = ble_hci_block_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    ble_gap_master.state = BLE_GAP_STATE_M_CONN_ACTIVE;

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

    STATS_INC(ble_gap_stats, initiate);

    if (addr_type != BLE_HCI_CONN_PEER_ADDR_PUBLIC &&
        addr_type != BLE_HCI_CONN_PEER_ADDR_RANDOM &&
        addr_type != BLE_GAP_ADDR_TYPE_WL) {

        rc = BLE_HS_EINVAL;
        goto done;
    }

    rc = ble_gap_master_set_op(BLE_GAP_OP_M_CONN);
    if (rc != 0) {
        goto done;
    }
    ble_gap_master.state = BLE_GAP_STATE_M_CONN_PENDING;

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

    rc = ble_gap_conn_create_tx(NULL);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc == 0) {
        BLE_HS_LOG(INFO, "GAP procedure initiated: connect; ");
        ble_gap_log_conn();
        BLE_HS_LOG(INFO, "\n");
    } else {
        if (rc != BLE_HS_EALREADY) {
            ble_gap_master_reset_state();
        }
        STATS_INC(ble_gap_stats, initiate_fail);
    }

    return rc;
}

/*****************************************************************************
 * $terminate connection procedure                                           *
 *****************************************************************************/

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
int
ble_gap_terminate(uint16_t conn_handle)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_DISCONNECT_CMD_LEN];
    int rc;

    STATS_INC(ble_gap_stats, terminate);

    if (!ble_hs_conn_exists(conn_handle)) {
        rc = BLE_HS_ENOTCONN;
        goto done;
    }

    host_hci_cmd_build_disconnect(conn_handle, BLE_ERR_REM_USER_CONN_TERM,
                                  buf, sizeof buf);
    rc = ble_hci_block_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc == 0) {
        BLE_HS_LOG(INFO, "GAP procedure initiated: terminate connection; "
                         "conn_handle=%d\n", conn_handle);
    } else {
        STATS_INC(ble_gap_stats, terminate_fail);
    }

    return rc;
}

/*****************************************************************************
 * $cancel                                                                   *
 *****************************************************************************/

/**
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
int
ble_gap_cancel(void)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN];
    int rc;

    STATS_INC(ble_gap_stats, cancel);

    if (!ble_gap_master_in_progress()) {
        rc = BLE_HS_ENOENT;
        goto done;
    }

    host_hci_cmd_build_le_create_conn_cancel(buf, sizeof buf);
    rc = ble_hci_block_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc == 0) {
        BLE_HS_LOG(INFO, "GAP procedure initiated: cancel connection\n");
    } else {
        STATS_INC(ble_gap_stats, cancel_fail);
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
    BLE_HS_DBG_ASSERT(entry->state == BLE_GAP_STATE_U_NEG_REPLY);
    BLE_HS_DBG_ASSERT(entry->hci_handle == ack->bha_hci_handle);

    entry->hci_handle = BLE_HCI_SCHED_HANDLE_NONE;

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
    BLE_HS_DBG_ASSERT(entry->state == BLE_GAP_STATE_U_REPLY);
    BLE_HS_DBG_ASSERT(entry->hci_handle == ack->bha_hci_handle);

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
    struct ble_gap_conn_ctxt ctxt;
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
            memset(&ctxt, 0, sizeof ctxt);
            ctxt.desc = &snap.desc;
            ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN_UPDATE_REQ, rc, &ctxt,
                                 snap.cb, snap.cb_arg);
        }
    }

    return rc;
}

static int
ble_gap_tx_param_neg_reply(void *arg)
{
    struct hci_conn_param_neg_reply neg_reply;
    struct ble_gap_update_entry *entry;
    struct ble_gap_conn_ctxt ctxt;
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
            memset(&ctxt, 0, sizeof ctxt);
            ctxt.desc = &snap.desc;
            ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN_UPDATE_REQ, rc, &ctxt,
                                 snap.cb, snap.cb_arg);
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
    struct ble_gap_conn_ctxt ctxt;
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

    entry = ble_gap_update_entry_alloc();
    if (entry == NULL) {
        /* Out of memory; reject. */
        /* XXX: Our negative response should indicate which connection handle
         * it applies to, but we don't have anywhere to store this information.
         * For now, just send a connection handle of 0 and hope the peer can
         * sort it out.
         */
        rc = BLE_ERR_MEM_CAPACITY;
    } else {
        entry->state = BLE_GAP_STATE_U_REPLY;
        entry->conn_handle = evt->connection_handle;
        entry->params = peer_params;

        memset(&ctxt, 0, sizeof ctxt);
        ctxt.desc = &snap.desc;
        ctxt.update.self_params = &entry->params;
        ctxt.update.peer_params = &peer_params;
        rc = ble_gap_call_conn_cb(BLE_GAP_EVENT_CONN_UPDATE_REQ, 0, &ctxt,
                                  snap.cb, snap.cb_arg);
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
 *     o Caller locks gap.
 */
static int
ble_gap_update_tx(struct ble_gap_update_entry *entry)
{
    uint8_t buf[BLE_HCI_CMD_HDR_LEN + BLE_HCI_CONN_UPDATE_LEN];
    struct hci_conn_update cmd;
    int rc;

    cmd.handle = entry->conn_handle;
    cmd.conn_itvl_min = entry->params.itvl_min;
    cmd.conn_itvl_max = entry->params.itvl_max;
    cmd.conn_latency = entry->params.latency;
    cmd.supervision_timeout = entry->params.supervision_timeout;
    cmd.min_ce_len = entry->params.min_ce_len;
    cmd.max_ce_len = entry->params.max_ce_len;

    rc = host_hci_cmd_build_le_conn_update(&cmd, buf, sizeof buf);
    if (rc != 0) {
        return rc;
    }

    rc = ble_hci_block_tx(buf, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
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

    ble_gap_lock();

    if (!ble_hs_conn_exists(conn_handle)) {
        rc = BLE_HS_ENOTCONN;
        goto done;
    }

    entry = ble_gap_update_find(conn_handle);
    if (entry != NULL) {
        rc = BLE_HS_EALREADY;
        goto done;
    }

    entry = ble_gap_update_entry_alloc();
    if (entry == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    entry->state = BLE_GAP_STATE_U_UPDATE;
    entry->conn_handle = conn_handle;
    entry->params = *params;

    rc = ble_gap_update_tx(entry);
    if (rc != 0) {
        goto done;
    }

done:
    if (rc == 0) {
        BLE_HS_LOG(INFO, "GAP procedure initiated: ");
        ble_gap_log_update(entry);
        BLE_HS_LOG(INFO, "\n");
        SLIST_INSERT_HEAD(&ble_gap_update_entries, entry, next);
    } else {
        STATS_INC(ble_gap_stats, update_fail);
    }

    ble_gap_unlock();

    return rc;
}

/*****************************************************************************
 * $security                                                                 *
 *****************************************************************************/

#if NIMBLE_OPT_SM
int
ble_gap_security_initiate(uint16_t conn_handle)
{
    ble_hs_conn_flags_t conn_flags;
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_conn_lock();
    conn = ble_hs_conn_find(conn_handle);
    if (conn != NULL) {
        conn_flags = conn->bhc_flags;
    }
    ble_hs_conn_unlock();

    if (conn == NULL) {
        return BLE_HS_ENOTCONN;
    }
    if (!(conn_flags & BLE_HS_CONN_F_MASTER)) {
        return BLE_HS_EROLE;
    }

    rc = ble_l2cap_sm_initiate(conn_handle);
    return rc;
}
#endif

void
ble_gap_security_event(uint16_t conn_handle, int status,
                       struct ble_gap_sec_params *sec_params)
{
    struct ble_gap_conn_ctxt ctxt;
    struct ble_gap_snapshot snap;
    struct ble_hs_conn *conn;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(conn_handle);
    if (conn != NULL) {
        conn->bhc_sec_params = *sec_params;
        ble_gap_conn_to_snapshot(conn, &snap);
    }

    ble_hs_conn_unlock();

    if (conn == NULL) {
        /* No longer connected. */
        return;
    }

    memset(&ctxt, 0, sizeof ctxt);
    ctxt.desc = &snap.desc;
    ctxt.sec_params = sec_params;
    ble_gap_call_conn_cb(BLE_GAP_EVENT_SECURITY, status, &ctxt,
                         snap.cb, snap.cb_arg);
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

    free(ble_gap_update_mem);
    ble_gap_update_mem = malloc(
        OS_MEMPOOL_BYTES(ble_hs_cfg.max_conn_update_entries,
                         sizeof (struct ble_gap_update_entry)));

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
