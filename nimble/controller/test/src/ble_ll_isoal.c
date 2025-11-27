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
#include <controller/ble_ll_isoal.h>
#include <controller/ble_ll_tmr.h>
#include <nimble/ble.h>
#include <nimble/hci_common.h>
#include <os/os_mbuf.h>
#include <stdint.h>
#include <sys/queue.h>
#include <testutil/testutil.h>

struct test_ial_userhdr {
    uint32_t timestamp;
    uint16_t seq_num;
    uint8_t pkt_status;
};

#define TSPX_max_sdu_length (754)
#define HCI_iso_sdu_max     (MYNEWT_VAL(BLE_TRANSPORT_ISO_SIZE) - 4)

#define TEST_PKTHDR_OVERHEAD                                                  \
    (sizeof(struct os_mbuf_pkthdr) + sizeof(struct test_ial_userhdr))
#define TEST_BUF_OVERHEAD (sizeof(struct os_mbuf) + TEST_PKTHDR_OVERHEAD)

#define TEST_DATA_PKT_LEN                                                     \
    (TSPX_max_sdu_length + sizeof(struct ble_hci_iso_data))
#define TEST_DATA_BUF_SIZE                                                    \
    OS_ALIGN(TEST_DATA_PKT_LEN + sizeof(struct ble_hci_iso), 4)

#define TEST_BUF_COUNT     (20)
#define TEST_BUF_SIZE      (TEST_DATA_BUF_SIZE + TEST_BUF_OVERHEAD)
#define TEST_BUF_POOL_SIZE OS_MEMPOOL_SIZE(TEST_BUF_COUNT, TEST_BUF_SIZE)

static struct os_mbuf_pool g_mbuf_pool;
static struct os_mempool g_mbuf_mempool;
static os_membuf_t g_mbuf_buffer[TEST_BUF_POOL_SIZE];
static uint8_t g_test_sdu_data[TSPX_max_sdu_length];

STAILQ_HEAD(, os_mbuf_pkthdr) sdu_q = STAILQ_HEAD_INITIALIZER(sdu_q);

void
ble_ll_isoal_test_suite_init(void)
{
    int rc;
    int i;

    rc = os_mempool_init(&g_mbuf_mempool, TEST_BUF_COUNT, TEST_BUF_SIZE,
                         &g_mbuf_buffer[0], "mbuf_pool");
    TEST_ASSERT_FATAL(rc == 0, "Error creating memory pool %d", rc);

    rc = os_mbuf_pool_init(&g_mbuf_pool, &g_mbuf_mempool, TEST_BUF_SIZE, TEST_BUF_COUNT);
    TEST_ASSERT_FATAL(rc == 0, "Error creating mbuf pool %d", rc);

    for (i = 0; i < sizeof g_test_sdu_data; i++) {
        g_test_sdu_data[i] = i;
    }
}

static void
isoal_demux_sdu_cb(struct ble_ll_isoal_demux *demux, const struct os_mbuf *sdu,
                   uint32_t timestamp, uint16_t seq_num, bool valid)
{
    struct test_ial_userhdr *userhdr;
    struct os_mbuf_pkthdr *pkthdr;
    struct os_mbuf *om;
    uint16_t sdu_len;
    void *data;

    om = os_mbuf_get_pkthdr(&g_mbuf_pool, sizeof(*userhdr));
    TEST_ASSERT_FATAL(om != NULL);

    userhdr = OS_MBUF_USRHDR(om);
    if (sdu == NULL) {
        userhdr->pkt_status = BLE_HCI_ISO_PKT_STATUS_LOST;
    } else if (valid) {
        userhdr->pkt_status = BLE_HCI_ISO_PKT_STATUS_VALID;
    } else {
        userhdr->pkt_status = BLE_HCI_ISO_PKT_STATUS_INVALID;
    }
    userhdr->timestamp = timestamp;
    userhdr->seq_num = seq_num;

    if (sdu != NULL) {
        sdu_len = os_mbuf_len(sdu);

        data = os_mbuf_extend(om, sdu_len);
        TEST_ASSERT_FATAL(data != NULL);

        os_mbuf_copydata(sdu, 0, sdu_len, data);
    }

    pkthdr = OS_MBUF_PKTHDR(om);
    STAILQ_INSERT_TAIL(&sdu_q, pkthdr, omp_next);
}

static struct ble_ll_isoal_demux_cb isoal_demux_cb = {
    .sdu_cb = isoal_demux_sdu_cb,
};

struct test_ll_isoal_fixture {
    struct ble_ll_isoal_mux mux;
    struct ble_ll_isoal_demux demux;
};

static void
test_ll_isoal_setup(struct test_ll_isoal_fixture *fixture, uint16_t max_sdu,
                    uint8_t max_pdu, uint32_t iso_interval_us,
                    uint32_t sdu_interval_us, uint8_t bn, bool framed, bool unsegmented)
{
    struct ble_ll_isoal_config config = {
        .iso_interval_us = iso_interval_us,
        .sdu_interval_us = sdu_interval_us,
        .max_sdu = max_sdu,
        .max_pdu = max_pdu,
        .bn = bn,
        .pte = 0,
        .framed = framed,
        .unsegmented = unsegmented,
    };

    ble_ll_isoal_mux_init(&fixture->mux, &config);

    ble_ll_isoal_demux_init(&fixture->demux, &config);
    ble_ll_isoal_demux_cb_set(&fixture->demux, &isoal_demux_cb);

    STAILQ_INIT(&sdu_q);
}

static void
test_ll_isoal_teardown(struct test_ll_isoal_fixture *fixture)
{
    struct os_mbuf_pkthdr *pkthdr;
    struct os_mbuf *om;

    ble_ll_isoal_mux_reset(&fixture->mux);
    ble_ll_isoal_demux_reset(&fixture->demux);

    pkthdr = STAILQ_FIRST(&sdu_q);
    while (pkthdr) {
        om = OS_MBUF_PKTHDR_TO_MBUF(pkthdr);

        os_mbuf_free_chain(om);

        STAILQ_REMOVE_HEAD(&sdu_q, omp_next);

        pkthdr = STAILQ_FIRST(&sdu_q);
    }

    STAILQ_INIT(&sdu_q);

    /* Memory leak test */
    TEST_ASSERT_FATAL(g_mbuf_mempool.mp_block_size == TEST_BUF_SIZE,
                      "mp_block_size is %d", g_mbuf_mempool.mp_block_size);
    TEST_ASSERT_FATAL(g_mbuf_mempool.mp_num_free == TEST_BUF_COUNT,
                      "mp_num_free is %d", g_mbuf_mempool.mp_num_free);
}

TEST_CASE_SELF(test_ble_ll_isoal_mux_init)
{
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_mux *mux = &fixture.mux;
    const uint32_t iso_interval_us = 10000;
    const uint32_t sdu_interval_us = 10000;

    test_ll_isoal_setup(&fixture, 250, 250, iso_interval_us, sdu_interval_us,
                        1, false, false);

    TEST_ASSERT(mux->pdu_per_sdu == sdu_interval_us / iso_interval_us);

    test_ll_isoal_teardown(&fixture);
}

TEST_CASE_SELF(ble_ll_isoal_mux_pdu_get_unframed_1_sdu_2_pdu) {
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_mux *mux = &fixture.mux;
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

    test_ll_isoal_setup(&fixture, sdu_len, max_pdu, iso_interval_us,
                        sdu_interval_us, bn, Framed, Framing_Mode);

    /* SDU #1 */
    sdu_1 = os_mbuf_get_pkthdr(&g_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_1 != NULL);
    rc = os_mbuf_append(sdu_1, g_test_sdu_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_put(mux, sdu_1);

    /* SDU #2 */
    sdu_2 = os_mbuf_get_pkthdr(&g_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_2 != NULL);
    rc = os_mbuf_append(sdu_2, g_test_sdu_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_put(mux, sdu_2);

    ble_ll_isoal_mux_event_start(mux, 90990);

    /* PDU #1 */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; start or continuation fragment of an SDU. */
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);

    /* PDU #2 */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 1, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; start or continuation fragment of an SDU. */
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);

    /* PDU #3 */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 2, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; end fragment of an SDU or a complete SDU. */
    TEST_ASSERT(llid == 0b00, "LLID is incorrect %d", llid);

    /* PDU #4 */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; start or continuation fragment of an SDU. */
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);

    /* PDU #5 */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 1, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; start or continuation fragment of an SDU. */
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);

    /* PDU #6 */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 2, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; end fragment of an SDU or a complete SDU. */
    TEST_ASSERT(llid == 0b00, "LLID is incorrect %d", llid);

    num_completed_pkt = ble_ll_isoal_mux_event_done(mux);
    TEST_ASSERT(num_completed_pkt > 0, "num_completed_pkt is incorrect %d", num_completed_pkt);

    test_ll_isoal_teardown(&fixture);
}

TEST_CASE_SELF(test_ble_ll_isoal_mux_get_unframed_pdu) {
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_mux *mux = &fixture.mux;
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

    test_ll_isoal_setup(&fixture, sdu_len, max_pdu, iso_interval_us,
                        sdu_interval_us, bn, Framed, Framing_Mode);

    /* SDU #1 */
    sdu_1 = os_mbuf_get_pkthdr(&g_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_1 != NULL);
    rc = os_mbuf_append(sdu_1, g_test_sdu_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_put(mux, sdu_1);

    /* SDU #2 */
    sdu_2 = os_mbuf_get_pkthdr(&g_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_2 != NULL);
    rc = os_mbuf_append(sdu_2, g_test_sdu_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_put(mux, sdu_2);

    ble_ll_isoal_mux_event_start(mux, 90990);

    /* PDU #1 */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; start or continuation fragment of an SDU. */
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);

    /* PDU #2 */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 1, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; start or continuation fragment of an SDU. */
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);

    /* PDU #3 */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 2, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; end fragment of an SDU or a complete SDU. */
    TEST_ASSERT(llid == 0b00, "LLID is incorrect %d", llid);

    /* PDU #4 */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; start or continuation fragment of an SDU. */
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);

    /* PDU #5 */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 1, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; start or continuation fragment of an SDU. */
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);

    /* PDU #6 */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 2, &llid, data);
    TEST_ASSERT(pdu_len == max_pdu, "PDU length is incorrect %d", pdu_len);
    /* Unframed CIS Data PDU; end fragment of an SDU or a complete SDU. */
    TEST_ASSERT(llid == 0b00, "LLID is incorrect %d", llid);

    num_completed_pkt = ble_ll_isoal_mux_event_done(mux);
    TEST_ASSERT(num_completed_pkt > 0, "num_completed_pkt is incorrect %d",
                num_completed_pkt);

    test_ll_isoal_teardown(&fixture);
}

