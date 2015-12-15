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
#include "ble_hci_ack.h"
#include "ble_hci_sched.h"
#include "ble_gap_conn.h"

#define BLE_GAP_CONN_STATE_IDLE                             0

#define BLE_GAP_CONN_STATE_M_GEN_DISC_PENDING               1
#define BLE_GAP_CONN_STATE_M_GEN_DISC_PARAMS                2
#define BLE_GAP_CONN_STATE_M_GEN_DISC_PARAMS_ACKED          3
#define BLE_GAP_CONN_STATE_M_GEN_DISC_ENABLE                4
#define BLE_GAP_CONN_STATE_M_GEN_DISC_ENABLE_ACKED          5

#define BLE_GAP_CONN_STATE_M_DIRECT_PENDING                 6
#define BLE_GAP_CONN_STATE_M_DIRECT_UNACKED                 7
#define BLE_GAP_CONN_STATE_M_DIRECT_ACKED                   8

#define BLE_GAP_CONN_STATE_S_PENDING                        1
#define BLE_GAP_CONN_STATE_S_PARAMS                         2
#define BLE_GAP_CONN_STATE_S_PARAMS_ACKED                   3
#define BLE_GAP_CONN_STATE_S_ENABLE                         4
#define BLE_GAP_CONN_STATE_S_ENABLE_ACKED                   5

/** 30 ms. */
#define BLE_GAP_ADV_FAST_INTERVAL1_MIN      (30 * 1000 / BLE_HCI_ADV_ITVL)

/** 60 ms. */
#define BLE_GAP_ADV_FAST_INTERVAL1_MAX      (60 * 1000 / BLE_HCI_ADV_ITVL)

/** 1.28 seconds. */
#define BLE_GAP_SCAN_SLOW_INTERVAL1         (1280 * 1000 / BLE_HCI_SCAN_ITVL)

/** 11.25 ms. */
#define BLE_GAP_SCAN_SLOW_WINDOW1           (11.25 * 1000 / BLE_HCI_SCAN_ITVL)

/** 10.24 seconds. */
#define BLE_GAP_GEN_DISC_SCAN_MIN           (10.24 * 1000)

ble_gap_connect_fn *ble_gap_conn_cb;
void *ble_gap_conn_arg;

static int ble_gap_conn_master_state;
static int ble_gap_conn_slave_state;
static uint8_t ble_gap_conn_master_addr[BLE_DEV_ADDR_LEN];
static uint8_t ble_gap_conn_slave_addr[BLE_DEV_ADDR_LEN];
static struct os_callout_func ble_gap_conn_master_timer;
static struct os_callout_func ble_gap_conn_slave_timer;

/*****************************************************************************
 * @misc                                                                     *
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

/**
 * Calls the connect callback if one is configured.
 *
 * @param status                The HCI status of the connection attempt.
 * @param conn                  The connection this notification concerns;
 *                                  null if a connection was never created.
 */
static void
ble_gap_conn_notify_app(uint8_t status, struct ble_hs_conn *conn)
{
    struct ble_gap_connect_desc desc;

    if (ble_gap_conn_cb != NULL) {
        desc.status = status;
        if (conn != NULL) {
            desc.handle = conn->bhc_handle;
            memcpy(desc.peer_addr, conn->bhc_addr, sizeof desc.peer_addr);
        } else {
            desc.handle = 0;
            memset(&desc.peer_addr, 0, sizeof desc.peer_addr);
        }

        ble_gap_conn_cb(&desc, ble_gap_conn_arg);
    }
}

/**
 * Called when an error is encountered while the master-connection-fsm is
 * active.  Resets the state machine, clears the HCI ack callback, and notifies
 * the host task that the next hci_batch item can be processed.
 */
