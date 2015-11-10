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

#include <string.h>
#include <errno.h>
#include <assert.h>
#include "os/os.h"
#include "nimble/ble.h"
#include "ble_l2cap.h"
#include "ble_l2cap_util.h"
#include "ble_hs_conn.h"
#include "ble_hs_att_cmd.h"
#include "ble_hs_att.h"

typedef int ble_hs_att_rx_fn(struct ble_hs_conn *conn,
                             struct ble_l2cap_chan *chan,
                             struct os_mbuf *om);
struct ble_hs_att_rx_dispatch_entry {
    uint8_t bde_op;
    ble_hs_att_rx_fn *bde_fn;
};

/** Dispatch table for incoming ATT requests.  Sorted by op code. */
static int ble_hs_att_rx_mtu_req(struct ble_hs_conn *conn,
                                 struct ble_l2cap_chan *chan,
                                 struct os_mbuf *om);
static int ble_hs_att_rx_read_req(struct ble_hs_conn *conn,
                                  struct ble_l2cap_chan *chan,
                                  struct os_mbuf *om);
static int ble_hs_att_rx_write_req(struct ble_hs_conn *conn,
                                   struct ble_l2cap_chan *chan,
                                   struct os_mbuf *om);

static struct ble_hs_att_rx_dispatch_entry ble_hs_att_rx_dispatch[] = {
    { BLE_HS_ATT_OP_MTU_REQ,    ble_hs_att_rx_mtu_req },
    { BLE_HS_ATT_OP_READ_REQ,   ble_hs_att_rx_read_req },
    { BLE_HS_ATT_OP_WRITE_REQ,  ble_hs_att_rx_write_req },
};

#define BLE_HS_ATT_RX_DISPATCH_SZ \
    (sizeof ble_hs_att_rx_dispatch / sizeof ble_hs_att_rx_dispatch[0])

static STAILQ_HEAD(, ble_hs_att_entry) g_ble_hs_att_list;
static uint16_t g_ble_hs_att_id;

static struct os_mutex g_ble_hs_att_list_mutex;

#define BLE_HS_ATT_NUM_ENTRIES          1024
static void *ble_hs_att_entry_mem;
static struct os_mempool ble_hs_att_entry_pool;

/**
 * Lock the host attribute list.
 * 
 * @return 0 on success, non-zero error code on failure.
 */
static int 
ble_hs_att_list_lock(void)
{
    int rc;

    rc = os_mutex_pend(&g_ble_hs_att_list_mutex, OS_WAIT_FOREVER);
    if (rc != 0 && rc != OS_NOT_STARTED) {
        goto err;
    }

    return (0);
err:
    return (rc);
}

/**
 * Unlock the host attribute list
 *
 * @return 0 on success, non-zero error code on failure.
 */
static int 
ble_hs_att_list_unlock(void)
{
    int rc;

    rc = os_mutex_release(&g_ble_hs_att_list_mutex);
    if (rc != 0 && rc != OS_NOT_STARTED) {
        goto err;
    }

    return (0);
err:
    return (rc);
}

static struct ble_hs_att_entry *
ble_hs_att_entry_alloc(void)
{
    struct ble_hs_att_entry *entry;

    entry = os_memblock_get(&ble_hs_att_entry_pool);
    if (entry != NULL) {
        memset(entry, 0, sizeof *entry);
    }

    return entry;
}

#if 0
static void
ble_hs_att_entry_free(struct ble_hs_att_entry *entry)
{
    int rc;

    rc = os_memblock_put(&ble_hs_att_entry_pool, entry);
    assert(rc == 0);
}
#endif

/**
 * Allocate the next handle id and return it.
 *
 * @return A new 16-bit handle ID.
 */
static uint16_t
ble_hs_att_next_id(void)
{
    return (++g_ble_hs_att_id);
}

/**
 * Register a host attribute with the BLE stack.
 *
 * @param ha A filled out ble_hs_att structure to register
 * @param handle_id A pointer to a 16-bit handle ID, which will be the 
 *                  handle that is allocated.
 *
 * @return 0 on success, non-zero error code on failure.
 */
