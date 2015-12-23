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
#include "host/ble_hs_uuid.h"
#include "ble_hs_priv.h"
#include "ble_hs_priv.h"
#include "ble_l2cap.h"
#include "ble_hs_conn.h"
#include "ble_att_cmd.h"
#include "ble_att_priv.h"

static STAILQ_HEAD(, ble_att_svr_entry) ble_att_svr_list;
static uint16_t ble_att_svr_id;

static struct os_mutex ble_att_svr_list_mutex;

#define BLE_ATT_SVR_NUM_ENTRIES          32
static void *ble_att_svr_entry_mem;
static struct os_mempool ble_att_svr_entry_pool;


#define BLE_ATT_SVR_PREP_MBUF_BUF_SIZE         (128)
#define BLE_ATT_SVR_PREP_MBUF_MEMBLOCK_SIZE                     \
    (BLE_ATT_SVR_PREP_MBUF_BUF_SIZE + sizeof(struct os_mbuf) +  \
     sizeof(struct os_mbuf_pkthdr))

#define BLE_ATT_SVR_NUM_PREP_ENTRIES     8
#define BLE_ATT_SVR_NUM_PREP_MBUFS       12
static void *ble_att_svr_prep_entry_mem;
static struct os_mempool ble_att_svr_prep_entry_pool;
static void *ble_att_svr_prep_mbuf_mem;
static struct os_mempool ble_att_svr_prep_mbuf_mempool;
static struct os_mbuf_pool ble_att_svr_prep_mbuf_pool;

static uint8_t ble_att_svr_flat_buf[BLE_ATT_ATTR_MAX_LEN];

/**
 * Locks the host attribute list.
 *
 * @return 0 on success, non-zero error code on failure.
 */
static void
ble_att_svr_list_lock(void)
{
    int rc;

    rc = os_mutex_pend(&ble_att_svr_list_mutex, OS_WAIT_FOREVER);
    assert(rc == 0 || rc == OS_NOT_STARTED);
}

/**
 * Unlocks the host attribute list
 *
 * @return 0 on success, non-zero error code on failure.
 */
static void
ble_att_svr_list_unlock(void)
{
    int rc;

    rc = os_mutex_release(&ble_att_svr_list_mutex);
    assert(rc == 0 || rc == OS_NOT_STARTED);
}

static struct ble_att_svr_entry *
ble_att_svr_entry_alloc(void)
{
    struct ble_att_svr_entry *entry;

    entry = os_memblock_get(&ble_att_svr_entry_pool);
    if (entry != NULL) {
        memset(entry, 0, sizeof *entry);
    }

    return entry;
}

#if 0
static void
ble_att_svr_entry_free(struct ble_att_svr_entry *entry)
{
    int rc;

    rc = os_memblock_put(&ble_att_svr_entry_pool, entry);
    assert(rc == 0);
}
#endif

/**
 * Allocate the next handle id and return it.
 *
 * @return A new 16-bit handle ID.
 */
static uint16_t
ble_att_svr_next_id(void)
{
    /* Rollover is fatal. */
    assert(ble_att_svr_id != UINT16_MAX);

    return (++ble_att_svr_id);
}

/**
 * Register a host attribute with the BLE stack.
 *
 * @param ha                    A filled out ble_att structure to register
 * @param handle_id             A pointer to a 16-bit handle ID, which will be
 *                                  the handle that is allocated.
 * @param fn                    The callback function that gets executed when
 *                                  the attribute is operated on.
 *
 * @return 0 on success, non-zero error code on failure.
 */
