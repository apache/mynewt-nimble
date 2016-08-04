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

#include <stddef.h>
#include <errno.h>
#include "testutil/testutil.h"
#include "nimble/hci_common.h"
#include "host/ble_hs_test.h"
#include "ble_hs_test_util.h"

#define BLE_L2CAP_TEST_CID  99

static int ble_l2cap_test_update_status;
static void *ble_l2cap_test_update_arg;

/*****************************************************************************
 * $util                                                                     *
 *****************************************************************************/

#define BLE_L2CAP_TEST_UTIL_HCI_HDR(handle, pb, len)    \
    ((struct hci_data_hdr) {                            \
        .hdh_handle_pb_bc = ((handle)  << 0) |          \
                            ((pb)      << 12),          \
        .hdh_len = (len)                                \
    })

static void
ble_l2cap_test_util_init(void)
{
    ble_hs_test_util_init();
    ble_l2cap_test_update_status = -1;
    ble_l2cap_test_update_arg = (void *)(uintptr_t)-1;
}

static void
ble_l2cap_test_util_rx_update_req(uint16_t conn_handle, uint8_t id,
                                  struct ble_l2cap_sig_update_params *params)
{
    struct ble_l2cap_sig_update_req req;
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int rc;

    hci_hdr = BLE_L2CAP_TEST_UTIL_HCI_HDR(
        2, BLE_HCI_PB_FIRST_FLUSH,
        BLE_L2CAP_HDR_SZ + BLE_L2CAP_SIG_HDR_SZ + BLE_L2CAP_SIG_UPDATE_REQ_SZ);

    rc = ble_l2cap_sig_init_cmd(BLE_L2CAP_SIG_OP_UPDATE_REQ, id,
                                BLE_L2CAP_SIG_UPDATE_REQ_SZ, &om, &v);
    TEST_ASSERT_FATAL(rc == 0);

    req.itvl_min = params->itvl_min;
    req.itvl_max = params->itvl_max;
    req.slave_latency = params->slave_latency;
    req.timeout_multiplier = params->timeout_multiplier;
    ble_l2cap_sig_update_req_write(v, BLE_L2CAP_SIG_UPDATE_REQ_SZ, &req);

    ble_hs_test_util_set_ack(
        ble_hs_hci_util_opcode_join(BLE_HCI_OGF_LE,
                                    BLE_HCI_OCF_LE_CONN_UPDATE), 0);
    rc = ble_hs_test_util_l2cap_rx_first_frag(conn_handle, BLE_L2CAP_CID_SIG,
                                              &hci_hdr, om);
    TEST_ASSERT_FATAL(rc == 0);
}

static int
ble_l2cap_test_util_rx_update_rsp(uint16_t conn_handle,
                                  uint8_t id, uint16_t result)
{
    struct ble_l2cap_sig_update_rsp rsp;
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int rc;

    hci_hdr = BLE_L2CAP_TEST_UTIL_HCI_HDR(
        2, BLE_HCI_PB_FIRST_FLUSH,
        BLE_L2CAP_HDR_SZ + BLE_L2CAP_SIG_HDR_SZ + BLE_L2CAP_SIG_UPDATE_RSP_SZ);

    rc = ble_l2cap_sig_init_cmd(BLE_L2CAP_SIG_OP_UPDATE_RSP, id,
                                BLE_L2CAP_SIG_UPDATE_RSP_SZ, &om, &v);
    TEST_ASSERT_FATAL(rc == 0);

    rsp.result = result;
    ble_l2cap_sig_update_rsp_write(v, BLE_L2CAP_SIG_UPDATE_RSP_SZ, &rsp);

    rc = ble_hs_test_util_l2cap_rx_first_frag(conn_handle, BLE_L2CAP_CID_SIG,
                                              &hci_hdr, om);
    return rc;
}


static struct os_mbuf *
ble_l2cap_test_util_verify_tx_sig_hdr(uint8_t op, uint8_t id,
                                      uint16_t payload_len,
                                      struct ble_l2cap_sig_hdr *out_hdr)
{
    struct ble_l2cap_sig_hdr hdr;
    struct os_mbuf *om;

