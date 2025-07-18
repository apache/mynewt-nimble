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

#include "../../src/ble_ll_iso_priv.h"
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

#define TSPX_max_tx_nse        3
#define TSPX_max_tx_payload    32

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
    struct ble_ll_iso_params params;
    struct ble_ll_iso_conn conn;
    struct ble_ll_iso_rx rx;
    struct ble_ll_iso_tx tx;
};

static void
test_ll_iso_setup(struct test_ll_iso_fixture *fixture,
                  const struct test_ll_common_params *params)
{
    struct ble_ll_iso_params *iso_params;
    struct ble_ll_iso_conn *conn;
    struct ble_ll_iso_rx *rx;
    struct ble_ll_iso_tx *tx;

    memset(fixture, 0, sizeof(*fixture));
    iso_params = &fixture->params;
    conn = &fixture->conn;
    rx = &fixture->rx;
    tx = &fixture->tx;

    iso_params->iso_interval = params->ISO_Interval * 1000 / 1250;
    iso_params->sdu_interval = params->SDU_Interval * 1000;
    iso_params->max_sdu = TSPX_max_tx_payload;
    iso_params->max_pdu = TSPX_max_tx_payload;
    iso_params->bn = params->BN;
    iso_params->pte = 0;
    iso_params->framed = params->Framing != 0x00;
    iso_params->framing_mode = params->Framing == 0x02;

    ble_ll_iso_conn_init(conn, 0x0001, rx, tx);
    ble_ll_iso_rx_init(rx, conn, iso_params);
    ble_ll_iso_tx_init(tx, conn, iso_params);
}

static void
test_ll_iso_teardown(struct test_ll_iso_fixture *fixture)
{
    struct ble_ll_iso_conn *conn;

    conn = &fixture->conn;

    ble_ll_iso_conn_reset(conn);
}

