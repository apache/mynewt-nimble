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

#define TSPX_max_sdu_length         (503)
#define HCI_iso_sdu_max             (MYNEWT_VAL(BLE_TRANSPORT_ISO_SIZE) - 4)

#define MBUF_TEST_POOL_BUF_SIZE     (TSPX_max_sdu_length + BLE_MBUF_MEMBLOCK_OVERHEAD)
#define MBUF_TEST_POOL_BUF_COUNT    (10)

os_membuf_t os_mbuf_membuf[OS_MEMPOOL_SIZE(MBUF_TEST_POOL_BUF_SIZE, MBUF_TEST_POOL_BUF_COUNT)];

static struct os_mbuf_pool os_mbuf_pool;
static struct os_mempool os_mbuf_mempool;
static uint8_t os_mbuf_test_data[TSPX_max_sdu_length];

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

    TEST_ASSERT_FATAL(os_mbuf_mempool.mp_block_size == MBUF_TEST_POOL_BUF_SIZE,
                      "mp_block_size is %d", os_mbuf_mempool.mp_block_size);
    TEST_ASSERT_FATAL(os_mbuf_mempool.mp_num_free == MBUF_TEST_POOL_BUF_COUNT,
                      "mp_num_free is %d", os_mbuf_mempool.mp_num_free);
}

TEST_CASE_SELF(test_ble_ll_isoal_mux_init) {
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

TEST_CASE_SELF(ble_ll_isoal_mux_pdu_get_unframed_1_sdu_2_pdu) {
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
    TEST_ASSERT(num_completed_pkt > 0, "num_completed_pkt is incorrect %d", num_completed_pkt);

    ble_ll_isoal_mux_free(&mux);
}

TEST_CASE_SELF(test_ble_ll_isoal_mux_get_unframed_pdu) {
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

TEST_CASE_SELF(test_ble_ll_isoal_mux_sdu_not_in_event) {
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

    ble_ll_isoal_mux_init(&mux, max_pdu, iso_interval_us, sdu_interval_us, bn,
                          0, Framed, Framing_Mode);

    ble_ll_isoal_mux_event_start(&mux, 90990);
    TEST_ASSERT_FATAL(mux.sdu_in_event == 0,
                      "sdu_in_event %d != 0", mux.sdu_in_event);

    /* SDU #1 */
    sdu_1 = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_1 != NULL);
    rc = os_mbuf_append(sdu_1, os_mbuf_test_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_enqueue(&mux, sdu_1);

    TEST_ASSERT_FATAL(mux.sdu_in_event == 0,
                      "sdu_in_event %d != 0", mux.sdu_in_event);

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

static int
test_sdu_enqueue(struct ble_ll_isoal_mux *mux, uint16_t sdu_len,
                 uint16_t packet_seq_num, uint32_t timestamp)
{
    struct ble_mbuf_hdr *blehdr;
    struct os_mbuf *sdu, *frag;
    uint16_t sdu_frag_len;
    uint16_t offset = 0;
    uint8_t num_pkt = 0;
    int rc;

    TEST_ASSERT_FATAL(sdu_len <= TSPX_max_sdu_length, "incorrect sdu length");

    sdu = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu != NULL);
    blehdr = BLE_MBUF_HDR_PTR(sdu);
    blehdr->txiso.packet_seq_num = packet_seq_num;
    blehdr->txiso.cpu_timestamp = timestamp;

    /* First SDU Fragment */
    sdu_frag_len = min(sdu_len, HCI_iso_sdu_max);
    rc = os_mbuf_append(sdu, os_mbuf_test_data, sdu_frag_len);
    TEST_ASSERT_FATAL(rc == 0);

    offset += sdu_frag_len;
    num_pkt++;

    while (offset < sdu_len) {
        frag = os_mbuf_get_pkthdr(&os_mbuf_pool, sizeof(struct ble_mbuf_hdr));
        TEST_ASSERT_FATAL(frag != NULL);

        /* Subsequent SDU Fragments */
        sdu_frag_len = min(sdu_len - offset, HCI_iso_sdu_max);
        rc = os_mbuf_append(sdu, &os_mbuf_test_data[offset], sdu_frag_len);
        TEST_ASSERT_FATAL(rc == 0);

        offset += sdu_frag_len;
        num_pkt++;

        os_mbuf_concat(sdu, frag);
    }

    ble_ll_isoal_mux_sdu_enqueue(mux, sdu);

    return num_pkt;
}

static void
test_pdu_verify(uint8_t *pdu, int pdu_len, uint16_t sdu_offset)
{
    for (int i = 0; i < pdu_len; i++) {
        TEST_ASSERT(pdu[i] == os_mbuf_test_data[sdu_offset + i],
                    "PDU verification failed pdu[%d] %d != %d",
                    i, pdu[i], os_mbuf_test_data[sdu_offset + i]);
    }
}

struct test_ial_broadcast_single_sdu_bis_cfg {
    uint8_t NSE;
    uint8_t Framed;
    uint8_t Framing_Mode;
    uint8_t Max_PDU;
    uint8_t LLID;
    uint8_t BN;
    uint32_t SDU_Interval;
    uint32_t ISO_Interval;
};

static void
test_ial_teardown(struct ble_ll_isoal_mux *mux)
{
    ble_ll_isoal_mux_free(mux);
    TEST_ASSERT_FATAL(os_mbuf_mempool.mp_block_size == MBUF_TEST_POOL_BUF_SIZE,
                      "mp_block_size is %d", os_mbuf_mempool.mp_block_size);
    TEST_ASSERT_FATAL(os_mbuf_mempool.mp_num_free == MBUF_TEST_POOL_BUF_COUNT,
                      "mp_num_free is %d", os_mbuf_mempool.mp_num_free);
}

static void
test_ial_setup(struct ble_ll_isoal_mux *mux, uint8_t max_pdu,
               uint32_t iso_interval_us, uint32_t sdu_interval_us,
               uint8_t bn, uint8_t pte, bool framed, uint8_t framing_mode)
{
    ble_ll_isoal_mux_init(mux, max_pdu, iso_interval_us, sdu_interval_us,
                          bn, pte, framed, framing_mode);
}

static void
test_ial_broadcast_single_sdu_bis(const struct test_ial_broadcast_single_sdu_bis_cfg *cfg)
{
    struct ble_ll_isoal_mux mux;
    int num_completed_pkt;
    int pdu_len;
    uint32_t timeoffset;
    uint16_t seg_hdr;
    const uint8_t Max_SDU = 32;
    uint8_t pdu[cfg->Max_PDU];
    uint8_t llid = 0xff;

    test_ial_setup(&mux, cfg->Max_PDU, cfg->ISO_Interval,
                   cfg->SDU_Interval, cfg->BN, 0, cfg->Framed,
                   cfg->Framing_Mode);

    /* Send Single SDU */
    test_sdu_enqueue(&mux, Max_SDU, 0, 20000);

    ble_ll_isoal_mux_event_start(&mux, 30500);

    /* PDU #1 */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, pdu);
    TEST_ASSERT(llid == cfg->LLID, "LLID is incorrect %d", llid);

    if (cfg->Framed) {
        TEST_ASSERT(pdu_len == 2 /* Header */ + 3 /* TimeOffset */ + Max_SDU,
                    "PDU length is incorrect %d", pdu_len);
        seg_hdr = get_le16(&pdu[0]);
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seg_hdr) == 0, "SC is incorrect %d",
                    BLE_LL_ISOAL_SEGHDR_SC(seg_hdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seg_hdr) == 1, "CMPLT is incorrect %d",
                    BLE_LL_ISOAL_SEGHDR_CMPLT(seg_hdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_LEN(seg_hdr) == 3 /* TimeOffset */ + Max_SDU,
                    "Length is incorrect %d", BLE_LL_ISOAL_SEGHDR_LEN(seg_hdr));
        timeoffset = get_le24(&pdu[2]);
        TEST_ASSERT(timeoffset == 10500, "Time offset is incorrect %d", timeoffset);

        test_pdu_verify(&pdu[5], Max_SDU, 0);
    } else {
        TEST_ASSERT(pdu_len == Max_SDU, "PDU length is incorrect %d", pdu_len);

        test_pdu_verify(&pdu[0], Max_SDU, 0);
    }

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt > 0, "num_completed_pkt is incorrect %d", num_completed_pkt);

    test_ial_teardown(&mux);
}