    om = ble_hs_test_util_prev_tx_dequeue();
    TEST_ASSERT_FATAL(om != NULL);

    TEST_ASSERT(OS_MBUF_PKTLEN(om) == BLE_L2CAP_SIG_HDR_SZ + payload_len);
    ble_l2cap_sig_hdr_parse(om->om_data, om->om_len, &hdr);
    TEST_ASSERT(hdr.op == op);
    if (id != 0) {
        TEST_ASSERT(hdr.identifier == id);
    }
    TEST_ASSERT(hdr.length == payload_len);

    om->om_data += BLE_L2CAP_SIG_HDR_SZ;
    om->om_len -= BLE_L2CAP_SIG_HDR_SZ;

    if (out_hdr != NULL) {
        *out_hdr = hdr;
    }

    return om;
}

/**
 * @return                      The L2CAP sig identifier in the request.
 */
static uint8_t
ble_l2cap_test_util_verify_tx_update_req(
    struct ble_l2cap_sig_update_params *params)
{
    struct ble_l2cap_sig_update_req req;
    struct ble_l2cap_sig_hdr hdr;
    struct os_mbuf *om;

    om = ble_l2cap_test_util_verify_tx_sig_hdr(BLE_L2CAP_SIG_OP_UPDATE_REQ, 0,
                                               BLE_L2CAP_SIG_UPDATE_REQ_SZ,
                                               &hdr);

    /* Verify payload. */
    ble_l2cap_sig_update_req_parse(om->om_data, om->om_len, &req);
    TEST_ASSERT(req.itvl_min == params->itvl_min);
    TEST_ASSERT(req.itvl_max == params->itvl_max);
    TEST_ASSERT(req.slave_latency == params->slave_latency);
    TEST_ASSERT(req.timeout_multiplier == params->timeout_multiplier);

    return hdr.identifier;
}

static void
ble_l2cap_test_util_verify_tx_update_rsp(uint8_t exp_id, uint16_t exp_result)
{
    struct ble_l2cap_sig_update_rsp rsp;
    struct os_mbuf *om;

    om = ble_l2cap_test_util_verify_tx_sig_hdr(BLE_L2CAP_SIG_OP_UPDATE_RSP,
                                               exp_id,
                                               BLE_L2CAP_SIG_UPDATE_RSP_SZ,
                                               NULL);

    ble_l2cap_sig_update_rsp_parse(om->om_data, om->om_len, &rsp);
    TEST_ASSERT(rsp.result == exp_result);
}

static void
ble_l2cap_test_util_verify_tx_update_conn(
    struct ble_gap_upd_params *params)
{
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_CONN_UPDATE,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_CONN_UPDATE_LEN);
    TEST_ASSERT(le16toh(param + 0) == 2);
    TEST_ASSERT(le16toh(param + 2) == params->itvl_min);
    TEST_ASSERT(le16toh(param + 4) == params->itvl_max);
    TEST_ASSERT(le16toh(param + 6) == params->latency);
    TEST_ASSERT(le16toh(param + 8) == params->supervision_timeout);
    TEST_ASSERT(le16toh(param + 10) == params->min_ce_len);
    TEST_ASSERT(le16toh(param + 12) == params->max_ce_len);
}

static int
ble_l2cap_test_util_dummy_rx(uint16_t conn_handle, struct os_mbuf **om)
{
    return 0;
}

static void
ble_l2cap_test_util_create_conn(uint16_t conn_handle, uint8_t *addr,
                                ble_gap_event_fn *cb, void *cb_arg)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;

    ble_hs_test_util_create_conn(conn_handle, addr, cb, cb_arg);

    ble_hs_lock();

    conn = ble_hs_conn_find(conn_handle);
    TEST_ASSERT_FATAL(conn != NULL);

    chan = ble_l2cap_chan_alloc();
    TEST_ASSERT_FATAL(chan != NULL);

    chan->blc_cid = BLE_L2CAP_TEST_CID;
    chan->blc_my_mtu = 240;
    chan->blc_default_mtu = 240;
    chan->blc_rx_fn = ble_l2cap_test_util_dummy_rx;

    ble_hs_conn_chan_insert(conn, chan);

    ble_hs_test_util_prev_hci_tx_clear();

    ble_hs_unlock();
}

