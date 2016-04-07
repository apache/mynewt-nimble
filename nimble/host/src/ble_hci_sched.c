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

#include <stdlib.h>
#include <string.h>
#include "os/queue.h"
#include "os/os_mempool.h"
#include "ble_hs_priv.h"

struct ble_hci_sched_entry {
    STAILQ_ENTRY(ble_hci_sched_entry) next;

    ble_hci_sched_tx_fn *tx_cb;
    void *tx_cb_arg;

    uint8_t handle;
};

static STAILQ_HEAD(, ble_hci_sched_entry) ble_hci_sched_list;

static void *ble_hci_sched_entry_mem;
static struct os_mempool ble_hci_sched_entry_pool;
static struct ble_hci_sched_entry *ble_hci_sched_cur_entry;
static uint8_t ble_hci_sched_prev_handle;

static ble_hci_sched_ack_fn *ble_hci_sched_ack_cb;
static void *ble_hci_sched_ack_arg;

static struct os_mutex ble_hci_sched_mutex;

static void
ble_hci_sched_lock(void)
{
    struct os_task *owner;
    int rc;

    owner = ble_hci_sched_mutex.mu_owner;
    BLE_HS_DBG_ASSERT_EVAL(owner == NULL ||
                           owner != os_sched_get_current_task());

    rc = os_mutex_pend(&ble_hci_sched_mutex, 0xffffffff);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0 || rc == OS_NOT_STARTED);
}

static void
ble_hci_sched_unlock(void)
{
    int rc;

    rc = os_mutex_release(&ble_hci_sched_mutex);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0 || rc == OS_NOT_STARTED);
}

int
ble_hci_sched_locked_by_cur_task(void)
{
    struct os_task *owner;

    owner = ble_hci_sched_mutex.mu_owner;
    return owner != NULL && owner == os_sched_get_current_task();
}

/**
 * Lock restrictions: none.
 */
static struct ble_hci_sched_entry *
ble_hci_sched_entry_alloc(void)
{
    struct ble_hci_sched_entry *entry;

    entry = os_memblock_get(&ble_hci_sched_entry_pool);
    if (entry != NULL) {
        memset(entry, 0, sizeof *entry);
    }

    return entry;
}

/**
 * Lock restrictions: none.
 */
static void
ble_hci_sched_entry_free(struct ble_hci_sched_entry *entry)
{
    int rc;

    rc = os_memblock_put(&ble_hci_sched_entry_pool, entry);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);
}

/**
 * Removes the specified entry from the HCI reservation list.
 *
 * Lock restrictions:
 *     o Caller locks hci_sched.
 */
static void
ble_hci_sched_entry_remove(struct ble_hci_sched_entry *entry,
                           struct ble_hci_sched_entry *prev)
{
    if (os_started()) {
        BLE_HS_DBG_ASSERT(ble_hci_sched_locked_by_cur_task());
    }

    if (prev == NULL) {
        BLE_HS_DBG_ASSERT(STAILQ_FIRST(&ble_hci_sched_list) == entry);
        STAILQ_REMOVE_HEAD(&ble_hci_sched_list, next);
    } else {
        BLE_HS_DBG_ASSERT(STAILQ_NEXT(prev, next) == prev);
        STAILQ_NEXT(prev, next) = STAILQ_NEXT(prev, next);
    }
}

/**
 * Generates an HCI handle for a newly-allocated entry.
 *
 * Lock restrictions:
 *     o Caller unlocks hci_sched.
 *
 * @return                      A new HCI handle.
 */
static uint8_t
ble_hci_sched_new_handle(void)
{
    uint8_t handle;

    ble_hci_sched_lock();

    ble_hci_sched_prev_handle++;
    if (ble_hci_sched_prev_handle == BLE_HCI_SCHED_HANDLE_NONE) {
        ble_hci_sched_prev_handle++;
    }

    handle = ble_hci_sched_prev_handle;

    ble_hci_sched_unlock();

    return handle;
}

