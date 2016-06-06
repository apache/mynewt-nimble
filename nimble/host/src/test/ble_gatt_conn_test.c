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
#include "ble_hs_test_util.h"

#define BLE_GATT_BREAK_TEST_READ_ATTR_HANDLE        0x9383
#define BLE_GATT_BREAK_TEST_WRITE_ATTR_HANDLE       0x1234

static uint8_t ble_gatt_conn_test_write_value[] = { 1, 3, 64, 21, 6 };

struct ble_gatt_conn_test_cb_arg {
    uint16_t exp_conn_handle;
    int called;
};

static int
ble_gatt_conn_test_attr_cb(uint16_t conn_handle, uint16_t attr_handle,
                           uint8_t *uuid128, uint8_t op,
                           struct ble_att_svr_access_ctxt *ctxt,
                           void *arg)
{
    static uint8_t data = 1;

    switch (op) {
    case BLE_ATT_ACCESS_OP_READ:
        ctxt->attr_data = &data;
        ctxt->data_len = 1;
        return 0;

    default:
        return -1;
    }
}

static int
ble_gatt_conn_test_mtu_cb(uint16_t conn_handle, struct ble_gatt_error *error,
                          uint16_t mtu, void *arg)
{
    struct ble_gatt_conn_test_cb_arg *cb_arg;

    cb_arg = arg;

    TEST_ASSERT(cb_arg->exp_conn_handle == conn_handle);
    TEST_ASSERT(!cb_arg->called);
    TEST_ASSERT_FATAL(error != NULL);
    TEST_ASSERT(error->status == BLE_HS_ENOTCONN);
    TEST_ASSERT(mtu == 0);

    cb_arg->called++;

    return 0;
}

static int
ble_gatt_conn_test_disc_all_svcs_cb(uint16_t conn_handle,
                                   struct ble_gatt_error *error,
                                   struct ble_gatt_svc *service,
                                   void *arg)
{
    struct ble_gatt_conn_test_cb_arg *cb_arg;

    cb_arg = arg;

    TEST_ASSERT(cb_arg->exp_conn_handle == conn_handle);
    TEST_ASSERT(!cb_arg->called);
    TEST_ASSERT_FATAL(error != NULL);
    TEST_ASSERT(error->status == BLE_HS_ENOTCONN);
    TEST_ASSERT(service == NULL);

    cb_arg->called++;

    return 0;
}

static int
ble_gatt_conn_test_disc_svc_uuid_cb(uint16_t conn_handle,
                                    struct ble_gatt_error *error,
                                    struct ble_gatt_svc *service,
                                    void *arg)
{
    struct ble_gatt_conn_test_cb_arg *cb_arg;

    cb_arg = arg;

    TEST_ASSERT(cb_arg->exp_conn_handle == conn_handle);
    TEST_ASSERT(!cb_arg->called);
    TEST_ASSERT_FATAL(error != NULL);
    TEST_ASSERT(error->status == BLE_HS_ENOTCONN);
    TEST_ASSERT(service == NULL);

    cb_arg->called++;

    return 0;
}

static int
ble_gatt_conn_test_find_inc_svcs_cb(uint16_t conn_handle,
                                    struct ble_gatt_error *error,
                                    struct ble_gatt_svc *service,
                                    void *arg)
{
    struct ble_gatt_conn_test_cb_arg *cb_arg;

    cb_arg = arg;

    TEST_ASSERT(cb_arg->exp_conn_handle == conn_handle);
    TEST_ASSERT(!cb_arg->called);
    TEST_ASSERT_FATAL(error != NULL);
    TEST_ASSERT(error->status == BLE_HS_ENOTCONN);
    TEST_ASSERT(service == NULL);

    cb_arg->called++;

    return 0;
}

static int
ble_gatt_conn_test_disc_all_chrs_cb(uint16_t conn_handle,
                                    struct ble_gatt_error *error,
                                    struct ble_gatt_chr *chr, void *arg)
{
    struct ble_gatt_conn_test_cb_arg *cb_arg;

    cb_arg = arg;

    TEST_ASSERT(cb_arg->exp_conn_handle == conn_handle);
    TEST_ASSERT(!cb_arg->called);
    TEST_ASSERT_FATAL(error != NULL);
    TEST_ASSERT(error->status == BLE_HS_ENOTCONN);
    TEST_ASSERT(chr == NULL);

