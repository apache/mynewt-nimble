/*
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

/* Tests for GATT server authorization (BLE_GAP_EVENT_AUTHORIZE and
 * ble_gatts_authorize_pending_att_req()).
 */

#include <stddef.h>
#include <string.h>
#include "testutil/testutil.h"
#include "nimble/hci_common.h"
#include "host/ble_gatt.h"
#include "ble_hs_test.h"
#include "ble_hs_test_util.h"

#define AUTHOR_TEST_CONN_HANDLE 2
#define AUTHOR_TEST_MAX_EVENTS  8

#define AUTHOR_TEST_F_R         (BLE_ATT_F_READ)
#define AUTHOR_TEST_F_R_AUTHOR  (BLE_ATT_F_READ | BLE_ATT_F_READ_AUTHOR)
#define AUTHOR_TEST_F_W         (BLE_ATT_F_WRITE)
#define AUTHOR_TEST_F_W_AUTHOR  (BLE_ATT_F_WRITE | BLE_ATT_F_WRITE_AUTHOR)

/** Attribute value and access statistics, passed as access callback arg.
 * The UUID is kept here because the ATT server only stores a pointer to it.
 */
struct author_test_attr {
    ble_uuid16_t uuid;
    uint16_t handle;
    uint8_t value[64];
    uint16_t value_len;
    int num_reads;
    int num_writes;
};

/** What the GAP event callback saw and how it answers. */
static struct {
    int rsp;
    int resolve_in_cb;
    int resolve_rc;

    int num_events;
    uint16_t conn_handle;
    uint16_t cid;
    uint8_t access_opcode;
    uint16_t attr_handles[AUTHOR_TEST_MAX_EVENTS];
} author_test_gap;

/** A request injected from within a read access callback, i.e. while a
 * pending request is being resolved.
 */
static struct {
    uint16_t attr_handle; /* 0: nothing to inject */
    int rc;
    int disconnect; /* disconnect the peer instead */
} author_test_inject;

static int
author_test_gap_cb(struct ble_gap_event *event, void *arg)
{
    if (event->type != BLE_GAP_EVENT_AUTHORIZE) {
        return 0;
    }

    TEST_ASSERT_FATAL(author_test_gap.num_events < AUTHOR_TEST_MAX_EVENTS);
    author_test_gap.attr_handles[author_test_gap.num_events] =
        event->authorize.attr_handle;
    author_test_gap.num_events++;
    author_test_gap.conn_handle = event->authorize.conn_handle;
    author_test_gap.cid = event->authorize.cid;
    author_test_gap.access_opcode = event->authorize.access_opcode;

    if (author_test_gap.resolve_in_cb) {
        author_test_gap.resolve_rc = ble_gatts_authorize_pending_att_req(
            event->authorize.conn_handle, event->authorize.cid,
            BLE_GAP_AUTHORIZE_ACCEPT);
    }

    return author_test_gap.rsp;
}

