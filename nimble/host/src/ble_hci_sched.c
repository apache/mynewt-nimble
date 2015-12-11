#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "os/queue.h"
#include "os/os_mempool.h"
#include "ble_hs_priv.h"
#include "ble_hci_sched.h"

#define BLE_HCI_SCHED_NUM_ENTRIES       8

struct ble_hci_sched_entry {
    STAILQ_ENTRY(ble_hci_sched_entry) next;

    ble_hci_sched_tx_fn *tx_cb;
    void *tx_cb_arg;
};

static STAILQ_HEAD(, ble_hci_sched_entry) ble_hci_sched_list;

static void *ble_hci_sched_entry_mem;
static struct os_mempool ble_hci_sched_entry_pool;
static struct ble_hci_sched_entry *ble_hci_sched_cur_entry;

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

static void
ble_hci_sched_entry_free(struct ble_hci_sched_entry *entry)
{
    int rc;

    rc = os_memblock_put(&ble_hci_sched_entry_pool, entry);
    assert(rc == 0);
}

int
ble_hci_sched_enqueue(ble_hci_sched_tx_fn *tx_cb, void *tx_cb_arg)
{
    struct ble_hci_sched_entry *entry;

    entry = ble_hci_sched_entry_alloc();
    if (entry == NULL) {
        return BLE_HS_ENOMEM;
    }

    entry->tx_cb = tx_cb;
    entry->tx_cb_arg = tx_cb_arg;
    STAILQ_INSERT_TAIL(&ble_hci_sched_list, entry, next);

    if (ble_hci_sched_cur_entry == NULL) {
        ble_hs_kick_hci();
    }

    return 0;
}

static int
ble_hci_sched_process_next(void)
{
    struct ble_hci_sched_entry *entry;
    int rc;

    assert(ble_hci_sched_cur_entry == NULL);

    entry = STAILQ_FIRST(&ble_hci_sched_list);
    if (entry == NULL) {
        return 0;
    }

    STAILQ_REMOVE_HEAD(&ble_hci_sched_list, next);

    rc = entry->tx_cb(entry->tx_cb_arg);
    if (rc == 0) {
        ble_hci_sched_cur_entry = entry;
        return 0;
    } else {
        return BLE_HS_EAGAIN;
    }
}

void
ble_hci_sched_wakeup(void)
{
    int rc;

    do {
        rc = ble_hci_sched_process_next();
    } while (rc == BLE_HS_EAGAIN);
}

void
ble_hci_sched_command_complete(void)
{
    if (ble_hci_sched_cur_entry == NULL) {
        /* XXX: Increment stat. */
        return;
    }

    ble_hci_sched_entry_free(ble_hci_sched_cur_entry);
    ble_hci_sched_cur_entry = NULL;

    if (!STAILQ_EMPTY(&ble_hci_sched_list)) {
        ble_hs_kick_hci();
    }
}

static void
ble_hci_sched_free_mem(void)
{
    free(ble_hci_sched_entry_mem);
    ble_hci_sched_entry_mem = NULL;
}

int
ble_hci_sched_init(void)
{
    int rc;

    ble_hci_sched_free_mem();

    ble_hci_sched_entry_mem = malloc(
        OS_MEMPOOL_BYTES(BLE_HCI_SCHED_NUM_ENTRIES,
                         sizeof (struct ble_hci_sched_entry)));
    if (ble_hci_sched_entry_mem == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = os_mempool_init(&ble_hci_sched_entry_pool,
                         BLE_HCI_SCHED_NUM_ENTRIES,
                         sizeof (struct ble_hci_sched_entry),
                         ble_hci_sched_entry_mem,
                         "ble_hci_sched_entry_pool");
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    STAILQ_INIT(&ble_hci_sched_list);

    ble_hci_sched_cur_entry = NULL;

    return 0;

err:
    ble_hci_sched_free_mem();
    return rc;
}
