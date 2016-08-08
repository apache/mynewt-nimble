/**
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

#include <string.h>
#include <errno.h>
#include "stats/stats.h"
#include "testutil/testutil.h"
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "nimble/ble_hci_trans.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "transport/ram/ble_hci_ram.h"
#include "ble_hs_test_util.h"

/* Our global device address. */
uint8_t g_dev_addr[BLE_DEV_ADDR_LEN];

#define BLE_HS_TEST_UTIL_PUB_ADDR_VAL { 0x0a, 0x54, 0xab, 0x49, 0x7f, 0x06 }

static const uint8_t ble_hs_test_util_pub_addr[BLE_DEV_ADDR_LEN] =
    BLE_HS_TEST_UTIL_PUB_ADDR_VAL;

/** Use lots of small mbufs to ensure correct mbuf usage. */
#define BLE_HS_TEST_UTIL_NUM_MBUFS      (100)
#define BLE_HS_TEST_UTIL_BUF_SIZE       OS_ALIGN(100, 4)
#define BLE_HS_TEST_UTIL_MEMBLOCK_SIZE  \
    (BLE_HS_TEST_UTIL_BUF_SIZE + BLE_MBUF_MEMBLOCK_OVERHEAD)
#define BLE_HS_TEST_UTIL_MEMPOOL_SIZE   \
    OS_MEMPOOL_SIZE(BLE_HS_TEST_UTIL_NUM_MBUFS, BLE_HS_TEST_UTIL_MEMBLOCK_SIZE)

#define BLE_HS_TEST_UTIL_LE_OPCODE(ocf) \
    ble_hs_hci_util_opcode_join(BLE_HCI_OGF_LE, (ocf))

struct os_eventq ble_hs_test_util_evq;

os_membuf_t ble_hs_test_util_mbuf_mpool_data[BLE_HS_TEST_UTIL_MEMPOOL_SIZE];
struct os_mbuf_pool ble_hs_test_util_mbuf_pool;
struct os_mempool ble_hs_test_util_mbuf_mpool;

static STAILQ_HEAD(, os_mbuf_pkthdr) ble_hs_test_util_prev_tx_queue;
struct os_mbuf *ble_hs_test_util_prev_tx_cur;

#define BLE_HS_TEST_UTIL_PREV_HCI_TX_CNT      64
static uint8_t
ble_hs_test_util_prev_hci_tx[BLE_HS_TEST_UTIL_PREV_HCI_TX_CNT][260];
int ble_hs_test_util_num_prev_hci_txes;

uint8_t ble_hs_test_util_cur_hci_tx[260];

const struct ble_gap_adv_params ble_hs_test_util_adv_params = {
    .conn_mode = BLE_GAP_CONN_MODE_UND,
    .disc_mode = BLE_GAP_DISC_MODE_GEN,

    .itvl_min = 0,
    .itvl_max = 0,
    .channel_map = 0,
    .filter_policy = 0,
    .high_duty_cycle = 0,
};

void
ble_hs_test_util_prev_tx_enqueue(struct os_mbuf *om)
{
    struct os_mbuf_pkthdr *omp;

    assert(OS_MBUF_IS_PKTHDR(om));

    omp = OS_MBUF_PKTHDR(om);
    if (STAILQ_EMPTY(&ble_hs_test_util_prev_tx_queue)) {
        STAILQ_INSERT_HEAD(&ble_hs_test_util_prev_tx_queue, omp, omp_next);
    } else {
        STAILQ_INSERT_TAIL(&ble_hs_test_util_prev_tx_queue, omp, omp_next);
    }
}

static struct os_mbuf *
ble_hs_test_util_prev_tx_dequeue_once(struct hci_data_hdr *out_hci_hdr)
{
    struct os_mbuf_pkthdr *omp;
    struct os_mbuf *om;
    int rc;

    omp = STAILQ_FIRST(&ble_hs_test_util_prev_tx_queue);
    if (omp == NULL) {
        return NULL;
    }
    STAILQ_REMOVE_HEAD(&ble_hs_test_util_prev_tx_queue, omp_next);

    om = OS_MBUF_PKTHDR_TO_MBUF(omp);

    rc = ble_hs_hci_util_data_hdr_strip(om, out_hci_hdr);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT_FATAL(out_hci_hdr->hdh_len == OS_MBUF_PKTLEN(om));

    return om;
}

struct os_mbuf *
ble_hs_test_util_prev_tx_dequeue(void)
{
    struct ble_l2cap_hdr l2cap_hdr;
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    uint8_t pb;
    int rc;

    os_mbuf_free_chain(ble_hs_test_util_prev_tx_cur);

    om = ble_hs_test_util_prev_tx_dequeue_once(&hci_hdr);
    if (om != NULL) {
        pb = BLE_HCI_DATA_PB(hci_hdr.hdh_handle_pb_bc);
        TEST_ASSERT_FATAL(pb == BLE_HCI_PB_FIRST_NON_FLUSH);

        rc = ble_l2cap_parse_hdr(om, 0, &l2cap_hdr);
        TEST_ASSERT_FATAL(rc == 0);

        os_mbuf_adj(om, BLE_L2CAP_HDR_SZ);

        ble_hs_test_util_prev_tx_cur = om;
        while (OS_MBUF_PKTLEN(ble_hs_test_util_prev_tx_cur) <
               l2cap_hdr.blh_len) {

            om = ble_hs_test_util_prev_tx_dequeue_once(&hci_hdr);
            TEST_ASSERT_FATAL(om != NULL);

            pb = BLE_HCI_DATA_PB(hci_hdr.hdh_handle_pb_bc);
            TEST_ASSERT_FATAL(pb == BLE_HCI_PB_MIDDLE);

            os_mbuf_concat(ble_hs_test_util_prev_tx_cur, om);
        }
    } else {
        ble_hs_test_util_prev_tx_cur = NULL;
    }

    return ble_hs_test_util_prev_tx_cur;
}