static void
ble_gap_conn_master_failed(uint8_t status)
{
    os_callout_stop(&ble_gap_conn_master_timer.cf_c);

    ble_gap_conn_master_state = BLE_GAP_CONN_STATE_IDLE;
    ble_hci_ack_set_callback(NULL, NULL);
    ble_gap_conn_notify_app(status, NULL);
}

/**
 * Called when an error is encountered while the slave-connection-fsm is
 * active.  Resets the state machine, clears the HCI ack callback, and notifies
 * the host task that the next hci_batch item can be processed.
 */
static void
ble_gap_conn_slave_failed(uint8_t status)
{
    os_callout_stop(&ble_gap_conn_slave_timer.cf_c);

    ble_gap_conn_slave_state = BLE_GAP_CONN_STATE_IDLE;
    ble_hci_ack_set_callback(NULL, NULL);
    ble_gap_conn_notify_app(status, NULL);
}

int
ble_gap_conn_rx_disconn_complete(struct hci_disconn_complete *evt)
{
    struct ble_hs_conn *conn;

    conn = ble_hs_conn_find(evt->connection_handle);
    if (conn == NULL) {
        return BLE_HS_ENOENT;
    }

    if (evt->status == 0) {
        ble_hs_conn_remove(conn);
        ble_gap_conn_notify_app(evt->reason, conn);
        ble_hs_conn_free(conn);
    } else {
        /* XXX: Ensure we have a disconnect operation in progress. */
        ble_gap_conn_notify_app(evt->status, conn);
    }

    return 0;
}

/**
 * Tells you if the BLE host is in the process of creating a master connection.
 */
int
ble_gap_conn_master_in_progress(void)
{
    return ble_gap_conn_master_state != BLE_GAP_CONN_STATE_IDLE;
}

/**
 * Tells you if the BLE host is in the process of creating a slave connection.
 */
int
ble_gap_conn_slave_in_progress(void)
{
    return ble_gap_conn_slave_state != BLE_GAP_CONN_STATE_IDLE;
}

static int
ble_gap_conn_accept_new_conn(uint8_t *addr)
{
    switch (ble_gap_conn_master_state) {
    case BLE_GAP_CONN_STATE_M_DIRECT_ACKED:
        if (memcmp(ble_gap_conn_master_addr, addr, BLE_DEV_ADDR_LEN) == 0) {
            os_callout_stop(&ble_gap_conn_master_timer.cf_c);
            ble_gap_conn_master_state = BLE_GAP_CONN_STATE_IDLE;
            return 0;
        }
    }

    switch (ble_gap_conn_slave_state) {
    case BLE_GAP_CONN_STATE_S_ENABLE_ACKED:
        if (memcmp(ble_gap_conn_slave_addr, addr, BLE_DEV_ADDR_LEN) == 0) {
            os_callout_stop(&ble_gap_conn_master_timer.cf_c);
            ble_gap_conn_slave_state = BLE_GAP_CONN_STATE_IDLE;
            return 0;
        }
        break;
    }

    return BLE_HS_ENOENT;
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
            ble_gap_conn_notify_app(evt->status, conn);
            ble_hs_conn_free(conn);
        }

        return 0;
    }

    /* This event refers to a new connection. */
    rc = ble_gap_conn_accept_new_conn(evt->peer_addr);
    if (rc != 0) {
        return BLE_HS_ENOENT;
    }

    if (evt->status != BLE_ERR_SUCCESS) {
        return 0;
    }

    /* XXX: Ensure device address is expected. */
    /* XXX: Ensure event fields are acceptable. */

    conn = ble_hs_conn_alloc();
    if (conn == NULL) {
        /* XXX: Ensure this never happens. */
        ble_gap_conn_notify_app(BLE_ERR_MEM_CAPACITY, NULL);
        return BLE_HS_ENOMEM;
    }

    conn->bhc_handle = evt->connection_handle;
    memcpy(conn->bhc_addr, evt->peer_addr, sizeof conn->bhc_addr);

    ble_hs_conn_insert(conn);

    ble_gap_conn_notify_app(0, conn);

    return 0;
}