int 
ble_hs_att_register(uint8_t *uuid, uint8_t flags, uint16_t *handle_id,
                    ble_hs_att_handle_func *fn)
{
    struct ble_hs_att_entry *entry;
    int rc;

    entry = ble_hs_att_entry_alloc();
    if (entry == NULL) {
        return ENOMEM;
    }

    memcpy(&entry->ha_uuid, uuid, sizeof entry->ha_uuid);
    entry->ha_flags = flags;
    entry->ha_handle_id = ble_hs_att_next_id();
    entry->ha_fn = fn;

    rc = ble_hs_att_list_lock();
    if (rc != 0) {
        goto err;
    }

    STAILQ_INSERT_TAIL(&g_ble_hs_att_list, entry, ha_next);

    rc = ble_hs_att_list_unlock();
    if (rc != 0) {
        goto err;
    }

    if (handle_id != NULL) {
        *handle_id = entry->ha_handle_id;
    }

    return (0);

err:
    return (rc);
}

/**
 * Walk the host attribute list, calling walk_func on each entry with argument.
 * If walk_func wants to stop iteration, it returns 1.  To continue iteration 
 * it returns 0.  
 *
 * @param walk_func The function to call for each element in the host attribute
 *                  list.
 * @param arg       The argument to provide to walk_func
 * @param ha_ptr    A pointer to a pointer which will be set to the last 
 *                  ble_hs_att element processed, or NULL if the entire list has 
 *                  been processed
 *
 * @return 1 on stopped, 0 on fully processed and an error code otherwise.
 */
int
ble_hs_att_walk(ble_hs_att_walk_func_t walk_func, void *arg, 
        struct ble_hs_att_entry **ha_ptr)
{
    struct ble_hs_att_entry *ha;
    int rc;

    rc = ble_hs_att_list_lock();
    if (rc != 0) {
        goto err;
    }

    *ha_ptr = NULL;
    ha = NULL;
    STAILQ_FOREACH(ha, &g_ble_hs_att_list, ha_next) {
        rc = walk_func(ha, arg);
        if (rc == 1) {
            rc = ble_hs_att_list_unlock();
            if (rc != 0) {
                goto err;
            }
            *ha_ptr = ha;
            return (1);
        }
    }

    rc = ble_hs_att_list_unlock();
    if (rc != 0) {
        goto err;
    }

    return (0);
err:
    return (rc);
}

static int 
ble_hs_att_match_handle(struct ble_hs_att_entry *ha, void *arg)
{
    if (ha->ha_handle_id == *(uint16_t *) arg) {
        return (1);
    } else {
        return (0);
    }
}


/**
 * Find a host attribute by handle id. 
 *
 * @param handle_id The handle_id to search for
 * @param ble_hs_att A pointer to a pointer to put the matching host attr into.
 *
 * @return 0 on success, BLE_ERR_ATTR_NOT_FOUND on not found, and non-zero on 
 *         error.
 */
int
ble_hs_att_find_by_handle(uint16_t handle_id, struct ble_hs_att_entry **ha_ptr)
{
    int rc;

    rc = ble_hs_att_walk(ble_hs_att_match_handle, &handle_id, ha_ptr);
    if (rc == 1) {
        /* Found a matching handle */
        return (0);
    } else if (rc == 0) {
        /* Not found */
        return (BLE_ERR_ATTR_NOT_FOUND);
    } else {
        return (rc);
    }
}

static int 
ble_hs_att_match_uuid(struct ble_hs_att_entry *ha, void *arg)
{
    ble_uuid_t *uuid;

    uuid = (ble_uuid_t *) arg;

    if (memcmp(&ha->ha_uuid, uuid, sizeof(*uuid)) == 0) {
        return (1);
    } else {
        return (0);
    }
}

/**
 * Find a host attribute by UUID.
 *
 * @param uuid The ble_uuid_t to search for 
 * @param ha_ptr A pointer to a pointer to put the matching host attr into.
 *
 * @return 0 on success, BLE_ERR_ATTR_NOT_FOUND on not found, and non-zero on 
 *         error.
 */
