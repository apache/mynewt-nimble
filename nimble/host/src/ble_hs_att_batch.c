#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include "os/os_mempool.h"
#include "ble_hs_conn.h"
#include "ble_hs_att_cmd.h"
#include "ble_hs_att.h"

#define BLE_HS_ATT_BATCH_OP_NONE            0
#define BLE_HS_ATT_BATCH_OP_FIND_INFO       1

struct ble_hs_att_batch_entry {
    STAILQ_ENTRY(ble_hs_att_batch_entry) next;

    uint8_t op;
    uint16_t peer_handle;
    union {
        struct {
            uint16_t start_handle;
            uint16_t end_handle;
        } find_info;
    };
};

#define BLE_HS_ATT_BATCH_NUM_ENTRIES          4
static void *ble_hs_att_batch_entry_mem;
static struct os_mempool ble_hs_att_batch_entry_pool;

static STAILQ_HEAD(, ble_hs_att_batch_entry) ble_hs_att_batch_list;

static struct ble_hs_att_batch_entry *
ble_hs_att_batch_find(uint16_t peer_handle)
{
    struct ble_hs_att_batch_entry *entry;

    STAILQ_FOREACH(entry, &ble_hs_att_batch_list, next) {
        if (entry->peer_handle == peer_handle) {
            return entry;
        }
    }

    return NULL;
}

void
ble_hs_att_batch_rx_error(struct ble_hs_conn *conn,
                          struct ble_hs_att_error_rsp *rsp)
{
    struct ble_hs_att_batch_entry *entry;

    entry = ble_hs_att_batch_find(conn->bhc_handle);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    switch (entry->op) {
    case BLE_HS_ATT_BATCH_OP_NONE:
        break;

    case BLE_HS_ATT_BATCH_OP_FIND_INFO:
        /* XXX: Branch on error status. */
        break;

    default:
        assert(0);
        break;
    }
}

void
ble_hs_att_batch_rx_find_info(struct ble_hs_conn *conn, int status,
                              uint16_t last_handle_id)
{
    struct ble_hs_att_batch_entry *entry;

    entry = ble_hs_att_batch_find(conn->bhc_handle);
    if (entry == NULL) {
        /* Not expecting a response from this device. */
        return;
    }

    if (entry->op != BLE_HS_ATT_BATCH_OP_FIND_INFO) {
        /* Not expecting a find info response from this device. */
        return;
    }

    if (status != 0) {
        /* XXX: Call failure callback. */
        return;
    }

    if (last_handle_id == 0xffff) {
        /* Call success callback. */
    }

    /* XXX: Send follow up request. */
}

int
ble_hs_att_batch_init(void)
{
    int rc;

    free(ble_hs_att_batch_entry_mem);

    ble_hs_att_batch_entry_mem = malloc(
        OS_MEMPOOL_BYTES(BLE_HS_ATT_BATCH_NUM_ENTRIES,
                         sizeof (struct ble_hs_att_batch_entry)));
    if (ble_hs_att_batch_entry_mem == NULL) {
        rc = ENOMEM;
        goto err;
    }

    rc = os_mempool_init(&ble_hs_att_batch_entry_pool,
                         BLE_HS_ATT_BATCH_NUM_ENTRIES,
                         sizeof (struct ble_hs_att_batch_entry),
                         ble_hs_att_batch_entry_mem,
                         "ble_hs_att_batch_entry_pool");
    if (rc != 0) {
        goto err;
    }

    STAILQ_INIT(&ble_hs_att_batch_list);

    return 0;

err:
    free(ble_hs_att_batch_entry_mem);
    ble_hs_att_batch_entry_mem = NULL;

    return rc;
}
