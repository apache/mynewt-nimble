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
#include <nimble/hci_common.h>
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
    const bool Framed = 0;
    const bool Framing_Mode = 0;
    const uint8_t bn = 1;
    const uint8_t max_pdu = 250;

    ble_ll_isoal_mux_init(&mux, max_pdu, iso_interval_us, sdu_interval_us, bn,
                          0, Framed, Framing_Mode);

    TEST_ASSERT(mux.pdu_per_sdu == (bn * sdu_interval_us) / iso_interval_us);

    ble_ll_isoal_mux_free(&mux);
}

TEST_CASE_SELF(ble_ll_isoal_mux_pdu_get_unframed_1_sdu_3_pdu)
{
    struct ble_ll_isoal_mux mux;
    struct os_mbuf *om;
    const uint32_t iso_interval_us = 10000;
    const uint32_t sdu_interval_us = 10000;
    const bool Framed = 0;
    const bool Framing_Mode = 0;
    const uint8_t bn = 3;
    const uint8_t max_pdu = 40;
    const uint8_t sdu_len = 3 * max_pdu;
    static uint8_t data[40];
    int num_completed_pkt;
    int pdu_len;
    uint8_t llid = 0x00;
    int rc;

    _Static_assert(sdu_len <= sizeof(os_mbuf_test_data), "incorrect sdu length");

    ble_ll_isoal_mux_init(&mux, max_pdu, iso_interval_us, sdu_interval_us, bn,
                          0, Framed, Framing_Mode);

    om = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(om != NULL);

    rc = os_mbuf_append(om, os_mbuf_test_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);

    ble_ll_isoal_mux_sdu_enqueue(&mux, om);

    ble_ll_isoal_mux_event_start(&mux, 90990);

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

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt > 0);

    ble_ll_isoal_mux_free(&mux);
}

TEST_CASE_SELF(ble_ll_isoal_mux_pdu_get_unframed_1_sdu_2_pdu)
{
    struct ble_ll_isoal_mux mux;
    struct os_mbuf *sdu_1, *sdu_2;
    const uint32_t iso_interval_us = 20000;
    const uint32_t sdu_interval_us = 10000;
    const bool Framed = 0;
    const bool Framing_Mode = 0;
    const uint8_t bn = 6;
    const uint8_t max_pdu = 40;
    const uint8_t sdu_len = 3 * max_pdu;
    static uint8_t data[40];
    int num_completed_pkt;
    int pdu_len;
    uint8_t llid = 0x00;
    int rc;

    _Static_assert(sdu_len <= sizeof(os_mbuf_test_data), "incorrect sdu length");

    ble_ll_isoal_mux_init(&mux, max_pdu, iso_interval_us, sdu_interval_us, bn,
                          0, Framed, Framing_Mode);

    /* SDU #1 */
    sdu_1 = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_1 != NULL);
    rc = os_mbuf_append(sdu_1, os_mbuf_test_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu_1);

    /* SDU #2 */
    sdu_2 = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_2 != NULL);
    rc = os_mbuf_append(sdu_2, os_mbuf_test_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu_2);

    ble_ll_isoal_mux_event_start(&mux, 90990);

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

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt > 0, "num_completed_pkt is incorrect %d",
                num_completed_pkt);

    ble_ll_isoal_mux_free(&mux);
}

TEST_CASE_SELF(ble_ll_isoal_mux_pdu_get_unframed_2_sdu_6_pdu)
{
    struct ble_ll_isoal_mux mux;
    struct os_mbuf *sdu_1, *sdu_2;
    const uint32_t iso_interval_us = 20000;
    const uint32_t sdu_interval_us = 10000;
    const bool Framed = 0;
    const bool Framing_Mode = 0;
    const uint8_t bn = 6;
    const uint8_t max_pdu = 40;
    const uint8_t sdu_len = 3 * max_pdu;
    static uint8_t data[40];
    int num_completed_pkt;
    int pdu_len;
    uint8_t llid = 0x00;
    int rc;

    _Static_assert(sdu_len <= sizeof(os_mbuf_test_data), "incorrect sdu length");

    ble_ll_isoal_mux_init(&mux, max_pdu, iso_interval_us, sdu_interval_us, bn,
                          0, Framed, Framing_Mode);

    /* SDU #1 */
    sdu_1 = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_1 != NULL);
    rc = os_mbuf_append(sdu_1, os_mbuf_test_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu_1);

    /* SDU #2 */
    sdu_2 = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_2 != NULL);
    rc = os_mbuf_append(sdu_2, os_mbuf_test_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu_2);

    ble_ll_isoal_mux_event_start(&mux, 90990);

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

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt > 0, "num_completed_pkt is incorrect %d",
                num_completed_pkt);

    ble_ll_isoal_mux_free(&mux);
}