TEST_CASE_SELF(test_ble_ll_isoal_mux_sdu_not_in_event) {
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_mux *mux = &fixture.mux;
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

    test_ll_isoal_setup(&fixture, sdu_len, max_pdu, iso_interval_us,
                        sdu_interval_us, bn, Framed, Framing_Mode);

    ble_ll_isoal_mux_event_start(mux, 90990);
    TEST_ASSERT_FATAL(mux->sdu_in_event == 0, "sdu_in_event %d != 0", mux->sdu_in_event);

    /* SDU #1 */
    sdu_1 = os_mbuf_get_pkthdr(&g_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu_1 != NULL);
    rc = os_mbuf_append(sdu_1, g_test_sdu_data, sdu_len);
    TEST_ASSERT_FATAL(rc == 0);
    ble_ll_isoal_mux_sdu_put(mux, sdu_1);

    TEST_ASSERT_FATAL(mux->sdu_in_event == 0, "sdu_in_event %d != 0", mux->sdu_in_event);

    /* PDU #1 */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, data);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

    /* PDU #2 */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 1, &llid, data);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);

    num_completed_pkt = ble_ll_isoal_mux_event_done(mux);
    TEST_ASSERT(num_completed_pkt == 0, "num_completed_pkt is incorrect %d",
                num_completed_pkt);

    test_ll_isoal_teardown(&fixture);
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

    sdu = os_mbuf_get_pkthdr(&g_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(sdu != NULL);
    blehdr = BLE_MBUF_HDR_PTR(sdu);
    blehdr->txiso.packet_seq_num = packet_seq_num;
    blehdr->txiso.cpu_timestamp = timestamp;

    /* First SDU Fragment */
    sdu_frag_len = min(sdu_len, HCI_iso_sdu_max);
    rc = os_mbuf_append(sdu, g_test_sdu_data, sdu_frag_len);
    TEST_ASSERT_FATAL(rc == 0);

    offset += sdu_frag_len;
    num_pkt++;

    while (offset < sdu_len) {
        frag = os_mbuf_get_pkthdr(&g_mbuf_pool, sizeof(struct ble_mbuf_hdr));
        TEST_ASSERT_FATAL(frag != NULL);

        /* Subsequent SDU Fragments */
        sdu_frag_len = min(sdu_len - offset, HCI_iso_sdu_max);
        rc = os_mbuf_append(sdu, &g_test_sdu_data[offset], sdu_frag_len);
        TEST_ASSERT_FATAL(rc == 0);

        offset += sdu_frag_len;
        num_pkt++;

        os_mbuf_concat(sdu, frag);
    }

    ble_ll_isoal_mux_sdu_put(mux, sdu);

    return num_pkt;
}

