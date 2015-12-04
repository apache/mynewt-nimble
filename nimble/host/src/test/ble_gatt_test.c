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
#include "ble_att_cmd.h"
#include "ble_hs_conn.h"
#include "ble_hs_test_util.h"

#define BLE_GATT_TEST_MAX_SERVICES  256
static struct ble_gatt_service
    ble_gatt_test_services[BLE_GATT_TEST_MAX_SERVICES];
static int ble_gatt_test_num_services;

static void
ble_gatt_test_misc_rx_disc_services_rsp(struct ble_hs_conn *conn,
                                        struct ble_gatt_service *services)
{
    struct ble_att_read_group_type_rsp rsp;
    struct ble_l2cap_chan *chan;
    uint8_t buf[1024];
    int off;
    int rc;
    int i;

    rsp.bhagp_length = 20;
    rc = ble_att_read_group_type_rsp_write(
        buf, BLE_ATT_READ_GROUP_TYPE_RSP_BASE_SZ, &rsp);
    TEST_ASSERT_FATAL(rc == 0);

    off = BLE_ATT_READ_GROUP_TYPE_RSP_BASE_SZ;
    for (i = 0; services[i].start_handle != 0; i++) {
        htole16(buf + off, services[i].start_handle);
        off += 2;

        htole16(buf + off, services[i].end_handle);
        off += 2;

        memcpy(buf + off, services[i].uuid128, 16);
        off += 16;
    }

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    TEST_ASSERT_FATAL(chan != NULL);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn, chan, buf, off);
    TEST_ASSERT(rc == 0);
}

static void
ble_gatt_test_misc_verify_services(struct ble_gatt_service *services)
{
    int i;

    for (i = 0; services[i].start_handle != 0; i++) {
        TEST_ASSERT(memcmp(services + i, ble_gatt_test_services + i,
                           sizeof *services) == 0);
    }

    TEST_ASSERT(i == ble_gatt_test_num_services);
}

static int
ble_gatt_test_misc_disc_cb(uint16_t conn_handle, int status,
                           struct ble_gatt_service *service, void *arg)
{
    TEST_ASSERT_FATAL(ble_gatt_test_num_services < BLE_GATT_TEST_MAX_SERVICES);
    TEST_ASSERT(status == 0);

    if (status == 0) {
        ble_gatt_test_services[ble_gatt_test_num_services++] = *service;
    }

    return 0;
}

static void
ble_gatt_test_misc_successful_disc_services(struct ble_gatt_service *services)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_test_util_init();
    ble_gatt_test_num_services = 0;

    conn = ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}));

    rc = ble_gatt_disc_all_services(2, ble_gatt_test_misc_disc_cb, NULL);
    TEST_ASSERT(rc == 0);
    ble_gatt_wakeup();

    ble_gatt_test_misc_rx_disc_services_rsp(conn, services);
    ble_gatt_test_misc_verify_services(services);
}

TEST_CASE(ble_gatt_test_1)
{
    /*** One service. */
    ble_gatt_test_misc_successful_disc_services((struct ble_gatt_service[]) {
        { 1, 5,     {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 0 }
    });

    /*** Two services. */
    ble_gatt_test_misc_successful_disc_services((struct ble_gatt_service[]) {
        { 1, 5,     {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 10, 50,   {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 }, },
        { 0 }
    });

    /*** Five services. */
    ble_gatt_test_misc_successful_disc_services((struct ble_gatt_service[]) {
        { 1, 5,     {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 10, 50,   {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 }, },
        { 123, 678,  {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 }, },
        { 0 }
    });

    /* XXX: Test multiple responses. */
    /* XXX: Test 16-bit UUIDs. */
}

TEST_SUITE(ble_gatt_suite)
{
    ble_gatt_test_1();
}

int
ble_gatt_test_all(void)
{
    ble_gatt_suite();

    return tu_any_failed;
}