static int
author_test_access_cb(uint16_t conn_handle, uint16_t attr_handle, uint8_t op,
                      uint16_t offset, struct os_mbuf **om, void *arg)
{
    struct author_test_attr *attr = arg;
    uint16_t inject_handle;
    uint16_t len;
    int rc;

    switch (op) {
    case BLE_ATT_ACCESS_OP_READ:
        attr->num_reads++;
        if (author_test_inject.disconnect) {
            author_test_inject.disconnect = 0;
            ble_hs_test_util_conn_disconnect(conn_handle);
        }
        if (author_test_inject.attr_handle != 0) {
            inject_handle = author_test_inject.attr_handle;
            author_test_inject.attr_handle = 0;
            author_test_inject.rc =
                ble_hs_test_util_rx_att_read_req(conn_handle, inject_handle);
        }
        if (offset > attr->value_len) {
            return BLE_ATT_ERR_INVALID_OFFSET;
        }
        rc = os_mbuf_append(*om, attr->value + offset, attr->value_len - offset);
        TEST_ASSERT_FATAL(rc == 0);
        return 0;

    case BLE_ATT_ACCESS_OP_WRITE:
        attr->num_writes++;
        len = OS_MBUF_PKTLEN(*om);
        TEST_ASSERT_FATAL(offset + len <= sizeof attr->value);
        rc = os_mbuf_copydata(*om, 0, len, attr->value + offset);
        TEST_ASSERT_FATAL(rc == 0);
        attr->value_len = offset + len;
        return 0;

    default:
        /* BLE_ATT_ACCESS_OP_WRITE_CMD must never reach access callbacks */
        TEST_ASSERT(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static uint16_t
author_test_init(void)
{
    memset(&author_test_gap, 0, sizeof author_test_gap);
    memset(&author_test_inject, 0, sizeof author_test_inject);
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_ACCEPT;

    ble_hs_test_util_init();
    ble_hs_test_util_create_conn(AUTHOR_TEST_CONN_HANDLE,
                                 ((uint8_t[]){ 2, 3, 4, 5, 6, 7 }),
                                 author_test_gap_cb, NULL);

    return AUTHOR_TEST_CONN_HANDLE;
}

static void
author_test_register(struct author_test_attr *attr, uint16_t uuid16,
                     uint8_t flags, const void *value, uint16_t value_len)
{
    int rc;

    memset(attr, 0, sizeof *attr);
    TEST_ASSERT_FATAL(value_len <= sizeof attr->value);
    memcpy(attr->value, value, value_len);
    attr->value_len = value_len;
    attr->uuid = (ble_uuid16_t)BLE_UUID16_INIT(uuid16);

    rc = ble_att_svr_register(&attr->uuid.u, flags, 0, &attr->handle,
                              author_test_access_cb, attr);
    TEST_ASSERT_FATAL(rc == 0);
}

static void
author_test_encrypt(uint16_t conn_handle)
{
    struct ble_hs_conn *conn;

    ble_hs_lock();
    conn = ble_hs_conn_find(conn_handle);
    TEST_ASSERT_FATAL(conn != NULL);
    conn->bhc_sec_state.encrypted = 1;
    ble_hs_unlock();
}

static void
author_test_verify_no_tx(void)
{
    TEST_ASSERT(ble_hs_test_util_prev_tx_dequeue() == NULL);
}

static void
author_test_verify_event(int num_events, uint16_t attr_handle, uint8_t access_opcode)
{
    TEST_ASSERT_FATAL(author_test_gap.num_events == num_events);
    TEST_ASSERT(author_test_gap.attr_handles[num_events - 1] == attr_handle);
    TEST_ASSERT(author_test_gap.conn_handle == AUTHOR_TEST_CONN_HANDLE);
    TEST_ASSERT(author_test_gap.cid == BLE_L2CAP_CID_ATT);
    TEST_ASSERT(author_test_gap.access_opcode == access_opcode);
}

/** Verifies a response consisting of an opcode followed by raw data. */
static void
author_test_verify_tx_flat(uint8_t op, const void *data, uint16_t data_len)
{
    struct os_mbuf *om;
    uint8_t u8;
    int rc;

    om = ble_hs_test_util_prev_tx_dequeue();
    TEST_ASSERT_FATAL(om != NULL);
    TEST_ASSERT_FATAL(OS_MBUF_PKTLEN(om) == 1 + data_len);

    rc = os_mbuf_copydata(om, 0, 1, &u8);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(u8 == op);
    if (data_len > 0) {
        TEST_ASSERT(os_mbuf_cmpf(om, 1, data, data_len) == 0);
    }
}

static void
author_test_verify_tx_prep_write_rsp(uint16_t attr_handle, uint16_t offset,
                                     const void *data, uint16_t data_len)
{
    uint8_t buf[BLE_ATT_PREP_WRITE_CMD_BASE_SZ - 1 + 64];

    TEST_ASSERT_FATAL(data_len <= 64);

    put_le16(buf, attr_handle);
    put_le16(buf + 2, offset);
    memcpy(buf + 4, data, data_len);

    author_test_verify_tx_flat(BLE_ATT_OP_PREP_WRITE_RSP, buf, 4 + data_len);
}

/** Verifies a Read By Type Response; all attributes have the same length. */
static void
author_test_verify_tx_read_type_rsp(struct author_test_attr **attrs, int num_attrs)
{
    uint8_t buf[256];
    int off;
    int i;

    buf[0] = 2 + attrs[0]->value_len;
    off = 1;
    for (i = 0; i < num_attrs; i++) {
        TEST_ASSERT_FATAL(attrs[i]->value_len == attrs[0]->value_len);
        put_le16(buf + off, attrs[i]->handle);
        memcpy(buf + off + 2, attrs[i]->value, attrs[i]->value_len);
        off += 2 + attrs[i]->value_len;
    }

    author_test_verify_tx_flat(BLE_ATT_OP_READ_TYPE_RSP, buf, off);
}

static int
author_test_rx_read_mult_var_req(uint16_t conn_handle, const uint16_t *handles,
                                 int num_handles)
{
    uint8_t buf[1 + 2 * 8];
    int i;

    TEST_ASSERT_FATAL(num_handles <= 8);

    buf[0] = BLE_ATT_OP_READ_MULT_VAR_REQ;
    for (i = 0; i < num_handles; i++) {
        put_le16(buf + 1 + 2 * i, handles[i]);
    }

    return ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                  buf, 1 + 2 * num_handles);
}

static void
author_test_assert_mbufs_freed(void)
{
    ble_hs_test_util_assert_mbufs_freed(NULL);
}

/*****************************************************************************
 * $read                                                                     *
 *****************************************************************************/

TEST_CASE_SELF(ble_att_svr_author_test_read_no_author)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_R, "abc", 3);

    /* No authorization flag: no event, regular response. */
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(author_test_gap.num_events == 0);
    ble_hs_test_util_verify_tx_read_rsp(attr.value, attr.value_len);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_read_accept)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_R_AUTHOR, "abc", 3);

    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(1, attr.handle, BLE_ATT_ACCESS_OP_READ);
    TEST_ASSERT(attr.num_reads == 1);
    ble_hs_test_util_verify_tx_read_rsp(attr.value, attr.value_len);

    /* Answered immediately, so the request must not stay pending: a
     * resolve call finds nothing.
     */
    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == BLE_HS_ENOENT);
    author_test_verify_no_tx();

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_read_reject)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_R_AUTHOR, "abc", 3);

    /* Explicit reject. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_REJECT;
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHOR));
    author_test_verify_event(1, attr.handle, BLE_ATT_ACCESS_OP_READ);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_REQ, attr.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);

    /* Unknown answer is treated as reject. */
    author_test_gap.rsp = 42;
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHOR));
    author_test_verify_event(2, attr.handle, BLE_ATT_ACCESS_OP_READ);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_REQ, attr.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);

    /* The value was never read. */
    TEST_ASSERT(attr.num_reads == 0);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_read_pending)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_R_AUTHOR, "abc", 3);

    /*** Pending, then accepted. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    author_test_verify_event(1, attr.handle, BLE_ATT_ACCESS_OP_READ);
    author_test_verify_no_tx();
    TEST_ASSERT(attr.num_reads == 0);

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(attr.num_reads == 1);
    ble_hs_test_util_verify_tx_read_rsp(attr.value, attr.value_len);

    /* The request is finished: resolving it again finds nothing. */
    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == BLE_HS_ENOENT);
    author_test_verify_no_tx();

    /*** Pending, then rejected. */
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    author_test_verify_event(2, attr.handle, BLE_ATT_ACCESS_OP_READ);

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_REJECT);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(attr.num_reads == 1);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_REQ, attr.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);

    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == BLE_HS_EPENDING);

    /* Unknown answer is treated as reject. */
    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT, 42);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_REQ, attr.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_resolve_in_cb)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_R_AUTHOR, "abc", 3);

    /* The request is stored before the event, so the application can
     * resolve it from within the callback.
     */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    author_test_gap.resolve_in_cb = 1;
    author_test_gap.resolve_rc = -1;

    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    TEST_ASSERT(author_test_gap.resolve_rc == 0);
    author_test_verify_event(1, attr.handle, BLE_ATT_ACCESS_OP_READ);

    /* Exactly one response. */
    ble_hs_test_util_verify_tx_read_rsp(attr.value, attr.value_len);
    author_test_verify_no_tx();

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_authorize_no_pending)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_R_AUTHOR, "abc", 3);

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == BLE_HS_ENOENT);

    /* Pending request is only resolved on its own connection and bearer. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == BLE_HS_EPENDING);

    rc = ble_gatts_authorize_pending_att_req(conn_handle + 1, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == BLE_HS_ENOENT);
    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_SIG,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == BLE_HS_ENOENT);
    author_test_verify_no_tx();

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_verify_tx_read_rsp(attr.value, attr.value_len);

    author_test_assert_mbufs_freed();
}

/*****************************************************************************
 * $security                                                                 *
 *****************************************************************************/