static void
test_data_verify(uint8_t *pdu, int pdu_len, uint16_t sdu_offset)
{
    for (int i = 0; i < pdu_len; i++) {
        TEST_ASSERT(pdu[i] == g_test_sdu_data[sdu_offset + i],
                    "PDU verification failed pdu[%d] %d != %d", i, pdu[i],
                    g_test_sdu_data[sdu_offset + i]);
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
test_ial_broadcast_single_sdu_bis(const struct test_ial_broadcast_single_sdu_bis_cfg *cfg)
{
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_mux *mux = &fixture.mux;
    int num_completed_pkt;
    int pdu_len;
    uint32_t timeoffset;
    uint16_t seg_hdr;
    const uint8_t Max_SDU = 32;
    uint8_t pdu[cfg->Max_PDU];
    uint8_t llid = 0xff;

    test_ll_isoal_setup(&fixture, Max_SDU, cfg->Max_PDU, cfg->ISO_Interval,
                        cfg->SDU_Interval, cfg->BN, cfg->Framed, cfg->Framing_Mode);

    /* Send Single SDU */
    test_sdu_enqueue(mux, Max_SDU, 0, 20000);

    ble_ll_isoal_mux_event_start(mux, 30500);

    /* PDU #1 */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, pdu);
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

        test_data_verify(&pdu[5], Max_SDU, 0);
    } else {
        TEST_ASSERT(pdu_len == Max_SDU, "PDU length is incorrect %d", pdu_len);

        test_data_verify(&pdu[0], Max_SDU, 0);
    }

    num_completed_pkt = ble_ll_isoal_mux_event_done(mux);
    TEST_ASSERT(num_completed_pkt > 0, "num_completed_pkt is incorrect %d", num_completed_pkt);

    test_ll_isoal_teardown(&fixture);
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
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_mux *mux = &fixture.mux;
    const uint16_t Max_SDU = 503;
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

    test_ll_isoal_setup(&fixture, Max_SDU, Max_PDU, cfg->ISO_Interval,
                        cfg->SDU_Interval, cfg->BN, cfg->Framed, cfg->Framing_Mode);

    for (size_t round = 0; round < ARRAY_SIZE(rounds); round++) {
        sc_packets_num = 0;
        sdu_offset = 0;

        timestamp = (round + 1) * cfg->SDU_Interval;

        num_expected_pkt =
            test_sdu_enqueue(mux, rounds[round].sdu_len, round, timestamp);

        ble_ll_isoal_mux_event_start(mux, timestamp + 100);

        for (idx = 0; idx < cfg->BN; idx++) {
            pdu_len = ble_ll_isoal_mux_pdu_get(mux, idx, &llid, pdu);
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

                        test_data_verify(&pdu[5], seg_len - 3, 0);
                        sdu_offset += seg_len - 3;
                    } else {
                        TEST_ASSERT_FATAL(BLE_LL_ISOAL_SEGHDR_SC(seg_hdr) == 1,
                                          "Round #%d: SC is incorrect %d",
                                          round, BLE_LL_ISOAL_SEGHDR_SC(seg_hdr));

                        test_data_verify(&pdu[2], seg_len, sdu_offset);
                        sdu_offset += seg_len;
                    }
                } else {
                    TEST_ASSERT_FATAL(llid == 0b01, "Round #%d: LLID is incorrect %d", round, llid);

                    test_data_verify(&pdu[0], pdu_len, sdu_offset);
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

                    test_data_verify(&pdu[2], seg_len, sdu_offset);
                    sdu_offset += seg_len;
                } else {
                    TEST_ASSERT_FATAL(pdu_len == rounds[round].sdu_len - sdu_offset,
                                      "Round #%d: idx %d: PDU length is incorrect %d != %d",
                                      round, idx, pdu_len, rounds[round].sdu_len - sdu_offset);
                    TEST_ASSERT_FATAL(llid == 0b00, "Round #%d: LLID is incorrect %d", round, llid);

                    test_data_verify(&pdu[0], pdu_len, sdu_offset);
                    sdu_offset += pdu_len;
                }
            }
        }

        num_completed_pkt = ble_ll_isoal_mux_event_done(mux);
        TEST_ASSERT(num_completed_pkt == num_expected_pkt,
                    "num_completed_pkt %d != %d", num_completed_pkt, num_expected_pkt);
    }

    test_ll_isoal_teardown(&fixture);
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
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_mux *mux = &fixture.mux;
    const uint16_t Max_SDU = 25;
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

    test_ll_isoal_setup(&fixture, Max_SDU, cfg->Max_PDU, cfg->ISO_Interval,
                        cfg->SDU_Interval, cfg->BN, Framed, Framing_Mode);

    /* The Upper Tester sends to the IUT a small SDU1 with data length of 20 bytes. */
    sdu_1_ts = 100;
    test_sdu_enqueue(mux, 20, 0, sdu_1_ts);

    /* The Upper Tester sends to the IUT a small SDU2 with data length of 25 bytes. */
    sdu_2_ts = sdu_1_ts + cfg->SDU_Interval;
    test_sdu_enqueue(mux, 25, 0, sdu_2_ts);

    event_ts = sdu_2_ts + 200;
    ble_ll_isoal_mux_event_start(mux, event_ts);

    /* The IUT sends a single Broadcast ISO Data PDU with SDU1 followed by SDU2 over the BIS.
     * Each SDU header has SC = 0 and CMPT = 1.
     */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, pdu);
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

    (void)ble_ll_isoal_mux_event_done(mux);

    test_ll_isoal_teardown(&fixture);
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
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_mux *mux = &fixture.mux;
    const uint32_t ISO_Interval = 10000;
    const uint32_t SDU_Interval = 10000;
    const uint16_t Max_SDU = 32;
    const uint16_t Max_PDU = 32;
    int pdu_len;
    uint8_t pdu[Max_PDU];
    uint32_t timeoffset;
    uint16_t seg_hdr;
    uint8_t llid = 0xff;

    test_ll_isoal_setup(&fixture, Max_SDU, Max_PDU, ISO_Interval, SDU_Interval,
                        cfg->BN, cfg->Framed, cfg->Framing_Mode);

    /* The Upper Tester sends an HCI ISO Data packet to the IUT with zero data length. */
    test_sdu_enqueue(mux, 0, 0, 100);

    ble_ll_isoal_mux_event_start(mux, 500);

    /* The IUT sends a single Broadcast ISO Data PDU with the LLID,
     * Framed, Framing_Mode, the segmentation header and time offset
     * fields as specified in Table 4.35. Length is 0 if LLID is 0b00
     * and is 5 (Segmentation Header + TimeOffset) if LLID is 0b10.
     * SDU field is empty..
     */
    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, pdu);
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
        pdu_len = ble_ll_isoal_mux_pdu_get(mux, idx, &llid, pdu);
        TEST_ASSERT(llid == cfg->LLID, "LLID is incorrect %d", llid);
        TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);
    }

    (void)ble_ll_isoal_mux_event_done(mux);

    test_ll_isoal_teardown(&fixture);
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
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_mux *mux = &fixture.mux;
    int pdu_len;
    uint8_t pdu[cfg->mx_pdu];
    uint32_t timestamp;
    uint8_t llid = 0xff;

    test_ll_isoal_setup(&fixture, cfg->mx_sdu, cfg->mx_pdu, cfg->iso_int,
                        cfg->sdu_int, cfg->bn, false, 0);

    for (uint16_t sdu_len = 4; sdu_len < cfg->mx_sdu; sdu_len++) {
        timestamp = sdu_len * cfg->sdu_int;
        test_sdu_enqueue(mux, sdu_len, sdu_len, timestamp);

        ble_ll_isoal_mux_event_start(mux, timestamp + 50);

        /* As the mx_sdu == mx_pdu, the data will always fit the single PDU */
        TEST_ASSERT(cfg->mx_sdu == cfg->mx_pdu,
                    "#%d: SDU and PDU length should be same", sdu_len);

        pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, pdu);
        TEST_ASSERT(llid == 0b00,
                    "#%d: LLID is incorrect %d", sdu_len, llid);
        TEST_ASSERT(pdu_len == sdu_len,
                    "#%d: PDU length is incorrect %d", sdu_len, pdu_len);

        /* Padding */
        for (uint8_t idx = 1; idx < cfg->bn; idx++) {
            pdu_len = ble_ll_isoal_mux_pdu_get(mux, idx, &llid, pdu);
            TEST_ASSERT(llid == 0b01,
                        "#%d #%d: LLID is incorrect %d", sdu_len, idx, llid);
            TEST_ASSERT(pdu_len == 0,
                        "#%d #%d: PDU length is incorrect %d",
                        sdu_len, idx, pdu_len);
        }

        (void)ble_ll_isoal_mux_event_done(mux);
    }

    test_ll_isoal_teardown(&fixture);
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
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_mux *mux = &fixture.mux;
    const uint32_t sdu_int = 7500;
    const uint32_t iso_int = 7500;
    const uint16_t mx_sdu = 40;
    const uint8_t mx_pdu = 40;
    const uint8_t bn = 4;
    int num_completed_pkt;
    int pdu_len;
    uint8_t pdu[mx_pdu];
    uint32_t timestamp = 0;
    uint8_t llid = 0xff;

    test_ll_isoal_setup(&fixture, mx_sdu, mx_pdu, iso_int, sdu_int, bn, false, 0);

    test_sdu_enqueue(mux, 21, 0, timestamp++);
    test_sdu_enqueue(mux, 32, 0, timestamp++);
    test_sdu_enqueue(mux, 40, 0, timestamp++);

    ble_ll_isoal_mux_event_start(mux, timestamp + 50);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b00, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 21, "PDU length is incorrect %d", pdu_len);
    test_data_verify(pdu, pdu_len, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 2, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 3, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    num_completed_pkt = ble_ll_isoal_mux_event_done(mux);
    TEST_ASSERT(num_completed_pkt == 1,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    ble_ll_isoal_mux_event_start(mux, timestamp + 50 + iso_int);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b00, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 32, "PDU length is incorrect %d", pdu_len);
    test_data_verify(pdu, pdu_len, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 2, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 3, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    num_completed_pkt = ble_ll_isoal_mux_event_done(mux);
    TEST_ASSERT(num_completed_pkt == 1,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    ble_ll_isoal_mux_event_start(mux, timestamp + 50 + 2 * iso_int);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b00, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 40, "PDU length is incorrect %d", pdu_len);
    test_data_verify(pdu, pdu_len, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 2, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 3, &llid, pdu);
    TEST_ASSERT(llid == 0b01, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == 0, "PDU length is incorrect %d", pdu_len);

    num_completed_pkt = ble_ll_isoal_mux_event_done(mux);
    TEST_ASSERT(num_completed_pkt == 1,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    test_ll_isoal_teardown(&fixture);
}

TEST_CASE_SELF(test_ial_bis_fra_early_sdus) {
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_mux *mux = &fixture.mux;
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

    test_ll_isoal_setup(&fixture, mx_sdu, mx_pdu, iso_int, sdu_int, bn, true, 0);

    for (int seq_num = 0; seq_num < 10; seq_num++) {
        test_sdu_enqueue(mux, mx_sdu, seq_num, timestamp++);
    }

    ble_ll_isoal_mux_event_start(mux, timestamp);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_data_verify(&pdu[5], mx_sdu, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_data_verify(&pdu[5], mx_sdu, 0);

    num_completed_pkt = ble_ll_isoal_mux_event_done(mux);
    TEST_ASSERT(num_completed_pkt == 2,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    ble_ll_isoal_mux_event_start(mux, timestamp);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_data_verify(&pdu[5], mx_sdu, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_data_verify(&pdu[5], mx_sdu, 0);

    num_completed_pkt = ble_ll_isoal_mux_event_done(mux);
    TEST_ASSERT(num_completed_pkt == 2,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    ble_ll_isoal_mux_event_start(mux, timestamp);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_data_verify(&pdu[5], mx_sdu, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_data_verify(&pdu[5], mx_sdu, 0);

    num_completed_pkt = ble_ll_isoal_mux_event_done(mux);
    TEST_ASSERT(num_completed_pkt == 2,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    ble_ll_isoal_mux_event_start(mux, timestamp);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_data_verify(&pdu[5], mx_sdu, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_data_verify(&pdu[5], mx_sdu, 0);

    num_completed_pkt = ble_ll_isoal_mux_event_done(mux);
    TEST_ASSERT(num_completed_pkt == 2,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    ble_ll_isoal_mux_event_start(mux, timestamp);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 0, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_data_verify(&pdu[5], mx_sdu, 0);

    pdu_len = ble_ll_isoal_mux_pdu_get(mux, 1, &llid, pdu);
    TEST_ASSERT(llid == 0b10, "LLID is incorrect %d", llid);
    TEST_ASSERT(pdu_len == mx_pdu, "PDU length is incorrect %d", pdu_len);
    test_data_verify(&pdu[5], mx_sdu, 0);

    num_completed_pkt = ble_ll_isoal_mux_event_done(mux);
    TEST_ASSERT(num_completed_pkt == 2,
                "num_completed_pkt is incorrect %d", num_completed_pkt);

    test_ll_isoal_teardown(&fixture);
}

static void
test_data_verify_om(struct os_mbuf *om, uint16_t len)
{
    struct os_mbuf *om_next;
    uint16_t offset;
    uint16_t dlen;

    offset = 0;

    while (offset < len) {
        dlen = min(len - offset, om->om_len);
        test_data_verify(om->om_data, dlen, offset);
        os_mbuf_adj(om, dlen);
        offset += dlen;

        if (om->om_len == 0) {
            om_next = SLIST_NEXT(om, om_next);
            om = om_next;
        }
    }
}

static struct os_mbuf *
test_ial_lt_pdu_get(uint8_t llid, uint8_t **payload_len)
{
    struct ble_mbuf_hdr *blehdr;
    struct os_mbuf *om;
    uint32_t ticks = 0;
    uint8_t rem_us = 0;
    uint8_t *pdu_hdr;

    om = os_mbuf_get_pkthdr(&g_mbuf_pool, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(om != NULL);

    blehdr = BLE_MBUF_HDR_PTR(om);
    blehdr->beg_cputime = ticks;
    blehdr->rem_usecs = rem_us;

    pdu_hdr = os_mbuf_extend(om, BLE_LL_PDU_HDR_LEN);
    TEST_ASSERT_FATAL(pdu_hdr != NULL);

    pdu_hdr[0] = llid;
    pdu_hdr[1] = 0;

    *payload_len = &pdu_hdr[1];

    return om;
}

static void
test_ial_lt_pdu_seghdr_put(struct os_mbuf *om, uint8_t *payload_len, bool start,
                           bool cmplt, uint32_t time_offset, uint8_t **seg_len)
{
    uint32_t *timeoffset;
    uint8_t *seg_hdr;

    /* Segmentation Header */
    *payload_len += 2;
    seg_hdr = os_mbuf_extend(om, 2);
    TEST_ASSERT_FATAL(seg_hdr != NULL);

    if (start) {
        /* TimeOffset */
        *payload_len += 3;
        timeoffset = os_mbuf_extend(om, 3);
        TEST_ASSERT_FATAL(timeoffset != NULL);
        put_le24(timeoffset, time_offset);
    }

    seg_hdr[0] = (start ? 0b00 : 0b01) | (cmplt ? 0b10 : 0b00);
    /* Length, including TimeOffset if present */
    seg_hdr[1] = start ? 3 : 0;

    *seg_len = &seg_hdr[1];
}

static uint8_t
test_ial_lt_pdu_data_put(struct os_mbuf *om, uint8_t *payload_len,
                         const void *data, uint8_t dlen)
{
    int rc;

    TEST_ASSERT_FATAL(dlen + *payload_len <= UINT8_MAX);

    rc = os_mbuf_append(om, data, dlen);
    TEST_ASSERT_FATAL(rc == 0);

    *payload_len += dlen;

    return dlen;
}

static void
test_ial_lt_pdu_send(struct os_mbuf *om, uint8_t idx, uint8_t *payload_len,
                     struct ble_ll_isoal_demux *demux)
{
    ble_ll_isoal_demux_pdu_put(demux, idx, om);
}

static uint8_t
test_ial_lt_unf_pdu_send(struct ble_ll_isoal_demux *demux, uint8_t idx,
                         const void *data, uint16_t dlen, uint8_t llid)
{
    struct os_mbuf *om;
    uint8_t *payload_len;

    om = test_ial_lt_pdu_get(llid, &payload_len);

    if (dlen > demux->config.max_pdu) {
        dlen = demux->config.max_pdu;
    }

    test_ial_lt_pdu_data_put(om, payload_len, data, dlen);
    test_ial_lt_pdu_send(om, idx, payload_len, demux);

    return *payload_len;
}

static uint8_t
test_ial_lt_fra_pdu_send(struct ble_ll_isoal_demux *demux, uint8_t idx,
                         const void *data, uint16_t dlen, uint32_t time_offset,
                         bool start, bool cmplt)
{
    struct os_mbuf *om;
    uint8_t *payload_len;
    uint8_t *seg_len;

    om = test_ial_lt_pdu_get(0b10, &payload_len);
    test_ial_lt_pdu_seghdr_put(om, payload_len, start, cmplt, time_offset, &seg_len);

    if (dlen + *payload_len > demux->config.max_pdu) {
        dlen = demux->config.max_pdu - *payload_len;
    }

    dlen = test_ial_lt_pdu_data_put(om, payload_len, data, dlen);
    *seg_len += dlen;

    test_ial_lt_pdu_send(om, idx, payload_len, demux);

    return dlen;
}

static void
test_ial_lt_send_padding(struct ble_ll_isoal_demux *demux, uint8_t idx, bool framed)
{
    struct os_mbuf *pdu;
    uint8_t *payload_len;
    uint8_t llid;

    llid = framed ? BLE_LL_BIS_LLID_DATA_PDU_FRAMED
                  : BLE_LL_BIS_LLID_DATA_PDU_UNFRAMED_SC;

    pdu = test_ial_lt_pdu_get(llid, &payload_len);
    *payload_len = 0;

    test_ial_lt_pdu_send(pdu, idx, payload_len, demux);
}

static void
test_ial_lt_fra_null_pdu_send(struct ble_ll_isoal_demux *demux, uint8_t idx)
{
    test_ial_lt_send_padding(demux, idx, true);
}

static void
test_ial_sdu_verify(struct ble_ll_isoal_demux *demux, uint16_t sdu_len, uint8_t pkt_status)
{
    struct os_mbuf_pkthdr *pkthdr;
    struct os_mbuf *om;
    struct test_ial_userhdr *userhdr;

    pkthdr = STAILQ_FIRST(&sdu_q);
    TEST_ASSERT_FATAL(pkthdr != NULL);

    om = OS_MBUF_PKTHDR_TO_MBUF(pkthdr);

    userhdr = OS_MBUF_USRHDR(om);
    TEST_ASSERT(userhdr->pkt_status == pkt_status);

    if (pkt_status == 0b00) {
        /* Verify the SDU Length */
        TEST_ASSERT_FATAL(sdu_len == os_mbuf_len(om));

        /* Verify the SDU contents */
        test_data_verify_om(om, os_mbuf_len(om));
    }

    os_mbuf_free_chain(om);

    STAILQ_REMOVE_HEAD(&sdu_q, omp_next);
}

struct test_ial_receive_a_single_sdu_bis_cfg {
    uint8_t Max_PDU;
    uint8_t Max_SDU;
    uint8_t NSE;
    uint8_t Framed;
    uint8_t Framing_Mode;
    uint8_t LLID;
    uint8_t BN;
    uint32_t SDU_Interval;
    uint32_t ISO_Interval;
};

static void
test_ial_receive_a_single_sdu_bis(struct test_ial_receive_a_single_sdu_bis_cfg *cfg)
{
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_demux *demux = &fixture.demux;
    uint8_t sent;

    test_ll_isoal_setup(&fixture, cfg->Max_SDU, cfg->Max_PDU, cfg->ISO_Interval,
                        cfg->SDU_Interval, cfg->BN, cfg->Framed, cfg->Framing_Mode);

    ble_ll_isoal_demux_event_start(demux, cfg->ISO_Interval);

    if (cfg->Framed) {
        sent = test_ial_lt_fra_pdu_send(demux, 0, &g_test_sdu_data[0],
                                        cfg->Max_SDU, 4000u, true, true);
    } else {
        sent = test_ial_lt_unf_pdu_send(demux, 0, &g_test_sdu_data[0],
                                        cfg->Max_SDU, cfg->LLID);
    }

    TEST_ASSERT_FATAL(sent == cfg->Max_SDU);

    ble_ll_isoal_demux_event_done(demux);

    test_ial_sdu_verify(demux, cfg->Max_SDU, 0b00);

    test_ll_isoal_teardown(&fixture);
}

TEST_CASE_SELF(test_ial_bis_unf_snc_bv_01_c)
{
    struct test_ial_receive_a_single_sdu_bis_cfg cfg = {
        .Max_PDU = 40,
        .Max_SDU = 32,
        .NSE = 2,
        .Framed = 0,
        .Framing_Mode = 0,
        .LLID = 0b00,
        .BN = 2,
        .SDU_Interval = 5000,
        .ISO_Interval = 10000,
    };

    test_ial_receive_a_single_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_unf_snc_bv_02_c)
{
    struct test_ial_receive_a_single_sdu_bis_cfg cfg = {
        .Max_PDU = 40,
        .Max_SDU = 32,
        .NSE = 1,
        .Framed = 0,
        .Framing_Mode = 0,
        .LLID = 0b00,
        .BN = 1,
        .SDU_Interval = 10000,
        .ISO_Interval = 10000,
    };

    test_ial_receive_a_single_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_unf_snc_bv_03_c)
{
    struct test_ial_receive_a_single_sdu_bis_cfg cfg = {
        .Max_PDU = 40,
        .Max_SDU = 32,
        .NSE = 2,
        .Framed = 0,
        .Framing_Mode = 0,
        .LLID = 0b00,
        .BN = 2,
        .SDU_Interval = 10000,
        .ISO_Interval = 10000,
    };

    test_ial_receive_a_single_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bv_06_c)
{
    struct test_ial_receive_a_single_sdu_bis_cfg cfg = {
        .Max_PDU = 42,
        .Max_SDU = 32,
        .NSE = 4,
        .Framed = 1,
        .Framing_Mode = 0,
        .LLID = 0b10,
        .BN = 2,
        .SDU_Interval = 5000,
        .ISO_Interval = 10000,
    };

    test_ial_receive_a_single_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bv_08_c)
{
    struct test_ial_receive_a_single_sdu_bis_cfg cfg = {
        .Max_PDU = 45,
        .Max_SDU = 32,
        .NSE = 2,
        .Framed = 1,
        .Framing_Mode = 0,
        .LLID = 0b10,
        .BN = 1,
        .SDU_Interval = 10000,
        .ISO_Interval = 10000,
    };

    test_ial_receive_a_single_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bv_29_c)
{
    struct test_ial_receive_a_single_sdu_bis_cfg cfg = {
        .Max_PDU = 3 * (32 + 5),
        .Max_SDU = 32,
        .NSE = 4,
        .Framed = 1,
        .Framing_Mode = 1,
        .LLID = 0b10,
        .BN = 1,
        .SDU_Interval = 5000,
        .ISO_Interval = 10000,
    };

    test_ial_receive_a_single_sdu_bis(&cfg);
}

struct test_ial_receive_large_sdu_bis_round {
    uint16_t sdu_len;
    uint8_t sc_packets_num;
};
struct test_ial_receive_large_sdu_bis_cfg {
    uint8_t NSE;
    uint8_t Framing;
    uint8_t BN;
    uint32_t SDU_Interval;
    uint32_t ISO_Interval;
    struct test_ial_receive_large_sdu_bis_round rounds[2];
};

static void
test_ial_receive_large_sdu_bis(const struct test_ial_receive_large_sdu_bis_cfg *cfg)
{
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_demux *demux = &fixture.demux;
    const uint8_t Max_PDU = 251;
    uint16_t sdu_offset;

    test_ll_isoal_setup(&fixture, TSPX_max_sdu_length, Max_PDU, cfg->ISO_Interval,
                        cfg->SDU_Interval, cfg->BN, cfg->Framing, 0);

    for (uint8_t round = 0; round < ARRAY_SIZE(cfg->rounds); round++) {
        TEST_ASSERT_FATAL(cfg->rounds[round].sc_packets_num + 1 <= cfg->BN);

        sdu_offset = 0;

        ble_ll_isoal_demux_event_start(demux, (round + 1) * cfg->ISO_Interval);

        for (uint8_t i = 0; i < cfg->BN; i++) {
            if (i < cfg->rounds[round].sc_packets_num) {
                /* 1. The Lower Tester sends the number of Start/Continuation packets */
                if (cfg->Framing) {
                    sdu_offset += test_ial_lt_fra_pdu_send(
                        demux, i, &g_test_sdu_data[sdu_offset],
                        cfg->rounds[round].sdu_len - sdu_offset, 100,
                        sdu_offset == 0, false);
                } else {
                    sdu_offset += test_ial_lt_unf_pdu_send(
                        demux, i, &g_test_sdu_data[sdu_offset],
                        cfg->rounds[round].sdu_len - sdu_offset, 0b01);
                }
            } else if (i == cfg->rounds[round].sc_packets_num) {
                /* 2. The Lower Tester sends the last ISO Data PDU, with the remaining Payload Data */
                if (cfg->Framing) {
                    sdu_offset += test_ial_lt_fra_pdu_send(
                        demux, i, &g_test_sdu_data[sdu_offset],
                        cfg->rounds[round].sdu_len - sdu_offset,
                        0 /* ignored */, false, true);
                } else {
                    sdu_offset += test_ial_lt_unf_pdu_send(
                        demux, i, &g_test_sdu_data[sdu_offset],
                        cfg->rounds[round].sdu_len - sdu_offset, 0b00);
                }
            } else {
                /* Padding */
                test_ial_lt_send_padding(demux, i, cfg->Framing);
            }
        }

        TEST_ASSERT_FATAL(sdu_offset == cfg->rounds[round].sdu_len);

        ble_ll_isoal_demux_event_done(demux);

        /* Pass verdict */
        test_ial_sdu_verify(demux, cfg->rounds[round].sdu_len, 0b00);
    }

    test_ll_isoal_teardown(&fixture);
}

TEST_CASE_SELF(test_ial_bis_unf_snc_bv_09_c)
{
    struct test_ial_receive_large_sdu_bis_cfg cfg = {
        .NSE = 8,
        .Framing = 0,
        .BN = 4,
        .SDU_Interval = 25000,
        .ISO_Interval = 25000,
        .rounds = { { .sdu_len = 753, .sc_packets_num = 2 },
                   { .sdu_len = 754, .sc_packets_num = 3 } }
    };

    test_ial_receive_large_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_unf_snc_bv_10_c)
{
    struct test_ial_receive_large_sdu_bis_cfg cfg = {
        .NSE = 8,
        .Framing = 0,
        .BN = 4,
        .SDU_Interval = 50000,
        .ISO_Interval = 50000,
        .rounds = { { .sdu_len = 753, .sc_packets_num = 2 },
                   { .sdu_len = 754, .sc_packets_num = 3 } }
    };

    test_ial_receive_large_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bv_11_c)
{
    struct test_ial_receive_large_sdu_bis_cfg cfg = {
        .NSE = 8,
        .Framing = 1,
        .BN = 4,
        .SDU_Interval = 40000,
        .ISO_Interval = 50000,
        .rounds = { { .sdu_len = 744, .sc_packets_num = 2 },
                   { .sdu_len = 745, .sc_packets_num = 3 } }
    };

    test_ial_receive_large_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bv_13_c)
{
    struct test_ial_receive_large_sdu_bis_cfg cfg = {
        .NSE = 8,
        .Framing = 1,
        .BN = 4,
        .SDU_Interval = 25000,
        .ISO_Interval = 25000,
        .rounds = { { .sdu_len = 744, .sc_packets_num = 2 },
                   { .sdu_len = 745, .sc_packets_num = 3 } }
    };

    test_ial_receive_large_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bv_15_c)
{
    struct test_ial_receive_large_sdu_bis_cfg cfg = {
        .NSE = 8,
        .Framing = 1,
        .BN = 4,
        .SDU_Interval = 30000,
        .ISO_Interval = 35000,
        .rounds = { { .sdu_len = 744, .sc_packets_num = 2 },
                   { .sdu_len = 745, .sc_packets_num = 3 } }
    };

    test_ial_receive_large_sdu_bis(&cfg);
}

struct test_ial_receive_multiple_small_sdus_bis_cfg {
    uint8_t NSE;
    uint8_t BN;
    uint32_t SDU_Interval;
    uint32_t ISO_Interval;
};

static void
test_ial_receive_multiple_small_sdus_bis(struct test_ial_receive_multiple_small_sdus_bis_cfg *cfg)
{
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_demux *demux = &fixture.demux;
    struct ble_mbuf_hdr *blehdr;
    struct os_mbuf *pdu;
    const uint8_t Max_PDU = 68;
    uint32_t ticks = 0;
    uint8_t rem_us = 0;
    uint32_t *timeoffset;
    uint16_t *pdu_hdr;
    uint16_t *seg_hdr;
    int rc;

    test_ll_isoal_setup(&fixture, TSPX_max_sdu_length, Max_PDU,
                        cfg->ISO_Interval, cfg->SDU_Interval, cfg->BN, 1, 0);

    seg_hdr = NULL;
    timeoffset = NULL;

    ble_ll_isoal_demux_event_start(demux, cfg->ISO_Interval);

    pdu = os_mbuf_get_pkthdr(&g_mbuf_pool, sizeof(*blehdr));
    TEST_ASSERT_FATAL(pdu != NULL);

    blehdr = BLE_MBUF_HDR_PTR(pdu);
    blehdr->beg_cputime = ticks;
    blehdr->rem_usecs = rem_us;

    pdu_hdr = os_mbuf_extend(pdu, sizeof(*pdu_hdr));
    TEST_ASSERT_FATAL(pdu_hdr != NULL);

    /**
     * SDU1 with data length of 20 bytes
     */

    /* Segmentation Header */
    seg_hdr = os_mbuf_extend(pdu, sizeof(*seg_hdr));
    TEST_ASSERT_FATAL(seg_hdr != NULL);
    put_le16(seg_hdr, BLE_LL_ISOAL_SEGHDR(false, true, 23));

    /* TimeOffset */
    timeoffset = os_mbuf_extend(pdu, 3);
    TEST_ASSERT_FATAL(timeoffset != NULL);
    put_le24(timeoffset, cfg->ISO_Interval);

    rc = os_mbuf_append(pdu, g_test_sdu_data, 20);
    TEST_ASSERT_FATAL(rc == 0);

    /**
     * SDU2 with data length of 25 bytes
     */

    /* Segmentation Header */
    seg_hdr = os_mbuf_extend(pdu, sizeof(*seg_hdr));
    TEST_ASSERT_FATAL(seg_hdr != NULL);
    put_le16(seg_hdr, BLE_LL_ISOAL_SEGHDR(false, true, 28));

    /* TimeOffset */
    timeoffset = os_mbuf_extend(pdu, 3);
    TEST_ASSERT_FATAL(timeoffset != NULL);
    put_le24(timeoffset, cfg->ISO_Interval - cfg->SDU_Interval);

    rc = os_mbuf_append(pdu, g_test_sdu_data, 25);
    TEST_ASSERT_FATAL(rc == 0);

    *pdu_hdr = 0b10 | ((os_mbuf_len(pdu) - sizeof(*pdu_hdr)) << 8);

    ble_ll_isoal_demux_pdu_put(demux, 0, pdu);

    ble_ll_isoal_demux_event_done(demux);

    /**
     * Pass verdict
     */

    /* IUT sends an SDU to the Upper Tester with the data for SDU1 */
    test_ial_sdu_verify(demux, 20, 0b00);

    /* IUT sends an SDU to the Upper Tester with the data for SDU2 */
    test_ial_sdu_verify(demux, 25, 0b00);

    test_ll_isoal_teardown(&fixture);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bv_17_c)
{
    struct test_ial_receive_multiple_small_sdus_bis_cfg cfg = {
        .NSE = 2,
        .BN = 1,
        .SDU_Interval = 5000,
        .ISO_Interval = 10000,
    };

    test_ial_receive_multiple_small_sdus_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bv_18_c)
{
    struct test_ial_receive_multiple_small_sdus_bis_cfg cfg = {
        .NSE = 2,
        .BN = 2,
        .SDU_Interval = 5000,
        .ISO_Interval = 20000,
    };

    test_ial_receive_multiple_small_sdus_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bv_20_c)
{
    struct test_ial_receive_multiple_small_sdus_bis_cfg cfg = {
        .NSE = 4,
        .BN = 2,
        .SDU_Interval = 5000,
        .ISO_Interval = 20000,
    };

    test_ial_receive_multiple_small_sdus_bis(&cfg);
}

struct test_ial_seg_hdr {
    uint8_t SC;
    uint8_t CMPLT;
    uint8_t LENGTH;
};
struct test_ial_receive_a_zero_length_sdu_bis_cfg {
    uint8_t NSE;
    uint8_t Framed;
    uint8_t Framing_Mode;
    uint8_t LLID;
    uint8_t BN;
    struct test_ial_seg_hdr *seg_hdr;
    bool time_offset;
};

static void
test_ial_receive_a_zero_length_sdu_bis(struct test_ial_receive_a_zero_length_sdu_bis_cfg *cfg)
{
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_demux *demux = &fixture.demux;
    struct ble_mbuf_hdr *blehdr;
    struct os_mbuf *pdu;
    const uint16_t ISO_Interval = 10000;
    const uint16_t SDU_Interval = 10000;
    const uint8_t Max_PDU = 32;
    uint32_t ticks = 0;
    uint8_t rem_us = 0;
    uint32_t *timeoffset;
    uint16_t *pdu_hdr;
    uint16_t *seg_hdr;

    test_ll_isoal_setup(&fixture, TSPX_max_sdu_length, Max_PDU, ISO_Interval,
                        SDU_Interval, cfg->BN, cfg->Framed, cfg->Framing_Mode);

    ble_ll_isoal_demux_event_start(demux, ISO_Interval);

    seg_hdr = NULL;
    timeoffset = NULL;

    pdu = os_mbuf_get_pkthdr(&g_mbuf_pool, sizeof(*blehdr));
    TEST_ASSERT_FATAL(pdu != NULL);

    blehdr = BLE_MBUF_HDR_PTR(pdu);
    blehdr->beg_cputime = ticks;
    blehdr->rem_usecs = rem_us;

    pdu_hdr = os_mbuf_extend(pdu, sizeof(*pdu_hdr));
    TEST_ASSERT_FATAL(pdu_hdr != NULL);

    if (cfg->seg_hdr) {
        /* Segmentation Header */
        seg_hdr = os_mbuf_extend(pdu, sizeof(*seg_hdr));
        TEST_ASSERT_FATAL(seg_hdr != NULL);
        put_le16(seg_hdr, BLE_LL_ISOAL_SEGHDR(cfg->seg_hdr->SC, cfg->seg_hdr->CMPLT,
                                              cfg->seg_hdr->LENGTH));
    }

    if (cfg->time_offset) {
        /* TimeOffset */
        timeoffset = os_mbuf_extend(pdu, 3);
        TEST_ASSERT_FATAL(timeoffset != NULL);
        put_le24(timeoffset, 0);
    }

    *pdu_hdr = cfg->LLID | ((os_mbuf_len(pdu) - sizeof(*pdu_hdr)) << 8);

    ble_ll_isoal_demux_pdu_put(demux, 0, pdu);

    /* Padding if needed */
    for (uint8_t i = 1; i < cfg->BN; i++) {
        test_ial_lt_send_padding(demux, i, cfg->Framed);
    }

    ble_ll_isoal_demux_event_done(demux);

    /**
     * Pass verdict
     */

    /* IUT sends an empty SDU to the Upper Tester with the TimeOffset field */
    test_ial_sdu_verify(demux, 0, 0b00);

    test_ll_isoal_teardown(&fixture);
}

TEST_CASE_SELF(test_ial_bis_unf_snc_bv_21_c)
{
    struct test_ial_receive_a_zero_length_sdu_bis_cfg cfg = {
        .NSE = 4,
        .Framed = 0,
        .Framing_Mode = 0,
        .LLID = 0b00,
        .BN = 2,
        .seg_hdr = NULL,
    };

    test_ial_receive_a_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_unf_snc_bv_22_c)
{
    struct test_ial_receive_a_zero_length_sdu_bis_cfg cfg = {
        .NSE = 6,
        .Framed = 0,
        .Framing_Mode = 0,
        .LLID = 0b00,
        .BN = 3,
        .seg_hdr = NULL,
    };

    test_ial_receive_a_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_unf_snc_bv_23_c)
{
    struct test_ial_receive_a_zero_length_sdu_bis_cfg cfg = {
        .NSE = 1,
        .Framed = 0,
        .Framing_Mode = 0,
        .LLID = 0b00,
        .BN = 1,
        .seg_hdr = NULL,
    };

    test_ial_receive_a_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_unf_snc_bv_24_c)
{
    struct test_ial_receive_a_zero_length_sdu_bis_cfg cfg = {
        .NSE = 2,
        .Framed = 0,
        .Framing_Mode = 0,
        .LLID = 0b00,
        .BN = 1,
        .seg_hdr = NULL,
    };

    test_ial_receive_a_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bv_25_c)
{
    struct test_ial_seg_hdr seg_hdr = {
        .SC = 0,
        .CMPLT = 1,
        .LENGTH = 3,
    };
    struct test_ial_receive_a_zero_length_sdu_bis_cfg cfg = {
        .NSE = 6,
        .Framed = 1,
        .Framing_Mode = 0,
        .LLID = 0b10,
        .BN = 2,
        .seg_hdr = &seg_hdr,
        .time_offset = true,
    };

    test_ial_receive_a_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bv_26_c)
{
    struct test_ial_seg_hdr seg_hdr = {
        .SC = 0,
        .CMPLT = 1,
        .LENGTH = 3,
    };
    struct test_ial_receive_a_zero_length_sdu_bis_cfg cfg = {
        .NSE = 2,
        .Framed = 1,
        .Framing_Mode = 0,
        .LLID = 0b10,
        .BN = 1,
        .seg_hdr = &seg_hdr,
        .time_offset = true,
    };

    test_ial_receive_a_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bv_27_c)
{
    struct test_ial_seg_hdr seg_hdr = {
        .SC = 0,
        .CMPLT = 1,
        .LENGTH = 3,
    };
    struct test_ial_receive_a_zero_length_sdu_bis_cfg cfg = {
        .NSE = 4,
        .Framed = 1,
        .Framing_Mode = 0,
        .LLID = 0b10,
        .BN = 1,
        .seg_hdr = &seg_hdr,
        .time_offset = true,
    };

    test_ial_receive_a_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bv_28_c)
{
    struct test_ial_seg_hdr seg_hdr = {
        .SC = 0,
        .CMPLT = 1,
        .LENGTH = 3,
    };
    struct test_ial_receive_a_zero_length_sdu_bis_cfg cfg = {
        .NSE = 6,
        .Framed = 1,
        .Framing_Mode = 0,
        .LLID = 0b10,
        .BN = 3,
        .seg_hdr = &seg_hdr,
        .time_offset = true,
    };

    test_ial_receive_a_zero_length_sdu_bis(&cfg);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bv_30_c)
{
    struct test_ial_seg_hdr seg_hdr = {
        .SC = 0,
        .CMPLT = 1,
        .LENGTH = 3,
    };
    struct test_ial_receive_a_zero_length_sdu_bis_cfg cfg = {
        .NSE = 6,
        .Framed = 1,
        .Framing_Mode = 1,
        .LLID = 0b10,
        .BN = 2,
        .seg_hdr = &seg_hdr,
        .time_offset = true,
    };

    test_ial_receive_a_zero_length_sdu_bis(&cfg);
}

/**
 * Send 4 PDUs with LLID=0b01 and no data
 */
static void
test_ial_bis_unf_snc_bi_02_c_round_1(struct ble_ll_isoal_demux *demux)
{
    ble_ll_isoal_demux_event_start(demux, 0);

    test_ial_lt_unf_pdu_send(demux, 0, &g_test_sdu_data[0], 0, 0b01);
    test_ial_lt_unf_pdu_send(demux, 1, &g_test_sdu_data[0], 0, 0b01);
    test_ial_lt_unf_pdu_send(demux, 2, &g_test_sdu_data[0], 0, 0b01);
    test_ial_lt_unf_pdu_send(demux, 3, &g_test_sdu_data[0], 0, 0b01);

    ble_ll_isoal_demux_event_done(demux);

    /* Shall be reported as lost data */
    test_ial_sdu_verify(demux, 0, 0b10);
}

/**
 * Send 4 PDUs with LLID=0b01 and data
 */
static void
test_ial_bis_unf_snc_bi_02_c_round_2(struct ble_ll_isoal_demux *demux)
{
    uint16_t sdu_len, sdu_offset;

    sdu_len = TSPX_max_sdu_length;
    sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, 0);

    sdu_offset += test_ial_lt_unf_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 1, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 2, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 3, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);

    ble_ll_isoal_demux_event_done(demux);

    /* Expected to be reported as data with possible errors */
    test_ial_sdu_verify(demux, 0, 0b01);
}

/**
 * Send 3 PDUs with LLID=0b01 and data, then 1 PDU with LLID=0b00 and data;
 * one of the first three PDUs has a CRC error
 */
static void
test_ial_bis_unf_snc_bi_02_c_round_3(struct ble_ll_isoal_demux *demux)
{
    uint16_t sdu_len, sdu_offset;

    sdu_len = TSPX_max_sdu_length;
    sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, 0);

    sdu_offset += test_ial_lt_unf_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    /* Since PDUs with CRC error are discarded, assume the one below is lost. */
    //  sdu_offset += test_ial_lt_send_sdu_frag(demux, 1, &g_test_sdu_data[sdu_offset],
    //                                          sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 2, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 3, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b00);

    ble_ll_isoal_demux_event_done(demux);

    /* Expected to be reported as data with possible errors */
    test_ial_sdu_verify(demux, 0, 0b01);
}

/**
 * Send 2 PDUs with LLID=0b01 and data, then 2 PDUs with LLID=0b00 and data
 */
static void
test_ial_bis_unf_snc_bi_02_c_round_4(struct ble_ll_isoal_demux *demux)
{
    uint16_t sdu_len, sdu_offset;

    sdu_len = TSPX_max_sdu_length;
    sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, 0);

    sdu_offset += test_ial_lt_unf_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 1, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 2, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b00);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 3, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b00);

    ble_ll_isoal_demux_event_done(demux);

    /* Expected to be reported as data with possible errors */
    test_ial_sdu_verify(demux, sdu_offset, 0b01);
}

/**
 * Send 2 PDUs with LLID=0b01 and no data, then 1 PDU with LLID=0b00 and data,
 * then 1 PDU with LLID=0b01 and data
 */
static void
test_ial_bis_unf_snc_bi_02_c_round_5(struct ble_ll_isoal_demux *demux)
{
    uint16_t sdu_len, sdu_offset;

    sdu_len = TSPX_max_sdu_length;
    sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, 0);

    sdu_offset +=
        test_ial_lt_unf_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset], 0, 0b01);
    sdu_offset +=
        test_ial_lt_unf_pdu_send(demux, 1, &g_test_sdu_data[sdu_offset], 0, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 2, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b00);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 3, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);

    ble_ll_isoal_demux_event_done(demux);

    /* Expected to be reported as data with possible errors */
    test_ial_sdu_verify(demux, sdu_offset, 0b01);
}

/**
 * Send 2 PDUs with LLID=0b01 and no data, then 1 PDU with LLID=0b00 and data,
 * then 1 PDU with LLID=0b10 and no data.
 */
static void
test_ial_bis_unf_snc_bi_02_c_round_6(struct ble_ll_isoal_demux *demux)
{
    uint16_t sdu_len, sdu_offset;

    sdu_len = TSPX_max_sdu_length;
    sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, 0);

    sdu_offset +=
        test_ial_lt_unf_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset], 0, 0b01);
    sdu_offset +=
        test_ial_lt_unf_pdu_send(demux, 1, &g_test_sdu_data[sdu_offset], 0, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 2, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b00);
    sdu_offset +=
        test_ial_lt_unf_pdu_send(demux, 3, &g_test_sdu_data[sdu_offset], 0, 0b10);

    ble_ll_isoal_demux_event_done(demux);

    /* Expected to be reported as data with possible errors */
    test_ial_sdu_verify(demux, 0, 0b01);
}