TEST_CASE_SELF(test_ial_bis_unf_brd_bv_01_c) {
    const struct test_ial_broadcast_single_sdu_bis_cfg cfg = {
        .NSE = 2,
        .Framed = 0,
        .Framing_Mode = 0,
        .Max_PDU = 40,
        .LLID = 0b00,
        .BN = 1,
        .SDU_Interval = 10000,
        .ISO_Interval = 10000,
    };

    test_ial_broadcast_single_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_unf_brd_bv_02_c) {
    const struct test_ial_broadcast_single_sdu_bis_cfg cfg = {
        .NSE = 4,
        .Framed = 0,
        .Framing_Mode = 0,
        .Max_PDU = 40,
        .LLID = 0b00,
        .BN = 2,
        .SDU_Interval = 5000,
        .ISO_Interval = 10000,
    };

    test_ial_broadcast_single_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_brd_bv_06_c) {
    const struct test_ial_broadcast_single_sdu_bis_cfg cfg = {
        .NSE = 4,
        .Framed = 1,
        .Framing_Mode = 0,
        .Max_PDU = 40,
        .LLID = 0b10,
        .BN = 2,
        .SDU_Interval = 5000,
        .ISO_Interval = 10000,
    };

    test_ial_broadcast_single_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_brd_bv_08_c) {
    const struct test_ial_broadcast_single_sdu_bis_cfg cfg = {
        .NSE = 2,
        .Framed = 1,
        .Framing_Mode = 0,
        .Max_PDU = 40,
        .LLID = 0b10,
        .BN = 1,
        .SDU_Interval = 10000,
        .ISO_Interval = 10000,
    };

    test_ial_broadcast_single_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_brd_bv_29_c) {
    const struct test_ial_broadcast_single_sdu_bis_cfg cfg = {
        .NSE = 6,
        .Framed = 1,
        .Framing_Mode = 1,
        .Max_PDU = 32 + 5,
        .LLID = 0b10,
        .BN = 3,
        .SDU_Interval = 5000,
        .ISO_Interval = 10000,
    };

    test_ial_broadcast_single_sdu_bis(&cfg);
}