struct os_mbuf *
ble_hs_test_util_prev_tx_dequeue_pullup(void)
{
    struct os_mbuf *om;

    om = ble_hs_test_util_prev_tx_dequeue();
    if (om != NULL) {
        om = os_mbuf_pullup(om, OS_MBUF_PKTLEN(om));
        TEST_ASSERT_FATAL(om != NULL);
        ble_hs_test_util_prev_tx_cur = om;
    }

    return om;
}

int
ble_hs_test_util_prev_tx_queue_sz(void)
{
    struct os_mbuf_pkthdr *omp;
    int cnt;

    cnt = 0;
    STAILQ_FOREACH(omp, &ble_hs_test_util_prev_tx_queue, omp_next) {
        cnt++;
    }

    return cnt;
}

void
ble_hs_test_util_prev_tx_queue_clear(void)
{
    ble_hs_test_util_tx_all();
    while (!STAILQ_EMPTY(&ble_hs_test_util_prev_tx_queue)) {
        ble_hs_test_util_prev_tx_dequeue();
    }
}

void *
ble_hs_test_util_get_first_hci_tx(void)
{
    if (ble_hs_test_util_num_prev_hci_txes == 0) {
        return NULL;
    }

    memcpy(ble_hs_test_util_cur_hci_tx, ble_hs_test_util_prev_hci_tx[0],
           sizeof ble_hs_test_util_cur_hci_tx);

    ble_hs_test_util_num_prev_hci_txes--;
    if (ble_hs_test_util_num_prev_hci_txes > 0) {
        memmove(
            ble_hs_test_util_prev_hci_tx, ble_hs_test_util_prev_hci_tx + 1,
            sizeof ble_hs_test_util_prev_hci_tx[0] *
            ble_hs_test_util_num_prev_hci_txes);
    }

    return ble_hs_test_util_cur_hci_tx;
}

void *
ble_hs_test_util_get_last_hci_tx(void)
{
    if (ble_hs_test_util_num_prev_hci_txes == 0) {
        return NULL;
    }

    ble_hs_test_util_num_prev_hci_txes--;
    memcpy(ble_hs_test_util_cur_hci_tx,
           ble_hs_test_util_prev_hci_tx + ble_hs_test_util_num_prev_hci_txes,
           sizeof ble_hs_test_util_cur_hci_tx);

    return ble_hs_test_util_cur_hci_tx;
}

void
ble_hs_test_util_enqueue_hci_tx(void *cmd)
{
    TEST_ASSERT_FATAL(ble_hs_test_util_num_prev_hci_txes <
                      BLE_HS_TEST_UTIL_PREV_HCI_TX_CNT);
    memcpy(ble_hs_test_util_prev_hci_tx + ble_hs_test_util_num_prev_hci_txes,
           cmd, 260);

    ble_hs_test_util_num_prev_hci_txes++;
}

void
ble_hs_test_util_prev_hci_tx_clear(void)
{
    ble_hs_test_util_num_prev_hci_txes = 0;
}

static void
ble_hs_test_util_rx_hci_evt(uint8_t *evt)
{
    uint8_t *evbuf;
    int totlen;
    int rc;

    totlen = BLE_HCI_EVENT_HDR_LEN + evt[1];
    TEST_ASSERT_FATAL(totlen <= UINT8_MAX + BLE_HCI_EVENT_HDR_LEN);

    if (os_started()) {
        evbuf = ble_hci_trans_buf_alloc(
            BLE_HCI_TRANS_BUF_EVT_LO);
        TEST_ASSERT_FATAL(evbuf != NULL);

        memcpy(evbuf, evt, totlen);
        rc = ble_hci_trans_ll_evt_tx(evbuf);
    } else {
        rc = ble_hs_hci_evt_process(evt);
    }

    TEST_ASSERT_FATAL(rc == 0);
}

void
ble_hs_test_util_build_cmd_complete(uint8_t *dst, int len,
                                    uint8_t param_len, uint8_t num_pkts,
                                    uint16_t opcode)
{
    TEST_ASSERT(len >= BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN);

    dst[0] = BLE_HCI_EVCODE_COMMAND_COMPLETE;
    dst[1] = 3 + param_len;
    dst[2] = num_pkts;
    htole16(dst + 3, opcode);
}

void
ble_hs_test_util_build_cmd_status(uint8_t *dst, int len,
                                  uint8_t status, uint8_t num_pkts,
                                  uint16_t opcode)
{
    TEST_ASSERT(len >= BLE_HCI_EVENT_CMD_STATUS_LEN);

    dst[0] = BLE_HCI_EVCODE_COMMAND_STATUS;
    dst[1] = BLE_HCI_EVENT_CMD_STATUS_LEN;
    dst[2] = status;
    dst[3] = num_pkts;
    htole16(dst + 4, opcode);
}

#define BLE_HS_TEST_UTIL_PHONY_ACK_MAX  64
struct ble_hs_test_util_phony_ack {
    uint16_t opcode;
    uint8_t status;
    uint8_t evt_params[256];
    uint8_t evt_params_len;
};

static struct ble_hs_test_util_phony_ack
ble_hs_test_util_phony_acks[BLE_HS_TEST_UTIL_PHONY_ACK_MAX];
static int ble_hs_test_util_num_phony_acks;

static int
ble_hs_test_util_phony_ack_cb(uint8_t *ack, int ack_buf_len)
{
    struct ble_hs_test_util_phony_ack *entry;

    if (ble_hs_test_util_num_phony_acks == 0) {
        return BLE_HS_ETIMEOUT_HCI;
    }

    entry = ble_hs_test_util_phony_acks;

    ble_hs_test_util_build_cmd_complete(ack, 256,
                                        entry->evt_params_len + 1, 1,
                                        entry->opcode);
    ack[BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN] = entry->status;
    memcpy(ack + BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN + 1, entry->evt_params,
           entry->evt_params_len);

    ble_hs_test_util_num_phony_acks--;
    if (ble_hs_test_util_num_phony_acks > 0) {
        memmove(ble_hs_test_util_phony_acks, ble_hs_test_util_phony_acks + 1,
                sizeof *entry * ble_hs_test_util_num_phony_acks);
    }

    return 0;
}