TEST_CASE_SELF(ble_ll_isoal_mux_pdu_get_segmentable_0_sdu_2_pdu)
{
    struct ble_ll_isoal_mux mux;
    const uint32_t iso_interval_us = 10000;
    const uint32_t sdu_interval_us = 10000;
    const bool Framed = 1;
    const bool Framing_Mode = 0;
    const uint8_t bn = 2;
    const uint8_t max_pdu = 40;
    const uint8_t sdu_len = 40;
    static uint8_t data[40];
    int num_completed_pkt;
    int pdu_len;
    uint8_t llid = 0x00;

    _Static_assert(sdu_len <= sizeof(os_mbuf_test_data), "incorrect sdu length");

    ble_ll_isoal_mux_init(&mux, max_pdu, iso_interval_us, sdu_interval_us, bn,
                          0, Framed, Framing_Mode);

    ble_ll_isoal_mux_event_start(&mux, 90990);

    /* PDU #1 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, data);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

    /* PDU #2 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, data);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt == 0, "num_completed_pkt is incorrect %d",
                num_completed_pkt);

    ble_ll_isoal_mux_free(&mux);
}

TEST_CASE_SELF(ble_ll_isoal_mux_pdu_get_segmentable_0_sdu_2_pdu_1_sdu_not_in_event)
{
    struct ble_ll_isoal_mux mux;
    struct os_mbuf *sdu_1;
    const uint32_t iso_interval_us = 10000;
    const uint32_t sdu_interval_us = 10000;
    const bool Framed = 1;
    const bool Framing_Mode = 0;
    const uint8_t bn = 2;
    const uint8_t max_pdu = 40;
    const uint8_t sdu_len = 40;
    static uint8_t data[40];
    int num_completed_pkt;
    int pdu_len;
    uint8_t llid = 0x00;
    int rc;

    _Static_assert(sdu_len <= sizeof(os_mbuf_test_data), "incorrect sdu length");

    ble_ll_isoal_mux_init(&mux, max_pdu, iso_interval_us, sdu_interval_us, bn,
                          0, Framed, Framing_Mode);

    ble_ll_isoal_mux_event_start(&mux, 90990);

    /* SDU #1 */
    sdu_1 = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_1 != NULL);
    rc = os_mbuf_append(sdu_1, os_mbuf_test_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu_1);

    /* PDU #1 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, data);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

    /* PDU #2 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, data);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt == 0, "num_completed_pkt is incorrect %d",
                num_completed_pkt);

    ble_ll_isoal_mux_free(&mux);
}

TEST_CASE_SELF(ble_ll_isoal_mux_pdu_get_segmentable_1_sdu_2_pdu)
{
    struct ble_ll_isoal_mux mux;
    struct os_mbuf *sdu_1;
    const uint32_t iso_interval_us = 10000;
    const uint32_t sdu_interval_us = 10000;
    const bool Framed = 1;
    const bool Framing_Mode = 0;
    const uint8_t bn = 2;
    const uint8_t max_pdu = 40;
    const uint8_t sdu_len = 40;
    static uint8_t data[40];
    int num_completed_pkt;
    int pdu_len;
    uint8_t llid = 0x00;
    int rc;

    _Static_assert(sdu_len <= sizeof(os_mbuf_test_data), "incorrect sdu length");

    ble_ll_isoal_mux_init(&mux, max_pdu, iso_interval_us, sdu_interval_us, bn,
                          0, Framed, Framing_Mode);

    /* SDU #1 */
    sdu_1 = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_1 != NULL);
    rc = os_mbuf_append(sdu_1, os_mbuf_test_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu_1);

    ble_ll_isoal_mux_event_start(&mux, 90990);

    /* PDU #1 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Framed Data PDU. */
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

    /* PDU #2 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, data);
    TEST_ASSERT(pdu_len == 7, "PDU length is incorrect %d", pdu_len);
    /* Framed Data PDU. */
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt > 0, "num_completed_pkt is incorrect %d",
                num_completed_pkt);

    ble_ll_isoal_mux_free(&mux);
}

