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
#include "ble_hs_priv.h"
#include "host/host_hci.h"
#include "host/ble_hs_test.h"
#include "ble_hs_conn.h"
#include "ble_hci_sched.h"
#include "ble_hs_test_util.h"
#include "ble_l2cap_priv.h"
#include "testutil/testutil.h"

#define BLE_L2CAP_TEST_CID  99

#define BLE_L2CAP_TEST_UTIL_HCI_HDR(handle, pb, len)    \
    ((struct hci_data_hdr) {                            \
        .hdh_handle_pb_bc = ((handle)  << 0) |          \
                            ((pb)      << 12),          \
        .hdh_len = (len)                                \
    })

static int
ble_l2cap_test_util_rx(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                       struct os_mbuf **om)
{
    return 0;
}

static struct ble_hs_conn *
ble_l2cap_test_create_conn(uint16_t handle, uint8_t *addr)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;

    conn = ble_hs_test_util_create_conn(handle, addr, NULL, NULL);

    chan = ble_l2cap_chan_alloc();
    TEST_ASSERT_FATAL(chan != NULL);

    chan->blc_cid = BLE_L2CAP_TEST_CID;
    chan->blc_my_mtu = 240;
    chan->blc_default_mtu = 240;
    chan->blc_rx_fn = ble_l2cap_test_util_rx;

    SLIST_INSERT_HEAD(&conn->bhc_channels, chan, blc_next);

    return conn;
}

static int
ble_l2cap_test_rx_first_frag(struct ble_hs_conn *conn,
                             uint16_t l2cap_frag_len,
                             uint16_t cid, uint16_t l2cap_len)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    uint16_t hci_len;
    void *v;
    int rc;

    om = ble_att_get_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    v = os_mbuf_extend(om, l2cap_frag_len);
    TEST_ASSERT_FATAL(v != NULL);

    om = ble_l2cap_prepend_hdr(om, cid, l2cap_len);
    TEST_ASSERT_FATAL(om != NULL);

    hci_len = sizeof hci_hdr + l2cap_frag_len;
    hci_hdr = BLE_L2CAP_TEST_UTIL_HCI_HDR(conn->bhc_handle,
                                          BLE_HCI_PB_FIRST_FLUSH, hci_len);
    rc = ble_l2cap_rx(conn, &hci_hdr, om);
    return rc;
}

static int
ble_l2cap_test_rx_next_frag(struct ble_hs_conn *conn, uint16_t hci_len)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    void *v;
    int rc;

    om = ble_att_get_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    v = os_mbuf_extend(om, hci_len);
    TEST_ASSERT_FATAL(v != NULL);

    hci_hdr = BLE_L2CAP_TEST_UTIL_HCI_HDR(conn->bhc_handle,
                                          BLE_HCI_PB_MIDDLE, hci_len);
    rc = ble_l2cap_rx(conn, &hci_hdr, om);
    return rc;
}

static void
ble_l2cap_test_verify_first_frag(struct ble_hs_conn *conn,
                                 uint16_t l2cap_frag_len,
                                 uint16_t l2cap_len)
{
    int rc;

    rc = ble_l2cap_test_rx_first_frag(conn, l2cap_frag_len,
                                      BLE_L2CAP_TEST_CID, l2cap_len);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(conn->bhc_rx_chan != NULL &&
                conn->bhc_rx_chan->blc_cid == BLE_L2CAP_TEST_CID);
}

static void
ble_l2cap_test_verify_middle_frag(struct ble_hs_conn *conn, uint16_t hci_len)
{
    int rc;

    rc = ble_l2cap_test_rx_next_frag(conn, hci_len);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(conn->bhc_rx_chan != NULL &&
                conn->bhc_rx_chan->blc_cid == BLE_L2CAP_TEST_CID);
}

static void
ble_l2cap_test_verify_last_frag(struct ble_hs_conn *conn, uint16_t hci_len)
{
    int rc;

    rc = ble_l2cap_test_rx_next_frag(conn, hci_len);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(conn->bhc_rx_chan == NULL);
}

TEST_CASE(ble_l2cap_test_case_bad_header)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_test_util_init();

    conn = ble_l2cap_test_create_conn(2, ((uint8_t[]){1,2,3,4,5,6}));

    rc = ble_l2cap_test_rx_first_frag(conn, 14, 1234, 10);
    TEST_ASSERT(rc == BLE_HS_ENOENT);
}

TEST_CASE(ble_l2cap_test_case_frag_single)
{
    struct hci_data_hdr hci_hdr;
    struct ble_hs_conn *conn;
    struct os_mbuf *om;
    int rc;

    ble_hs_test_util_init();

    conn = ble_l2cap_test_create_conn(2, ((uint8_t[]){1,2,3,4,5,6}));

    /*** HCI header specifies middle fragment without start. */
    hci_hdr = BLE_L2CAP_TEST_UTIL_HCI_HDR(2, BLE_HCI_PB_MIDDLE, 10);

    om = ble_att_get_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    om = ble_l2cap_prepend_hdr(om, 0, 5);
    TEST_ASSERT_FATAL(om != NULL);

    rc = ble_l2cap_rx(conn, &hci_hdr, om);
    TEST_ASSERT(rc == BLE_HS_EBADDATA);

    /*** Packet consisting of three fragments. */
    ble_l2cap_test_verify_first_frag(conn, 10, 30);
    ble_l2cap_test_verify_middle_frag(conn, 10);
    ble_l2cap_test_verify_last_frag(conn, 10);

    /*** Packet consisting of five fragments. */
    ble_l2cap_test_verify_first_frag(conn, 8, 49);
    ble_l2cap_test_verify_middle_frag(conn, 13);
    ble_l2cap_test_verify_middle_frag(conn, 2);
    ble_l2cap_test_verify_middle_frag(conn, 21);
    ble_l2cap_test_verify_last_frag(conn, 5);
}

