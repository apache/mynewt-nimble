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
#include "ble_att_priv.h"
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

static void
ble_att_clt_test_misc_verify_tx_write(uint16_t handle_id, void *value,
                                      int value_len, int is_req)
{
    struct ble_att_write_req req;
    struct os_mbuf *om;
    int rc;

    TEST_ASSERT_FATAL(ble_hs_test_util_prev_tx != NULL);

    om = os_mbuf_pullup(ble_hs_test_util_prev_tx,
                        OS_MBUF_PKTLEN(ble_hs_test_util_prev_tx));
    TEST_ASSERT_FATAL(om != NULL);

    if (is_req) {
        rc = ble_att_write_req_parse(om->om_data, om->om_len, &req);
    } else {
        rc = ble_att_write_cmd_parse(om->om_data, om->om_len, &req);
    }
    TEST_ASSERT_FATAL(rc == 0);

    TEST_ASSERT(req.bawq_handle == handle_id);
    TEST_ASSERT(om->om_len == BLE_ATT_WRITE_REQ_BASE_SZ + value_len);
    TEST_ASSERT(memcmp(om->om_data + BLE_ATT_WRITE_REQ_BASE_SZ, value,
                       value_len) == 0);
}

static void
ble_att_clt_test_tx_write_req_or_cmd(struct ble_hs_conn *conn,
                                     struct ble_att_write_req *req,
                                     void *value, int value_len, int is_req)
{
    int rc;

    if (is_req) {
        rc = ble_att_clt_tx_write_req(conn, req, value, value_len);
    } else {
        rc = ble_att_clt_tx_write_cmd(conn, req, value, value_len);
    }
    TEST_ASSERT(rc == 0);
}

TEST_CASE(ble_att_clt_test_tx_find_info)
{
    struct ble_att_find_info_req req;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    ble_att_clt_test_misc_init(&conn, &chan);

    /*** Success. */
    req.bafq_start_handle = 1;
    req.bafq_end_handle = 0xffff;
    rc = ble_att_clt_tx_find_info(conn, &req);
    TEST_ASSERT(rc == 0);

    /*** Error: start handle of 0. */
    req.bafq_start_handle = 0;
    req.bafq_end_handle = 0xffff;
    rc = ble_att_clt_tx_find_info(conn, &req);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    /*** Error: start handle greater than end handle. */
    req.bafq_start_handle = 500;
    req.bafq_end_handle = 499;
    rc = ble_att_clt_tx_find_info(conn, &req);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    /*** Success; start and end handles equal. */
    req.bafq_start_handle = 500;
    req.bafq_end_handle = 500;
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
    rsp.bafp_format = BLE_ATT_FIND_INFO_RSP_FORMAT_128BIT;
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
    rsp.bafp_format = BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT;
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
    rsp.bafp_format = BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT;
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

static void
ble_att_clt_test_case_tx_write_req_or_cmd(int is_req)
{
    struct ble_att_write_req req;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint8_t value300[500] = { 0 };
    uint8_t value5[5] = { 6, 7, 54, 34, 8 };

    ble_att_clt_test_misc_init(&conn, &chan);

    /*** 5-byte write. */
    req.bawq_handle = 0x1234;
    ble_att_clt_test_tx_write_req_or_cmd(conn, &req, value5, sizeof value5,
                                         is_req);
    ble_hs_test_util_tx_all();
    ble_att_clt_test_misc_verify_tx_write(0x1234, value5, sizeof value5,
                                          is_req);

    /*** Overlong write; verify command truncated to ATT MTU. */
    req.bawq_handle = 0xab83;
    ble_att_clt_test_tx_write_req_or_cmd(conn, &req, value300, sizeof value300,
                                         is_req);
    ble_hs_test_util_tx_all();
    ble_att_clt_test_misc_verify_tx_write(0xab83, value300,
                                          BLE_ATT_MTU_DFLT - 3, is_req);
}

TEST_CASE(ble_att_clt_test_tx_write)
{
    ble_att_clt_test_case_tx_write_req_or_cmd(0);
    ble_att_clt_test_case_tx_write_req_or_cmd(1);
}

TEST_CASE(ble_att_clt_test_tx_read)
{
    struct ble_att_read_req req;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    ble_att_clt_test_misc_init(&conn, &chan);

    /*** Success. */
    req.barq_handle = 1;
    rc = ble_att_clt_tx_read(conn, &req);
    TEST_ASSERT(rc == 0);

    /*** Error: handle of 0. */
    req.barq_handle = 0;
    rc = ble_att_clt_tx_read(conn, &req);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
}

TEST_CASE(ble_att_clt_test_rx_read)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint8_t buf[1024];
    int rc;

    ble_att_clt_test_misc_init(&conn, &chan);

    /*** Basic success. */
    buf[0] = BLE_ATT_OP_READ_RSP;
    buf[1] = 0;
    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn, chan, buf, 2);
    TEST_ASSERT(rc == 0);

    /*** Larger response. */
    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn, chan, buf, 20);
    TEST_ASSERT(rc == 0);

    /*** Zero-length response. */
    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn, chan, buf, 1);
    TEST_ASSERT(rc == 0);
}

