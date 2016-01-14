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
#include "os/os.h"
#include "ble_hs_priv.h"
#include "host/host_hci.h"
#include "ble_hci_ack.h"
#include "ble_hs_conn.h"
#include "ble_hs_adv.h"
#include "ble_hci_ack.h"
#include "ble_hci_sched.h"
#include "ble_gatt_priv.h"
#include "ble_gap_conn.h"

#define BLE_GAP_CONN_OP_NULL                                0

#define BLE_GAP_CONN_M_OP_DISC                              1
#define BLE_GAP_CONN_M_OP_CONN_AUTO                         2
#define BLE_GAP_CONN_M_OP_CONN_DIR                          3

#define BLE_GAP_CONN_S_OP_NON                               1
#define BLE_GAP_CONN_S_OP_UND                               2
#define BLE_GAP_CONN_S_OP_DIR                               3

/** General discovery master states. */
#define BLE_GAP_CONN_M_STATE_DISC_PARAMS                    0
#define BLE_GAP_CONN_M_STATE_DISC_ENABLE                    1
#define BLE_GAP_CONN_M_STATE_DISC_ACKED                     2

/** General discovery master states. */
#define BLE_GAP_CONN_M_STATE_AUTO_CLEAR_WL                  0
#define BLE_GAP_CONN_M_STATE_AUTO_ADD_WL                    1
#define BLE_GAP_CONN_M_STATE_AUTO_CREATE                    2
#define BLE_GAP_CONN_M_STATE_AUTO_ACKED                     3

/** General discovery master states. */
#define BLE_GAP_CONN_M_STATE_DIRECT_PENDING                 0
#define BLE_GAP_CONN_M_STATE_DIRECT_UNACKED                 1
#define BLE_GAP_CONN_M_STATE_DIRECT_ACKED                   2

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

/** 30 ms. */
#define BLE_GAP_ADV_FAST_INTERVAL1_MIN      (30 * 1000 / BLE_HCI_ADV_ITVL)

/** 60 ms. */
#define BLE_GAP_ADV_FAST_INTERVAL1_MAX      (60 * 1000 / BLE_HCI_ADV_ITVL)

/** 30 ms; active scanning. */
#define BLE_GAP_SCAN_FAST_INTERVAL_MIN      (30 * 1000 / BLE_HCI_ADV_ITVL)

/** 60 ms; active scanning. */
#define BLE_GAP_SCAN_FAST_INTERVAL_MAX      (60 * 1000 / BLE_HCI_ADV_ITVL)

/** 30 ms; active scanning. */
#define BLE_GAP_SCAN_FAST_WINDOW            (30 * 1000 / BLE_HCI_SCAN_ITVL)

/* 30.72 seconds; active scanning. */
#define BLE_GAP_SCAN_FAST_PERIOD            (30.72 * 1000)

/** 1.28 seconds; background scanning. */
#define BLE_GAP_SCAN_SLOW_INTERVAL1         (1280 * 1000 / BLE_HCI_SCAN_ITVL)

/** 11.25 ms; background scanning. */
#define BLE_GAP_SCAN_SLOW_WINDOW1           (11.25 * 1000 / BLE_HCI_SCAN_ITVL)

/** 10.24 seconds. */
#define BLE_GAP_GEN_DISC_SCAN_MIN           (10.24 * 1000)

#define BLE_GAP_CONN_MODE_MAX               4
#define BLE_GAP_DISC_MODE_MAX               4

/**
 * The maximum amount of user data that can be put into the advertising data.
 * Six bytes are reserved at the end for the flags field and the transmit power
 * field.
 */
#define BLE_GAP_CONN_ADV_DATA_LIMIT         (BLE_HCI_MAX_ADV_DATA_LEN - 6)

/**
 * The state of the in-progress master connection.  If no master connection is
 * currently in progress, then the op field is set to BLE_GAP_CONN_OP_NULL.
 */
static struct {
    uint8_t op;
    uint8_t state;

    union {
        struct {
            struct ble_gap_white_entry *wl;
            uint8_t wl_count;
            uint8_t wl_cur;
        } conn_auto;

