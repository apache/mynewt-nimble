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
#include "host/host_hci.h"
#include "ble_hs_ack.h"
#include "ble_hs_conn.h"
#include "ble_gap_conn.h"

int ble_gap_conn_state_master;
int ble_gap_conn_state_slave;

static uint8_t ble_gap_conn_addr_master[BLE_DEV_ADDR_LEN];
static uint8_t ble_gap_conn_addr_slave[BLE_DEV_ADDR_LEN];

/**
 * Initiates a connection using the GAP Direct Connection Establishment
 * Procedure.
 * XXX: Add timeout parameter.
 *
 * @return 0 on success; nonzero on failure.
 */
int
ble_gap_conn_initiate_direct(int addr_type, uint8_t *addr)
{
    struct hci_create_conn hcc;
    int rc;

    /* Make sure no master connection attempt is already in progress. */
    if (ble_gap_conn_state_master != BLE_GAP_CONN_STATE_NULL) {
        return EALREADY;
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

    rc = host_hci_cmd_le_create_connection(&hcc);
    if (rc != 0) {
        return rc;
    }

    ble_gap_conn_state_master = BLE_GAP_CONN_STATE_MASTER_DIRECT_UNACKED;
    memcpy(ble_gap_conn_addr_master, addr, BLE_DEV_ADDR_LEN);

    return 0;
}

/* XXX: Add timeout parameter. */
int
ble_gap_conn_advertise_direct(int addr_type, uint8_t *addr)
{
    struct hci_adv_params hap;
    int rc;

    /* Make sure no slave connection attempt is already in progress. */
    if (ble_gap_conn_state_slave != BLE_GAP_CONN_STATE_NULL) {
        return EALREADY;
    }

    hap.adv_itvl_min = BLE_HCI_ADV_ITVL_DEF;
    hap.adv_itvl_max = BLE_HCI_ADV_ITVL_DEF;
    hap.adv_type = BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD;
    hap.own_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
    hap.peer_addr_type = addr_type;
    memcpy(hap.peer_addr, addr, sizeof hap.peer_addr);
    hap.adv_channel_map = BLE_HCI_ADV_CHANMASK_DEF;
    hap.adv_filter_policy = BLE_HCI_ADV_FILT_DEF;

    rc = host_hci_cmd_le_set_adv_params(&hap);
    if (rc != 0) {
        return rc;
    }

    ble_gap_conn_state_slave = BLE_GAP_CONN_STATE_SLAVE_DIRECT_UNACKED;
    memcpy(ble_gap_conn_addr_slave, addr, BLE_DEV_ADDR_LEN);

    return 0;
}

int
ble_gap_conn_rx_ack_create_conn(struct ble_hs_ack *ack)
{
    if (ble_gap_conn_state_master != BLE_GAP_CONN_STATE_MASTER_DIRECT_UNACKED) {
        return ENOENT;
    }

    if (ack->bha_status == BLE_ERR_SUCCESS) {
        ble_gap_conn_state_master = BLE_GAP_CONN_STATE_MASTER_DIRECT_ACKED;
    } else {
        ble_gap_conn_state_master = BLE_GAP_CONN_STATE_NULL;
    }

    return 0;
}

static int
ble_gap_conn_accept_conn(uint8_t *addr)
{
    switch (ble_gap_conn_state_master) {
    case BLE_GAP_CONN_STATE_MASTER_DIRECT_ACKED:
        if (memcmp(ble_gap_conn_addr_master, addr, BLE_DEV_ADDR_LEN) == 0) {
            ble_gap_conn_state_master = BLE_GAP_CONN_STATE_NULL;
            return 0;
        }
        break;
    }

    switch (ble_gap_conn_state_slave) {
    case BLE_GAP_CONN_STATE_SLAVE_DIRECT_ACKED:
        if (memcmp(ble_gap_conn_addr_slave, addr, BLE_DEV_ADDR_LEN) == 0) {
            ble_gap_conn_state_slave = BLE_GAP_CONN_STATE_NULL;
            return 0;
        }
        break;
    }

    return ENOENT;
}


int
ble_gap_conn_rx_conn_complete(struct hci_le_conn_complete *evt)
{
    struct ble_hs_conn *conn;
    int rc;

    rc = ble_gap_conn_accept_conn(evt->peer_addr);
    if (rc != 0) {
        return ENOENT;
    }

    if (evt->status != BLE_ERR_SUCCESS) {
        return 0;
    }

    /* XXX: Ensure device address is expected. */
    /* XXX: Ensure event fields are acceptable. */

    conn = ble_hs_conn_alloc();
    if (conn == NULL) {
        return ENOMEM;
    }

    conn->bhc_handle = evt->connection_handle;
    memcpy(conn->bhc_addr, evt->peer_addr, sizeof conn->bhc_addr);

    ble_hs_conn_insert(conn);

    /* XXX: Notify someone. */

    return 0;
}

int
ble_gap_conn_rx_ack_set_adv_params(struct ble_hs_ack *ack)
{
    if (ble_gap_conn_state_slave != BLE_GAP_CONN_STATE_SLAVE_DIRECT_UNACKED) {
        return ENOENT;
    }

    if (ack->bha_status == BLE_ERR_SUCCESS) {
        ble_gap_conn_state_slave = BLE_GAP_CONN_STATE_SLAVE_DIRECT_ACKED;
    } else {
        ble_gap_conn_state_slave = BLE_GAP_CONN_STATE_NULL;
    }

    return 0;
}

int
ble_gap_conn_init(void)
{
    ble_gap_conn_state_master = BLE_GAP_CONN_STATE_NULL;
    ble_gap_conn_state_slave = BLE_GAP_CONN_STATE_NULL;

    return 0;
}
