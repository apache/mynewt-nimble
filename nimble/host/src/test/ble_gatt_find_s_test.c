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

static struct ble_gatt_svc ble_gatt_find_s_test_svcs[256];
static int ble_gatt_find_s_test_num_svcs;
static int ble_gatt_find_s_test_proc_complete;

struct ble_gatt_find_s_test_entry {
    uint16_t inc_handle; /* 0 indicates no more entries. */
    uint16_t start_handle;
    uint16_t end_handle;
    uint8_t uuid128[16];
};

static void
ble_gatt_find_s_test_misc_init(void)
{
    ble_hs_test_util_init();
    ble_gatt_find_s_test_num_svcs = 0;
    ble_gatt_find_s_test_proc_complete = 0;
}

static int
ble_gatt_find_s_test_misc_cb(uint16_t conn_handle,
                             struct ble_gatt_error *error,
                             struct ble_gatt_svc *service,
                             void *arg)
{
    TEST_ASSERT(!ble_gatt_find_s_test_proc_complete);
    TEST_ASSERT(error == NULL);

    if (service == NULL) {
        ble_gatt_find_s_test_proc_complete = 1;
    } else {
        ble_gatt_find_s_test_svcs[ble_gatt_find_s_test_num_svcs++] = *service;
    }

    return 0;
}
static int
ble_gatt_find_s_test_misc_rx_read_type(
    uint16_t conn_handle, struct ble_gatt_find_s_test_entry *entries)
{
    struct ble_att_read_type_rsp rsp;
    uint16_t uuid16;
    uint8_t buf[1024];
    int off;
    int rc;
    int i;

    memset(&rsp, 0, sizeof rsp);

    off = BLE_ATT_READ_TYPE_RSP_BASE_SZ;
    for (i = 0; entries[i].inc_handle != 0; i++) {
        if (rsp.batp_length == BLE_GATTS_INC_SVC_LEN_NO_UUID + 2) {
            break;
        }

        uuid16 = ble_uuid_128_to_16(entries[i].uuid128);
        if (uuid16 == 0) {
            if (rsp.batp_length != 0) {
                break;
            }
            rsp.batp_length = BLE_GATTS_INC_SVC_LEN_NO_UUID + 2;
        } else {
            rsp.batp_length = BLE_GATTS_INC_SVC_LEN_UUID + 2;
        }

        TEST_ASSERT_FATAL(off + rsp.batp_length <= sizeof buf);

        htole16(buf + off, entries[i].inc_handle);
        off += 2;

        htole16(buf + off, entries[i].start_handle);
        off += 2;

        htole16(buf + off, entries[i].end_handle);
        off += 2;

        if (uuid16 != 0) {
            htole16(buf + off, uuid16);
            off += 2;
        }
    }

    if (i == 0) {
        ble_hs_test_util_rx_att_err_rsp(conn_handle, BLE_ATT_OP_READ_TYPE_REQ,
                                        BLE_ATT_ERR_ATTR_NOT_FOUND, 0);
        return 0;
    }

    ble_att_read_type_rsp_write(buf + 0, BLE_ATT_READ_TYPE_RSP_BASE_SZ, &rsp);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, off);
    TEST_ASSERT(rc == 0);

    return i;
}

static void
ble_gatt_find_s_test_misc_rx_read(uint16_t conn_handle, uint8_t *uuid128)
{
    uint8_t buf[17];
    int rc;

    buf[0] = BLE_ATT_OP_READ_RSP;
    memcpy(buf + 1, uuid128, 16);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, 17);
    TEST_ASSERT(rc == 0);
}

static void
ble_gatt_find_s_test_misc_verify_tx_read_type(uint16_t start_handle,
                                              uint16_t end_handle)
{
    struct ble_att_read_type_req req;
    struct os_mbuf *om;
    uint16_t uuid16;

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue_pullup();
    TEST_ASSERT_FATAL(om != NULL);

    ble_att_read_type_req_parse(om->om_data, om->om_len, &req);

    TEST_ASSERT(req.batq_start_handle == start_handle);
    TEST_ASSERT(req.batq_end_handle == end_handle);
    TEST_ASSERT(om->om_len == BLE_ATT_READ_TYPE_REQ_BASE_SZ + 2);
    uuid16 = le16toh(om->om_data + BLE_ATT_READ_TYPE_REQ_BASE_SZ);
    TEST_ASSERT(uuid16 == BLE_ATT_UUID_INCLUDE);
}

static void
ble_gatt_find_s_test_misc_verify_tx_read(uint16_t handle)
{
    struct ble_att_read_req req;
    struct os_mbuf *om;

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue_pullup();
    TEST_ASSERT_FATAL(om != NULL);

    ble_att_read_req_parse(om->om_data, om->om_len, &req);

    TEST_ASSERT(req.barq_handle == handle);
    TEST_ASSERT(om->om_len == BLE_ATT_READ_REQ_SZ);
}

