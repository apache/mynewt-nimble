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
#include "host/attr.h"
#include "ble_l2cap.h"
#include "ble_l2cap_util.h"
#include "ble_hs_conn.h"
#include "ble_hs_att.h"

typedef int ble_hs_att_rx_fn(struct ble_hs_conn *conn,
                             struct ble_l2cap_chan *chan);
struct ble_hs_att_rx_dispatch_entry {
    uint8_t bde_op;
    ble_hs_att_rx_fn *bde_fn;
};

#define BLE_HS_ATT_OP_READ          0x0a

/** Dispatch table for incoming ATT requests.  Sorted by op code. */
static int ble_hs_att_rx_read(struct ble_hs_conn *conn,
                              struct ble_l2cap_chan *chan);

struct ble_hs_att_rx_dispatch_entry ble_hs_att_rx_dispatch[] = {
    { BLE_HS_ATT_OP_READ, ble_hs_att_rx_read },
};

#define BLE_HS_ATT_RX_DISPATCH_SZ \
    (sizeof ble_hs_att_rx_dispatch / sizeof ble_hs_att_rx_dispatch[0])

static STAILQ_HEAD(, host_attr) g_host_attr_list = 
    STAILQ_HEAD_INITIALIZER(g_host_attr_list);
static uint16_t g_host_attr_id;

static struct os_mutex g_host_attr_list_mutex;

/**
 * Lock the host attribute list.
 * 
 * @return 0 on success, non-zero error code on failure.
 */
static int 
host_attr_list_lock(void)
{
    int rc;

    rc = os_mutex_pend(&g_host_attr_list_mutex, OS_WAIT_FOREVER);
    if (rc != 0) {
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
host_attr_list_unlock(void)
{
    int rc;

    rc = os_mutex_release(&g_host_attr_list_mutex);
    if (rc != 0) {
        goto err;
    }

    return (0);
err:
    return (rc);
}


/**
 * Allocate the next handle id and return it.
 *
 * @return A new 16-bit handle ID.
 */
static uint16_t
host_attr_next_id(void)
{
    return (++g_host_attr_id);
}

/**
 * Register a host attribute with the BLE stack.
 *
 * @param ha A filled out host_attr structure to register
 * @param handle_id A pointer to a 16-bit handle ID, which will be the 
 *                  handle that is allocated.
 *
 * @return 0 on success, non-zero error code on failure.
 */
int 
host_attr_register(struct host_attr *ha, uint16_t *handle_id)
{
    int rc;

    *handle_id = host_attr_next_id();
    ha->ha_handle_id = *handle_id;

    rc = host_attr_list_lock();
    if (rc != 0) {
        goto err;
    }

    STAILQ_INSERT_TAIL(&g_host_attr_list, ha, ha_next);

    rc = host_attr_list_unlock();
    if (rc != 0) {
        goto err;
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
 *                  host_attr element processed, or NULL if the entire list has 
 *                  been processed
 *
 * @return 1 on stopped, 0 on fully processed and an error code otherwise.
 */
int
host_attr_walk(host_attr_walk_func_t walk_func, void *arg, 
        struct host_attr **ha_ptr)
{
    struct host_attr *ha;
    int rc;

    rc = host_attr_list_lock();
    if (rc != 0) {
        goto err;
    }

    *ha_ptr = NULL;
    ha = NULL;
    STAILQ_FOREACH(ha, &g_host_attr_list, ha_next) {
        rc = walk_func(ha, arg);
        if (rc == 1) {
            rc = host_attr_list_unlock();
            if (rc != 0) {
                goto err;
            }
            *ha_ptr = ha;
            return (1);
        }
    }

    rc = host_attr_list_unlock();
    if (rc != 0) {
        goto err;
    }

    return (0);
err:
    return (rc);
}

static int 
host_attr_match_handle(struct host_attr *ha, void *arg)
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
 * @param host_attr A pointer to a pointer to put the matching host attr into.
 *
 * @return 0 on success, BLE_ERR_ATTR_NOT_FOUND on not found, and non-zero on 
 *         error.
 */
int
host_attr_find_by_handle(uint16_t handle_id, struct host_attr **ha_ptr)
{
    int rc;

    rc = host_attr_walk(host_attr_match_handle, &handle_id, ha_ptr);
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
host_attr_match_uuid(struct host_attr *ha, void *arg)
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
host_attr_find_by_uuid(ble_uuid_t *uuid, struct host_attr **ha_ptr) 
{
    int rc;
    
    rc = host_attr_walk(host_attr_match_uuid, uuid, ha_ptr);
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

/**
 * | Parameter          | Size (octets) |
 * +--------------------+---------------+
 * | Attribute Opcode   | 1             |
 * | Attribute Handle   | 2             |
 */
static int
ble_hs_att_rx_read(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan)
{
    uint16_t handle;
    uint8_t op;
    int off;
    int rc;

    off = 0;

    rc = os_mbuf_copydata(chan->blc_rx_buf, off, 1, &op);
    if (rc != 0) {
        return EMSGSIZE;
    }
    off += 1;

    assert(op == BLE_HS_ATT_OP_READ);

    rc = ble_l2cap_read_uint16(chan, off, &handle);
    if (rc != 0) {
        return EMSGSIZE;
    }
    off += 2;

    /* XXX: Perform read. */

    /* Strip ATT read request from the buffer. */
    ble_l2cap_strip(chan, off);

    return 0;
}

static int
ble_hs_att_rx(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan)
{
    struct ble_hs_att_rx_dispatch_entry *entry;
    uint8_t op;
    int rc;

    rc = os_mbuf_copydata(chan->blc_rx_buf, 0, 1, &op);
    if (rc != 0) {
        return EMSGSIZE;
    }

    entry = ble_hs_att_rx_dispatch_entry_find(op);
    if (entry == NULL) {
        return EINVAL;
    }

    rc = entry->bde_fn(conn, chan);
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