void
ble_hs_test_util_set_ack_params(uint16_t opcode, uint8_t status, void *params,
                                uint8_t params_len)
{
    struct ble_hs_test_util_phony_ack *ack;

    ack = ble_hs_test_util_phony_acks + 0;
    ack->opcode = opcode;
    ack->status = status;

    if (params == NULL || params_len == 0) {
        ack->evt_params_len = 0;
    } else {
        memcpy(ack->evt_params, params, params_len);
        ack->evt_params_len = params_len;
    }
    ble_hs_test_util_num_phony_acks = 1;

    ble_hs_hci_set_phony_ack_cb(ble_hs_test_util_phony_ack_cb);
}

void
ble_hs_test_util_set_ack(uint16_t opcode, uint8_t status)
{
    ble_hs_test_util_set_ack_params(opcode, status, NULL, 0);
}

static void
ble_hs_test_util_set_ack_seq(struct ble_hs_test_util_phony_ack *acks)
{
    int i;

    for (i = 0; acks[i].opcode != 0; i++) {
        ble_hs_test_util_phony_acks[i] = acks[i];
    }
    ble_hs_test_util_num_phony_acks = i;

    ble_hs_hci_set_phony_ack_cb(ble_hs_test_util_phony_ack_cb);
}

void
ble_hs_test_util_create_rpa_conn(uint16_t handle, uint8_t own_addr_type,
                                 const uint8_t *our_rpa,
                                 uint8_t peer_addr_type,
                                 const uint8_t *peer_id_addr,
                                 const uint8_t *peer_rpa,
                                 ble_gap_event_fn *cb, void *cb_arg)
{
    struct hci_le_conn_complete evt;
    int rc;

    ble_hs_test_util_connect(own_addr_type, peer_addr_type,
                             peer_id_addr, 0, NULL, cb, cb_arg, 0);

    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = handle;
    evt.role = BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER;
    evt.peer_addr_type = peer_addr_type;
    memcpy(evt.peer_addr, peer_id_addr, 6);
    evt.conn_itvl = BLE_GAP_INITIAL_CONN_ITVL_MAX;
    evt.conn_latency = BLE_GAP_INITIAL_CONN_LATENCY;
    evt.supervision_timeout = BLE_GAP_INITIAL_SUPERVISION_TIMEOUT;
    memcpy(evt.local_rpa, our_rpa, 6);
    memcpy(evt.peer_rpa, peer_rpa, 6);

    rc = ble_gap_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);

    ble_hs_test_util_prev_hci_tx_clear();
}

void
ble_hs_test_util_create_conn(uint16_t handle, uint8_t *peer_id_addr,
                             ble_gap_event_fn *cb, void *cb_arg)
{
    static uint8_t null_addr[6];

    ble_hs_test_util_create_rpa_conn(handle, BLE_ADDR_TYPE_PUBLIC, null_addr,
                                     BLE_ADDR_TYPE_PUBLIC, peer_id_addr,
                                     null_addr, cb, cb_arg);
}

static void
ble_hs_test_util_conn_params_dflt(struct ble_gap_conn_params *conn_params)
{
    conn_params->scan_itvl = 0x0010;
    conn_params->scan_window = 0x0010;
    conn_params->itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN;
    conn_params->itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX;
    conn_params->latency = BLE_GAP_INITIAL_CONN_LATENCY;
    conn_params->supervision_timeout = BLE_GAP_INITIAL_SUPERVISION_TIMEOUT;
    conn_params->min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN;
    conn_params->max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN;
}

static void
ble_hs_test_util_hcc_from_conn_params(
    struct hci_create_conn *hcc, uint8_t own_addr_type, uint8_t peer_addr_type,
    const uint8_t *peer_addr, const struct ble_gap_conn_params *conn_params)
{
    hcc->scan_itvl = conn_params->scan_itvl;
    hcc->scan_window = conn_params->scan_window;

    if (peer_addr_type == BLE_GAP_ADDR_TYPE_WL) {
        hcc->filter_policy = BLE_HCI_CONN_FILT_USE_WL;
        hcc->peer_addr_type = 0;
        memset(hcc->peer_addr, 0, 6);
    } else {
        hcc->filter_policy = BLE_HCI_CONN_FILT_NO_WL;
        hcc->peer_addr_type = peer_addr_type;
        memcpy(hcc->peer_addr, peer_addr, 6);
    }
    hcc->own_addr_type = own_addr_type;
    hcc->conn_itvl_min = conn_params->itvl_min;
    hcc->conn_itvl_max = conn_params->itvl_max;
    hcc->conn_latency = conn_params->latency;
    hcc->supervision_timeout = conn_params->supervision_timeout;
    hcc->min_ce_len = conn_params->min_ce_len;
    hcc->max_ce_len = conn_params->max_ce_len;
}

void
ble_hs_test_util_verify_tx_create_conn(const struct hci_create_conn *exp)
{
    uint8_t param_len;
    uint8_t *param;

    param = ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                           BLE_HCI_OCF_LE_CREATE_CONN,
                                           &param_len);
    TEST_ASSERT(param_len == BLE_HCI_CREATE_CONN_LEN);

    TEST_ASSERT(le16toh(param + 0) == exp->scan_itvl);
    TEST_ASSERT(le16toh(param + 2) == exp->scan_window);
    TEST_ASSERT(param[4] == exp->filter_policy);
    TEST_ASSERT(param[5] == exp->peer_addr_type);
    TEST_ASSERT(memcmp(param + 6, exp->peer_addr, 6) == 0);
    TEST_ASSERT(param[12] == exp->own_addr_type);
    TEST_ASSERT(le16toh(param + 13) == exp->conn_itvl_min);
    TEST_ASSERT(le16toh(param + 15) == exp->conn_itvl_max);
    TEST_ASSERT(le16toh(param + 17) == exp->conn_latency);
    TEST_ASSERT(le16toh(param + 19) == exp->supervision_timeout);
    TEST_ASSERT(le16toh(param + 21) == exp->min_ce_len);
    TEST_ASSERT(le16toh(param + 23) == exp->max_ce_len);
}