TEST_CASE_SELF(ble_att_svr_author_test_security_first)
{
    struct author_test_attr attr_enc;
    struct author_test_attr attr_no_r;
    struct author_test_attr attr_no_w;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr_enc, 0x7701,
                         AUTHOR_TEST_F_R_AUTHOR | BLE_ATT_F_READ_ENC, "abc", 3);
    author_test_register(&attr_no_r, 0x7702,
                         BLE_ATT_F_READ_AUTHOR | AUTHOR_TEST_F_W, "abc", 3);
    author_test_register(&attr_no_w, 0x7703,
                         BLE_ATT_F_WRITE_AUTHOR | AUTHOR_TEST_F_R, "abc", 3);

    /* Security fails first: the application is not asked. */
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr_enc.handle);
    TEST_ASSERT(rc == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN));
    TEST_ASSERT(author_test_gap.num_events == 0);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_REQ, attr_enc.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHEN);

    /* Not permitted: the application is not asked. */
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr_no_r.handle);
    TEST_ASSERT(rc != 0);
    TEST_ASSERT(author_test_gap.num_events == 0);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_REQ, attr_no_r.handle,
                                       BLE_ATT_ERR_READ_NOT_PERMITTED);

    rc = ble_hs_test_util_rx_att_write_req(conn_handle, attr_no_w.handle, "x", 1);
    TEST_ASSERT(rc != 0);
    TEST_ASSERT(author_test_gap.num_events == 0);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_WRITE_REQ, attr_no_w.handle,
                                       BLE_ATT_ERR_WRITE_NOT_PERMITTED);

    /* Once security is satisfied, authorization is asked for. */
    author_test_encrypt(conn_handle);
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr_enc.handle);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(1, attr_enc.handle, BLE_ATT_ACCESS_OP_READ);
    ble_hs_test_util_verify_tx_read_rsp(attr_enc.value, attr_enc.value_len);

    TEST_ASSERT(attr_no_r.num_reads == 0);
    TEST_ASSERT(attr_no_w.num_writes == 0);

    author_test_assert_mbufs_freed();
}

/*****************************************************************************
 * $write                                                                    *
 *****************************************************************************/

TEST_CASE_SELF(ble_att_svr_author_test_write)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_W_AUTHOR, "abc", 3);

    /*** Accept. */
    rc = ble_hs_test_util_rx_att_write_req(conn_handle, attr.handle, "de", 2);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(1, attr.handle, BLE_ATT_ACCESS_OP_WRITE);
    ble_hs_test_util_verify_tx_write_rsp();
    TEST_ASSERT(attr.num_writes == 1);
    TEST_ASSERT(attr.value_len == 2 && memcmp(attr.value, "de", 2) == 0);

    /*** Reject. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_REJECT;
    rc = ble_hs_test_util_rx_att_write_req(conn_handle, attr.handle, "fg", 2);
    TEST_ASSERT(rc == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHOR));
    author_test_verify_event(2, attr.handle, BLE_ATT_ACCESS_OP_WRITE);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_WRITE_REQ, attr.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);
    TEST_ASSERT(attr.num_writes == 1);

    /*** Pending, then accepted. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = ble_hs_test_util_rx_att_write_req(conn_handle, attr.handle, "hij", 3);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    author_test_verify_event(3, attr.handle, BLE_ATT_ACCESS_OP_WRITE);
    author_test_verify_no_tx();
    TEST_ASSERT(attr.num_writes == 1);

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_verify_tx_write_rsp();
    TEST_ASSERT(attr.num_writes == 2);
    TEST_ASSERT(attr.value_len == 3 && memcmp(attr.value, "hij", 3) == 0);

    /*** Pending, then rejected. */
    rc = ble_hs_test_util_rx_att_write_req(conn_handle, attr.handle, "kl", 2);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_REJECT);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_WRITE_REQ, attr.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);
    TEST_ASSERT(attr.num_writes == 2);
    TEST_ASSERT(attr.value_len == 3 && memcmp(attr.value, "hij", 3) == 0);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_write_cmd)
{
    struct author_test_attr attr;
    struct author_test_attr attr_plain;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_W_AUTHOR, "abc", 3);
    author_test_register(&attr_plain, 0x7702, AUTHOR_TEST_F_W, "abc", 3);

    /*** No authorization flag: written, no event. */
    rc = ble_hs_test_util_rx_att_write_cmd(conn_handle, attr_plain.handle, "de", 2);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(author_test_gap.num_events == 0);
    TEST_ASSERT(attr_plain.num_writes == 1);

    /*** Accept: written; the event tells a command from a request. */
    rc = ble_hs_test_util_rx_att_write_cmd(conn_handle, attr.handle, "de", 2);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(1, attr.handle, BLE_ATT_ACCESS_OP_WRITE_CMD);
    TEST_ASSERT(attr.num_writes == 1);
    TEST_ASSERT(attr.value_len == 2 && memcmp(attr.value, "de", 2) == 0);

    /*** Reject: dropped. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_REJECT;
    rc = ble_hs_test_util_rx_att_write_cmd(conn_handle, attr.handle, "fg", 2);
    TEST_ASSERT(rc != 0);
    author_test_verify_event(2, attr.handle, BLE_ATT_ACCESS_OP_WRITE_CMD);
    TEST_ASSERT(attr.num_writes == 1);

    /*** Pending: dropped, nothing kept for later. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = ble_hs_test_util_rx_att_write_cmd(conn_handle, attr.handle, "hi", 2);
    TEST_ASSERT(rc != 0);
    author_test_verify_event(3, attr.handle, BLE_ATT_ACCESS_OP_WRITE_CMD);
    TEST_ASSERT(attr.num_writes == 1);

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == BLE_HS_ENOENT);
    TEST_ASSERT(attr.num_writes == 1);
    TEST_ASSERT(attr.value_len == 2 && memcmp(attr.value, "de", 2) == 0);

    /* A command never gets a response. */
    author_test_verify_no_tx();

    author_test_assert_mbufs_freed();
}