int
ble_att_svr_register(uint8_t *uuid, uint8_t flags, uint16_t *handle_id,
                     ble_att_svr_access_fn *cb, void *cb_arg)
{
    struct ble_att_svr_entry *entry;

    entry = ble_att_svr_entry_alloc();
    if (entry == NULL) {
        return BLE_HS_ENOMEM;
    }

    memcpy(&entry->ha_uuid, uuid, sizeof entry->ha_uuid);
    entry->ha_flags = flags;
    entry->ha_handle_id = ble_att_svr_next_id();
    entry->ha_cb = cb;
    entry->ha_cb_arg = cb_arg;

    ble_att_svr_list_lock();
    STAILQ_INSERT_TAIL(&ble_att_svr_list, entry, ha_next);
    ble_att_svr_list_unlock();

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
 * @param ha_ptr    On input: Indicates the starting point of the walk;
 *                      null means start at the beginning of the list,
 *                      non-null means start at the following entry.
 *                  On output: Indicates the last ble_att element processed,
 *                      or NULL if the entire list has been processed.
 *
 * @return 1 on stopped, 0 on fully processed and an error code otherwise.
 */
int
ble_att_svr_walk(ble_att_svr_walk_func_t walk_func, void *arg,
                 struct ble_att_svr_entry **ha_ptr)
{
    struct ble_att_svr_entry *ha;
    int rc;

    assert(ha_ptr != NULL);

    ble_att_svr_list_lock();

    if (*ha_ptr == NULL) {
        ha = STAILQ_FIRST(&ble_att_svr_list);
    } else {
        ha = STAILQ_NEXT(*ha_ptr, ha_next);
    }

    while (ha != NULL) {
        rc = walk_func(ha, arg);
        if (rc == 1) {
            *ha_ptr = ha;
            goto done;
        }

        ha = STAILQ_NEXT(ha, ha_next);
    }

    rc = 0;

done:
    ble_att_svr_list_unlock();
    return rc;
}

static int
ble_att_svr_match_handle(struct ble_att_svr_entry *ha, void *arg)
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
 * @param handle_id             The handle_id to search for
 * @param ha_ptr                A pointer to a pointer to put the matching host
 *                                  attr into.
 *
 * @return                      0 on success; BLE_HS_ENOENT on not found.
 */
int
ble_att_svr_find_by_handle(uint16_t handle_id,
                           struct ble_att_svr_entry **ha_ptr)
{
    int rc;

    rc = ble_att_svr_walk(ble_att_svr_match_handle, &handle_id, ha_ptr);
    if (rc == 1) {
        /* Found a matching handle */
        return 0;
    } else {
        /* Not found */
        return BLE_HS_ENOENT;
    }
}

static int
ble_att_svr_match_uuid(struct ble_att_svr_entry *ha, void *arg)
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
 * @param uuid                  The ble_uuid_t to search for
 * @param ha_ptr                A pointer to a pointer to put the matching host
 *                                  attr into.
 *
 * @return                      0 on success; BLE_HS_ENOENT on not found.
 */
int
ble_att_svr_find_by_uuid(uint8_t *uuid,
                         struct ble_att_svr_entry **ha_ptr)
{
    int rc;

    rc = ble_att_svr_walk(ble_att_svr_match_uuid, uuid, ha_ptr);
    if (rc == 1) {
        /* Found a matching handle */
        return 0;
    } else {
        /* No match */
        return BLE_HS_ENOENT;
    }
}

static int
ble_att_svr_tx_error_rsp(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                         uint8_t req_op, uint16_t handle, uint8_t error_code)
{
    struct ble_att_error_rsp rsp;
    struct os_mbuf *txom;
    void *dst;
    int rc;

    txom = ble_att_get_pkthdr();
    if (txom == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    dst = os_mbuf_extend(txom, BLE_ATT_ERROR_RSP_SZ);
    if (dst == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rsp.baep_req_op = req_op;
    rsp.baep_handle = handle;
    rsp.baep_error_code = error_code;

    rc = ble_att_error_rsp_write(dst, BLE_ATT_ERROR_RSP_SZ, &rsp);
    assert(rc == 0);

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

static int
ble_att_svr_tx_mtu_rsp(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                       uint8_t op, uint16_t mtu, uint8_t *att_err)
{
    struct ble_att_mtu_cmd cmd;
    struct os_mbuf *txom;
    void *dst;
    int rc;

    *att_err = 0; /* Silence unnecessary warning. */

    assert(op == BLE_ATT_OP_MTU_REQ || op == BLE_ATT_OP_MTU_RSP);
    assert(mtu >= BLE_ATT_MTU_DFLT);

    txom = ble_att_get_pkthdr();
    if (txom == NULL) {
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    dst = os_mbuf_extend(txom, BLE_ATT_MTU_CMD_SZ);
    if (dst == NULL) {
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    cmd.bamc_mtu = mtu;

    rc = ble_att_mtu_rsp_write(dst, BLE_ATT_MTU_CMD_SZ, &cmd);
    assert(rc == 0);

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        *att_err = BLE_ATT_ERR_UNLIKELY;
        goto err;
    }

    chan->blc_flags |= BLE_L2CAP_CHAN_F_TXED_MTU;
    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_att_svr_rx_mtu(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                   struct os_mbuf **om)
{
    struct ble_att_mtu_cmd cmd;
    uint8_t att_err;
    int rc;

    *om = os_mbuf_pullup(*om, BLE_ATT_MTU_CMD_SZ);
    if (*om == NULL) {
        att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = ble_att_mtu_cmd_parse((*om)->om_data, (*om)->om_len, &cmd);
    assert(rc == 0);

    ble_att_set_peer_mtu(chan, cmd.bamc_mtu);
    rc = ble_att_svr_tx_mtu_rsp(conn, chan, BLE_ATT_OP_MTU_RSP,
                                chan->blc_my_mtu, &att_err);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    ble_att_svr_tx_error_rsp(conn, chan, BLE_ATT_OP_MTU_REQ, 0, att_err);
    return rc;
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
 *                                     o BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT
 *                                     o BLE_ATT_FIND_INFO_RSP_FORMAT_128BIT
 *
 * @return                      0 on success; nonzero on failure.
 */
static int
ble_att_svr_fill_info(struct ble_att_find_info_req *req, struct os_mbuf *om,
                      uint16_t mtu, uint8_t *format)
{
    struct ble_att_svr_entry *ha;
    uint16_t handle_id;
    uint16_t uuid16;
    int num_entries;
    int rsp_sz;
    int rc;

    *format = 0;
    num_entries = 0;
    rc = 0;

    ble_att_svr_list_lock();

    STAILQ_FOREACH(ha, &ble_att_svr_list, ha_next) {
        if (ha->ha_handle_id > req->bafq_end_handle) {
            rc = 0;
            goto done;
        }
        if (ha->ha_handle_id >= req->bafq_start_handle) {
            uuid16 = ble_hs_uuid_16bit(ha->ha_uuid);

            if (*format == 0) {
                if (uuid16 != 0) {
                    *format = BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT;
                } else {
                    *format = BLE_ATT_FIND_INFO_RSP_FORMAT_128BIT;
                }
            }

            switch (*format) {
            case BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT:
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

            case BLE_ATT_FIND_INFO_RSP_FORMAT_128BIT:
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
    ble_att_svr_list_unlock();

    if (rc == 0 && num_entries == 0) {
        return BLE_HS_ENOENT;
    } else {
        return rc;
    }
}

static int
ble_att_svr_tx_find_info(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                         struct ble_att_find_info_req *req, uint8_t *att_err)
{
    struct ble_att_find_info_rsp rsp;
    struct os_mbuf *txom;
    void *buf;
    int rc;

    txom = ble_att_get_pkthdr();
    if (txom == NULL) {
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    /* Write the response base at the start of the buffer.  The format field is
     * unknown at this point; it will be filled in later.
     */
    buf = os_mbuf_extend(txom, BLE_ATT_FIND_INFO_RSP_BASE_SZ);
    if (buf == NULL) {
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = ble_att_find_info_rsp_write(buf, BLE_ATT_FIND_INFO_RSP_BASE_SZ, &rsp);
    assert(rc == 0);

    /* Write the variable length Information Data field, populating the format
     * field as appropriate.
     */
    rc = ble_att_svr_fill_info(req, txom, ble_l2cap_chan_mtu(chan),
                               txom->om_data + 1);
    if (rc != 0) {
        *att_err = BLE_ATT_ERR_ATTR_NOT_FOUND;
        rc = BLE_HS_ENOENT;
        goto err;
    }

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        *att_err = BLE_ATT_ERR_UNLIKELY;
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_att_svr_rx_find_info(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                         struct os_mbuf **rxom)
{
    struct ble_att_find_info_req req;
    uint16_t err_handle;
    uint8_t att_err;
    int rc;

    *rxom = os_mbuf_pullup(*rxom, BLE_ATT_MTU_CMD_SZ);
    if (*rxom == NULL) {
        att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        err_handle = 0;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = ble_att_find_info_req_parse((*rxom)->om_data, (*rxom)->om_len, &req);
    assert(rc == 0);

    /* Tx error response if start handle is greater than end handle or is equal
     * to 0 (Vol. 3, Part F, 3.4.3.1).
     */
    if (req.bafq_start_handle > req.bafq_end_handle ||
        req.bafq_start_handle == 0) {

        att_err = BLE_ATT_ERR_INVALID_HANDLE;
        err_handle = req.bafq_start_handle;
        rc = BLE_HS_EBADDATA;
        goto err;
    }

    rc = ble_att_svr_tx_find_info(conn, chan, &req, &att_err);
    if (rc != 0) {
        err_handle = req.bafq_start_handle;
        goto err;
    }

    return 0;

err:
    ble_att_svr_tx_error_rsp(conn, chan, BLE_ATT_OP_FIND_INFO_REQ,
                             err_handle, att_err);
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
 *                              BLE_HS_EAGAIN if the entry was successfully
 *                                  processed and subsequent entries can be
 *                                  inspected.
 *                              Other nonzero on error.
 */
static int
ble_att_svr_fill_type_value_no_match(struct os_mbuf *om, uint16_t *first,
                                     uint16_t *prev, int mtu)
{
    uint16_t u16;
    int rsp_sz;
    int rc;

    /* If there is no current group, then there is nothing to do. */
    if (*first == 0) {
        return BLE_HS_EAGAIN;
    }

    rsp_sz = OS_MBUF_PKTHDR(om)->omp_len + 4;
    if (rsp_sz > mtu) {
        return 0;
    }

    u16 = *first;
    htole16(&u16, u16);
    rc = os_mbuf_append(om, &u16, 2);
    if (rc != 0) {
        return BLE_HS_ENOMEM;
    }

    u16 = *prev;
    htole16(&u16, u16);
    rc = os_mbuf_append(om, &u16, 2);
    if (rc != 0) {
        return BLE_HS_ENOMEM;
    }

    *first = 0;
    *prev = 0;

    return BLE_HS_EAGAIN;
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
 *                              BLE_HS_EAGAIN if the entry was successfully
 *                                  processed and subsequent entries can be
 *                                  inspected.
 *                              Other nonzero on error.
 */
static int
ble_att_svr_fill_type_value_match(struct os_mbuf *om, uint16_t *first,
                                  uint16_t *prev, uint16_t handle_id,
                                  int mtu)
{
    int rc;

    /* If this is the start of a group, record it as the first ID and keep
     * searching.
     */
    if (*first == 0) {
        *first = handle_id;
        *prev = handle_id;
        return BLE_HS_EAGAIN;
    }

    /* If this is the continuation of a group, keep searching. */
    if (handle_id == *prev + 1) {
        *prev = handle_id;
        return BLE_HS_EAGAIN;
    }

    /* Otherwise, this handle is not a part of the previous group.  Write the
     * previous group to the response, and remember this ID as the start of the
     * next group.
     */
    rc = ble_att_svr_fill_type_value_no_match(om, first, prev, mtu);
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
 * @return                      0 on success;
 *                              BLE_HS_ENOENT if attribute not found;
 *                              BLE_HS_EAPP on other error.
 */
static int
ble_att_svr_fill_type_value(struct ble_att_find_type_value_req *req,
                            struct os_mbuf *rxom, struct os_mbuf *txom,
                            uint16_t mtu)
{
    union ble_att_svr_access_ctxt arg;
    struct ble_att_svr_entry *ha;
    uint16_t uuid16;
    uint16_t first;
    uint16_t prev;
    int any_entries;
    int match;
    int rc;

    first = 0;
    prev = 0;
    rc = 0;

    ble_att_svr_list_lock();

    /* Iterate through the attribute list, keeping track of the current
     * matching group.  For each attribute entry, determine if data needs to be
     * written to the response.
     */
    STAILQ_FOREACH(ha, &ble_att_svr_list, ha_next) {
        match = 0;

        if (ha->ha_handle_id > req->bavq_end_handle) {
            break;
        }

        if (ha->ha_handle_id >= req->bavq_start_handle) {
            /* Compare the attribute type and value to the request fields to
             * determine if this attribute matches.
             */
            uuid16 = ble_hs_uuid_16bit(ha->ha_uuid);
            if (uuid16 == req->bavq_attr_type) {
                rc = ha->ha_cb(ha->ha_handle_id, ha->ha_uuid,
                               BLE_ATT_ACCESS_OP_READ, &arg, ha->ha_cb_arg);
                if (rc != 0) {
                    rc = BLE_HS_EAPP;
                    goto done;
                }
                rc = os_mbuf_memcmp(rxom,
                                    BLE_ATT_FIND_TYPE_VALUE_REQ_BASE_SZ,
                                    arg.ahc_read.attr_data,
                                    arg.ahc_read.attr_len);
                if (rc == 0) {
                    match = 1;
                }
            }
        }

        if (match) {
            rc = ble_att_svr_fill_type_value_match(txom, &first, &prev,
                                                   ha->ha_handle_id, mtu);
        } else {
            rc = ble_att_svr_fill_type_value_no_match(txom, &first, &prev,
                                                      mtu);
        }

        if (rc == 0) {
            goto done;
        }
        if (rc != BLE_HS_EAGAIN) {
            rc = BLE_HS_EAPP;
            goto done;
        }
    }

    /* Process one last non-matching ID in case a group was in progress when
     * the end of the attribute list was reached.
     */
    rc = ble_att_svr_fill_type_value_no_match(txom, &first, &prev, mtu);
    if (rc == BLE_HS_EAGAIN) {
        rc = 0;
    } else if (rc != 0) {
        rc = BLE_HS_EAPP;
    }

done:
    ble_att_svr_list_unlock();

    any_entries = OS_MBUF_PKTHDR(txom)->omp_len >
                  BLE_ATT_FIND_TYPE_VALUE_RSP_BASE_SZ;
    if (rc == 0 && !any_entries) {
        return BLE_HS_ENOENT;
    } else {
        return rc;
    }
}

static int
ble_att_svr_tx_find_type_value(struct ble_hs_conn *conn,
                               struct ble_l2cap_chan *chan,
                               struct ble_att_find_type_value_req *req,
                               struct os_mbuf *rxom,
                               uint8_t *att_err)
{
    struct os_mbuf *txom;
    uint8_t *buf;
    int rc;

    txom = ble_att_get_pkthdr();
    if (txom == NULL) {
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    /* Write the response base at the start of the buffer. */
    buf = os_mbuf_extend(txom, BLE_ATT_FIND_TYPE_VALUE_RSP_BASE_SZ);
    if (buf == NULL) {
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        rc = BLE_HS_ENOMEM;
        goto err;
    }
    buf[0] = BLE_ATT_OP_FIND_TYPE_VALUE_RSP;

    /* Write the variable length Information Data field. */
    rc = ble_att_svr_fill_type_value(req, rxom, txom,
                                     ble_l2cap_chan_mtu(chan));
    switch (rc) {
    case 0:
        break;

    case BLE_HS_ENOENT:
        *att_err = BLE_ATT_ERR_ATTR_NOT_FOUND;
        goto err;

    case BLE_HS_EAPP:
        *att_err = BLE_ATT_ERR_UNLIKELY;
        goto err;

    default:
        assert(0);
        *att_err = BLE_ATT_ERR_UNLIKELY;
        goto err;
    }

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        *att_err = BLE_ATT_ERR_UNLIKELY;
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_att_svr_rx_find_type_value(struct ble_hs_conn *conn,
                               struct ble_l2cap_chan *chan,
                               struct os_mbuf **rxom)
{
    struct ble_att_find_type_value_req req;
    uint16_t err_handle;
    uint8_t att_err;
    int rc;

    *rxom = os_mbuf_pullup(*rxom, BLE_ATT_MTU_CMD_SZ);
    if (*rxom == NULL) {
        att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        err_handle = 0;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = ble_att_find_type_value_req_parse((*rxom)->om_data, (*rxom)->om_len,
                                           &req);
    assert(rc == 0);

    /* Tx error response if start handle is greater than end handle or is equal
     * to 0 (Vol. 3, Part F, 3.4.3.3).
     */
    if (req.bavq_start_handle > req.bavq_end_handle ||
        req.bavq_start_handle == 0) {

        att_err = BLE_ATT_ERR_INVALID_HANDLE;
        err_handle = req.bavq_start_handle;
        rc = BLE_HS_EBADDATA;
        goto err;
    }

    rc = ble_att_svr_tx_find_type_value(conn, chan, &req, *rxom, &att_err);
    if (rc != 0) {
        err_handle = req.bavq_start_handle;
        goto err;
    }

    return 0;

err:
    ble_att_svr_tx_error_rsp(conn, chan, BLE_ATT_OP_FIND_TYPE_VALUE_REQ,
                             err_handle, att_err);
    return rc;
}

static int
ble_att_svr_tx_read_type_rsp(struct ble_hs_conn *conn,
                             struct ble_l2cap_chan *chan,
                             struct ble_att_read_type_req *req,
                             uint8_t *uuid128, uint8_t *att_err,
                             uint16_t *err_handle)
{
    struct ble_att_read_type_rsp rsp;
    union ble_att_svr_access_ctxt arg;
    struct ble_att_svr_entry *entry;
    struct os_mbuf *txom;
    uint8_t *dptr;
    int txomlen;
    int prev_attr_len;
    int attr_len;
    int rc;

    *att_err = 0;    /* Silence unnecessary warning. */
    *err_handle = 0; /* Silence unnecessary warning. */

    prev_attr_len = 0;

    txom = ble_att_get_pkthdr();
    if (txom == NULL) {
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        *err_handle = 0;
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    /* Allocate space for the respose base, but don't fill in the fields.  They
     * get filled in at the end, when we know the value of the length field.
     */
    dptr = os_mbuf_extend(txom, BLE_ATT_READ_TYPE_RSP_BASE_SZ);
    if (dptr == NULL) {
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        *err_handle = 0;
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    /* Find all matching attributes, writing a record for each. */
    entry = NULL;
    while (1) {
        rc = ble_att_svr_find_by_uuid(uuid128, &entry);
        if (rc == BLE_HS_ENOENT) {
            break;
        } else if (rc != 0) {
            *att_err = BLE_ATT_ERR_UNLIKELY;
            *err_handle = 0;
            goto done;
        }

        if (entry->ha_handle_id > req->batq_end_handle) {
            break;
        }

        if (entry->ha_handle_id >= req->batq_start_handle &&
            entry->ha_handle_id <= req->batq_end_handle) {

            rc = entry->ha_cb(entry->ha_handle_id, entry->ha_uuid,
                              BLE_ATT_ACCESS_OP_READ, &arg, entry->ha_cb_arg);
            if (rc != 0) {
                *att_err = BLE_ATT_ERR_UNLIKELY;
                *err_handle = entry->ha_handle_id;
                goto done;
            }

            if (arg.ahc_read.attr_len > ble_l2cap_chan_mtu(chan) - 4) {
                attr_len = ble_l2cap_chan_mtu(chan) - 4;
            } else {
                attr_len = arg.ahc_read.attr_len;
            }

            if (prev_attr_len == 0) {
                prev_attr_len = attr_len;
            } else if (prev_attr_len != attr_len) {
                break;
            }

            txomlen = OS_MBUF_PKTHDR(txom)->omp_len + 2 + attr_len;
            if (txomlen > ble_l2cap_chan_mtu(chan)) {
                break;
            }

            dptr = os_mbuf_extend(txom, 2 + attr_len);
            if (dptr == NULL) {
                *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
                *err_handle = entry->ha_handle_id;
                rc = BLE_HS_ENOMEM;
                goto done;
            }

            htole16(dptr + 0, entry->ha_handle_id);
            memcpy(dptr + 2, arg.ahc_read.attr_data, attr_len);
        }
    }

done:
    if (OS_MBUF_PKTLEN(txom) == 0) {
        /* No matching attributes. */
        if (*att_err == 0) {
            *att_err = BLE_ATT_ERR_ATTR_NOT_FOUND;
        }
        rc = BLE_HS_ENOENT;
    } else {
        /* Send what we can, even if an error was encountered. */
        *att_err = 0;

        /* Fill the response base. */
        rsp.batp_length = BLE_ATT_READ_TYPE_ADATA_BASE_SZ + prev_attr_len;
        rc = ble_att_read_type_rsp_write(txom->om_data, txom->om_len, &rsp);
        assert(rc == 0);

        /* Transmit the response. */
        rc = ble_l2cap_tx(conn, chan, txom);
        txom = NULL;
        if (rc != 0) {
            *att_err = BLE_ATT_ERR_UNLIKELY;
            *err_handle = entry->ha_handle_id;
        }
    }

    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_att_svr_rx_read_type(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                         struct os_mbuf **rxom)
{
    struct ble_att_read_type_req req;
    uint16_t err_handle;
    uint16_t uuid16;
    uint8_t uuid128[16];
    uint8_t att_err;
    int rc;

    *rxom = os_mbuf_pullup(*rxom, OS_MBUF_PKTLEN(*rxom));
    if (*rxom == NULL) {
        att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        err_handle = 0;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = ble_att_read_type_req_parse((*rxom)->om_data, (*rxom)->om_len, &req);
    assert(rc == 0);

    switch ((*rxom)->om_len) {
    case BLE_ATT_READ_TYPE_REQ_SZ_16:
        uuid16 = le16toh((*rxom)->om_data + 5);
        rc = ble_hs_uuid_from_16bit(uuid16, uuid128);
        if (rc != 0) {
            att_err = BLE_ATT_ERR_ATTR_NOT_FOUND;
            err_handle = 0;
            rc = BLE_HS_EBADDATA;
            goto err;
        }
        break;

    case BLE_ATT_READ_TYPE_REQ_SZ_128:
        memcpy(uuid128, (*rxom)->om_data + 5, 16);
        break;

    default:
        att_err = BLE_ATT_ERR_INVALID_PDU;
        err_handle = 0;
        rc = BLE_HS_EMSGSIZE;
        goto err;
    }

    rc = ble_att_svr_tx_read_type_rsp(conn, chan, &req, uuid128, &att_err,
                                      &err_handle);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    ble_att_svr_tx_error_rsp(conn, chan, BLE_ATT_OP_READ_TYPE_REQ,
                             err_handle, att_err);
    return rc;
}

/**
 * @return                      0 on success; ATT error code on failure.
 */
static int
ble_att_svr_tx_read_rsp(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                        void *attr_data, int attr_len, uint8_t *att_err)
{
    struct os_mbuf *txom;
    uint16_t data_len;
    uint8_t op;
    int rc;

    txom = ble_att_get_pkthdr();
    if (txom == NULL) {
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    op = BLE_ATT_OP_READ_RSP;
    rc = os_mbuf_append(txom, &op, 1);
    if (rc != 0) {
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        rc = BLE_HS_ENOMEM;
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
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = ble_l2cap_tx(conn, chan, txom);
    if (rc != 0) {
        *att_err = BLE_ATT_ERR_UNLIKELY;
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_att_svr_rx_read(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                    struct os_mbuf **rxom)
{
    union ble_att_svr_access_ctxt arg;
    struct ble_att_svr_entry *entry;
    struct ble_att_read_req req;
    uint16_t err_handle;
    uint8_t att_err;
    int rc;

    *rxom = os_mbuf_pullup(*rxom, OS_MBUF_PKTLEN(*rxom));
    if (*rxom == NULL) {
        att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        err_handle = 0;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = ble_att_read_req_parse((*rxom)->om_data, (*rxom)->om_len, &req);
    if (rc != 0) {
        att_err = BLE_ATT_ERR_INVALID_PDU;
        err_handle = 0;
        rc = BLE_HS_EBADDATA;
        goto err;
    }

    entry = NULL;
    rc = ble_att_svr_find_by_handle(req.barq_handle, &entry);
    if (rc != 0) {
        att_err = BLE_ATT_ERR_INVALID_HANDLE;
        err_handle = req.barq_handle;
        rc = BLE_HS_ENOENT;
        goto err;
    }

    if (entry->ha_cb == NULL) {
        att_err = BLE_ATT_ERR_UNLIKELY;
        err_handle = req.barq_handle;
        rc = BLE_HS_ENOTSUP;
        goto err;
    }

    rc = entry->ha_cb(entry->ha_handle_id, entry->ha_uuid,
                      BLE_ATT_ACCESS_OP_READ, &arg, entry->ha_cb_arg);
    if (rc != 0) {
        att_err = BLE_ATT_ERR_UNLIKELY;
        err_handle = req.barq_handle;
        rc = BLE_HS_ENOTSUP;
        goto err;
    }

    rc = ble_att_svr_tx_read_rsp(conn, chan, arg.ahc_read.attr_data,
                                 arg.ahc_read.attr_len, &att_err);
    if (rc != 0) {
        err_handle = req.barq_handle;
        goto err;
    }

    return 0;

err:
    ble_att_svr_tx_error_rsp(conn, chan, BLE_ATT_OP_READ_REQ,
                             err_handle, att_err);
    return rc;
}

static int
ble_att_svr_is_valid_group_type(uint8_t *uuid128)
{
    uint16_t uuid16;

    uuid16 = ble_hs_uuid_16bit(uuid128);

    return uuid16 == BLE_ATT_UUID_PRIMARY_SERVICE ||
           uuid16 == BLE_ATT_UUID_SECONDARY_SERVICE;
}

static int
ble_att_svr_service_uuid(struct ble_att_svr_entry *entry, uint16_t *uuid16,
                         uint8_t *uuid128)
{
    union ble_att_svr_access_ctxt arg;
    int rc;

    rc = entry->ha_cb(entry->ha_handle_id, entry->ha_uuid,
                      BLE_ATT_ACCESS_OP_READ, &arg, entry->ha_cb_arg);
    if (rc != 0) {
        return rc;
    }

    switch (arg.ahc_read.attr_len) {
    case 16:
        *uuid16 = 0;
        memcpy(uuid128, arg.ahc_read.attr_data, 16);
        return 0;

    case 2:
        *uuid16 = le16toh(arg.ahc_read.attr_data);
        if (*uuid16 == 0) {
            return BLE_HS_EINVAL;
        }
        return 0;

    default:
        return BLE_HS_EINVAL;
    }
}

static int
ble_att_svr_read_group_type_entry_write(struct os_mbuf *om, uint16_t mtu,
                                        uint16_t start_group_handle,
                                        uint16_t end_group_handle,
                                        uint16_t service_uuid16,
                                        uint8_t *service_uuid128)
{
    uint8_t *buf;
    int len;

    if (service_uuid16 != 0) {
        len = BLE_ATT_READ_GROUP_TYPE_ADATA_SZ_16;
    } else {
        len = BLE_ATT_READ_GROUP_TYPE_ADATA_SZ_128;
    }
    if (OS_MBUF_PKTLEN(om) + len > mtu) {
        return BLE_HS_EMSGSIZE;
    }

    buf = os_mbuf_extend(om, len);
    if (buf == NULL) {
        return BLE_HS_ENOMEM;
    }

    htole16(buf + 0, start_group_handle);
    htole16(buf + 2, end_group_handle);
    if (service_uuid16 != 0) {
        htole16(buf + 4, service_uuid16);
    } else {
        memcpy(buf + 4, service_uuid128, 16);
    }

    return 0;
}

/**
 * @return                      0 on success; BLE_HS error code on failure.
 */
static int
ble_att_svr_tx_read_group_type(struct ble_hs_conn *conn,
                               struct ble_l2cap_chan *chan,
                               struct ble_att_read_group_type_req *req,
                               uint8_t *group_uuid128, uint8_t *att_err,
                               uint16_t *err_handle)
{
    struct ble_att_read_group_type_rsp rsp;
    struct ble_att_svr_entry *entry;
    struct os_mbuf *txom;
    uint16_t start_group_handle;
    uint16_t end_group_handle;
    uint16_t service_uuid16;
    uint8_t service_uuid128[16];
    void *rsp_buf;
    int rc;

    /* Silence warnings. */
    rsp_buf = NULL;
    service_uuid16 = 0;
    end_group_handle = 0;

    *att_err = 0;
    *err_handle = req->bagq_start_handle;

    txom = ble_att_get_pkthdr();
    if (txom == NULL) {
        rc = BLE_ATT_ERR_INSUFFICIENT_RES;
        goto done;
    }

    /* Reserve space for the response base. */
    rsp_buf = os_mbuf_extend(txom, BLE_ATT_READ_GROUP_TYPE_RSP_BASE_SZ);
    if (rsp_buf == NULL) {
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    start_group_handle = 0;
    rsp.bagp_length = 0;
    STAILQ_FOREACH(entry, &ble_att_svr_list, ha_next) {
        if (entry->ha_handle_id < req->bagq_start_handle) {
            continue;
        }
        if (entry->ha_handle_id > req->bagq_end_handle) {
            /* The full input range has been searched. */
            rc = 0;
            goto done;
        }

        if (start_group_handle != 0) {
            /* We have already found the start of a group. */
            if (!ble_att_svr_is_valid_group_type(entry->ha_uuid)) {
                /* This attribute is part of the current group. */ 
                end_group_handle = entry->ha_handle_id;
            } else {
                /* This attribute marks the end of the group.  Write an entry
                 * representing the group to the response.
                 */
                rc = ble_att_svr_read_group_type_entry_write(
                    txom, ble_l2cap_chan_mtu(chan),
                    start_group_handle, end_group_handle,
                    service_uuid16, service_uuid128);
                start_group_handle = 0;
                end_group_handle = 0;
                if (rc != 0) {
                    *err_handle = entry->ha_handle_id;
                    if (rc == BLE_HS_ENOMEM) {
                        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
                    } else {
                        assert(rc == BLE_HS_EMSGSIZE);
                    }
                    goto done;
                }
            }
        }

        if (start_group_handle == 0) {
            /* We are looking for the start of a group. */
            if (memcmp(entry->ha_uuid, group_uuid128, 16) == 0) {
                /* Found a group start.  Read the group UUID. */
                rc = ble_att_svr_service_uuid(entry, &service_uuid16,
                                              service_uuid128);
                if (rc != 0) {
                    *err_handle = entry->ha_handle_id;
                    *att_err = BLE_ATT_ERR_UNLIKELY;
                    rc = BLE_HS_ENOTSUP;
                    goto done;
                }

                /* Make sure the group UUID lengths are consistent.  If this
                 * group has a different length UUID, then cut the response
                 * short.
                 */
                switch (rsp.bagp_length) {
                case 0:
                    if (service_uuid16 != 0) {
                        rsp.bagp_length = BLE_ATT_READ_GROUP_TYPE_ADATA_SZ_16;
                    } else {
                        rsp.bagp_length = BLE_ATT_READ_GROUP_TYPE_ADATA_SZ_128;
                    }
                    break;

                case BLE_ATT_READ_GROUP_TYPE_ADATA_SZ_16:
                    if (service_uuid16 == 0) {
                        rc = 0;
                        goto done;
                    }
                    break;

                case BLE_ATT_READ_GROUP_TYPE_ADATA_SZ_128:
                    if (service_uuid16 != 0) {
                        rc = 0;
                        goto done;
                    }
                    break;

                default:
                    assert(0);
                    goto done;
                }

                start_group_handle = entry->ha_handle_id;
                end_group_handle = entry->ha_handle_id;
            }
        }
    }

    rc = 0;

done:
    if (rc == 0) {
        if (start_group_handle != 0) {
            /* A group was being processed.  Add its corresponding entry to the
             * response.
             */
            rc = ble_att_svr_read_group_type_entry_write(
                txom, ble_l2cap_chan_mtu(chan),
                start_group_handle, end_group_handle,
                service_uuid16, service_uuid128);
            if (rc == BLE_HS_ENOMEM) {
                *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
            }
        }

        if (OS_MBUF_PKTLEN(txom) <= BLE_ATT_READ_GROUP_TYPE_RSP_BASE_SZ) {
            *att_err = BLE_ATT_ERR_ATTR_NOT_FOUND;
            rc = BLE_HS_ENOENT;
        }
    }

    if (rc == 0 || rc == BLE_HS_EMSGSIZE) {
        rc = ble_att_read_group_type_rsp_write(
            rsp_buf, BLE_ATT_READ_GROUP_TYPE_RSP_BASE_SZ, &rsp);
        assert(rc == 0);

        rc = ble_l2cap_tx(conn, chan, txom);
    } else {
        os_mbuf_free_chain(txom);
    }

    return rc;
}


int
ble_att_svr_rx_read_group_type(struct ble_hs_conn *conn,
                               struct ble_l2cap_chan *chan,
                               struct os_mbuf **rxom)
{
    struct ble_att_read_group_type_req req;
    uint8_t uuid128[16];
    uint16_t err_handle;
    uint8_t att_err;
    int rc;

    *rxom = os_mbuf_pullup(*rxom, OS_MBUF_PKTLEN(*rxom));
    if (*rxom == NULL) {
        att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        err_handle = 0;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = ble_att_read_group_type_req_parse((*rxom)->om_data, (*rxom)->om_len,
                                           &req);
    if (rc != 0) {
        att_err = BLE_ATT_ERR_INVALID_PDU;
        err_handle = 0;
        rc = BLE_HS_EBADDATA;
        goto err;
    }

    if (req.bagq_start_handle > req.bagq_end_handle ||
        req.bagq_start_handle == 0) {

        att_err = BLE_ATT_ERR_INVALID_HANDLE;
        err_handle = req.bagq_start_handle;
        rc = BLE_HS_EBADDATA;
        goto err;
    }

    rc = ble_hs_uuid_extract(*rxom, BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ,
                             uuid128);
    if (rc != 0) {
        att_err = BLE_ATT_ERR_INVALID_PDU;
        err_handle = req.bagq_start_handle;
        rc = BLE_HS_EBADDATA;
        goto err;
    }

    if (!ble_att_svr_is_valid_group_type(uuid128)) {
        att_err = BLE_ATT_ERR_UNSUPPORTED_GROUP;
        err_handle = req.bagq_start_handle;
        rc = BLE_HS_ENOTSUP;
        goto err;
    }

    rc = ble_att_svr_tx_read_group_type(conn, chan, &req, uuid128,
                                        &att_err, &err_handle);
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    ble_att_svr_tx_error_rsp(conn, chan, BLE_ATT_OP_READ_GROUP_TYPE_REQ,
                             err_handle, att_err);
    return rc;
}

static int
ble_att_svr_tx_write_rsp(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                         uint8_t *att_err)
{
    struct os_mbuf *txom;
    uint8_t *dst;
    int rc;

    txom = ble_att_get_pkthdr();
    if (txom == NULL) {
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    dst = os_mbuf_extend(txom, BLE_ATT_WRITE_RSP_SZ);
    if (dst == NULL) {
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    *dst = BLE_ATT_OP_WRITE_RSP;
    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        *att_err = BLE_ATT_ERR_UNLIKELY;
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_att_svr_rx_write(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                     struct os_mbuf **rxom)
{
    union ble_att_svr_access_ctxt arg;
    struct ble_att_svr_entry *entry;
    struct ble_att_write_req req;
    uint16_t err_handle;
    uint8_t att_err;
    int rc;

    *rxom = os_mbuf_pullup(*rxom, BLE_ATT_WRITE_REQ_BASE_SZ);
    if (*rxom == NULL) {
        att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        err_handle = 0;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = ble_att_write_req_parse((*rxom)->om_data, (*rxom)->om_len, &req);
    assert(rc == 0);

    os_mbuf_adj(*rxom, BLE_ATT_WRITE_REQ_BASE_SZ);

    entry = NULL;
    rc = ble_att_svr_find_by_handle(req.bawq_handle, &entry);
    if (rc != 0) {
        att_err = BLE_ATT_ERR_INVALID_HANDLE;
        err_handle = req.bawq_handle;
        rc = BLE_HS_ENOENT;
        goto err;
    }

    if (entry->ha_cb == NULL) {
        att_err = BLE_ATT_ERR_UNLIKELY;
        err_handle = req.bawq_handle;
        rc = BLE_HS_ENOTSUP;
        goto err;
    }

    arg.ahc_write.attr_data = ble_att_svr_flat_buf;
    arg.ahc_write.attr_len = OS_MBUF_PKTLEN(*rxom);
    os_mbuf_copydata(*rxom, 0, arg.ahc_write.attr_len,
                     arg.ahc_write.attr_data);
    att_err = entry->ha_cb(entry->ha_handle_id, entry->ha_uuid,
                           BLE_ATT_ACCESS_OP_WRITE, &arg, entry->ha_cb_arg);
    if (att_err != 0) {
        err_handle = req.bawq_handle;
        rc = BLE_HS_EAPP;
        goto err;
    }

    rc = ble_att_svr_tx_write_rsp(conn, chan, &att_err);
    if (rc != 0) {
        err_handle = req.bawq_handle;
        goto err;
    }

    return 0;

err:
    ble_att_svr_tx_error_rsp(conn, chan, BLE_ATT_OP_WRITE_REQ,
                             err_handle, att_err);
    return rc;
}

static void
ble_att_svr_prep_free(struct ble_att_prep_entry *entry)
{
    os_mbuf_free_chain(entry->bape_value);
    os_memblock_put(&ble_att_svr_prep_entry_pool, entry);
}

static struct ble_att_prep_entry *
ble_att_svr_prep_alloc(void)
{
    struct ble_att_prep_entry *entry;

    entry = os_memblock_get(&ble_att_svr_prep_entry_pool);
    if (entry == NULL) {
        return NULL;
    }

    memset(entry, 0, sizeof *entry);
    entry->bape_value = os_mbuf_get_pkthdr(&ble_att_svr_prep_mbuf_pool, 0);
    if (entry->bape_value == NULL) {
        ble_att_svr_prep_free(entry);
        return NULL;
    }

    return entry;
}

static struct ble_att_prep_entry *
ble_att_svr_prep_find_prev(struct ble_att_svr_conn *basc, uint16_t handle,
                           uint16_t offset)
{
    struct ble_att_prep_entry *entry;
    struct ble_att_prep_entry *prev;

    prev = NULL;
    SLIST_FOREACH(entry, &basc->basc_prep_list, bape_next) {
        if (entry->bape_handle > handle) {
            break;
        }

        if (entry->bape_handle == handle && entry->bape_offset > offset) {
            break;
        }

        prev = entry;
    }

    return prev;
}

void
ble_att_svr_prep_clear(struct ble_att_svr_conn *basc)
{
    struct ble_att_prep_entry *entry;

    while ((entry = SLIST_FIRST(&basc->basc_prep_list)) != NULL) {
        SLIST_REMOVE_HEAD(&basc->basc_prep_list, bape_next);
        ble_att_svr_prep_free(entry);
    }
}

/**
 * @return                      0 on success; ATT error code on failure.
 */
static int
ble_att_svr_prep_validate(struct ble_att_svr_conn *basc, uint16_t *err_handle)
{
    struct ble_att_prep_entry *entry;
    struct ble_att_prep_entry *prev;
    int cur_len;

    prev = NULL;
    SLIST_FOREACH(entry, &basc->basc_prep_list, bape_next) {
        if (prev == NULL || prev->bape_handle != entry->bape_handle) {
            /* Ensure attribute write starts at offset 0. */
            if (entry->bape_offset != 0) {
                *err_handle = entry->bape_handle;
                return BLE_ATT_ERR_INVALID_OFFSET;
            }
        } else {
            /* Ensure entry continues where previous left off. */
            if (prev->bape_offset + OS_MBUF_PKTLEN(prev->bape_value) !=
                entry->bape_offset) {

                *err_handle = entry->bape_handle;
                return BLE_ATT_ERR_INVALID_OFFSET;
            }
        }

        cur_len = entry->bape_offset + OS_MBUF_PKTLEN(entry->bape_value);
        if (cur_len > BLE_ATT_ATTR_MAX_LEN) {
            *err_handle = entry->bape_handle;
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        prev = entry;
    }

    return 0;
}

/**
 * @return                      0 on success; ATT error code on failure.
 */
static int
ble_att_svr_prep_write(struct ble_att_svr_conn *basc, uint16_t *err_handle)
{
    union ble_att_svr_access_ctxt arg;
    struct ble_att_prep_entry *entry;
    struct ble_att_prep_entry *next;
    struct ble_att_svr_entry *attr;
    int buf_off;
    int rc;

    *err_handle = 0; /* Silence unnecessary warning. */

    /* First, validate the contents of the prepare queue. */
    rc = ble_att_svr_prep_validate(basc, err_handle);
    if (rc != 0) {
        return rc;
    }

    /* Contents are valid; perform the writes. */
    buf_off = 0;
    entry = SLIST_FIRST(&basc->basc_prep_list);
    while (entry != NULL) {
        next = SLIST_NEXT(entry, bape_next);

        rc = os_mbuf_copydata(entry->bape_value, 0,
                              OS_MBUF_PKTLEN(entry->bape_value),
                              ble_att_svr_flat_buf + buf_off);
        assert(rc == 0);
        buf_off += OS_MBUF_PKTLEN(entry->bape_value);

        /* If this is the last entry for this attribute, perform the write. */
        if (next == NULL || entry->bape_handle != next->bape_handle) {
            attr = NULL;
            rc = ble_att_svr_find_by_handle(entry->bape_handle, &attr);
            if (rc != 0) {
                *err_handle = entry->bape_handle;
                return BLE_ATT_ERR_INVALID_HANDLE;
            }

            arg.ahc_write.attr_data = ble_att_svr_flat_buf;
            arg.ahc_write.attr_len = buf_off;
            rc = attr->ha_cb(attr->ha_handle_id, attr->ha_uuid,
                             BLE_ATT_ACCESS_OP_WRITE, &arg, attr->ha_cb_arg);
            if (rc != 0) {
                *err_handle = entry->bape_handle;
                return BLE_ATT_ERR_UNLIKELY;
            }

            buf_off = 0;
        }

        entry = next;
    }

    return 0;
}

int
ble_att_svr_rx_prep_write(struct ble_hs_conn *conn,
                          struct ble_l2cap_chan *chan,
                          struct os_mbuf **rxom)
{
    struct ble_att_prep_write_cmd req;
    struct ble_att_prep_entry *prep_entry;
    struct ble_att_prep_entry *prep_prev;
    struct ble_att_svr_entry *attr_entry;
    struct os_mbuf *srcom;
    struct os_mbuf *txom;
    uint16_t err_handle;
    uint8_t att_err;
    int rc;

    /* Initialize some values in case of early error. */
    prep_entry = NULL;

    *rxom = os_mbuf_pullup(*rxom, BLE_ATT_PREP_WRITE_CMD_BASE_SZ);
    if (*rxom == NULL) {
        att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        err_handle = 0;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = ble_att_prep_write_req_parse((*rxom)->om_data, (*rxom)->om_len, &req);
    assert(rc == 0);

    os_mbuf_adj(*rxom, BLE_ATT_PREP_WRITE_CMD_BASE_SZ);

    attr_entry = NULL;
    rc = ble_att_svr_find_by_handle(req.bapc_handle, &attr_entry);
    if (rc != 0) {
        att_err = BLE_ATT_ERR_INVALID_HANDLE;
        err_handle = req.bapc_handle;
        goto err;
    }

    prep_entry = ble_att_svr_prep_alloc();
    if (prep_entry == NULL) {
        att_err = BLE_ATT_ERR_PREPARE_QUEUE_FULL;
        err_handle = req.bapc_handle;
        rc = BLE_HS_ENOMEM;
        goto err;
    }
    prep_entry->bape_handle = req.bapc_handle;
    prep_entry->bape_offset = req.bapc_offset;

    prep_prev = ble_att_svr_prep_find_prev(&conn->bhc_att_svr, req.bapc_handle,
                                           req.bapc_offset);
    if (prep_prev == NULL) {
        SLIST_INSERT_HEAD(&conn->bhc_att_svr.basc_prep_list, prep_entry,
                          bape_next);
    } else {
        SLIST_INSERT_AFTER(prep_prev, prep_entry, bape_next);
    }

    /* Append attribute value from request onto prep mbuf. */
    for (srcom = *rxom; srcom != NULL; srcom = SLIST_NEXT(srcom, om_next)) {
        rc = os_mbuf_append(prep_entry->bape_value, srcom->om_data,
                            srcom->om_len);
        if (rc != 0) {
            att_err = BLE_ATT_ERR_PREPARE_QUEUE_FULL;
            err_handle = req.bapc_handle;
            goto err;
        }
    }

    /* The receive buffer now contains the attribute value.  Repurpose this
     * buffer for the response.  Prepend a response header.
     */
    *rxom = os_mbuf_prepend(*rxom, BLE_ATT_PREP_WRITE_CMD_BASE_SZ);
    if (*rxom == NULL) {
        att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        err_handle = req.bapc_handle;
        rc = BLE_HS_ENOMEM;
        goto err;
    }
    txom = *rxom;

    rc = ble_att_prep_write_rsp_write(txom->om_data,
                                      BLE_ATT_PREP_WRITE_CMD_BASE_SZ, &req);
    assert(rc == 0);

    rc = ble_l2cap_tx(conn, chan, txom);
    if (rc != 0) {
        att_err = BLE_ATT_ERR_UNLIKELY;
        err_handle = req.bapc_handle;
        goto err;
    }

    /* Make sure the receive buffer doesn't get freed since we are using it for
     * the response.
     */
    *rxom = NULL;

    return 0;

err:
    if (prep_entry != NULL) {
        if (prep_prev == NULL) {
            SLIST_REMOVE_HEAD(&conn->bhc_att_svr.basc_prep_list, bape_next);
        } else {
            SLIST_NEXT(prep_prev, bape_next) =
                SLIST_NEXT(prep_entry, bape_next);
        }

        ble_att_svr_prep_free(prep_entry);
    }

    ble_att_svr_tx_error_rsp(conn, chan, BLE_ATT_OP_PREP_WRITE_REQ,
                             err_handle, att_err);
    return rc;
}

/**
 * @return                      0 on success; ATT error code on failure.
 */
static int
ble_att_svr_tx_exec_write_rsp(struct ble_hs_conn *conn,
                              struct ble_l2cap_chan *chan, uint8_t *att_err)
{
    struct os_mbuf *txom;
    uint8_t *dst;
    int rc;

    txom = ble_att_get_pkthdr();
    if (txom == NULL) {
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    dst = os_mbuf_extend(txom, BLE_ATT_EXEC_WRITE_RSP_SZ);
    if (dst == NULL) {
        *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = ble_att_exec_write_rsp_write(dst, BLE_ATT_EXEC_WRITE_RSP_SZ);
    assert(rc == 0);

    rc = ble_l2cap_tx(conn, chan, txom);
    txom = NULL;
    if (rc != 0) {
        *att_err = BLE_ATT_ERR_UNLIKELY;
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_att_svr_rx_exec_write(struct ble_hs_conn *conn,
                          struct ble_l2cap_chan *chan,
                          struct os_mbuf **rxom)
{
    struct ble_att_exec_write_req req;
    uint16_t err_handle;
    uint8_t att_err;
    int rc;

    *rxom = os_mbuf_pullup(*rxom, BLE_ATT_EXEC_WRITE_REQ_SZ);
    if (*rxom == NULL) {
        att_err = BLE_ATT_ERR_INSUFFICIENT_RES;
        err_handle = 0;
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = ble_att_exec_write_req_parse((*rxom)->om_data, (*rxom)->om_len, &req);
    assert(rc == 0);

    if (req.baeq_flags & BLE_ATT_EXEC_WRITE_F_CONFIRM) {
        /* Perform attribute writes. */
        att_err = ble_att_svr_prep_write(&conn->bhc_att_svr, &err_handle);
    } else {
        att_err = 0;
        err_handle = 0;
    }

    /* Erase all prep entries. */
    ble_att_svr_prep_clear(&conn->bhc_att_svr);

    if (att_err != 0) {
        rc = BLE_HS_EAPP;
        goto err;
    }

    /* Send response. */
    rc = ble_att_svr_tx_exec_write_rsp(conn, chan, &att_err);
    if (rc != 0) {
        err_handle = 0;
        goto err;
    }

    return 0;

err:
    ble_att_svr_tx_error_rsp(conn, chan, BLE_ATT_OP_EXEC_WRITE_REQ,
                             err_handle, att_err);
    return rc;
}

static void
ble_att_svr_free_mem(void)
{
    free(ble_att_svr_entry_mem);
    ble_att_svr_entry_mem = NULL;

    free(ble_att_svr_prep_entry_mem);
    ble_att_svr_prep_entry_mem = NULL;

    free(ble_att_svr_prep_mbuf_mem);
    ble_att_svr_prep_mbuf_mem = NULL;
}

int
ble_att_svr_init(void)
{
    int rc;

    ble_att_svr_free_mem();

    STAILQ_INIT(&ble_att_svr_list);

    rc = os_mutex_init(&ble_att_svr_list_mutex);
    if (rc != 0) {
        goto err;
    }

    rc = ble_hs_misc_malloc_mempool(&ble_att_svr_entry_mem,
                                    &ble_att_svr_entry_pool,
                                    BLE_ATT_SVR_NUM_ENTRIES,
                                    sizeof (struct ble_att_svr_entry),
                                    "ble_att_svr_entry_pool");
    if (rc != 0) {
        goto err;
    }

    rc = ble_hs_misc_malloc_mempool(&ble_att_svr_prep_entry_mem,
                                    &ble_att_svr_prep_entry_pool,
                                    BLE_ATT_SVR_NUM_PREP_ENTRIES,
                                    sizeof (struct ble_att_prep_entry),
                                    "ble_att_prep_entry_pool");
    if (rc != 0) {
        goto err;
    }

    rc = ble_hs_misc_malloc_mempool(&ble_att_svr_prep_mbuf_mem,
                                    &ble_att_svr_prep_mbuf_mempool,
                                    BLE_ATT_SVR_NUM_PREP_MBUFS,
                                    BLE_ATT_SVR_PREP_MBUF_MEMBLOCK_SIZE,
                                    "ble_att_prep_mbuf_mempool");
    if (rc != 0) {
        goto err;
    }

    rc = os_mbuf_pool_init(&ble_att_svr_prep_mbuf_pool,
                           &ble_att_svr_prep_mbuf_mempool,
                           BLE_ATT_SVR_PREP_MBUF_MEMBLOCK_SIZE,
                           BLE_ATT_SVR_NUM_PREP_MBUFS);
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    ble_att_svr_id = 0;

    return 0;

err:
    ble_att_svr_free_mem();
    return rc;
}