        struct {
            uint8_t addr_type;
            uint8_t addr[6];
        } conn_dir;

        struct {
            uint8_t disc_mode;
        } disc;
    };
} ble_gap_conn_master;

/**
 * The state of the in-progress slave connection.  If no slave connection is
 * currently in progress, then the op field is set to BLE_GAP_CONN_OP_NULL.
 */
static struct {
    uint8_t op;
    uint8_t state;
    uint8_t disc_mode;

    uint8_t dir_addr_type;
    uint8_t dir_addr[BLE_DEV_ADDR_LEN];

    struct hci_adv_params adv_params;
    int8_t tx_pwr_lvl;
    uint8_t adv_data_len;
    uint8_t adv_data[BLE_HCI_MAX_ADV_DATA_LEN];
} ble_gap_conn_slave;

static int ble_gap_conn_adv_params_tx(void *arg);
static int ble_gap_conn_adv_power_tx(void *arg);
static int ble_gap_conn_adv_data_tx(void *arg);
static int ble_gap_conn_adv_rsp_data_tx(void *arg);
static int ble_gap_conn_adv_enable_tx(void *arg);
static int ble_gap_conn_auto_tx_add_wl(void *arg);

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

static ble_gap_connect_fn *ble_gap_conn_cb;
static void *ble_gap_conn_arg;

static struct os_callout_func ble_gap_conn_master_timer;
static struct os_callout_func ble_gap_conn_slave_timer;

/*****************************************************************************
 * $misc                                                                     *
 *****************************************************************************/

/**
 * Configures the connection event callback.  The callback is executed whenever
 * any of the following events occurs:
 *     o Connection creation succeeds.
 *     o Connection creation fails.
 *     o Connection establishment fails.
 *     o Established connection broken.
 */
void
ble_gap_conn_set_cb(ble_gap_connect_fn *cb, void *arg)
{
    ble_gap_conn_cb = cb;
    ble_gap_conn_arg = arg;
}

static void
ble_gap_conn_call_cb(struct ble_gap_conn_event *event)
{
    if (ble_gap_conn_cb != NULL) {
        ble_gap_conn_cb(event, ble_gap_conn_arg);
    }
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
    struct ble_gap_conn_event event;

    event.type = BLE_GAP_CONN_EVENT_TYPE_CONNECT;
    event.conn.status = status;
    if (conn != NULL) {
        event.conn.handle = conn->bhc_handle;
        memcpy(event.conn.peer_addr, conn->bhc_addr,
               sizeof event.conn.peer_addr);
    } else {
        event.conn.handle = 0;
        memset(&event.conn.peer_addr, 0, sizeof event.conn.peer_addr);
    }

    ble_gap_conn_call_cb(&event);
}

static void
ble_gap_conn_notify_terminate(uint16_t handle, int status, uint8_t reason)
{
    struct ble_gap_conn_event event;

    event.type = BLE_GAP_CONN_EVENT_TYPE_TERMINATE;
    event.term.handle = handle;
    event.term.status = status;
    event.term.reason = reason;

    ble_gap_conn_call_cb(&event);
}

static void
ble_gap_conn_notify_adv_done(int status)
{
    struct ble_gap_conn_event event;

    event.type = BLE_GAP_CONN_EVENT_TYPE_ADV_DONE;
    event.adv_done.status = status;

    ble_gap_conn_call_cb(&event);
}

static void
ble_gap_conn_master_reset_state(void)
{
    ble_gap_conn_master.op = BLE_GAP_CONN_OP_NULL;
}