/*****************************************************************************
 * $pending bearer                                                           *
 *****************************************************************************/

TEST_CASE_SELF(ble_att_svr_author_test_busy_bearer)
{
    struct author_test_attr attr;
    struct author_test_attr attr_plain;
    struct author_test_attr attr_cmd;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_R_AUTHOR, "abc", 3);
    author_test_register(&attr_plain, 0x7702,
                         AUTHOR_TEST_F_R | AUTHOR_TEST_F_W, "xyz", 3);
    author_test_register(&attr_cmd, 0x7703, AUTHOR_TEST_F_W_AUTHOR, "abc", 3);

    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == BLE_HS_EPENDING);

    /* Further requests on this bearer are dropped while one is pending. */
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr_plain.handle);
    TEST_ASSERT(rc == BLE_HS_EBUSY);
    rc = ble_hs_test_util_rx_att_write_req(conn_handle, attr_plain.handle, "de", 2);
    TEST_ASSERT(rc == BLE_HS_EBUSY);
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == BLE_HS_EBUSY);
    author_test_verify_no_tx();
    TEST_ASSERT(attr_plain.num_reads == 0);
    TEST_ASSERT(attr_plain.num_writes == 0);
    TEST_ASSERT(author_test_gap.num_events == 1);

    /* Commands are still processed and don't touch the pending request. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_ACCEPT;
    rc = ble_hs_test_util_rx_att_write_cmd(conn_handle, attr_cmd.handle, "de", 2);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(2, attr_cmd.handle, BLE_ATT_ACCESS_OP_WRITE_CMD);
    TEST_ASSERT(attr_cmd.num_writes == 1);
    author_test_verify_no_tx();

    /* The original request is answered. */
    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_verify_tx_read_rsp(attr.value, attr.value_len);

    /* The bearer accepts requests again. */
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr_plain.handle);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_verify_tx_read_rsp(attr_plain.value, attr_plain.value_len);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_busy_while_resolving)
{
    struct author_test_attr attr;
    struct author_test_attr attr_plain;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_R_AUTHOR, "abc", 3);
    author_test_register(&attr_plain, 0x7702, AUTHOR_TEST_F_R, "xyz", 3);

    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == BLE_HS_EPENDING);

    /* A request arriving while the pending one is being resolved (here:
     * from the access callback) is dropped like while it is pending.
     */
    author_test_inject.attr_handle = attr_plain.handle;
    author_test_inject.rc = -1;
    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(author_test_inject.rc == BLE_HS_EBUSY);
    TEST_ASSERT(attr_plain.num_reads == 0);

    /* Only the original request is answered. */
    ble_hs_test_util_verify_tx_read_rsp(attr.value, attr.value_len);
    author_test_verify_no_tx();

    /* The bearer accepts requests again. */
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr_plain.handle);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_verify_tx_read_rsp(attr_plain.value, attr_plain.value_len);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_disconnect_while_resolving)
{
    struct author_test_attr auth;
    struct author_test_attr enc;
    uint16_t handles[2];
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&auth, 0x7701, AUTHOR_TEST_F_R_AUTHOR, "ab", 2);
    author_test_register(&enc, 0x7702, AUTHOR_TEST_F_R | BLE_ATT_F_READ_ENC, "cd", 2);
    author_test_encrypt(conn_handle);
    handles[0] = auth.handle;
    handles[1] = enc.handle;

    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = ble_hs_test_util_rx_att_read_mult_req(conn_handle, handles, 2);
    TEST_ASSERT(rc == BLE_HS_EPENDING);

    /* The peer disconnects while the request is being resolved (here: from
     * the first access callback); the security check of the next attribute
     * then finds no connection. It must not crash.
     */
    author_test_inject.disconnect = 1;
    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(auth.num_reads == 1);
    TEST_ASSERT(enc.num_reads == 0);
    author_test_verify_no_tx();

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_disconnect)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_R_AUTHOR, "abc", 3);

    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == BLE_HS_EPENDING);

    /* Disconnect releases the pending request. */
    ble_hs_test_util_conn_disconnect(conn_handle);

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == BLE_HS_ENOENT);
    author_test_verify_no_tx();
    TEST_ASSERT(attr.num_reads == 0);

    author_test_assert_mbufs_freed();
}