/**
 * Send 2 PDUs with LLID=0b01 and no data, then 1 PDU with LLID=0b10 and data,
 * then 1 PDU with LLID=0b00 and no data
 */
static void
test_ial_bis_unf_snc_bi_02_c_round_7(struct ble_ll_isoal_demux *demux)
{
    uint16_t sdu_len, sdu_offset;

    sdu_len = TSPX_max_sdu_length;
    sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, 0);

    sdu_offset +=
        test_ial_lt_unf_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset], 0, 0b01);
    sdu_offset +=
        test_ial_lt_unf_pdu_send(demux, 1, &g_test_sdu_data[sdu_offset], 0, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 2, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b10);
    sdu_offset +=
        test_ial_lt_unf_pdu_send(demux, 3, &g_test_sdu_data[sdu_offset], 0, 0b00);

    ble_ll_isoal_demux_event_done(demux);

    /* Shall be reported as lost data */
    test_ial_sdu_verify(demux, 0, 0b10);
}

static void
test_ial_bis_unf_snc_bi_02_c_round_8a(struct ble_ll_isoal_demux *demux)
{
    uint16_t sdu_len, sdu_offset;
    uint16_t incomplete_sdu_len;

    sdu_len = TSPX_max_sdu_length;
    sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, 0);

    incomplete_sdu_len = sdu_offset;
    // sdu_offset += test_ial_lt_unf_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset],
    //                                   sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 1, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 2, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 3, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b00);

    ble_ll_isoal_demux_event_done(demux);

    /* Expected to be reported as lost data */
    test_ial_sdu_verify(demux, incomplete_sdu_len, 0b10);
}

