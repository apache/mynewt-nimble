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
#include <string.h>
#include "nimble/hci_common.h"
#include "host/ble_hs.h"
#include "host/ble_hs_test.h"
#include "testutil/testutil.h"
#include "ble_l2cap.h"
#include "ble_hs_test_util.h"
#include "ble_hs_conn.h"
#include "ble_hs_att.h"
#include "ble_hs_att_cmd.h"

static uint8_t *ble_hs_att_test_attr_1;
static int ble_hs_att_test_attr_1_len;

static uint8_t ble_hs_att_test_attr_2[1024];
static int ble_hs_att_test_attr_2_len;

static int
ble_hs_att_test_misc_attr_fn_1(struct ble_hs_att_entry *entry,
                               uint8_t op, union ble_hs_att_handle_arg *arg)
{
    switch (op) {
    case BLE_HS_ATT_OP_READ_REQ:
        arg->aha_read.attr_data = ble_hs_att_test_attr_1;
        arg->aha_read.attr_len = ble_hs_att_test_attr_1_len;
        return 0;

    default:
        return -1;
    }
}

static int
ble_hs_att_test_misc_attr_fn_2(struct ble_hs_att_entry *entry,
                               uint8_t op, union ble_hs_att_handle_arg *arg)
{
    struct os_mbuf_pkthdr *omp;
    int rc;

    switch (op) {
    case BLE_HS_ATT_OP_WRITE_REQ:
        omp = OS_MBUF_PKTHDR(arg->aha_write.om);
        rc = os_mbuf_copydata(arg->aha_write.om, 0, arg->aha_write.attr_len,
                              ble_hs_att_test_attr_2);
        TEST_ASSERT(rc == 0);
        ble_hs_att_test_attr_2_len = arg->aha_write.attr_len;
        return 0;

    default:
        return -1;
    }

    (void)omp;
}

static void
ble_hs_att_test_misc_verify_tx_err_rsp(struct ble_l2cap_chan *chan,
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

    /* Remove the error response from the buffer. */
    os_mbuf_adj(&ble_l2cap_mbuf_pool, chan->blc_tx_buf,
                BLE_HS_ATT_ERROR_RSP_SZ);
}

static void
ble_hs_att_test_misc_verify_tx_read_rsp(struct ble_l2cap_chan *chan,
                                        uint8_t *attr_data, int attr_len)
{
    uint8_t u8;
    int rc;
    int i;

    rc = os_mbuf_copydata(chan->blc_tx_buf, 0, 1, &u8);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(u8 == BLE_HS_ATT_OP_READ_RSP);

    for (i = 0; i < attr_len; i++) {
        rc = os_mbuf_copydata(chan->blc_tx_buf, i + 1, 1, &u8);
        TEST_ASSERT(rc == 0);
        TEST_ASSERT(u8 == attr_data[i]);
    }

    rc = os_mbuf_copydata(chan->blc_tx_buf, i + 1, 1, &u8);
    TEST_ASSERT(rc != 0);

    /* Remove the read response from the buffer. */
    os_mbuf_adj(&ble_l2cap_mbuf_pool, chan->blc_tx_buf, attr_len + 1);
}

static void
ble_hs_att_test_misc_verify_tx_write_rsp(struct ble_l2cap_chan *chan)
{
    uint8_t u8;
    int rc;

    rc = os_mbuf_copydata(chan->blc_tx_buf, 0, 1, &u8);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(u8 == BLE_HS_ATT_OP_WRITE_RSP);

    /* Remove the write response from the buffer. */
    os_mbuf_adj(&ble_l2cap_mbuf_pool, chan->blc_tx_buf,
                BLE_HS_ATT_WRITE_RSP_SZ);
}

