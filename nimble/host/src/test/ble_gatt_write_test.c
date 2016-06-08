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

#include <string.h>
#include <errno.h>
#include "testutil/testutil.h"
#include "nimble/ble.h"
#include "host/ble_hs_test.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "ble_hs_test_util.h"

#define BLE_GATT_WRITE_TEST_MAX_ATTRS   128

static int ble_gatt_write_test_cb_called;

static uint8_t ble_gatt_write_test_attr_value[BLE_ATT_ATTR_MAX_LEN];
static struct ble_gatt_error ble_gatt_write_test_error;

static struct ble_gatt_attr *
ble_gatt_write_test_attrs[BLE_GATT_WRITE_TEST_MAX_ATTRS];
static int ble_gatt_write_test_num_attrs;

static void
ble_gatt_write_test_init(void)
{
    int i;

    ble_hs_test_util_init();
    ble_gatt_write_test_cb_called = 0;
    ble_gatt_write_test_num_attrs = 0;

    for (i = 0; i < sizeof ble_gatt_write_test_attr_value; i++) {
        ble_gatt_write_test_attr_value[i] = i;
    }
}

static int
ble_gatt_write_test_cb_good(uint16_t conn_handle, struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr, void *arg)
{
    int *attr_len;

    attr_len = arg;

    TEST_ASSERT(conn_handle == 2);
    if (attr_len != NULL) {
        TEST_ASSERT(error == NULL);
        TEST_ASSERT(attr->handle == 100);
    } else {
        TEST_ASSERT(error != NULL);
        ble_gatt_write_test_error = *error;
    }

    ble_gatt_write_test_cb_called = 1;

    return 0;
}

static void
ble_gatt_write_test_rx_rsp(uint16_t conn_handle)
{
    uint8_t op;
    int rc;

    op = BLE_ATT_OP_WRITE_RSP;
    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                &op, 1);
    TEST_ASSERT(rc == 0);
}

static void
ble_gatt_write_test_rx_prep_rsp(uint16_t conn_handle, uint16_t attr_handle,
                                uint16_t offset,
                                void *attr_data, uint16_t attr_data_len)
{
    struct ble_att_prep_write_cmd rsp;
    uint8_t buf[512];
    int rc;

    rsp.bapc_handle = attr_handle;
    rsp.bapc_offset = offset;
    ble_att_prep_write_rsp_write(buf, sizeof buf, &rsp);

    memcpy(buf + BLE_ATT_PREP_WRITE_CMD_BASE_SZ, attr_data, attr_data_len);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(
        conn_handle, BLE_L2CAP_CID_ATT, buf,
        BLE_ATT_PREP_WRITE_CMD_BASE_SZ + attr_data_len);
    TEST_ASSERT(rc == 0);
}

static void
ble_gatt_write_test_rx_exec_rsp(uint16_t conn_handle)
{
    uint8_t op;
    int rc;

    op = BLE_ATT_OP_EXEC_WRITE_RSP;
    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                &op, 1);
    TEST_ASSERT(rc == 0);
}

static void
ble_gatt_write_test_misc_long_good(int attr_len)
{
    int off;
    int len;
    int rc;

    ble_gatt_write_test_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);

    rc = ble_gattc_write_long(2, 100, ble_gatt_write_test_attr_value,
                              attr_len, ble_gatt_write_test_cb_good,
                              &attr_len);
    TEST_ASSERT(rc == 0);

    off = 0;
    while (off < attr_len) {
        /* Send the pending ATT Prep Write Command. */
        ble_hs_test_util_tx_all();

        /* Receive Prep Write response. */
        len = BLE_ATT_MTU_DFLT - BLE_ATT_PREP_WRITE_CMD_BASE_SZ;
        if (off + len > attr_len) {
            len = attr_len - off;
        }
        ble_gatt_write_test_rx_prep_rsp(2, 100, off,
                                        ble_gatt_write_test_attr_value + off,
                                        len);

        /* Verify callback hasn't gotten called. */
        TEST_ASSERT(!ble_gatt_write_test_cb_called);

        off += len;
    }

    /* Receive Exec Write response. */
    ble_hs_test_util_tx_all();
    ble_gatt_write_test_rx_exec_rsp(2);

    /* Verify callback got called. */
    TEST_ASSERT(ble_gatt_write_test_cb_called);
}

typedef void ble_gatt_write_test_long_fail_fn(uint16_t conn_handle,
                                              int off, int len);