int
ble_hs_test_util_connect(uint8_t own_addr_type, uint8_t peer_addr_type,
                         const uint8_t *peer_addr, int32_t duration_ms,
                         const struct ble_gap_conn_params *params,
                         ble_gap_event_fn *cb, void *cb_arg,
                         uint8_t ack_status)
{
    struct ble_gap_conn_params dflt_params;
    struct hci_create_conn hcc;
    int rc;

    /* This function ensures the most recently sent HCI command is the expected
     * create connection command.  If the current test case has unverified HCI
     * commands, assume we are not interested in them and clear the queue.
     */
    ble_hs_test_util_prev_hci_tx_clear();

    ble_hs_test_util_set_ack(
        ble_hs_hci_util_opcode_join(BLE_HCI_OGF_LE,
                                    BLE_HCI_OCF_LE_CREATE_CONN),
        ack_status);

    rc = ble_gap_connect(own_addr_type, peer_addr_type, peer_addr, duration_ms,
                         params, cb, cb_arg);

    TEST_ASSERT(rc == BLE_HS_HCI_ERR(ack_status));

    if (params == NULL) {
        ble_hs_test_util_conn_params_dflt(&dflt_params);
        params = &dflt_params;
    }

    ble_hs_test_util_hcc_from_conn_params(&hcc, own_addr_type,
                                          peer_addr_type, peer_addr, params);
    ble_hs_test_util_verify_tx_create_conn(&hcc);

    return rc;
}

int
ble_hs_test_util_conn_cancel(uint8_t ack_status)
{
    int rc;

    ble_hs_test_util_set_ack(
        ble_hs_hci_util_opcode_join(BLE_HCI_OGF_LE,
                                    BLE_HCI_OCF_LE_CREATE_CONN_CANCEL),
        ack_status);

    rc = ble_gap_conn_cancel();
    return rc;
}

void
ble_hs_test_util_conn_cancel_full(void)
{
    struct hci_le_conn_complete evt;
    int rc;

    ble_hs_test_util_conn_cancel(0);

    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_UNK_CONN_ID;
    evt.role = BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER;

    rc = ble_gap_rx_conn_complete(&evt);
    TEST_ASSERT_FATAL(rc == 0);
}

int
ble_hs_test_util_conn_terminate(uint16_t conn_handle, uint8_t hci_status)
{
    int rc;

    ble_hs_test_util_set_ack(
        ble_hs_hci_util_opcode_join(BLE_HCI_OGF_LINK_CTRL,
                                    BLE_HCI_OCF_DISCONNECT_CMD),
        hci_status);

    rc = ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return rc;
}

void
ble_hs_test_util_conn_disconnect(uint16_t conn_handle)
{
    struct hci_disconn_complete evt;
    int rc;

    rc = ble_hs_test_util_conn_terminate(conn_handle, 0);
    TEST_ASSERT_FATAL(rc == 0);

    /* Receive disconnection complete event. */
    evt.connection_handle = conn_handle;
    evt.status = 0;
    evt.reason = BLE_ERR_CONN_TERM_LOCAL;
    ble_gap_rx_disconn_complete(&evt);
}

int
ble_hs_test_util_exp_hci_status(int cmd_idx, int fail_idx, uint8_t fail_status)
{
    if (cmd_idx == fail_idx) {
        return BLE_HS_HCI_ERR(fail_status);
    } else {
        return 0;
    }
}

int
ble_hs_test_util_disc(uint8_t own_addr_type, int32_t duration_ms,
                      const struct ble_gap_disc_params *disc_params,
                      ble_gap_event_fn *cb, void *cb_arg, int fail_idx,
                      uint8_t fail_status)
{
    int rc;

    ble_hs_test_util_set_ack_seq(((struct ble_hs_test_util_phony_ack[]) {
        {
            BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_SET_SCAN_PARAMS),
            ble_hs_test_util_exp_hci_status(0, fail_idx, fail_status),
        },
        {
            BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_SET_SCAN_ENABLE),
            ble_hs_test_util_exp_hci_status(1, fail_idx, fail_status),
        },

        { 0 }
    }));

    rc = ble_gap_disc(own_addr_type, duration_ms, disc_params,
                      cb, cb_arg);
    return rc;
}

int
ble_hs_test_util_disc_cancel(uint8_t ack_status)
{
    int rc;

    ble_hs_test_util_set_ack(
        ble_hs_hci_util_opcode_join(BLE_HCI_OGF_LE,
                                    BLE_HCI_OCF_LE_SET_SCAN_ENABLE),
        ack_status);

    rc = ble_gap_disc_cancel();
    return rc;
}

static void
ble_hs_test_util_verify_tx_rd_pwr(void)
{
    uint8_t param_len;

    ble_hs_test_util_verify_tx_hci(BLE_HCI_OGF_LE,
                                   BLE_HCI_OCF_LE_RD_ADV_CHAN_TXPWR,
                                   &param_len);
    TEST_ASSERT(param_len == 0);
}

int
ble_hs_test_util_adv_set_fields(struct ble_hs_adv_fields *adv_fields,
                                uint8_t hci_status)
{
    int auto_pwr;
    int rc;

    auto_pwr = adv_fields->tx_pwr_lvl_is_present &&
               adv_fields->tx_pwr_lvl == BLE_HS_ADV_TX_PWR_LVL_AUTO;

    if (auto_pwr) {
        ble_hs_test_util_set_ack_params(
            ble_hs_hci_util_opcode_join(BLE_HCI_OGF_LE,
                                        BLE_HCI_OCF_LE_RD_ADV_CHAN_TXPWR),
            hci_status,
            ((uint8_t[1]){0}), 1);
    }

    rc = ble_gap_adv_set_fields(adv_fields);
    if (rc == 0 && auto_pwr) {
        /* Verify tx of set advertising params command. */
        ble_hs_test_util_verify_tx_rd_pwr();
    }

    return rc;
}