struct test_ial_broadcast_large_sdu_bis_cfg {
    uint8_t NSE;
    uint8_t Framed;
    uint8_t Framing_Mode;
    uint8_t BN;
    uint32_t SDU_Interval;
    uint32_t ISO_Interval;
};
struct test_ial_broadcast_large_sdu_bis_round {
    uint16_t sdu_len;
    uint8_t sc_packets_num;
};

static void
test_ial_broadcast_large_sdu_bis(const struct test_ial_broadcast_large_sdu_bis_cfg *cfg)
{
    const struct test_ial_broadcast_large_sdu_bis_round rounds[] = {
        {.sdu_len = 495, .sc_packets_num = 1},
        {.sdu_len = 503, .sc_packets_num = 2},
    };
    struct ble_ll_isoal_mux mux;
    /* const uint16_t Max_SDU = 503; */
    const uint8_t Max_PDU = 251;
    int num_completed_pkt;
    int num_expected_pkt;
    int pdu_len;
    uint8_t pdu[Max_PDU];
    uint32_t timestamp;
    uint16_t seg_hdr;
    uint16_t sdu_offset;
    uint8_t llid = 0xff;
    uint8_t sc_packets_num;
    uint8_t seg_len;
    uint8_t idx;

    test_ial_setup(&mux, Max_PDU, cfg->ISO_Interval,
                   cfg->SDU_Interval, cfg->BN, 0, cfg->Framed,
                   cfg->Framing_Mode);

    for (size_t round = 0; round < ARRAY_SIZE(rounds); round++) {
        sc_packets_num = 0;
        sdu_offset = 0;

        timestamp = (round + 1) * cfg->SDU_Interval;

        num_expected_pkt = test_sdu_enqueue(&mux, rounds[round].sdu_len, round, timestamp);

        ble_ll_isoal_mux_event_start(&mux, timestamp + 100);

        for (idx = 0; idx < cfg->BN; idx++) {
            pdu_len = ble_ll_isoal_mux_pdu_get(&mux, idx, &llid, pdu);
            if (pdu_len == 0) {
                TEST_ASSERT_FATAL(sdu_offset == rounds[round].sdu_len,
                                  "Round #%d: idx %d sdu_offset %d",
                                  round, idx, sdu_offset);
                continue;
            }

            /* The IUT sends the specified number of Start/Continuation
             * packets specified in Table 4.29 of ISO Data PDUs to the
             * Lower Tester with the LLID=0b01 for unframed payloads and
             * LLID=0b10 for framed payloads, and Payload Data every 251
             * bytes offset in step 1.
             */
            if (sc_packets_num < rounds[round].sc_packets_num) {
                TEST_ASSERT_FATAL(pdu_len == 251, "Round #%d: idx #%d: Length is incorrect %d",
                                  round, idx, pdu_len);

                if (cfg->Framed) {
                    TEST_ASSERT_FATAL(llid == 0b10, "Round #%d: LLID is incorrect %d", round, llid);

                    seg_hdr = get_le16(&pdu[0]);
                    seg_len = BLE_LL_ISOAL_SEGHDR_LEN(seg_hdr);
                    if (idx == 0) {
                        TEST_ASSERT_FATAL(BLE_LL_ISOAL_SEGHDR_SC(seg_hdr) == 0,
                                          "Round #%d: SC is incorrect %d",
                                          round, BLE_LL_ISOAL_SEGHDR_SC(seg_hdr));

                        test_pdu_verify(&pdu[5], seg_len - 3, 0);
                        sdu_offset += seg_len - 3;
                    } else {
                        TEST_ASSERT_FATAL(BLE_LL_ISOAL_SEGHDR_SC(seg_hdr) == 1,
                                          "Round #%d: SC is incorrect %d",
                                          round, BLE_LL_ISOAL_SEGHDR_SC(seg_hdr));

                        test_pdu_verify(&pdu[2], seg_len, sdu_offset);
                        sdu_offset += seg_len;
                    }
                } else {
                    TEST_ASSERT_FATAL(llid == 0b01, "Round #%d: LLID is incorrect %d", round, llid);

                    test_pdu_verify(&pdu[0], pdu_len, sdu_offset);
                    sdu_offset += pdu_len;
                }

                sc_packets_num++;
            } else {
                /* The IUT sends the last ISO Data PDU to the Lower Tester
                 * with the LLID=0b00 for unframed payloads and LLID=0b10
                 * for framed payloads, with the remaining Payload Data.
                 */
                if (cfg->Framed) {
                    TEST_ASSERT_FATAL(pdu_len == rounds[round].sdu_len - sdu_offset + 2,
                                      "Round #%d: idx %d: PDU length is incorrect %d != %d",
                                      round, idx, pdu_len, rounds[round].sdu_len - sdu_offset + 2);
                    TEST_ASSERT_FATAL(llid == 0b10, "Round #%d: LLID is incorrect %d",
                                      round, llid);

                    seg_hdr = get_le16(&pdu[0]);
                    TEST_ASSERT_FATAL(BLE_LL_ISOAL_SEGHDR_SC(seg_hdr),
                                      "Round #%d: SC is incorrect %d",
                                      round, BLE_LL_ISOAL_SEGHDR_SC(seg_hdr));
                    TEST_ASSERT_FATAL(BLE_LL_ISOAL_SEGHDR_CMPLT(seg_hdr),
                                      "Round #%d: CMPLT is incorrect %d",
                                      round, BLE_LL_ISOAL_SEGHDR_CMPLT(seg_hdr));
                    seg_len = BLE_LL_ISOAL_SEGHDR_LEN(seg_hdr);

                    test_pdu_verify(&pdu[2], seg_len, sdu_offset);
                    sdu_offset += seg_len;
                } else {
                    TEST_ASSERT_FATAL(pdu_len == rounds[round].sdu_len - sdu_offset,
                                      "Round #%d: idx %d: PDU length is incorrect %d != %d",
                                      round, idx, pdu_len, rounds[round].sdu_len - sdu_offset);
                    TEST_ASSERT_FATAL(llid == 0b00, "Round #%d: LLID is incorrect %d", round, llid);

                    test_pdu_verify(&pdu[0], pdu_len, sdu_offset);
                    sdu_offset += pdu_len;
                }
            }
        }

        num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
        TEST_ASSERT(num_completed_pkt == num_expected_pkt,
                    "num_completed_pkt %d != %d", num_completed_pkt, num_expected_pkt);
    }

    test_ial_teardown(&mux);
}

