/*
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

#include <stdint.h>
#include <controller/ble_ll_iso.h>
#include <os/os_mbuf.h>
#include <nimble/ble.h>
#include <nimble/hci_common.h>
#include <testutil/testutil.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#define TSPX_max_tx_nse     3
#define TSPX_max_tx_payload 32

#define TEST_BUF_SIZE                                                         \
    (sizeof(struct os_mbuf) + sizeof(struct os_mbuf_pkthdr) +                 \
     sizeof(struct ble_mbuf_hdr) + 64)
#define TEST_BUF_COUNT (10)

static struct os_mbuf_pool g_test_mbuf_pool;
static struct os_mempool g_test_mempool;
static os_membuf_t g_test_mbuf_buffer[OS_MEMPOOL_SIZE(TEST_BUF_COUNT, TEST_BUF_SIZE)];

/* LL.TS.p24 4.11.2 Common Parameters */
struct test_ll_common_params {
    uint8_t TxNumBIS;
    uint8_t RxNumBIS;
    uint8_t NumDataPDUs;
    uint8_t RTN;
    uint8_t NSE;
    uint8_t IRC;
    uint8_t PTO;
    uint8_t BN;
    uint8_t Transport_Latency;
    uint8_t SDU_Interval;
    uint8_t ISO_Interval;
    uint8_t BIG_Sync_Timeout;
    uint8_t Data_Size;
    uint8_t PHY;
    uint8_t Packing;
    uint8_t Framing;
    uint8_t Encryption;
    uint8_t PADV_Interval;
    uint8_t Sync_Timeout;
};

const struct test_ll_common_params test_ll_common_params_bn_1 = {
    .TxNumBIS = 1,
    .RxNumBIS = 1,
    .NumDataPDUs = 20,
    .RTN = TSPX_max_tx_nse,
    .NSE = TSPX_max_tx_nse,
    .IRC = TSPX_max_tx_nse,
    .PTO = 0,
    .BN = 1,
    .Transport_Latency = 20,
    .SDU_Interval = 10,
    .ISO_Interval = 10,
    .BIG_Sync_Timeout = 100,
    .Data_Size = 0,
    .PHY = 0x01,
    .Packing = 0x00,
    .Framing = 0x00,
    .Encryption = 0x00,
    .PADV_Interval = 20,
    .Sync_Timeout = 100,
};

struct test_ll_iso_fixture {
    struct ble_ll_iso_conn conn;
};

static void
test_ll_iso_setup(struct test_ll_iso_fixture *fixture,
                  const struct test_ll_common_params *params)
{
    struct ble_ll_iso_conn_init_param conn_param = {
        .iso_interval_us = params->SDU_Interval * 1000,
        .sdu_interval_us = params->SDU_Interval * 1000,
        .conn_handle = 0x0001,
        .max_sdu = TSPX_max_tx_payload,
        .max_pdu = TSPX_max_tx_payload,
        .framing = params->Framing,
        .bn = params->BN
    };
    struct ble_ll_iso_conn *conn;

    memset(fixture, 0, sizeof(*fixture));
    conn = &fixture->conn;

    ble_ll_iso_conn_init(conn, &conn_param);
}

static void
test_ll_iso_teardown(struct test_ll_iso_fixture *fixture)
{
    struct ble_ll_iso_conn *conn;

    conn = &fixture->conn;

    ble_ll_iso_conn_free(conn);
}