TEST_CASE_SELF(ble_ll_isoal_mux_pdu_get_segmentable_2_sdu_2_pdu)
{
    struct ble_ll_isoal_mux mux;
    struct ble_mbuf_hdr *blehdr;
    struct os_mbuf *sdu_1, *sdu_2;
    const uint32_t iso_interval_us = 10000;
    const uint32_t sdu_interval_us = 10000;
    const bool Framed = 1;
    const bool Framing_Mode = 0;
    const uint8_t bn = 2;
    const uint8_t max_pdu = 40;
    const uint8_t sdu_len = 40;
    static uint8_t data[40];
    int num_completed_pkt;
    int pdu_len;
    uint32_t timeoffset;
    uint16_t seghdr;
    uint8_t llid = 0x00;
    int rc;

    _Static_assert(sdu_len <= sizeof(os_mbuf_test_data), "incorrect sdu length");

    ble_ll_isoal_mux_init(&mux, max_pdu, iso_interval_us, sdu_interval_us, bn,
                          0, Framed, Framing_Mode);

    /* SDU #1 */
    sdu_1 = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_1 != NULL);
    blehdr = BLE_MBUF_HDR_PTR(sdu_1);
    blehdr->txiso.packet_seq_num = 1;
    blehdr->txiso.cpu_timestamp = 20000;
    rc = os_mbuf_append(sdu_1, os_mbuf_test_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu_1);

    /* SDU #1 */
    sdu_2 = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_2 != NULL);
    blehdr = BLE_MBUF_HDR_PTR(sdu_2);
    blehdr->txiso.packet_seq_num = 2;
    blehdr->txiso.cpu_timestamp = 30000;
    rc = os_mbuf_append(sdu_2, os_mbuf_test_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu_2);

    ble_ll_isoal_mux_event_start(&mux, 30500);

    /**
 	 * Event #1; PDU #1
	 * +---------------------+------------------------+-----------------+
	 * | Segmentation Header | TimeOffset (ISO SDU 1) | ISO SDU 1 Seg 1 |
	 * | 2 bytes             | 3 bytes                | 35 bytes        |
	 * +---------------------+------------------------+-----------------+
	 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

    /* ISO SDU 1 Seg 1 */
    seghdr = get_le16(&data[0]);
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seghdr) == 0,
                "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_SC(seghdr));
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr) == 0,
                "CMPLT is incorrect %d", BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr));
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_LEN(seghdr) == 38,
                "Length is incorrect %d", BLE_LL_ISOAL_SEGHDR_LEN(seghdr));
	timeoffset = get_le24(&data[2]);
    TEST_ASSERT(timeoffset == 10500, "Time offset is incorrect %d", timeoffset);

    /**
 	 * Event #1; PDU #2
	 * +---------------------+-----------------+---------------------+------------------------+-----------------+
	 * | Segmentation Header | ISO SDU 1 Seg 2 | Segmentation Header | TimeOffset (ISO SDU 2) | ISO SDU 2 Seg 1 |
	 * | 2 bytes             | 5 bytes         | 2 bytes             | 3 bytes                | 28 bytes        |
	 * +---------------------+-----------------+---------------------+------------------------+-----------------+
	 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

    /* ISO SDU 1 Seg 2 */
    seghdr = get_le16(&data[0]);
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seghdr) == 1,
                "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_SC(seghdr));
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr) == 1,
                "CMPLT is incorrect %d", BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr));
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_LEN(seghdr) == 5,
                "Length is incorrect %d", BLE_LL_ISOAL_SEGHDR_LEN(seghdr));

    /* ISO SDU 2 Seg 1 */
    seghdr = get_le16(&data[7]);
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seghdr) == 0,
                "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_SC(seghdr));
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr) == 0,
                "CMPLT is incorrect %d", BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr));
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_LEN(seghdr) == 31,
                "Length is incorrect %d", BLE_LL_ISOAL_SEGHDR_LEN(seghdr));
    timeoffset = get_le24(&data[9]);
    TEST_ASSERT(timeoffset == 500, "Time offset is incorrect %d", timeoffset);

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt > 0, "num_completed_pkt is incorrect %d",
                num_completed_pkt);

    ble_ll_isoal_mux_event_start(&mux, 30500 + iso_interval_us);

    /**
 	 * Event #2; PDU #1
	 * +---------------------+-----------------+
	 * | Segmentation Header | ISO SDU 2 Seg 2 |
	 * | 2 bytes             | 12 bytes        |
	 * +---------------------+-----------------+
	 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, data);
    TEST_ASSERT(pdu_len == 14, "PDU length is incorrect %d", pdu_len);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

    /* ISO SDU 2 Seg 2 */
    seghdr = get_le16(&data[0]);
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seghdr) == 1,
                "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_SC(seghdr));
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr) == 1,
                "CMPLT is incorrect %d", BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr));
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_LEN(seghdr) == 12,
                "Length is incorrect %d", BLE_LL_ISOAL_SEGHDR_LEN(seghdr));

    /**
 	 * Event #2; PDU #2 Empty
	 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, data);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt > 0, "num_completed_pkt is incorrect %d",
                num_completed_pkt);

    ble_ll_isoal_mux_free(&mux);
}

TEST_CASE_SELF(ble_ll_isoal_mux_pdu_get_segmentable_1_sdu_2_pdu_sdu_has_zero_length)
{
    struct ble_ll_isoal_mux mux;
    struct ble_mbuf_hdr *blehdr;
    struct os_mbuf *sdu_1;
    const uint32_t iso_interval_us = 10000;
    const uint32_t sdu_interval_us = 10000;
    const bool Framed = 1;
    const bool Framing_Mode = 0;
    const uint8_t bn = 2;
    const uint8_t max_pdu = 40;
    const uint8_t sdu_len = 40;
    static uint8_t data[40];
    int num_completed_pkt;
    int pdu_len;
    uint32_t timeoffset;
    uint16_t seghdr;
    uint8_t llid = 0x00;
    int rc;

    _Static_assert(sdu_len <= sizeof(os_mbuf_test_data), "incorrect sdu length");

    ble_ll_isoal_mux_init(&mux, max_pdu, iso_interval_us, sdu_interval_us, bn,
                          0, Framed, Framing_Mode);

    /* SDU #1 */
    sdu_1 = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_1 != NULL);
    blehdr = BLE_MBUF_HDR_PTR(sdu_1);
    blehdr->txiso.packet_seq_num = 1;
    blehdr->txiso.cpu_timestamp = 20000;
    rc = os_mbuf_append(sdu_1, os_mbuf_test_data, 0);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu_1);

    ble_ll_isoal_mux_event_start(&mux, 30500);

    /**
 	 * Event #1; PDU #1
	 * +---------------------+------------------------+
	 * | Segmentation Header | TimeOffset (ISO SDU 1) |
	 * | 2 bytes             | 3 bytes                |
	 * +---------------------+------------------------+
	 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, data);
    TEST_ASSERT(pdu_len == 5, "PDU length is incorrect %d", pdu_len);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

    /* ISO SDU 1 Seg 1 */
    seghdr = get_le16(&data[0]);
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seghdr) == 0,
                "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_SC(seghdr));
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr) == 1,
                "CMPLT is incorrect %d", BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr));
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_LEN(seghdr) == 3,
                "Length is incorrect %d", BLE_LL_ISOAL_SEGHDR_LEN(seghdr));
	timeoffset = get_le24(&data[2]);
    TEST_ASSERT(timeoffset == 10500, "Time offset is incorrect %d", timeoffset);

    /**
     * Event #1; PDU #2 Empty
	 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, data);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt > 0, "num_completed_pkt is incorrect %d",
                num_completed_pkt);

    ble_ll_isoal_mux_free(&mux);
}

TEST_CASE_SELF(ble_ll_ial_bis_fra_brd_bv_06_c)
{
    struct ble_ll_isoal_mux mux;
    struct ble_mbuf_hdr *blehdr;
    struct os_mbuf *sdu_1;
    const uint8_t NSE = 4;
    const bool Framed = 1;
    const bool Framing_Mode = 0;
    const uint8_t Max_PDU = 40;
    const uint8_t LLID = 0b10;
    const uint8_t BN = 2;
    const uint32_t SDU_Interval = 5000;
    const uint32_t ISO_Interval = 10000;
    const uint8_t Max_SDU = 32;
    static uint8_t data[40];
    int num_completed_pkt;
    int pdu_len;
    uint32_t timeoffset;
    uint16_t seghdr;
    uint8_t llid = 0xff;
    int rc;

    _Static_assert(Max_SDU <= sizeof(os_mbuf_test_data), "incorrect sdu length");

    ble_ll_isoal_mux_init(&mux, Max_PDU, ISO_Interval, SDU_Interval, BN,
                          0, Framed, Framing_Mode);

    /* SDU #1 */
    sdu_1 = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_1 != NULL);
    blehdr = BLE_MBUF_HDR_PTR(sdu_1);
    blehdr->txiso.packet_seq_num = 0;
    blehdr->txiso.cpu_timestamp = 20000;
    rc = os_mbuf_append(sdu_1, os_mbuf_test_data, 27);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu_1);

    ble_ll_isoal_mux_event_start(&mux, 30500);

    for (uint8_t i = 0; i < NSE; i++) {
        /**
         * Event #1; PDU #1
         * +---------------------+------------------------+-----------------+
         * | Segmentation Header | TimeOffset (ISO SDU 1) | ISO SDU 1 Seg 1 |
         * | 2 bytes             | 3 bytes                | 27 bytes        |
         * +---------------------+------------------------+-----------------+
         */
        pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, data);
        TEST_ASSERT(pdu_len == 32, "PDU length is incorrect %d", pdu_len);
        TEST_ASSERT(llid == LLID, "LLID is incorrect %d", llid);

        /* ISO SDU 1 Seg 1 */
        seghdr = get_le16(&data[0]);
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seghdr) == 0,
                    "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_SC(seghdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr) == 1,
                    "CMPLT is incorrect %d", BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_LEN(seghdr) == 30,
                    "Length is incorrect %d", BLE_LL_ISOAL_SEGHDR_LEN(seghdr));
        timeoffset = get_le24(&data[2]);
        TEST_ASSERT(timeoffset == 10500, "Time offset is incorrect %d", timeoffset);
    }

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt > 0, "num_completed_pkt is incorrect %d",
                num_completed_pkt);

    ble_ll_isoal_mux_free(&mux);
}