TEST_CASE_SELF(test_ial_bis_unf_brd_bv_09_c) {
    const struct test_ial_broadcast_large_sdu_bis_cfg cfg = {
        .NSE = 12,
        .Framed = 0,
        .Framing_Mode = 0,
        .BN = 6,
        .SDU_Interval = 20000,
        .ISO_Interval = 40000,
    };

    test_ial_broadcast_large_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_unf_brd_bv_10_c) {
    const struct test_ial_broadcast_large_sdu_bis_cfg cfg = {
        .NSE = 6,
        .Framed = 0,
        .Framing_Mode = 0,
        .BN = 4,
        .SDU_Interval = 20000,
        .ISO_Interval = 20000,
    };

    test_ial_broadcast_large_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_unf_brd_bv_11_c) {
    const struct test_ial_broadcast_large_sdu_bis_cfg cfg = {
        .NSE = 8,
        .Framed = 0,
        .Framing_Mode = 0,
        .BN = 4,
        .SDU_Interval = 25000,
        .ISO_Interval = 25000,
    };

    test_ial_broadcast_large_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_brd_bv_13_c) {
    const struct test_ial_broadcast_large_sdu_bis_cfg cfg = {
        .NSE = 10,
        .Framed = 1,
        .Framing_Mode = 0,
        .BN = 5,
        .SDU_Interval = 15000,
        .ISO_Interval = 30000,
    };

    test_ial_broadcast_large_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_brd_bv_15_c) {
    const struct test_ial_broadcast_large_sdu_bis_cfg cfg = {
        .NSE = 6,
        .Framed = 1,
        .Framing_Mode = 0,
        .BN = 3,
        .SDU_Interval = 20000,
        .ISO_Interval = 20000,
    };

    test_ial_broadcast_large_sdu_bis(&cfg);
}

struct test_ial_broadcast_multiple_small_sdus_bis_cfg {
    uint8_t NSE;
    uint8_t BN;
    uint8_t Max_PDU;
    uint32_t SDU_Interval;
    uint32_t ISO_Interval;
};

