#include <stddef.h>
#include <errno.h>
#include "nimble/hci_common.h"
#include "ble_hs_conn.h"
#include "ble_l2cap.h"
#include "host/host_task.h"
#include "host/host_test.h"
#include "testutil/testutil.h"

TEST_CASE(l2cap_test_bad_header)
{
    struct ble_l2cap_hdr l2cap_hdr;
    struct hci_data_hdr hci_hdr;
    struct ble_hs_conn *conn;
    uint8_t pkt[8];
    int rc;

    conn = ble_hs_conn_alloc();
    TEST_ASSERT_FATAL(conn != NULL);

    hci_hdr.hdh_handle_pb_bc = 0;
    hci_hdr.hdh_len = 10;

    /* HCI header indicates a length of 10, but L2CAP header has a length
     * of 0.
     */
    l2cap_hdr.blh_len = 0;
    l2cap_hdr.blh_cid = 0;
    rc = ble_l2cap_write_hdr(pkt, sizeof pkt, &l2cap_hdr);
    TEST_ASSERT(rc == 0);
    rc = ble_l2cap_rx(conn, &hci_hdr, pkt);
    TEST_ASSERT(rc == EMSGSIZE);

    /* Length is correct; specified channel doesn't exist. */
    l2cap_hdr.blh_len = 6;
    l2cap_hdr.blh_cid = 0;
    rc = ble_l2cap_write_hdr(pkt, sizeof pkt, &l2cap_hdr);
    TEST_ASSERT(rc == 0);
    rc = ble_l2cap_rx(conn, &hci_hdr, pkt);
    TEST_ASSERT(rc == ENOENT);

    ble_hs_conn_free(conn);
}

TEST_SUITE(l2cap_gen)
{
    int rc;

    rc = host_init();
    TEST_ASSERT_FATAL(rc == 0);

    l2cap_test_bad_header();
}

int
l2cap_test_all(void)
{
    l2cap_gen();

    return tu_any_failed;
}

#ifdef PKG_TEST

int
main(void)
{
    tu_config.tc_print_results = 1;
    tu_init();

    l2cap_test_all();

    return tu_any_failed;
}

#endif

