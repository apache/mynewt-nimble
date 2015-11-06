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

#include <stddef.h>
#include <errno.h>
#include "nimble/hci_common.h"
#include "host/ble_hs.h"
#include "host/ble_hs_test.h"
#include "ble_l2cap.h"
#include "ble_hs_conn.h"
#include "ble_hs_att_cmd.h"
#include "testutil/testutil.h"

static void
ble_hs_att_test_misc_verify_err_rsp(struct ble_l2cap_chan *chan,
                                    uint8_t req_op, uint16_t handle,
                                    uint8_t error_code)
{
    struct ble_hs_att_error_rsp rsp;
    uint8_t buf[BLE_HS_ATT_ERROR_RSP_SZ];
    int rc;

    rc = os_mbuf_copydata(chan->blc_tx_buf, 0, sizeof buf, buf);
    TEST_ASSERT(rc == 0);

    rc = ble_hs_att_error_rsp_parse(buf, sizeof buf, &rsp);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(rsp.bhaep_op == BLE_HS_ATT_OP_ERROR_RSP);
    TEST_ASSERT(rsp.bhaep_req_op == req_op);
    TEST_ASSERT(rsp.bhaep_handle == handle);
    TEST_ASSERT(rsp.bhaep_error_code == error_code);
}

TEST_CASE(ble_hs_att_test_small_read)
{
    struct ble_hs_att_read_req req;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint8_t buf[BLE_HS_ATT_READ_REQ_SZ];
    int rc;

    conn = ble_hs_conn_alloc();
    TEST_ASSERT_FATAL(conn != NULL);

    chan = ble_l2cap_chan_find(conn, BLE_L2CAP_CID_ATT);
    TEST_ASSERT_FATAL(chan != NULL);

    /*** Nonexistent attribute. */
    req.bharq_op = BLE_HS_ATT_OP_READ_REQ;
    req.bharq_handle = 0;
    rc = ble_hs_att_read_req_write(buf, sizeof buf, &req);
    TEST_ASSERT(rc == 0);

    rc = ble_l2cap_rx_payload(conn, chan, buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_hs_att_test_misc_verify_err_rsp(chan, BLE_HS_ATT_OP_READ_REQ, 0,
                                        BLE_ERR_ATTR_NOT_FOUND);

    ble_hs_conn_free(conn);
}

TEST_SUITE(att_suite)
{
    int rc;

    rc = host_init();
    TEST_ASSERT_FATAL(rc == 0);

    ble_hs_att_test_small_read();
}

int
ble_hs_att_test_all(void)
{
    att_suite();

    return tu_any_failed;
}
