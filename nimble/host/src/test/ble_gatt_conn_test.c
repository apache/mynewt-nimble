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
#include "ble_hs_priv.h"
#include "host/ble_hs_test.h"
#include "ble_hs_conn.h"
#include "ble_gatt_priv.h"
#include "ble_hs_test_util.h"

#define BLE_GATT_BREAK_TEST_DISC_SERVICE_HANDLE     1
#define BLE_GATT_BREAK_TEST_DISC_CHR_HANDLE         2
#define BLE_GATT_BREAK_TEST_READ_HANDLE             3
#define BLE_GATT_BREAK_TEST_WRITE_HANDLE            4
#define BLE_GATT_BREAK_TEST_READ_ATTR_HANDLE        0x9383
#define BLE_GATT_BREAK_TEST_WRITE_ATTR_HANDLE       0x1234

static uint8_t ble_gatt_conn_test_write_value[] = { 1, 3, 64, 21, 6 };

static int
ble_gatt_conn_test_disc_service_cb(uint16_t conn_handle, int status,
                                   struct ble_gatt_service *service,
                                   void *arg)
{
    int *called;

    called = arg;
    *called = 1;

    TEST_ASSERT(conn_handle == BLE_GATT_BREAK_TEST_DISC_SERVICE_HANDLE);
    TEST_ASSERT(status == BLE_HS_ENOTCONN);
    TEST_ASSERT(service == NULL);

    return 0;
}

static int
ble_gatt_conn_test_disc_chr_cb(uint16_t conn_handle, int status,
                               struct ble_gatt_chr *chr, void *arg)
{
    int *called;

    called = arg;
    *called = 1;

    TEST_ASSERT(conn_handle == BLE_GATT_BREAK_TEST_DISC_CHR_HANDLE);
    TEST_ASSERT(status == BLE_HS_ENOTCONN);
    TEST_ASSERT(chr == NULL);

    return 0;
}

static int
ble_gatt_conn_test_write_cb(uint16_t conn_handle, int status,
                            struct ble_gatt_attr *attr, void *arg)
{
    int *called;

    called = arg;
    *called = 1;

    TEST_ASSERT(conn_handle == BLE_GATT_BREAK_TEST_WRITE_HANDLE);
    TEST_ASSERT(status == BLE_HS_ENOTCONN);
    TEST_ASSERT(attr != NULL);
    TEST_ASSERT(attr->handle == BLE_GATT_BREAK_TEST_WRITE_ATTR_HANDLE);
    TEST_ASSERT(attr->value_len == sizeof ble_gatt_conn_test_write_value);
    TEST_ASSERT(memcmp(attr->value, ble_gatt_conn_test_write_value,
                       sizeof ble_gatt_conn_test_write_value) == 0);

    return 0;
}

static int
ble_gatt_conn_test_read_cb(uint16_t conn_handle, int status,
                           struct ble_gatt_attr *attr, void *arg)
{
    int *called;

    called = arg;
    *called = 1;

    TEST_ASSERT(conn_handle == BLE_GATT_BREAK_TEST_READ_HANDLE);
    TEST_ASSERT(status == BLE_HS_ENOTCONN);
    TEST_ASSERT(attr == NULL);

    return 0;
}