static void
ble_gap_conn_slave_reset_state(void)
{
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
    struct ble_gap_conn_event event;
    uint8_t old_proc;

    os_callout_stop(&ble_gap_conn_master_timer.cf_c);

    old_proc = ble_gap_conn_master.op;
    ble_gap_conn_master_reset_state();

    switch (old_proc) {
    case BLE_GAP_CONN_M_OP_DISC:
        event.type = BLE_GAP_CONN_EVENT_TYPE_SCAN_DONE;
        ble_gap_conn_call_cb(&event);
        break;

    case BLE_GAP_CONN_M_OP_CONN_AUTO:
    case BLE_GAP_CONN_M_OP_CONN_DIR:
        ble_gap_conn_notify_connect(status, NULL);
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
ble_gap_conn_slave_failed(int status)
{
    os_callout_stop(&ble_gap_conn_slave_timer.cf_c);

    ble_gap_conn_slave_reset_state();
    ble_gap_conn_notify_connect(status, NULL);
}

void
ble_gap_conn_rx_disconn_complete(struct hci_disconn_complete *evt)
{
    struct ble_hs_conn *conn;

    /* Find the connection that this event refers to. */
    conn = ble_hs_conn_find(evt->connection_handle);
    if (conn == NULL) {
        return;
    }

    if (evt->status == 0) {
        ble_hs_conn_remove(conn);
        ble_hs_conn_free(conn);
    }

    ble_gap_conn_notify_terminate(evt->connection_handle, evt->status,
                                  evt->reason);
    ble_gattc_connection_broken(evt->connection_handle);
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
    int i;

    switch (ble_gap_conn_master.op) {
    case BLE_GAP_CONN_OP_NULL:
    case BLE_GAP_CONN_M_OP_DISC:
        return BLE_HS_ENOENT;

    case BLE_GAP_CONN_M_OP_CONN_DIR:
        if (ble_gap_conn_master.state != BLE_GAP_CONN_M_STATE_DIRECT_ACKED) {
            return BLE_HS_ENOENT;
        }

        if (addr_type == ble_gap_conn_master.conn_dir.addr_type &&
            memcmp(addr, ble_gap_conn_master.conn_dir.addr,
                   BLE_DEV_ADDR_LEN) == 0) {

            os_callout_stop(&ble_gap_conn_master_timer.cf_c);
            ble_gap_conn_master_reset_state();
            return 0;
        } else {
            ble_gap_conn_master_failed(BLE_HS_ECONTROLLER);
            return BLE_HS_ECONTROLLER;
        }

    case BLE_GAP_CONN_M_OP_CONN_AUTO:
        if (ble_gap_conn_master.state != BLE_GAP_CONN_M_STATE_AUTO_ACKED) {
            return BLE_HS_ENOENT;
        }

        for (i = 0; i < ble_gap_conn_master.conn_auto.wl_count; i++) {
            if (addr_type == ble_gap_conn_master.conn_auto.wl[i].addr_type &&
                memcmp(addr, ble_gap_conn_master.conn_auto.wl[i].addr,
                       BLE_DEV_ADDR_LEN) == 0) {

                break;
            }
        }
        if (i == ble_gap_conn_master.conn_auto.wl_count) {
            ble_gap_conn_master_failed(BLE_HS_ECONTROLLER);
            return BLE_HS_ECONTROLLER;
        } else {
            os_callout_stop(&ble_gap_conn_master_timer.cf_c);
            ble_gap_conn_master_reset_state();
            return 0;
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
    switch (ble_gap_conn_slave.op) {
    case BLE_GAP_CONN_OP_NULL:
    case BLE_GAP_CONN_S_OP_NON:
        return BLE_HS_ENOENT;

    case BLE_GAP_CONN_S_OP_UND:
        if (ble_gap_conn_slave.state != BLE_GAP_CONN_S_STATE_UND_ADV) {
            return BLE_HS_ENOENT;
        }

        os_callout_stop(&ble_gap_conn_slave_timer.cf_c);
        ble_gap_conn_slave_reset_state();
        return 0;

    case BLE_GAP_CONN_S_OP_DIR:
        if (ble_gap_conn_slave.state != BLE_GAP_CONN_S_STATE_DIR_ADV) {
            return BLE_HS_ENOENT;
        }

        if (ble_gap_conn_slave.dir_addr_type == addr_type &&
            memcmp(ble_gap_conn_slave.dir_addr, addr, BLE_DEV_ADDR_LEN) == 0) {

            os_callout_stop(&ble_gap_conn_slave_timer.cf_c);
            ble_gap_conn_slave_reset_state();
            return 0;
        } else {
            return BLE_HS_ENOENT;
        }

    default:
        assert(0);
        return BLE_HS_ENOENT;
    }
}

void
ble_gap_conn_rx_adv_report(struct ble_hs_adv *adv)
{
    struct ble_gap_conn_event event;
    int rc;

    if (ble_gap_conn_master.op      != BLE_GAP_CONN_M_OP_DISC &&
        ble_gap_conn_master.state   != BLE_GAP_CONN_M_STATE_DISC_ACKED) {

        return;
    }

    rc = ble_hs_adv_parse_fields(&event.adv.fields, adv->data,
                                 adv->length_data);
    if (rc != 0) {
        /* XXX: Increment stat. */
        return;
    }

    if (ble_gap_conn_master.disc.disc_mode == BLE_GAP_DISC_MODE_LTD &&
        !(event.adv.fields.flags & BLE_HS_ADV_F_DISC_LTD)) {

        return;
    }

    event.type = BLE_GAP_CONN_EVENT_TYPE_ADV_RPT;
    event.adv.event_type = adv->event_type;
    event.adv.addr_type = adv->addr_type;
    event.adv.length_data = adv->length_data;
    event.adv.rssi = adv->rssi;
    memcpy(event.adv.addr, adv->addr, sizeof event.adv.addr);
    event.adv.data = adv->data;

    ble_gap_conn_call_cb(&event);
}

/**
 * Processes an incoming connection-complete HCI event.
 */
int
ble_gap_conn_rx_conn_complete(struct hci_le_conn_complete *evt)
{
    struct ble_hs_conn *conn;
    int rc;

    /* Determine if this event refers to a completed connection or a connection
     * in progress.
     */
    conn = ble_hs_conn_find(evt->connection_handle);

    /* Apply the event to the existing connection if it exists. */
    if (conn != NULL) {
        if (evt->status != 0) {
            ble_hs_conn_remove(conn);
            ble_gap_conn_notify_connect(evt->status, conn);
            ble_hs_conn_free(conn);
        }

        return 0;
    }

    /* This event refers to a new connection. */

    if (evt->status != BLE_ERR_SUCCESS) {
        switch (evt->role) {
        case BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER:
            ble_gap_conn_master_failed(evt->status);
            break;

        case BLE_HCI_LE_CONN_COMPLETE_ROLE_SLAVE:
            ble_gap_conn_slave_failed(evt->status);
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
        if (rc == 0) {
            ble_gap_conn_master_reset_state();
        } else {
            return rc;
        }
        break;

    case BLE_HCI_LE_CONN_COMPLETE_ROLE_SLAVE:
        rc = ble_gap_conn_accept_slave_conn(evt->peer_addr_type,
                                            evt->peer_addr);
        if (rc == 0) {
            ble_gap_conn_slave_reset_state();
        } else {
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

    ble_hs_conn_insert(conn);

    ble_gap_conn_notify_connect(0, conn);

    return 0;
}

static void
ble_gap_conn_master_timer_exp(void *arg)
{
    int status;

    assert(ble_gap_conn_master_in_progress());

    switch (ble_gap_conn_master.op) {
    case BLE_GAP_CONN_M_OP_DISC:
        /* When a discovery procedure times out, it is not a failure. */
        status = 0;
        break;

    default:
        status = BLE_HS_ETIMEOUT;
        break;
    }

    ble_gap_conn_master_failed(status);
}

static void
ble_gap_conn_slave_timer_exp(void *arg)
{
    assert(ble_gap_conn_slave_in_progress());
    ble_gap_conn_slave_failed(BLE_HS_ETIMEOUT);
}

/*****************************************************************************
 * $stop advertise                                                           *
 *****************************************************************************/

static void
ble_gap_conn_adv_ack_disable(struct ble_hci_ack *ack, void *arg)
{
    if (ack->bha_status == 0) {
        /* Advertising should now be aborted. */
        ble_gap_conn_slave_reset_state();
        ble_gap_conn_notify_adv_done(0);
    }
}

static int
ble_gap_conn_adv_disable_tx(void *arg)
{
    int rc;

    ble_hci_ack_set_callback(ble_gap_conn_adv_ack_disable, NULL);
    rc = host_hci_cmd_le_set_adv_enable(0);
    if (rc != BLE_ERR_SUCCESS) {
        ble_gap_conn_notify_adv_done(BLE_HS_ECONTROLLER);
        return 1;
    }

    return 0;
}

static int
ble_gap_conn_adv_stop(void)
{
    int rc;

    /* Do nothing if advertising is already disabled. */
    if (ble_gap_conn_slave.op == BLE_GAP_CONN_OP_NULL) {
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
            ble_gap_conn_slave_failed(rc);
        }
    }
}

static void
ble_gap_conn_adv_ack(struct ble_hci_ack *ack, void *arg)
{
    if (ack->bha_status != 0) {
        ble_gap_conn_slave_failed(ack->bha_status);
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
        ble_gap_conn_slave_failed(rc);
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
        ble_gap_conn_slave_failed(rc);
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
        ble_gap_conn_slave_failed(rc);
        return 1;
    }

    return 0;
}

static void
ble_gap_conn_adv_power_ack(struct ble_hci_ack *ack, void *arg)
{
    int8_t power_level;

    if (ack->bha_status != 0) {
        ble_gap_conn_slave_failed(ack->bha_status);
        return;
    }

    if (ack->bha_params_len != BLE_HCI_ADV_CHAN_TXPWR_ACK_PARAM_LEN) {
        ble_gap_conn_slave_failed(BLE_HS_ECONTROLLER);
        return;
    }

    power_level = ack->bha_params[1];
    if (power_level < BLE_HCI_ADV_CHAN_TXPWR_MIN ||
        power_level > BLE_HCI_ADV_CHAN_TXPWR_MAX) {

        /* XXX: Probably can do something nicer than abort the entire
         * procedure.
         */
        ble_gap_conn_slave_failed(BLE_HS_ECONTROLLER);
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
        ble_gap_conn_slave_failed(rc);
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
        ble_gap_conn_slave_failed(rc);
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
ble_gap_conn_advertise(uint8_t discoverable_mode, uint8_t connectable_mode,
                       uint8_t *peer_addr, uint8_t peer_addr_type)
{
    int rc;

    if (discoverable_mode == BLE_GAP_DISC_MODE_NULL ||
        connectable_mode == BLE_GAP_DISC_MODE_NULL) {

        rc = ble_gap_conn_adv_stop();
        return rc;
    }

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
        ble_gap_conn_slave.op = BLE_GAP_CONN_S_OP_DIR;
        ble_gap_conn_slave.dir_addr_type = peer_addr_type;
        memcpy(ble_gap_conn_slave.dir_addr, peer_addr, 6);
        break;

    default:
        assert(0);
        break;
    }

    ble_gap_conn_slave.state = 0;
    ble_gap_conn_slave.disc_mode = discoverable_mode;

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
ble_gap_conn_disc_ack_enable(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_conn_master.op == BLE_GAP_CONN_M_OP_DISC);
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

    assert(ble_gap_conn_master.op == BLE_GAP_CONN_M_OP_DISC);
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
    assert(ble_gap_conn_master.op == BLE_GAP_CONN_M_OP_DISC);
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

    assert(ble_gap_conn_master.op == BLE_GAP_CONN_M_OP_DISC);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_M_STATE_DISC_PARAMS);

    ble_hci_ack_set_callback(ble_gap_conn_disc_ack_params, NULL);
    rc = host_hci_cmd_le_set_scan_params(BLE_HCI_SCAN_TYPE_ACTIVE,
                                         BLE_GAP_SCAN_FAST_INTERVAL_MIN,
                                         BLE_GAP_SCAN_FAST_WINDOW,
                                         BLE_HCI_ADV_OWN_ADDR_PUBLIC,
                                         BLE_HCI_SCAN_FILT_NO_WL);
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
ble_gap_conn_disc(uint32_t duration_ms, uint8_t discovery_mode)
{
    int rc;

    if (discovery_mode != BLE_GAP_DISC_MODE_LTD &&
        discovery_mode != BLE_GAP_DISC_MODE_GEN) {

        return BLE_HS_EINVAL;
    }

    /* Make sure no master connection attempt is already in progress. */
    if (ble_gap_conn_master_in_progress()) {
        return BLE_HS_EALREADY;
    }

    if (duration_ms == 0) {
        duration_ms = BLE_GAP_GEN_DISC_SCAN_MIN;
    }

    ble_gap_conn_master.op = BLE_GAP_CONN_M_OP_DISC;
    ble_gap_conn_master.disc.disc_mode = discovery_mode;
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
 * $auto connection establishment procedure                                  *
 *****************************************************************************/

/**
 * Processes an HCI acknowledgement (either command status or command complete)
 * while a master connection is being established.
 */
static void
ble_gap_conn_auto_ack_create(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_conn_master.op == BLE_GAP_CONN_M_OP_CONN_AUTO);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_M_STATE_AUTO_CREATE);

    if (ack->bha_status != 0) {
        ble_gap_conn_master_failed(ack->bha_status);
        return;
    }

    ble_gap_conn_master.state = BLE_GAP_CONN_M_STATE_AUTO_ACKED;
}

static int
ble_gap_conn_auto_tx_create(void *arg)
{
    struct hci_create_conn hcc;
    int rc;

    assert(ble_gap_conn_master.op == BLE_GAP_CONN_M_OP_CONN_AUTO);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_M_STATE_AUTO_CREATE);

    hcc.scan_itvl = 0x0010;
    hcc.scan_window = 0x0010;
    hcc.filter_policy = BLE_HCI_CONN_FILT_USE_WL;
    hcc.peer_addr_type = BLE_HCI_ADV_PEER_ADDR_PUBLIC;
    memset(hcc.peer_addr, 0, sizeof hcc.peer_addr);
    hcc.own_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
    hcc.conn_itvl_min = 24;
    hcc.conn_itvl_max = 40;
    hcc.conn_latency = 0;
    hcc.supervision_timeout = 0x0100; // XXX
    hcc.min_ce_len = 0x0010; // XXX
    hcc.max_ce_len = 0x0300; // XXX

    ble_hci_ack_set_callback(ble_gap_conn_auto_ack_create, NULL);

    rc = host_hci_cmd_le_create_connection(&hcc);
    if (rc != 0) {
        ble_gap_conn_master_failed(rc);
        return 1;
    }

    return 0;
}

static void
ble_gap_conn_auto_ack_add_wl(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_conn_master.op == BLE_GAP_CONN_M_OP_CONN_AUTO);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_M_STATE_AUTO_ADD_WL);

    if (ack->bha_status != 0) {
        ble_gap_conn_master_failed(ack->bha_status);
        return;
    }

    ble_gap_conn_master.conn_auto.wl_cur++;
    if (ble_gap_conn_master.conn_auto.wl_cur <
        ble_gap_conn_master.conn_auto.wl_count) {

        ble_gap_conn_master_enqueue(BLE_GAP_CONN_M_STATE_AUTO_ADD_WL, 1,
                                    ble_gap_conn_auto_tx_add_wl, NULL);
    } else {
        ble_gap_conn_master_enqueue(BLE_GAP_CONN_M_STATE_AUTO_CREATE, 1,
                                    ble_gap_conn_auto_tx_create, NULL);
    }
}

static int
ble_gap_conn_auto_tx_add_wl(void *arg)
{
    struct ble_gap_white_entry *white_entry;
    int rc;

    assert(ble_gap_conn_master.op == BLE_GAP_CONN_M_OP_CONN_AUTO);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_M_STATE_AUTO_ADD_WL);
    assert(ble_gap_conn_master.conn_auto.wl != NULL);
    assert(ble_gap_conn_master.conn_auto.wl_cur <
           ble_gap_conn_master.conn_auto.wl_count);

    ble_hci_ack_set_callback(ble_gap_conn_auto_ack_add_wl, NULL);

    white_entry = ble_gap_conn_master.conn_auto.wl +
                  ble_gap_conn_master.conn_auto.wl_cur;
    rc = host_hci_cmd_le_add_to_whitelist(white_entry->addr,
                                          white_entry->addr_type);
    if (rc != 0) {
        ble_gap_conn_master_failed(rc);
        return 1;
    }

    return 0;
}

static void
ble_gap_conn_auto_ack_clear_wl(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_conn_master.op == BLE_GAP_CONN_M_OP_CONN_AUTO);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_M_STATE_AUTO_CLEAR_WL);

    if (ack->bha_status != 0) {
        ble_gap_conn_master_failed(ack->bha_status);
        return;
    }

    ble_gap_conn_master_enqueue(BLE_GAP_CONN_M_STATE_AUTO_ADD_WL, 1,
                                ble_gap_conn_auto_tx_add_wl, NULL);
}

