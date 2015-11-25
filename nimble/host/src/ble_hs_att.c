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
#include "host/ble_hs.h"
#include "ble_l2cap.h"
#include "ble_hs_conn.h"
#include "ble_hs_uuid.h"
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
static int ble_hs_att_rx_find_info_req(struct ble_hs_conn *conn,
                                       struct ble_l2cap_chan *chan,
                                       struct os_mbuf *om);
static int ble_hs_att_rx_find_type_value_req(struct ble_hs_conn *conn,
                                             struct ble_l2cap_chan *chan,
                                             struct os_mbuf *om);
static int ble_hs_att_rx_read_req(struct ble_hs_conn *conn,
                                  struct ble_l2cap_chan *chan,
                                  struct os_mbuf *om);
static int ble_hs_att_rx_write_req(struct ble_hs_conn *conn,
                                   struct ble_l2cap_chan *chan,
                                   struct os_mbuf *om);

static struct ble_hs_att_rx_dispatch_entry ble_hs_att_rx_dispatch[] = {
    { BLE_HS_ATT_OP_MTU_REQ,              ble_hs_att_rx_mtu_req },
    { BLE_HS_ATT_OP_FIND_INFO_REQ,        ble_hs_att_rx_find_info_req },
    { BLE_HS_ATT_OP_FIND_TYPE_VALUE_REQ,  ble_hs_att_rx_find_type_value_req },
    { BLE_HS_ATT_OP_READ_REQ,             ble_hs_att_rx_read_req },
    { BLE_HS_ATT_OP_WRITE_REQ,            ble_hs_att_rx_write_req },
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
 * Locks the host attribute list.
 *
 * @return 0 on success, non-zero error code on failure.
 */
static void
ble_hs_att_list_lock(void)
{
    int rc;

    rc = os_mutex_pend(&g_ble_hs_att_list_mutex, OS_WAIT_FOREVER);
    assert(rc == 0 || rc == OS_NOT_STARTED);
}

/**
 * Unlocks the host attribute list
 *
 * @return 0 on success, non-zero error code on failure.
 */
static void
ble_hs_att_list_unlock(void)
{
    int rc;

    rc = os_mutex_release(&g_ble_hs_att_list_mutex);
    assert(rc == 0 || rc == OS_NOT_STARTED);
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
    /* Rollover is fatal. */
    assert(g_ble_hs_att_id != UINT16_MAX);

    return (++g_ble_hs_att_id);
}

/**
 * Register a host attribute with the BLE stack.
 *
 * @param ha                    A filled out ble_hs_att structure to register
 * @param handle_id             A pointer to a 16-bit handle ID, which will be
 *                                  the handle that is allocated.
 * @param fn                    The callback function that gets executed when
 *                                  the attribute is operated on.
 *
 * @return 0 on success, non-zero error code on failure.
 */
int
ble_hs_att_register(uint8_t *uuid, uint8_t flags, uint16_t *handle_id,
                    ble_hs_att_handle_func *fn)
{
    struct ble_hs_att_entry *entry;

    entry = ble_hs_att_entry_alloc();
    if (entry == NULL) {
        return ENOMEM;
    }

    memcpy(&entry->ha_uuid, uuid, sizeof entry->ha_uuid);
    entry->ha_flags = flags;
    entry->ha_handle_id = ble_hs_att_next_id();
    entry->ha_fn = fn;

    ble_hs_att_list_lock();
    STAILQ_INSERT_TAIL(&g_ble_hs_att_list, entry, ha_next);
    ble_hs_att_list_unlock();

    if (handle_id != NULL) {
        *handle_id = entry->ha_handle_id;
    }

    return 0;
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

    ble_hs_att_list_lock();

    *ha_ptr = NULL;
    ha = NULL;
    STAILQ_FOREACH(ha, &g_ble_hs_att_list, ha_next) {
        rc = walk_func(ha, arg);
        if (rc == 1) {
            *ha_ptr = ha;
            goto done;
        }
    }

    rc = 0;

done:
    ble_hs_att_list_unlock();
    return rc;
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
        return 0;
    } else {
        /* Not found */
        return ENOENT;
    }
}