    cb_arg->called++;

    return 0;
}

static int
ble_gatt_conn_test_disc_chr_uuid_cb(uint16_t conn_handle,
                                    struct ble_gatt_error *error,
                                    struct ble_gatt_chr *chr, void *arg)
{
    struct ble_gatt_conn_test_cb_arg *cb_arg;

    cb_arg = arg;

    TEST_ASSERT(cb_arg->exp_conn_handle == conn_handle);
    TEST_ASSERT(!cb_arg->called);
    TEST_ASSERT_FATAL(error != NULL);
    TEST_ASSERT(error->status == BLE_HS_ENOTCONN);
    TEST_ASSERT(chr == NULL);

    cb_arg->called++;

    return 0;
}

static int
ble_gatt_conn_test_disc_all_dscs_cb(uint16_t conn_handle,
                                    struct ble_gatt_error *error,
                                    uint16_t chr_def_handle,
                                    struct ble_gatt_dsc *dsc,
                                    void *arg)
{
    struct ble_gatt_conn_test_cb_arg *cb_arg;

    cb_arg = arg;

    TEST_ASSERT(cb_arg->exp_conn_handle == conn_handle);
    TEST_ASSERT(!cb_arg->called);
    TEST_ASSERT_FATAL(error != NULL);
    TEST_ASSERT(error->status == BLE_HS_ENOTCONN);
    TEST_ASSERT(dsc == NULL);

    cb_arg->called++;

    return 0;
}

static int
ble_gatt_conn_test_read_cb(uint16_t conn_handle, struct ble_gatt_error *error,
                           struct ble_gatt_attr *attr, void *arg)
{
    struct ble_gatt_conn_test_cb_arg *cb_arg;

    cb_arg = arg;

    TEST_ASSERT(cb_arg->exp_conn_handle == conn_handle);
    TEST_ASSERT(!cb_arg->called);
    TEST_ASSERT_FATAL(error != NULL);
    TEST_ASSERT(error->status == BLE_HS_ENOTCONN);
    TEST_ASSERT(attr == NULL);

    cb_arg->called++;

    return 0;
}

static int
ble_gatt_conn_test_read_uuid_cb(uint16_t conn_handle,
                                struct ble_gatt_error *error,
                                struct ble_gatt_attr *attr, void *arg)
{
    struct ble_gatt_conn_test_cb_arg *cb_arg;

    cb_arg = arg;

    TEST_ASSERT(cb_arg->exp_conn_handle == conn_handle);
    TEST_ASSERT(!cb_arg->called);
    TEST_ASSERT_FATAL(error != NULL);
    TEST_ASSERT(error->status == BLE_HS_ENOTCONN);
    TEST_ASSERT(attr == NULL);

    cb_arg->called++;

    return 0;
}

static int
ble_gatt_conn_test_read_long_cb(uint16_t conn_handle,
                                struct ble_gatt_error *error,
                                struct ble_gatt_attr *attr, void *arg)
{
    struct ble_gatt_conn_test_cb_arg *cb_arg;

    cb_arg = arg;

    TEST_ASSERT(cb_arg->exp_conn_handle == conn_handle);
    TEST_ASSERT(!cb_arg->called);
    TEST_ASSERT_FATAL(error != NULL);
    TEST_ASSERT(error->status == BLE_HS_ENOTCONN);
    TEST_ASSERT(attr == NULL);

    cb_arg->called++;

    return 0;
}
static int
ble_gatt_conn_test_read_mult_cb(uint16_t conn_handle,
                                struct ble_gatt_error *error,
                                struct ble_gatt_attr *attr, void *arg)
{
    struct ble_gatt_conn_test_cb_arg *cb_arg;

    cb_arg = arg;

    TEST_ASSERT(cb_arg->exp_conn_handle == conn_handle);
    TEST_ASSERT(!cb_arg->called);
    TEST_ASSERT_FATAL(error != NULL);
    TEST_ASSERT(error->status == BLE_HS_ENOTCONN);
    TEST_ASSERT(attr == NULL);

    cb_arg->called++;

    return 0;
}

