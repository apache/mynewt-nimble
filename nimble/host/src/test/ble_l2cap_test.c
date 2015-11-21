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
#include "ble_hs_conn.h"
#include "ble_hs_test_util.h"
#include "ble_l2cap.h"
#include "testutil/testutil.h"

TEST_CASE(l2cap_test_bad_header)
{
    struct ble_l2cap_hdr l2cap_hdr;
    struct hci_data_hdr hci_hdr;
    struct ble_hs_conn *conn;
    struct os_mbuf *om;
    int rc;

    ble_hs_test_util_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}));
    conn = ble_hs_conn_find(2);
    TEST_ASSERT_FATAL(conn != NULL);

    hci_hdr.hdh_handle_pb_bc = 0;
    hci_hdr.hdh_len = 10;

    /*** HCI header indicates a length of 10, but L2CAP header has a length
     *   of 0.
     */
    om = os_mbuf_get_pkthdr(&ble_hs_mbuf_pool);
    TEST_ASSERT_FATAL(om != NULL);

    l2cap_hdr.blh_len = 0;
    l2cap_hdr.blh_cid = 0;

    om = ble_l2cap_prepend_hdr(om, &l2cap_hdr);
    TEST_ASSERT_FATAL(om != NULL);

    rc = ble_l2cap_rx(conn, &hci_hdr, om);
    TEST_ASSERT(rc == EMSGSIZE);

    os_mbuf_free_chain(&ble_hs_mbuf_pool, om);

    /*** Length is correct; specified channel doesn't exist. */
    om = os_mbuf_get_pkthdr(&ble_hs_mbuf_pool);
    TEST_ASSERT_FATAL(om != NULL);

    l2cap_hdr.blh_len = 6;
    l2cap_hdr.blh_cid = 0;

    om = ble_l2cap_prepend_hdr(om, &l2cap_hdr);
    TEST_ASSERT_FATAL(om != NULL);

    rc = ble_l2cap_rx(conn, &hci_hdr, om);
    TEST_ASSERT(rc == ENOENT);

    os_mbuf_free_chain(&ble_hs_mbuf_pool, om);
}

TEST_SUITE(l2cap_gen)
{
    l2cap_test_bad_header();
}

int
l2cap_test_all(void)
{
    l2cap_gen();

    return tu_any_failed;
}