TEST_CASE(ble_gatt_conn_test_disconnect)
{
    int disc_s_called;
    int disc_c_called;
    int read_called;
    int write_called;
    int rc;

    ble_hs_test_util_init();

    /* Create three connections. */
    ble_hs_test_util_create_conn(BLE_GATT_BREAK_TEST_DISC_SERVICE_HANDLE,
                                 ((uint8_t[]){1,2,3,4,5,6,7,8}));
    ble_hs_test_util_create_conn(BLE_GATT_BREAK_TEST_DISC_CHR_HANDLE,
                                 ((uint8_t[]){2,3,4,5,6,7,8,9}));
    ble_hs_test_util_create_conn(BLE_GATT_BREAK_TEST_READ_HANDLE,
                                 ((uint8_t[]){3,4,5,6,7,8,9,10}));
    ble_hs_test_util_create_conn(BLE_GATT_BREAK_TEST_WRITE_HANDLE,
                                 ((uint8_t[]){4,5,6,7,8,9,10,11}));

    /* Schedule some GATT procedures. */
    rc = ble_gatt_disc_all_services(BLE_GATT_BREAK_TEST_DISC_SERVICE_HANDLE,
                                    ble_gatt_conn_test_disc_service_cb,
                                    &disc_s_called);
    TEST_ASSERT_FATAL(rc == 0);

    rc = ble_gatt_disc_all_chars(BLE_GATT_BREAK_TEST_DISC_CHR_HANDLE,
                                 1, 0xffff, ble_gatt_conn_test_disc_chr_cb,
                                 &disc_c_called);
    TEST_ASSERT_FATAL(rc == 0);

    rc = ble_gatt_read(BLE_GATT_BREAK_TEST_READ_HANDLE,
                       BLE_GATT_BREAK_TEST_READ_ATTR_HANDLE,
                       ble_gatt_conn_test_read_cb, &read_called);
    TEST_ASSERT_FATAL(rc == 0);

    rc = ble_gatt_write(BLE_GATT_BREAK_TEST_WRITE_HANDLE,
                        BLE_GATT_BREAK_TEST_WRITE_ATTR_HANDLE,
                        ble_gatt_conn_test_write_value,
                        sizeof ble_gatt_conn_test_write_value,
                        ble_gatt_conn_test_write_cb, &write_called);
    TEST_ASSERT_FATAL(rc == 0);

    /* Start the procedures. */
    ble_gatt_wakeup();

    /* Break the connections; verify callbacks got called. */
    ble_gatt_connection_broken(BLE_GATT_BREAK_TEST_DISC_SERVICE_HANDLE);
    ble_gatt_connection_broken(BLE_GATT_BREAK_TEST_DISC_CHR_HANDLE);
    ble_gatt_connection_broken(BLE_GATT_BREAK_TEST_READ_HANDLE);
    ble_gatt_connection_broken(BLE_GATT_BREAK_TEST_WRITE_HANDLE);

    TEST_ASSERT(disc_s_called == 1);
    TEST_ASSERT(disc_c_called == 1);
    TEST_ASSERT(read_called == 1);
    TEST_ASSERT(write_called == 1);
}

TEST_CASE(ble_gatt_conn_test_congestion)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_test_util_init();

    /* Allow only one outstanding packet per connection. */
    ble_hs_cfg.max_outstanding_pkts_per_conn = 1;

    /* Create a connection. */
    conn = ble_hs_test_util_create_conn(1, ((uint8_t[]){1,2,3,4,5,6,7,8}));

    /* Try to send two data packets. */
    rc = ble_gatt_write(1, 0x1234, ble_gatt_conn_test_write_value,
                        sizeof ble_gatt_conn_test_write_value, NULL, NULL);
    TEST_ASSERT_FATAL(rc == 0);

    rc = ble_gatt_write(1, 0x1234, ble_gatt_conn_test_write_value,
                        sizeof ble_gatt_conn_test_write_value, NULL, NULL);
    TEST_ASSERT_FATAL(rc == 0);
    ble_gatt_wakeup();
    ble_hs_process_tx_data_queue();

    /* Ensure only one packet got sent. */
    TEST_ASSERT(conn->bhc_outstanding_pkts == 1);

    /* Additional wakeups should not trigger the second send. */
    ble_gatt_wakeup();
    ble_hs_process_tx_data_queue();
    TEST_ASSERT(conn->bhc_outstanding_pkts == 1);

    /* Receive a num-packets-completed event. */
    ble_hs_test_util_rx_num_completed_pkts_event(
        (struct ble_hs_test_util_num_completed_pkts_entry []) {
            { 1, 1 },
            { 0 }});

    /* Outstanding packet count should have been reduced to 0. */
    TEST_ASSERT(conn->bhc_outstanding_pkts == 0);

    /* Now the second write should get sent. */
    ble_gatt_wakeup();
    ble_hs_process_tx_data_queue();
    TEST_ASSERT(conn->bhc_outstanding_pkts == 1);
}

TEST_SUITE(ble_gatt_break_suite)
{
    ble_gatt_conn_test_disconnect();
    ble_gatt_conn_test_congestion();
}

int
ble_gatt_conn_test_all(void)
{
    ble_gatt_break_suite();

    return tu_any_failed;
}