/**
 * Schedules an HCI transmit slot.  When it is the slot's turn to transmit,
 * the specified callback is executed.  The expectation is for the callback to
 * transmit an HCI command via the functions in host_hci_cmd.c.
 *
 * Lock restrictions:
 *     o Caller unlocks hci_sched.
 *
 * @param tx_cb                 Callback that gets executed when a transmit
 *                                  slot is available; should send a single
 *                                  HCI command.
 * @param tx_cb_arg             Argument that gets passed to the tx callback.
 * @param out_hci_handle        On success, the HCI slot handle gets written
 *                                  here.  This handle can be used to cancel
 *                                  the HCI slot reservation.  You can pass
 *                                  null here if you won't need to cancel.
 *
 * @return                      0 on success; BLE_HS_E[...] on failure.
 */
int
ble_hci_sched_enqueue(ble_hci_sched_tx_fn *tx_cb, void *tx_cb_arg,
                      uint8_t *out_hci_handle)
{
    struct ble_hci_sched_entry *entry;

    entry = ble_hci_sched_entry_alloc();
    if (entry == NULL) {
        return BLE_HS_ENOMEM;
    }

    entry->handle = ble_hci_sched_new_handle();
    entry->tx_cb = tx_cb;
    entry->tx_cb_arg = tx_cb_arg;

    if (out_hci_handle != NULL) {
        *out_hci_handle = entry->handle;
    }

    ble_hci_sched_lock();
    STAILQ_INSERT_TAIL(&ble_hci_sched_list, entry, next);
    ble_hci_sched_unlock();

    if (ble_hci_sched_cur_entry == NULL) {
        ble_hs_kick_hci();
    }

    return 0;
}

/**
 * Cancels the HCI slot reservation with the specified handle.  If the slot
 * being cancelled is already in progress, the HCI ack callback is reset, and
 * the next HCI slot is initiated (if there is one).
 *
 * Lock restrictions:
 *     o Caller unlocks hci_sched.
 *
 * @param handle                The handle of the slot to cancel.
 *
 * @return                      0 on success; BLE_HS_E[...] on failure.
 */
int
ble_hci_sched_cancel(uint8_t handle)
{
    struct ble_hci_sched_entry *entry;
    struct ble_hci_sched_entry *prev;
    int do_kick;
    int rc;

    ble_hci_sched_lock();

    if (ble_hci_sched_cur_entry != NULL &&
        ble_hci_sched_cur_entry->handle == handle) {

        /* User is cancelling an in-progress operation. */
        entry = ble_hci_sched_cur_entry;
        ble_hci_sched_cur_entry = NULL;
        ble_hci_sched_set_ack_cb(NULL, NULL);
        do_kick = !STAILQ_EMPTY(&ble_hci_sched_list);
    } else {
        do_kick = 0;

        prev = NULL;
        STAILQ_FOREACH(entry, &ble_hci_sched_list, next) {
            if (entry->handle == handle) {
                ble_hci_sched_entry_remove(entry, prev);
                break;
            }

            prev = entry;
        }
    }

    ble_hci_sched_unlock();

    if (entry == NULL) {
        rc = BLE_HS_ENOENT;
    } else {
        ble_hci_sched_entry_free(entry);
        rc = 0;
    }

    if (do_kick) {
        ble_hs_kick_hci();
    }

    return rc;
}

/**
 * Executes the specified scheduled HCI transmit slot.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      0 on success; nonzero on failure.
 */
static int
ble_hci_sched_tx(struct ble_hci_sched_entry *entry)
{
    int rc;

    ble_hs_misc_assert_no_locks();

    rc = entry->tx_cb(entry->tx_cb_arg);
    if (rc == 0) {
        ble_hci_sched_cur_entry = entry;
    }

    return rc;
}

/**
 * Executes the next scheduled HCI transmit slot if there is one.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 *
 * @return                      0 if no more slots should be executed (either a
 *                                  transmit succeeded or there are no more
 *                                  reserved slots);
 *                              BLE_HS_EAGAIN if the next slot should be
 *                                  executed.
 */
