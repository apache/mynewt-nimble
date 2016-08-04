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
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "os/os.h"
#include "nimble/ble_hci_trans.h"
#include "ble_hs_priv.h"
#include "host_dbg_priv.h"

#define BLE_HCI_CMD_TIMEOUT     (OS_TICKS_PER_SEC)

static struct os_mutex ble_hci_cmd_mutex;
static struct os_sem ble_hci_cmd_sem;

static uint8_t *ble_hci_cmd_ack;

#if PHONY_HCI_ACKS
static ble_hci_cmd_phony_ack_fn *ble_hci_cmd_phony_ack_cb;
#endif

#if PHONY_HCI_ACKS
void
ble_hci_set_phony_ack_cb(ble_hci_cmd_phony_ack_fn *cb)
{
    ble_hci_cmd_phony_ack_cb = cb;
}
#endif

static void
ble_hci_cmd_lock(void)
{
    int rc;

    rc = os_mutex_pend(&ble_hci_cmd_mutex, 0xffffffff);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0 || rc == OS_NOT_STARTED);
}

static void
ble_hci_cmd_unlock(void)
{
    int rc;

    rc = os_mutex_release(&ble_hci_cmd_mutex);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0 || rc == OS_NOT_STARTED);
}

static int
ble_hci_cmd_rx_cmd_complete(uint8_t event_code, uint8_t *data, int len,
                            struct ble_hci_ack *out_ack)
{
    uint16_t opcode;
    uint8_t *params;
    uint8_t params_len;
    uint8_t num_pkts;

    if (len < BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    num_pkts = data[2];
    opcode = le16toh(data + 3);
    params = data + 5;

    /* XXX: Process num_pkts field. */
    (void)num_pkts;

    out_ack->bha_opcode = opcode;

    params_len = len - BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN;
    if (params_len > 0) {
        out_ack->bha_status = BLE_HS_HCI_ERR(params[0]);
    } else if (opcode == BLE_HCI_OPCODE_NOP) {
        out_ack->bha_status = 0;
    } else {
        out_ack->bha_status = BLE_HS_ECONTROLLER;
    }

    /* Don't include the status byte in the parameters blob. */
    if (params_len > 1) {
        out_ack->bha_params = params + 1;
        out_ack->bha_params_len = params_len - 1;
    } else {
        out_ack->bha_params = NULL;
        out_ack->bha_params_len = 0;
    }

    return 0;
}

static int
ble_hci_cmd_rx_cmd_status(uint8_t event_code, uint8_t *data, int len,
                          struct ble_hci_ack *out_ack)
{
    uint16_t opcode;
    uint8_t num_pkts;
    uint8_t status;

    if (len < BLE_HCI_EVENT_CMD_STATUS_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    status = data[2];
    num_pkts = data[3];
    opcode = le16toh(data + 4);

    /* XXX: Process num_pkts field. */
    (void)num_pkts;

    out_ack->bha_opcode = opcode;
    out_ack->bha_params = NULL;
    out_ack->bha_params_len = 0;
    out_ack->bha_status = BLE_HS_HCI_ERR(status);

    return 0;
}

static int
ble_hci_cmd_process_ack(uint16_t expected_opcode,
                        uint8_t *params_buf, uint8_t params_buf_len,
                        struct ble_hci_ack *out_ack)
{
    uint8_t event_code;
    uint8_t param_len;
    uint8_t event_len;
    int rc;

    BLE_HS_DBG_ASSERT(ble_hci_cmd_ack != NULL);

    /* Count events received */
    STATS_INC(ble_hs_stats, hci_event);

    /* Display to console */
    host_hci_dbg_event_disp(ble_hci_cmd_ack);

    event_code = ble_hci_cmd_ack[0];
    param_len = ble_hci_cmd_ack[1];
    event_len = param_len + 2;

    /* Clear ack fields up front to silence spurious gcc warnings. */
    memset(out_ack, 0, sizeof *out_ack);

    switch (event_code) {
    case BLE_HCI_EVCODE_COMMAND_COMPLETE:
        rc = ble_hci_cmd_rx_cmd_complete(event_code, ble_hci_cmd_ack,
                                         event_len, out_ack);
        break;

    case BLE_HCI_EVCODE_COMMAND_STATUS:
        rc = ble_hci_cmd_rx_cmd_status(event_code, ble_hci_cmd_ack,
                                       event_len, out_ack);
        break;

    default:
        BLE_HS_DBG_ASSERT(0);
        rc = BLE_HS_EUNKNOWN;
        break;
    }

