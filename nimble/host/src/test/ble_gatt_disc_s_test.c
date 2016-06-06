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
#include "host/ble_uuid.h"
#include "ble_hs_test_util.h"

struct ble_gatt_disc_s_test_svc {
    uint16_t start_handle;
    uint16_t end_handle;
    uint16_t uuid16;
    uint8_t uuid128[16];
};

#define BLE_GATT_DISC_S_TEST_MAX_SERVICES  256
static struct ble_gatt_svc
    ble_gatt_disc_s_test_svcs[BLE_GATT_DISC_S_TEST_MAX_SERVICES];
static int ble_gatt_disc_s_test_num_svcs;
static int ble_gatt_disc_s_test_rx_complete;

static void
ble_gatt_disc_s_test_init(void)
{
    ble_hs_test_util_init();
    ble_gatt_disc_s_test_num_svcs = 0;
    ble_gatt_disc_s_test_rx_complete = 0;
}

static int
ble_gatt_disc_s_test_misc_svc_length(struct ble_gatt_disc_s_test_svc *service)
{
    if (service->uuid16 != 0) {
        return 6;
    } else {
        return 20;
    }
}

static int
ble_gatt_disc_s_test_misc_rx_all_rsp_once(
    uint16_t conn_handle, struct ble_gatt_disc_s_test_svc *services)
{
    struct ble_att_read_group_type_rsp rsp;
    uint8_t buf[1024];
    int off;
    int rc;
    int i;

    /* Send the pending ATT Read By Group Type Request. */
    ble_hs_test_util_tx_all();

    rsp.bagp_length = ble_gatt_disc_s_test_misc_svc_length(services);
    ble_att_read_group_type_rsp_write(buf, BLE_ATT_READ_GROUP_TYPE_RSP_BASE_SZ,
                                      &rsp);

    off = BLE_ATT_READ_GROUP_TYPE_RSP_BASE_SZ;
    for (i = 0; ; i++) {
        if (services[i].start_handle == 0) {
            /* No more services. */
            break;
        }

        rc = ble_gatt_disc_s_test_misc_svc_length(services + i);
        if (rc != rsp.bagp_length) {
            /* UUID length is changing; Need a separate response. */
            break;
        }

        htole16(buf + off, services[i].start_handle);
        off += 2;

        htole16(buf + off, services[i].end_handle);
        off += 2;

        if (services[i].uuid16 != 0) {
            htole16(buf + off, services[i].uuid16);
            off += 2;
        } else {
            memcpy(buf + off, services[i].uuid128, 16);
            off += 16;
        }
    }

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, off);
    TEST_ASSERT(rc == 0);

    return i;
}

static void
ble_gatt_disc_s_test_misc_rx_all_rsp(
    uint16_t conn_handle, struct ble_gatt_disc_s_test_svc *services)
{
    int count;
    int idx;

    idx = 0;
    while (services[idx].start_handle != 0) {
        count = ble_gatt_disc_s_test_misc_rx_all_rsp_once(conn_handle,
                                                          services + idx);
        idx += count;
    }

    if (services[idx - 1].end_handle != 0xffff) {
        /* Send the pending ATT Request. */
        ble_hs_test_util_tx_all();
        ble_hs_test_util_rx_att_err_rsp(conn_handle,
                                        BLE_ATT_OP_READ_GROUP_TYPE_REQ,
                                        BLE_ATT_ERR_ATTR_NOT_FOUND,
                                        services[idx - 1].start_handle);
    }
}

static int
ble_gatt_disc_s_test_misc_rx_uuid_rsp_once(
    uint16_t conn_handle, struct ble_gatt_disc_s_test_svc *services)
{
    uint8_t buf[1024];
    int off;
    int rc;
    int i;

    /* Send the pending ATT Find By Type Value Request. */
    ble_hs_test_util_tx_all();

    buf[0] = BLE_ATT_OP_FIND_TYPE_VALUE_RSP;
    off = BLE_ATT_FIND_TYPE_VALUE_RSP_BASE_SZ;
    for (i = 0; ; i++) {
        if (services[i].start_handle == 0) {
            /* No more services. */
            break;
        }

        htole16(buf + off, services[i].start_handle);
        off += 2;

        htole16(buf + off, services[i].end_handle);
        off += 2;
    }

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, off);
    TEST_ASSERT(rc == 0);

    return i;
}

