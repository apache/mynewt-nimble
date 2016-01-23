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

#include <assert.h>
#include <string.h>
#include <errno.h>
#include "bsp/bsp.h"
#include "os/os.h"
#include "ble_hs_priv.h"
#include "host/host_hci.h"
#include "ble_hci_ack.h"
#include "ble_hs_conn.h"
#include "ble_hs_adv.h"
#include "ble_hci_ack.h"
#include "ble_hci_sched.h"
#include "ble_gatt_priv.h"
#include "ble_gap_priv.h"

#define BLE_GAP_CONN_OP_NULL                                0
#define BLE_GAP_CONN_STATE_NULL                             255

#define BLE_GAP_CONN_OP_M_DISC                              1
#define BLE_GAP_CONN_OP_M_CONN                              2

#define BLE_GAP_CONN_S_OP_NON                               1
#define BLE_GAP_CONN_S_OP_UND                               2
#define BLE_GAP_CONN_S_OP_DIR                               3

#define BLE_GAP_CONN_OP_W_SET                               1

/** Discovery master states. */
#define BLE_GAP_CONN_M_STATE_DISC_PARAMS                    0
#define BLE_GAP_CONN_M_STATE_DISC_ENABLE                    1
#define BLE_GAP_CONN_M_STATE_DISC_ACKED                     2
#define BLE_GAP_CONN_M_STATE_DISC_DISABLE                   3

/** Connect master states. */
#define BLE_GAP_CONN_STATE_M_PENDING                        0
#define BLE_GAP_CONN_STATE_M_UNACKED                        1
#define BLE_GAP_CONN_STATE_M_ACKED                          2

/** Undirected slave states. */
#define BLE_GAP_CONN_S_STATE_UND_PARAMS                     0
#define BLE_GAP_CONN_S_STATE_UND_POWER                      1
#define BLE_GAP_CONN_S_STATE_UND_ADV_DATA                   2
#define BLE_GAP_CONN_S_STATE_UND_RSP_DATA                   3
#define BLE_GAP_CONN_S_STATE_UND_ENABLE                     4
#define BLE_GAP_CONN_S_STATE_UND_ADV                        5

/** Directed slave states. */
#define BLE_GAP_CONN_S_STATE_DIR_PARAMS                     0
#define BLE_GAP_CONN_S_STATE_DIR_ENABLE                     1
#define BLE_GAP_CONN_S_STATE_DIR_ADV                        2

/** White list states. */
#define BLE_GAP_CONN_STATE_W_CLEAR                          0
#define BLE_GAP_CONN_STATE_W_ADD                            1

/** Connection update states. */
#define BLE_GAP_CONN_STATE_U_UPDATE                         0
#define BLE_GAP_CONN_STATE_U_UPDATE_ACKED                   1
#define BLE_GAP_CONN_STATE_U_REPLY                          2
#define BLE_GAP_CONN_STATE_U_REPLY_ACKED                    3
#define BLE_GAP_CONN_STATE_U_NEG_REPLY                      4

/**
 * The maximum amount of user data that can be put into the advertising data.
 * Six bytes are reserved at the end for the flags field and the transmit power
 * field.
 */
#define BLE_GAP_CONN_ADV_DATA_LIMIT         (BLE_HCI_MAX_ADV_DATA_LEN - 6)

#define BLE_GAP_CONN_MAX_UPDATES            4

static const struct ble_gap_conn_crt_params ble_gap_conn_params_dflt = {
    .scan_itvl = 0x0010,
    .scan_window = 0x0010,
    .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN,
    .itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX,
    .latency = 0,
    .supervision_timeout = 0x0100,
    .min_ce_len = 0x0010,
    .max_ce_len = 0x0300,
};

/**
 * The state of the in-progress master connection.  If no master connection is
 * currently in progress, then the op field is set to BLE_GAP_CONN_OP_NULL.
 */