static int
ble_l2cap_test_util_rx_first_frag(uint16_t conn_handle,
                                  uint16_t l2cap_frag_len,
                                  uint16_t cid, uint16_t l2cap_len)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    uint16_t hci_len;
    void *v;
    int rc;

    om = ble_hs_mbuf_l2cap_pkt();
    TEST_ASSERT_FATAL(om != NULL);

    v = os_mbuf_extend(om, l2cap_frag_len);
    TEST_ASSERT_FATAL(v != NULL);

    om = ble_l2cap_prepend_hdr(om, cid, l2cap_len);
    TEST_ASSERT_FATAL(om != NULL);

    hci_len = sizeof hci_hdr + l2cap_frag_len;
    hci_hdr = BLE_L2CAP_TEST_UTIL_HCI_HDR(conn_handle,
                                          BLE_HCI_PB_FIRST_FLUSH, hci_len);
    rc = ble_hs_test_util_l2cap_rx(conn_handle, &hci_hdr, om);
    return rc;
}

static int
ble_l2cap_test_util_rx_next_frag(uint16_t conn_handle, uint16_t hci_len)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int rc;

    om = ble_hs_mbuf_l2cap_pkt();
    TEST_ASSERT_FATAL(om != NULL);

    v = os_mbuf_extend(om, hci_len);
    TEST_ASSERT_FATAL(v != NULL);

    hci_hdr = BLE_L2CAP_TEST_UTIL_HCI_HDR(conn_handle,
                                          BLE_HCI_PB_MIDDLE, hci_len);
    rc = ble_hs_test_util_l2cap_rx(conn_handle, &hci_hdr, om);
    return rc;
}

static void
ble_l2cap_test_util_verify_first_frag(uint16_t conn_handle,
                                      uint16_t l2cap_frag_len,
                                      uint16_t l2cap_len)
{
    struct ble_hs_conn *conn;
    int rc;

    rc = ble_l2cap_test_util_rx_first_frag(conn_handle, l2cap_frag_len,
                                           BLE_L2CAP_TEST_CID, l2cap_len);
    TEST_ASSERT(rc == 0);

    ble_hs_lock();

    conn = ble_hs_conn_find(conn_handle);
    TEST_ASSERT_FATAL(conn != NULL);
    TEST_ASSERT(conn->bhc_rx_chan != NULL &&
                conn->bhc_rx_chan->blc_cid == BLE_L2CAP_TEST_CID);

    ble_hs_unlock();
}

static void
ble_l2cap_test_util_verify_middle_frag(uint16_t conn_handle,
                                       uint16_t hci_len)
{
    struct ble_hs_conn *conn;
    int rc;

    rc = ble_l2cap_test_util_rx_next_frag(conn_handle, hci_len);
    TEST_ASSERT(rc == 0);

    ble_hs_lock();

    conn = ble_hs_conn_find(conn_handle);
    TEST_ASSERT_FATAL(conn != NULL);
    TEST_ASSERT(conn->bhc_rx_chan != NULL &&
                conn->bhc_rx_chan->blc_cid == BLE_L2CAP_TEST_CID);

    ble_hs_unlock();
}

static void
ble_l2cap_test_util_verify_last_frag(uint16_t conn_handle,
                                     uint16_t hci_len)
{
    struct ble_hs_conn *conn;
    int rc;

    rc = ble_l2cap_test_util_rx_next_frag(conn_handle, hci_len);
    TEST_ASSERT(rc == 0);

    ble_hs_lock();

    conn = ble_hs_conn_find(conn_handle);
    TEST_ASSERT_FATAL(conn != NULL);
    TEST_ASSERT(conn->bhc_rx_chan == NULL);

    ble_hs_unlock();
}

/*****************************************************************************
 * $rx                                                                       *
 *****************************************************************************/

TEST_CASE(ble_l2cap_test_case_bad_header)
{
    int rc;

    ble_l2cap_test_util_init();

    ble_l2cap_test_util_create_conn(2, ((uint8_t[]){1,2,3,4,5,6}),
                                    NULL, NULL);

    rc = ble_l2cap_test_util_rx_first_frag(2, 14, 1234, 10);
    TEST_ASSERT(rc == BLE_HS_ENOENT);
}

