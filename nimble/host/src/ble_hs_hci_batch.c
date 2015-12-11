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
#include <errno.h>
#include "os/os.h"
#include "host/host_hci.h"
#include "ble_hs_priv.h"
#include "ble_hci_ack.h"
#include "ble_gap_conn.h"
#include "ble_hs_hci_batch.h"

#define BLE_HS_HCI_BATCH_NUM_ENTRIES     16

static void *ble_hs_hci_batch_entry_mem;
static struct os_mempool ble_hs_hci_batch_entry_pool;
static STAILQ_HEAD(, ble_hs_hci_batch_entry) ble_hs_hci_batch_queue;

static struct ble_hs_hci_batch_entry *ble_hs_hci_batch_cur_entry;

struct ble_hs_hci_batch_entry *
ble_hs_hci_batch_entry_alloc(void)
{
    struct ble_hs_hci_batch_entry *entry;

    entry = os_memblock_get(&ble_hs_hci_batch_entry_pool);
    return entry;
}

void
ble_hs_hci_batch_enqueue(struct ble_hs_hci_batch_entry *entry)
{
    STAILQ_INSERT_TAIL(&ble_hs_hci_batch_queue, entry, bhb_next);
    if (ble_hs_hci_batch_cur_entry == NULL) {
        ble_hs_kick_hci();
    }
}

void
ble_hs_hci_batch_done(void)
{
    assert(ble_hs_hci_batch_cur_entry != NULL);
    os_memblock_put(&ble_hs_hci_batch_entry_pool, ble_hs_hci_batch_cur_entry);
    ble_hs_hci_batch_cur_entry = NULL;

    if (!STAILQ_EMPTY(&ble_hs_hci_batch_queue)) {
        ble_hs_kick_hci();
    }
}

static void
ble_hs_hci_read_buf_size_ack(struct ble_hci_ack *ack, void *arg)
{
    uint16_t pktlen;
    uint8_t max_pkts;
    int rc;

    if (ack->bha_status != 0) {
        /* XXX: Log / stat this. */
        return;
    }

    if (ack->bha_params_len != BLE_HCI_RD_BUF_SIZE_RSPLEN + 1) {
        /* XXX: Log / stat this. */
        return;
    }

    pktlen = le16toh(ack->bha_params + 1);
    max_pkts = ack->bha_params[3];

    rc = host_hci_set_buf_size(pktlen, max_pkts);
    if (rc != 0) {
        /* XXX: Send BR command instead. */
        return;
    }

    ble_hs_hci_batch_done();
}

void
ble_hs_hci_batch_process_next(void)
{
    struct ble_hs_hci_batch_entry *entry;
    int rc;

    entry = STAILQ_FIRST(&ble_hs_hci_batch_queue);
    if (entry == NULL) {
        return;
    }

    STAILQ_REMOVE_HEAD(&ble_hs_hci_batch_queue, bhb_next);
    ble_hs_hci_batch_cur_entry = entry;

    switch (entry->bhb_type) {
    case BLE_HS_HCI_BATCH_TYPE_DIRECT_CONNECT:
        rc = ble_gap_conn_direct_connect(
            entry->bhb_direct_connect.bwdc_peer_addr_type,
            entry->bhb_direct_connect.bwdc_peer_addr);
        break;

    case BLE_HS_HCI_BATCH_TYPE_DIRECT_ADVERTISE:
        rc = ble_gap_conn_direct_advertise(
            entry->bhb_direct_advertise.bwda_peer_addr_type,
            entry->bhb_direct_advertise.bwda_peer_addr);
        break;

    case BLE_HS_HCI_BATCH_TYPE_READ_HCI_BUF_SIZE:
        ble_hci_ack_set_callback(ble_hs_hci_read_buf_size_ack, NULL);
        rc = host_hci_cmd_le_read_buffer_size();
        if (rc != 0) {
            /* XXX: Call callback. */
        }
        break;

    case BLE_HS_HCI_BATCH_TYPE_GENERAL_DISCOVERY:
        rc = ble_gap_conn_general_discovery();
        break;

    default:
        assert(0);
        break;
    }

    if (rc != 0) {
        ble_hs_hci_batch_done();
    }
}

int
ble_hs_hci_batch_init(void)
{
    int rc;

    free(ble_hs_hci_batch_entry_mem);
    ble_hs_hci_batch_entry_mem = malloc(
        OS_MEMPOOL_BYTES(BLE_HS_HCI_BATCH_NUM_ENTRIES,
                         sizeof (struct ble_hs_hci_batch_entry)));
    if (ble_hs_hci_batch_entry_mem == NULL) {
        return BLE_HS_ENOMEM;
    }

    rc = os_mempool_init(&ble_hs_hci_batch_entry_pool,
                         BLE_HS_HCI_BATCH_NUM_ENTRIES,
                         sizeof (struct ble_hs_hci_batch_entry),
                         ble_hs_hci_batch_entry_mem,
                         "ble_hs_hci_batch_entry_pool");
    if (rc != 0) {
        return BLE_HS_EINVAL; // XXX
    }

    STAILQ_INIT(&ble_hs_hci_batch_queue);

    ble_hs_hci_batch_cur_entry = NULL;

    return 0;
}