/*****************************************************************************
 * $long read                                                                *
 *****************************************************************************/

static const uint8_t author_test_long_value[40] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
};

TEST_CASE_SELF(ble_att_svr_author_test_long_read)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_R_AUTHOR,
                         author_test_long_value, sizeof author_test_long_value);

    /* Every piece of a long read raises the event; the application answers
     * from its own records.
     */
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(1, attr.handle, BLE_ATT_ACCESS_OP_READ);
    ble_hs_test_util_verify_tx_read_rsp(attr.value, BLE_ATT_MTU_DFLT - 1);

    rc = ble_hs_test_util_rx_att_read_blob_req(conn_handle, attr.handle,
                                               BLE_ATT_MTU_DFLT - 1);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(2, attr.handle, BLE_ATT_ACCESS_OP_READ);
    ble_hs_test_util_verify_tx_read_blob_rsp(
        attr.value + BLE_ATT_MTU_DFLT - 1, attr.value_len - (BLE_ATT_MTU_DFLT - 1));

    rc = ble_hs_test_util_rx_att_read_blob_req(conn_handle, attr.handle, 0);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(3, attr.handle, BLE_ATT_ACCESS_OP_READ);
    ble_hs_test_util_verify_tx_read_blob_rsp(attr.value, BLE_ATT_MTU_DFLT - 1);

    rc = ble_hs_test_util_rx_att_read_blob_req(conn_handle, attr.handle, 30);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(4, attr.handle, BLE_ATT_ACCESS_OP_READ);
    ble_hs_test_util_verify_tx_read_blob_rsp(attr.value + 30, attr.value_len - 30);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_long_read_refused)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_R_AUTHOR,
                         author_test_long_value, sizeof author_test_long_value);

    /* A refused read can't be continued without asking again. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_REJECT;
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc != 0);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_REQ, attr.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);

    rc = ble_hs_test_util_rx_att_read_blob_req(conn_handle, attr.handle, 22);
    TEST_ASSERT(rc != 0);
    author_test_verify_event(2, attr.handle, BLE_ATT_ACCESS_OP_READ);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_BLOB_REQ, attr.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);

    /* Same after an asynchronous reject. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_REJECT);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_REQ, attr.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);

    author_test_gap.rsp = BLE_GAP_AUTHORIZE_REJECT;
    rc = ble_hs_test_util_rx_att_read_blob_req(conn_handle, attr.handle, 22);
    TEST_ASSERT(rc != 0);
    author_test_verify_event(4, attr.handle, BLE_ATT_ACCESS_OP_READ);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_BLOB_REQ, attr.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);

    TEST_ASSERT(attr.num_reads == 0);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_read_blob_pending)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_R_AUTHOR,
                         author_test_long_value, sizeof author_test_long_value);

    /* The offset of a pending Read Blob is kept until it is resolved. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = ble_hs_test_util_rx_att_read_blob_req(conn_handle, attr.handle, 22);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    author_test_verify_event(1, attr.handle, BLE_ATT_ACCESS_OP_READ);
    author_test_verify_no_tx();

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_verify_tx_read_blob_rsp(attr.value + 22, attr.value_len - 22);

    /* The next piece is asked again; the application answers right away. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_ACCEPT;
    rc = ble_hs_test_util_rx_att_read_blob_req(conn_handle, attr.handle, 30);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(2, attr.handle, BLE_ATT_ACCESS_OP_READ);
    ble_hs_test_util_verify_tx_read_blob_rsp(attr.value + 30, attr.value_len - 30);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_app_revokes)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_R_AUTHOR,
                         author_test_long_value, sizeof author_test_long_value);

    /* The application decides on every piece: it can stop a long read it
     * allowed at first.
     */
    rc = ble_hs_test_util_rx_att_read_req(conn_handle, attr.handle);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(1, attr.handle, BLE_ATT_ACCESS_OP_READ);
    ble_hs_test_util_verify_tx_read_rsp(attr.value, BLE_ATT_MTU_DFLT - 1);

    author_test_gap.rsp = BLE_GAP_AUTHORIZE_REJECT;
    rc = ble_hs_test_util_rx_att_read_blob_req(conn_handle, attr.handle,
                                               BLE_ATT_MTU_DFLT - 1);
    TEST_ASSERT(rc != 0);
    author_test_verify_event(2, attr.handle, BLE_ATT_ACCESS_OP_READ);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_BLOB_REQ, attr.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);
    TEST_ASSERT(attr.num_reads == 1);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_accept_op_fails)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_R_AUTHOR,
                         author_test_long_value, sizeof author_test_long_value);

    /* Accepted, but the read itself fails (offset beyond the value): the
     * request is still answered, with an error response.
     */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = ble_hs_test_util_rx_att_read_blob_req(conn_handle, attr.handle, 50);
    TEST_ASSERT(rc == BLE_HS_EPENDING);

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(attr.num_reads == 1);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_BLOB_REQ, attr.handle,
                                       BLE_ATT_ERR_INVALID_OFFSET);
    author_test_verify_no_tx();

    author_test_assert_mbufs_freed();
}

/*****************************************************************************
 * $long write                                                               *
 *****************************************************************************/