static bssnz_t struct {
    uint8_t op;
    uint8_t state;

    union {
        struct {
            uint8_t addr_type;
            uint8_t addr[6];
            struct ble_gap_conn_crt_params params;
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
} ble_gap_conn_master;

/**
 * The state of the in-progress slave connection.  If no slave connection is
 * currently in progress, then the op field is set to BLE_GAP_CONN_OP_NULL.
 */
static bssnz_t struct {
    uint8_t op;
    uint8_t state;
    uint8_t disc_mode;
    ble_gap_conn_fn *cb;
    void *cb_arg;

    uint8_t dir_addr_type;
    uint8_t dir_addr[BLE_DEV_ADDR_LEN];

    struct hci_adv_params adv_params;
    int8_t tx_pwr_lvl;
    uint8_t adv_data_len;
    uint8_t adv_data[BLE_HCI_MAX_ADV_DATA_LEN];
} ble_gap_conn_slave;

static bssnz_t struct {
    ble_gap_wl_fn *cb;
    void *cb_arg;

    struct ble_gap_white_entry *entries;
    uint8_t op;
    uint8_t state;
    uint8_t count;
    uint8_t cur;
} ble_gap_conn_wl;

struct ble_gap_conn_update_entry {
    SLIST_ENTRY(ble_gap_conn_update_entry) next;
    struct ble_gap_conn_upd_params params;
    uint16_t conn_handle;
    uint8_t state;
};
static SLIST_HEAD(, ble_gap_conn_update_entry) ble_gap_conn_update_entries;

static bssnz_t os_membuf_t 
ble_gap_conn_update_mem[OS_MEMPOOL_SIZE(BLE_GAP_CONN_MAX_UPDATES,
                         sizeof (struct ble_gap_conn_update_entry))];
static struct os_mempool ble_gap_conn_update_pool;

static int ble_gap_conn_adv_params_tx(void *arg);
static int ble_gap_conn_adv_power_tx(void *arg);
static int ble_gap_conn_adv_data_tx(void *arg);
static int ble_gap_conn_adv_rsp_data_tx(void *arg);
static int ble_gap_conn_adv_enable_tx(void *arg);
static int ble_gap_conn_wl_tx_add(void *arg);
static int ble_gap_conn_disc_tx_disable(void *arg);

static ble_hci_sched_tx_fn * const ble_gap_conn_dispatch_adv_und[] = {
    [BLE_GAP_CONN_S_STATE_UND_PARAMS]   = ble_gap_conn_adv_params_tx,
    [BLE_GAP_CONN_S_STATE_UND_POWER]    = ble_gap_conn_adv_power_tx,
    [BLE_GAP_CONN_S_STATE_UND_ADV_DATA] = ble_gap_conn_adv_data_tx,
    [BLE_GAP_CONN_S_STATE_UND_RSP_DATA] = ble_gap_conn_adv_rsp_data_tx,
    [BLE_GAP_CONN_S_STATE_UND_ENABLE]   = ble_gap_conn_adv_enable_tx,
    [BLE_GAP_CONN_S_STATE_UND_ADV]      = NULL,
};

static ble_hci_sched_tx_fn * const ble_gap_conn_dispatch_adv_dir[] = {
    [BLE_GAP_CONN_S_STATE_DIR_PARAMS]   = ble_gap_conn_adv_params_tx,
    [BLE_GAP_CONN_S_STATE_DIR_ENABLE]   = ble_gap_conn_adv_enable_tx,
    [BLE_GAP_CONN_S_STATE_DIR_ADV]      = NULL,
};

static struct os_callout_func ble_gap_conn_master_timer;
static struct os_callout_func ble_gap_conn_slave_timer;

/*****************************************************************************
 * $misc                                                                     *
 *****************************************************************************/

static struct ble_gap_conn_update_entry *
ble_gap_conn_update_entry_alloc(void)
{
    struct ble_gap_conn_update_entry *entry;

    entry = os_memblock_get(&ble_gap_conn_update_pool);
    if (entry == NULL) {
        return NULL;
    }

    memset(entry, 0, sizeof *entry);
    SLIST_INSERT_HEAD(&ble_gap_conn_update_entries, entry, next);

    return entry;
}

static void
ble_gap_conn_update_entry_free(struct ble_gap_conn_update_entry *entry)
{
    int rc;

    rc = os_memblock_put(&ble_gap_conn_update_pool, entry);
    assert(rc == 0);
}

static struct ble_gap_conn_update_entry *
ble_gap_conn_update_find(uint16_t conn_handle)
{
    struct ble_gap_conn_update_entry *entry;

    SLIST_FOREACH(entry, &ble_gap_conn_update_entries, next) {
        if (entry->conn_handle == conn_handle) {
            return entry;
        }
    }

    return NULL;
}

static int
ble_gap_conn_call_conn_cb(int event, int status, struct ble_hs_conn *conn,
                          struct ble_gap_conn_upd_params *self_params,
                          struct ble_gap_conn_upd_params *peer_params)
{
    struct ble_gap_conn_ctxt ctxt;
    ble_gap_conn_fn *cb;
    void *cb_arg;
    int rc;

    memset(&ctxt, 0, sizeof ctxt);

    if (conn != NULL) {
        ctxt.desc.conn_handle = conn->bhc_handle;
        ctxt.desc.peer_addr_type = conn->bhc_addr_type;
        memcpy(ctxt.desc.peer_addr, conn->bhc_addr,
               sizeof ctxt.desc.peer_addr);
        ctxt.desc.conn_itvl = conn->bhc_itvl;
        ctxt.desc.conn_latency = conn->bhc_latency;
        ctxt.desc.supervision_timeout = conn->bhc_supervision_timeout;

        cb = conn->bhc_cb;
        cb_arg = conn->bhc_cb_arg;
    } else {
        ctxt.desc.conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ctxt.desc.peer_addr_type = ble_gap_conn_master.conn.addr_type;
        memcpy(ctxt.desc.peer_addr, ble_gap_conn_master.conn.addr,
               sizeof ctxt.desc.peer_addr);
        cb = ble_gap_conn_master.conn.cb;
        cb_arg = ble_gap_conn_master.conn.cb_arg;
    }

    ctxt.self_params = self_params;
    ctxt.peer_params = peer_params;

    if (cb != NULL) {
        rc = cb(event, status, &ctxt, cb_arg);
    } else {
        if (event == BLE_GAP_EVENT_CONN_UPDATE_REQ) {
            /* Just copy peer parameters back into reply. */
            *self_params = *peer_params;
        }
        rc = 0;
    }

    return rc;
}

static void
ble_gap_conn_call_slave_cb(int event, int status)
{
    struct ble_gap_conn_ctxt ctxt;

    if (ble_gap_conn_slave.cb == NULL) {
        return;
    }

    memset(&ctxt, 0, sizeof ctxt);
    ctxt.desc.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ctxt.desc.peer_addr_type = ble_gap_conn_slave.dir_addr_type;
    memcpy(ctxt.desc.peer_addr, ble_gap_conn_slave.dir_addr,
           sizeof ctxt.desc.peer_addr);

    ble_gap_conn_slave.cb(event, status, &ctxt, ble_gap_conn_slave.cb_arg);
}

/**
 * Notifies the application of a connection event if a callback is configured.
 *
 * @param status                The HCI status of the connection attempt.
 * @param conn                  The connection this notification concerns;
 *                                  null if a connection was never created.
 */
static void
ble_gap_conn_notify_connect(int status, struct ble_hs_conn *conn)
{
    ble_gap_conn_call_conn_cb(BLE_GAP_EVENT_CONN, status, conn, NULL, NULL);
}

static void
ble_gap_conn_notify_term_failure(int status, struct ble_hs_conn *conn)
{
    ble_gap_conn_call_conn_cb(BLE_GAP_EVENT_TERM_FAILURE, status, conn,
                              NULL, NULL);
}

static void
ble_gap_conn_notify_disc(struct ble_hs_adv *adv,
                         struct ble_hs_adv_fields *fields)
{
    struct ble_gap_disc_desc desc;

    if (ble_gap_conn_master.disc.cb == NULL) {
        return;
    }

    desc.event_type = adv->event_type;
    desc.addr_type = adv->addr_type;
    desc.length_data = adv->length_data;
    desc.rssi = adv->rssi;
    memcpy(desc.addr, adv->addr, sizeof adv->addr);
    desc.data = adv->data;
    desc.fields = fields;

    ble_gap_conn_master.disc.cb(BLE_GAP_EVENT_DISC_SUCCESS, 0, &desc,
                                ble_gap_conn_master.disc.cb_arg);
}

static void
ble_gap_conn_notify_master_conn_failure(int status)
{
    ble_gap_conn_call_conn_cb(BLE_GAP_EVENT_CONN, status, NULL, NULL, NULL);
}

static void
ble_gap_conn_notify_master_cancel_failure(int status)
{
    ble_gap_conn_call_conn_cb(BLE_GAP_EVENT_CANCEL_FAILURE, status, NULL,
                              NULL, NULL);
}

static void
ble_gap_conn_notify_master_disc_failure(int status)
{
    struct ble_gap_disc_desc desc;

    if (ble_gap_conn_master.disc.cb == NULL) {
        return;
    }

    memset(&desc, 0, sizeof desc);
    ble_gap_conn_master.disc.cb(BLE_GAP_EVENT_DISC_FINISHED, status, &desc,
                                ble_gap_conn_master.disc.cb_arg);
}

static void
ble_gap_conn_notify_master_term_failure(int status, uint16_t conn_handle)
{
    struct ble_hs_conn *conn;

    conn = ble_hs_conn_find(conn_handle);
    if (conn == NULL) {
        /* XXX: Stat. */
        return;
    }

    ble_gap_conn_notify_term_failure(status, conn);
}

static void
ble_gap_conn_notify_slave_conn_failure(int status)
{
    ble_gap_conn_call_slave_cb(BLE_GAP_EVENT_CONN, status);
}

static void
ble_gap_conn_notify_adv_finished(void)
{
    ble_gap_conn_call_slave_cb(BLE_GAP_EVENT_ADV_FINISHED, 0);
}

static void
ble_gap_conn_notify_slave_adv_cancel_failure(int status)
{
    ble_gap_conn_call_slave_cb(BLE_GAP_EVENT_ADV_STOP_FAILURE, status);
}

static void
ble_gap_conn_notify_wl(int status)
{
    if (ble_gap_conn_wl.cb == NULL) {
        return;
    }

    ble_gap_conn_wl.cb(status, ble_gap_conn_wl.cb_arg);
}

static void
ble_gap_conn_notify_update(struct ble_gap_conn_update_entry *entry, int status)
{
    struct ble_hs_conn *conn;

    conn = ble_hs_conn_find(entry->conn_handle);
    if (conn == NULL) {
        return;
    }

    ble_gap_conn_call_conn_cb(BLE_GAP_EVENT_CONN_UPDATED, status, NULL, NULL,
                              NULL);
}

static void
ble_gap_conn_master_reset_state(void)
{
    os_callout_stop(&ble_gap_conn_master_timer.cf_c);
    ble_gap_conn_master.op = BLE_GAP_CONN_OP_NULL;
}

static void
ble_gap_conn_slave_reset_state(void)
{
    os_callout_stop(&ble_gap_conn_slave_timer.cf_c);
    ble_gap_conn_slave.op = BLE_GAP_CONN_OP_NULL;
}

/**
 * Called when an error is encountered while the master-connection-fsm is
 * active.  Resets the state machine, clears the HCI ack callback, and notifies
 * the host task that the next hci_batch item can be processed.
 */
static void
ble_gap_conn_master_failed(int status)
{
    uint8_t old_proc;

    assert(ble_gap_conn_master.op != BLE_GAP_CONN_OP_NULL);

    os_callout_stop(&ble_gap_conn_master_timer.cf_c);

    old_proc = ble_gap_conn_master.op;
    ble_gap_conn_master_reset_state();

    switch (old_proc) {
    case BLE_GAP_CONN_OP_M_DISC:
        ble_gap_conn_notify_master_disc_failure(status);
        break;

    case BLE_GAP_CONN_OP_M_CONN:
        ble_gap_conn_notify_master_conn_failure(status);
        break;

    default:
        break;
    }
}

/**
 * Called when an error is encountered while the slave-connection-fsm is
 * active.
 */
static void
ble_gap_conn_slave_failed(int event, int status)
{
    ble_gap_conn_call_slave_cb(event, status);
    ble_gap_conn_slave_reset_state();
}

static void
ble_gap_conn_wl_failed(int status)
{
    ble_gap_conn_wl.op = BLE_GAP_CONN_OP_NULL;
    ble_gap_conn_notify_wl(status);
}

static void
ble_gap_conn_update_entry_remove_free(struct ble_gap_conn_update_entry *entry)
{
    SLIST_REMOVE(&ble_gap_conn_update_entries, entry,
                 ble_gap_conn_update_entry, next);
    ble_gap_conn_update_entry_free(entry);
}

static void
ble_gap_conn_update_failed(struct ble_gap_conn_update_entry *entry, int status)
{
    ble_gap_conn_notify_update(entry, status);
    ble_gap_conn_update_entry_remove_free(entry);
}

void
ble_gap_conn_rx_disconn_complete(struct hci_disconn_complete *evt)
{
    struct ble_hs_conn *conn;

    if (evt->status == 0) {
        /* Find the connection that this event refers to. */
        conn = ble_hs_conn_find(evt->connection_handle);
        if (conn == NULL) {
            return;
        }
        ble_gap_conn_call_conn_cb(BLE_GAP_EVENT_CONN, BLE_HS_ENOTCONN, conn,
                                  NULL, NULL);
        ble_hs_conn_remove(conn);
        ble_hs_conn_free(conn);
    } else {
        ble_gap_conn_notify_master_term_failure(BLE_HS_HCI_ERR(evt->status),
                                                evt->connection_handle);
    }

    ble_gattc_connection_broken(evt->connection_handle);
}

void
ble_gap_conn_rx_update_complete(struct hci_le_conn_upd_complete *evt)
{
    struct ble_gap_conn_update_entry *entry;
    struct ble_hs_conn *conn;

    conn = ble_hs_conn_find(evt->connection_handle);
    if (conn == NULL) {
        return;
    }

    entry = ble_gap_conn_update_find(evt->connection_handle);
    if (entry != NULL) {
        ble_gap_conn_update_entry_remove_free(entry);
    }

    if (evt->status == 0) {
        conn->bhc_itvl = evt->conn_itvl;
        conn->bhc_latency = evt->conn_latency;
        conn->bhc_supervision_timeout = evt->supervision_timeout;
    }

    ble_gap_conn_call_conn_cb(BLE_GAP_EVENT_CONN_UPDATED,
                              BLE_HS_HCI_ERR(evt->status), conn, NULL, NULL);
}

/**
 * Tells you if the BLE host is in the process of creating a master connection.
 */
int
ble_gap_conn_master_in_progress(void)
{
    return ble_gap_conn_master.op != BLE_GAP_CONN_OP_NULL;
}

/**
 * Tells you if the BLE host is in the process of creating a slave connection.
 */
int
ble_gap_conn_slave_in_progress(void)
{
    return ble_gap_conn_slave.op != BLE_GAP_CONN_OP_NULL;
}

static int
ble_gap_conn_currently_advertising(void)
{
    switch (ble_gap_conn_slave.op) {
    case BLE_GAP_CONN_OP_NULL:
        return 0;

    case BLE_GAP_CONN_S_OP_NON:
        return ble_gap_conn_slave.state == BLE_GAP_CONN_S_STATE_UND_ADV;

    case BLE_GAP_CONN_S_OP_UND:
        return ble_gap_conn_slave.state == BLE_GAP_CONN_S_STATE_UND_ADV;

    case BLE_GAP_CONN_S_OP_DIR:
        return ble_gap_conn_slave.state == BLE_GAP_CONN_S_STATE_DIR_ADV;

    default:
        assert(0);
        return 0;
    }
}

static int
ble_gap_conn_master_enqueue(uint8_t state, int in_progress,
                            ble_hci_sched_tx_fn *hci_tx_cb, void *cb_arg)
{
    int rc;

    ble_gap_conn_master.state = state;
    rc = ble_hci_sched_enqueue(hci_tx_cb, cb_arg);
    if (rc != 0) {
        if (in_progress) {
            ble_gap_conn_master_failed(rc);
        } else {
            ble_gap_conn_master_reset_state();
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
ble_gap_conn_accept_master_conn(uint8_t addr_type, uint8_t *addr)
{
    switch (ble_gap_conn_master.op) {
    case BLE_GAP_CONN_OP_NULL:
    case BLE_GAP_CONN_OP_M_DISC:
        return BLE_HS_ENOENT;

    case BLE_GAP_CONN_OP_M_CONN:
        if (ble_gap_conn_master.state != BLE_GAP_CONN_STATE_M_ACKED) {
            return BLE_HS_ENOENT;
        }

        if (ble_gap_conn_master.conn.addr_type == BLE_GAP_ADDR_TYPE_WL ||
            (addr_type == ble_gap_conn_master.conn.addr_type &&
             memcmp(addr, ble_gap_conn_master.conn.addr,
                    BLE_DEV_ADDR_LEN) == 0)) {
            return 0;
        } else {
            ble_gap_conn_master_failed(BLE_HS_ECONTROLLER);
            return BLE_HS_ECONTROLLER;
        }

    default:
        assert(0);
        return BLE_HS_ENOENT;
    }
}

/**
 * Attempts to complete the slave connection process in response to a
 * "connection complete" event from the controller.  If the slave connection
 * FSM is in a state that can accept this event, and the peer device address is
 * valid, the master FSM is reset and success is returned.
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
ble_gap_conn_accept_slave_conn(uint8_t addr_type, uint8_t *addr)
{
    if (!ble_gap_conn_currently_advertising()) {
        return BLE_HS_ENOENT;
    }

    switch (ble_gap_conn_slave.op) {
    case BLE_GAP_CONN_OP_NULL:
    case BLE_GAP_CONN_S_OP_NON:
        return BLE_HS_ENOENT;

    case BLE_GAP_CONN_S_OP_UND:
        return 0;

    case BLE_GAP_CONN_S_OP_DIR:
        if (ble_gap_conn_slave.dir_addr_type != addr_type ||
            memcmp(ble_gap_conn_slave.dir_addr, addr, BLE_DEV_ADDR_LEN) != 0) {

            return BLE_HS_ENOENT;
        } else {
            return 0;
        }

    default:
        assert(0);
        return BLE_HS_ENOENT;
    }
}

void
ble_gap_conn_rx_adv_report(struct ble_hs_adv *adv)
{
    struct ble_hs_adv_fields fields;
    int rc;

    if (ble_gap_conn_master.op      != BLE_GAP_CONN_OP_M_DISC ||
        ble_gap_conn_master.state   != BLE_GAP_CONN_M_STATE_DISC_ACKED) {

        return;
    }

    rc = ble_hs_adv_parse_fields(&fields, adv->data, adv->length_data);
    if (rc != 0) {
        /* XXX: Increment stat. */
        return;
    }

    if (ble_gap_conn_master.disc.disc_mode == BLE_GAP_DISC_MODE_LTD &&
        !(fields.flags & BLE_HS_ADV_F_DISC_LTD)) {

        return;
    }

    ble_gap_conn_notify_disc(adv, &fields);
}

/**
 * Processes an incoming connection-complete HCI event.
 */
int
ble_gap_conn_rx_conn_complete(struct hci_le_conn_complete *evt)
{
    struct ble_hs_conn *conn;
    int status;
    int rc;

    /* Determine if this event refers to a completed connection or a connection
     * in progress.
     */
    conn = ble_hs_conn_find(evt->connection_handle);

    /* Apply the event to the existing connection if it exists. */
    if (conn != NULL) {
        /* XXX: Does this ever happen? */
        if (evt->status != 0) {
            ble_hs_conn_remove(conn);
            ble_gap_conn_notify_connect(evt->status, conn);
            ble_hs_conn_free(conn);
        }

        return 0;
    }

    /* This event refers to a new connection. */

    if (evt->status != BLE_ERR_SUCCESS) {
        status = BLE_HS_HCI_ERR(evt->status);

        /* Some error codes need special handling. */
        switch (evt->status) {
        case BLE_ERR_DIR_ADV_TMO:
            if (ble_gap_conn_slave_in_progress()) {
                ble_gap_conn_slave_failed(BLE_GAP_EVENT_ADV_FINISHED, 0);
            }
            return 0;

        default:
            break;
        }

        switch (evt->role) {
        case BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER:
            if (ble_gap_conn_master_in_progress()) {
                ble_gap_conn_master_failed(status);
            }
            break;

        case BLE_HCI_LE_CONN_COMPLETE_ROLE_SLAVE:
            if (ble_gap_conn_slave_in_progress()) {
                ble_gap_conn_notify_slave_conn_failure(status);
            }
            break;

        default:
            assert(0);
            break;
        }

        return 0;
    }

    switch (evt->role) {
    case BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER:
        rc = ble_gap_conn_accept_master_conn(evt->peer_addr_type,
                                             evt->peer_addr);
        if (rc != 0) {
            return rc;
        }
        break;

    case BLE_HCI_LE_CONN_COMPLETE_ROLE_SLAVE:
        rc = ble_gap_conn_accept_slave_conn(evt->peer_addr_type,
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
        conn->bhc_cb = ble_gap_conn_master.conn.cb;
        conn->bhc_cb_arg = ble_gap_conn_master.conn.cb_arg;
        ble_gap_conn_master_reset_state();
    } else {
        conn->bhc_cb = ble_gap_conn_slave.cb;
        conn->bhc_cb_arg = ble_gap_conn_slave.cb_arg;
        ble_gap_conn_slave_reset_state();
    }

    ble_hs_conn_insert(conn);
    ble_gap_conn_notify_connect(0, conn);

    return 0;
}

static void
ble_gap_conn_master_timer_exp(void *arg)
{
    assert(ble_gap_conn_master_in_progress());

    switch (ble_gap_conn_master.op) {
    case BLE_GAP_CONN_OP_M_DISC:
        /* When a discovery procedure times out, it is not a failure. */
        ble_gap_conn_master_enqueue(BLE_GAP_CONN_M_STATE_DISC_DISABLE, 1,
                                    ble_gap_conn_disc_tx_disable, NULL);
        break;

    default:
        ble_gap_conn_master_failed(BLE_HS_ETIMEOUT);
        break;
    }

}

static void
ble_gap_conn_slave_timer_exp(void *arg)
{
    assert(ble_gap_conn_slave_in_progress());
    ble_gap_conn_slave_failed(BLE_GAP_EVENT_ADV_FINISHED, 0);
}

/*****************************************************************************
 * $white list                                                               *
 *****************************************************************************/

int
ble_gap_conn_wl_busy(void)
{
    /* Check if application is currently setting the white list. */
    if (ble_gap_conn_wl.op != BLE_GAP_CONN_OP_NULL) {
        return 1;
    }

    /* Check if an auto or selective connection establishment procedure is in
     * progress.
     */
    if (ble_gap_conn_master.op == BLE_GAP_CONN_OP_M_CONN &&
        ble_gap_conn_master.conn.addr_type == BLE_GAP_ADDR_TYPE_WL) {

        return 1;
    }

    return 0;
}

static int
ble_gap_conn_wl_enqueue(uint8_t state, int in_progress,
                        ble_hci_sched_tx_fn *hci_tx_cb, void *cb_arg)
{
    int rc;

    ble_gap_conn_wl.state = state;
    rc = ble_hci_sched_enqueue(hci_tx_cb, cb_arg);
    if (rc != 0) {
        if (in_progress) {
            ble_gap_conn_wl_failed(rc);
        } else {
            ble_gap_conn_wl.op = BLE_GAP_CONN_OP_NULL;
        }
    }

    return rc;
}

static void
ble_gap_conn_wl_ack_add(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_conn_wl.op == BLE_GAP_CONN_OP_W_SET);
    assert(ble_gap_conn_wl.state == BLE_GAP_CONN_STATE_W_ADD);

    if (ack->bha_status != 0) {
        ble_gap_conn_wl_failed(ack->bha_status);
        return;
    }

    ble_gap_conn_wl.cur++;
    if (ble_gap_conn_wl.cur < ble_gap_conn_wl.count) {
        ble_gap_conn_wl_enqueue(BLE_GAP_CONN_STATE_W_ADD, 1,
                                ble_gap_conn_wl_tx_add, NULL);
    } else {
        /* Success. */
        ble_gap_conn_wl.op = BLE_GAP_CONN_OP_NULL;
        ble_gap_conn_notify_wl(0);
    }
}

static int
ble_gap_conn_wl_tx_add(void *arg)
{
    struct ble_gap_white_entry *white_entry;
    int rc;

    assert(ble_gap_conn_wl.op == BLE_GAP_CONN_OP_W_SET);
    assert(ble_gap_conn_wl.state == BLE_GAP_CONN_STATE_W_ADD);
    assert(ble_gap_conn_wl.entries != NULL);
    assert(ble_gap_conn_wl.cur < ble_gap_conn_wl.count);

    ble_hci_ack_set_callback(ble_gap_conn_wl_ack_add, NULL);

    white_entry = ble_gap_conn_wl.entries + ble_gap_conn_wl.cur;
    rc = host_hci_cmd_le_add_to_whitelist(white_entry->addr,
                                          white_entry->addr_type);
    if (rc != 0) {
        ble_gap_conn_wl_failed(rc);
        return 1;
    }

    return 0;
}

static void
ble_gap_conn_wl_ack_clear(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_conn_wl.op == BLE_GAP_CONN_OP_W_SET);
    assert(ble_gap_conn_wl.state == BLE_GAP_CONN_STATE_W_CLEAR);

    if (ack->bha_status != 0) {
        ble_gap_conn_wl_failed(ack->bha_status);
        return;
    }

    ble_gap_conn_wl_enqueue(BLE_GAP_CONN_STATE_W_ADD, 1,
                            ble_gap_conn_wl_tx_add, NULL);
}

static int
ble_gap_conn_wl_tx_clear(void *arg)
{
    int rc;

    assert(ble_gap_conn_wl.op == BLE_GAP_CONN_OP_W_SET);
    assert(ble_gap_conn_wl.state == BLE_GAP_CONN_STATE_W_CLEAR);

    ble_hci_ack_set_callback(ble_gap_conn_wl_ack_clear, NULL);

    rc = host_hci_cmd_le_clear_whitelist();
    if (rc != 0) {
        ble_gap_conn_wl_failed(rc);
        return 1;
    }

    return 0;
}

int
ble_gap_conn_wl_set(struct ble_gap_white_entry *white_list,
                    uint8_t white_list_count, ble_gap_wl_fn *cb, void *cb_arg)
{
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

    if (ble_gap_conn_wl_busy()) {
        return BLE_HS_EBUSY;
    }

    ble_gap_conn_wl.cb = cb;
    ble_gap_conn_wl.cb_arg = cb_arg;
    ble_gap_conn_wl.op = BLE_GAP_CONN_OP_W_SET;
    ble_gap_conn_wl.entries = white_list;
    ble_gap_conn_wl.count = white_list_count;
    ble_gap_conn_wl.cur = 0;

    rc = ble_gap_conn_wl_enqueue(BLE_GAP_CONN_STATE_W_CLEAR, 0,
                                 ble_gap_conn_wl_tx_clear, NULL);
    return rc;
}

/*****************************************************************************
 * $stop advertise                                                           *
 *****************************************************************************/

static void
ble_gap_conn_adv_ack_disable(struct ble_hci_ack *ack, void *arg)
{
    if (ack->bha_status == 0) {
        /* Advertising should now be aborted. */
        ble_gap_conn_notify_adv_finished();
        ble_gap_conn_slave_reset_state();
    } else {
        ble_gap_conn_notify_slave_adv_cancel_failure(ack->bha_status);
    }
}

static int
ble_gap_conn_adv_disable_tx(void *arg)
{
    int rc;

    ble_hci_ack_set_callback(ble_gap_conn_adv_ack_disable, NULL);
    rc = host_hci_cmd_le_set_adv_enable(0);
    if (rc != BLE_ERR_SUCCESS) {
        ble_gap_conn_notify_slave_adv_cancel_failure(BLE_HS_HCI_ERR(rc));
        return 1;
    }

    return 0;
}

int
ble_gap_conn_adv_stop(void)
{
    int rc;

    /* Do nothing if advertising is already disabled. */
    if (!ble_gap_conn_currently_advertising()) {
        return BLE_HS_EALREADY;
    }

    rc = ble_hci_sched_enqueue(ble_gap_conn_adv_disable_tx, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $advertise                                                                *
 *****************************************************************************/

static void
ble_gap_conn_adv_itvls(uint8_t disc_mode, uint8_t conn_mode,
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

static ble_hci_sched_tx_fn *
ble_gap_conn_adv_get_dispatch(void)
{
    switch (ble_gap_conn_slave.op) {
    case BLE_GAP_CONN_S_OP_NON:
        return ble_gap_conn_dispatch_adv_und[ble_gap_conn_slave.state];

    case BLE_GAP_CONN_S_OP_UND:
        return ble_gap_conn_dispatch_adv_und[ble_gap_conn_slave.state];

    case BLE_GAP_CONN_S_OP_DIR:
        return ble_gap_conn_dispatch_adv_dir[ble_gap_conn_slave.state];

    default:
        assert(0);
        return NULL;
    }
}

static void
ble_gap_conn_adv_next_state(void)
{
    ble_hci_sched_tx_fn *tx_fn;
    int rc;

    ble_gap_conn_slave.state++;
    tx_fn = ble_gap_conn_adv_get_dispatch();
    if (tx_fn != NULL) {
        rc = ble_hci_sched_enqueue(tx_fn, NULL);
        if (rc != 0) {
            ble_gap_conn_slave_failed(BLE_GAP_EVENT_ADV_FAILURE, rc);
        }
    }
}

static void
ble_gap_conn_adv_ack(struct ble_hci_ack *ack, void *arg)
{
    if (ack->bha_status != 0) {
        ble_gap_conn_slave_failed(BLE_GAP_EVENT_ADV_FAILURE, ack->bha_status);
    } else {
        ble_gap_conn_adv_next_state();
    }
}

static int
ble_gap_conn_adv_enable_tx(void *arg)
{
    int rc;

    ble_hci_ack_set_callback(ble_gap_conn_adv_ack, NULL);
    rc = host_hci_cmd_le_set_adv_enable(1);
    if (rc != BLE_ERR_SUCCESS) {
        ble_gap_conn_slave_failed(BLE_GAP_EVENT_ADV_FAILURE,
                                  BLE_HS_HCI_ERR(rc));
        return 1;
    }

    return 0;
}

static int
ble_gap_conn_adv_rsp_data_tx(void *arg)
{
    uint8_t rsp_data[BLE_HCI_MAX_SCAN_RSP_DATA_LEN] = { 0 }; /* XXX */
    int rc;

    ble_hci_ack_set_callback(ble_gap_conn_adv_ack, NULL);
    rc = host_hci_cmd_le_set_scan_rsp_data(rsp_data, sizeof rsp_data);
    if (rc != 0) {
        ble_gap_conn_slave_failed(BLE_GAP_EVENT_ADV_FAILURE,
                                  BLE_HS_HCI_ERR(rc));
        return 1;
    }

    return 0;
}

static int
ble_gap_conn_adv_data_tx(void *arg)
{
    uint8_t adv_data_len;
    uint8_t flags;
    int rc;

    assert(ble_gap_conn_slave.op != BLE_GAP_CONN_OP_NULL);

    /* Calculate the value of the flags field from the discoverable mode. */
    flags = 0;
    switch (ble_gap_conn_slave.disc_mode) {
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
    adv_data_len = ble_gap_conn_slave.adv_data_len;
    if (flags != 0) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_FLAGS, 1, &flags,
                                 ble_gap_conn_slave.adv_data, &adv_data_len,
                                 BLE_HCI_MAX_ADV_DATA_LEN);
        assert(rc == 0);
    }

    /* Encode the transmit power AD field. */
    rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_TX_PWR_LEVEL, 1,
                             &ble_gap_conn_slave.tx_pwr_lvl,
                             ble_gap_conn_slave.adv_data,
                             &adv_data_len, BLE_HCI_MAX_ADV_DATA_LEN);
    assert(rc == 0);

    ble_hci_ack_set_callback(ble_gap_conn_adv_ack, NULL);
    rc = host_hci_cmd_le_set_adv_data(ble_gap_conn_slave.adv_data,
                                      adv_data_len);
    if (rc != 0) {
        ble_gap_conn_slave_failed(BLE_GAP_EVENT_ADV_FAILURE,
                                  BLE_HS_HCI_ERR(rc));
        return 1;
    }

    return 0;
}

static void
ble_gap_conn_adv_power_ack(struct ble_hci_ack *ack, void *arg)
{
    int8_t power_level;

    if (ack->bha_status != 0) {
        ble_gap_conn_slave_failed(BLE_GAP_EVENT_ADV_FAILURE, ack->bha_status);
        return;
    }

    if (ack->bha_params_len != BLE_HCI_ADV_CHAN_TXPWR_ACK_PARAM_LEN) {
        ble_gap_conn_slave_failed(BLE_GAP_EVENT_ADV_FAILURE,
                                  BLE_HS_ECONTROLLER);
        return;
    }

    power_level = ack->bha_params[1];
    if (power_level < BLE_HCI_ADV_CHAN_TXPWR_MIN ||
        power_level > BLE_HCI_ADV_CHAN_TXPWR_MAX) {

        /* XXX: Probably can do something nicer than abort the entire
         * procedure.
         */
        ble_gap_conn_slave_failed(BLE_GAP_EVENT_ADV_FAILURE,
                                  BLE_HS_ECONTROLLER);
        return;
    }

    /* Save power level value so it can be put in the advertising data. */
    ble_gap_conn_slave.tx_pwr_lvl = power_level;

    ble_gap_conn_adv_next_state();
}

static int
ble_gap_conn_adv_power_tx(void *arg)
{
    int rc;

    ble_hci_ack_set_callback(ble_gap_conn_adv_power_ack, NULL);
    rc = host_hci_cmd_read_adv_pwr();
    if (rc != 0) {
        ble_gap_conn_slave_failed(BLE_GAP_EVENT_ADV_FAILURE,
                                  BLE_HS_HCI_ERR(rc));
        return 1;
    }

    return 0;
}

static int
ble_gap_conn_adv_params_tx(void *arg)
{
    struct hci_adv_params hap;
    int rc;

    hap = ble_gap_conn_slave.adv_params;

    switch (ble_gap_conn_slave.op) {
    case BLE_GAP_CONN_S_OP_NON:
        hap.adv_type = BLE_HCI_ADV_TYPE_ADV_NONCONN_IND;
        break;

    case BLE_GAP_CONN_S_OP_DIR:
        hap.adv_type = BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD;
        memcpy(hap.peer_addr, ble_gap_conn_slave.dir_addr,
               sizeof hap.peer_addr);
        break;

    case BLE_GAP_CONN_S_OP_UND:
        hap.adv_type = BLE_HCI_ADV_TYPE_ADV_IND;
        break;

    default:
        assert(0);
        break;
    }

    ble_hci_ack_set_callback(ble_gap_conn_adv_ack, NULL);
    rc = host_hci_cmd_le_set_adv_params(&hap);
    if (rc != 0) {
        ble_gap_conn_slave_failed(BLE_GAP_EVENT_ADV_FAILURE,
                                  BLE_HS_HCI_ERR(rc));
        return 1;
    }

    return 0;
}

static int
ble_gap_conn_adv_initiate(void)
{
    int rc;

    rc = ble_hci_sched_enqueue(ble_gap_conn_adv_params_tx, NULL);
    if (rc != 0) {
        ble_gap_conn_slave_reset_state();
        return rc;
    }

    return 0;
}

/**
 * Enables the specified discoverable mode and connectable mode, and initiates
 * the advertising process.
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
ble_gap_conn_adv_start(uint8_t discoverable_mode, uint8_t connectable_mode,
                       uint8_t *peer_addr, uint8_t peer_addr_type,
                       ble_gap_conn_fn *cb, void *cb_arg)
{
    int rc;

    if (discoverable_mode >= BLE_GAP_DISC_MODE_MAX ||
        connectable_mode >= BLE_GAP_CONN_MODE_MAX) {

        return BLE_HS_EINVAL;
    }

    /* Make sure no slave connection attempt is already in progress. */
    if (ble_gap_conn_slave_in_progress()) {
        return BLE_HS_EALREADY;
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
        ble_gap_conn_slave.op = BLE_GAP_CONN_S_OP_NON;
        break;

    case BLE_GAP_CONN_MODE_UND:
        ble_gap_conn_slave.op = BLE_GAP_CONN_S_OP_UND;
        break;

    case BLE_GAP_CONN_MODE_DIR:
        if (peer_addr_type != BLE_ADDR_TYPE_PUBLIC &&
            peer_addr_type != BLE_ADDR_TYPE_RANDOM) {

            return BLE_HS_EINVAL;
        }

        ble_gap_conn_slave.op = BLE_GAP_CONN_S_OP_DIR;
        ble_gap_conn_slave.dir_addr_type = peer_addr_type;
        memcpy(ble_gap_conn_slave.dir_addr, peer_addr, 6);
        break;

    default:
        assert(0);
        break;
    }

    ble_gap_conn_slave.cb = cb;
    ble_gap_conn_slave.cb_arg = cb_arg;
    ble_gap_conn_slave.state = 0;
    ble_gap_conn_slave.disc_mode = discoverable_mode;

    ble_gap_conn_adv_itvls(discoverable_mode, connectable_mode,
                           &ble_gap_conn_slave.adv_params.adv_itvl_min,
                           &ble_gap_conn_slave.adv_params.adv_itvl_max);

    rc = ble_gap_conn_adv_initiate();
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_gap_conn_set_adv_fields(struct ble_hs_adv_fields *adv_fields)
{
    int rc;

    rc = ble_hs_adv_set_fields(adv_fields, ble_gap_conn_slave.adv_data,
                               &ble_gap_conn_slave.adv_data_len,
                               BLE_GAP_CONN_ADV_DATA_LIMIT);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $discovery procedures                                                     *
 *****************************************************************************/

static void
ble_gap_conn_disc_ack_disable(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_conn_master.op == BLE_GAP_CONN_OP_M_DISC);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_M_STATE_DISC_DISABLE);

    if (ack->bha_status != 0) {
        ble_gap_conn_master_failed(ack->bha_status);
    } else {
        ble_gap_conn_master_failed(0);
    }
}

static int
ble_gap_conn_disc_tx_disable(void *arg)
{
    int rc;

    assert(ble_gap_conn_master.op == BLE_GAP_CONN_OP_M_DISC);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_M_STATE_DISC_DISABLE);

    ble_hci_ack_set_callback(ble_gap_conn_disc_ack_disable, NULL);
    rc = host_hci_cmd_le_set_scan_enable(0, 0);
    if (rc != 0) {
        /* XXX: What can we do? */
        ble_gap_conn_master_failed(rc);
        return rc;
    }

    return 0;
}

static void
ble_gap_conn_disc_ack_enable(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_conn_master.op == BLE_GAP_CONN_OP_M_DISC);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_M_STATE_DISC_ENABLE);

    if (ack->bha_status != 0) {
        ble_gap_conn_master_failed(ack->bha_status);
    } else {
        ble_gap_conn_master.state = BLE_GAP_CONN_M_STATE_DISC_ACKED;
    }
}