static int
ble_gatt_conn_test_write_cb(uint16_t conn_handle, struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr, void *arg)
{
    struct ble_gatt_conn_test_cb_arg *cb_arg;

    cb_arg = arg;

    TEST_ASSERT(cb_arg->exp_conn_handle == conn_handle);
    TEST_ASSERT(!cb_arg->called);
    TEST_ASSERT_FATAL(error != NULL);
    TEST_ASSERT(error->status == BLE_HS_ENOTCONN);
    TEST_ASSERT(attr != NULL);
    TEST_ASSERT(attr->handle == BLE_GATT_BREAK_TEST_WRITE_ATTR_HANDLE);

    cb_arg->called++;

    return 0;
}

static int
ble_gatt_conn_test_write_long_cb(uint16_t conn_handle,
                                 struct ble_gatt_error *error,
                                 struct ble_gatt_attr *attr, void *arg)
{
    struct ble_gatt_conn_test_cb_arg *cb_arg;

    cb_arg = arg;

    TEST_ASSERT(cb_arg->exp_conn_handle == conn_handle);
    TEST_ASSERT(!cb_arg->called);
    TEST_ASSERT_FATAL(error != NULL);
    TEST_ASSERT(error->status == BLE_HS_ENOTCONN);
    TEST_ASSERT(attr != NULL);
    TEST_ASSERT(attr->handle == BLE_GATT_BREAK_TEST_WRITE_ATTR_HANDLE);

    cb_arg->called++;

    return 0;
}

static int
ble_gatt_conn_test_write_rel_cb(uint16_t conn_handle,
                                struct ble_gatt_error *error,
                                struct ble_gatt_attr *attrs, uint8_t num_attrs,
                                void *arg)
{
    struct ble_gatt_conn_test_cb_arg *cb_arg;

    cb_arg = arg;

    TEST_ASSERT(cb_arg->exp_conn_handle == conn_handle);
    TEST_ASSERT(!cb_arg->called);
    TEST_ASSERT_FATAL(error != NULL);
    TEST_ASSERT(error->status == BLE_HS_ENOTCONN);
    TEST_ASSERT(attrs != NULL);

    cb_arg->called++;

    return 0;
}

static int
ble_gatt_conn_test_indicate_cb(uint16_t conn_handle,
                               struct ble_gatt_error *error,
                               struct ble_gatt_attr *attr, void *arg)
{
    struct ble_gatt_conn_test_cb_arg *cb_arg;

    cb_arg = arg;

    TEST_ASSERT(cb_arg->exp_conn_handle == conn_handle);
    TEST_ASSERT(!cb_arg->called);
    TEST_ASSERT_FATAL(error != NULL);
    TEST_ASSERT(error->status == BLE_HS_ENOTCONN);
    TEST_ASSERT(attr != NULL);

    cb_arg->called++;

    return 0;
}