static void
ble_gatt_write_test_misc_long_bad(int attr_len,
                                  ble_gatt_write_test_long_fail_fn *cb)
{
    int fail_now;
    int off;
    int len;
    int rc;

    ble_gatt_write_test_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);

    rc = ble_gattc_write_long(2, 100, ble_gatt_write_test_attr_value,
                              attr_len, ble_gatt_write_test_cb_good, NULL);
    TEST_ASSERT(rc == 0);

    fail_now = 0;
    off = 0;
    while (off < attr_len) {
        /* Send the pending ATT Prep Write Command. */
        ble_hs_test_util_tx_all();
        TEST_ASSERT(ble_hs_test_util_prev_tx_dequeue() != NULL);

        /* Receive Prep Write response. */
        len = BLE_ATT_MTU_DFLT - BLE_ATT_PREP_WRITE_CMD_BASE_SZ;
        if (off + len >= attr_len) {
            len = attr_len - off;
            fail_now = 1;
        }
        if (!fail_now) {
            ble_gatt_write_test_rx_prep_rsp(
                2, 100, off, ble_gatt_write_test_attr_value + off, len);
        } else {
            cb(2, off, len);
            break;
        }

        /* Verify callback hasn't gotten called. */
        TEST_ASSERT(!ble_gatt_write_test_cb_called);

        off += len;
    }

    /* Verify callback was called. */
    TEST_ASSERT(ble_gatt_write_test_cb_called);
    TEST_ASSERT(ble_gatt_write_test_error.status == BLE_HS_EBADDATA);
    TEST_ASSERT(ble_gatt_write_test_error.att_handle == 0);
}

static void
ble_gatt_write_test_misc_long_fail_handle(uint16_t conn_handle,
                                          int off, int len)
{
    ble_gatt_write_test_rx_prep_rsp(
        conn_handle, 99, off, ble_gatt_write_test_attr_value + off,
        len);
}

static void
ble_gatt_write_test_misc_long_fail_offset(uint16_t conn_handle,
                                          int off, int len)
{
    ble_gatt_write_test_rx_prep_rsp(
        conn_handle, 100, off + 1, ble_gatt_write_test_attr_value + off,
        len);
}

static void
ble_gatt_write_test_misc_long_fail_value(uint16_t conn_handle,
                                         int off, int len)
{
    ble_gatt_write_test_rx_prep_rsp(
        conn_handle, 100, off, ble_gatt_write_test_attr_value + off + 1,
        len);
}

static void
ble_gatt_write_test_misc_long_fail_length(uint16_t conn_handle,
                                          int off, int len)
{
    ble_gatt_write_test_rx_prep_rsp(
        conn_handle, 100, off, ble_gatt_write_test_attr_value + off,
        len - 1);
}

static int
ble_gatt_write_test_reliable_cb_good(uint16_t conn_handle,
                                     struct ble_gatt_error *error,
                                     struct ble_gatt_attr *attrs,
                                     uint8_t num_attrs, void *arg)
{
    int i;

    TEST_ASSERT_FATAL(num_attrs <= BLE_GATT_WRITE_TEST_MAX_ATTRS);

    TEST_ASSERT(conn_handle == 2);

    ble_gatt_write_test_num_attrs = num_attrs;
    for (i = 0; i < num_attrs; i++) {
        ble_gatt_write_test_attrs[i] = attrs + i;
    }

    ble_gatt_write_test_cb_called = 1;

    return 0;
}

static void
ble_gatt_write_test_misc_reliable_good(struct ble_gatt_attr *attrs)
{
    int num_attrs;
    int attr_idx;
    int rc;
    int i;

    ble_gatt_write_test_init();

    for (num_attrs = 0; attrs[num_attrs].handle != 0; num_attrs++)
        ;

    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);

    rc = ble_gattc_write_reliable(2, attrs, num_attrs,
                                  ble_gatt_write_test_reliable_cb_good, NULL);
    TEST_ASSERT(rc == 0);

    for (attr_idx = 0; attr_idx < num_attrs; attr_idx++) {
        /* Send the pending ATT Prep Write Command. */
        ble_hs_test_util_tx_all();

        /* Receive Prep Write response. */
        ble_gatt_write_test_rx_prep_rsp(2, attrs[attr_idx].handle, 0,
                                        attrs[attr_idx].value,
                                        attrs[attr_idx].value_len);

        /* Verify callback hasn't gotten called. */
        TEST_ASSERT(!ble_gatt_write_test_cb_called);
    }

    /* Receive Exec Write response. */
    ble_hs_test_util_tx_all();
    ble_gatt_write_test_rx_exec_rsp(2);

    /* Verify callback got called. */
    TEST_ASSERT(ble_gatt_write_test_cb_called);
    TEST_ASSERT(ble_gatt_write_test_num_attrs == num_attrs);
    for (i = 0; i < num_attrs; i++) {
        TEST_ASSERT(ble_gatt_write_test_attrs[i] == attrs + i);
    }
}