int
ble_hs_att_find_by_uuid(ble_uuid_t *uuid, struct ble_hs_att_entry **ha_ptr) 
{
    int rc;
    
    rc = ble_hs_att_walk(ble_hs_att_match_uuid, uuid, ha_ptr);
    if (rc == 1) {
        /* Found a matching handle */
        return (0);
    } else if (rc == 0) {
        /* No match */
        return (BLE_ERR_ATTR_NOT_FOUND);
    } else {
        return (rc);
    }
}

static struct ble_hs_att_rx_dispatch_entry *
ble_hs_att_rx_dispatch_entry_find(uint8_t op)
{
    struct ble_hs_att_rx_dispatch_entry *entry;
    int i;

    for (i = 0; i < BLE_HS_ATT_RX_DISPATCH_SZ; i++) {
        entry = ble_hs_att_rx_dispatch + i;
        if (entry->bde_op == op) {
            return entry;
        }

        if (entry->bde_op > op) {
            break;
        }
    }

    return NULL;
}

static int
ble_hs_att_tx_error_rsp(struct ble_l2cap_chan *chan, uint8_t req_op,
                        uint16_t handle, uint8_t error_code)
{
    struct ble_hs_att_error_rsp rsp;
    uint8_t buf[BLE_HS_ATT_ERROR_RSP_SZ];
    int rc;

    rsp.bhaep_op = BLE_HS_ATT_OP_ERROR_RSP;
    rsp.bhaep_req_op = req_op;
    rsp.bhaep_handle = handle;
    rsp.bhaep_error_code = error_code;

    rc = ble_hs_att_error_rsp_write(buf, sizeof buf, &rsp);
    assert(rc == 0);

    rc = ble_l2cap_tx(chan, buf, sizeof buf);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_hs_att_rx_mtu_req(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                      struct os_mbuf *om)
{
    struct ble_hs_att_mtu_req req;
    uint8_t buf[BLE_HS_ATT_MTU_REQ_SZ];
    int rc;

    rc = os_mbuf_copydata(om, 0, sizeof buf, buf);
    if (rc != 0) {
        return rc;
    }

    rc = ble_hs_att_mtu_req_parse(buf, sizeof buf, &req);
    assert(rc == 0);

    /* XXX: Update connection MTU. */
    /* XXX: Send response. */

    return 0;
}

static int
ble_hs_att_tx_read_rsp(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                       void *attr_data, int attr_len)
{
    uint16_t data_len;
    uint8_t op;
    int rc;

    op = BLE_HS_ATT_OP_READ_RSP;
    rc = ble_l2cap_tx(chan, &op, 1);
    if (rc != 0) {
        return rc;
    }

    /* Vol. 3, part F, 3.2.9; don't send more than ATT_MTU-1 bytes of data. */
    if (attr_len > conn->bhc_att_mtu - 1) {
        data_len = conn->bhc_att_mtu - 1;
    } else {
        data_len = attr_len;
    }

    rc = ble_l2cap_tx(chan, attr_data, data_len);
    if (rc != 0) {
        return rc;
    }

    /* XXX: Kick L2CAP. */

    return 0;
}

static int
ble_hs_att_rx_read_req(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                       struct os_mbuf *om)
{
    union ble_hs_att_handle_arg arg;
    struct ble_hs_att_read_req req;
    struct ble_hs_att_entry *entry;
    uint8_t buf[BLE_HS_ATT_READ_REQ_SZ];
    int rc;

    rc = os_mbuf_copydata(om, 0, sizeof buf, buf);
    if (rc != 0) {
        return rc;
    }

    rc = ble_hs_att_read_req_parse(buf, sizeof buf, &req);
    assert(rc == 0);

    rc = ble_hs_att_find_by_handle(req.bharq_handle, &entry);
    if (rc != 0) {
        goto send_err;
    }

    if (entry->ha_fn == NULL) {
        rc = BLE_ERR_UNSPECIFIED;
        goto send_err;
    }

    rc = entry->ha_fn(entry, BLE_HS_ATT_OP_READ_REQ, &arg);
    if (rc != 0) {
        rc = BLE_ERR_UNSPECIFIED;
        goto send_err;
    }

    rc = ble_hs_att_tx_read_rsp(conn, chan, arg.aha_read.attr_data,
                                arg.aha_read.attr_len);
    if (rc != 0) {
        goto send_err;
    }

    return 0;

send_err:
    ble_hs_att_tx_error_rsp(chan, BLE_HS_ATT_OP_READ_REQ,
                            req.bharq_handle, rc);
    return rc;
}

static int
ble_hs_att_tx_write_rsp(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan)
{
    uint8_t op;
    int rc;

    op = BLE_HS_ATT_OP_WRITE_RSP;
    rc = ble_l2cap_tx(chan, &op, 1);
    if (rc != 0) {
        return rc;
    }

    /* XXX: Kick L2CAP. */

    return 0;
}

static int
ble_hs_att_rx_write_req(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                        struct os_mbuf *om)
{
    union ble_hs_att_handle_arg arg;
    struct ble_hs_att_write_req req;
    struct ble_hs_att_entry *entry;
    uint8_t buf[BLE_HS_ATT_WRITE_REQ_MIN_SZ];
    int rc;

    rc = os_mbuf_copydata(om, 0, sizeof buf, buf);
    if (rc != 0) {
        return rc;
    }

    rc = ble_hs_att_write_req_parse(buf, sizeof buf, &req);
    assert(rc == 0);

    rc = ble_hs_att_find_by_handle(req.bhawq_handle, &entry);
    if (rc != 0) {
        goto send_err;
    }

    if (entry->ha_fn == NULL) {
        rc = BLE_ERR_UNSPECIFIED;
        goto send_err;
    }

    arg.aha_write.om = om;
    arg.aha_write.attr_len = OS_MBUF_PKTHDR(om)->omp_len;
    rc = entry->ha_fn(entry, BLE_HS_ATT_OP_WRITE_REQ, &arg);
    if (rc != 0) {
        rc = BLE_ERR_UNSPECIFIED;
        goto send_err;
    }

    rc = ble_hs_att_tx_write_rsp(conn, chan);
    if (rc != 0) {
        goto send_err;
    }

    return 0;

send_err:
    ble_hs_att_tx_error_rsp(chan, BLE_HS_ATT_OP_WRITE_REQ,
                            req.bhawq_handle, rc);
    return rc;
}

static int
ble_hs_att_rx(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
              struct os_mbuf *om)
{
    struct ble_hs_att_rx_dispatch_entry *entry;
    uint8_t op;
    int rc;

    rc = os_mbuf_copydata(om, 0, 1, &op);
    if (rc != 0) {
        return EMSGSIZE;
    }

    entry = ble_hs_att_rx_dispatch_entry_find(op);
    if (entry == NULL) {
        return EINVAL;
    }

    rc = entry->bde_fn(conn, chan, om);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

struct ble_l2cap_chan *
ble_hs_att_create_chan(void)
{
    struct ble_l2cap_chan *chan;

    chan = ble_l2cap_chan_alloc();
    if (chan == NULL) {
        return NULL;
    }

    chan->blc_cid = BLE_L2CAP_CID_ATT;
    chan->blc_rx_fn = ble_hs_att_rx;

    return chan;
}

int
ble_hs_att_init(void)
{
    int rc;

    STAILQ_INIT(&g_ble_hs_att_list);

    rc = os_mutex_init(&g_ble_hs_att_list_mutex);
    if (rc != 0) {
        return rc;
    }

    free(ble_hs_att_entry_mem);
    ble_hs_att_entry_mem = malloc(
        OS_MEMPOOL_BYTES(BLE_HS_ATT_NUM_ENTRIES,
                         sizeof (struct ble_hs_att_entry)));
    if (ble_hs_att_entry_mem == NULL) {
        return ENOMEM;
    }

    rc = os_mempool_init(&ble_hs_att_entry_pool, BLE_HS_ATT_NUM_ENTRIES,
                         sizeof (struct ble_hs_att_entry),
                         ble_hs_att_entry_mem, "ble_hs_att_entry_pool");
    if (rc != 0) {
        return rc;
    }

    return 0;
}
