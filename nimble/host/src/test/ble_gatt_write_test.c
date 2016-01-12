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
#include "testutil/testutil.h"
#include "nimble/ble.h"
#include "host/ble_hs_test.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "ble_hs_priv.h"
#include "ble_att_cmd.h"
#include "ble_gatt_priv.h"
#include "ble_hs_conn.h"
#include "ble_hs_test_util.h"

static int ble_gatt_write_test_cb_called;

static uint8_t ble_gatt_write_test_attr_value[] = { 1, 2, 3, 4 };

static void
ble_gatt_write_test_init(void)
{
    ble_hs_test_util_init();
    ble_gatt_write_test_cb_called = 0;
}

static int
ble_gatt_write_test_cb(uint16_t conn_handle, struct ble_gatt_error *error,
                       struct ble_gatt_attr *attr, void *arg)
{
    TEST_ASSERT(conn_handle == 2);
    TEST_ASSERT(error == NULL);
    TEST_ASSERT(attr->handle == 100);
    TEST_ASSERT(attr->value_len == sizeof ble_gatt_write_test_attr_value);
    TEST_ASSERT(attr->value == ble_gatt_write_test_attr_value);
    TEST_ASSERT(arg == NULL);

    ble_gatt_write_test_cb_called = 1;

    return 0;
}

static void
ble_gatt_write_test_rx_rsp(struct ble_hs_conn *conn)
{
    struct ble_l2cap_chan *chan;
    uint8_t op;
    int rc;

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    TEST_ASSERT_FATAL(chan != NULL);

    op = BLE_ATT_OP_WRITE_RSP;
    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn, chan, &op, 1);
    TEST_ASSERT(rc == 0);
}

TEST_CASE(ble_gatt_write_test_no_rsp)
{
    int rc;

    ble_gatt_write_test_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}));

    rc = ble_gattc_write_no_rsp(2, 100, ble_gatt_write_test_attr_value,
                                sizeof ble_gatt_write_test_attr_value,
                                ble_gatt_write_test_cb, NULL);
    TEST_ASSERT(rc == 0);

    /* Send the pending ATT Write Command. */
    ble_hs_test_util_tx_all();

    /* No response expected; verify callback got called. */
    TEST_ASSERT(ble_gatt_write_test_cb_called);
}

TEST_CASE(ble_gatt_write_test_rsp)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_gatt_write_test_init();

    conn = ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}));

    rc = ble_gattc_write(2, 100, ble_gatt_write_test_attr_value,
                         sizeof ble_gatt_write_test_attr_value,
                         ble_gatt_write_test_cb, NULL);
    TEST_ASSERT(rc == 0);

    /* Send the pending ATT Write Command. */
    ble_hs_test_util_tx_all();

    /* Response not received yet; verify callback not called. */
    TEST_ASSERT(!ble_gatt_write_test_cb_called);

    /* Receive write response. */
    ble_gatt_write_test_rx_rsp(conn);

    /* Verify callback got called. */
    TEST_ASSERT(ble_gatt_write_test_cb_called);
}

TEST_SUITE(ble_gatt_write_test_suite)
{
    ble_gatt_write_test_no_rsp();
    ble_gatt_write_test_rsp();
}

int
ble_gatt_write_test_all(void)
{
    ble_gatt_write_test_suite();

    return tu_any_failed;
}