TEST_CASE(ble_l2cap_test_case_frag_multiple)
{
    struct ble_hs_conn *conns[3];

    ble_hs_test_util_init();

    conns[0] = ble_l2cap_test_create_conn(2, ((uint8_t[]){1,2,3,4,5,6}));
    conns[1] = ble_l2cap_test_create_conn(3, ((uint8_t[]){2,3,4,5,6,7}));
    conns[2] = ble_l2cap_test_create_conn(4, ((uint8_t[]){3,4,5,6,7,8}));

    ble_l2cap_test_verify_first_frag(conns[0], 3, 10);
    ble_l2cap_test_verify_first_frag(conns[1], 2, 5);
    ble_l2cap_test_verify_middle_frag(conns[0], 6);
    ble_l2cap_test_verify_first_frag(conns[2], 1, 4);
    ble_l2cap_test_verify_middle_frag(conns[1], 2);
    ble_l2cap_test_verify_last_frag(conns[1], 1);
    ble_l2cap_test_verify_middle_frag(conns[2], 2);
    ble_l2cap_test_verify_last_frag(conns[2], 1);
    ble_l2cap_test_verify_last_frag(conns[0], 1);
}

TEST_CASE(ble_l2cap_test_case_frag_channels)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_test_util_init();

    conn = ble_l2cap_test_create_conn(2, ((uint8_t[]){1,2,3,4,5,6}));

    /* Receive a starting fragment on the first channel. */
    rc = ble_l2cap_test_rx_first_frag(conn, 14, BLE_L2CAP_TEST_CID, 30);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(conn->bhc_rx_chan != NULL &&
                conn->bhc_rx_chan->blc_cid == BLE_L2CAP_TEST_CID);

    /* Receive a starting fragment on a different channel.  The first fragment
     * should get discarded.
     */
    rc = ble_l2cap_test_rx_first_frag(conn, 14, BLE_L2CAP_CID_ATT, 30);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(conn->bhc_rx_chan != NULL &&
                conn->bhc_rx_chan->blc_cid == BLE_L2CAP_CID_ATT);
}

static void
ble_l2cap_test_util_verify_tx_update_conn(
    struct ble_gap_conn_upd_params *params)
{
    uint8_t param_len;
    uint8_t *param;

    TEST_ASSERT_FATAL(ble_hs_test_util_prev_hci_tx != NULL);

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

TEST_CASE(ble_l2cap_test_case_sig_update)
{
    struct ble_gap_conn_upd_params params;
    struct ble_hs_conn *conn;
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    int rc;

    ble_hs_test_util_init();

    conn = ble_l2cap_test_create_conn(2, ((uint8_t[]){1,2,3,4,5,6}));

    uint8_t pkt[] = {
        BLE_L2CAP_SIG_OP_UPDATE_REQ,    /* Op. */
        1,                              /* Identifier. */
        0x08, 0x00,                     /* Length (8). */
        0x00, 0x02,                     /* Interval min (0x200). */
        0x00, 0x03,                     /* Interval max (0x300). */
        0x00, 0x00,                     /* Slave latency (0). */
        0x00, 0x01,                     /* Timeout multiplier (0x100). */
    };

    hci_hdr = BLE_L2CAP_TEST_UTIL_HCI_HDR(
        2, BLE_HCI_PB_FIRST_FLUSH,
        BLE_L2CAP_HDR_SZ + BLE_L2CAP_SIG_HDR_SZ + sizeof pkt);

    om = ble_att_get_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    om = ble_l2cap_prepend_hdr(om, BLE_L2CAP_CID_SIG, sizeof pkt);
    TEST_ASSERT_FATAL(om != NULL);

    rc = os_mbuf_append(om, pkt, sizeof pkt);
    TEST_ASSERT_FATAL(rc == 0);

    rc = ble_l2cap_rx(conn, &hci_hdr, om);
    TEST_ASSERT_FATAL(rc == 0);

    /* Ensure an update response command got sent. */
    ble_hs_process_tx_data_queue();
    TEST_ASSERT_FATAL(ble_hs_test_util_prev_tx != NULL);
    TEST_ASSERT(OS_MBUF_PKTLEN(ble_hs_test_util_prev_tx) ==
                BLE_L2CAP_SIG_HDR_SZ + BLE_L2CAP_SIG_UPDATE_RSP_SZ);
    TEST_ASSERT(ble_hs_test_util_prev_tx->om_data[0] ==
                BLE_L2CAP_SIG_OP_UPDATE_RSP);
    TEST_ASSERT(ble_hs_test_util_prev_tx->om_data[1] == 1);
    TEST_ASSERT(le16toh(ble_hs_test_util_prev_tx->om_data + 2) == 2);
    TEST_ASSERT(le16toh(ble_hs_test_util_prev_tx->om_data + 4) == 0);

    /* Ensure update request gets sent. */
    ble_gattc_wakeup();
    ble_hci_sched_wakeup();

    params.itvl_min = 0x200;
    params.itvl_max = 0x300;
    params.latency = 0;
    params.supervision_timeout = 0x100;
    params.min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN;
    params.max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN;

    ble_l2cap_test_util_verify_tx_update_conn(&params);
}

TEST_SUITE(ble_l2cap_test_suite)
{
    ble_l2cap_test_case_bad_header();
    ble_l2cap_test_case_frag_single();
    ble_l2cap_test_case_frag_multiple();
    ble_l2cap_test_case_frag_channels();
    ble_l2cap_test_case_sig_update();
}

int
ble_l2cap_test_all(void)
{
    ble_l2cap_test_suite();

    return tu_any_failed;
}