TEST_CASE(ble_gatt_write_test_no_rsp)
{
    int attr_len;
    int rc;

    ble_gatt_write_test_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);

    attr_len = 4;
    rc = ble_gattc_write_no_rsp(2, 100, ble_gatt_write_test_attr_value,
                                attr_len);
    TEST_ASSERT(rc == 0);

    /* Send the pending ATT Write Command. */
    ble_hs_test_util_tx_all();

    /* No response expected; verify callback not called. */
    TEST_ASSERT(!ble_gatt_write_test_cb_called);
}

TEST_CASE(ble_gatt_write_test_rsp)
{
    int attr_len;

    ble_gatt_write_test_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);

    attr_len = 4;
    ble_gattc_write(2, 100, ble_gatt_write_test_attr_value, attr_len,
                    ble_gatt_write_test_cb_good, &attr_len);

    /* Send the pending ATT Write Command. */
    ble_hs_test_util_tx_all();

    /* Response not received yet; verify callback not called. */
    TEST_ASSERT(!ble_gatt_write_test_cb_called);

    /* Receive write response. */
    ble_gatt_write_test_rx_rsp(2);

    /* Verify callback got called. */
    TEST_ASSERT(ble_gatt_write_test_cb_called);
}

TEST_CASE(ble_gatt_write_test_long_good)
{
    /*** 1 prep write req/rsp. */
    ble_gatt_write_test_misc_long_good(
        BLE_ATT_MTU_DFLT - BLE_ATT_PREP_WRITE_CMD_BASE_SZ);

    /*** 2 prep write reqs/rsps. */
    ble_gatt_write_test_misc_long_good(
        BLE_ATT_MTU_DFLT - BLE_ATT_PREP_WRITE_CMD_BASE_SZ + 1);

    /*** Maximum reqs/rsps. */
    ble_gatt_write_test_misc_long_good(BLE_ATT_ATTR_MAX_LEN);
}

TEST_CASE(ble_gatt_write_test_long_bad_handle)
{
    /*** 1 prep write req/rsp. */
    ble_gatt_write_test_misc_long_bad(
        BLE_ATT_MTU_DFLT - BLE_ATT_PREP_WRITE_CMD_BASE_SZ,
        ble_gatt_write_test_misc_long_fail_handle);

    /*** 2 prep write reqs/rsps. */
    ble_gatt_write_test_misc_long_bad(
        BLE_ATT_MTU_DFLT - BLE_ATT_PREP_WRITE_CMD_BASE_SZ + 1,
        ble_gatt_write_test_misc_long_fail_handle);

    /*** Maximum reqs/rsps. */
    ble_gatt_write_test_misc_long_bad(
        BLE_ATT_ATTR_MAX_LEN,
        ble_gatt_write_test_misc_long_fail_handle);
}

TEST_CASE(ble_gatt_write_test_long_bad_offset)
{
    /*** 1 prep write req/rsp. */
    ble_gatt_write_test_misc_long_bad(
        BLE_ATT_MTU_DFLT - BLE_ATT_PREP_WRITE_CMD_BASE_SZ,
        ble_gatt_write_test_misc_long_fail_offset);

    /*** 2 prep write reqs/rsps. */
    ble_gatt_write_test_misc_long_bad(
        BLE_ATT_MTU_DFLT - BLE_ATT_PREP_WRITE_CMD_BASE_SZ + 1,
        ble_gatt_write_test_misc_long_fail_offset);

    /*** Maximum reqs/rsps. */
    ble_gatt_write_test_misc_long_bad(
        BLE_ATT_ATTR_MAX_LEN,
        ble_gatt_write_test_misc_long_fail_offset);
}

TEST_CASE(ble_gatt_write_test_long_bad_value)
{
    /*** 1 prep write req/rsp. */
    ble_gatt_write_test_misc_long_bad(
        BLE_ATT_MTU_DFLT - BLE_ATT_PREP_WRITE_CMD_BASE_SZ,
        ble_gatt_write_test_misc_long_fail_value);

    /*** 2 prep write reqs/rsps. */
    ble_gatt_write_test_misc_long_bad(
        BLE_ATT_MTU_DFLT - BLE_ATT_PREP_WRITE_CMD_BASE_SZ + 1,
        ble_gatt_write_test_misc_long_fail_value);

    /*** Maximum reqs/rsps. */
    ble_gatt_write_test_misc_long_bad(
        BLE_ATT_ATTR_MAX_LEN,
        ble_gatt_write_test_misc_long_fail_value);
}