static void
ble_gap_conn_master_timer_exp(void *arg)
{
    uint8_t status;

    assert(ble_gap_conn_master_in_progress());

    switch (ble_gap_conn_master_state) {
    case BLE_GAP_CONN_STATE_M_GEN_DISC_ENABLE_ACKED:
        /* When a discovery procedure times out, it is not a failure. */
        status = 0;
        break;

    default:
        status = 1; /* XXX */
        break;
    }

    ble_gap_conn_master_failed(status);
}

static void
ble_gap_conn_slave_timer_exp(void *arg)
{
    assert(ble_gap_conn_slave_in_progress());
    ble_gap_conn_slave_failed(1 /*XXX*/);
}

/*****************************************************************************
 * @general discovery procedure                                              *
 *****************************************************************************/

static void
ble_gap_conn_gen_disc_ack_enable(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_conn_master_state == BLE_GAP_CONN_STATE_M_GEN_DISC_ENABLE);

    if (ack->bha_status != 0) {
        ble_gap_conn_master_failed(ack->bha_status);
    } else {
        ble_gap_conn_master_state = BLE_GAP_CONN_STATE_M_GEN_DISC_ENABLE_ACKED;
    }
}

static int
ble_gap_conn_gen_disc_tx_enable(void *arg)
{
    int rc;

    assert(ble_gap_conn_master_state ==
           BLE_GAP_CONN_STATE_M_GEN_DISC_PARAMS_ACKED);

    ble_gap_conn_master_state = BLE_GAP_CONN_STATE_M_GEN_DISC_ENABLE;
    ble_hci_ack_set_callback(ble_gap_conn_gen_disc_ack_enable, NULL);

    rc = host_hci_cmd_le_set_scan_enable(1, 0);
    if (rc != 0) {
        ble_gap_conn_master_failed(rc);
        return rc;
    }

    return 0;
}

static void
ble_gap_conn_gen_disc_ack_params(struct ble_hci_ack *ack, void *arg)
{
    int rc;

    assert(ble_gap_conn_master_state == BLE_GAP_CONN_STATE_M_GEN_DISC_PARAMS);

    if (ack->bha_status != 0) {
        ble_gap_conn_master_failed(ack->bha_status);
        return;
    }

    ble_gap_conn_master_state = BLE_GAP_CONN_STATE_M_GEN_DISC_PARAMS_ACKED;

    rc = ble_hci_sched_enqueue(ble_gap_conn_gen_disc_tx_enable, NULL);
    if (rc != 0) {
        ble_gap_conn_master_failed(rc);
        return;
    }
}

static int
ble_gap_conn_gen_disc_tx_params(void *arg)
{
    int rc;

    assert(ble_gap_conn_master_state == BLE_GAP_CONN_STATE_M_GEN_DISC_PENDING);

    ble_gap_conn_master_state = BLE_GAP_CONN_STATE_M_GEN_DISC_PARAMS;
    ble_hci_ack_set_callback(ble_gap_conn_gen_disc_ack_params, NULL);

    rc = host_hci_cmd_le_set_scan_params(BLE_HCI_SCAN_TYPE_ACTIVE,
                                         BLE_GAP_SCAN_SLOW_INTERVAL1,
                                         BLE_GAP_SCAN_SLOW_WINDOW1,
                                         BLE_HCI_ADV_OWN_ADDR_PUBLIC,
                                         BLE_HCI_SCAN_FILT_NO_WL);
    if (rc != 0) {
        ble_gap_conn_master_failed(rc);
        return rc;
    }

    return 0;
}