static void
ble_gatt_find_s_test_misc_find_inc(uint16_t conn_handle,
                                   uint16_t start_handle, uint16_t end_handle,
                                   struct ble_gatt_find_s_test_entry *entries)
{
    struct ble_gatt_svc service;
    int cur_start;
    int num_found;
    int idx;
    int rc;
    int i;

    rc = ble_gattc_find_inc_svcs(conn_handle, start_handle, end_handle,
                                 ble_gatt_find_s_test_misc_cb, &service);
    TEST_ASSERT(rc == 0);

    cur_start = start_handle;
    idx = 0;
    while (1) {
        ble_gatt_find_s_test_misc_verify_tx_read_type(cur_start, end_handle);
        num_found = ble_gatt_find_s_test_misc_rx_read_type(conn_handle,
                                                           entries + idx);
        if (num_found == 0) {
            break;
        }

        if (ble_uuid_128_to_16(entries[idx].uuid128) == 0) {
            TEST_ASSERT(num_found == 1);
            ble_gatt_find_s_test_misc_verify_tx_read(
                entries[idx].start_handle);
            ble_gatt_find_s_test_misc_rx_read(conn_handle,
                                              entries[idx].uuid128);
        }

        idx += num_found;
        cur_start = entries[idx - 1].inc_handle + 1;
    }
    TEST_ASSERT(idx == ble_gatt_find_s_test_num_svcs);
    TEST_ASSERT(ble_gatt_find_s_test_proc_complete);

    for (i = 0; i < ble_gatt_find_s_test_num_svcs; i++) {
        TEST_ASSERT(ble_gatt_find_s_test_svcs[i].start_handle ==
                    entries[i].start_handle);
        TEST_ASSERT(ble_gatt_find_s_test_svcs[i].end_handle ==
                    entries[i].end_handle);
        TEST_ASSERT(memcmp(ble_gatt_find_s_test_svcs[i].uuid128,
                           entries[i].uuid128, 16) == 0);
    }
}

TEST_CASE(ble_gatt_find_s_test_1)
{
    /* Two 16-bit UUID services; one response. */
    ble_gatt_find_s_test_misc_init();
    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);
    ble_gatt_find_s_test_misc_find_inc(2, 5, 10,
        ((struct ble_gatt_find_s_test_entry[]) { {
            .inc_handle = 6,
            .start_handle = 35,
            .end_handle = 49,
            .uuid128 = BLE_UUID16_ARR(0x5155),
        }, {
            .inc_handle = 9,
            .start_handle = 543,
            .end_handle = 870,
            .uuid128 = BLE_UUID16_ARR(0x1122),
        }, {
            0,
        } })
    );

    /* One 128-bit UUID service; two responses. */
    ble_gatt_find_s_test_misc_init();
    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);
    ble_gatt_find_s_test_misc_find_inc(2, 34, 100,
        ((struct ble_gatt_find_s_test_entry[]) { {
            .inc_handle = 36,
            .start_handle = 403,
            .end_handle = 859,
            .uuid128 = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 },
        }, {
            0,
        } })
    );

    /* Two 128-bit UUID service; four responses. */
    ble_gatt_find_s_test_misc_init();
    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);
    ble_gatt_find_s_test_misc_find_inc(2, 34, 100,
        ((struct ble_gatt_find_s_test_entry[]) { {
            .inc_handle = 36,
            .start_handle = 403,
            .end_handle = 859,
            .uuid128 = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 },
        }, {
            .inc_handle = 39,
            .start_handle = 900,
            .end_handle = 932,
            .uuid128 = { 2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17 },
        }, {
            0,
        } })
    );

    /* Two 16-bit UUID; three 128-bit UUID; seven responses. */
    ble_gatt_find_s_test_misc_init();
    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);
    ble_gatt_find_s_test_misc_find_inc(2, 1, 100,
        ((struct ble_gatt_find_s_test_entry[]) { {
            .inc_handle = 36,
            .start_handle = 403,
            .end_handle = 859,
            .uuid128 = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 },
        }, {
            .inc_handle = 37,
            .start_handle = 35,
            .end_handle = 49,
            .uuid128 = BLE_UUID16_ARR(0x5155),
        }, {
            .inc_handle = 38,
            .start_handle = 543,
            .end_handle = 870,
            .uuid128 = BLE_UUID16_ARR(0x1122),
        }, {
            .inc_handle = 39,
            .start_handle = 900,
            .end_handle = 932,
            .uuid128 = { 2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17 },
        }, {
            .inc_handle = 40,
            .start_handle = 940,
            .end_handle = 950,
            .uuid128 = { 3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18 },
        }, {
            0,
        } })
    );
}

TEST_SUITE(ble_gatt_find_s_test_suite)
{
    ble_gatt_find_s_test_1();
}

int
ble_gatt_find_s_test_all(void)
{
    ble_gatt_find_s_test_suite();

    return tu_any_failed;
}
