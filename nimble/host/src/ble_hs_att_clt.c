#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "os/os_mempool.h"
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "ble_hs_uuid.h"
#include "ble_hs_conn.h"
#include "ble_hs_att_cmd.h"
#include "ble_hs_att.h"

#define BLE_HS_ATT_CLT_NUM_ENTRIES  128
static void *ble_hs_att_clt_entry_mem;
static struct os_mempool ble_hs_att_clt_entry_pool;

static struct ble_hs_att_clt_entry *
ble_hs_att_clt_entry_alloc(void)
{
    struct ble_hs_att_clt_entry *entry;

    entry = os_memblock_get(&ble_hs_att_clt_entry_pool);
    if (entry != NULL) {
        memset(entry, 0, sizeof *entry);
    }

    return entry;
}

static void
ble_hs_att_clt_entry_free(struct ble_hs_att_clt_entry *entry)
{
    int rc;

    rc = os_memblock_put(&ble_hs_att_clt_entry_pool, entry);
    assert(rc == 0);
}

void
ble_hs_att_clt_entry_list_free(struct ble_hs_att_clt_entry_list *list)
{
    struct ble_hs_att_clt_entry *entry;

    while ((entry = SLIST_FIRST(list)) != NULL) {
        SLIST_REMOVE_HEAD(list, bhac_next);
        ble_hs_att_clt_entry_free(entry);
    }
}

int
ble_hs_att_clt_entry_insert(struct ble_hs_conn *conn, uint16_t handle_id,
                            uint8_t *uuid)
{
    struct ble_hs_att_clt_entry *entry;
    struct ble_hs_att_clt_entry *prev;
    struct ble_hs_att_clt_entry *cur;

    /* XXX: Probably need to lock a semaphore here. */

    entry = ble_hs_att_clt_entry_alloc();
    if (entry == NULL) {
        return ENOMEM;
    }

    entry->bhac_handle_id = handle_id;
    memcpy(entry->bhac_uuid, uuid, sizeof entry->bhac_uuid);

    prev = NULL;
    SLIST_FOREACH(cur, &conn->bhc_att_clt_list, bhac_next) {
        if (cur->bhac_handle_id == handle_id) {
            return EEXIST;
        }
        if (cur->bhac_handle_id > handle_id) {
            break;
        }

        prev = cur;
    }

    if (prev == NULL) {
        SLIST_INSERT_HEAD(&conn->bhc_att_clt_list, entry, bhac_next);
    } else {
        SLIST_INSERT_AFTER(prev, entry, bhac_next);
    }

    return 0;
}

uint16_t
ble_hs_att_clt_find_entry_uuid128(struct ble_hs_conn *conn, void *uuid128)
{
    struct ble_hs_att_clt_entry *entry;
    int rc;

    SLIST_FOREACH(entry, &conn->bhc_att_clt_list, bhac_next) {
        rc = memcmp(entry->bhac_uuid, uuid128, 16);
        if (rc == 0) {
            return entry->bhac_handle_id;
        }
    }

    return 0;
}

uint16_t
ble_hs_att_clt_find_entry_uuid16(struct ble_hs_conn *conn, uint16_t uuid16)
{
    uint8_t uuid128[16];
    uint16_t handle_id;
    int rc;

    rc = ble_hs_uuid_from_16bit(uuid16, uuid128);
    if (rc != 0) {
        return 0;
    }

    handle_id = ble_hs_att_clt_find_entry_uuid128(conn, uuid128);
    return handle_id;
}

int
ble_hs_att_clt_tx_find_info(struct ble_hs_conn *conn,
                            struct ble_l2cap_chan *chan,
                            struct ble_hs_att_find_info_req *req)
{
    struct os_mbuf *txom;
    void *buf;
    int rc;

    txom = NULL;

    if (req->bhafq_start_handle == 0 ||
        req->bhafq_start_handle > req->bhafq_end_handle) {

        rc = EINVAL;
        goto err;
    }

    txom = os_mbuf_get_pkthdr(&ble_hs_mbuf_pool, 0);
    if (txom == NULL) {
        rc = ENOMEM;
        goto err;
    }

    buf = os_mbuf_extend(txom, BLE_HS_ATT_FIND_INFO_REQ_SZ);
    if (buf == NULL) {
        rc = ENOMEM;
        goto err;
    }

    rc = ble_hs_att_find_info_req_write(buf, BLE_HS_ATT_FIND_INFO_REQ_SZ, req);
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_tx(chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    rc = 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_hs_att_clt_rx_find_info(struct ble_hs_conn *conn,
                            struct ble_l2cap_chan *chan,
                            struct os_mbuf *om)
{
    struct ble_hs_att_find_info_rsp rsp;
    uint16_t handle_id;
    uint16_t uuid16;
    uint8_t uuid128[16];
    int off;
    int rc;

    /* XXX: Pull up om */

    rc = ble_hs_att_find_info_rsp_parse(om->om_data, om->om_len, &rsp);
    if (rc != 0) {
        return rc;
    }

    off = BLE_HS_ATT_FIND_INFO_RSP_MIN_SZ;
    while (off < OS_MBUF_PKTHDR(om)->omp_len) {
        rc = os_mbuf_copydata(om, off, 2, &handle_id);
        if (rc != 0) {
            return EINVAL;
        }
        off += 2;
        handle_id = le16toh(&handle_id);

        switch (rsp.bhafp_format) {
        case BLE_HS_ATT_FIND_INFO_RSP_FORMAT_16BIT:
            rc = os_mbuf_copydata(om, off, 2, &uuid16);
            if (rc != 0) {
                return EINVAL;
            }
            off += 2;
            uuid16 = le16toh(&uuid16);

            rc = ble_hs_uuid_from_16bit(uuid16, uuid128);
            if (rc != 0) {
                return EINVAL;
            }
            break;

        case BLE_HS_ATT_FIND_INFO_RSP_FORMAT_128BIT:
            rc = os_mbuf_copydata(om, off, 16, &uuid128);
            if (rc != 0) {
                return EINVAL;
            }
            off += 16;
            break;

        default:
            return EINVAL;
        }

        rc = ble_hs_att_clt_entry_insert(conn, handle_id, uuid128);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

int
ble_hs_att_clt_init(void)
{
    int rc;

    free(ble_hs_att_clt_entry_mem);
    ble_hs_att_clt_entry_mem = malloc(
        OS_MEMPOOL_BYTES(BLE_HS_ATT_CLT_NUM_ENTRIES,
                         sizeof (struct ble_hs_att_clt_entry)));
    if (ble_hs_att_clt_entry_mem == NULL) {
        rc = ENOMEM;
        goto err;
    }

    rc = os_mempool_init(&ble_hs_att_clt_entry_pool,
                         BLE_HS_ATT_CLT_NUM_ENTRIES,
                         sizeof (struct ble_hs_att_clt_entry),
                         ble_hs_att_clt_entry_mem,
                         "ble_hs_att_clt_entry_pool");
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    free(ble_hs_att_clt_entry_mem);
    ble_hs_att_clt_entry_mem = NULL;

    return rc;
}