TEST_CASE(ble_gatt_conn_test_disconnect)
{
    struct ble_gatt_conn_test_cb_arg mtu_arg            = { 0 };
    struct ble_gatt_conn_test_cb_arg disc_all_svcs_arg  = { 0 };
    struct ble_gatt_conn_test_cb_arg disc_svc_uuid_arg  = { 0 };
    struct ble_gatt_conn_test_cb_arg find_inc_svcs_arg  = { 0 };
    struct ble_gatt_conn_test_cb_arg disc_all_chrs_arg  = { 0 };
    struct ble_gatt_conn_test_cb_arg disc_chr_uuid_arg  = { 0 };
    struct ble_gatt_conn_test_cb_arg disc_all_dscs_arg  = { 0 };
    struct ble_gatt_conn_test_cb_arg read_arg           = { 0 };
    struct ble_gatt_conn_test_cb_arg read_uuid_arg      = { 0 };
    struct ble_gatt_conn_test_cb_arg read_long_arg      = { 0 };
    struct ble_gatt_conn_test_cb_arg read_mult_arg      = { 0 };
    struct ble_gatt_conn_test_cb_arg write_arg          = { 0 };
    struct ble_gatt_conn_test_cb_arg write_long_arg     = { 0 };
    struct ble_gatt_conn_test_cb_arg write_rel_arg      = { 0 };
    struct ble_gatt_conn_test_cb_arg indicate_arg       = { 0 };
    uint16_t attr_handle;
    int rc;

    ble_hs_test_util_init();

    /*** Register an attribute to allow indicatations to be sent. */
    rc = ble_att_svr_register(BLE_UUID16(0x1212), BLE_ATT_F_READ,
                              &attr_handle,
                              ble_gatt_conn_test_attr_cb, NULL);
    TEST_ASSERT(rc == 0);

    /* Create three connections. */
    ble_hs_test_util_create_conn(1, ((uint8_t[]){1,2,3,4,5,6,7,8}),
                                 NULL, NULL);
    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);
    ble_hs_test_util_create_conn(3, ((uint8_t[]){3,4,5,6,7,8,9,10}),
                                 NULL, NULL);

    /*** Schedule some GATT procedures. */
    /* Connection 1. */
    mtu_arg.exp_conn_handle = 1;
    ble_gattc_exchange_mtu(1, ble_gatt_conn_test_mtu_cb, &mtu_arg);

    disc_all_svcs_arg.exp_conn_handle = 1;
    rc = ble_gattc_disc_all_svcs(1, ble_gatt_conn_test_disc_all_svcs_cb,
                                 &disc_all_svcs_arg);
    TEST_ASSERT_FATAL(rc == 0);

    disc_svc_uuid_arg.exp_conn_handle = 1;
    rc = ble_gattc_disc_svc_by_uuid(1, BLE_UUID16(0x1111),
                                    ble_gatt_conn_test_disc_svc_uuid_cb,
                                    &disc_svc_uuid_arg);
    TEST_ASSERT_FATAL(rc == 0);

    find_inc_svcs_arg.exp_conn_handle = 1;
    rc = ble_gattc_find_inc_svcs(1, 1, 0xffff,
                                 ble_gatt_conn_test_find_inc_svcs_cb,
                                 &find_inc_svcs_arg);
    TEST_ASSERT_FATAL(rc == 0);

    disc_all_chrs_arg.exp_conn_handle = 1;
    rc = ble_gattc_disc_all_chrs(1, 1, 0xffff,
                                 ble_gatt_conn_test_disc_all_chrs_cb,
                                 &disc_all_chrs_arg);
    TEST_ASSERT_FATAL(rc == 0);

    /* Connection 2. */
    disc_all_dscs_arg.exp_conn_handle = 2;
    rc = ble_gattc_disc_all_dscs(2, 1, 0xffff,
                                 ble_gatt_conn_test_disc_all_dscs_cb,
                                 &disc_all_dscs_arg);

    disc_chr_uuid_arg.exp_conn_handle = 2;
    rc = ble_gattc_disc_chrs_by_uuid(2, 1, 0xffff, BLE_UUID16(0x2222),
                                     ble_gatt_conn_test_disc_chr_uuid_cb,
                                     &disc_chr_uuid_arg);

    read_arg.exp_conn_handle = 2;
    rc = ble_gattc_read(2, BLE_GATT_BREAK_TEST_READ_ATTR_HANDLE,
                        ble_gatt_conn_test_read_cb, &read_arg);
    TEST_ASSERT_FATAL(rc == 0);

    read_uuid_arg.exp_conn_handle = 2;
    rc = ble_gattc_read_by_uuid(2, 1, 0xffff, BLE_UUID16(0x3333),
                                ble_gatt_conn_test_read_uuid_cb,
                                &read_uuid_arg);
    TEST_ASSERT_FATAL(rc == 0);

    read_long_arg.exp_conn_handle = 2;
    rc = ble_gattc_read_long(2, BLE_GATT_BREAK_TEST_READ_ATTR_HANDLE,
                             ble_gatt_conn_test_read_long_cb, &read_long_arg);
    TEST_ASSERT_FATAL(rc == 0);

    /* Connection 3. */
    read_mult_arg.exp_conn_handle = 3;
    rc = ble_gattc_read_mult(3, ((uint16_t[3]){5,6,7}), 3,
                             ble_gatt_conn_test_read_mult_cb, &read_mult_arg);
    TEST_ASSERT_FATAL(rc == 0);

    write_arg.exp_conn_handle = 3;
    rc = ble_gattc_write(3, BLE_GATT_BREAK_TEST_WRITE_ATTR_HANDLE,
                         ble_gatt_conn_test_write_value,
                         sizeof ble_gatt_conn_test_write_value,
                         ble_gatt_conn_test_write_cb, &write_arg);
    TEST_ASSERT_FATAL(rc == 0);

    write_long_arg.exp_conn_handle = 3;
    rc = ble_gattc_write_long(3, BLE_GATT_BREAK_TEST_WRITE_ATTR_HANDLE,
                              ble_gatt_conn_test_write_value,
                              sizeof ble_gatt_conn_test_write_value,
                              ble_gatt_conn_test_write_long_cb,
                              &write_long_arg);
    TEST_ASSERT_FATAL(rc == 0);

    write_rel_arg.exp_conn_handle = 3;
    rc = ble_gattc_write_reliable(3,
                                  ((struct ble_gatt_attr[]){{8, 0, 0, NULL}}),
                                  1, ble_gatt_conn_test_write_rel_cb,
                                  &write_rel_arg);
    TEST_ASSERT_FATAL(rc == 0);

    indicate_arg.exp_conn_handle = 3;
    rc = ble_gattc_indicate(3, attr_handle,
                            ble_gatt_conn_test_indicate_cb,
                            &indicate_arg);
    TEST_ASSERT_FATAL(rc == 0);

    /*** Start the procedures. */
    ble_hs_test_util_tx_all();

    /*** Break the connections; verify proper callbacks got called. */
    /* Connection 1. */
    ble_gattc_connection_broken(1);
    TEST_ASSERT(mtu_arg.called == 1);
    TEST_ASSERT(disc_all_svcs_arg.called == 1);
    TEST_ASSERT(disc_svc_uuid_arg.called == 1);
    TEST_ASSERT(find_inc_svcs_arg.called == 1);
    TEST_ASSERT(disc_all_chrs_arg.called == 1);
    TEST_ASSERT(disc_chr_uuid_arg.called == 0);
    TEST_ASSERT(disc_all_dscs_arg.called == 0);
    TEST_ASSERT(read_arg.called == 0);
    TEST_ASSERT(read_uuid_arg.called == 0);
    TEST_ASSERT(read_long_arg.called == 0);
    TEST_ASSERT(read_mult_arg.called == 0);
    TEST_ASSERT(write_arg.called == 0);
    TEST_ASSERT(write_long_arg.called == 0);
    TEST_ASSERT(write_rel_arg.called == 0);
    TEST_ASSERT(indicate_arg.called == 0);

    /* Connection 2. */
    ble_gattc_connection_broken(2);
    TEST_ASSERT(mtu_arg.called == 1);
    TEST_ASSERT(disc_all_svcs_arg.called == 1);
    TEST_ASSERT(disc_svc_uuid_arg.called == 1);
    TEST_ASSERT(find_inc_svcs_arg.called == 1);
    TEST_ASSERT(disc_all_chrs_arg.called == 1);
    TEST_ASSERT(disc_chr_uuid_arg.called == 1);
    TEST_ASSERT(disc_all_dscs_arg.called == 1);
    TEST_ASSERT(read_arg.called == 1);
    TEST_ASSERT(read_uuid_arg.called == 1);
    TEST_ASSERT(read_long_arg.called == 1);
    TEST_ASSERT(read_mult_arg.called == 0);
    TEST_ASSERT(write_arg.called == 0);
    TEST_ASSERT(write_long_arg.called == 0);
    TEST_ASSERT(write_rel_arg.called == 0);
    TEST_ASSERT(indicate_arg.called == 0);

    /* Connection 3. */
    ble_gattc_connection_broken(3);
    TEST_ASSERT(mtu_arg.called == 1);
    TEST_ASSERT(disc_all_svcs_arg.called == 1);
    TEST_ASSERT(disc_svc_uuid_arg.called == 1);
    TEST_ASSERT(find_inc_svcs_arg.called == 1);
    TEST_ASSERT(disc_all_chrs_arg.called == 1);
    TEST_ASSERT(disc_chr_uuid_arg.called == 1);
    TEST_ASSERT(disc_all_dscs_arg.called == 1);
    TEST_ASSERT(read_arg.called == 1);
    TEST_ASSERT(read_uuid_arg.called == 1);
    TEST_ASSERT(read_long_arg.called == 1);
    TEST_ASSERT(read_mult_arg.called == 1);
    TEST_ASSERT(write_arg.called == 1);
    TEST_ASSERT(write_long_arg.called == 1);
    TEST_ASSERT(write_rel_arg.called == 1);
    TEST_ASSERT(indicate_arg.called == 1);
}

TEST_SUITE(ble_gatt_break_suite)
{
    ble_gatt_conn_test_disconnect();
}

int
ble_gatt_conn_test_all(void)
{
    ble_gatt_break_suite();

    return tu_any_failed;
}