/*****************************************************************************
 * $fragmentation                                                            *
 *****************************************************************************/

TEST_CASE(ble_l2cap_test_case_frag_single)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    int rc;

    ble_l2cap_test_util_init();

    ble_l2cap_test_util_create_conn(2, ((uint8_t[]){1,2,3,4,5,6}),
                                    NULL, NULL);

    /*** HCI header specifies middle fragment without start. */
    hci_hdr = BLE_L2CAP_TEST_UTIL_HCI_HDR(2, BLE_HCI_PB_MIDDLE, 10);

    om = ble_hs_mbuf_l2cap_pkt();
    TEST_ASSERT_FATAL(om != NULL);

    om = ble_l2cap_prepend_hdr(om, 0, 5);
    TEST_ASSERT_FATAL(om != NULL);

    rc = ble_hs_test_util_l2cap_rx(2, &hci_hdr, om);
    TEST_ASSERT(rc == BLE_HS_EBADDATA);

    /*** Packet consisting of three fragments. */
    ble_l2cap_test_util_verify_first_frag(2, 10, 30);
    ble_l2cap_test_util_verify_middle_frag(2, 10);
    ble_l2cap_test_util_verify_last_frag(2, 10);

    /*** Packet consisting of five fragments. */
    ble_l2cap_test_util_verify_first_frag(2, 8, 49);
    ble_l2cap_test_util_verify_middle_frag(2, 13);
    ble_l2cap_test_util_verify_middle_frag(2, 2);
    ble_l2cap_test_util_verify_middle_frag(2, 21);
    ble_l2cap_test_util_verify_last_frag(2, 5);
}

TEST_CASE(ble_l2cap_test_case_frag_multiple)
{
    ble_l2cap_test_util_init();

    ble_l2cap_test_util_create_conn(2, ((uint8_t[]){1,2,3,4,5,6}),
                                    NULL, NULL);
    ble_l2cap_test_util_create_conn(3, ((uint8_t[]){2,3,4,5,6,7}),
                                    NULL, NULL);
    ble_l2cap_test_util_create_conn(4, ((uint8_t[]){3,4,5,6,7,8}),
                                    NULL, NULL);

    ble_l2cap_test_util_verify_first_frag(2, 3, 10);
    ble_l2cap_test_util_verify_first_frag(3, 2, 5);
    ble_l2cap_test_util_verify_middle_frag(2, 6);
    ble_l2cap_test_util_verify_first_frag(4, 1, 4);
    ble_l2cap_test_util_verify_middle_frag(3, 2);
    ble_l2cap_test_util_verify_last_frag(3, 1);
    ble_l2cap_test_util_verify_middle_frag(4, 2);
    ble_l2cap_test_util_verify_last_frag(4, 1);
    ble_l2cap_test_util_verify_last_frag(2, 1);
}

TEST_CASE(ble_l2cap_test_case_frag_channels)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_l2cap_test_util_init();

    ble_l2cap_test_util_create_conn(2, ((uint8_t[]){1,2,3,4,5,6}),
                                    NULL, NULL);

    /* Receive a starting fragment on the first channel. */
    rc = ble_l2cap_test_util_rx_first_frag(2, 14, BLE_L2CAP_TEST_CID, 30);
    TEST_ASSERT(rc == 0);

    ble_hs_lock();
    conn = ble_hs_conn_find(2);
    TEST_ASSERT_FATAL(conn != NULL);
    TEST_ASSERT(conn->bhc_rx_chan != NULL &&
                conn->bhc_rx_chan->blc_cid == BLE_L2CAP_TEST_CID);
    ble_hs_unlock();

    /* Receive a starting fragment on a different channel.  The first fragment
     * should get discarded.
     */
    rc = ble_l2cap_test_util_rx_first_frag(2, 14, BLE_L2CAP_CID_ATT, 30);
    TEST_ASSERT(rc == 0);

    ble_hs_lock();
    conn = ble_hs_conn_find(2);
    TEST_ASSERT_FATAL(conn != NULL);
    TEST_ASSERT(conn->bhc_rx_chan != NULL &&
                conn->bhc_rx_chan->blc_cid == BLE_L2CAP_CID_ATT);
    ble_hs_unlock();
}