static void
test_ial_broadcast_multiple_small_sdus_bis(const struct test_ial_broadcast_multiple_small_sdus_bis_cfg *cfg)
{
    struct ble_ll_isoal_mux mux;
    /* const uint16_t Max_SDU = 25; */
    const uint8_t LLID = 0b10;
    const uint8_t Framed = 0x01;
    const uint8_t Framing_Mode = 0;
    int pdu_len;
    uint8_t pdu[cfg->Max_PDU];
    uint32_t sdu_1_ts, sdu_2_ts, event_ts;
    uint32_t timeoffset;
    uint16_t seg_hdr;
    uint8_t llid = 0xff;
    uint8_t seg_len;
    uint8_t *seg;

    test_ial_setup(&mux, cfg->Max_PDU, cfg->ISO_Interval,
                   cfg->SDU_Interval, cfg->BN, 0, Framed,
                   Framing_Mode);

    /* The Upper Tester sends to the IUT a small SDU1 with data length of 20 bytes. */
    sdu_1_ts = 100;
    test_sdu_enqueue(&mux, 20, 0, sdu_1_ts);

    /* The Upper Tester sends to the IUT a small SDU2 with data length of 25 bytes. */
    sdu_2_ts = sdu_1_ts + cfg->SDU_Interval;
    test_sdu_enqueue(&mux, 25, 0, sdu_2_ts);

    event_ts = sdu_2_ts + 200;
    ble_ll_isoal_mux_event_start(&mux, event_ts);

    /* The IUT sends a single Broadcast ISO Data PDU with SDU1 followed by SDU2 over the BIS.
     * Each SDU header has SC = 0 and CMPT = 1.
     */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, pdu);
    TEST_ASSERT(llid == LLID, "LLID is incorrect %d", llid);

    /* SDU 1 */
    seg = &pdu[0];
    TEST_ASSERT(pdu_len > 24, "PDU length is incorrect %d", pdu_len);
    seg_hdr = get_le16(&seg[0]);
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seg_hdr) == 0,
                "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_SC(seg_hdr));
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seg_hdr) == 1,
                "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_CMPLT(seg_hdr));
    seg_len = BLE_LL_ISOAL_SEGHDR_LEN(seg_hdr);
    TEST_ASSERT(seg_len == 20 + 3, "Segment length is incorrect %d", pdu_len);
    timeoffset = get_le24(&seg[2]);
    TEST_ASSERT(timeoffset == event_ts - sdu_1_ts,
                "Time offset is incorrect %d", timeoffset);

    /* SDU 1 */
    seg = &pdu[25];
    TEST_ASSERT(pdu_len == 55, "PDU length is incorrect %d", pdu_len);
    seg_hdr = get_le16(&seg[0]);
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seg_hdr) == 0,
                "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_SC(seg_hdr));
    TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seg_hdr) == 1,
                "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_CMPLT(seg_hdr));
    seg_len = BLE_LL_ISOAL_SEGHDR_LEN(seg_hdr);
    TEST_ASSERT(seg_len == 25 + 3, "Segment length is incorrect %d", pdu_len);
    timeoffset = get_le24(&seg[2]);
    TEST_ASSERT(timeoffset == event_ts - sdu_2_ts,
                "Time offset is incorrect %d", timeoffset);

    (void)ble_ll_isoal_mux_event_done(&mux);

    test_ial_teardown(&mux);
}

TEST_CASE_SELF(test_ial_bis_fra_brd_bv_17_c) {
    const struct test_ial_broadcast_multiple_small_sdus_bis_cfg cfg = {
        .NSE = 2,
        .BN = 1,
        .Max_PDU = 68,
        .SDU_Interval = 500,
        .ISO_Interval = 1000,
    };

    test_ial_broadcast_multiple_small_sdus_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_brd_bv_18_c) {
    const struct test_ial_broadcast_multiple_small_sdus_bis_cfg cfg = {
        .NSE = 2,
        .BN = 1,
        .Max_PDU = 68,
        .SDU_Interval = 1000,
        .ISO_Interval = 2000,
    };

    test_ial_broadcast_multiple_small_sdus_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_brd_bv_20_c) {
    const struct test_ial_broadcast_multiple_small_sdus_bis_cfg cfg = {
        .NSE = 4,
        .BN = 2,
        .Max_PDU = 65,
        .SDU_Interval = 500,
        .ISO_Interval = 2000,
    };

    test_ial_broadcast_multiple_small_sdus_bis(&cfg);
}

struct test_ial_segmentation_header {
    uint8_t SC;
    uint8_t CMPLT;
    uint8_t LENGTH;
};
struct test_ial_broadcast_zero_length_sdu_bis_cfg {
    uint8_t NSE;
    uint8_t Framed;
    uint8_t Framing_Mode;
    uint8_t LLID;
    uint8_t BN;
    struct test_ial_segmentation_header Segmentation_Header;
    bool Time_Offset;
};