static void
test_ial_bis_unf_snc_bi_02_c_round_8b(struct ble_ll_isoal_demux *demux)
{
    uint16_t sdu_len, sdu_offset;
    uint16_t incomplete_sdu_len;

    sdu_len = TSPX_max_sdu_length;
    sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, 0);

    sdu_offset += test_ial_lt_unf_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    incomplete_sdu_len = sdu_offset;
    // sdu_offset += test_ial_lt_unf_pdu_send(demux, 1, &g_test_sdu_data[sdu_offset],
    //                                        sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 2, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 3, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b00);

    ble_ll_isoal_demux_event_done(demux);

    /* Expected to be reported as data with possible errors */
    test_ial_sdu_verify(demux, incomplete_sdu_len, 0b01);
}

static void
test_ial_bis_unf_snc_bi_02_c_round_8c(struct ble_ll_isoal_demux *demux)
{
    uint16_t sdu_len, sdu_offset;
    uint16_t incomplete_sdu_len;

    sdu_len = TSPX_max_sdu_length;
    sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, 0);

    sdu_offset += test_ial_lt_unf_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 1, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    incomplete_sdu_len = sdu_offset;
    // sdu_offset += test_ial_lt_unf_pdu_send(demux, 2, &g_test_sdu_data[sdu_offset],
    //                                        sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 3, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b00);

    ble_ll_isoal_demux_event_done(demux);

    /* Expected to be reported as data with possible errors */
    test_ial_sdu_verify(demux, incomplete_sdu_len, 0b01);
}