/*****************************************************************************
 * $unsolicited response                                                     *
 *****************************************************************************/

TEST_CASE(ble_l2cap_test_case_sig_unsol_rsp)
{
    int rc;

    ble_l2cap_test_util_init();

    ble_l2cap_test_util_create_conn(2, ((uint8_t[]){1,2,3,4,5,6}),
                                    NULL, NULL);

    /* Receive an unsolicited response. */
    rc = ble_l2cap_test_util_rx_update_rsp(2, 100, 0);
    TEST_ASSERT(rc == BLE_HS_ENOENT);

    /* Ensure we did not send anything in return. */
    ble_hs_test_util_tx_all();
    TEST_ASSERT_FATAL(ble_hs_test_util_prev_tx_dequeue() == NULL);
}

/*****************************************************************************
 * $update                                                                   *
 *****************************************************************************/

static int
ble_l2cap_test_util_conn_cb(struct ble_gap_event *event, void *arg)
{
    int *accept;

    switch (event->type) {
    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
        accept = arg;
        return !*accept;

    default:
        return 0;
    }
}

static void
ble_l2cap_test_util_peer_updates(int accept)
{
    struct ble_l2cap_sig_update_params l2cap_params;
    struct ble_gap_upd_params params;
    ble_hs_conn_flags_t conn_flags;
    int rc;

    ble_l2cap_test_util_init();

    ble_l2cap_test_util_create_conn(2, ((uint8_t[]){1,2,3,4,5,6}),
                                    ble_l2cap_test_util_conn_cb,
                                    &accept);

    l2cap_params.itvl_min = 0x200;
    l2cap_params.itvl_max = 0x300;
    l2cap_params.slave_latency = 0;
    l2cap_params.timeout_multiplier = 0x100;
    ble_l2cap_test_util_rx_update_req(2, 1, &l2cap_params);

    /* Ensure an update response command got sent. */
    ble_hs_process_tx_data_queue();
    ble_l2cap_test_util_verify_tx_update_rsp(1, !accept);

    if (accept) {
        params.itvl_min = 0x200;
        params.itvl_max = 0x300;
        params.latency = 0;
        params.supervision_timeout = 0x100;
        params.min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN;
        params.max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN;
        ble_l2cap_test_util_verify_tx_update_conn(&params);
    } else {
        /* Ensure no update got scheduled. */
        rc = ble_hs_atomic_conn_flags(2, &conn_flags);
        TEST_ASSERT(rc == 0 && !(conn_flags & BLE_HS_CONN_F_UPDATE));
    }
}

static void
ble_l2cap_test_util_update_cb(int status, void *arg)
{
    ble_l2cap_test_update_status = status;
    ble_l2cap_test_update_arg = arg;
}

static void
ble_l2cap_test_util_we_update(int peer_accepts)
{
    struct ble_l2cap_sig_update_params params;
    uint8_t id;
    int rc;

    ble_l2cap_test_util_init();

    ble_l2cap_test_util_create_conn(2, ((uint8_t[]){1,2,3,4,5,6}),
                                    ble_l2cap_test_util_conn_cb, NULL);

    /* Only the slave can initiate the L2CAP connection update procedure. */
    ble_hs_atomic_conn_set_flags(2, BLE_HS_CONN_F_MASTER, 0);

    params.itvl_min = 0x200;
    params.itvl_min = 0x300;
    params.slave_latency = 0;
    params.timeout_multiplier = 0x100;
    rc = ble_l2cap_sig_update(2, &params, ble_l2cap_test_util_update_cb, NULL);
    TEST_ASSERT_FATAL(rc == 0);

    ble_hs_test_util_tx_all();

    /* Ensure an update request got sent. */
    id = ble_l2cap_test_util_verify_tx_update_req(&params);

    /* Receive response from peer. */
    rc = ble_l2cap_test_util_rx_update_rsp(2, id, !peer_accepts);
    TEST_ASSERT(rc == 0);

    /* Ensure callback got called. */
    if (peer_accepts) {
        TEST_ASSERT(ble_l2cap_test_update_status == 0);
    } else {
        TEST_ASSERT(ble_l2cap_test_update_status == BLE_HS_EREJECT);
    }
    TEST_ASSERT(ble_l2cap_test_update_arg == NULL);
}