static void
ble_gatt_disc_s_test_misc_rx_uuid_rsp(
    uint16_t conn_handle, struct ble_gatt_disc_s_test_svc *services)
{
    int count;
    int idx;

    idx = 0;
    while (services[idx].start_handle != 0) {
        count = ble_gatt_disc_s_test_misc_rx_uuid_rsp_once(conn_handle,
                                                           services + idx);
        idx += count;
    }

    if (services[idx - 1].end_handle != 0xffff) {
        /* Send the pending ATT Request. */
        ble_hs_test_util_tx_all();
        ble_hs_test_util_rx_att_err_rsp(conn_handle,
                                        BLE_ATT_OP_FIND_TYPE_VALUE_REQ,
                                        BLE_ATT_ERR_ATTR_NOT_FOUND,
                                        services[idx - 1].start_handle);
    }
}

static void
ble_gatt_disc_s_test_misc_verify_services(
    struct ble_gatt_disc_s_test_svc *services)
{
    uint16_t uuid16;
    uint8_t *uuid128;
    int i;

    for (i = 0; services[i].start_handle != 0; i++) {
        TEST_ASSERT(services[i].start_handle ==
                    ble_gatt_disc_s_test_svcs[i].start_handle);
        TEST_ASSERT(services[i].end_handle ==
                    ble_gatt_disc_s_test_svcs[i].end_handle);

        uuid128 = ble_gatt_disc_s_test_svcs[i].uuid128;
        uuid16 = ble_uuid_128_to_16(uuid128);
        if (uuid16 != 0) {
            TEST_ASSERT(services[i].uuid16 == uuid16);
        } else {
            TEST_ASSERT(memcmp(services[i].uuid128, uuid128, 16) == 0);
        }
    }

    TEST_ASSERT(i == ble_gatt_disc_s_test_num_svcs);
    TEST_ASSERT(ble_gatt_disc_s_test_rx_complete);
}

static int
ble_gatt_disc_s_test_misc_disc_cb(uint16_t conn_handle,
                                  struct ble_gatt_error *error,
                                  struct ble_gatt_svc *service, void *arg)
{
    TEST_ASSERT(error == NULL);
    TEST_ASSERT(!ble_gatt_disc_s_test_rx_complete);

    if (service == NULL) {
        ble_gatt_disc_s_test_rx_complete = 1;
    } else {
        TEST_ASSERT_FATAL(ble_gatt_disc_s_test_num_svcs <
                          BLE_GATT_DISC_S_TEST_MAX_SERVICES);
        ble_gatt_disc_s_test_svcs[ble_gatt_disc_s_test_num_svcs++] = *service;
    }

    return 0;
}

static void
ble_gatt_disc_s_test_misc_good_all(struct ble_gatt_disc_s_test_svc *services)
{
    int rc;

    ble_gatt_disc_s_test_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);

    rc = ble_gattc_disc_all_svcs(2, ble_gatt_disc_s_test_misc_disc_cb, NULL);
    TEST_ASSERT(rc == 0);

    ble_gatt_disc_s_test_misc_rx_all_rsp(2, services);
    ble_gatt_disc_s_test_misc_verify_services(services);
}

static void
ble_gatt_disc_s_test_misc_good_uuid(
    struct ble_gatt_disc_s_test_svc *services)
{
    int rc;

    ble_gatt_disc_s_test_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);

    if (services[0].uuid16 != 0) {
        rc = ble_uuid_16_to_128(services[0].uuid16, services[0].uuid128);
        TEST_ASSERT_FATAL(rc == 0);
    }
    rc = ble_gattc_disc_svc_by_uuid(2, services[0].uuid128,
                                    ble_gatt_disc_s_test_misc_disc_cb, NULL);
    TEST_ASSERT(rc == 0);

    ble_gatt_disc_s_test_misc_rx_uuid_rsp(2, services);
    ble_gatt_disc_s_test_misc_verify_services(services);
}