static int
ble_gap_conn_auto_tx_clear_wl(void *arg)
{
    int rc;

    assert(ble_gap_conn_master.op == BLE_GAP_CONN_M_OP_CONN_AUTO);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_M_STATE_AUTO_CLEAR_WL);

    ble_hci_ack_set_callback(ble_gap_conn_auto_ack_clear_wl, NULL);

    rc = host_hci_cmd_le_clear_whitelist();
    if (rc != 0) {
        ble_gap_conn_master_failed(rc);
        return 1;
    }

    return 0;
}

/**
 * Performs the Auto Connection Establishment Procedure, as described in
 * vol. 3, part C, section 9.3.5.
 *
 * @param addr_type             The peer's address type; one of:
 *                                  o BLE_HCI_ADV_PEER_ADDR_PUBLIC
 *                                  o BLE_HCI_ADV_PEER_ADDR_RANDOM
 *                                  o BLE_HCI_ADV_PEER_ADDR_PUBLIC_IDENT
 *                                  o BLE_HCI_ADV_PEER_ADDR_RANDOM_IDENT
 * @param addr                  The address of the peer to connect to.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gap_conn_auto_connect(struct ble_gap_white_entry *white_list,
                          int white_list_count)
{
    int rc;
    int i;

    if (white_list_count <= 0) {
        return BLE_HS_EINVAL;
    }

    /* XXX: Verify white list not too large. */

    for (i = 0; i < white_list_count; i++) {
        if (white_list[i].addr_type != BLE_ADDR_TYPE_PUBLIC &&
            white_list[i].addr_type != BLE_ADDR_TYPE_RANDOM) {

            return BLE_HS_EINVAL;
        }
    }

    /* Make sure no master connection attempt is already in progress. */
    if (ble_gap_conn_master_in_progress()) {
        return BLE_HS_EALREADY;
    }

    ble_gap_conn_master.op = BLE_GAP_CONN_M_OP_CONN_AUTO;
    ble_gap_conn_master.conn_auto.wl = white_list;
    ble_gap_conn_master.conn_auto.wl_count = white_list_count;
    ble_gap_conn_master.conn_auto.wl_cur = 0;

    rc = ble_gap_conn_master_enqueue(BLE_GAP_CONN_M_STATE_AUTO_CLEAR_WL, 0,
                                     ble_gap_conn_auto_tx_clear_wl, NULL);
    return rc;
}