TEST_CASE_SELF(test_ll_ist_brd_bv_01_c) {
    const uint8_t payload_types[] = {
        BLE_HCI_PAYLOAD_TYPE_ZERO_LENGTH,
        BLE_HCI_PAYLOAD_TYPE_VARIABLE_LENGTH,
        BLE_HCI_PAYLOAD_TYPE_MAXIMUM_LENGTH
    };
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

        /* 2. The Upper Tester sends the HCI_LE_ISO_Transmit_Test command with
         * Payload_Type as specified in Table 4.12-2 and receives a successful
         * HCI_Command_Complete event from the IUT in response.
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
            rc = ble_ll_iso_tx_event_start(conn->tx, 30000);
            TEST_ASSERT(rc == 0);

            for (uint8_t k = 0; k < conn->tx->params->bn; k++) {
                llid = 0xFF;
                rc = ble_ll_iso_tx_pdu_get(conn->tx, k, k, &llid, pdu);
                if (payload_type == BLE_HCI_PAYLOAD_TYPE_ZERO_LENGTH) {
                    TEST_ASSERT(rc == 0);
                    TEST_ASSERT(llid == 0b00);
                } else if (payload_type == BLE_HCI_PAYLOAD_TYPE_VARIABLE_LENGTH) {
                    TEST_ASSERT(rc >= 4);
                    TEST_ASSERT(llid == 0b00);
                } else if (payload_type == BLE_HCI_PAYLOAD_TYPE_MAXIMUM_LENGTH) {
                    TEST_ASSERT(rc == conn->tx->params->max_pdu);
                    TEST_ASSERT(llid == 0b00);
                }
            }

            rc = ble_ll_iso_tx_event_done(conn->tx);
            TEST_ASSERT(rc == 0);
        }

        /* 5. The Upper Tester sends an HCI_LE_Setup_ISO_Data_Path command to the IUT.
         * 6. The IUT sends an HCI_Command_Complete event to the Upper Tester with Status set to
         *    0x0C.
         */
        memset(&setup_iso_data_path_cp, 0, sizeof(setup_iso_data_path_cp));
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

struct test_ll_iso_receive_test_mode_round {
    uint8_t framing;
    uint8_t payload_type;
};

TEST_CASE_SELF(test_ll_ist_snc_bv_01_c)
{
    const struct test_ll_iso_receive_test_mode_round rounds[] = {
        { 0, 0 },
        { 0, 1 },
        { 0, 2 },
        { 1, 2 },
        { 1, 1 }
    };
    const struct test_ll_common_params *params = &test_ll_common_params_bn_1;
    struct ble_hci_le_iso_read_test_counters_cp iso_read_test_counters_cp;
    struct ble_hci_le_iso_read_test_counters_rp iso_read_test_counters_rp;
    struct ble_hci_le_setup_iso_data_path_cp setup_iso_data_path_cp;
    struct ble_hci_le_setup_iso_data_path_rp setup_iso_data_path_rp;
    struct ble_hci_le_iso_receive_test_cp iso_receive_test_cp;
    struct ble_hci_le_iso_receive_test_rp iso_receive_test_rp;
    struct ble_hci_le_iso_test_end_cp iso_test_end_cp;
    struct ble_hci_le_iso_test_end_rp iso_test_end_rp;
    struct test_ll_iso_fixture fixture;
    struct ble_ll_iso_conn *conn;
    uint8_t payload_type;
    uint8_t rsplen = 0;
    int rc;

    test_ll_iso_setup(&fixture, params);

    conn = &fixture.conn;

    for (uint8_t i = 0; i < ARRAY_SIZE(rounds); i++) {
        payload_type = rounds[i].payload_type;

        /* 3. The Upper Tester sends the HCI_LE_ISO_Receive_Test command to the
         * IUT with Payload_Type as specified in Table 4.12-4 and receives a
         * successful HCI_Command_Complete event from the IUT in response.
         */
        rsplen = 0xFF;
        iso_receive_test_cp.conn_handle = htole16(conn->handle);
        iso_receive_test_cp.payload_type = payload_type;
        rc = ble_ll_iso_receive_test((uint8_t *)&iso_receive_test_cp,
                                     sizeof(iso_receive_test_cp),
                                     (uint8_t *)&iso_receive_test_rp, &rsplen);
        TEST_ASSERT(rc == 0);
        TEST_ASSERT(rsplen == sizeof(iso_receive_test_rp));
        TEST_ASSERT(iso_receive_test_rp.conn_handle == iso_receive_test_cp.conn_handle);

        /* 6. Repeat steps 4 and 5 for a total of 5 payloads. */
        for (uint8_t j = 0; j < 5; j++) {
            /* 4. TODO The Lower Tester sends an SDU as specified in Table 4.12-4. */
            /* 5. TODO The IUT receives the expected test Payload PDU as specified in Table. */
        }

        /* 7. The Upper Tester sends the HCI_LE_ISO_Read_Test_Counters command to the IUT. */
        rsplen = 0xFF;
        iso_read_test_counters_cp.conn_handle = htole16(conn->handle);
        rc = ble_ll_iso_read_counters_test((uint8_t *)&iso_read_test_counters_cp,
                                           sizeof(iso_read_test_counters_cp),
                                           (uint8_t *)&iso_read_test_counters_rp,
                                           &rsplen);
        TEST_ASSERT(rc == 0);
        TEST_ASSERT(rsplen == sizeof(iso_read_test_counters_rp));
        TEST_ASSERT(iso_read_test_counters_rp.conn_handle ==
                    iso_read_test_counters_cp.conn_handle);

        /* 8. TODO The Upper Tester receives a return parameter of
         * Received_SDU_Count as specified in Table 4.12-4.
         */

        /* 9.  The Upper Tester sends an HCI_LE_Setup_ISO_Data_Path command to the IUT.
         * 10. The IUT sends an HCI_Command_Complete event to the Upper Tester with
         *     Status set to 0x0C.
         */
        memset(&setup_iso_data_path_cp, 0, sizeof(setup_iso_data_path_cp));
        setup_iso_data_path_cp.conn_handle = htole16(conn->handle);
        setup_iso_data_path_cp.data_path_dir = 0x01;
        setup_iso_data_path_cp.data_path_id = 0x00;
        rc = ble_ll_iso_setup_iso_data_path(
            (uint8_t *)&setup_iso_data_path_cp, sizeof(setup_iso_data_path_cp),
            (uint8_t *)&setup_iso_data_path_rp, &rsplen);
        TEST_ASSERT(rc == 0x0C);

        /* 11. The Upper Tester sends the HCI_LE_ISO_Test_End command to the IUT and receives an
         *     HCI_Command_Status event from the IUT with the Status field set to Success.
         */
        rsplen = 0xFF;
        iso_test_end_cp.conn_handle = htole16(conn->handle);
        rc = ble_ll_iso_end_test((uint8_t *)&iso_test_end_cp, sizeof(iso_test_end_cp),
                                 (uint8_t *)&iso_test_end_rp, &rsplen);
        TEST_ASSERT(rc == 0);
        TEST_ASSERT(rsplen == sizeof(iso_test_end_rp));
        TEST_ASSERT(iso_test_end_rp.conn_handle == iso_test_end_cp.conn_handle);
    }

    test_ll_iso_teardown(&fixture);
}

TEST_SUITE(ble_ll_iso_test_suite) {
    ble_ll_iso_init();

    test_ll_ist_brd_bv_01_c();
    test_ll_ist_snc_bv_01_c();

    ble_ll_iso_reset();
}