TEST_CASE_SELF(ble_att_svr_author_test_long_write)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_W_AUTHOR, "", 0);

    /* Every Prepare Write raises the event. */
    rc = ble_hs_test_util_rx_att_prep_write_req(conn_handle, attr.handle, 0,
                                                author_test_long_value, 18);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(1, attr.handle, BLE_ATT_ACCESS_OP_WRITE);
    author_test_verify_tx_prep_write_rsp(attr.handle, 0, author_test_long_value, 18);

    rc = ble_hs_test_util_rx_att_prep_write_req(conn_handle, attr.handle, 18,
                                                author_test_long_value + 18, 18);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(2, attr.handle, BLE_ATT_ACCESS_OP_WRITE);
    rc = ble_hs_test_util_rx_att_prep_write_req(conn_handle, attr.handle, 36,
                                                author_test_long_value + 36, 4);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(3, attr.handle, BLE_ATT_ACCESS_OP_WRITE);
    author_test_verify_tx_prep_write_rsp(attr.handle, 18,
                                         author_test_long_value + 18, 18);
    author_test_verify_tx_prep_write_rsp(attr.handle, 36,
                                         author_test_long_value + 36, 4);
    TEST_ASSERT(attr.num_writes == 0);

    rc = ble_hs_test_util_rx_att_exec_write_req(conn_handle,
                                                BLE_ATT_EXEC_WRITE_F_EXECUTE);
    TEST_ASSERT(rc == 0);
    author_test_verify_tx_flat(BLE_ATT_OP_EXEC_WRITE_RSP, NULL, 0);
    TEST_ASSERT(attr.num_writes == 1);
    TEST_ASSERT(attr.value_len == sizeof author_test_long_value);
    TEST_ASSERT(memcmp(attr.value, author_test_long_value,
                       sizeof author_test_long_value) == 0);

    /* A cancelled long write writes nothing. */
    rc = ble_hs_test_util_rx_att_prep_write_req(conn_handle, attr.handle, 0, "ab", 2);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(4, attr.handle, BLE_ATT_ACCESS_OP_WRITE);
    author_test_verify_tx_prep_write_rsp(attr.handle, 0, "ab", 2);
    rc = ble_hs_test_util_rx_att_exec_write_req(conn_handle,
                                                BLE_ATT_EXEC_WRITE_F_CANCEL);
    TEST_ASSERT(rc == 0);
    author_test_verify_tx_flat(BLE_ATT_OP_EXEC_WRITE_RSP, NULL, 0);
    TEST_ASSERT(attr.num_writes == 1);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_long_write_refused)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_W_AUTHOR, "", 0);

    /* Refused first piece: nothing is queued. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_REJECT;
    rc = ble_hs_test_util_rx_att_prep_write_req(conn_handle, attr.handle, 0,
                                                author_test_long_value, 18);
    TEST_ASSERT(rc != 0);
    author_test_verify_event(1, attr.handle, BLE_ATT_ACCESS_OP_WRITE);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_PREP_WRITE_REQ, attr.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);

    /* The following piece is asked again. */
    rc = ble_hs_test_util_rx_att_prep_write_req(conn_handle, attr.handle, 18,
                                                author_test_long_value + 18, 18);
    TEST_ASSERT(rc != 0);
    author_test_verify_event(2, attr.handle, BLE_ATT_ACCESS_OP_WRITE);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_PREP_WRITE_REQ, attr.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);

    rc = ble_hs_test_util_rx_att_exec_write_req(conn_handle,
                                                BLE_ATT_EXEC_WRITE_F_EXECUTE);
    TEST_ASSERT(rc == 0);
    author_test_verify_tx_flat(BLE_ATT_OP_EXEC_WRITE_RSP, NULL, 0);
    TEST_ASSERT(attr.num_writes == 0);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_long_write_pending)
{
    struct author_test_attr attr;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&attr, 0x7701, AUTHOR_TEST_F_W_AUTHOR, "", 0);

    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = ble_hs_test_util_rx_att_prep_write_req(conn_handle, attr.handle, 0,
                                                author_test_long_value, 18);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    author_test_verify_event(1, attr.handle, BLE_ATT_ACCESS_OP_WRITE);
    author_test_verify_no_tx();

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == 0);
    author_test_verify_tx_prep_write_rsp(attr.handle, 0, author_test_long_value, 18);

    /* Following pieces are asked again; the application answers right
     * away.
     */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_ACCEPT;
    rc = ble_hs_test_util_rx_att_prep_write_req(conn_handle, attr.handle, 18,
                                                author_test_long_value + 18, 18);
    TEST_ASSERT(rc == 0);
    rc = ble_hs_test_util_rx_att_prep_write_req(conn_handle, attr.handle, 36,
                                                author_test_long_value + 36, 4);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(3, attr.handle, BLE_ATT_ACCESS_OP_WRITE);
    author_test_verify_tx_prep_write_rsp(attr.handle, 18,
                                         author_test_long_value + 18, 18);
    author_test_verify_tx_prep_write_rsp(attr.handle, 36,
                                         author_test_long_value + 36, 4);

    rc = ble_hs_test_util_rx_att_exec_write_req(conn_handle,
                                                BLE_ATT_EXEC_WRITE_F_EXECUTE);
    TEST_ASSERT(rc == 0);
    author_test_verify_tx_flat(BLE_ATT_OP_EXEC_WRITE_RSP, NULL, 0);
    TEST_ASSERT(attr.num_writes == 1);
    TEST_ASSERT(attr.value_len == sizeof author_test_long_value);
    TEST_ASSERT(memcmp(attr.value, author_test_long_value,
                       sizeof author_test_long_value) == 0);

    author_test_assert_mbufs_freed();
}

/*****************************************************************************
 * $read multiple                                                            *
 *****************************************************************************/