    if (rc == 0) {
        if (params_buf == NULL) {
            out_ack->bha_params_len = 0;
        } else {
            if (out_ack->bha_params_len > params_buf_len) {
                out_ack->bha_params_len = params_buf_len;
                rc = BLE_HS_ECONTROLLER;
            }
            memcpy(params_buf, out_ack->bha_params, out_ack->bha_params_len);
        }
        out_ack->bha_params = params_buf;

        if (out_ack->bha_opcode != expected_opcode) {
            rc = BLE_HS_ECONTROLLER;
        }
    }

    if (rc != 0) {
        STATS_INC(ble_hs_stats, hci_invalid_ack);
    }

    return rc;
}

static int
ble_hci_cmd_wait_for_ack(void)
{
    int rc;

#if PHONY_HCI_ACKS
    if (ble_hci_cmd_phony_ack_cb == NULL) {
        rc = BLE_HS_ETIMEOUT_HCI;
    } else {
        ble_hci_cmd_ack =
            ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_CMD);
        BLE_HS_DBG_ASSERT(ble_hci_cmd_ack != NULL);
        rc = ble_hci_cmd_phony_ack_cb(ble_hci_cmd_ack, 260);
    }
#else
    rc = os_sem_pend(&ble_hci_cmd_sem, BLE_HCI_CMD_TIMEOUT);
    switch (rc) {
    case 0:
        BLE_HS_DBG_ASSERT(ble_hci_cmd_ack != NULL);
        break;
    case OS_TIMEOUT:
        rc = BLE_HS_ETIMEOUT_HCI;
        STATS_INC(ble_hs_stats, hci_timeout);
        break;
    default:
        rc = BLE_HS_EOS;
        break;
    }
#endif

    return rc;
}

int
ble_hci_cmd_tx(void *cmd, void *evt_buf, uint8_t evt_buf_len,
               uint8_t *out_evt_buf_len)
{
    struct ble_hci_ack ack;
    uint16_t opcode;
    int rc;

    opcode = le16toh((uint8_t *)cmd);

    BLE_HS_DBG_ASSERT(ble_hci_cmd_ack == NULL);
    ble_hci_cmd_lock();

    rc = host_hci_cmd_send_buf(cmd);
    if (rc != 0) {
        goto done;
    }

    rc = ble_hci_cmd_wait_for_ack();
    if (rc != 0) {
        ble_hs_sched_reset(rc);
        goto done;
    }

    rc = ble_hci_cmd_process_ack(opcode, evt_buf, evt_buf_len, &ack);
    if (rc != 0) {
        ble_hs_sched_reset(rc);
        goto done;
    }

    if (out_evt_buf_len != NULL) {
        *out_evt_buf_len = ack.bha_params_len;
    }

    rc = ack.bha_status;

done:
    if (ble_hci_cmd_ack != NULL) {
        ble_hci_trans_buf_free(ble_hci_cmd_ack);
        ble_hci_cmd_ack = NULL;
    }

    ble_hci_cmd_unlock();
    return rc;
}

int
ble_hci_cmd_tx_empty_ack(void *cmd)
{
    int rc;

    rc = ble_hci_cmd_tx(cmd, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_hci_cmd_rx_ack(uint8_t *ack_ev)
{
    if (ble_hci_cmd_sem.sem_tokens != 0) {
        /* This ack is unexpected; ignore it. */
        ble_hci_trans_buf_free(ack_ev);
        return;
    }
    BLE_HS_DBG_ASSERT(ble_hci_cmd_ack == NULL);

    /* Unblock the application now that the HCI command buffer is populated
     * with the acknowledgement.
     */
    ble_hci_cmd_ack = ack_ev;
    os_sem_release(&ble_hci_cmd_sem);
}

void
ble_hci_cmd_init(void)
{
    int rc;

    rc = os_sem_init(&ble_hci_cmd_sem, 0);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

    rc = os_mutex_init(&ble_hci_cmd_mutex);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);
}
