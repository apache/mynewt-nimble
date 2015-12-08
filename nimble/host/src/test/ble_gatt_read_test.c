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
#include "host/ble_hs_uuid.h"
#include "ble_att_cmd.h"
#include "ble_hs_conn.h"
#include "ble_hs_test_util.h"

static uint8_t ble_gatt_read_test_attr_val[1024];
static int ble_gatt_read_test_attr_len;

static int
ble_gatt_read_test_cb(uint16_t conn_handle, int status,
                      struct ble_gatt_attr *attr, void *arg)
{
    TEST_ASSERT_FATAL(status == 0);
    TEST_ASSERT_FATAL(attr->value_len <= sizeof ble_gatt_read_test_attr_val);

    memcpy(ble_gatt_read_test_attr_val, attr->value, attr->value_len);
    ble_gatt_read_test_attr_len = attr->value_len;

    return 0;
}

static void
ble_gatt_read_test_misc_rx_rsp_good(struct ble_hs_conn *conn,
                                    struct ble_gatt_attr *attr)
{
    struct ble_l2cap_chan *chan;
    uint8_t buf[1024];
    int rc;

    TEST_ASSERT_FATAL(attr->value_len <= sizeof buf);

    /* Send the pending ATT Read Request. */
    ble_gatt_wakeup();

    buf[0] = BLE_ATT_OP_READ_RSP;
    memcpy(buf + 1, attr->value, attr->value_len);

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    TEST_ASSERT_FATAL(chan != NULL);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn, chan, buf,
                                                1 + attr->value_len);
    TEST_ASSERT(rc == 0);
}

static void
ble_gatt_read_test_misc_verify_good(struct ble_hs_conn *conn,
                                    struct ble_gatt_attr *attr)
{
    int rc;

    rc = ble_gatt_read(conn->bhc_handle, attr->handle, ble_gatt_read_test_cb,
                       NULL);
    TEST_ASSERT_FATAL(rc == 0);

    ble_gatt_read_test_misc_rx_rsp_good(conn, attr);

    TEST_ASSERT(ble_gatt_read_test_attr_len == attr->value_len);
    TEST_ASSERT(memcmp(ble_gatt_read_test_attr_val, attr->value,
                       attr->value_len) == 0);
}

TEST_CASE(ble_gatt_read_test_by_handle)
{
    struct ble_hs_conn *conn;

    ble_hs_test_util_init();

    conn = ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}));

    /* Read a seven-byte attribute. */
    ble_gatt_read_test_misc_verify_good(conn, (struct ble_gatt_attr[]) { {
        .handle = 43,
        .value = (uint8_t[]){ 1,2,3,4,5,6,7 },
        .value_len = 7
    } });

    /* Read a one-byte attribute. */
    ble_gatt_read_test_misc_verify_good(conn, (struct ble_gatt_attr[]) { {
        .handle = 0x5432,
        .value = (uint8_t[]){ 0xff },
        .value_len = 7
    } });

    /* Read a 200-byte attribute. */
    ble_gatt_read_test_misc_verify_good(conn, (struct ble_gatt_attr[]) { {
        .handle = 815,
        .value = (uint8_t[200]){ 0 },
        .value_len = 200,
    } });
}

TEST_SUITE(gle_gatt_read_test_suite)
{
    ble_gatt_read_test_by_handle();
}

int
ble_gatt_read_test_all(void)
{
    gle_gatt_read_test_suite();

    return tu_any_failed;
}