static void
test_ial_broadcast_zero_length_sdu_bis(const struct test_ial_broadcast_zero_length_sdu_bis_cfg *cfg)
{
    struct ble_ll_isoal_mux mux;
    const uint32_t ISO_Interval = 10000;
    const uint32_t SDU_Interval = 10000;
    /* const uint16_t Max_SDU = 32; */
    const uint16_t Max_PDU = 32;
    int pdu_len;
    uint8_t pdu[Max_PDU];
    uint32_t timeoffset;
    uint16_t seg_hdr;
    uint8_t llid = 0xff;

    test_ial_setup(&mux, Max_PDU, ISO_Interval, SDU_Interval,
                   cfg->BN, 0, cfg->Framed, cfg->Framing_Mode);

    /* The Upper Tester sends an HCI ISO Data packet to the IUT with zero data length. */
    test_sdu_enqueue(&mux, 0, 0, 100);

    ble_ll_isoal_mux_event_start(&mux, 500);

    /* The IUT sends a single Broadcast ISO Data PDU with the LLID,
     * Framed, Framing_Mode, the segmentation header and time offset
     * fields as specified in Table 4.35. Length is 0 if LLID is 0b00
     * and is 5 (Segmentation Header + TimeOffset) if LLID is 0b10.
     * SDU field is empty..
     */
    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, pdu);
    TEST_ASSERT(llid == cfg->LLID, "LLID is incorrect %d", llid);

    if (cfg->LLID == 0b00) {
        TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
    } else if (cfg->LLID == 0b01) {
        TEST_ASSERT(pdu_len == 5, "PDU length is incorrect %d", pdu_len);
    }

    if (cfg->Framed) {
        seg_hdr = get_le16(&pdu[0]);
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_SC(seg_hdr) == cfg->Segmentation_Header.SC,
                    "SC is incorrect %d", BLE_LL_ISOAL_SEGHDR_SC(seg_hdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_CMPLT(seg_hdr) == cfg->Segmentation_Header.CMPLT,
                    "CMPLT is incorrect %d", BLE_LL_ISOAL_SEGHDR_CMPLT(seg_hdr));
        TEST_ASSERT(BLE_LL_ISOAL_SEGHDR_LEN(seg_hdr) == cfg->Segmentation_Header.LENGTH,
                    "LENGTH is incorrect %d", BLE_LL_ISOAL_SEGHDR_LEN(seg_hdr));
        timeoffset = get_le24(&pdu[2]);
        TEST_ASSERT(timeoffset == 400, "Time offset is incorrect %d", timeoffset);
    }

    for (uint8_t idx = 1; idx < cfg->BN; idx++) {
        pdu_len = ble_ll_isoal_mux_pdu_get(&mux, idx, &llid, pdu);
        TEST_ASSERT(llid == cfg->LLID, "LLID is incorrect %d", llid);
        TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
    }

    (void)ble_ll_isoal_mux_event_done(&mux);

    test_ial_teardown(&mux);
}

TEST_CASE_SELF(test_ial_bis_unf_brd_bv_21_c) {
    const struct test_ial_broadcast_zero_length_sdu_bis_cfg cfg = {
        .NSE = 4,
        .Framed = 0,
        .Framing_Mode = 0,
        .LLID = 0b00,
        .BN = 2,
    };

    test_ial_broadcast_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_unf_brd_bv_22_c) {
    const struct test_ial_broadcast_zero_length_sdu_bis_cfg cfg = {
        .NSE = 6,
        .Framed = 0,
        .Framing_Mode = 0,
        .LLID = 0b00,
        .BN = 3,
    };

    test_ial_broadcast_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_unf_brd_bv_23_c) {
    const struct test_ial_broadcast_zero_length_sdu_bis_cfg cfg = {
        .NSE = 1,
        .Framed = 0,
        .Framing_Mode = 0,
        .LLID = 0b00,
        .BN = 1,
    };

    test_ial_broadcast_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_unf_brd_bv_24_c) {
    const struct test_ial_broadcast_zero_length_sdu_bis_cfg cfg = {
        .NSE = 2,
        .Framed = 0,
        .Framing_Mode = 0,
        .LLID = 0b00,
        .BN = 1,
    };

    test_ial_broadcast_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_brd_bv_25_c) {
    const struct test_ial_broadcast_zero_length_sdu_bis_cfg cfg = {
        .NSE = 6,
        .Framed = 1,
        .Framing_Mode = 0,
        .LLID = 0b10,
        .BN = 2,
        .Segmentation_Header.SC = 0,
        .Segmentation_Header.CMPLT = 1,
        .Segmentation_Header.LENGTH = 3,
        .Time_Offset = true,
    };

    test_ial_broadcast_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_brd_bv_26_c) {
    const struct test_ial_broadcast_zero_length_sdu_bis_cfg cfg = {
        .NSE = 2,
        .Framed = 1,
        .Framing_Mode = 0,
        .LLID = 0b10,
        .BN = 1,
        .Segmentation_Header.SC = 0,
        .Segmentation_Header.CMPLT = 1,
        .Segmentation_Header.LENGTH = 3,
        .Time_Offset = true,
    };

    test_ial_broadcast_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_brd_bv_27_c) {
    const struct test_ial_broadcast_zero_length_sdu_bis_cfg cfg = {
        .NSE = 4,
        .Framed = 1,
        .Framing_Mode = 0,
        .LLID = 0b10,
        .BN = 1,
        .Segmentation_Header.SC = 0,
        .Segmentation_Header.CMPLT = 1,
        .Segmentation_Header.LENGTH = 3,
        .Time_Offset = true,
    };

    test_ial_broadcast_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_brd_bv_28_c) {
    const struct test_ial_broadcast_zero_length_sdu_bis_cfg cfg = {
        .NSE = 6,
        .Framed = 1,
        .Framing_Mode = 0,
        .LLID = 0b10,
        .BN = 3,
        .Segmentation_Header.SC = 0,
        .Segmentation_Header.CMPLT = 1,
        .Segmentation_Header.LENGTH = 3,
        .Time_Offset = true,
    };

    test_ial_broadcast_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_brd_bv_30_c) {
    const struct test_ial_broadcast_zero_length_sdu_bis_cfg cfg = {
        .NSE = 6,
        .Framed = 1,
        .Framing_Mode = 1,
        .LLID = 0b10,
        .BN = 2,
        .Segmentation_Header.SC = 0,
        .Segmentation_Header.CMPLT = 1,
        .Segmentation_Header.LENGTH = 3,
        .Time_Offset = true,
    };

    test_ial_broadcast_zero_length_sdu_bis(&cfg);
}