static void
test_ial_bis_unf_snc_bi_02_c_round_8d(struct ble_ll_isoal_demux *demux)
{
    uint16_t sdu_len, sdu_offset;
    uint16_t incomplete_sdu_len;

    sdu_len = TSPX_max_sdu_length;
    sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, 0);

    sdu_offset += test_ial_lt_unf_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 1, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 2, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    // sdu_offset += test_ial_lt_unf_pdu_send(demux, 3, &g_test_sdu_data[sdu_offset],
    //                                        sdu_len - sdu_offset, 0b00);
    incomplete_sdu_len = sdu_offset;

    ble_ll_isoal_demux_event_done(demux);

    /* Expected to be reported as data with possible errors */
    test_ial_sdu_verify(demux, incomplete_sdu_len, 0b01);
}

/**
 * Send 2 PDUs with LLID=0b01 and data, then 1 PDU with LLID=0b00 and data;
 * one of the four PDUs is omitted to simulate losing one PDU
 */
static void
test_ial_bis_unf_snc_bi_02_c_round_8(struct ble_ll_isoal_demux *demux)
{
    test_ial_bis_unf_snc_bi_02_c_round_8a(demux);
    test_ial_bis_unf_snc_bi_02_c_round_8b(demux);
    test_ial_bis_unf_snc_bi_02_c_round_8c(demux);
    test_ial_bis_unf_snc_bi_02_c_round_8d(demux);
}

/**
 * 1 PDU with LLID=0b00 and data, then 3 PDUs with LLID=0b01, at least one of which has data
 */
static void
test_ial_bis_unf_snc_bi_02_c_round_9(struct ble_ll_isoal_demux *demux)
{
    uint16_t sdu_len, sdu_offset;

    sdu_len = TSPX_max_sdu_length;
    sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, 0);

    sdu_offset += test_ial_lt_unf_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b00);
    sdu_offset +=
        test_ial_lt_unf_pdu_send(demux, 1, &g_test_sdu_data[sdu_offset], 0, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 2, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset +=
        test_ial_lt_unf_pdu_send(demux, 3, &g_test_sdu_data[sdu_offset], 0, 0b01);

    ble_ll_isoal_demux_event_done(demux);

    /* Expected to be reported as data with possible errors */
    test_ial_sdu_verify(demux, sdu_offset, 0b01);
}
/**
 * No PDUs
 */
static void
test_ial_bis_unf_snc_bi_02_c_round_10(struct ble_ll_isoal_demux *demux)
{
    ble_ll_isoal_demux_event_start(demux, 0);
    ble_ll_isoal_demux_event_done(demux);

    /* Shall be reported as lost data */
    test_ial_sdu_verify(demux, 0, 0b10);
}

TEST_CASE_SELF(test_ial_bis_unf_snc_bi_02_c)
{
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_demux *demux = &fixture.demux;
    const uint16_t ISO_Interval = 10000;
    const uint16_t SDU_Interval = 10000;
    const uint8_t Max_PDU = 32;
    const uint8_t NSE = 4;
    const uint8_t BN = 4;

    (void)NSE;

    test_ll_isoal_setup(&fixture, TSPX_max_sdu_length, Max_PDU, ISO_Interval,
                        SDU_Interval, BN, 0, 0);

    test_ial_bis_unf_snc_bi_02_c_round_1(demux);
    test_ial_bis_unf_snc_bi_02_c_round_2(demux);
    test_ial_bis_unf_snc_bi_02_c_round_3(demux);
    test_ial_bis_unf_snc_bi_02_c_round_4(demux);
    test_ial_bis_unf_snc_bi_02_c_round_5(demux);
    test_ial_bis_unf_snc_bi_02_c_round_6(demux);
    test_ial_bis_unf_snc_bi_02_c_round_7(demux);
    test_ial_bis_unf_snc_bi_02_c_round_8(demux);
    test_ial_bis_unf_snc_bi_02_c_round_9(demux);
    test_ial_bis_unf_snc_bi_02_c_round_10(demux);

    test_ll_isoal_teardown(&fixture);
}