int
ble_hs_test_util_adv_start(uint8_t own_addr_type,
                           uint8_t peer_addr_type, const uint8_t *peer_addr, 
                           const struct ble_gap_adv_params *adv_params,
                           ble_gap_event_fn *cb, void *cb_arg,
                           int fail_idx, uint8_t fail_status)
{
    struct ble_hs_test_util_phony_ack acks[6];
    int rc;
    int i;

    i = 0;

    acks[i] = (struct ble_hs_test_util_phony_ack) {
        BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_SET_ADV_PARAMS),
        fail_idx == i ? fail_status : 0,
    };
    i++;

    if (adv_params->conn_mode != BLE_GAP_CONN_MODE_DIR) {
        acks[i] = (struct ble_hs_test_util_phony_ack) {
            BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_SET_ADV_DATA),
            ble_hs_test_util_exp_hci_status(i, fail_idx, fail_status),
        };
        i++;

        acks[i] = (struct ble_hs_test_util_phony_ack) {
            BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_SET_SCAN_RSP_DATA),
            ble_hs_test_util_exp_hci_status(i, fail_idx, fail_status),
        };
        i++;
    }

    acks[i] = (struct ble_hs_test_util_phony_ack) {
        BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_SET_ADV_ENABLE),
        ble_hs_test_util_exp_hci_status(i, fail_idx, fail_status),
    };
    i++;

    memset(acks + i, 0, sizeof acks[i]);

    ble_hs_test_util_set_ack_seq(acks);
    
    rc = ble_gap_adv_start(own_addr_type, peer_addr_type, peer_addr, 
                           BLE_HS_FOREVER, adv_params, cb, cb_arg);

    return rc;
}

int
ble_hs_test_util_adv_stop(uint8_t hci_status)
{
    int rc;

    ble_hs_test_util_set_ack(
        BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_SET_ADV_ENABLE),
        hci_status);

    rc = ble_gap_adv_stop();
    return rc;
}

int
ble_hs_test_util_wl_set(struct ble_gap_white_entry *white_list,
                        uint8_t white_list_count,
                        int fail_idx, uint8_t fail_status)
{
    struct ble_hs_test_util_phony_ack acks[64];
    int cmd_idx;
    int rc;
    int i;

    TEST_ASSERT_FATAL(white_list_count < 63);

    cmd_idx = 0;
    acks[cmd_idx] = (struct ble_hs_test_util_phony_ack) {
        BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_CLEAR_WHITE_LIST),
        ble_hs_test_util_exp_hci_status(cmd_idx, fail_idx, fail_status),
    };
    cmd_idx++;

    for (i = 0; i < white_list_count; i++) {
        acks[cmd_idx] = (struct ble_hs_test_util_phony_ack) {
            BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_ADD_WHITE_LIST),
            ble_hs_test_util_exp_hci_status(cmd_idx, fail_idx, fail_status),
        };

        cmd_idx++;
    }
    memset(acks + cmd_idx, 0, sizeof acks[cmd_idx]);

    ble_hs_test_util_set_ack_seq(acks);
    rc = ble_gap_wl_set(white_list, white_list_count);
    return rc;
}

int
ble_hs_test_util_conn_update(uint16_t conn_handle,
                             struct ble_gap_upd_params *params,
                             uint8_t hci_status)
{
    int rc;

    ble_hs_test_util_set_ack(
        BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_CONN_UPDATE), hci_status);

    rc = ble_gap_update_params(conn_handle, params);
    return rc;
}

int
ble_hs_test_util_set_our_irk(const uint8_t *irk, int fail_idx,
                             uint8_t hci_status)
{
    int rc;

    ble_hs_test_util_set_ack_seq(((struct ble_hs_test_util_phony_ack[]) {
        {
            BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_SET_ADDR_RES_EN),
            ble_hs_test_util_exp_hci_status(0, fail_idx, hci_status),
        },
        {
            BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_CLR_RESOLV_LIST),
            ble_hs_test_util_exp_hci_status(1, fail_idx, hci_status),
        },
        {
            BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_SET_ADDR_RES_EN),
            ble_hs_test_util_exp_hci_status(2, fail_idx, hci_status),
        },
        {
            BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_ADD_RESOLV_LIST),
            ble_hs_test_util_exp_hci_status(3, fail_idx, hci_status),
        },
    }));

    rc = ble_hs_pvcy_set_our_irk(irk);
    return rc;
}

int
ble_hs_test_util_security_initiate(uint16_t conn_handle, uint8_t hci_status)
{
    int rc;

    ble_hs_test_util_set_ack(
        BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_START_ENCRYPT), hci_status);

    rc = ble_gap_security_initiate(conn_handle);
    return rc;
}

int
ble_hs_test_util_l2cap_rx_first_frag(uint16_t conn_handle, uint16_t cid,
                                     struct hci_data_hdr *hci_hdr,
                                     struct os_mbuf *om)
{
    int rc;

    om = ble_l2cap_prepend_hdr(om, cid, OS_MBUF_PKTLEN(om));
    TEST_ASSERT_FATAL(om != NULL);

    rc = ble_hs_test_util_l2cap_rx(conn_handle, hci_hdr, om);
    return rc;
}

int
ble_hs_test_util_l2cap_rx(uint16_t conn_handle,
                          struct hci_data_hdr *hci_hdr,
                          struct os_mbuf *om)
{
    struct ble_hs_conn *conn;
    ble_l2cap_rx_fn *rx_cb;
    struct os_mbuf *rx_buf;
    int rc;

    ble_hs_lock();

    conn = ble_hs_conn_find(conn_handle);
    if (conn != NULL) {
        rc = ble_l2cap_rx(conn, hci_hdr, om, &rx_cb, &rx_buf);
    } else {
        os_mbuf_free_chain(om);
    }

    ble_hs_unlock();

    if (conn == NULL) {
        rc = BLE_HS_ENOTCONN;
    } else if (rc == 0) {
        TEST_ASSERT_FATAL(rx_cb != NULL);
        TEST_ASSERT_FATAL(rx_buf != NULL);
        rc = rx_cb(conn_handle, &rx_buf);
        os_mbuf_free_chain(rx_buf);
    } else if (rc == BLE_HS_EAGAIN) {
        /* More fragments on the way. */
        rc = 0;
    }

    return rc;
}