TEST_CASE_SELF(ble_ll_ial_bis_fra_brd_bv_08_c)
{
    struct ble_ll_isoal_mux mux;
    struct ble_mbuf_hdr *blehdr;
    struct os_mbuf *sdu_1;
    const uint8_t NSE = 2;
    const bool Framed = 1;
    const bool Framing_Mode = 0;
    const uint8_t Max_PDU = 40;
    const uint8_t LLID = 0b10;
    const uint8_t BN = 1;
    const uint32_t SDU_Interval = 10000;
    const uint32_t ISO_Interval = 10000;
    const uint8_t Max_SDU = 32;
    static uint8_t data[40];
    int num_completed_pkt;
    int pdu_len;
    uint32_t timeoffset;
    uint16_t seghdr;
    uint8_t llid = 0xff;
    int rc;

    _Static_assert(Max_SDU <= sizeof(os_mbuf_test_data), "incorrect sdu length");

    ble_ll_isoal_mux_init(&mux, Max_PDU, ISO_Interval, SDU_Interval, BN,
                          0, Framed, Framing_Mode);

    /* SDU #1 */
    sdu_1 = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_1 != NULL);
    blehdr = BLE_MBUF_HDR_PTR(sdu_1);
    blehdr->txiso.packet_seq_num = 0;
    blehdr->txiso.cpu_timestamp = 20000;
    rc = os_mbuf_append(sdu_1, os_mbuf_test_data, 27);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu_1);

    ble_ll_isoal_mux_event_start(&mux, 30500);

    for (uint8_t i = 0; i < NSE; i++) {
        /**
         * Event #1; PDU #1
         * +---------------------+------------------------+-----------------+
         * | Segmentation Header | TimeOffset (ISO SDU 1) | ISO SDU 1 Seg 1 |
         * | 2 bytes             | 3 bytes                | 27 bytes        |
         * +---------------------+------------------------+-----------------+
         */
        pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, data);
        TEST_ASSERT(pdu_len == 32, "PDU length is incorrect %d", pdu_len);
        TEST_ASSERT(llid == LLID, "LLID is incorrect %d", llid);

        /* ISO SDU 1 Seg 1 */
        seghdr = get_le16(&data[0]);
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seghdr) == 0,
                    "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_SC(seghdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr) == 1,
                    "CMPLT is incorrect %d", BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_LEN(seghdr) == 30,
                    "Length is incorrect %d", BLE_LL_ISOAL_SEGHDR_LEN(seghdr));
        timeoffset = get_le24(&data[2]);
        TEST_ASSERT(timeoffset == 10500, "Time offset is incorrect %d", timeoffset);

        /**
         * Event #1; PDU #2 Empty
         */
        pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, data);
        TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
        TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    }

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt > 0, "num_completed_pkt is incorrect %d",
                num_completed_pkt);

    ble_ll_isoal_mux_free(&mux);
}

