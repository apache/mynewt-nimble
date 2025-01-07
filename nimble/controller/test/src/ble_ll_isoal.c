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
#include <controller/ble_ll_isoal.h>
#include <os/os_mbuf.h>
#include <nimble/ble.h>
#include <testutil/testutil.h>

#define MBUF_TEST_POOL_BUF_SIZE     (320)
#define MBUF_TEST_POOL_BUF_COUNT    (10)

#define MBUF_TEST_DATA_LEN          (1024)

os_membuf_t os_mbuf_membuf[OS_MEMPOOL_SIZE(MBUF_TEST_POOL_BUF_SIZE,
                                           MBUF_TEST_POOL_BUF_COUNT)];

struct os_mbuf_pool os_mbuf_pool;
struct os_mempool os_mbuf_mempool;
uint8_t os_mbuf_test_data[MBUF_TEST_DATA_LEN];

void
os_mbuf_test_setup(void)
{
    int rc;
    int i;

    rc = os_mempool_init(&os_mbuf_mempool, MBUF_TEST_POOL_BUF_COUNT,
            MBUF_TEST_POOL_BUF_SIZE, &os_mbuf_membuf[0], "mbuf_pool");
    TEST_ASSERT_FATAL(rc == 0, "Error creating memory pool %d", rc);

    rc = os_mbuf_pool_init(&os_mbuf_pool, &os_mbuf_mempool,
            MBUF_TEST_POOL_BUF_SIZE, MBUF_TEST_POOL_BUF_COUNT);
    TEST_ASSERT_FATAL(rc == 0, "Error creating mbuf pool %d", rc);

    for (i = 0; i < sizeof os_mbuf_test_data; i++) {
        os_mbuf_test_data[i] = i;
    }
}

TEST_CASE_SELF(ble_ll_isoal_test_mux_init_free)
{
    struct ble_ll_isoal_mux mux;
    const uint32_t iso_interval_us = 10000;
    const uint32_t sdu_interval_us = 10000;
    const uint8_t bn = 1;
    const uint8_t max_pdu = 250;

    ble_ll_isoal_mux_init(&mux, max_pdu, iso_interval_us, sdu_interval_us, bn, 0);

    TEST_ASSERT(mux.pdu_per_sdu == (bn * sdu_interval_us) / iso_interval_us);

    ble_ll_isoal_mux_free(&mux);
}

TEST_CASE_SELF(ble_ll_isoal_mux_pdu_get_unframed_1_sdu_3_pdu)
{
    struct ble_ll_isoal_mux mux;
    struct os_mbuf *om;
    const uint32_t iso_interval_us = 10000;
    const uint32_t sdu_interval_us = 10000;
    const uint8_t bn = 3;
    const uint8_t max_pdu = 40;
    const uint8_t sdu_len = 3 * max_pdu;
    static uint8_t data[40];
    int num_sdu;
    int pdu_len;
    uint8_t llid = 0x00;
    int rc;

    _Static_assert(sdu_len <= sizeof(os_mbuf_test_data), "incorrect sdu length");

    ble_ll_isoal_mux_init(&mux, max_pdu, iso_interval_us, sdu_interval_us, bn, 0);

    om = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(om != NULL);

    rc = os_mbuf_append(om, os_mbuf_test_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);

    ble_ll_isoal_mux_sdu_enqueue(&mux, om, 90950);

    num_sdu = ble_ll_isoal_mux_event_start(&mux, 90990);
    TEST_ASSERT(num_sdu == 1);

    /* 1st PDU */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; start or continuation fragment of an SDU. */
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);

    /* 2nd PDU */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; start or continuation fragment of an SDU. */
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);

    /* 3rd PDU */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 2, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; end fragment of an SDU or a complete SDU. */
    TEST_ASSERT(llid == 0b00, "LLID is incorrect %d", llid);

    num_sdu = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_sdu == 1);

    ble_ll_isoal_mux_free(&mux);
}

TEST_CASE_SELF(ble_ll_isoal_mux_pdu_get_unframed_2_sdu_6_pdu)
{
    struct ble_ll_isoal_mux mux;
    struct os_mbuf *sdu_1, *sdu_2;
    const uint32_t iso_interval_us = 20000;
    const uint32_t sdu_interval_us = 10000;
    const uint8_t bn = 6;
    const uint8_t max_pdu = 40;
    const uint8_t sdu_len = 3 * max_pdu;
    static uint8_t data[40];
    int num_sdu;
    int pdu_len;
    uint8_t llid = 0x00;
    int rc;

    _Static_assert(sdu_len <= sizeof(os_mbuf_test_data), "incorrect sdu length");

    ble_ll_isoal_mux_init(&mux, max_pdu, iso_interval_us, sdu_interval_us, bn, 0);

    /* SDU #1 */
    sdu_1 = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_1 != NULL);
    rc = os_mbuf_append(sdu_1, os_mbuf_test_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu_1, 90950);

    /* SDU #2 */
    sdu_2 = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_2 != NULL);
    rc = os_mbuf_append(sdu_2, os_mbuf_test_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu_2, 90950);

    num_sdu = ble_ll_isoal_mux_event_start(&mux, 90990);
    TEST_ASSERT(num_sdu == 2);

    /* PDU #1 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; start or continuation fragment of an SDU. */
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);

    /* PDU #2 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; start or continuation fragment of an SDU. */
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);

    /* PDU #3 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 2, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; end fragment of an SDU or a complete SDU. */
    TEST_ASSERT(llid == 0b00, "LLID is incorrect %d", llid);

    /* PDU #4 */
	pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; start or continuation fragment of an SDU. */
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);

    /* PDU #5 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; start or continuation fragment of an SDU. */
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);

    /* PDU #6 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 2, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; end fragment of an SDU or a complete SDU. */
    TEST_ASSERT(llid == 0b00, "LLID is incorrect %d", llid);

    num_sdu = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_sdu == 2);

    ble_ll_isoal_mux_free(&mux);
}

TEST_SUITE(ble_ll_isoal_test_suite)
{
	os_mbuf_test_setup();

    ble_ll_isoal_init();

	ble_ll_isoal_test_mux_init_free();
	ble_ll_isoal_mux_pdu_get_unframed_1_sdu_3_pdu();
	ble_ll_isoal_mux_pdu_get_unframed_2_sdu_6_pdu();

    ble_ll_isoal_reset();
}
