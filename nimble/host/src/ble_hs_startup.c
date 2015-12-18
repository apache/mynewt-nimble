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

#include <stddef.h>
#include <assert.h>
#include "host/host_hci.h"
#include "host/ble_hs.h"
#include "ble_hci_ack.h"
#include "ble_hci_sched.h"
#include "ble_hs_startup.h"

#define BLE_HS_STARTUP_STATE_IDLE                       0
#define BLE_HS_STARTUP_STATE_RESET                      1
#define BLE_HS_STARTUP_STATE_RESET_ACKED                2
#define BLE_HS_STARTUP_STATE_SET_EVENT_MASK             3
#define BLE_HS_STARTUP_STATE_SET_EVENT_MASK_ACKED       4
#define BLE_HS_STARTUP_STATE_LE_SET_EVENT_MASK          5
#define BLE_HS_STARTUP_STATE_LE_SET_EVENT_MASK_ACKED    6
#define BLE_HS_STARTUP_STATE_LE_READ_BUF_SIZE           7
#define BLE_HS_STARTUP_STATE_LE_READ_BUF_SIZE_ACKED     8

static uint8_t ble_hs_startup_state;

static void
ble_hs_startup_failure(int status)
{
    ble_hs_startup_state = BLE_HS_STARTUP_STATE_IDLE;
    /* XXX: Signal failure. */
}

static void
ble_hs_startup_read_buf_size_ack(struct ble_hci_ack *ack, void *arg)
{
    uint16_t pktlen;
    uint8_t max_pkts;
    int rc;

    assert(ble_hs_startup_state ==
           BLE_HS_STARTUP_STATE_LE_READ_BUF_SIZE_ACKED - 1);

    if (ack->bha_status != 0) {
        ble_hs_startup_failure(ack->bha_status);
        return;
    }

    if (ack->bha_params_len != BLE_HCI_RD_BUF_SIZE_RSPLEN + 1) {
        ble_hs_startup_failure(BLE_HS_EBADDATA);
        return;
    }

    pktlen = le16toh(ack->bha_params + 1);
    max_pkts = ack->bha_params[3];

    rc = host_hci_set_buf_size(pktlen, max_pkts);
    if (rc != 0) {
        ble_hs_startup_failure(rc);
        return;
    }

    ble_hs_startup_state = BLE_HS_STARTUP_STATE_LE_READ_BUF_SIZE_ACKED;

    /* XXX: Send read buffer size. */
}

static int
ble_hs_startup_le_read_buf_size_tx(void *arg)
{
    int rc;

    assert(ble_hs_startup_state == BLE_HS_STARTUP_STATE_LE_READ_BUF_SIZE - 1);

    ble_hs_startup_state = BLE_HS_STARTUP_STATE_LE_READ_BUF_SIZE;
    ble_hci_ack_set_callback(ble_hs_startup_read_buf_size_ack, NULL);

    rc = host_hci_cmd_le_read_buffer_size();
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
ble_hs_startup_le_set_event_mask_ack(struct ble_hci_ack *ack, void *arg)
{
    int rc;

    assert(ble_hs_startup_state ==
           BLE_HS_STARTUP_STATE_LE_SET_EVENT_MASK_ACKED - 1);

    if (ack->bha_status != 0) {
        ble_hs_startup_failure(ack->bha_status);
        return;
    }

    ble_hs_startup_state = BLE_HS_STARTUP_STATE_LE_SET_EVENT_MASK_ACKED;

    /* XXX: Send LE Set event mask. */
    rc = ble_hci_sched_enqueue(ble_hs_startup_le_read_buf_size_tx, NULL);
    if (rc != 0) {
        ble_hs_startup_failure(rc);
        return;
    }
}

static int
ble_hs_startup_le_set_event_mask_tx(void *arg)
{
    int rc;

    assert(ble_hs_startup_state == BLE_HS_STARTUP_STATE_LE_SET_EVENT_MASK - 1);

    ble_hs_startup_state = BLE_HS_STARTUP_STATE_LE_SET_EVENT_MASK;
    ble_hci_ack_set_callback(ble_hs_startup_le_set_event_mask_ack, NULL);

    rc = host_hci_cmd_le_set_event_mask(0x000000000000001f);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
ble_hs_startup_set_event_mask_ack(struct ble_hci_ack *ack, void *arg)
{
    int rc;

    assert(ble_hs_startup_state ==
           BLE_HS_STARTUP_STATE_SET_EVENT_MASK_ACKED - 1);

    if (ack->bha_status != 0) {
        ble_hs_startup_failure(ack->bha_status);
        return;
    }

    ble_hs_startup_state = BLE_HS_STARTUP_STATE_SET_EVENT_MASK_ACKED;

    rc = ble_hci_sched_enqueue(ble_hs_startup_le_set_event_mask_tx, NULL);
    if (rc != 0) {
        ble_hs_startup_failure(rc);
        return;
    }
}

static int
ble_hs_startup_set_event_mask_tx(void *arg)
{
    int rc;

    assert(ble_hs_startup_state == BLE_HS_STARTUP_STATE_SET_EVENT_MASK - 1);

    ble_hs_startup_state = BLE_HS_STARTUP_STATE_SET_EVENT_MASK;
    ble_hci_ack_set_callback(ble_hs_startup_set_event_mask_ack, NULL);

    rc = host_hci_cmd_set_event_mask(0x20001fffffffffff);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
ble_hs_startup_reset_ack(struct ble_hci_ack *ack, void *arg)
{
    int rc;

    assert(ble_hs_startup_state == BLE_HS_STARTUP_STATE_RESET_ACKED - 1);

    if (ack->bha_status != 0) {
        ble_hs_startup_failure(ack->bha_status);
        return;
    }

    ble_hs_startup_state = BLE_HS_STARTUP_STATE_RESET_ACKED;

    rc = ble_hci_sched_enqueue(ble_hs_startup_set_event_mask_tx, NULL);
    if (rc != 0) {
        ble_hs_startup_failure(rc);
        return;
    }
}


static int
ble_hs_startup_reset_tx(void *arg)
{
    int rc;

    assert(ble_hs_startup_state == BLE_HS_STARTUP_STATE_RESET - 1);

    ble_hs_startup_state = BLE_HS_STARTUP_STATE_RESET;
    ble_hci_ack_set_callback(ble_hs_startup_reset_ack, NULL);

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

    ble_hs_startup_state = BLE_HS_STARTUP_STATE_IDLE;

    rc = ble_hci_sched_enqueue(ble_hs_startup_reset_tx, NULL);
    if (rc != 0) {
        ble_hs_startup_failure(rc);
        return rc;
    }

    return 0;
}
