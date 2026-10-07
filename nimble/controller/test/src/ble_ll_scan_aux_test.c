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
#include <string.h>
#include <os/os_mbuf.h>
#include <nimble/ble.h>
#include <controller/ble_ll.h>
#include <controller/ble_ll_scan.h>
#include <controller/ble_ll_scan_aux.h>
#include <testutil/testutil.h>

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)

#define TEST_BUF_SIZE                                                         \
    (sizeof(struct os_mbuf) + sizeof(struct os_mbuf_pkthdr) +                 \
     sizeof(struct ble_mbuf_hdr) + 64)
#define TEST_BUF_COUNT (4)

static struct os_mbuf_pool g_test_mbuf_pool;
static struct os_mempool g_test_mempool;
static os_membuf_t g_test_mbuf_buffer[OS_MEMPOOL_SIZE(TEST_BUF_COUNT, TEST_BUF_SIZE)];

static void
test_scan_aux_init(void)
{
    int rc;

    rc = os_mempool_init(&g_test_mempool, TEST_BUF_COUNT, TEST_BUF_SIZE,
                         &g_test_mbuf_buffer[0], "test_scan_aux_pool");
    TEST_ASSERT_FATAL(rc == 0);

    rc = os_mbuf_pool_init(&g_test_mbuf_pool, &g_test_mempool, TEST_BUF_SIZE,
                           TEST_BUF_COUNT);
    TEST_ASSERT_FATAL(rc == 0);
}

static int
test_scan_aux_rx_ext(const uint8_t *pdu, uint8_t pdu_len)
{
    struct ble_ll_scan_phy scanp;
    struct ble_ll_scan_sm scansm;
    struct os_mbuf *om;
    int rc;

    memset(&scanp, 0, sizeof(scanp));
    memset(&scansm, 0, sizeof(scansm));
    scanp.scan_type = BLE_SCAN_TYPE_PASSIVE;
    scansm.scanp = &scanp;

    ble_ll_scan_aux_init();

    om = os_mbuf_get_pkthdr(&g_test_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(om != NULL);
    memset(BLE_MBUF_HDR_PTR(om), 0, sizeof(struct ble_mbuf_hdr));

    rc = os_mbuf_append(om, pdu, pdu_len);
    TEST_ASSERT_FATAL(rc == 0);

    rc = ble_ll_scan_aux_rx_isr_end_on_ext(&scansm, om);

    os_mbuf_free_chain(om);

    return rc;
}

TEST_CASE_SELF(test_ll_scan_aux_ext_hdr_valid)
{
    /* ADV_EXT_IND, Extended Header with ADI and AuxPtr */
    const uint8_t pdu[] = { 0x07, 0x07, 0x06, 0x18, 0x01,
                            0x00, 0x01, 0x10, 0x00 };

    TEST_ASSERT(test_scan_aux_rx_ext(pdu, sizeof(pdu)) == 0);
}

TEST_CASE_SELF(test_ll_scan_aux_ext_hdr_len_exceeds_pdu)
{
    /* ADV_EXT_IND, Extended Header Length does not fit in PDU */
    const uint8_t pdu[] = { 0x07, 0x02, 0x06, 0x18 };

    TEST_ASSERT(test_scan_aux_rx_ext(pdu, sizeof(pdu)) < 0);
}

TEST_CASE_SELF(test_ll_scan_aux_ext_hdr_fields_exceed_hdr)
{
    /* ADV_EXT_IND, AdvA, ADI and AuxPtr flagged but only ADI and AuxPtr fit */
    const uint8_t pdu[] = { 0x07, 0x07, 0x06, 0x19, 0x01,
                            0x00, 0x01, 0x10, 0x00 };

    TEST_ASSERT(test_scan_aux_rx_ext(pdu, sizeof(pdu)) < 0);
}

TEST_SUITE(ble_ll_scan_aux_test_suite)
{
    test_scan_aux_init();

    test_ll_scan_aux_ext_hdr_valid();
    test_ll_scan_aux_ext_hdr_len_exceeds_pdu();
    test_ll_scan_aux_ext_hdr_fields_exceed_hdr();
}

#else

TEST_SUITE(ble_ll_scan_aux_test_suite)
{
}

#endif