TEST_CASE_SELF(test_ll_ist_brd_bv_01_c)
{
    const uint8_t payload_types[] = { BLE_HCI_PAYLOAD_TYPE_ZERO_LENGTH,
                                      BLE_HCI_PAYLOAD_TYPE_VARIABLE_LENGTH,
                                      BLE_HCI_PAYLOAD_TYPE_MAXIMUM_LENGTH };
    const struct test_ll_common_params *params = &test_ll_common_params_bn_1;
    struct ble_hci_le_setup_iso_data_path_cp setup_iso_data_path_cp;
    struct ble_hci_le_setup_iso_data_path_rp setup_iso_data_path_rp;
    struct ble_hci_le_iso_transmit_test_cp iso_transmit_test_cp;
    struct ble_hci_le_iso_transmit_test_rp iso_transmit_test_rp;
    struct ble_hci_le_iso_test_end_cp iso_test_end_cp;
    struct ble_hci_le_iso_test_end_rp iso_test_end_rp;
    struct test_ll_iso_fixture fixture;
    struct ble_ll_iso_conn *conn;
    uint8_t payload_type;
    uint8_t pdu[100];
    uint8_t llid;
    uint8_t rsplen = 0;
    int rc;

    test_ll_iso_setup(&fixture, params);

    conn = &fixture.conn;

    for (uint8_t i = 0; i < ARRAY_SIZE(payload_types); i++) {
        payload_type = payload_types[i];

        /* 2. The Upper Tester sends the HCI_LE_ISO_Transmit_Test command with Payload_Type as
         *    specified in Table 4.12-2 and receives a successful HCI_Command_Complete event from the IUT in response.
         */
        rsplen = 0xFF;
        iso_transmit_test_cp.conn_handle = htole16(conn->handle);
        iso_transmit_test_cp.payload_type = payload_type;
        rc = ble_ll_iso_transmit_test((uint8_t *)&iso_transmit_test_cp,
                                      sizeof(iso_transmit_test_cp),
                                      (uint8_t *)&iso_transmit_test_rp, &rsplen);
        TEST_ASSERT(rc == 0);
        TEST_ASSERT(rsplen == sizeof(iso_transmit_test_rp));
        TEST_ASSERT(iso_transmit_test_rp.conn_handle == iso_transmit_test_cp.conn_handle);

        /* 3. The IUT sends isochronous data PDUs with Payload as specified in Table 4.12-2. The SDU
         *    Count value meets the requirements for unframed PDUs as specified in [14] Section 7.1.
         * 4. Repeat step 3 for a total of 5 payloads.
         */
        for (uint8_t j = 0; j < 5; j++) {
            rc = ble_ll_iso_conn_event_start(conn, 30000);
            TEST_ASSERT(rc == 0);

            for (uint8_t k = 0; k < conn->mux.bn; k++) {
                llid = 0xFF;
                rc = ble_ll_iso_pdu_get(conn, k, k, &llid, pdu);
                if (payload_type == BLE_HCI_PAYLOAD_TYPE_ZERO_LENGTH) {
                    TEST_ASSERT(rc == 0);
                    TEST_ASSERT(llid == 0b00);
                } else if (payload_type == BLE_HCI_PAYLOAD_TYPE_VARIABLE_LENGTH) {
                    TEST_ASSERT(rc >= 4);
                    TEST_ASSERT(llid == 0b00);
                } else if (payload_type == BLE_HCI_PAYLOAD_TYPE_MAXIMUM_LENGTH) {
                    TEST_ASSERT(rc == conn->mux.max_pdu);
                    TEST_ASSERT(llid == 0b00);
                }
            }

            rc = ble_ll_iso_conn_event_done(conn);
            TEST_ASSERT(rc == 0);
        }

        /* 5. The Upper Tester sends an HCI_LE_Setup_ISO_Data_Path command to the IUT.
         * 6. The IUT sends an HCI_Command_Complete event to the Upper Tester with Status set to 0x0C.
         */
        setup_iso_data_path_cp.conn_handle = htole16(conn->handle);
        setup_iso_data_path_cp.data_path_dir = 0x00;
        setup_iso_data_path_cp.data_path_id = 0x00;
        rc = ble_ll_iso_setup_iso_data_path(
            (uint8_t *)&setup_iso_data_path_cp, sizeof(setup_iso_data_path_cp),
            (uint8_t *)&setup_iso_data_path_rp, &rsplen);
        TEST_ASSERT(rc == 0x0C);

        /* 7. The Upper Tester sends the HCI_LE_ISO_Test_End command to the IUT and receives an
         *    HCI_Command_Status event from the IUT with the Status field set to Success. The returned
         *    Received_SDU_Count, Missed_SDU_Count, and Failed_SDU_Count are all zero.
         */
        rsplen = 0xFF;
        iso_test_end_cp.conn_handle = htole16(conn->handle);
        rc = ble_ll_iso_end_test((uint8_t *)&iso_test_end_cp, sizeof(iso_test_end_cp),
                                 (uint8_t *)&iso_test_end_rp, &rsplen);
        TEST_ASSERT(rc == 0);
        TEST_ASSERT(rsplen == sizeof(iso_test_end_rp));
        TEST_ASSERT(iso_test_end_rp.conn_handle == iso_test_end_cp.conn_handle);
        TEST_ASSERT(iso_test_end_rp.received_sdu_count == 0);
        TEST_ASSERT(iso_test_end_rp.missed_sdu_count == 0);
        TEST_ASSERT(iso_test_end_rp.failed_sdu_count == 0);
    }

    test_ll_iso_teardown(&fixture);
}