TEST_CASE_SELF(test_ial_bis_unf_snc_bi_05_c)
{
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_demux *demux = &fixture.demux;
    const uint16_t ISO_Interval = 10000;
    const uint16_t SDU_Interval = 10000;
    const uint8_t Max_PDU = 32;
    const uint8_t NSE = 4;
    const uint8_t BN = 4;
    uint16_t sdu_len, sdu_offset;

    (void)NSE;

    test_ll_isoal_setup(&fixture, TSPX_max_sdu_length, Max_PDU, ISO_Interval,
                        SDU_Interval, BN, 0, 0);

    sdu_len = TSPX_max_sdu_length;
    sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, 0);

    /* 1. The Lower Tester sends 2 unframed Start/Continuation ISO Data PDUs to
     * the IUT with the LLID=0b01
     */
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 1, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);

    /* 2. The Lower Tester sends the IUT 1 unframed Start/Continuation ISO Data
     * PDU with an invalid CRC.
     */
    /* NOP */

    /* 3. The Lower Tester sends the last unframed ISO Data PDU to the IUT with
     * the LLID=0b00 and with the remaining Payload Data.
     */
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 3, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b00);

    ble_ll_isoal_demux_event_done(demux);

    /* Alternative 4A (The IUT reports the SDU as 0b01 “data with possible errors”) */
    test_ial_sdu_verify(demux, 0, 0b01);

    ble_ll_isoal_demux_event_start(demux, 0);

    sdu_offset = 0;

    /* 5. The Lower Tester sends unframed ISO Data PDUs to the IUT with all LLID = 0b01. */
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 1, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 2, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);
    sdu_offset += test_ial_lt_unf_pdu_send(demux, 3, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0b01);

    ble_ll_isoal_demux_event_done(demux);

    /* Alternative 6A (The IUT reports the SDU as 0b01 “data with possible errors”) */
    test_ial_sdu_verify(demux, 0, 0b01);

    test_ll_isoal_teardown(&fixture);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bi_01_c)
{
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_demux *demux = &fixture.demux;
    const uint16_t ISO_Interval = 10000;
    const uint16_t SDU_Interval = 10000;
    const uint8_t Max_SDU = 108;
    const uint8_t Max_PDU = 32;
    const uint8_t NSE = 4;
    const uint8_t BN = 4;
    uint16_t sdu_len, sdu_offset;

    (void)NSE;

    test_ll_isoal_setup(&fixture, Max_SDU, Max_PDU, ISO_Interval, SDU_Interval,
                        BN, 1, 0);

    sdu_len = Max_SDU;
    sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, ISO_Interval);

    /* 1. The Lower Tester sends 2 framed Start/Continuation ISO Data PDUs to the IUT */
    sdu_offset += test_ial_lt_fra_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 100, true, false);
    sdu_offset += test_ial_lt_fra_pdu_send(demux, 1, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset,
                                           0 /* ignored */, false, false);

    /* 2. The Lower Tester sends 1 framed Start/Continuation ISO Data PDU to the IUT with the Length field in the Segmentation Header set to 255. */
    do {
        const void *data = &g_test_sdu_data[sdu_offset];
        struct os_mbuf *om;
        uint8_t *payload_len;
        uint8_t *seg_len;
        uint16_t dlen;

        dlen = sdu_len - sdu_offset;

        om = test_ial_lt_pdu_get(0b10, &payload_len);
        test_ial_lt_pdu_seghdr_put(om, payload_len, false, false, 0, &seg_len);

        if (dlen + *payload_len > demux->config.max_pdu) {
            dlen = demux->config.max_pdu - *payload_len;
        }

        dlen = test_ial_lt_pdu_data_put(om, payload_len, data, dlen);
        *seg_len = 255;

        test_ial_lt_pdu_send(om, 2, payload_len, demux);
    } while (false);

    /* 3. The Lower Tester sends the last framed ISO Data PDU to the IUT with the remaining Payload Data. */
    sdu_offset += test_ial_lt_fra_pdu_send(demux, 3, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0, false, true);

    ble_ll_isoal_demux_event_done(demux);

    /* Alternative 4A (The IUT reports the SDU as 0b01 “data with possible errors”) */
    test_ial_sdu_verify(demux, 0, 0b01);

    test_ll_isoal_teardown(&fixture);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bi_02_c)
{
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_demux *demux = &fixture.demux;
    const uint16_t ISO_Interval = 10000;
    const uint16_t SDU_Interval = 10000;
    const uint8_t Max_SDU = 32;
    const uint8_t Max_PDU = 45;
    const uint8_t NSE = 1;
    const uint8_t BN = 1;
    uint16_t sdu_len, sdu_offset;

    (void)NSE;

    test_ll_isoal_setup(&fixture, Max_SDU, Max_PDU, ISO_Interval, SDU_Interval,
                        BN, 1, 0);

    sdu_len = Max_SDU;
    sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, ISO_Interval);

    /* 1. The Lower Tester sends 1 framed complete ISO Data PDU to the IUT with
     * the Length field of the Segmentation Header set to 255. */
    do {
        const void *data = &g_test_sdu_data[sdu_offset];
        struct os_mbuf *om;
        uint8_t *payload_len;
        uint8_t *seg_len;
        uint16_t dlen;

        dlen = sdu_len - sdu_offset;

        om = test_ial_lt_pdu_get(0b10, &payload_len);
        test_ial_lt_pdu_seghdr_put(om, payload_len, true, true, 100, &seg_len);

        if (dlen + *payload_len > demux->config.max_pdu) {
            dlen = demux->config.max_pdu - *payload_len;
        }

        dlen = test_ial_lt_pdu_data_put(om, payload_len, data, dlen);
        *seg_len = 255;

        test_ial_lt_pdu_send(om, 0, payload_len, demux);
    } while (false);

    ble_ll_isoal_demux_event_done(demux);

    /* 2A.1 The IUT sends an HCI ISO Data packet with data to the Upper Tester
     * with the Packet_Status_Flag set to 0b01 “data with possible errors”.
     */
    test_ial_sdu_verify(demux, 0, 0b01);

    test_ll_isoal_teardown(&fixture);
}

/** @brief IAL/BIS/FRA/SNC/BI-03-C Step 1A
 *
 * The Lower Tester sends two ISO Data PDUs to the IUT with the LLID = 0b10 in the same
 * isochronous interval. The IUT sends the Upper Tester an ISO Data packet with
 * Packet_Status_Flag = 0b00 and PB_Flag = 0b10 and containing all the data.
 *
 * @param mux ISOAL multiplexer
 */
static void
test_ial_bis_fra_snc_bi_03_c_step_1a(struct ble_ll_isoal_demux *demux, uint32_t timestamp)
{
    uint16_t sdu_len = demux->config.max_sdu;
    uint16_t sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, timestamp);

    /* 1. The Lower Tester sends 2 framed Start/Continuation ISO Data PDUs to the IUT */
    sdu_offset += test_ial_lt_fra_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 100, true, false);
    sdu_offset += test_ial_lt_fra_pdu_send(demux, 1, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 0, false, true);

    ble_ll_isoal_demux_event_done(demux);

    /* Shall report data with status set to 0b00 (data valid) */
    test_ial_sdu_verify(demux, sdu_offset, 0b00);
}

/** @brief IAL/BIS/FRA/SNC/BI-03-C Step 1B
 *
 * The Lower Tester sends an ISO Data PDU with the LLID = 0b10 in the first sub-event of an
 * ISO interval and nothing in the second sub-event. The IUT sends the Upper Tester an ISO
 * Data packet either with Packet_Status_Flag = 0b01 and containing the data, or with
 * Packet_Status_Flag = 0b10 and ISO_SDU_Length = 0 and with no data.
 *
 * @param mux ISOAL multiplexer
 */
static void
test_ial_bis_fra_snc_bi_03_c_step_1b(struct ble_ll_isoal_demux *demux, uint32_t timestamp)
{
    uint16_t sdu_len = demux->config.max_sdu;
    uint16_t sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, timestamp);

    /* 1. The Lower Tester sends 2 framed Start/Continuation ISO Data PDUs to the IUT */
    sdu_offset += test_ial_lt_fra_pdu_send(demux, 0, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset, 100, true, false);

    ble_ll_isoal_demux_event_done(demux);

    /* Shall report data with status set to 0b01 (data invalid) */
    test_ial_sdu_verify(demux, sdu_offset, 0b01);
}

/** @brief IAL/BIS/FRA/SNC/BI-03-C Step 1C
 *
 * The Lower Tester sends nothing in the first sub-event of an ISO interval and an ISO Data
 * PDU with the LLID = 0b10 in the second sub-event. The IUT sends the Upper Tester an
 * ISO Data packet either with Packet_Status_Flag = 0b01 and containing the data, or with
 * Packet_Status_Flag = 0b10 and ISO_SDU_Length = 0 and with no data.
 *
 * @param mux ISOAL multiplexer
 */