TEST_CASE_SELF(ble_att_svr_author_test_read_mult)
{
    struct author_test_attr plain;
    struct author_test_attr auth1;
    struct author_test_attr auth2;
    uint16_t handles[3];
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&plain, 0x7701, AUTHOR_TEST_F_R, "ab", 2);
    author_test_register(&auth1, 0x7702, AUTHOR_TEST_F_R_AUTHOR, "cd", 2);
    author_test_register(&auth2, 0x7703, AUTHOR_TEST_F_R_AUTHOR, "ef", 2);
    handles[0] = plain.handle;
    handles[1] = auth1.handle;
    handles[2] = auth2.handle;

    /*** Accept: one event per protected attribute. */
    rc = ble_hs_test_util_rx_att_read_mult_req(conn_handle, handles, 3);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(author_test_gap.num_events == 2);
    TEST_ASSERT(author_test_gap.attr_handles[0] == auth1.handle);
    TEST_ASSERT(author_test_gap.attr_handles[1] == auth2.handle);
    author_test_verify_tx_flat(BLE_ATT_OP_READ_MULT_RSP, "abcdef", 6);

    /*** Reject: error names the refused attribute; nothing is read. */
    plain.num_reads = 0;
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_REJECT;
    rc = ble_hs_test_util_rx_att_read_mult_req(conn_handle, handles, 3);
    TEST_ASSERT(rc != 0);
    TEST_ASSERT(author_test_gap.num_events == 3);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_MULT_REQ, auth1.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);
    TEST_ASSERT(plain.num_reads == 0);

    /*** Pending on two attributes, one after the other. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = ble_hs_test_util_rx_att_read_mult_req(conn_handle, handles, 3);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    author_test_verify_event(4, auth1.handle, BLE_ATT_ACCESS_OP_READ);
    author_test_verify_no_tx();

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    author_test_verify_event(5, auth2.handle, BLE_ATT_ACCESS_OP_READ);
    author_test_verify_no_tx();

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == 0);
    author_test_verify_tx_flat(BLE_ATT_OP_READ_MULT_RSP, "abcdef", 6);

    /*** Pending, rejected on the second attribute. */
    rc = ble_hs_test_util_rx_att_read_mult_req(conn_handle, handles, 3);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_REJECT);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_MULT_REQ, auth2.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_read_mult_var)
{
    struct author_test_attr plain;
    struct author_test_attr auth;
    uint16_t handles[2];
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&plain, 0x7701, AUTHOR_TEST_F_R, "ab", 2);
    author_test_register(&auth, 0x7702, AUTHOR_TEST_F_R_AUTHOR, "cde", 3);
    handles[0] = plain.handle;
    handles[1] = auth.handle;

    /* Resumed request still answers with a variable length response. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = author_test_rx_read_mult_var_req(conn_handle, handles, 2);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    author_test_verify_event(1, auth.handle, BLE_ATT_ACCESS_OP_READ);
    author_test_verify_no_tx();

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == 0);
    author_test_verify_tx_flat(BLE_ATT_OP_READ_MULT_VAR_RSP,
                               "\x02\x00"
                               "ab"
                               "\x03\x00"
                               "cde",
                               9);

    /* Rejected: error names the refused attribute. */
    rc = author_test_rx_read_mult_var_req(conn_handle, handles, 2);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_REJECT);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_MULT_VAR_REQ, auth.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_read_mult_many)
{
    struct author_test_attr plain;
    struct author_test_attr auth;
    uint8_t req[1 + 2 * 200];
    uint8_t rsp[2 * 200];
    uint16_t conn_handle;
    int rc;
    int i;

    /* More than 127 handles: the position in the handle list does not fit
     * in 8 bits, also while the request is pending.
     */
    conn_handle = author_test_init();
    ble_hs_test_util_set_att_mtu(conn_handle, BLE_ATT_MTU_MAX);
    author_test_register(&plain, 0x7701, AUTHOR_TEST_F_R, "ab", 2);
    author_test_register(&auth, 0x7702, AUTHOR_TEST_F_R_AUTHOR, "cd", 2);

    req[0] = BLE_ATT_OP_READ_MULT_REQ;
    for (i = 0; i < 199; i++) {
        put_le16(req + 1 + 2 * i, plain.handle);
        memcpy(rsp + 2 * i, "ab", 2);
    }
    put_le16(req + 1 + 2 * 199, auth.handle);
    memcpy(rsp + 2 * 199, "cd", 2);

    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                req, sizeof req);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    author_test_verify_event(1, auth.handle, BLE_ATT_ACCESS_OP_READ);

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(author_test_gap.num_events == 1);
    author_test_verify_tx_flat(BLE_ATT_OP_READ_MULT_RSP, rsp, sizeof rsp);

    author_test_assert_mbufs_freed();
}

/*****************************************************************************
 * $read by type                                                             *
 *****************************************************************************/

#define AUTHOR_TEST_TYPE_UUID 0x7710