TEST_CASE_SELF(ble_ll_ial_bis_fra_brd_bv_13_c)
{
    struct ble_ll_isoal_mux mux;
    struct ble_mbuf_hdr *blehdr;
    struct os_mbuf *sdu;
    const uint8_t NSE = 10;
    const bool Framed = 1;
    const bool Framing_Mode = 0;
    const uint8_t Max_PDU = 251;
    const uint8_t LLID = 0b10;
    const uint8_t BN = 5;
    const uint32_t SDU_Interval = 15000;
    const uint32_t ISO_Interval = 30000;
    const uint16_t Max_SDU = 503;
    static uint8_t data[251];
    int num_completed_pkt;
    int pdu_len;
    uint32_t timeoffset;
    uint16_t seghdr;
    uint8_t llid = 0xff;
    int rc;

    TEST_ASSERT(Max_SDU <= sizeof(os_mbuf_test_data), "incorrect sdu length");

    ble_ll_isoal_mux_init(&mux, Max_PDU, ISO_Interval, SDU_Interval, BN,
                          0, Framed, Framing_Mode);

    /* Round 1 */
    sdu = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu != NULL);
    blehdr = BLE_MBUF_HDR_PTR(sdu);
    blehdr->txiso.packet_seq_num = 0;
    blehdr->txiso.cpu_timestamp = 10000;
    rc = os_mbuf_append(sdu, os_mbuf_test_data, 495);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu);

    ble_ll_isoal_mux_event_start(&mux, 10500);

    for (uint8_t i = 0; i < NSE / BN; i += BN) {
        /**
         * P0
         * +---------------------+------------------------+-----------------+
         * | Segmentation Header | TimeOffset (ISO SDU 1) | ISO SDU 1 Seg 1 |
         * | 2 bytes             | 3 bytes                | 246 bytes       |
         * +---------------------+------------------------+-----------------+
         */
        pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, data);
        TEST_ASSERT(pdu_len == 251, "PDU length is incorrect %d", pdu_len);
        TEST_ASSERT(llid == LLID, "LLID is incorrect %d", llid);

        /* ISO SDU 1 Seg 1 */
        seghdr = get_le16(&data[0]);
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seghdr) == 0,
                    "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_SC(seghdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr) == 0,
                    "CMPLT is incorrect %d", BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_LEN(seghdr) == 249,
                    "Length is incorrect %d", BLE_LL_ISOAL_SEGHDR_LEN(seghdr));
        timeoffset = get_le24(&data[2]);
        TEST_ASSERT(timeoffset == 500, "Time offset is incorrect %d",
                    timeoffset);

        /**
         * P1
         * +---------------------+-----------------+
         * | Segmentation Header | ISO SDU 1 Seg 2 |
         * | 2 bytes             | 249 bytes       |
         * +---------------------+-----------------+
         */
        pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, data);
        TEST_ASSERT(pdu_len == 251, "PDU length is incorrect %d", pdu_len);
        TEST_ASSERT(llid == LLID, "LLID is incorrect %d", llid);

        /* ISO SDU 1 Seg 2 */
        seghdr = get_le16(&data[0]);
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seghdr) == 1,
                    "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_SC(seghdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr) == 1,
                    "CMPLT is incorrect %d", BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_LEN(seghdr) == 249,
                    "Length is incorrect %d", BLE_LL_ISOAL_SEGHDR_LEN(seghdr));

        /**
         * P2
         */
        pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 2, &llid, data);
        TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
        TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

        /**
         * P3
         */
        pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 3, &llid, data);
        TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
        TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

        /**
         * P4
         */
        pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 4, &llid, data);
        TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
        TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    }

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt > 0, "num_completed_pkt is incorrect %d",
                num_completed_pkt);

    /* Round 2 */
    sdu = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu != NULL);
    blehdr = BLE_MBUF_HDR_PTR(sdu);
    blehdr->txiso.packet_seq_num = 0;
    blehdr->txiso.cpu_timestamp = 25000;
    rc = os_mbuf_append(sdu, os_mbuf_test_data, 503);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu);

    ble_ll_isoal_mux_event_start(&mux, 40500);

    for (uint8_t i = 0; i < NSE / BN; i += BN) {
        /**
         * P0
         * +---------------------+------------------------+-----------------+
         * | Segmentation Header | TimeOffset (ISO SDU 1) | ISO SDU 1 Seg 1 |
         * | 2 bytes             | 3 bytes                | 246 bytes       |
         * +---------------------+------------------------+-----------------+
         */
        pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, data);
        TEST_ASSERT(pdu_len == 251, "PDU length is incorrect %d", pdu_len);
        TEST_ASSERT(llid == LLID, "LLID is incorrect %d", llid);

        /* ISO SDU 1 Seg 1 */
        seghdr = get_le16(&data[0]);
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seghdr) == 0,
                    "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_SC(seghdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr) == 0,
                    "CMPLT is incorrect %d", BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_LEN(seghdr) == 249,
                    "Length is incorrect %d", BLE_LL_ISOAL_SEGHDR_LEN(seghdr));
        timeoffset = get_le24(&data[2]);
        TEST_ASSERT(timeoffset == 15500, "Time offset is incorrect %d",
                    timeoffset);

        /**
         * P1
         * +---------------------+-----------------+
         * | Segmentation Header | ISO SDU 1 Seg 2 |
         * | 2 bytes             | 249 bytes       |
         * +---------------------+-----------------+
         */
        pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, data);
        TEST_ASSERT(pdu_len == 251, "PDU length is incorrect %d", pdu_len);
        TEST_ASSERT(llid == LLID, "LLID is incorrect %d", llid);

        /* ISO SDU 1 Seg 2 */
        seghdr = get_le16(&data[0]);
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seghdr) == 1,
                    "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_SC(seghdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr) == 0,
                    "CMPLT is incorrect %d", BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_LEN(seghdr) == 249,
                    "Length is incorrect %d", BLE_LL_ISOAL_SEGHDR_LEN(seghdr));

        /**
         * P2
         * +---------------------+-----------------+
         * | Segmentation Header | ISO SDU 1 Seg 3 |
         * | 2 bytes             | 8 bytes         |
         * +---------------------+-----------------+
         */
        pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 2, &llid, data);
        TEST_ASSERT(pdu_len == 10, "PDU length is incorrect %d", pdu_len);
        TEST_ASSERT(llid == LLID, "LLID is incorrect %d", llid);

        /* ISO SDU 1 Seg 3 */
        seghdr = get_le16(&data[0]);
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seghdr) == 1,
                    "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_SC(seghdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr) == 1,
                    "CMPLT is incorrect %d", BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_LEN(seghdr) == 8,
                    "Length is incorrect %d", BLE_LL_ISOAL_SEGHDR_LEN(seghdr));

        /**
         * P3
         */
        pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 3, &llid, data);
        TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
        TEST_ASSERT(llid == LLID, "LLID is incorrect %d", llid);

        /**
         * P4
         */
        pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 4, &llid, data);
        TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
        TEST_ASSERT(llid == LLID, "LLID is incorrect %d", llid);
    }

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt > 0, "num_completed_pkt is incorrect %d",
                num_completed_pkt);

    ble_ll_isoal_mux_free(&mux);
}

TEST_SUITE(ble_ll_isoal_test_suite)
{
	os_mbuf_test_setup();

    ble_ll_isoal_init();

	ble_ll_isoal_test_mux_init_free();
	ble_ll_isoal_mux_pdu_get_unframed_1_sdu_3_pdu();
	ble_ll_isoal_mux_pdu_get_unframed_2_sdu_6_pdu();
    ble_ll_isoal_mux_pdu_get_segmentable_0_sdu_2_pdu();
    ble_ll_isoal_mux_pdu_get_segmentable_0_sdu_2_pdu_1_sdu_not_in_event();
    ble_ll_isoal_mux_pdu_get_segmentable_1_sdu_2_pdu();
    ble_ll_isoal_mux_pdu_get_segmentable_2_sdu_2_pdu();
    ble_ll_isoal_mux_pdu_get_segmentable_1_sdu_2_pdu_sdu_has_zero_length();

    ble_ll_ial_bis_fra_brd_bv_06_c();
    ble_ll_ial_bis_fra_brd_bv_08_c();
    ble_ll_ial_bis_fra_brd_bv_13_c();

    ble_ll_isoal_reset();
}