static void
test_ll_iso_data_in_init(void)
{
    int rc;

    rc = os_mempool_init(&g_test_mempool, TEST_BUF_COUNT, TEST_BUF_SIZE,
                         &g_test_mbuf_buffer[0], "test_iso_pool");
    TEST_ASSERT_FATAL(rc == 0);

    rc = os_mbuf_pool_init(&g_test_mbuf_pool, &g_test_mempool, TEST_BUF_SIZE,
                           TEST_BUF_COUNT);
    TEST_ASSERT_FATAL(rc == 0);
}

static int
test_ll_iso_data_in(uint16_t conn_handle, uint8_t pb_flag, uint16_t data_len)
{
    struct ble_hci_iso_data hci_iso_data;
    struct ble_hci_iso hci_iso;
    struct os_mbuf *om;
    uint8_t data = 0;
    uint16_t len;
    int rc;

    om = os_mbuf_get_pkthdr(&g_test_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(om != NULL);

    len = data_len;
    if ((pb_flag == BLE_HCI_ISO_PB_FIRST) || (pb_flag == BLE_HCI_ISO_PB_COMPLETE)) {
        len += sizeof(hci_iso_data);
    }

    hci_iso.handle = htole16(BLE_HCI_ISO_HANDLE(conn_handle, pb_flag, 0));
    hci_iso.length = htole16(len);
    rc = os_mbuf_append(om, &hci_iso, sizeof(hci_iso));
    TEST_ASSERT_FATAL(rc == 0);

    if ((pb_flag == BLE_HCI_ISO_PB_FIRST) || (pb_flag == BLE_HCI_ISO_PB_COMPLETE)) {
        hci_iso_data.packet_seq_num = 0;
        hci_iso_data.sdu_len = htole16(data_len);
        rc = os_mbuf_append(om, &hci_iso_data, sizeof(hci_iso_data));
        TEST_ASSERT_FATAL(rc == 0);
    }

    while (data_len--) {
        rc = os_mbuf_append(om, &data, 1);
        TEST_ASSERT_FATAL(rc == 0);
    }

    return ble_ll_iso_data_in(om);
}

static void
test_ll_iso_data_in_teardown(struct test_ll_iso_fixture *fixture)
{
    test_ll_iso_teardown(fixture);

    /* Memory leak test */
    TEST_ASSERT(g_test_mempool.mp_num_free == TEST_BUF_COUNT,
                "mp_num_free is %d", g_test_mempool.mp_num_free);
}

TEST_CASE_SELF(test_ll_iso_data_in_continuation_without_first)
{
    struct test_ll_iso_fixture fixture;
    int rc;

    test_ll_iso_setup(&fixture, &test_ll_common_params_bn_1);

    rc = test_ll_iso_data_in(fixture.conn.handle, BLE_HCI_ISO_PB_CONTINUATION, 4);
    TEST_ASSERT(rc == BLE_ERR_INV_HCI_CMD_PARMS);

    test_ll_iso_data_in_teardown(&fixture);
}

TEST_CASE_SELF(test_ll_iso_data_in_last_without_first)
{
    struct test_ll_iso_fixture fixture;
    int rc;

    test_ll_iso_setup(&fixture, &test_ll_common_params_bn_1);

    rc = test_ll_iso_data_in(fixture.conn.handle, BLE_HCI_ISO_PB_LAST, 4);
    TEST_ASSERT(rc == BLE_ERR_INV_HCI_CMD_PARMS);

    test_ll_iso_data_in_teardown(&fixture);
}

TEST_CASE_SELF(test_ll_iso_data_in_first_while_pending)
{
    struct test_ll_iso_fixture fixture;
    int rc;

    test_ll_iso_setup(&fixture, &test_ll_common_params_bn_1);

    rc = test_ll_iso_data_in(fixture.conn.handle, BLE_HCI_ISO_PB_FIRST, 4);
    TEST_ASSERT(rc == 0);

    /* Pending fragment shall be dropped */
    rc = test_ll_iso_data_in(fixture.conn.handle, BLE_HCI_ISO_PB_FIRST, 4);
    TEST_ASSERT(rc == 0);

    rc = test_ll_iso_data_in(fixture.conn.handle, BLE_HCI_ISO_PB_LAST, 4);
    TEST_ASSERT(rc == 0);

    /* Pending fragment shall be dropped */
    rc = test_ll_iso_data_in(fixture.conn.handle, BLE_HCI_ISO_PB_FIRST, 4);
    TEST_ASSERT(rc == 0);

    rc = test_ll_iso_data_in(fixture.conn.handle, BLE_HCI_ISO_PB_COMPLETE, 4);
    TEST_ASSERT(rc == 0);

    test_ll_iso_data_in_teardown(&fixture);
}

TEST_CASE_SELF(test_ll_iso_data_in_pending_on_free)
{
    struct test_ll_iso_fixture fixture;
    int rc;

    test_ll_iso_setup(&fixture, &test_ll_common_params_bn_1);

    rc = test_ll_iso_data_in(fixture.conn.handle, BLE_HCI_ISO_PB_FIRST, 4);
    TEST_ASSERT(rc == 0);

    /* Pending fragment shall be freed with ISO connection */
    test_ll_iso_data_in_teardown(&fixture);
}

TEST_CASE_SELF(test_ll_iso_data_in_short_header)
{
    struct test_ll_iso_fixture fixture;
    struct os_mbuf *om;
    uint8_t hdr[4];
    int rc;

    test_ll_iso_setup(&fixture, &test_ll_common_params_bn_1);

    om = os_mbuf_get_pkthdr(&g_test_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(om != NULL);

    put_le16(&hdr[0],
             BLE_HCI_ISO_HANDLE(fixture.conn.handle, BLE_HCI_ISO_PB_COMPLETE, 0));
    put_le16(&hdr[2], sizeof(struct ble_hci_iso_data));
    rc = os_mbuf_append(om, hdr, sizeof(hdr));
    TEST_ASSERT_FATAL(rc == 0);

    /* Only handle is included in packet */
    os_mbuf_adj(om, -2);

    rc = ble_ll_iso_data_in(om);
    TEST_ASSERT(rc == BLE_ERR_INV_HCI_CMD_PARMS);

    test_ll_iso_data_in_teardown(&fixture);
}

TEST_SUITE(ble_ll_iso_test_suite)
{
    ble_ll_iso_init();

    test_ll_iso_data_in_init();

    test_ll_ist_brd_bv_01_c();

    test_ll_iso_data_in_continuation_without_first();
    test_ll_iso_data_in_last_without_first();
    test_ll_iso_data_in_first_while_pending();
    test_ll_iso_data_in_pending_on_free();
    test_ll_iso_data_in_short_header();

    ble_ll_iso_reset();
}