struct test_ial_unframed_empty_pdus_with_llid_0b01_cfg {
    uint32_t sdu_int;
    uint32_t iso_int;
    uint8_t nse;
    uint16_t mx_sdu;
    uint8_t mx_pdu;
    uint8_t bn;
    uint8_t irc;
    uint8_t pto;
};

static void
test_ial_unframed_empty_pdus_with_llid_0b01(const struct test_ial_unframed_empty_pdus_with_llid_0b01_cfg *cfg)
{
    struct ble_ll_isoal_mux mux;
    int pdu_len;
    uint8_t pdu[cfg->mx_pdu];
    uint32_t timestamp;
    uint8_t llid = 0xff;

    ble_ll_isoal_mux_init(&mux, cfg->mx_pdu, cfg->iso_int, cfg->sdu_int,
                          cfg->bn, 0, false, 0);

    for (uint16_t sdu_len = 4; sdu_len < cfg->mx_sdu; sdu_len++) {
        timestamp = sdu_len * cfg->sdu_int;
        test_sdu_enqueue(&mux, sdu_len, sdu_len, timestamp);

        ble_ll_isoal_mux_event_start(&mux, timestamp + 50);

        /* As the mx_sdu == mx_pdu, the data will always fit the single PDU */
        TEST_ASSERT(cfg->mx_sdu == cfg->mx_pdu,
                    "#%d: SDU and PDU length should be same", sdu_len);

        pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, pdu);
        TEST_ASSERT(llid == 0b00,
                    "#%d: LLID is incorrect %d", sdu_len, llid);
        TEST_ASSERT(pdu_len == sdu_len,
                    "#%d: PDU length is incorrect %d", sdu_len, pdu_len);

        /* Padding */
        for (uint8_t idx = 1; idx < cfg->bn; idx++) {
            pdu_len = ble_ll_isoal_mux_pdu_get(&mux, idx, &llid, pdu);
            TEST_ASSERT(llid == 0b01,
                        "#%d #%d: LLID is incorrect %d", sdu_len, idx, llid);
            TEST_ASSERT(pdu_len == 0,
                        "#%d #%d: PDU length is incorrect %d",
                        sdu_len, idx, pdu_len);
        }

        (void)ble_ll_isoal_mux_event_done(&mux);
    }

    ble_ll_isoal_mux_free(&mux);
}

TEST_CASE_SELF(test_ial_bis_unf_brd_bv_29_c) {
    const struct test_ial_unframed_empty_pdus_with_llid_0b01_cfg cfg = {
        .sdu_int = 100,
        .iso_int = 100,
        .nse = 12,
        .mx_sdu = 128,
        .mx_pdu = 128,
        .bn = 4,
        .irc = 3,
        .pto = 0,
    };

    test_ial_unframed_empty_pdus_with_llid_0b01(&cfg);
}

TEST_CASE_SELF(test_ial_bis_unf_early_sdus) {
    struct ble_ll_isoal_mux mux;
    const uint32_t sdu_int = 7500;
    const uint32_t iso_int = 7500;
    /* const uint16_t mx_sdu = 40; */
    const uint8_t mx_pdu = 40;
    const uint8_t bn = 4;
    int num_completed_pkt;
    int pdu_len;
    uint8_t pdu[mx_pdu];
    uint32_t timestamp = 0;
    uint8_t llid = 0xff;

    test_ial_setup(&mux, mx_pdu, iso_int, sdu_int, bn, 0, false, 0);

    test_sdu_enqueue(&mux, 21, 0, timestamp++);
    test_sdu_enqueue(&mux, 32, 0, timestamp++);
    test_sdu_enqueue(&mux, 40, 0, timestamp++);

    ble_ll_isoal_mux_event_start(&mux, timestamp + 50);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b00, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 21, "PDU length is incorrect %d", pdu_len);
    test_pdu_verify(pdu, pdu_len, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 2, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 3, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt == 1,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    ble_ll_isoal_mux_event_start(&mux, timestamp + 50 + iso_int);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b00, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 32, "PDU length is incorrect %d", pdu_len);
    test_pdu_verify(pdu, pdu_len, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 2, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 3, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt == 1,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    ble_ll_isoal_mux_event_start(&mux, timestamp + 50 + 2 * iso_int);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b00, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 40, "PDU length is incorrect %d", pdu_len);
    test_pdu_verify(pdu, pdu_len, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 2, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 3, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt == 1,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    test_ial_teardown(&mux);
}