/*****************************************************************************
 * $direct connection establishment procedure                                *
 *****************************************************************************/

/**
 * Processes an HCI acknowledgement (either command status or command complete)
 * while a master connection is being established.
 */
static void
ble_gap_conn_direct_connect_ack(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_conn_master.op == BLE_GAP_CONN_M_OP_CONN_DIR);
    assert(ble_gap_conn_master.state ==
           BLE_GAP_CONN_M_STATE_DIRECT_UNACKED);

    if (ack->bha_status != 0) {
        ble_gap_conn_master_failed(ack->bha_status);
        return;
    }

    ble_gap_conn_master.state = BLE_GAP_CONN_M_STATE_DIRECT_ACKED;
}

static int
ble_gap_conn_direct_connect_tx(void *arg)
{
    struct hci_create_conn hcc;
    int rc;

    assert(ble_gap_conn_master.op == BLE_GAP_CONN_M_OP_CONN_DIR);
    assert(ble_gap_conn_master.state == BLE_GAP_CONN_M_STATE_DIRECT_PENDING);

    hcc.scan_itvl = 0x0010;
    hcc.scan_window = 0x0010;
    hcc.filter_policy = BLE_HCI_CONN_FILT_NO_WL;
    hcc.peer_addr_type = BLE_HCI_ADV_PEER_ADDR_PUBLIC;
    memcpy(hcc.peer_addr, ble_gap_conn_master.conn_dir.addr,
           sizeof hcc.peer_addr);
    hcc.own_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
    hcc.conn_itvl_min = 24;
    hcc.conn_itvl_max = 40;
    hcc.conn_latency = 0;
    hcc.supervision_timeout = 0x0100; // XXX
    hcc.min_ce_len = 0x0010; // XXX
    hcc.max_ce_len = 0x0300; // XXX

    ble_gap_conn_master.state = BLE_GAP_CONN_M_STATE_DIRECT_UNACKED;
    ble_hci_ack_set_callback(ble_gap_conn_direct_connect_ack, NULL);

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
 *                                  o BLE_HCI_ADV_PEER_ADDR_PUBLIC
 *                                  o BLE_HCI_ADV_PEER_ADDR_RANDOM
 *                                  o BLE_HCI_ADV_PEER_ADDR_PUBLIC_IDENT
 *                                  o BLE_HCI_ADV_PEER_ADDR_RANDOM_IDENT
 * @param addr                  The address of the peer to connect to.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gap_conn_direct_connect(int addr_type, uint8_t *addr)
{
    int rc;

    if (addr_type != BLE_ADDR_TYPE_PUBLIC &&
        addr_type != BLE_ADDR_TYPE_RANDOM) {

        return BLE_HS_EINVAL;
    }

    /* Make sure no master connection attempt is already in progress. */
    if (ble_gap_conn_master_in_progress()) {
        return BLE_HS_EALREADY;
    }

    ble_gap_conn_master.op = BLE_GAP_CONN_M_OP_CONN_DIR;
    ble_gap_conn_master.conn_dir.addr_type = addr_type;
    memcpy(ble_gap_conn_master.conn_dir.addr, addr, BLE_DEV_ADDR_LEN);

    rc = ble_gap_conn_master_enqueue(BLE_GAP_CONN_M_STATE_DIRECT_PENDING, 0,
                                     ble_gap_conn_direct_connect_tx, NULL);
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

    handle = (uintptr_t)arg;

    if (ack->bha_status != 0) {
        ble_gap_conn_notify_terminate(handle, ack->bha_status, 0);
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
        return rc;
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
        /* XXX: This may be ambiguous to the application. */
        ble_gap_conn_notify_connect(BLE_HS_ECONTROLLER, NULL);
    }
}

static int
ble_gap_conn_cancel_tx(void *arg)
{
    int rc;

    ble_hci_ack_set_callback(ble_gap_conn_cancel_ack, arg);

    rc = host_hci_cmd_le_create_conn_cancel();
    if (rc != 0) {
        return 1;
    }

    return 0;
}

int
ble_gap_conn_cancel(void)
{
    int rc;

    if (!ble_gap_conn_master_in_progress()) {
        return BLE_HS_EALREADY;
    }

    rc = ble_hci_sched_enqueue(ble_gap_conn_cancel_tx, NULL);
    if (rc != 0) {
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
        .adv_itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN,
        .adv_itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX,
        .adv_type = BLE_HCI_ADV_TYPE_ADV_IND,
        .own_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC,
        .peer_addr_type = BLE_HCI_ADV_PEER_ADDR_PUBLIC,
        .adv_channel_map = BLE_HCI_ADV_CHANMASK_DEF,
        .adv_filter_policy = BLE_HCI_ADV_FILT_DEF,
    };
}

int
ble_gap_conn_init(void)
{
    ble_gap_conn_cb = NULL;
    ble_gap_conn_master.op = BLE_GAP_CONN_OP_NULL;
    ble_gap_conn_slave.op = BLE_GAP_CONN_OP_NULL;

    ble_gap_conn_init_slave_params();

    os_callout_func_init(&ble_gap_conn_master_timer, &ble_hs_evq,
                         ble_gap_conn_master_timer_exp, NULL);
    os_callout_func_init(&ble_gap_conn_slave_timer, &ble_hs_evq,
                         ble_gap_conn_slave_timer_exp, NULL);
    return 0;
}
