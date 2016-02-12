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

#include <stddef.h>
#include <assert.h>
#include "host/host_hci.h"
#include "host/ble_hs.h"
#include "ble_hci_ack.h"
#include "ble_hci_sched.h"
#include "ble_hs_startup.h"

#define BLE_HS_STARTUP_STATE_IDLE                       0
#define BLE_HS_STARTUP_STATE_RESET                      1
/* XXX: Read local supported commands. */
/* XXX: Read local supported features. */
#define BLE_HS_STARTUP_STATE_SET_EVMASK                 2
#define BLE_HS_STARTUP_STATE_LE_SET_EVMASK              3
#define BLE_HS_STARTUP_STATE_LE_READ_BUF_SZ             4
/* XXX: Read buffer size. */
#define BLE_HS_STARTUP_STATE_LE_READ_SUP_F              5
/* XXX: Read BD_ADDR. */
#define BLE_HS_STARTUP_STATE_MAX                        6

static uint8_t ble_hs_startup_state;

static int ble_hs_startup_reset_tx(void *arg);
static int ble_hs_startup_set_evmask_tx(void *arg);
static int ble_hs_startup_le_set_evmask_tx(void *arg);
static int ble_hs_startup_le_read_buf_sz_tx(void *arg);
static int ble_hs_startup_le_read_sup_f_tx(void *arg);

static ble_hci_sched_tx_fn * const
    ble_hs_startup_dispatch[BLE_HS_STARTUP_STATE_MAX] = {

    [BLE_HS_STARTUP_STATE_IDLE]             = NULL,
    [BLE_HS_STARTUP_STATE_RESET]            = ble_hs_startup_reset_tx,
    [BLE_HS_STARTUP_STATE_SET_EVMASK]       = ble_hs_startup_set_evmask_tx,
    [BLE_HS_STARTUP_STATE_LE_SET_EVMASK]    = ble_hs_startup_le_set_evmask_tx,
    [BLE_HS_STARTUP_STATE_LE_READ_BUF_SZ]   = ble_hs_startup_le_read_buf_sz_tx,
    [BLE_HS_STARTUP_STATE_LE_READ_SUP_F]    = ble_hs_startup_le_read_sup_f_tx,
};

static void
ble_hs_startup_failure(int status)
{
    ble_hs_startup_state = BLE_HS_STARTUP_STATE_IDLE;
    /* XXX: Signal failure. */
}

static int
ble_hs_startup_enqueue_tx(void)
{
    ble_hci_sched_tx_fn *tx_fn;
    int rc;

    if (ble_hs_startup_state == BLE_HS_STARTUP_STATE_MAX) {
        return 0;
    }

    tx_fn = ble_hs_startup_dispatch[ble_hs_startup_state];
    assert(tx_fn != NULL);

    rc = ble_hci_sched_enqueue(tx_fn, NULL, NULL);
    if (rc != 0) {
        ble_hs_startup_failure(rc);
        return rc;
    }

    return 0;
}

static void
ble_hs_startup_gen_ack(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_hs_startup_state < BLE_HS_STARTUP_STATE_MAX);

    if (ack->bha_status != 0) {
        ble_hs_startup_failure(ack->bha_status);
        return;
    }

    ble_hs_startup_state++;
    ble_hs_startup_enqueue_tx();
}

static void
ble_hs_startup_le_read_sup_f_ack(struct ble_hci_ack *ack, void *arg)
{
    assert(ble_hs_startup_state == BLE_HS_STARTUP_STATE_LE_READ_SUP_F);

    if (ack->bha_status != 0) {
        ble_hs_startup_failure(ack->bha_status);
        return;
    }

    if (ack->bha_params_len != BLE_HCI_RD_LOC_SUPP_FEAT_RSPLEN) {
        ble_hs_startup_failure(BLE_HS_ECONTROLLER);
        return;
    }

    /* XXX: Do something with the supported features bit map. */
}

static int
ble_hs_startup_le_read_sup_f_tx(void *arg)
{
    int rc;

    assert(ble_hs_startup_state == BLE_HS_STARTUP_STATE_LE_READ_SUP_F);

    ble_hci_ack_set_callback(ble_hs_startup_le_read_sup_f_ack, NULL);
    rc = host_hci_cmd_le_read_loc_supp_feat();
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
ble_hs_startup_le_read_buf_size_ack(struct ble_hci_ack *ack, void *arg)
{
    uint16_t pktlen;
    uint8_t max_pkts;
    int rc;

    assert(ble_hs_startup_state == BLE_HS_STARTUP_STATE_LE_READ_BUF_SZ);

    if (ack->bha_status != 0) {
        ble_hs_startup_failure(ack->bha_status);
        return;
    }

    if (ack->bha_params_len != BLE_HCI_RD_BUF_SIZE_RSPLEN + 1) {
        ble_hs_startup_failure(BLE_HS_ECONTROLLER);
        return;
    }

    pktlen = le16toh(ack->bha_params + 1);
    max_pkts = ack->bha_params[3];

    rc = host_hci_set_buf_size(pktlen, max_pkts);
    if (rc != 0) {
        ble_hs_startup_failure(rc);
        return;
    }

    ble_hs_startup_state++;
    ble_hs_startup_enqueue_tx();
}

static int
ble_hs_startup_le_read_buf_sz_tx(void *arg)
{
    int rc;

    assert(ble_hs_startup_state == BLE_HS_STARTUP_STATE_LE_READ_BUF_SZ);

    ble_hci_ack_set_callback(ble_hs_startup_le_read_buf_size_ack, NULL);
    rc = host_hci_cmd_le_read_buffer_size();
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_hs_startup_le_set_evmask_tx(void *arg)
{
    int rc;

    assert(ble_hs_startup_state == BLE_HS_STARTUP_STATE_LE_SET_EVMASK);

    ble_hci_ack_set_callback(ble_hs_startup_gen_ack, NULL);
    rc = host_hci_cmd_le_set_event_mask(0x000000000000001f);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_hs_startup_set_evmask_tx(void *arg)
{
    int rc;

    assert(ble_hs_startup_state == BLE_HS_STARTUP_STATE_SET_EVMASK);

    ble_hci_ack_set_callback(ble_hs_startup_gen_ack, NULL);
    rc = host_hci_cmd_set_event_mask(0x20001fffffffffff);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_hs_startup_reset_tx(void *arg)
{
    int rc;

    assert(ble_hs_startup_state == BLE_HS_STARTUP_STATE_RESET);

    ble_hci_ack_set_callback(ble_hs_startup_gen_ack, NULL);
    rc = host_hci_cmd_reset();
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_hs_startup_go(void)
{
    int rc;

    ble_hs_startup_state = BLE_HS_STARTUP_STATE_RESET;

    rc = ble_hs_startup_enqueue_tx();
    return rc;
}
