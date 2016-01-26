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
#include "ble_hs_test_util.h"
#include "ble_l2cap.h"
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


TEST_SUITE(ble_l2cap_test_suite)
{
    ble_l2cap_test_case_bad_header();
    ble_l2cap_test_case_frag_single();
    ble_l2cap_test_case_frag_multiple();
    ble_l2cap_test_case_frag_channels();
}

int
ble_l2cap_test_all(void)
{
    ble_l2cap_test_suite();

    return tu_any_failed;
}