static int
ble_hci_sched_process_next(void)
{
    struct ble_hci_sched_entry *entry;
    int rc;

    BLE_HS_DBG_ASSERT(ble_hci_sched_cur_entry == NULL);

    ble_hci_sched_lock();

    entry = STAILQ_FIRST(&ble_hci_sched_list);
    if (entry != NULL) {
        STAILQ_REMOVE_HEAD(&ble_hci_sched_list, next);
    }

    ble_hci_sched_unlock();

    if (entry == NULL) {
        rc = 0;
    } else {
        rc = ble_hci_sched_tx(entry);
        if (rc != 0) {
            ble_hci_sched_entry_free(entry);
            rc = BLE_HS_EAGAIN;
        }
    }

    return rc;
}

/**
 * Executes the next scheduled HCI transmit slot if there is one.  If a
 * transmit fails (i.e., the user callback returns nonzero), the corresponding
 * slot is freed and the next slot is executed.  This process repeats until
 * either:
 *     o A transmit succeeds, or
 *     o There are no more scheduled transmit slots.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_hci_sched_wakeup(void)
{
    int rc;

    do {
        rc = ble_hci_sched_process_next();
    } while (rc == BLE_HS_EAGAIN);
}

/**
 * Called when the controller has acknowledged the current HCI command.
 * Acknowledgment occurs via either a command-complete or command-status HCI
 * event.
 *
 * Lock restrictions:
 *     o Caller unlocks hci_sched.
 */
static void
ble_hci_sched_transaction_complete(void)
{
    struct ble_hci_sched_entry *entry;

    ble_hci_sched_lock();

    entry = ble_hci_sched_cur_entry;
    ble_hci_sched_cur_entry = NULL;

    ble_hci_sched_unlock();

    if (entry != NULL) {
        ble_hci_sched_entry_free(entry);
    }

    if (!STAILQ_EMPTY(&ble_hci_sched_list)) {
        ble_hs_kick_hci();
    }
}

void
ble_hci_sched_rx_ack(struct ble_hci_ack *ack)
{
    ble_hci_sched_ack_fn *cb;

    if (ble_hci_sched_ack_cb != NULL) {
        cb = ble_hci_sched_ack_cb;
        ble_hci_sched_ack_cb = NULL;

        ack->bha_hci_handle = ble_hci_sched_prev_handle;
        cb(ack, ble_hci_sched_ack_arg);
    }

    ble_hci_sched_transaction_complete();
}

void
ble_hci_sched_set_ack_cb(ble_hci_sched_ack_fn *cb, void *arg)
{
    /* Don't allow the current callback to be replaced with another. */
    BLE_HS_DBG_ASSERT(ble_hci_sched_ack_cb == NULL || cb == NULL);

    ble_hci_sched_ack_cb = cb;
    ble_hci_sched_ack_arg = arg;
}

/**
 * This is really only useful for unit testing.
 *
 * Lock restrictions: none.
 */
ble_hci_sched_ack_fn *
ble_hci_sched_get_ack_cb(void)
{
    return ble_hci_sched_ack_cb;
}

/**
 * Lock restrictions: none.
 */
static void
ble_hci_sched_free_mem(void)
{
    free(ble_hci_sched_entry_mem);
    ble_hci_sched_entry_mem = NULL;
}

/**
 * Lock restrictions: none.
 */
int
ble_hci_sched_init(void)
{
    int rc;

    ble_hci_sched_free_mem();

    rc = os_mutex_init(&ble_hci_sched_mutex);
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    if (ble_hs_cfg.max_hci_tx_slots > 0) {
        ble_hci_sched_entry_mem = malloc(
            OS_MEMPOOL_BYTES(ble_hs_cfg.max_hci_tx_slots,
                             sizeof (struct ble_hci_sched_entry)));
        if (ble_hci_sched_entry_mem == NULL) {
            rc = BLE_HS_ENOMEM;
            goto err;
        }

        rc = os_mempool_init(&ble_hci_sched_entry_pool,
                             ble_hs_cfg.max_hci_tx_slots,
                             sizeof (struct ble_hci_sched_entry),
                             ble_hci_sched_entry_mem,
                             "ble_hci_sched_entry_pool");
        if (rc != 0) {
            rc = BLE_HS_EOS;
            goto err;
        }
    }

    STAILQ_INIT(&ble_hci_sched_list);

    ble_hci_sched_cur_entry = NULL;
    ble_hci_sched_set_ack_cb(NULL, NULL);

    return 0;

err:
    ble_hci_sched_free_mem();
    return rc;
}
