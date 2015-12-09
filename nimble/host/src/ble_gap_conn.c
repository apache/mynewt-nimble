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
#include "ble_hs_ack.h"
#include "ble_hs_conn.h"
#include "ble_hs_ack.h"
#include "ble_hs_hci_batch.h"
#include "ble_gap_conn.h"

#define BLE_GAP_CONN_STATE_IDLE                     0

#define BLE_GAP_CONN_STATE_MASTER_DIRECT_UNACKED    1
#define BLE_GAP_CONN_STATE_MASTER_DIRECT_ACKED      2

#define BLE_GAP_CONN_STATE_SLAVE_UNACKED            1
#define BLE_GAP_CONN_STATE_SLAVE_PARAMS_ACKED       2
#define BLE_GAP_CONN_STATE_SLAVE_ENABLE_ACKED       3

#define BLE_GAP_ADV_FAST_INTERVAL1_MIN      (30 * 1000 / BLE_HCI_ADV_ITVL)
#define BLE_GAP_ADV_FAST_INTERVAL1_MAX      (60 * 1000 / BLE_HCI_ADV_ITVL)

ble_gap_connect_fn *ble_gap_conn_cb;
void *ble_gap_conn_arg;

static int ble_gap_conn_master_state;
static int ble_gap_conn_slave_state;
static uint8_t ble_gap_conn_addr_master[BLE_DEV_ADDR_LEN];
static uint8_t ble_gap_conn_addr_slave[BLE_DEV_ADDR_LEN];

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
    ble_gap_conn_master_state = BLE_GAP_CONN_STATE_IDLE;
    ble_hs_ack_set_callback(NULL, NULL);
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
    ble_gap_conn_slave_state = BLE_GAP_CONN_STATE_IDLE;
    ble_hs_ack_set_callback(NULL, NULL);
    ble_gap_conn_notify_app(status, NULL);
}

/**
 * Processes an HCI acknowledgement (either command status or command complete)
 * while a master connection is being established.
 */
static void
ble_gap_conn_master_ack(struct ble_hs_ack *ack, void *arg)
{
    assert(ble_gap_conn_master_state ==
           BLE_GAP_CONN_STATE_MASTER_DIRECT_UNACKED);

    if (ack->bha_status != 0) {
        ble_gap_conn_master_failed(ack->bha_status);
    } else {
        ble_gap_conn_master_state = BLE_GAP_CONN_STATE_MASTER_DIRECT_ACKED;
        ble_hs_hci_batch_done();
    }
}

/**
 * Processes an HCI acknowledgement (either command status or command complete)
 * while a slave connection is being established.
 */
static void
ble_gap_conn_slave_ack(struct ble_hs_ack *ack, void *arg)
{
    int rc;

    switch (ble_gap_conn_slave_state) {
    case BLE_GAP_CONN_STATE_SLAVE_UNACKED:
        if (ack->bha_status != BLE_ERR_SUCCESS) {
            ble_gap_conn_slave_failed(ack->bha_status);
        } else {
            ble_gap_conn_slave_state = BLE_GAP_CONN_STATE_SLAVE_PARAMS_ACKED;
            ble_hs_ack_set_callback(ble_gap_conn_slave_ack, NULL);
            rc = host_hci_cmd_le_set_adv_enable(1);
            if (rc != BLE_ERR_SUCCESS) {
                ble_gap_conn_slave_failed(rc);
            }
        }
        break;

    case BLE_GAP_CONN_STATE_SLAVE_PARAMS_ACKED:
        if (ack->bha_status != 0) {
            ble_gap_conn_slave_failed(ack->bha_status);
        } else {
            ble_gap_conn_slave_state = BLE_GAP_CONN_STATE_SLAVE_ENABLE_ACKED;
            ble_hs_hci_batch_done();
        }
        break;

    default:
        assert(0);
        break;
    }
}

/**
 * Initiates a connection using the GAP Direct Connection Establishment
 * Procedure.
 *
 * @return 0 on success; nonzero on failure.
 */