/**
 * Performs the General Discovery Procedure, as described in
 * vol. 3, part C, section 9.2.6.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_gap_conn_gen_disc(uint32_t duration_ms)
{
    int rc;

    /* Make sure no master connection attempt is already in progress. */
    if (ble_gap_conn_master_in_progress()) {
        return BLE_HS_EALREADY;
    }

    if (duration_ms == 0) {
        duration_ms = BLE_GAP_GEN_DISC_SCAN_MIN;
    }

    ble_gap_conn_master_state = BLE_GAP_CONN_STATE_M_GEN_DISC_PENDING;
    memset(ble_gap_conn_master_addr, 0, BLE_DEV_ADDR_LEN);

    rc = ble_hci_sched_enqueue(ble_gap_conn_gen_disc_tx_params, NULL);
    if (rc != 0) {
        ble_gap_conn_master_state = BLE_GAP_CONN_STATE_IDLE;
        return rc;
    }

    os_callout_reset(&ble_gap_conn_master_timer.cf_c,
                     duration_ms * OS_TICKS_PER_SEC / 1000);

    return 0;
}

/*****************************************************************************
 * @directed connectable mode                                                *
 *****************************************************************************/

static void
ble_gap_conn_direct_connectable_ack_enable(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_conn_slave_state == BLE_GAP_CONN_STATE_S_ENABLE);

    if (ack->bha_status != BLE_ERR_SUCCESS) {
        ble_gap_conn_slave_failed(ack->bha_status);
    } else {
        ble_gap_conn_slave_state = BLE_GAP_CONN_STATE_S_ENABLE_ACKED;
    }
}

static int
ble_gap_conn_direct_connectable_tx_enable(void *arg)
{
    int rc;

    assert(ble_gap_conn_slave_state == BLE_GAP_CONN_STATE_S_PARAMS_ACKED);

    rc = host_hci_cmd_le_set_adv_enable(1);
    if (rc != BLE_ERR_SUCCESS) {
        ble_gap_conn_slave_failed(rc);
        return 1;
    }

    ble_hci_ack_set_callback(ble_gap_conn_direct_connectable_ack_enable, NULL);
    ble_gap_conn_slave_state = BLE_GAP_CONN_STATE_S_ENABLE;

    return 0;
}

static void
ble_gap_conn_direct_connectable_ack_params(struct ble_hci_ack *ack, void *arg)
{
    int rc;

    assert(ble_gap_conn_slave_state == BLE_GAP_CONN_STATE_S_PARAMS);

    if (ack->bha_status != BLE_ERR_SUCCESS) {
        ble_gap_conn_slave_failed(ack->bha_status);
        return;
    }

    ble_gap_conn_slave_state = BLE_GAP_CONN_STATE_S_PARAMS_ACKED;

    rc = ble_hci_sched_enqueue(ble_gap_conn_direct_connectable_tx_enable,
                               NULL);
    if (rc != 0) {
        ble_gap_conn_slave_failed(rc);
        return;
    }
}

static int
ble_gap_conn_direct_connectable_tx_params(void *arg)
{
    struct hci_adv_params hap;
    int rc;

    assert(ble_gap_conn_slave_state == BLE_GAP_CONN_STATE_S_PENDING);

    hap.adv_itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    hap.adv_itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;
    hap.adv_type = BLE_HCI_ADV_TYPE_ADV_IND;
    hap.own_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
    hap.peer_addr_type = BLE_HCI_ADV_PEER_ADDR_PUBLIC;
    memcpy(hap.peer_addr, ble_gap_conn_slave_addr, sizeof hap.peer_addr);
    hap.adv_channel_map = BLE_HCI_ADV_CHANMASK_DEF;
    hap.adv_filter_policy = BLE_HCI_ADV_FILT_DEF;

    ble_gap_conn_slave_state = BLE_GAP_CONN_STATE_S_PARAMS;
    ble_hci_ack_set_callback(ble_gap_conn_direct_connectable_ack_params, NULL);

    rc = host_hci_cmd_le_set_adv_params(&hap);
    if (rc != 0) {
        ble_gap_conn_slave_failed(rc);
        return 1;
    }

    return 0;
}

