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
#include "ble_hs_conn.h"
#include "ble_att.h"
#include "ble_att_cmd.h"
#include "ble_hs_test_util.h"

static void
ble_att_clt_test_misc_init(struct ble_hs_conn **conn,
                           struct ble_l2cap_chan **att_chan)
{
    ble_hs_test_util_init();

    *conn = ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}));
    *att_chan = ble_hs_conn_chan_find(*conn, BLE_L2CAP_CID_ATT);
    TEST_ASSERT_FATAL(*att_chan != NULL);
}

TEST_CASE(ble_att_clt_test_tx_find_info)
{
    struct ble_att_find_info_req req;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    ble_att_clt_test_misc_init(&conn, &chan);

    /*** Success. */
    req.bhafq_start_handle = 1;
    req.bhafq_end_handle = 0xffff;
    rc = ble_att_clt_tx_find_info(conn, &req);
    TEST_ASSERT(rc == 0);

    /*** Error: start handle of 0. */
    req.bhafq_start_handle = 0;
    req.bhafq_end_handle = 0xffff;
    rc = ble_att_clt_tx_find_info(conn, &req);
    TEST_ASSERT(rc == EINVAL);

    /*** Error: start handle greater than end handle. */
    req.bhafq_start_handle = 500;
    req.bhafq_end_handle = 499;
    rc = ble_att_clt_tx_find_info(conn, &req);
    TEST_ASSERT(rc == EINVAL);

    /*** Success; start and end handles equal. */
    req.bhafq_start_handle = 500;
    req.bhafq_end_handle = 500;
    rc = ble_att_clt_tx_find_info(conn, &req);
    TEST_ASSERT(rc == 0);
}

TEST_CASE(ble_att_clt_test_rx_find_info)
{
    struct ble_att_find_info_rsp rsp;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint8_t buf[1024];
    uint8_t uuid128_1[16] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };
    int off;
    int rc;

    ble_att_clt_test_misc_init(&conn, &chan);

    /*** One 128-bit UUID. */
    /* Receive response with attribute mapping. */
    off = 0;
    rsp.bhafp_format = BLE_ATT_FIND_INFO_RSP_FORMAT_128BIT;
    rc = ble_att_find_info_rsp_write(buf + off, sizeof buf - off, &rsp);
    TEST_ASSERT(rc == 0);
    off += BLE_ATT_FIND_INFO_RSP_BASE_SZ;

    htole16(buf + off, 1);
    off += 2;
    memcpy(buf + off, uuid128_1, 16);
    off += 16;

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn, chan, buf, off);
    TEST_ASSERT(rc == 0);

    /*** One 16-bit UUID. */
    /* Receive response with attribute mapping. */
    off = 0;
    rsp.bhafp_format = BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT;
    rc = ble_att_find_info_rsp_write(buf + off, sizeof buf - off, &rsp);
    TEST_ASSERT(rc == 0);
    off += BLE_ATT_FIND_INFO_RSP_BASE_SZ;

    htole16(buf + off, 2);
    off += 2;
    htole16(buf + off, 0x000f);
    off += 2;

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn, chan, buf, off);
    TEST_ASSERT(rc == 0);

    /*** Two 16-bit UUIDs. */
    /* Receive response with attribute mappings. */
    off = 0;
    rsp.bhafp_format = BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT;
    rc = ble_att_find_info_rsp_write(buf + off, sizeof buf - off, &rsp);
    TEST_ASSERT(rc == 0);
    off += BLE_ATT_FIND_INFO_RSP_BASE_SZ;

    htole16(buf + off, 3);
    off += 2;
    htole16(buf + off, 0x0010);
    off += 2;

    htole16(buf + off, 4);
    off += 2;
    htole16(buf + off, 0x0011);
    off += 2;

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn, chan, buf, off);
    TEST_ASSERT(rc == 0);
}

TEST_SUITE(ble_att_clt_suite)
{
    ble_att_clt_test_tx_find_info();
    ble_att_clt_test_rx_find_info();
}

int
ble_att_clt_test_all(void)
{
    ble_att_clt_suite();

    return tu_any_failed;
}