static int
ble_hs_att_match_uuid(struct ble_hs_att_entry *ha, void *arg)
{
    uint8_t *uuid;

    uuid = arg;

    if (memcmp(ha->ha_uuid, uuid, sizeof ha->ha_uuid) == 0) {
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
ble_hs_att_find_by_uuid(uint8_t *uuid, struct ble_hs_att_entry **ha_ptr)
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

    rc = ble_l2cap_tx_flat(chan, buf, BLE_HS_ATT_ERROR_RSP_SZ);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_hs_att_tx_mtu_cmd(struct ble_l2cap_chan *chan, uint8_t op, uint16_t mtu)
{
    struct ble_hs_att_mtu_cmd cmd;
    uint8_t buf[BLE_HS_ATT_MTU_CMD_SZ];
    int rc;

    assert(!(chan->blc_flags & BLE_L2CAP_CHAN_F_TXED_MTU));
    assert(op == BLE_HS_ATT_OP_MTU_REQ || op == BLE_HS_ATT_OP_MTU_RSP);
    assert(mtu >= BLE_HS_ATT_MTU_DFLT);

    cmd.bhamc_op = op;
    cmd.bhamc_mtu = mtu;

    rc = ble_hs_att_mtu_cmd_write(buf, sizeof buf, &cmd);
    assert(rc == 0);

    rc = ble_l2cap_tx_flat(chan, buf, sizeof buf);
    if (rc != 0) {
        return rc;
    }

    chan->blc_flags |= BLE_L2CAP_CHAN_F_TXED_MTU;

    return 0;
}

static int
ble_hs_att_rx_mtu_req(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                      struct os_mbuf *om)
{
    struct ble_hs_att_mtu_cmd cmd;
    uint8_t buf[BLE_HS_ATT_MTU_CMD_SZ];
    int rc;

    /* We should only receive this command as a server. */
    if (conn->bhc_flags & BLE_HS_CONN_F_CLIENT) {
        /* XXX: Unspecified what to do in this case. */
        return EINVAL;
    }

    rc = os_mbuf_copydata(om, 0, sizeof buf, buf);
    assert(rc == 0);

    rc = ble_hs_att_mtu_cmd_parse(buf, sizeof buf, &cmd);
    assert(rc == 0);

    if (cmd.bhamc_mtu < BLE_HS_ATT_MTU_DFLT) {
        cmd.bhamc_mtu = BLE_HS_ATT_MTU_DFLT;
    }

    chan->blc_peer_mtu = cmd.bhamc_mtu;

    rc = ble_hs_att_tx_mtu_cmd(chan, BLE_HS_ATT_OP_MTU_RSP, chan->blc_my_mtu);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Fills the supplied mbuf with the variable length Information Data field of a
 * Find Information ATT response.
 *
 * @param req                   The Find Information request being responded
 *                                  to.
 * @param om                    The destination mbuf where the Information
 *                                  Data field gets written.
 * @param mtu                   The ATT L2CAP channel MTU.
 * @param format                On success, the format field of the response
 *                                  gets stored here.  One of:
 *                                     o BLE_HS_ATT_FIND_INFO_RSP_FORMAT_16BIT
 *                                     o BLE_HS_ATT_FIND_INFO_RSP_FORMAT_128BIT
 *
 * @return                      0 on success; an ATT error code on failure.
 */
static int
ble_hs_att_fill_info(struct ble_hs_att_find_info_req *req, struct os_mbuf *om,
                     uint16_t mtu, uint8_t *format)
{
    struct ble_hs_att_entry *ha;
    uint16_t handle_id;
    uint16_t uuid16;
    int num_entries;
    int rsp_sz;
    int rc;

    *format = 0;
    num_entries = 0;
    rc = 0;

    ble_hs_att_list_lock();

    STAILQ_FOREACH(ha, &g_ble_hs_att_list, ha_next) {
        if (ha->ha_handle_id > req->bhafq_end_handle) {
            rc = 0;
            goto done;
        }
        if (ha->ha_handle_id >= req->bhafq_start_handle) {
            uuid16 = ble_hs_uuid_16bit(ha->ha_uuid);

            if (*format == 0) {
                if (uuid16 != 0) {
                    *format = BLE_HS_ATT_FIND_INFO_RSP_FORMAT_16BIT;
                } else {
                    *format = BLE_HS_ATT_FIND_INFO_RSP_FORMAT_128BIT;
                }
            }

            switch (*format) {
            case BLE_HS_ATT_FIND_INFO_RSP_FORMAT_16BIT:
                if (uuid16 == 0) {
                    rc = 0;
                    goto done;
                }

                rsp_sz = OS_MBUF_PKTHDR(om)->omp_len + 4;
                if (rsp_sz > mtu) {
                    rc = 0;
                    goto done;
                }

                htole16(&handle_id, ha->ha_handle_id);
                rc = os_mbuf_append(om, &handle_id, 2);
                if (rc != 0) {
                    goto done;
                }

                htole16(&uuid16, uuid16);
                rc = os_mbuf_append(om, &uuid16, 2);
                if (rc != 0) {
                    goto done;
                }
                break;

            case BLE_HS_ATT_FIND_INFO_RSP_FORMAT_128BIT:
                if (uuid16 != 0) {
                    rc = 0;
                    goto done;
                }

                rsp_sz = OS_MBUF_PKTHDR(om)->omp_len + 18;
                if (rsp_sz > mtu) {
                    rc = 0;
                    goto done;
                }

                htole16(&handle_id, ha->ha_handle_id);
                rc = os_mbuf_append(om, &handle_id, 2);
                if (rc != 0) {
                    goto done;
                }

                rc = os_mbuf_append(om, &ha->ha_uuid,
                                    16);
                if (rc != 0) {
                    goto done;
                }
                break;

            default:
                assert(0);
                break;
            }

            num_entries++;
        }
    }

done:
    ble_hs_att_list_unlock();

    if (rc == 0 && num_entries == 0) {
        return ENOENT;
    } else {
        return rc;
    }
}

static int
ble_hs_att_rx_find_info_req(struct ble_hs_conn *conn,
                            struct ble_l2cap_chan *chan,
                            struct os_mbuf *rxom)
{
    struct ble_hs_att_find_info_req req;
    struct ble_hs_att_find_info_rsp rsp;
    struct os_mbuf *txom;
    uint8_t buf[max(BLE_HS_ATT_FIND_INFO_REQ_SZ,
                    BLE_HS_ATT_FIND_INFO_RSP_MIN_SZ)];
    int rc;

    txom = NULL;

    rc = os_mbuf_copydata(rxom, 0, BLE_HS_ATT_FIND_INFO_REQ_SZ, buf);
    if (rc != 0) {
        req.bhafq_start_handle = 0;
        rc = BLE_HS_ATT_ERR_INVALID_PDU;
        goto err;
    }

    rc = ble_hs_att_find_info_req_parse(buf, BLE_HS_ATT_FIND_INFO_REQ_SZ,
                                        &req);
    assert(rc == 0);

    /* Tx error response if start handle is greater than end handle or is equal
     * to 0 (Vol. 3, Part F, 3.4.3.1).
     */
    if (req.bhafq_start_handle > req.bhafq_end_handle ||
        req.bhafq_start_handle == 0) {

        rc = BLE_HS_ATT_ERR_INVALID_HANDLE;
        goto err;
    }

    txom = os_mbuf_get_pkthdr(&ble_hs_mbuf_pool, 0);
    if (txom == NULL) {
        rc = BLE_HS_ATT_ERR_INSUFFICIENT_RES;
        goto err;
    }

    /* Write the response base at the start of the buffer.  The format field is
     * unknown at this point; it will be filled in later.
     */
    rsp.bhafp_op = BLE_HS_ATT_OP_FIND_INFO_RSP;
    rc = ble_hs_att_find_info_rsp_write(buf, BLE_HS_ATT_FIND_INFO_RSP_MIN_SZ,
                                        &rsp);
    assert(rc == 0);
    rc = os_mbuf_append(txom, buf,
                        BLE_HS_ATT_FIND_INFO_RSP_MIN_SZ);
    if (rc != 0) {
        rc = BLE_HS_ATT_ERR_INSUFFICIENT_RES;
        goto err;
    }

    /* Write the variable length Information Data field, populating the format
     * field as appropriate.
     */
    rc = ble_hs_att_fill_info(&req, txom, ble_l2cap_chan_mtu(chan),
                              txom->om_data + 1);
    if (rc != 0) {
        rc = BLE_HS_ATT_ERR_ATTR_NOT_FOUND;
        goto err;
    }

    rc = ble_l2cap_tx(chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    ble_hs_att_tx_error_rsp(chan, BLE_HS_ATT_OP_FIND_INFO_REQ,
                            req.bhafq_start_handle, rc);

    return rc;
}

/**
 * Processes a single non-matching attribute entry while filling a
 * Find-By-Type-Value-Response.
 *
 * @param om                    The response mbuf.
 * @param first                 Pointer to the first matching handle ID in the
 *                                  current group of IDs.  0 if there is not a
 *                                  current group.
 * @param prev                  Pointer to the most recent matching handle ID
 *                                  in the current group of IDs.  0 if there is
 *                                  not a current group.
 * @param mtu                   The ATT L2CAP channel MTU.
 *
 * @return                      0 if the response should be sent;
 *                              EAGAIN if the entry was successfully processed
 *                                  and subsequent entries can be inspected.
 *                              Other nonzero on error.
 */
static int
ble_hs_att_fill_type_value_no_match(struct os_mbuf *om, uint16_t *first,
                                    uint16_t *prev, int mtu)
{
    uint16_t u16;
    int rsp_sz;
    int rc;

    /* If there is no current group, then there is nothing to do. */
    if (*first == 0) {
        return EAGAIN;
    }

    rsp_sz = OS_MBUF_PKTHDR(om)->omp_len + 4;
    if (rsp_sz > mtu) {
        return 0;
    }

    u16 = *first;
    htole16(&u16, u16);
    rc = os_mbuf_append(om, &u16, 2);
    if (rc != 0) {
        return ENOMEM;
    }

    u16 = *prev;
    htole16(&u16, u16);
    rc = os_mbuf_append(om, &u16, 2);
    if (rc != 0) {
        return ENOMEM;
    }

    *first = 0;
    *prev = 0;

    return EAGAIN;
}

/**
 * Processes a single matching attribute entry while filling a
 * Find-By-Type-Value-Response.
 *
 * @param om                    The response mbuf.
 * @param first                 Pointer to the first matching handle ID in the
 *                                  current group of IDs.  0 if there is not a
 *                                  current group.
 * @param prev                  Pointer to the most recent matching handle ID
 *                                  in the current group of IDs.  0 if there is
 *                                  not a current group.
 * @param handle_id             The matching handle ID to process.
 * @param mtu                   The ATT L2CAP channel MTU.
 *
 * @return                      0 if the response should be sent;
 *                              EAGAIN if the entry was successfully processed
 *                                  and subsequent entries can be inspected.
 *                              Other nonzero on error.
 */
static int
ble_hs_att_fill_type_value_match(struct os_mbuf *om, uint16_t *first,
                                 uint16_t *prev, uint16_t handle_id, int mtu)
{
    int rc;

    /* If this is the start of a group, record it as the first ID and keep
     * searching.
     */
    if (*first == 0) {
        *first = handle_id;
        *prev = handle_id;
        return EAGAIN;
    }

    /* If this is the continuation of a group, keep searching. */
    if (handle_id == *prev + 1) {
        *prev = handle_id;
        return EAGAIN;
    }

    /* Otherwise, this handle is not a part of the previous group.  Write the
     * previous group to the response, and remember this ID as the start of the
     * next group.
     */
    rc = ble_hs_att_fill_type_value_no_match(om, first, prev, mtu);
    *first = handle_id;
    *prev = handle_id;
    return rc;
}

/**
 * Fills the supplied mbuf with the variable length Handles-Information-List
 * field of a Find-By-Type-Value ATT response.
 *
 * @param req                   The Find-By-Type-Value-Request being responded
 *                                  to.
 * @param rxom                  The mbuf containing the received request.
 * @param txom                  The destination mbuf where the
 *                                  Handles-Information-List field gets
 *                                  written.
 * @param mtu                   The ATT L2CAP channel MTU.
 *
 * @return                      0 on success; an ATT error code on failure.
 */
static int
ble_hs_att_fill_type_value(struct ble_hs_att_find_type_value_req *req,
                           struct os_mbuf *rxom, struct os_mbuf *txom,
                           uint16_t mtu)
{
    union ble_hs_att_handle_arg arg;
    struct ble_hs_att_entry *ha;
    uint16_t uuid16;
    uint16_t first;
    uint16_t prev;
    int any_entries;
    int match;
    int rc;

    first = 0;
    prev = 0;
    rc = 0;

    ble_hs_att_list_lock();

    /* Iterate through the attribute list, keeping track of the current
     * matching group.  For each attribute entry, determine if data needs to be
     * written to the response.
     */
    STAILQ_FOREACH(ha, &g_ble_hs_att_list, ha_next) {
        match = 0;

        if (ha->ha_handle_id > req->bhavq_end_handle) {
            break;
        }

        if (ha->ha_handle_id >= req->bhavq_start_handle) {
            /* Compare the attribute type and value to the request fields to
             * determine if this attribute matches.
             */
            uuid16 = ble_hs_uuid_16bit(ha->ha_uuid);
            if (uuid16 == req->bhavq_attr_type) {
                rc = ha->ha_fn(ha, BLE_HS_ATT_OP_READ_REQ, &arg);
                if (rc != 0) {
                    rc = BLE_HS_ATT_ERR_UNLIKELY;
                    goto done;
                }
                rc = os_mbuf_memcmp(rxom,
                                    BLE_HS_ATT_FIND_TYPE_VALUE_REQ_MIN_SZ,
                                    arg.aha_read.attr_data,
                                    arg.aha_read.attr_len);
                if (rc == 0) {
                    match = 1;
                }
            }
        }

        if (match) {
            rc = ble_hs_att_fill_type_value_match(txom, &first, &prev,
                                                  ha->ha_handle_id, mtu);
        } else {
            rc = ble_hs_att_fill_type_value_no_match(txom, &first, &prev, mtu);
        }

        if (rc == 0) {
            goto done;
        }
        if (rc != EAGAIN) {
            rc = BLE_HS_ATT_ERR_UNLIKELY;
            goto done;
        }
    }

    /* Process one last non-matching ID in case a group was in progress when
     * the end of the attribute list was reached.
     */
    rc = ble_hs_att_fill_type_value_no_match(txom, &first, &prev, mtu);
    if (rc == EAGAIN) {
        rc = 0;
    } else if (rc != 0) {
        rc = BLE_HS_ATT_ERR_UNLIKELY;
    }

done:
    ble_hs_att_list_unlock();

    any_entries = OS_MBUF_PKTHDR(txom)->omp_len >
                  BLE_HS_ATT_FIND_TYPE_VALUE_RSP_MIN_SZ;
    if (rc == 0 && !any_entries) {
        return BLE_HS_ATT_ERR_ATTR_NOT_FOUND;
    } else {
        return rc;
    }
}

static int
ble_hs_att_rx_find_type_value_req(struct ble_hs_conn *conn,
                                  struct ble_l2cap_chan *chan,
                                  struct os_mbuf *rxom)
{
    struct ble_hs_att_find_type_value_req req;
    struct os_mbuf *txom;
    uint8_t buf[max(BLE_HS_ATT_FIND_TYPE_VALUE_REQ_MIN_SZ,
                    BLE_HS_ATT_FIND_TYPE_VALUE_RSP_MIN_SZ)];
    int rc;

    txom = NULL;

    rc = os_mbuf_copydata(rxom, 0, BLE_HS_ATT_FIND_TYPE_VALUE_REQ_MIN_SZ, buf);
    if (rc != 0) {
        req.bhavq_start_handle = 0;
        rc = BLE_HS_ATT_ERR_INVALID_PDU;
        goto err;
    }

    rc = ble_hs_att_find_type_value_req_parse(
        buf, BLE_HS_ATT_FIND_TYPE_VALUE_REQ_MIN_SZ, &req);
    assert(rc == 0);

    /* Tx error response if start handle is greater than end handle or is equal
     * to 0 (Vol. 3, Part F, 3.4.3.3).
     */
    if (req.bhavq_start_handle > req.bhavq_end_handle ||
        req.bhavq_start_handle == 0) {

        rc = BLE_HS_ATT_ERR_INVALID_HANDLE;
        goto err;
    }

    txom = os_mbuf_get_pkthdr(&ble_hs_mbuf_pool, 0);
    if (txom == NULL) {
        rc = BLE_HS_ATT_ERR_INSUFFICIENT_RES;
        goto err;
    }

    /* Write the response base at the start of the buffer. */
    buf[0] = BLE_HS_ATT_OP_FIND_TYPE_VALUE_RSP;
    rc = os_mbuf_append(txom, buf,
                        BLE_HS_ATT_FIND_TYPE_VALUE_RSP_MIN_SZ);
    if (rc != 0) {
        rc = BLE_HS_ATT_ERR_INSUFFICIENT_RES;
        goto err;
    }

    /* Write the variable length Information Data field. */
    rc = ble_hs_att_fill_type_value(&req, rxom, txom,
                                    ble_l2cap_chan_mtu(chan));
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_tx(chan, txom);
    txom = NULL;
    if (rc != 0) {
        rc = BLE_HS_ATT_ERR_UNLIKELY;
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    ble_hs_att_tx_error_rsp(chan, BLE_HS_ATT_OP_FIND_TYPE_VALUE_REQ,
                            req.bhavq_start_handle, rc);
    return rc;
}

static int
ble_hs_att_tx_read_rsp(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                       void *attr_data, int attr_len)
{
    struct os_mbuf *txom;
    uint16_t data_len;
    uint8_t op;
    int rc;

    txom = os_mbuf_get_pkthdr(&ble_hs_mbuf_pool, 0);
    if (txom == NULL) {
        rc = BLE_HS_ATT_ERR_INSUFFICIENT_RES;
        goto err;
    }

    op = BLE_HS_ATT_OP_READ_RSP;
    rc = os_mbuf_append(txom, &op, 1);
    if (rc != 0) {
        rc = BLE_HS_ATT_ERR_INSUFFICIENT_RES;
        goto err;
    }

    /* Vol. 3, part F, 3.2.9; don't send more than ATT_MTU-1 bytes of data. */
    if (attr_len > ble_l2cap_chan_mtu(chan) - 1) {
        data_len = ble_l2cap_chan_mtu(chan) - 1;
    } else {
        data_len = attr_len;
    }

    rc = os_mbuf_append(txom, attr_data, data_len);
    if (rc != 0) {
        rc = BLE_HS_ATT_ERR_INSUFFICIENT_RES;
        goto err;
    }

    rc = ble_l2cap_tx(chan, txom);
    if (rc != 0) {
        rc = BLE_HS_ATT_ERR_UNLIKELY;
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
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
        req.bharq_handle = 0;
        rc = BLE_HS_ATT_ERR_INVALID_PDU;
        goto err;
    }

    rc = ble_hs_att_read_req_parse(buf, sizeof buf, &req);
    assert(rc == 0);

    rc = ble_hs_att_find_by_handle(req.bharq_handle, &entry);
    if (rc != 0) {
        rc = BLE_HS_ATT_ERR_INVALID_HANDLE;
        goto err;
    }

    if (entry->ha_fn == NULL) {
        rc = BLE_ERR_UNSPECIFIED;
        goto err;
    }

    rc = entry->ha_fn(entry, BLE_HS_ATT_OP_READ_REQ, &arg);
    if (rc != 0) {
        rc = BLE_ERR_UNSPECIFIED;
        goto err;
    }

    rc = ble_hs_att_tx_read_rsp(conn, chan, arg.aha_read.attr_data,
                                arg.aha_read.attr_len);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
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
    rc = ble_l2cap_tx_flat(chan, &op, 1);
    if (rc != 0) {
        return rc;
    }

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
        rc = BLE_HS_ATT_ERR_INVALID_HANDLE;
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
    chan->blc_my_mtu = BLE_HS_ATT_MTU_DFLT;
    chan->blc_default_mtu = BLE_HS_ATT_MTU_DFLT;
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
        goto err;
    }

    free(ble_hs_att_entry_mem);
    ble_hs_att_entry_mem = malloc(
        OS_MEMPOOL_BYTES(BLE_HS_ATT_NUM_ENTRIES,
                         sizeof (struct ble_hs_att_entry)));
    if (ble_hs_att_entry_mem == NULL) {
        rc = ENOMEM;
        goto err;
    }

    rc = os_mempool_init(&ble_hs_att_entry_pool, BLE_HS_ATT_NUM_ENTRIES,
                         sizeof (struct ble_hs_att_entry),
                         ble_hs_att_entry_mem, "ble_hs_att_entry_pool");
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    free(ble_hs_att_entry_mem);
    ble_hs_att_entry_mem = NULL;

    return rc;
}