int
ble_hs_test_util_l2cap_rx_payload_flat(uint16_t conn_handle, uint16_t cid,
                                       const void *data, int len)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    int rc;

    om = ble_hs_mbuf_l2cap_pkt();
    TEST_ASSERT_FATAL(om != NULL);

    rc = os_mbuf_append(om, data, len);
    TEST_ASSERT_FATAL(rc == 0);

    hci_hdr.hdh_handle_pb_bc =
        ble_hs_hci_util_handle_pb_bc_join(conn_handle,
                                          BLE_HCI_PB_FIRST_FLUSH, 0);
    hci_hdr.hdh_len = OS_MBUF_PKTHDR(om)->omp_len;

    rc = ble_hs_test_util_l2cap_rx_first_frag(conn_handle, cid, &hci_hdr, om);
    return rc;
}

void
ble_hs_test_util_rx_att_err_rsp(uint16_t conn_handle, uint8_t req_op,
                                uint8_t error_code, uint16_t err_handle)
{
    struct ble_att_error_rsp rsp;
    uint8_t buf[BLE_ATT_ERROR_RSP_SZ];
    int rc;

    rsp.baep_req_op = req_op;
    rsp.baep_handle = err_handle;
    rsp.baep_error_code = error_code;

    ble_att_error_rsp_write(buf, sizeof buf, &rsp);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
}

void
ble_hs_test_util_set_startup_acks(void)
{
    /* Receive acknowledgements for the startup sequence.  We sent the
     * corresponding requests when the host task was started.
     */
    ble_hs_test_util_set_ack_seq(((struct ble_hs_test_util_phony_ack[]) {
        {
            .opcode = ble_hs_hci_util_opcode_join(BLE_HCI_OGF_CTLR_BASEBAND,
                                                  BLE_HCI_OCF_CB_RESET),
        },
        {
            .opcode = ble_hs_hci_util_opcode_join(
                BLE_HCI_OGF_CTLR_BASEBAND, BLE_HCI_OCF_CB_SET_EVENT_MASK),
        },
        {
            .opcode = ble_hs_hci_util_opcode_join(
                BLE_HCI_OGF_CTLR_BASEBAND, BLE_HCI_OCF_CB_SET_EVENT_MASK2),
        },
        {
            .opcode = ble_hs_hci_util_opcode_join(
                BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_EVENT_MASK),
        },
        {
            .opcode = ble_hs_hci_util_opcode_join(
                BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_BUF_SIZE),
            /* Use a very low buffer size (16) to test fragmentation. */
            .evt_params = { 0x10, 0x00, 0x20 },
            .evt_params_len = 3,
        },
        {
            .opcode = ble_hs_hci_util_opcode_join(
                BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_LOC_SUPP_FEAT),
            .evt_params = { 0 },
            .evt_params_len = 8,
        },
        {
            .opcode = ble_hs_hci_util_opcode_join(
                BLE_HCI_OGF_INFO_PARAMS, BLE_HCI_OCF_IP_RD_BD_ADDR),
            .evt_params = BLE_HS_TEST_UTIL_PUB_ADDR_VAL,
            .evt_params_len = 6,
        },
        {
            .opcode = ble_hs_hci_util_opcode_join(
                BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_ADDR_RES_EN),
        },
        {
            .opcode = ble_hs_hci_util_opcode_join(
                BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_CLR_RESOLV_LIST),
        },
        {
            .opcode = ble_hs_hci_util_opcode_join(
                BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_ADDR_RES_EN),
        },
        {
            .opcode = ble_hs_hci_util_opcode_join(
                BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_ADD_RESOLV_LIST),
        },
        { 0 }
    }));
}

void
ble_hs_test_util_rx_num_completed_pkts_event(
    struct ble_hs_test_util_num_completed_pkts_entry *entries)
{
    struct ble_hs_test_util_num_completed_pkts_entry *entry;
    uint8_t buf[1024];
    int num_entries;
    int off;
    int i;

    /* Count number of entries. */
    num_entries = 0;
    for (entry = entries; entry->handle_id != 0; entry++) {
        num_entries++;
    }
    TEST_ASSERT_FATAL(num_entries <= UINT8_MAX);

    buf[0] = BLE_HCI_EVCODE_NUM_COMP_PKTS;
    buf[2] = num_entries;

    off = 3;
    for (i = 0; i < num_entries; i++) {
        htole16(buf + off, entries[i].handle_id);
        off += 2;
    }
    for (i = 0; i < num_entries; i++) {
        htole16(buf + off, entries[i].num_pkts);
        off += 2;
    }

    buf[1] = off - 2;

    ble_hs_test_util_rx_hci_evt(buf);
}

void
ble_hs_test_util_rx_disconn_complete_event(struct hci_disconn_complete *evt)
{
    uint8_t buf[BLE_HCI_EVENT_HDR_LEN + BLE_HCI_EVENT_DISCONN_COMPLETE_LEN];

    buf[0] = BLE_HCI_EVCODE_DISCONN_CMP;
    buf[1] = BLE_HCI_EVENT_DISCONN_COMPLETE_LEN;
    buf[2] = evt->status;
    htole16(buf + 3, evt->connection_handle);
    buf[5] = evt->reason;

    ble_hs_test_util_rx_hci_evt(buf);
}

uint8_t *
ble_hs_test_util_verify_tx_hci(uint8_t ogf, uint16_t ocf,
                               uint8_t *out_param_len)
{
    uint16_t opcode;
    uint8_t *cmd;

    cmd = ble_hs_test_util_get_first_hci_tx();
    TEST_ASSERT_FATAL(cmd != NULL);

    opcode = le16toh(cmd);
    TEST_ASSERT(BLE_HCI_OGF(opcode) == ogf);
    TEST_ASSERT(BLE_HCI_OCF(opcode) == ocf);

    if (out_param_len != NULL) {
        *out_param_len = cmd[2];
    }

    return cmd + 3;
}

void
ble_hs_test_util_tx_all(void)
{
    ble_hs_process_tx_data_queue();
}