TEST_CASE(ble_gatt_write_test_long_bad_length)
{
    /*** 1 prep write req/rsp. */
    ble_gatt_write_test_misc_long_bad(
        BLE_ATT_MTU_DFLT - BLE_ATT_PREP_WRITE_CMD_BASE_SZ,
        ble_gatt_write_test_misc_long_fail_length);

    /*** 2 prep write reqs/rsps. */
    ble_gatt_write_test_misc_long_bad(
        BLE_ATT_MTU_DFLT - BLE_ATT_PREP_WRITE_CMD_BASE_SZ + 1,
        ble_gatt_write_test_misc_long_fail_length);

    /*** Maximum reqs/rsps. */
    ble_gatt_write_test_misc_long_bad(
        BLE_ATT_ATTR_MAX_LEN,
        ble_gatt_write_test_misc_long_fail_length);
}

TEST_CASE(ble_gatt_write_test_reliable_good)
{
    /*** 1 attribute. */
    ble_gatt_write_test_misc_reliable_good(
        ((struct ble_gatt_attr[]) { {
            .handle = 100,
            .value_len = 2,
            .value = (uint8_t[]){ 1, 2 },
        }, {
            0
        } }));

    /*** 2 attributes. */
    ble_gatt_write_test_misc_reliable_good(
        ((struct ble_gatt_attr[]) { {
            .handle = 100,
            .value_len = 2,
            .value = (uint8_t[]){ 1,2 },
        }, {
            .handle = 113,
            .value_len = 6,
            .value = (uint8_t[]){ 5,6,7,8,9,10 },
        }, {
            0
        } }));

    /*** 3 attributes. */
    ble_gatt_write_test_misc_reliable_good(
        ((struct ble_gatt_attr[]) { {
            .handle = 100,
            .value_len = 2,
            .value = (uint8_t[]){ 1,2 },
        }, {
            .handle = 113,
            .value_len = 6,
            .value = (uint8_t[]){ 5,6,7,8,9,10 },
        }, {
            .handle = 144,
            .value_len = 1,
            .value = (uint8_t[]){ 0xff },
        }, {
            0
        } }));
}

TEST_CASE(ble_gatt_write_test_long_queue_full)
{
    int off;
    int len;
    int rc;
    int i;

    ble_gatt_write_test_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);

    rc = ble_gattc_write_long(2, 100, ble_gatt_write_test_attr_value,
                              128, ble_gatt_write_test_cb_good, NULL);
    TEST_ASSERT(rc == 0);

    off = 0;
    for (i = 0; i < 2; i++) {
        /* Verify prep write request was sent. */
        ble_hs_test_util_tx_all();
        TEST_ASSERT(ble_hs_test_util_prev_tx_dequeue() != NULL);

        /* Receive Prep Write response. */
        len = BLE_ATT_MTU_DFLT - BLE_ATT_PREP_WRITE_CMD_BASE_SZ;
        ble_gatt_write_test_rx_prep_rsp(
            2, 100, off, ble_gatt_write_test_attr_value + off, len);

        /* Verify callback hasn't gotten called. */
        TEST_ASSERT(!ble_gatt_write_test_cb_called);

        off += len;
    }

    /* Verify prep write request was sent. */
    ble_hs_test_util_tx_all();
    TEST_ASSERT(ble_hs_test_util_prev_tx_dequeue() != NULL);

    /* Receive queue full error. */
    ble_hs_test_util_rx_att_err_rsp(2, BLE_ATT_OP_PREP_WRITE_REQ,
                                    BLE_ATT_ERR_PREPARE_QUEUE_FULL, 100);

    /* Verify callback was called. */
    TEST_ASSERT(ble_gatt_write_test_cb_called);
    TEST_ASSERT(ble_gatt_write_test_error.status ==
                BLE_HS_ATT_ERR(BLE_ATT_ERR_PREPARE_QUEUE_FULL));
    TEST_ASSERT(ble_gatt_write_test_error.att_handle == 100);

    /* Verify clear queue command got sent. */
    ble_hs_test_util_verify_tx_exec_write(0);
}

TEST_SUITE(ble_gatt_write_test_suite)
{
    ble_gatt_write_test_no_rsp();
    ble_gatt_write_test_rsp();
    ble_gatt_write_test_long_good();
    ble_gatt_write_test_long_bad_handle();
    ble_gatt_write_test_long_bad_offset();
    ble_gatt_write_test_long_bad_value();
    ble_gatt_write_test_long_bad_length();
    ble_gatt_write_test_long_queue_full();
    ble_gatt_write_test_reliable_good();
}

int
ble_gatt_write_test_all(void)
{
    ble_gatt_write_test_suite();

    return tu_any_failed;
}