int
ble_gap_conn_direct_connect(int addr_type, uint8_t *addr)
{
    struct hci_create_conn hcc;
    int rc;

    /* Make sure no master connection attempt is already in progress. */
    if (ble_gap_conn_master_in_progress()) {
        rc = BLE_HS_EALREADY;
        goto err;
    }

    hcc.scan_itvl = 0x0010;
    hcc.scan_window = 0x0010;
    hcc.filter_policy = BLE_HCI_CONN_FILT_NO_WL;
    hcc.peer_addr_type = addr_type;
    memcpy(hcc.peer_addr, addr, sizeof hcc.peer_addr);
    hcc.own_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
    hcc.conn_itvl_min = 24;
    hcc.conn_itvl_max = 40;
    hcc.conn_latency = 0;
    hcc.supervision_timeout = 0x0100; // XXX
    hcc.min_ce_len = 0x0010; // XXX
    hcc.max_ce_len = 0x0300; // XXX

    memcpy(ble_gap_conn_addr_master, addr, BLE_DEV_ADDR_LEN);

    rc = host_hci_cmd_le_create_connection(&hcc);
    if (rc != 0) {
        goto err;
    }

    ble_gap_conn_master_state = BLE_GAP_CONN_STATE_MASTER_DIRECT_UNACKED;
    ble_hs_ack_set_callback(ble_gap_conn_master_ack, NULL);

    return 0;

err:
    ble_gap_conn_notify_app(rc, NULL);
    return rc;
}

/**
 * Enables the GAP Directed Connectable Mode.
 *
 * @return 0 on success; nonzero on failure.
 */
int
ble_gap_conn_direct_advertise(int addr_type, uint8_t *addr)
{
    struct hci_adv_params hap;
    int rc;

    /* Make sure no slave connection attempt is already in progress. */
    if (ble_gap_conn_slave_in_progress()) {
        rc = BLE_HS_EALREADY;
        goto err;
    }

    hap.adv_itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    hap.adv_itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;
    hap.adv_type = BLE_HCI_ADV_TYPE_ADV_IND;
    hap.own_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
    hap.peer_addr_type = addr_type;
    memcpy(hap.peer_addr, addr, sizeof hap.peer_addr);
    hap.adv_channel_map = BLE_HCI_ADV_CHANMASK_DEF;
    hap.adv_filter_policy = BLE_HCI_ADV_FILT_DEF;

    memcpy(ble_gap_conn_addr_slave, addr, BLE_DEV_ADDR_LEN);

    rc = host_hci_cmd_le_set_adv_params(&hap);
    if (rc != 0) {
        goto err;
    }

    ble_gap_conn_slave_state = BLE_GAP_CONN_STATE_SLAVE_UNACKED;
    ble_hs_ack_set_callback(ble_gap_conn_slave_ack, NULL);

    return 0;

err:
    ble_gap_conn_notify_app(rc, NULL);
    return rc;
}

static int
ble_gap_conn_accept_new_conn(uint8_t *addr)
{
    switch (ble_gap_conn_master_state) {
    case BLE_GAP_CONN_STATE_MASTER_DIRECT_ACKED:
        if (memcmp(ble_gap_conn_addr_master, addr, BLE_DEV_ADDR_LEN) == 0) {
            ble_gap_conn_master_state = BLE_GAP_CONN_STATE_IDLE;
            return 0;
        }
    }

    switch (ble_gap_conn_slave_state) {
    case BLE_GAP_CONN_STATE_SLAVE_ENABLE_ACKED:
        if (memcmp(ble_gap_conn_addr_slave, addr, BLE_DEV_ADDR_LEN) == 0) {
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

int
ble_gap_conn_init(void)
{
    ble_gap_conn_cb = NULL;
    ble_gap_conn_master_state = BLE_GAP_CONN_STATE_IDLE;
    ble_gap_conn_slave_state = BLE_GAP_CONN_STATE_IDLE;
    memset(ble_gap_conn_addr_master, 0, sizeof ble_gap_conn_addr_master);
    memset(ble_gap_conn_addr_slave, 0, sizeof ble_gap_conn_addr_slave);

    return 0;
}