/**
 * Enables Directed Connectable Mode, as described in vol. 3, part C, section
 * 9.3.3.
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
ble_gap_conn_direct_connectable(int addr_type, uint8_t *addr)
{
    int rc;

    /* Make sure no slave connection attempt is already in progress. */
    if (ble_gap_conn_slave_in_progress()) {
        return BLE_HS_EALREADY;
    }

    ble_gap_conn_slave_state = BLE_GAP_CONN_STATE_S_PENDING;
    memcpy(ble_gap_conn_slave_addr, addr, BLE_DEV_ADDR_LEN);

    rc = ble_hci_sched_enqueue(ble_gap_conn_direct_connectable_tx_params,
                               NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * @direct connection establishment procedure                                *
 *****************************************************************************/

/**
 * Processes an HCI acknowledgement (either command status or command complete)
 * while a master connection is being established.
 */
static void
ble_gap_conn_direct_connect_ack(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_gap_conn_master_state ==
           BLE_GAP_CONN_STATE_M_DIRECT_UNACKED);

    if (ack->bha_status != 0) {
        ble_gap_conn_master_failed(ack->bha_status);
        return;
    }

    ble_gap_conn_master_state = BLE_GAP_CONN_STATE_M_DIRECT_ACKED;
}

static int
ble_gap_conn_direct_connect_tx(void *arg)
{
    struct hci_create_conn hcc;
    int rc;

    assert(ble_gap_conn_master_state == BLE_GAP_CONN_STATE_M_DIRECT_PENDING);

    hcc.scan_itvl = 0x0010;
    hcc.scan_window = 0x0010;
    hcc.filter_policy = BLE_HCI_CONN_FILT_NO_WL;
    hcc.peer_addr_type = BLE_HCI_ADV_PEER_ADDR_PUBLIC;
    memcpy(hcc.peer_addr, ble_gap_conn_master_addr, sizeof hcc.peer_addr);
    hcc.own_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
    hcc.conn_itvl_min = 24;
    hcc.conn_itvl_max = 40;
    hcc.conn_latency = 0;
    hcc.supervision_timeout = 0x0100; // XXX
    hcc.min_ce_len = 0x0010; // XXX
    hcc.max_ce_len = 0x0300; // XXX

    ble_gap_conn_master_state = BLE_GAP_CONN_STATE_M_DIRECT_UNACKED;
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

    /* Make sure no master connection attempt is already in progress. */
    if (ble_gap_conn_master_in_progress()) {
        return BLE_HS_EALREADY;
    }

    ble_gap_conn_master_state = BLE_GAP_CONN_STATE_M_DIRECT_PENDING;
    memcpy(ble_gap_conn_master_addr, addr, BLE_DEV_ADDR_LEN);

    rc = ble_hci_sched_enqueue(ble_gap_conn_direct_connect_tx, NULL);
    if (rc != 0) {
        ble_gap_conn_master_state = BLE_GAP_CONN_STATE_IDLE;
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * @init                                                                     *
 *****************************************************************************/

int
ble_gap_conn_init(void)
{
    ble_gap_conn_cb = NULL;
    ble_gap_conn_master_state = BLE_GAP_CONN_STATE_IDLE;
    ble_gap_conn_slave_state = BLE_GAP_CONN_STATE_IDLE;
    memset(ble_gap_conn_master_addr, 0, sizeof ble_gap_conn_master_addr);
    memset(ble_gap_conn_slave_addr, 0, sizeof ble_gap_conn_slave_addr);

    os_callout_func_init(&ble_gap_conn_master_timer, &ble_hs_evq,
                         ble_gap_conn_master_timer_exp, NULL);
    os_callout_func_init(&ble_gap_conn_slave_timer, &ble_hs_evq,
                         ble_gap_conn_slave_timer_exp, NULL);
    return 0;
}