TEST_CASE(ble_gatt_disc_s_test_disc_all)
{
    /*** One 128-bit service. */
    ble_gatt_disc_s_test_misc_good_all((struct ble_gatt_disc_s_test_svc[]) {
        { 1, 5, 0,      {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 0 }
    });

    /*** Two 128-bit services. */
    ble_gatt_disc_s_test_misc_good_all((struct ble_gatt_disc_s_test_svc[]) {
        { 1, 5, 0,      {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 10, 50, 0,    {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 }, },
        { 0 }
    });

    /*** Five 128-bit services. */
    ble_gatt_disc_s_test_misc_good_all((struct ble_gatt_disc_s_test_svc[]) {
        { 1, 5, 0,      {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 10, 50, 0,    {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 }, },
        { 80, 120, 0,   {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 }, },
        { 123, 678, 0,  {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4 }, },
        { 751, 999, 0,  {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 }, },
        { 0 }
    });

    /*** One 128-bit service, one 16-bit-service. */
    ble_gatt_disc_s_test_misc_good_all((struct ble_gatt_disc_s_test_svc[]) {
        { 1, 5, 0,      {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 6, 7, 0x1234 },
        { 0 }
    });

    /*** End with handle 0xffff. */
    ble_gatt_disc_s_test_misc_good_all((struct ble_gatt_disc_s_test_svc[]) {
        { 1, 5, 0,      {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 7, 0xffff, 0, {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 }, },
        { 0 }
    });
}

TEST_CASE(ble_gatt_disc_s_test_disc_service_uuid)
{
    /*** 128-bit service; one entry. */
    ble_gatt_disc_s_test_misc_good_uuid((struct ble_gatt_disc_s_test_svc[]) {
        { 1, 5, 0,      {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 0 }
    });

    /*** 128-bit service; two entries. */
    ble_gatt_disc_s_test_misc_good_uuid((struct ble_gatt_disc_s_test_svc[]) {
        { 1, 5, 0,      {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 8, 43, 0,     {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 0 }
    });

    /*** 128-bit service; five entries. */
    ble_gatt_disc_s_test_misc_good_uuid((struct ble_gatt_disc_s_test_svc[]) {
        { 1, 5, 0,      {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 8, 43, 0,     {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 67, 100, 0,   {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 102, 103, 0,  {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 262, 900, 0,  {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 0 }
    });

    /*** 128-bit service; end with handle 0xffff. */
    ble_gatt_disc_s_test_misc_good_uuid((struct ble_gatt_disc_s_test_svc[]) {
        { 1, 5, 0,      {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 7, 0xffff, 0, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, },
        { 0 }
    });

    /*** 16-bit service; one entry. */
    ble_gatt_disc_s_test_misc_good_uuid((struct ble_gatt_disc_s_test_svc[]) {
        { 1, 5, 0x1234 },
        { 0 }
    });

    /*** 16-bit service; two entries. */
    ble_gatt_disc_s_test_misc_good_uuid((struct ble_gatt_disc_s_test_svc[]) {
        { 1, 5, 0x1234 },
        { 85, 243, 0x1234 },
        { 0 }
    });

    /*** 16-bit service; five entries. */
    ble_gatt_disc_s_test_misc_good_uuid((struct ble_gatt_disc_s_test_svc[]) {
        { 1, 5, 0x1234 },
        { 85, 243, 0x1234 },
        { 382, 383, 0x1234 },
        { 562, 898, 0x1234 },
        { 902, 984, 0x1234 },
        { 0 }
    });

    /*** 16-bit service; end with handle 0xffff. */
    ble_gatt_disc_s_test_misc_good_uuid((struct ble_gatt_disc_s_test_svc[]) {
        { 1, 5, 0x1234 },
        { 9, 0xffff, 0x1234 },
        { 0 }
    });
}

TEST_SUITE(ble_gatt_disc_s_test_suite)
{
    ble_gatt_disc_s_test_disc_all();
    ble_gatt_disc_s_test_disc_service_uuid();
}

int
ble_gatt_disc_s_test_all(void)
{
    ble_gatt_disc_s_test_suite();

    return tu_any_failed;
}