TEST_CASE_SELF(test_ial_bis_fra_early_sdus) {
    struct ble_ll_isoal_mux mux;
    const uint32_t sdu_int = 87072;
    const uint32_t iso_int = 87500;
    const uint16_t mx_sdu = 32;
    const uint8_t mx_pdu = 37;
    const uint8_t bn = 2;
    int num_completed_pkt;
    int pdu_len;
    uint8_t pdu[mx_pdu];
    uint32_t timestamp = 0;
    uint8_t llid = 0xff;

    test_ial_setup(&mux, mx_pdu, iso_int, sdu_int, bn, 0, true, 0);

    for (int seq_num = 0; seq_num < 10; seq_num++) {
        test_sdu_enqueue(&mux, mx_sdu, seq_num, timestamp++);
    }

    ble_ll_isoal_mux_event_start(&mux, timestamp);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_pdu_verify(&pdu[5], mx_sdu, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_pdu_verify(&pdu[5], mx_sdu, 0);

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt == 2,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    ble_ll_isoal_mux_event_start(&mux, timestamp);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_pdu_verify(&pdu[5], mx_sdu, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_pdu_verify(&pdu[5], mx_sdu, 0);

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt == 2,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    ble_ll_isoal_mux_event_start(&mux, timestamp);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_pdu_verify(&pdu[5], mx_sdu, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_pdu_verify(&pdu[5], mx_sdu, 0);

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt == 2,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    ble_ll_isoal_mux_event_start(&mux, timestamp);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_pdu_verify(&pdu[5], mx_sdu, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_pdu_verify(&pdu[5], mx_sdu, 0);

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt == 2,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    ble_ll_isoal_mux_event_start(&mux, timestamp);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_pdu_verify(&pdu[5], mx_sdu, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(&mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_pdu_verify(&pdu[5], mx_sdu, 0);

    num_completed_pkt = ble_ll_isoal_mux_event_done(&mux);
    TEST_ASSERT(num_completed_pkt == 2,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    test_ial_teardown(&mux);
}

TEST_SUITE(ble_ll_isoal_test_suite) {
    os_mbuf_test_setup();

    ble_ll_isoal_init();

    test_ble_ll_isoal_mux_init();
    test_ble_ll_isoal_mux_get_unframed_pdu();
    test_ble_ll_isoal_mux_sdu_not_in_event();

    /* Broadcast Single SDU, BIS */
    test_ial_bis_unf_brd_bv_01_c();
    test_ial_bis_unf_brd_bv_02_c();
    test_ial_bis_fra_brd_bv_06_c();
    test_ial_bis_fra_brd_bv_08_c();
    test_ial_bis_fra_brd_bv_29_c();

    /* Broadcast Large SDU, BIS */
    test_ial_bis_unf_brd_bv_09_c();
    test_ial_bis_unf_brd_bv_10_c();
    test_ial_bis_unf_brd_bv_11_c();
    test_ial_bis_fra_brd_bv_13_c();
    test_ial_bis_fra_brd_bv_15_c();

    /* Broadcast Multiple, Small SDUs, BIS */
    test_ial_bis_fra_brd_bv_17_c();
    test_ial_bis_fra_brd_bv_18_c();
    test_ial_bis_fra_brd_bv_20_c();

    /* Broadcast a Zero-Length SDU, BIS */
    test_ial_bis_unf_brd_bv_21_c();
    test_ial_bis_unf_brd_bv_22_c();
    test_ial_bis_unf_brd_bv_23_c();
    test_ial_bis_unf_brd_bv_24_c();
    test_ial_bis_fra_brd_bv_25_c();
    test_ial_bis_fra_brd_bv_26_c();
    test_ial_bis_fra_brd_bv_27_c();
    test_ial_bis_fra_brd_bv_28_c();
    test_ial_bis_fra_brd_bv_30_c();

    /* Broadcasting Unframed Empty PDUs with LLID=0b01, BIS */
    test_ial_bis_unf_brd_bv_29_c();
    /* test_ial_bis_unf_brd_bv_30_c();
     * Same as test_ial_bis_unf_brd_bv_29_c except encryption is required.
     */

    test_ial_bis_unf_early_sdus();
    test_ial_bis_fra_early_sdus();

    ble_ll_isoal_reset();
}
