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

/**
 * Provides a blocking HCI send interface.  These functions must not be called
 * from the ble_hs task.
 */

#include <stdlib.h>
#include <string.h>
#include "os/os.h"
#include "ble_hs_priv.h"

#define BLE_HCI_BLOCK_TIMEOUT       (OS_TICKS_PER_SEC)

/** Protects resources from 2+ application tasks. */
static struct os_mutex ble_hci_block_mutex;

/** Used to block on expected HCI acknowledgements. */
static struct os_sem ble_hci_block_sem;

/** Global state corresponding to the current blocking operation. */
static struct ble_hci_block_params *ble_hci_block_params;
static struct ble_hci_block_result *ble_hci_block_result;
static uint8_t ble_hci_block_handle;
static int ble_hci_block_status;

/**
 * Copies the parameters from an acknowledgement into the application event
 * buffer.
 */
static void
ble_hci_block_copy_evt_data(void *src_data, uint8_t src_data_len)
{
    if (ble_hci_block_params->evt_buf_len > src_data_len) {
        ble_hci_block_result->evt_buf_len = ble_hci_block_params->evt_buf_len;
    } else {
        ble_hci_block_result->evt_buf_len = src_data_len;
    }
    ble_hci_block_result->evt_total_len = src_data_len;

    if (ble_hci_block_result->evt_buf_len > 0) {
        memcpy(ble_hci_block_params->evt_buf, src_data,
               ble_hci_block_result->evt_buf_len);
    }
}

/**
 * Callback that gets executed upon receiving an HCI acknowledgement.
 */
static void
ble_hci_block_ack_cb(struct ble_hci_ack *ack, void *arg)
{
    BLE_HS_DBG_ASSERT(ack->bha_hci_handle == ble_hci_block_handle);

    /* +1/-1 to ignore the status byte. */
    ble_hci_block_copy_evt_data(ack->bha_params + 1, ack->bha_params_len - 1);
    ble_hci_block_status = ack->bha_status;

    /* Wake the application task up now that the acknowledgement has been
     * received.
     */
    os_sem_release(&ble_hci_block_sem);
}

/**
 * Callback that gets executed when an HCI tx reservation is services.
 * Transmits the HCI command specifed by the client task.
 */
static int
ble_hci_block_tx_cb(void *arg)
{
    uint16_t ocf;
    uint8_t ogf;
    int rc;

    ble_hci_sched_set_ack_cb(ble_hci_block_ack_cb, NULL);

    ogf = BLE_HCI_OGF(ble_hci_block_params->cmd_opcode);
    ocf = BLE_HCI_OCF(ble_hci_block_params->cmd_opcode);
    rc = host_hci_cmd_send(ogf, ocf, ble_hci_block_params->cmd_len,
                           ble_hci_block_params->cmd_data);
    if (rc != 0) {
        os_sem_release(&ble_hci_block_sem);
        return rc;
    }

    return 0;
}

/**
 * Performs a blocking HCI send.  Must not be called from the ble_hs task.
 */
int
ble_hci_block_tx(struct ble_hci_block_params *params,
                 struct ble_hci_block_result *result)
{
    int rc;

    BLE_HS_DBG_ASSERT(os_sched_get_current_task() != &ble_hs_task);

    os_mutex_pend(&ble_hci_block_mutex, OS_WAIT_FOREVER);

    memset(result, 0, sizeof *result);

    ble_hci_block_params = params;
    ble_hci_block_result = result;

    rc = ble_hci_sched_enqueue(ble_hci_block_tx_cb, NULL,
                               &ble_hci_block_handle);
    if (rc == 0) {
        rc = os_sem_pend(&ble_hci_block_sem, BLE_HCI_BLOCK_TIMEOUT);
        switch (rc) {
        case 0:
            rc = ble_hci_block_status;
            break;

        case OS_TIMEOUT:
            rc = BLE_HS_ETIMEOUT;
            break;

        default:
            BLE_HS_DBG_ASSERT(0);
            rc = BLE_HS_EOS;
            break;
        }
    }

    os_mutex_release(&ble_hci_block_mutex);
    return rc;
}

void
ble_hci_block_init(void)
{
    int rc;

    rc = os_mutex_init(&ble_hci_block_mutex);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

    rc = os_sem_init(&ble_hci_block_sem, 0);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);
}