void
ble_hs_test_util_verify_tx_prep_write(uint16_t attr_handle, uint16_t offset,
                                      const void *data, int data_len)
{
    struct ble_att_prep_write_cmd req;
    struct os_mbuf *om;

    ble_hs_test_util_tx_all();
    om = ble_hs_test_util_prev_tx_dequeue();
    TEST_ASSERT_FATAL(om != NULL);
    TEST_ASSERT(OS_MBUF_PKTLEN(om) ==
                BLE_ATT_PREP_WRITE_CMD_BASE_SZ + data_len);

    om = os_mbuf_pullup(om, BLE_ATT_PREP_WRITE_CMD_BASE_SZ);
    TEST_ASSERT_FATAL(om != NULL);

    ble_att_prep_write_req_parse(om->om_data, om->om_len, &req);
    TEST_ASSERT(req.bapc_handle == attr_handle);
    TEST_ASSERT(req.bapc_offset == offset);
    TEST_ASSERT(os_mbuf_cmpf(om, BLE_ATT_PREP_WRITE_CMD_BASE_SZ,
                             data, data_len) == 0);
}

void
ble_hs_test_util_verify_tx_exec_write(uint8_t expected_flags)
{
    struct ble_att_exec_write_req req;
    struct os_mbuf *om;

    ble_hs_test_util_tx_all();
    om = ble_hs_test_util_prev_tx_dequeue_pullup();
    TEST_ASSERT_FATAL(om != NULL);
    TEST_ASSERT(om->om_len == BLE_ATT_EXEC_WRITE_REQ_SZ);

    ble_att_exec_write_req_parse(om->om_data, om->om_len, &req);
    TEST_ASSERT(req.baeq_flags == expected_flags);
}

void
ble_hs_test_util_verify_tx_read_rsp_gen(uint8_t att_op,
                                        uint8_t *attr_data, int attr_len)
{
    struct os_mbuf *om;
    uint8_t u8;
    int rc;
    int i;

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue();

    rc = os_mbuf_copydata(om, 0, 1, &u8);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(u8 == att_op);

    for (i = 0; i < attr_len; i++) {
        rc = os_mbuf_copydata(om, i + 1, 1, &u8);
        TEST_ASSERT(rc == 0);
        TEST_ASSERT(u8 == attr_data[i]);
    }

    rc = os_mbuf_copydata(om, i + 1, 1, &u8);
    TEST_ASSERT(rc != 0);
}

void
ble_hs_test_util_verify_tx_read_rsp(uint8_t *attr_data, int attr_len)
{
    ble_hs_test_util_verify_tx_read_rsp_gen(BLE_ATT_OP_READ_RSP,
                                            attr_data, attr_len);
}

void
ble_hs_test_util_verify_tx_read_blob_rsp(uint8_t *attr_data, int attr_len)
{
    ble_hs_test_util_verify_tx_read_rsp_gen(BLE_ATT_OP_READ_BLOB_RSP,
                                            attr_data, attr_len);
}

void
ble_hs_test_util_set_static_rnd_addr(void)
{
    uint8_t addr[6] = { 1, 2, 3, 4, 5, 0xc1 };
    int rc;

    ble_hs_test_util_set_ack(
        BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_SET_RAND_ADDR), 0);

    rc = ble_hs_id_set_rnd(addr);
    TEST_ASSERT_FATAL(rc == 0);

    ble_hs_test_util_get_first_hci_tx();
}

struct os_mbuf *
ble_hs_test_util_om_from_flat(const void *buf, uint16_t len)
{
    struct os_mbuf *om;

    om = ble_hs_mbuf_from_flat(buf, len);
    TEST_ASSERT_FATAL(om != NULL);

    return om;
}

int
ble_hs_test_util_flat_attr_cmp(const struct ble_hs_test_util_flat_attr *a,
                               const struct ble_hs_test_util_flat_attr *b)
{
    if (a->handle != b->handle) {
        return -1;
    }
    if (a->offset != b->offset) {
        return -1;
    }
    if (a->value_len != b->value_len) {
        return -1;
    }
    return memcmp(a->value, b->value, a->value_len);
}

void
ble_hs_test_util_attr_to_flat(struct ble_hs_test_util_flat_attr *flat,
                              const struct ble_gatt_attr *attr)
{
    int rc;

    flat->handle = attr->handle;
    flat->offset = attr->offset;
    rc = ble_hs_mbuf_to_flat(attr->om, flat->value, sizeof flat->value,
                           &flat->value_len);
    TEST_ASSERT_FATAL(rc == 0);
}

void
ble_hs_test_util_attr_from_flat(struct ble_gatt_attr *attr,
                                const struct ble_hs_test_util_flat_attr *flat)
{
    attr->handle = flat->handle;
    attr->offset = flat->offset;
    attr->om = ble_hs_test_util_om_from_flat(flat->value, flat->value_len);
}

int
ble_hs_test_util_read_local_flat(uint16_t attr_handle, uint16_t max_len,
                                 void *buf, uint16_t *out_len)
{
    struct os_mbuf *om;
    int rc;

    rc = ble_att_svr_read_local(attr_handle, &om);
    if (rc != 0) {
        return rc;
    }

    TEST_ASSERT_FATAL(OS_MBUF_PKTLEN(om) <= max_len);

    rc = os_mbuf_copydata(om, 0, OS_MBUF_PKTLEN(om), buf);
    TEST_ASSERT_FATAL(rc == 0);

    *out_len = OS_MBUF_PKTLEN(om);

    os_mbuf_free_chain(om);
    return 0;
}

int
ble_hs_test_util_write_local_flat(uint16_t attr_handle,
                                  const void *buf, uint16_t buf_len)
{
    struct os_mbuf *om;
    int rc;

    om = ble_hs_test_util_om_from_flat(buf, buf_len);
    rc = ble_att_svr_write_local(attr_handle, om);
    return rc;
}

int
ble_hs_test_util_gatt_write_flat(uint16_t conn_handle, uint16_t attr_handle,
                                 const void *data, uint16_t data_len,
                                 ble_gatt_attr_fn *cb, void *cb_arg)
{
    struct os_mbuf *om;
    int rc;

    om = ble_hs_test_util_om_from_flat(data, data_len);
    rc = ble_gattc_write(conn_handle, attr_handle, om, cb, cb_arg);

    return rc;
}