TEST_CASE(ble_hs_att_test_read)
{
    struct ble_hs_att_read_req req;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint8_t buf[BLE_HS_ATT_READ_REQ_SZ];
    uint8_t uuid[16] = {0};
    int rc;

    rc = ble_hs_init(10);
    TEST_ASSERT_FATAL(rc == 0);

    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}));
    conn = ble_hs_conn_find(2);
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
    ble_hs_att_test_misc_verify_tx_err_rsp(chan, BLE_HS_ATT_OP_READ_REQ, 0,
                                           BLE_ERR_ATTR_NOT_FOUND);

    /*** Successful read. */
    ble_hs_att_test_attr_1 = (uint8_t[]){0,1,2,3,4,5,6,7};
    ble_hs_att_test_attr_1_len = 8;
    rc = ble_hs_att_register(uuid, 0, &req.bharq_handle,
                             ble_hs_att_test_misc_attr_fn_1);
    TEST_ASSERT(rc == 0);

    rc = ble_hs_att_read_req_write(buf, sizeof buf, &req);
    TEST_ASSERT(rc == 0);

    rc = ble_l2cap_rx_payload(conn, chan, buf, sizeof buf);
    TEST_ASSERT(rc == 0);

    ble_hs_att_test_misc_verify_tx_read_rsp(chan, ble_hs_att_test_attr_1,
                                            ble_hs_att_test_attr_1_len);


    /*** Partial read. */
    ble_hs_att_test_attr_1 =
        (uint8_t[]){0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,
                    22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39};
    ble_hs_att_test_attr_1_len = 40;

    rc = ble_hs_att_read_req_write(buf, sizeof buf, &req);
    TEST_ASSERT(rc == 0);

    rc = ble_l2cap_rx_payload(conn, chan, buf, sizeof buf);
    TEST_ASSERT(rc == 0);

    ble_hs_att_test_misc_verify_tx_read_rsp(chan, ble_hs_att_test_attr_1,
                                            BLE_HS_ATT_MTU_DFLT - 1);
}

TEST_CASE(ble_hs_att_test_write)
{
    struct ble_hs_att_write_req req;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint8_t buf[BLE_HS_ATT_READ_REQ_SZ + 8];
    uint8_t uuid[16] = {0};
    int rc;

    rc = ble_hs_init(10);
    TEST_ASSERT_FATAL(rc == 0);

    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}));
    conn = ble_hs_conn_find(2);
    TEST_ASSERT_FATAL(conn != NULL);

    chan = ble_l2cap_chan_find(conn, BLE_L2CAP_CID_ATT);
    TEST_ASSERT_FATAL(chan != NULL);

    /*** Nonexistent attribute. */
    req.bhawq_op = BLE_HS_ATT_OP_WRITE_REQ;
    req.bhawq_handle = 0;
    rc = ble_hs_att_write_req_write(buf, sizeof buf, &req);
    TEST_ASSERT(rc == 0);
    memcpy(buf + BLE_HS_ATT_READ_REQ_SZ, ((uint8_t[]){0,1,2,3,4,5,6,7}), 8);

    rc = ble_l2cap_rx_payload(conn, chan, buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_hs_att_test_misc_verify_tx_err_rsp(chan, BLE_HS_ATT_OP_WRITE_REQ, 0,
                                           BLE_ERR_ATTR_NOT_FOUND);

    /*** Successful write. */
    rc = ble_hs_att_register(uuid, 0, &req.bhawq_handle,
                             ble_hs_att_test_misc_attr_fn_2);
    TEST_ASSERT(rc == 0);

    rc = ble_hs_att_write_req_write(buf, sizeof buf, &req);
    TEST_ASSERT(rc == 0);
    memcpy(buf + BLE_HS_ATT_WRITE_REQ_MIN_SZ,
           ((uint8_t[]){0,1,2,3,4,5,6,7}), 8);

    rc = ble_l2cap_rx_payload(conn, chan, buf, sizeof buf);
    TEST_ASSERT(rc == 0);

    ble_hs_att_test_misc_verify_tx_write_rsp(chan);
}

TEST_SUITE(att_suite)
{
    ble_hs_att_test_read();
    ble_hs_att_test_write();
}

int
ble_hs_att_test_all(void)
{
    att_suite();

    return tu_any_failed;
}