TEST_CASE_SELF(ble_att_svr_author_test_read_type)
{
    struct author_test_attr a, b, c, d;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&a, AUTHOR_TEST_TYPE_UUID, AUTHOR_TEST_F_R, "aaa", 3);
    author_test_register(&b, AUTHOR_TEST_TYPE_UUID, AUTHOR_TEST_F_R_AUTHOR, "bbb", 3);
    author_test_register(&c, AUTHOR_TEST_TYPE_UUID, AUTHOR_TEST_F_R, "ccc", 3);
    author_test_register(&d, AUTHOR_TEST_TYPE_UUID, AUTHOR_TEST_F_R_AUTHOR, "ddd", 3);

    /* A protected attribute after collected entries ends the response
     * without asking.
     */
    rc = ble_hs_test_util_rx_att_read_type_req16(conn_handle, 1, 0xffff,
                                                 AUTHOR_TEST_TYPE_UUID);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(author_test_gap.num_events == 0);
    author_test_verify_tx_read_type_rsp((struct author_test_attr *[]){ &a }, 1);
    TEST_ASSERT(b.num_reads == 0);

    /* The client continues from it: now it's first and gets asked. */
    rc = ble_hs_test_util_rx_att_read_type_req16(conn_handle, b.handle, 0xffff,
                                                 AUTHOR_TEST_TYPE_UUID);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(1, b.handle, BLE_ATT_ACCESS_OP_READ);
    author_test_verify_tx_read_type_rsp((struct author_test_attr *[]){ &b, &c }, 2);
    TEST_ASSERT(d.num_reads == 0);

    rc = ble_hs_test_util_rx_att_read_type_req16(conn_handle, d.handle, 0xffff,
                                                 AUTHOR_TEST_TYPE_UUID);
    TEST_ASSERT(rc == 0);
    author_test_verify_event(2, d.handle, BLE_ATT_ACCESS_OP_READ);
    author_test_verify_tx_read_type_rsp((struct author_test_attr *[]){ &d }, 1);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_read_type_reject)
{
    struct author_test_attr a, b;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&a, AUTHOR_TEST_TYPE_UUID, AUTHOR_TEST_F_R_AUTHOR, "aaa", 3);
    author_test_register(&b, AUTHOR_TEST_TYPE_UUID, AUTHOR_TEST_F_R, "bbb", 3);

    /* Refused first attribute: error response naming it. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_REJECT;
    rc = ble_hs_test_util_rx_att_read_type_req16(conn_handle, 1, 0xffff,
                                                 AUTHOR_TEST_TYPE_UUID);
    TEST_ASSERT(rc != 0);
    author_test_verify_event(1, a.handle, BLE_ATT_ACCESS_OP_READ);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_TYPE_REQ, a.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);
    TEST_ASSERT(a.num_reads == 0);
    TEST_ASSERT(b.num_reads == 0);

    author_test_assert_mbufs_freed();
}

TEST_CASE_SELF(ble_att_svr_author_test_read_type_pending)
{
    struct author_test_attr a, b;
    uint16_t conn_handle;
    int rc;

    conn_handle = author_test_init();
    author_test_register(&a, AUTHOR_TEST_TYPE_UUID, AUTHOR_TEST_F_R_AUTHOR, "aaa", 3);
    author_test_register(&b, AUTHOR_TEST_TYPE_UUID, AUTHOR_TEST_F_R, "bbb", 3);

    /*** Pending, then rejected: error response naming it. */
    author_test_gap.rsp = BLE_GAP_AUTHORIZE_PENDING;
    rc = ble_hs_test_util_rx_att_read_type_req16(conn_handle, 1, 0xffff,
                                                 AUTHOR_TEST_TYPE_UUID);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    author_test_verify_event(1, a.handle, BLE_ATT_ACCESS_OP_READ);
    author_test_verify_no_tx();

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_REJECT);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_verify_tx_err_rsp(BLE_ATT_OP_READ_TYPE_REQ, a.handle,
                                       BLE_ATT_ERR_INSUFFICIENT_AUTHOR);
    TEST_ASSERT(a.num_reads == 0);

    /*** Pending, then accepted: the request is processed again without
     * asking a second time.
     */
    rc = ble_hs_test_util_rx_att_read_type_req16(conn_handle, 1, 0xffff,
                                                 AUTHOR_TEST_TYPE_UUID);
    TEST_ASSERT(rc == BLE_HS_EPENDING);
    author_test_verify_event(2, a.handle, BLE_ATT_ACCESS_OP_READ);
    author_test_verify_no_tx();
    TEST_ASSERT(a.num_reads == 0);

    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(author_test_gap.num_events == 2);
    author_test_verify_tx_read_type_rsp((struct author_test_attr *[]){ &a, &b }, 2);

    /* The re-run did not leave anything pending. */
    rc = ble_gatts_authorize_pending_att_req(conn_handle, BLE_L2CAP_CID_ATT,
                                             BLE_GAP_AUTHORIZE_ACCEPT);
    TEST_ASSERT(rc == BLE_HS_ENOENT);

    author_test_assert_mbufs_freed();
}

TEST_SUITE(ble_att_svr_author_suite)
{
    ble_att_svr_author_test_read_no_author();
    ble_att_svr_author_test_read_accept();
    ble_att_svr_author_test_read_reject();
    ble_att_svr_author_test_read_pending();
    ble_att_svr_author_test_resolve_in_cb();
    ble_att_svr_author_test_authorize_no_pending();
    ble_att_svr_author_test_security_first();
    ble_att_svr_author_test_write();
    ble_att_svr_author_test_write_cmd();
    ble_att_svr_author_test_busy_bearer();
    ble_att_svr_author_test_busy_while_resolving();
    ble_att_svr_author_test_disconnect_while_resolving();
    ble_att_svr_author_test_disconnect();
    ble_att_svr_author_test_long_read();
    ble_att_svr_author_test_long_read_refused();
    ble_att_svr_author_test_read_blob_pending();
    ble_att_svr_author_test_app_revokes();
    ble_att_svr_author_test_accept_op_fails();
    ble_att_svr_author_test_long_write();
    ble_att_svr_author_test_long_write_refused();
    ble_att_svr_author_test_long_write_pending();
    ble_att_svr_author_test_read_mult();
    ble_att_svr_author_test_read_mult_var();
    ble_att_svr_author_test_read_mult_many();
    ble_att_svr_author_test_read_type();
    ble_att_svr_author_test_read_type_reject();
    ble_att_svr_author_test_read_type_pending();
}