static int
ble_gap_conn_disc_tx_enable(void *arg)
{
    int rc;

    assert(ble_gap_conn_master.op == BLE_GAP_CONN_OP_M_DISC);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_M_STATE_DISC_ENABLE);

    ble_hci_ack_set_callback(ble_gap_conn_disc_ack_enable, NULL);
    rc = host_hci_cmd_le_set_scan_enable(1, 0);
    if (rc != 0) {
        ble_gap_conn_master_failed(rc);
        return rc;
    }

    return 0;
}

static void
ble_gap_conn_disc_ack_params(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_conn_master.op == BLE_GAP_CONN_OP_M_DISC);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_M_STATE_DISC_PARAMS);

    if (ack->bha_status != 0) {
        ble_gap_conn_master_failed(ack->bha_status);
        return;
    }

    ble_gap_conn_master_enqueue(BLE_GAP_CONN_M_STATE_DISC_ENABLE, 1,
                                ble_gap_conn_disc_tx_enable, NULL);
}

static int
ble_gap_conn_disc_tx_params(void *arg)
{
    int rc;

    assert(ble_gap_conn_master.op == BLE_GAP_CONN_OP_M_DISC);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_M_STATE_DISC_PARAMS);

    ble_hci_ack_set_callback(ble_gap_conn_disc_ack_params, NULL);
    rc = host_hci_cmd_le_set_scan_params(
        ble_gap_conn_master.disc.scan_type,
        BLE_GAP_SCAN_FAST_INTERVAL_MIN,
        BLE_GAP_SCAN_FAST_WINDOW,
        BLE_HCI_ADV_OWN_ADDR_PUBLIC,
        ble_gap_conn_master.disc.filter_policy);
    if (rc != 0) {
        ble_gap_conn_master_failed(rc);
        return rc;
    }

    return 0;
}