static void
test_ial_bis_fra_snc_bi_03_c_step_1c(struct ble_ll_isoal_demux *demux, uint32_t timestamp)
{
    uint16_t sdu_len = demux->config.max_sdu;
    uint16_t sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, timestamp);

    /* 1. The Lower Tester sends 2 framed Start/Continuation ISO Data PDUs to the IUT */
    sdu_offset += test_ial_lt_fra_pdu_send(demux, 1, &g_test_sdu_data[sdu_offset],
                                           sdu_len - sdu_offset,
                                           0 /* ignored */, false, true);

    ble_ll_isoal_demux_event_done(demux);

    /* Shall be reported as lost data */
    test_ial_sdu_verify(demux, sdu_offset, 0b10);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bi_03_c)
{
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_demux *demux = &fixture.demux;
    const uint16_t ISO_Interval = 10000;
    const uint16_t SDU_Interval = 10000;
    const uint8_t Max_SDU = 16;
    const uint8_t Max_PDU = 16;
    const uint8_t NSE = 2;
    const uint8_t BN = 2;
    uint32_t timestamp;

    (void)NSE;

    test_ll_isoal_setup(&fixture, Max_SDU, Max_PDU, ISO_Interval, SDU_Interval,
                        BN, 1, 0);

    timestamp = ISO_Interval;

    test_ial_bis_fra_snc_bi_03_c_step_1a(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1b(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1c(demux, timestamp);
    timestamp += ISO_Interval;

    test_ial_bis_fra_snc_bi_03_c_step_1b(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1c(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1a(demux, timestamp);
    timestamp += ISO_Interval;

    test_ial_bis_fra_snc_bi_03_c_step_1c(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1a(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1b(demux, timestamp);
    timestamp += ISO_Interval;

    test_ial_bis_fra_snc_bi_03_c_step_1c(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1b(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1a(demux, timestamp);
    timestamp += ISO_Interval;

    test_ll_isoal_teardown(&fixture);
}

/** @brief IAL/BIS/FRA/SNC/BI-03-C Step 1A
 *
 * The Lower Tester sends two ISO Data PDUs to the IUT with the LLID = 0b10 in the same
 * isochronous interval. The IUT sends the Upper Tester an ISO Data packet with
 * Packet_Status_Flag = 0b00 and PB_Flag = 0b10 and containing all the data.
 *
 * @param mux ISOAL multiplexer
 */
static void
test_ial_bis_fra_snc_bi_03_c_step_1a_harmony(struct ble_ll_isoal_demux *demux,
                                             uint32_t timestamp)
{
    uint16_t sdu_len = demux->config.max_sdu;
    uint16_t sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, timestamp);

    /* 1. The Lower Tester sends 2 framed Start/Continuation ISO Data PDUs to the IUT */
    sdu_offset += test_ial_lt_fra_pdu_send(demux, 0, &g_test_sdu_data, sdu_len,
                                           1.5 * demux->config.sdu_interval_us,
                                           true, true);
    sdu_offset += test_ial_lt_fra_pdu_send(demux, 1, &g_test_sdu_data, sdu_len,
                                           0.5 * demux->config.sdu_interval_us,
                                           true, true);

    ble_ll_isoal_demux_event_done(demux);

    /* Shall report data with status set to 0b00 (data valid) */
    test_ial_sdu_verify(demux, sdu_len, 0b00);
    test_ial_sdu_verify(demux, sdu_len, 0b00);
}

/** @brief IAL/BIS/FRA/SNC/BI-03-C Step 1B
 *
 * The Lower Tester sends an ISO Data PDU with the LLID = 0b10 in the first sub-event of an
 * ISO interval and nothing in the second sub-event. The IUT sends the Upper Tester an ISO
 * Data packet either with Packet_Status_Flag = 0b01 and containing the data, or with
 * Packet_Status_Flag = 0b10 and ISO_SDU_Length = 0 and with no data.
 *
 * @param mux ISOAL multiplexer
 */
static void
test_ial_bis_fra_snc_bi_03_c_step_1b_harmony(struct ble_ll_isoal_demux *demux,
                                             uint32_t timestamp)
{
    uint16_t sdu_len = demux->config.max_sdu;
    uint16_t sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, timestamp);

    /* 1. The Lower Tester sends 2 framed Start/Continuation ISO Data PDUs to the IUT */
    sdu_offset += test_ial_lt_fra_pdu_send(
        demux, 0, &g_test_sdu_data[sdu_offset], sdu_len - sdu_offset,
        1.5 * demux->config.sdu_interval_us, true, true);

    ble_ll_isoal_demux_event_done(demux);

    /* Shall report data with status set to 0b01 (data invalid) */
    test_ial_sdu_verify(demux, sdu_offset, 0b00);
    test_ial_sdu_verify(demux, sdu_offset, 0b10);
}

/** @brief IAL/BIS/FRA/SNC/BI-03-C Step 1C
 *
 * The Lower Tester sends nothing in the first sub-event of an ISO interval and an ISO Data
 * PDU with the LLID = 0b10 in the second sub-event. The IUT sends the Upper Tester an
 * ISO Data packet either with Packet_Status_Flag = 0b01 and containing the data, or with
 * Packet_Status_Flag = 0b10 and ISO_SDU_Length = 0 and with no data.
 *
 * @param mux ISOAL multiplexer
 */
static void
test_ial_bis_fra_snc_bi_03_c_step_1c_harmony(struct ble_ll_isoal_demux *demux,
                                             uint32_t timestamp)
{
    uint16_t sdu_len = demux->config.max_sdu;
    uint16_t sdu_offset = 0;

    ble_ll_isoal_demux_event_start(demux, timestamp);

    /* 1. The Lower Tester sends 2 framed Start/Continuation ISO Data PDUs to the IUT */
    sdu_offset += test_ial_lt_fra_pdu_send(
        demux, 1, &g_test_sdu_data[sdu_offset], sdu_len - sdu_offset,
        0.5 * demux->config.sdu_interval_us, true, true);

    ble_ll_isoal_demux_event_done(demux);

    /* Shall be reported as lost data */
    test_ial_sdu_verify(demux, sdu_offset, 0b10);
    test_ial_sdu_verify(demux, sdu_offset, 0b00);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bi_03_c_harmony)
{
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_demux *demux = &fixture.demux;
    const uint16_t ISO_Interval = 40000;
    const uint16_t SDU_Interval = 20000;
    const uint8_t Max_SDU = 11;
    const uint8_t Max_PDU = 16;
    const uint8_t NSE = 2;
    const uint8_t BN = 2;
    uint32_t timestamp;

    (void)NSE;

    test_ll_isoal_setup(&fixture, Max_SDU, Max_PDU, ISO_Interval, SDU_Interval,
                        BN, 1, 0);

    timestamp = ISO_Interval;

    test_ial_bis_fra_snc_bi_03_c_step_1a_harmony(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1b_harmony(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1c_harmony(demux, timestamp);
    timestamp += ISO_Interval;

    test_ial_bis_fra_snc_bi_03_c_step_1b_harmony(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1c_harmony(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1a_harmony(demux, timestamp);
    timestamp += ISO_Interval;

    test_ial_bis_fra_snc_bi_03_c_step_1c_harmony(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1a_harmony(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1b_harmony(demux, timestamp);
    timestamp += ISO_Interval;

    test_ial_bis_fra_snc_bi_03_c_step_1c_harmony(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1b_harmony(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_03_c_step_1a_harmony(demux, timestamp);
    timestamp += ISO_Interval;

    test_ll_isoal_teardown(&fixture);
}

/** @brief IAL/BIS/FRA/SNC/BI-04-C Step 1A
 *
 * The Lower Tester sends an ISO Data PDU to the IUT with the LLID = 0b10. The
 * IUT sends the Upper Tester an ISO Data packet with Packet_Status_Flag = 0b00
 * and PB_Flag = 0b10 and containing all the data.
 *
 * @param mux ISOAL multiplexer
 */
static void
test_ial_bis_fra_snc_bi_04_c_step_1a(struct ble_ll_isoal_demux *demux, uint32_t timestamp)
{
    ble_ll_isoal_demux_event_start(demux, timestamp);

    /* 1. The Lower Tester sends an ISO Data PDU to the IUT with the LLID = 0b10. */
    test_ial_lt_fra_pdu_send(demux, 0, &g_test_sdu_data[0],
                             demux->config.max_sdu, 500, true, true);

    ble_ll_isoal_demux_event_done(demux);

    /* Shall report data with status set to 0b00 (data valid) */
    test_ial_sdu_verify(demux, demux->config.max_sdu, 0b00);
}

/** @brief IAL/BIS/FRA/SNC/BI-04-C Step 1B
 *
 * The Lower Tester sends an ISO Null PDU. The IUT sends the Upper Tester an ISO Data
 * packet with Packet_Status_Flag = 0b10 and ISO_SDU_Length = 0 and with no data.
 *
 * @param mux ISOAL multiplexer
 */
static void
test_ial_bis_fra_snc_bi_04_c_step_1b(struct ble_ll_isoal_demux *demux, uint32_t timestamp)
{
    ble_ll_isoal_demux_event_start(demux, timestamp);

    /* The Lower Tester sends an ISO Null PDU */
    test_ial_lt_fra_null_pdu_send(demux, 0);

    ble_ll_isoal_demux_event_done(demux);

    /* Shall report data with status set to 0b10 (data lost) */
    test_ial_sdu_verify(demux, 0, 0b10);
}

TEST_CASE_SELF(test_ial_bis_fra_snc_bi_04_c)
{
    struct test_ll_isoal_fixture fixture;
    struct ble_ll_isoal_demux *demux = &fixture.demux;
    const uint16_t ISO_Interval = 10000;
    const uint16_t SDU_Interval = 10000;
    const uint8_t Max_SDU = 16;
    const uint8_t Max_PDU = Max_SDU + 13;
    const uint8_t NSE = 1;
    const uint8_t BN = 1;
    uint32_t timestamp;

    (void)NSE;

    timestamp = 33333;

    test_ll_isoal_setup(&fixture, Max_SDU, Max_PDU, ISO_Interval, SDU_Interval,
                        BN, 1, 0);

    test_ial_bis_fra_snc_bi_04_c_step_1a(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_04_c_step_1b(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_04_c_step_1a(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_04_c_step_1b(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_04_c_step_1b(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_04_c_step_1a(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_04_c_step_1b(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_04_c_step_1a(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_04_c_step_1a(demux, timestamp);
    timestamp += ISO_Interval;
    test_ial_bis_fra_snc_bi_04_c_step_1b(demux, timestamp);
    timestamp += ISO_Interval;

    test_ll_isoal_teardown(&fixture);
}

TEST_SUITE(ble_ll_isoal_test_suite)
{
    ble_ll_isoal_test_suite_init();

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

    test_ial_bis_unf_early_sdus();
    test_ial_bis_fra_early_sdus();

    /* Receive a Single SDU, BIS */
    test_ial_bis_unf_snc_bv_01_c();
    test_ial_bis_unf_snc_bv_02_c();
    test_ial_bis_unf_snc_bv_03_c();
    test_ial_bis_fra_snc_bv_06_c();
    test_ial_bis_fra_snc_bv_08_c();
    test_ial_bis_fra_snc_bv_29_c();

    /* Receive Large SDU, BIS */
    test_ial_bis_unf_snc_bv_09_c();
    test_ial_bis_unf_snc_bv_10_c();
    test_ial_bis_fra_snc_bv_11_c();
    test_ial_bis_fra_snc_bv_13_c();
    test_ial_bis_fra_snc_bv_15_c();

    /* Receive Multiple, Small SDUs, BIS */
    test_ial_bis_fra_snc_bv_17_c();
    test_ial_bis_fra_snc_bv_18_c();
    test_ial_bis_fra_snc_bv_20_c();

    /* Receive a Zero-Length SDU, BIS */
    test_ial_bis_unf_snc_bv_21_c();
    test_ial_bis_unf_snc_bv_22_c();
    test_ial_bis_unf_snc_bv_23_c();
    test_ial_bis_unf_snc_bv_24_c();
    test_ial_bis_fra_snc_bv_25_c();
    test_ial_bis_fra_snc_bv_26_c();
    test_ial_bis_fra_snc_bv_27_c();
    test_ial_bis_fra_snc_bv_28_c();
    test_ial_bis_fra_snc_bv_30_c();

    /* Receive an unsuccessful Large SDU, BIS */
    test_ial_bis_unf_snc_bi_02_c();

    /* SDU Reporting, BIS, Unframed PDU */
    test_ial_bis_unf_snc_bi_05_c();

    /* SDU Reporting, BIS, Framed PDU */
    test_ial_bis_fra_snc_bi_01_c();

    /* SDU Reporting, BIS, BN = 1, NSE = 1, Framed PDU */
    test_ial_bis_fra_snc_bi_02_c();

    /* Reporting an Unsuccessful Large SDU, Framed BIS */
    test_ial_bis_fra_snc_bi_03_c();
    test_ial_bis_fra_snc_bi_03_c_harmony();

    /* Reporting a missing or damaged SDU, Framed BIS */
    test_ial_bis_fra_snc_bi_04_c();
}