int
ble_hs_test_util_gatt_write_no_rsp_flat(uint16_t conn_handle,
                                        uint16_t attr_handle,
                                        const void *data, uint16_t data_len)
{
    struct os_mbuf *om;
    int rc;

    om = ble_hs_test_util_om_from_flat(data, data_len);
    rc = ble_gattc_write_no_rsp(conn_handle, attr_handle, om);

    return rc;
}

int
ble_hs_test_util_gatt_write_long_flat(uint16_t conn_handle,
                                      uint16_t attr_handle,
                                      const void *data, uint16_t data_len,
                                      ble_gatt_attr_fn *cb, void *cb_arg)
{
    struct os_mbuf *om;
    int rc;

    om = ble_hs_test_util_om_from_flat(data, data_len);
    rc = ble_gattc_write_long(conn_handle, attr_handle, om, cb, cb_arg);

    return rc;
}

static int
ble_hs_test_util_mbuf_chain_len(const struct os_mbuf *om)
{
    int count;

    count = 0;
    while (om != NULL) {
        count++;
        om = SLIST_NEXT(om, om_next);
    }

    return count;
}

int
ble_hs_test_util_mbuf_count(const struct ble_hs_test_util_mbuf_params *params)
{
    const struct ble_att_prep_entry *prep;
    const struct os_mbuf_pkthdr *omp;
    const struct ble_l2cap_chan *chan;
    const struct ble_hs_conn *conn;
    const struct os_mbuf *om;
    int count;
    int i;

    ble_hs_process_tx_data_queue();
    ble_hs_process_rx_data_queue();

    count = ble_hs_test_util_mbuf_mpool.mp_num_free;

    if (params->prev_tx) {
        count += ble_hs_test_util_mbuf_chain_len(ble_hs_test_util_prev_tx_cur);
        STAILQ_FOREACH(omp, &ble_hs_test_util_prev_tx_queue, omp_next) {
            om = OS_MBUF_PKTHDR_TO_MBUF(omp);
            count += ble_hs_test_util_mbuf_chain_len(om);
        }
    }

    ble_hs_lock();
    for (i = 0; ; i++) {
        conn = ble_hs_conn_find_by_idx(i);
        if (conn == NULL) {
            break;
        }

        if (params->rx_queue) {
            SLIST_FOREACH(chan, &conn->bhc_channels, blc_next) {
                count += ble_hs_test_util_mbuf_chain_len(chan->blc_rx_buf);
            }
        }

        if (params->prep_list) {
            SLIST_FOREACH(prep, &conn->bhc_att_svr.basc_prep_list, bape_next) {
                count += ble_hs_test_util_mbuf_chain_len(prep->bape_value);
            }
        }
    }
    ble_hs_unlock();

    return count;
}

void
ble_hs_test_util_assert_mbufs_freed(
    const struct ble_hs_test_util_mbuf_params *params)
{
    static const struct ble_hs_test_util_mbuf_params dflt = {
        .prev_tx = 1,
        .rx_queue = 1,
        .prep_list = 1,
    };

    int count;

    if (params == NULL) {
        params = &dflt;
    }

    count = ble_hs_test_util_mbuf_count(params);
    TEST_ASSERT(count == ble_hs_test_util_mbuf_mpool.mp_num_blocks);
}

void
ble_hs_test_util_post_test(void *arg)
{
    ble_hs_test_util_assert_mbufs_freed(arg);
}

static int
ble_hs_test_util_pkt_txed(struct os_mbuf *om, void *arg)
{
    ble_hs_test_util_prev_tx_enqueue(om);
    return 0;
}

static int
ble_hs_test_util_hci_txed(uint8_t *cmdbuf, void *arg)
{
    ble_hs_test_util_enqueue_hci_tx(cmdbuf);
    ble_hci_trans_buf_free(cmdbuf);
    return 0;
}

void
ble_hs_test_util_init(void)
{
    struct ble_hci_ram_cfg hci_cfg;
    struct ble_hs_cfg cfg;
    int rc;

    tu_init();

    os_eventq_init(&ble_hs_test_util_evq);
    STAILQ_INIT(&ble_hs_test_util_prev_tx_queue);
    ble_hs_test_util_prev_tx_cur = NULL;

    os_msys_reset();
    stats_module_reset();

    cfg = ble_hs_cfg_dflt;
    cfg.max_connections = 8;
    cfg.max_l2cap_chans = 3 * cfg.max_connections;
    cfg.max_services = 16;
    cfg.max_client_configs = 32;
    cfg.max_attrs = 64;
    cfg.max_gattc_procs = 16;

    rc = ble_hs_init(&ble_hs_test_util_evq, &cfg);
    TEST_ASSERT_FATAL(rc == 0);

    rc = os_mempool_init(&ble_hs_test_util_mbuf_mpool,
                         BLE_HS_TEST_UTIL_NUM_MBUFS, 
                         BLE_HS_TEST_UTIL_MEMBLOCK_SIZE,
                         ble_hs_test_util_mbuf_mpool_data, 
                         "ble_hs_test_util_mbuf_data");
    TEST_ASSERT_FATAL(rc == 0);

    rc = os_mbuf_pool_init(&ble_hs_test_util_mbuf_pool,
                           &ble_hs_test_util_mbuf_mpool,
                           BLE_HS_TEST_UTIL_MEMBLOCK_SIZE,
                           BLE_HS_TEST_UTIL_NUM_MBUFS);
    TEST_ASSERT_FATAL(rc == 0);

    rc = os_msys_register(&ble_hs_test_util_mbuf_pool);
    TEST_ASSERT_FATAL(rc == 0);

    ble_hs_hci_set_phony_ack_cb(NULL);

    ble_hci_trans_cfg_ll(ble_hs_test_util_hci_txed, NULL,
                         ble_hs_test_util_pkt_txed, NULL);

    hci_cfg = ble_hci_ram_cfg_dflt;
    rc = ble_hci_ram_init(&hci_cfg);
    TEST_ASSERT_FATAL(rc == 0);

    ble_hs_test_util_set_startup_acks();

    rc = ble_hs_start();
    TEST_ASSERT_FATAL(rc == 0);

    ble_hs_test_util_prev_hci_tx_clear();
}