/**
 * Performs the Limited or General Discovery Procedures, as described in
 * vol. 3, part C, section 9.2.5 / 9.2.6.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gap_conn_disc(uint32_t duration_ms, uint8_t discovery_mode,
                  uint8_t scan_type, uint8_t filter_policy,
                  ble_gap_disc_fn *cb, void *cb_arg)
{
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

    /* Make sure no master connection attempt is already in progress. */
    if (ble_gap_conn_master_in_progress()) {
        return BLE_HS_EALREADY;
    }

    if (duration_ms == 0) {
        duration_ms = BLE_GAP_GEN_DISC_SCAN_MIN;
    }

    ble_gap_conn_master.op = BLE_GAP_CONN_OP_M_DISC;
    ble_gap_conn_master.disc.disc_mode = discovery_mode;
    ble_gap_conn_master.disc.scan_type = scan_type;
    ble_gap_conn_master.disc.filter_policy = filter_policy;
    ble_gap_conn_master.disc.cb = cb;
    ble_gap_conn_master.disc.cb_arg = cb_arg;
    rc = ble_gap_conn_master_enqueue(BLE_GAP_CONN_M_STATE_DISC_PARAMS, 0,
                                     ble_gap_conn_disc_tx_params, NULL);
    if (rc != 0) {
        return rc;
    }

    os_callout_reset(&ble_gap_conn_master_timer.cf_c,
                     duration_ms * OS_TICKS_PER_SEC / 1000);

    return 0;
}