TEST_CASE(ble_att_clt_test_tx_read_blob)
{
    struct ble_att_read_blob_req req;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    ble_att_clt_test_misc_init(&conn, &chan);

    /*** Success. */
    req.babq_handle = 1;
    req.babq_offset = 0;
    rc = ble_att_clt_tx_read_blob(conn, &req);
    TEST_ASSERT(rc == 0);

    /*** Error: handle of 0. */
    req.babq_handle = 0;
    req.babq_offset = 0;
    rc = ble_att_clt_tx_read_blob(conn, &req);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
}

TEST_CASE(ble_att_clt_test_rx_read_blob)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint8_t buf[1024];
    int rc;

    ble_att_clt_test_misc_init(&conn, &chan);

    /*** Basic success. */
    buf[0] = BLE_ATT_OP_READ_BLOB_RSP;
    buf[1] = 0;
    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn, chan, buf, 2);
    TEST_ASSERT(rc == 0);

    /*** Larger response. */
    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn, chan, buf, 20);
    TEST_ASSERT(rc == 0);

    /*** Zero-length response. */
    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn, chan, buf, 1);
    TEST_ASSERT(rc == 0);
}

TEST_CASE(ble_att_clt_test_tx_read_mult)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    struct os_mbuf *om;
    int rc;

    ble_att_clt_test_misc_init(&conn, &chan);

    /*** Success. */
    rc = ble_att_clt_tx_read_mult(conn, ((uint16_t[]){ 1, 2 }), 2);
    TEST_ASSERT(rc == 0);

    ble_hs_test_util_tx_all();
    TEST_ASSERT_FATAL(ble_hs_test_util_prev_tx != NULL);

    om = os_mbuf_pullup(ble_hs_test_util_prev_tx,
                        OS_MBUF_PKTLEN(ble_hs_test_util_prev_tx));
    TEST_ASSERT_FATAL(om != NULL);
    TEST_ASSERT(om->om_len == BLE_ATT_READ_MULT_REQ_BASE_SZ + 4);

    rc = ble_att_read_mult_req_parse(om->om_data, om->om_len);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(le16toh(om->om_data + BLE_ATT_READ_MULT_REQ_BASE_SZ) == 1);
    TEST_ASSERT(le16toh(om->om_data + BLE_ATT_READ_MULT_REQ_BASE_SZ + 2) == 2);

    /*** Error: no handles. */
    rc = ble_att_clt_tx_read_mult(conn, NULL, 0);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
}

TEST_CASE(ble_att_clt_test_rx_read_mult)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint8_t buf[1024];
    int rc;

    ble_att_clt_test_misc_init(&conn, &chan);

    /*** Basic success. */
    rc = ble_att_read_mult_rsp_write(buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    htole16(buf + BLE_ATT_READ_MULT_RSP_BASE_SZ + 0, 12);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(
        conn, chan, buf, BLE_ATT_READ_MULT_RSP_BASE_SZ + 2);
    TEST_ASSERT(rc == 0);

    /*** Larger response. */
    htole16(buf + BLE_ATT_READ_MULT_RSP_BASE_SZ + 0, 12);
    htole16(buf + BLE_ATT_READ_MULT_RSP_BASE_SZ + 2, 43);
    htole16(buf + BLE_ATT_READ_MULT_RSP_BASE_SZ + 4, 91);
    rc = ble_hs_test_util_l2cap_rx_payload_flat(
        conn, chan, buf, BLE_ATT_READ_MULT_RSP_BASE_SZ + 6);
    TEST_ASSERT(rc == 0);

    /*** Zero-length response. */
    rc = ble_hs_test_util_l2cap_rx_payload_flat(
        conn, chan, buf, BLE_ATT_READ_MULT_RSP_BASE_SZ + 0);
    TEST_ASSERT(rc == 0);
}

TEST_SUITE(ble_att_clt_suite)
{
    ble_att_clt_test_tx_find_info();
    ble_att_clt_test_rx_find_info();
    ble_att_clt_test_tx_read();
    ble_att_clt_test_rx_read();
    ble_att_clt_test_tx_read_blob();
    ble_att_clt_test_rx_read_blob();
    ble_att_clt_test_tx_read_mult();
    ble_att_clt_test_rx_read_mult();
    ble_att_clt_test_tx_write();
}

int
ble_att_clt_test_all(void)
{
    ble_att_clt_suite();

    return tu_any_failed;
}