TEST_CASE(ble_l2cap_test_case_sig_update_accept)
{
    ble_l2cap_test_util_peer_updates(1);
}

TEST_CASE(ble_l2cap_test_case_sig_update_reject)
{
    ble_l2cap_test_util_peer_updates(0);
}

TEST_CASE(ble_l2cap_test_case_sig_update_init_accept)
{
    ble_l2cap_test_util_we_update(1);
}

TEST_CASE(ble_l2cap_test_case_sig_update_init_reject)
{
    ble_l2cap_test_util_we_update(0);
}

TEST_CASE(ble_l2cap_test_case_sig_update_init_fail_master)
{
    struct ble_l2cap_sig_update_params params;
    int rc;

    ble_l2cap_test_util_init();

    ble_l2cap_test_util_create_conn(2, ((uint8_t[]){1,2,3,4,5,6}),
                                    ble_l2cap_test_util_conn_cb, NULL);

    params.itvl_min = 0x200;
    params.itvl_min = 0x300;
    params.slave_latency = 0;
    params.timeout_multiplier = 0x100;
    rc = ble_l2cap_sig_update(2, &params, ble_l2cap_test_util_update_cb, NULL);
    TEST_ASSERT_FATAL(rc == BLE_HS_EINVAL);

    /* Ensure callback never called. */
    ble_hs_test_util_tx_all();
    TEST_ASSERT(ble_l2cap_test_update_status == -1);
}

TEST_CASE(ble_l2cap_test_case_sig_update_init_fail_bad_id)
{
    struct ble_l2cap_sig_update_params params;
    uint8_t id;
    int rc;

    ble_l2cap_test_util_init();

    ble_l2cap_test_util_create_conn(2, ((uint8_t[]){1,2,3,4,5,6}),
                                    ble_l2cap_test_util_conn_cb, NULL);

    /* Only the slave can initiate the L2CAP connection update procedure. */
    ble_hs_atomic_conn_set_flags(2, BLE_HS_CONN_F_MASTER, 0);

    params.itvl_min = 0x200;
    params.itvl_min = 0x300;
    params.slave_latency = 0;
    params.timeout_multiplier = 0x100;
    rc = ble_l2cap_sig_update(2, &params, ble_l2cap_test_util_update_cb, NULL);
    TEST_ASSERT_FATAL(rc == 0);

    ble_hs_test_util_tx_all();

    /* Ensure an update request got sent. */
    id = ble_l2cap_test_util_verify_tx_update_req(&params);

    /* Receive response from peer with incorrect ID. */
    rc = ble_l2cap_test_util_rx_update_rsp(2, id + 1, 0);
    TEST_ASSERT(rc == BLE_HS_ENOENT);

    /* Ensure callback did not get called. */
    TEST_ASSERT(ble_l2cap_test_update_status == -1);

    /* Receive response from peer with correct ID. */
    rc = ble_l2cap_test_util_rx_update_rsp(2, id, 0);
    TEST_ASSERT(rc == 0);

    /* Ensure callback got called. */
    TEST_ASSERT(ble_l2cap_test_update_status == 0);
    TEST_ASSERT(ble_l2cap_test_update_arg == NULL);
}

TEST_SUITE(ble_l2cap_test_suite)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_l2cap_test_case_bad_header();
    ble_l2cap_test_case_frag_single();
    ble_l2cap_test_case_frag_multiple();
    ble_l2cap_test_case_frag_channels();
    ble_l2cap_test_case_sig_unsol_rsp();
    ble_l2cap_test_case_sig_update_accept();
    ble_l2cap_test_case_sig_update_reject();
    ble_l2cap_test_case_sig_update_init_accept();
    ble_l2cap_test_case_sig_update_init_reject();
    ble_l2cap_test_case_sig_update_init_fail_master();
    ble_l2cap_test_case_sig_update_init_fail_bad_id();
}

int
ble_l2cap_test_all(void)
{
    ble_l2cap_test_suite();

    return tu_any_failed;
}