/*****************************************************************************
 * $connection establishment procedures                                      *
 *****************************************************************************/

/**
 * Processes an HCI acknowledgement (either command status or command complete)
 * while a master connection is being established.
 */
static void
ble_gap_conn_create_ack(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_conn_master.op == BLE_GAP_CONN_OP_M_CONN);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_STATE_M_UNACKED);

    if (ack->bha_status != 0) {
        ble_gap_conn_master_failed(ack->bha_status);
        return;
    }

    ble_gap_conn_master.state = BLE_GAP_CONN_STATE_M_ACKED;
}

static int
ble_gap_conn_create_tx(void *arg)
{
    struct hci_create_conn hcc;
    int rc;

    assert(ble_gap_conn_master.op == BLE_GAP_CONN_OP_M_CONN);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_STATE_M_PENDING);

    hcc.scan_itvl = ble_gap_conn_master.conn.params.scan_itvl;
    hcc.scan_window = ble_gap_conn_master.conn.params.scan_window;

    if (ble_gap_conn_master.conn.addr_type == BLE_GAP_ADDR_TYPE_WL) {
        hcc.filter_policy = BLE_HCI_CONN_FILT_USE_WL;
        hcc.peer_addr_type = BLE_HCI_ADV_PEER_ADDR_PUBLIC;
        memset(hcc.peer_addr, 0, sizeof hcc.peer_addr);
    } else {
        hcc.filter_policy = BLE_HCI_CONN_FILT_NO_WL;
        hcc.peer_addr_type = ble_gap_conn_master.conn.addr_type;
        memcpy(hcc.peer_addr, ble_gap_conn_master.conn.addr,
               sizeof hcc.peer_addr);
    }
    hcc.own_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
    hcc.conn_itvl_min = ble_gap_conn_master.conn.params.itvl_min;
    hcc.conn_itvl_max = ble_gap_conn_master.conn.params.itvl_max;
    hcc.conn_latency = ble_gap_conn_master.conn.params.latency;
    hcc.supervision_timeout =
        ble_gap_conn_master.conn.params.supervision_timeout;
    hcc.min_ce_len = ble_gap_conn_master.conn.params.min_ce_len;
    hcc.max_ce_len = ble_gap_conn_master.conn.params.max_ce_len;

    ble_gap_conn_master.state = BLE_GAP_CONN_STATE_M_UNACKED;
    ble_hci_ack_set_callback(ble_gap_conn_create_ack, NULL);

    rc = host_hci_cmd_le_create_connection(&hcc);
    if (rc != 0) {
        ble_gap_conn_master_failed(rc);
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
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gap_conn_initiate(int addr_type, uint8_t *addr,
                      struct ble_gap_conn_crt_params *params,
                      ble_gap_conn_fn *cb, void *cb_arg)
{
    int rc;

    if (addr_type != BLE_ADDR_TYPE_PUBLIC &&
        addr_type != BLE_ADDR_TYPE_RANDOM &&
        addr_type != BLE_GAP_ADDR_TYPE_WL) {

        return BLE_HS_EINVAL;
    }

    /* Make sure no master connection attempt is already in progress. */
    if (ble_gap_conn_master_in_progress()) {
        return BLE_HS_EALREADY;
    }

    if (params == NULL) {
        ble_gap_conn_master.conn.params = ble_gap_conn_params_dflt;
    } else {
        /* XXX: Verify params. */
        ble_gap_conn_master.conn.params = *params;
    }

    ble_gap_conn_master.op = BLE_GAP_CONN_OP_M_CONN;
    ble_gap_conn_master.conn.addr_type = addr_type;
    ble_gap_conn_master.conn.cb = cb;
    ble_gap_conn_master.conn.cb_arg = cb_arg;

    if (addr_type != BLE_GAP_ADDR_TYPE_WL) {
        memcpy(ble_gap_conn_master.conn.addr, addr, BLE_DEV_ADDR_LEN);
    }

    rc = ble_gap_conn_master_enqueue(BLE_GAP_CONN_STATE_M_PENDING, 0,
                                     ble_gap_conn_create_tx, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $terminate connection procedure                                           *
 *****************************************************************************/

static void
ble_gap_conn_terminate_ack(struct ble_hci_ack *ack, void *arg)
{
    uint16_t handle;

    if (ack->bha_status != 0) {
        handle = (uintptr_t)arg;
        ble_gap_conn_notify_master_term_failure(ack->bha_status, handle);
    }
}

static int
ble_gap_conn_terminate_tx(void *arg)
{
    uint16_t handle;
    int rc;

    handle = (uintptr_t)arg;

    ble_hci_ack_set_callback(ble_gap_conn_terminate_ack, arg);

    rc = host_hci_cmd_disconnect(handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0) {
        ble_gap_conn_notify_master_term_failure(rc, handle);
    }

    return 0;
}

int
ble_gap_conn_terminate(uint16_t handle)
{
    int rc;

    if (ble_hs_conn_find(handle) == NULL) {
        return BLE_HS_ENOENT;
    }

    rc = ble_hci_sched_enqueue(ble_gap_conn_terminate_tx,
                               (void *)(uintptr_t)handle);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $cancel                                                                   *
 *****************************************************************************/

static void
ble_gap_conn_cancel_ack(struct ble_hci_ack *ack, void *arg)
{
    if (ack->bha_status != 0) {
        ble_gap_conn_notify_master_cancel_failure(ack->bha_status);
    }
}

static int
ble_gap_conn_cancel_tx(void *arg)
{
    int rc;

    ble_hci_ack_set_callback(ble_gap_conn_cancel_ack, arg);

    rc = host_hci_cmd_le_create_conn_cancel();
    if (rc != 0) {
        ble_gap_conn_notify_master_cancel_failure(rc);
    }

    return 0;
}

int
ble_gap_conn_cancel(void)
{
    int rc;

    if (!ble_gap_conn_master_in_progress()) {
        return BLE_HS_ENOENT;
    }

    rc = ble_hci_sched_enqueue(ble_gap_conn_cancel_tx, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $update connection parameters                                             *
 *****************************************************************************/

static void
ble_gap_conn_param_neg_reply_ack(struct ble_hci_ack *ack, void *arg)
{
    struct ble_gap_conn_update_entry *entry;

    entry = arg;

    assert(entry->state == BLE_GAP_CONN_STATE_U_NEG_REPLY);
    ble_gap_conn_update_entry_remove_free(entry);
}

static void
ble_gap_conn_param_reply_ack(struct ble_hci_ack *ack, void *arg)
{
    struct ble_gap_conn_update_entry *entry;

    entry = arg;

    assert(entry->state == BLE_GAP_CONN_STATE_U_REPLY);

    if (ack->bha_status != 0) {
        ble_gap_conn_update_failed(entry, ack->bha_status);
    } else {
        entry->state = BLE_GAP_CONN_STATE_U_REPLY_ACKED;
    }
}

void
ble_gap_conn_rx_param_req(struct hci_le_conn_param_req *evt)
{
    struct ble_gap_conn_update_entry *entry;
    struct hci_conn_param_neg_reply neg_reply;
    struct hci_conn_param_reply pos_reply;
    struct ble_gap_conn_upd_params peer_params;
    struct ble_hs_conn *conn;
    int rc;

    entry = ble_gap_conn_update_find(evt->connection_handle);
    if (entry != NULL) {
        /* Parameter update already in progress; reject peer's attempt. */
        /* XXX: Is this antisocial? */
        rc = BLE_ERR_UNSPECIFIED;
        goto err;
    }

    conn = ble_hs_conn_find(evt->connection_handle);
    if (conn == NULL) {
        return;
    }

    entry = ble_gap_conn_update_entry_alloc();
    if (entry == NULL) {
        /* Out of memory; reject. */
        rc = BLE_ERR_MEM_CAPACITY;
        goto err;
    }

    peer_params.itvl_min = evt->itvl_min;
    peer_params.itvl_max = evt->itvl_max;
    peer_params.latency = evt->latency;
    peer_params.supervision_timeout = evt->timeout;
    peer_params.min_ce_len = 0;
    peer_params.max_ce_len = 0;

    rc = ble_gap_conn_call_conn_cb(BLE_GAP_EVENT_CONN_UPDATE_REQ, 0, conn,
                                   &entry->params, &peer_params);
    if (rc != 0) {
        goto err;
    }

    pos_reply.handle = entry->conn_handle;
    pos_reply.conn_itvl_min = entry->params.itvl_min;
    pos_reply.conn_itvl_max = entry->params.itvl_max;
    pos_reply.conn_latency = entry->params.latency;
    pos_reply.supervision_timeout = entry->params.supervision_timeout;
    pos_reply.min_ce_len = entry->params.min_ce_len;
    pos_reply.max_ce_len = entry->params.max_ce_len;

    ble_hci_ack_set_callback(ble_gap_conn_param_reply_ack, entry);

    rc = host_hci_cmd_le_conn_param_reply(&pos_reply);
    if (rc != 0) {
        goto err;
    }

    return;

err:
    ble_gap_conn_update_failed(entry, rc);

    neg_reply.handle = evt->connection_handle;
    neg_reply.reason = rc;

    ble_hci_ack_set_callback(ble_gap_conn_param_neg_reply_ack, entry);

    host_hci_cmd_le_conn_param_neg_reply(&neg_reply);
}

static void
ble_gap_conn_update_ack(struct ble_hci_ack *ack, void *arg)
{
    struct ble_gap_conn_update_entry *entry;

    entry = arg;

    assert(entry->state == BLE_GAP_CONN_STATE_U_UPDATE);

    if (ack->bha_status != 0) {
        ble_gap_conn_update_failed(entry, ack->bha_status);
        return;
    }

    entry->state = BLE_GAP_CONN_STATE_U_UPDATE_ACKED;
}

static int
ble_gap_conn_update_tx(void *arg)
{
    struct ble_gap_conn_update_entry *entry;
    struct hci_conn_update cmd;
    int rc;

    entry = arg;

    assert(entry->state == BLE_GAP_CONN_STATE_U_UPDATE);

    cmd.handle = entry->conn_handle;
    cmd.conn_itvl_min = entry->params.itvl_min;
    cmd.conn_itvl_max = entry->params.itvl_max;
    cmd.conn_latency = entry->params.latency;
    cmd.supervision_timeout = entry->params.supervision_timeout;
    cmd.min_ce_len = entry->params.min_ce_len;
    cmd.max_ce_len = entry->params.max_ce_len;

    ble_hci_ack_set_callback(ble_gap_conn_update_ack, entry);

    rc = host_hci_cmd_le_conn_update(&cmd);
    if (rc != 0) {
        ble_gap_conn_update_failed(entry, rc);
    }

    return 0;
}

int
ble_gap_conn_update_params(uint16_t conn_handle,
                           struct ble_gap_conn_upd_params *params)
{
    struct ble_gap_conn_update_entry *entry;
    int rc;

    entry = ble_gap_conn_update_find(conn_handle);
    if (entry != NULL) {
        return BLE_HS_EALREADY;
    }

    if (ble_hs_conn_find(conn_handle) == NULL) {
        return BLE_HS_ENOENT;
    }

    entry = ble_gap_conn_update_entry_alloc();
    if (entry == NULL) {
        return BLE_HS_ENOMEM;
    }

    entry->conn_handle = conn_handle;
    entry->params = *params;
    entry->state = BLE_GAP_CONN_STATE_U_UPDATE;

    rc = ble_hci_sched_enqueue(ble_gap_conn_update_tx, entry);
    if (rc != 0) {
        SLIST_REMOVE(&ble_gap_conn_update_entries, entry,
                     ble_gap_conn_update_entry, next);
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $init                                                                     *
 *****************************************************************************/

static void
ble_gap_conn_init_slave_params(void)
{
    ble_gap_conn_slave.adv_params = (struct hci_adv_params) {
        .adv_itvl_min = 0,
        .adv_itvl_max = 0,
        .adv_type = BLE_HCI_ADV_TYPE_ADV_IND,
        .own_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC,
        .peer_addr_type = BLE_HCI_ADV_PEER_ADDR_PUBLIC,
        .adv_channel_map = BLE_HCI_ADV_CHANMASK_DEF,
        .adv_filter_policy = BLE_HCI_ADV_FILT_DEF,
    };
}

static void
ble_gap_conn_free_mem(void)
{
}

int
ble_gap_conn_init(void)
{
    int rc;

    ble_gap_conn_free_mem();

    memset(&ble_gap_conn_master, 0, sizeof ble_gap_conn_master);
    memset(&ble_gap_conn_slave, 0, sizeof ble_gap_conn_slave);
    memset(&ble_gap_conn_wl, 0, sizeof ble_gap_conn_wl);

    ble_gap_conn_init_slave_params();

    os_callout_func_init(&ble_gap_conn_master_timer, &ble_hs_evq,
                         ble_gap_conn_master_timer_exp, NULL);
    os_callout_func_init(&ble_gap_conn_slave_timer, &ble_hs_evq,
                         ble_gap_conn_slave_timer_exp, NULL);

    rc = os_mempool_init(&ble_gap_conn_update_pool, BLE_GAP_CONN_MAX_UPDATES,
                         sizeof (struct ble_gap_conn_update_entry),
                         ble_gap_conn_update_mem, "ble_gap_conn_update_pool");
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    SLIST_INIT(&ble_gap_conn_update_entries);
    return 0;

err:
    ble_gap_conn_free_mem();
    return rc;
}
