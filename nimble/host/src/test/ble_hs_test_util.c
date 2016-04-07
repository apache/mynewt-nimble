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
#include "host/host_hci.h"
#include "ble_hs_test_util.h"

/** Use lots of small mbufs to ensure correct mbuf usage. */
#define BLE_HS_TEST_UTIL_NUM_MBUFS      (100)
#define BLE_HS_TEST_UTIL_BUF_SIZE       OS_ALIGN(32, 4)
#define BLE_HS_TEST_UTIL_MEMBLOCK_SIZE  \
    (BLE_HS_TEST_UTIL_BUF_SIZE + BLE_MBUF_MEMBLOCK_OVERHEAD)
#define BLE_HS_TEST_UTIL_MEMPOOL_SIZE   \
    OS_MEMPOOL_SIZE(BLE_HS_TEST_UTIL_NUM_MBUFS, BLE_HS_TEST_UTIL_MEMBLOCK_SIZE)

#define BLE_HS_TEST_UTIL_LE_OPCODE(ocf) \
    host_hci_opcode_join(BLE_HCI_OGF_LE, (ocf))

os_membuf_t ble_hs_test_util_mbuf_mpool_data[BLE_HS_TEST_UTIL_MEMPOOL_SIZE];
struct os_mbuf_pool ble_hs_test_util_mbuf_pool;
struct os_mempool ble_hs_test_util_mbuf_mpool;

struct os_mbuf *ble_hs_test_util_prev_tx;

#define BLE_HS_TEST_UTIL_MAX_PREV_HCI_TXES      64
static uint8_t
ble_hs_test_util_prev_hci_tx[BLE_HS_TEST_UTIL_MAX_PREV_HCI_TXES][260];
int ble_hs_test_util_num_prev_hci_txes;

uint8_t ble_hs_test_util_cur_hci_tx[260];

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
                      BLE_HS_TEST_UTIL_MAX_PREV_HCI_TXES);
    memcpy(ble_hs_test_util_prev_hci_tx + ble_hs_test_util_num_prev_hci_txes,
           cmd, 260);

    ble_hs_test_util_num_prev_hci_txes++;
}

void
ble_hs_test_util_prev_hci_tx_clear(void)
{
    ble_hs_test_util_num_prev_hci_txes = 0;
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
    uint8_t *evt_params;
    uint8_t evt_params_len;
};

static struct ble_hs_test_util_phony_ack
ble_hs_test_util_phony_acks[BLE_HS_TEST_UTIL_PHONY_ACK_MAX];
static int ble_hs_test_util_num_phony_acks;

static int
ble_hs_test_util_phony_ack_cb(void *cmd, uint8_t *ack, int ack_buf_len)
{
    struct ble_hs_test_util_phony_ack *entry;

    if (ble_hs_test_util_num_phony_acks == 0) {
        return BLE_HS_ETIMEOUT;
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
ble_hs_test_util_set_ack(uint16_t opcode, uint8_t status)
{
    ble_hs_test_util_phony_acks[0].opcode = opcode;
    ble_hs_test_util_phony_acks[0].status = status;
    ble_hs_test_util_num_phony_acks = 1;

    ble_hci_block_set_phony_ack_cb(ble_hs_test_util_phony_ack_cb);
}

static void
ble_hs_test_util_set_ack_seq(struct ble_hs_test_util_phony_ack *acks)
{
    int i;

    for (i = 0; acks[i].opcode != 0; i++) {
        ble_hs_test_util_phony_acks[i] = acks[i];
    }
    ble_hs_test_util_num_phony_acks = i;

    ble_hci_block_set_phony_ack_cb(ble_hs_test_util_phony_ack_cb);
}

struct ble_hs_conn *
ble_hs_test_util_create_conn(uint16_t handle, uint8_t *addr,
                             ble_gap_conn_fn *cb, void *cb_arg)
{
    struct hci_le_conn_complete evt;
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_test_util_set_ack(
        BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_CREATE_CONN), 0);
    rc = ble_gap_conn_initiate(0, addr, NULL, cb, cb_arg);
    TEST_ASSERT(rc == 0);

    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = handle;
    evt.role = BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER;
    evt.peer_addr_type = BLE_ADDR_TYPE_PUBLIC;
    memcpy(evt.peer_addr, addr, 6);
    evt.conn_itvl = BLE_GAP_INITIAL_CONN_ITVL_MAX;
    evt.conn_latency = BLE_GAP_INITIAL_CONN_LATENCY;
    evt.supervision_timeout = BLE_GAP_INITIAL_SUPERVISION_TIMEOUT;
    rc = ble_gap_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);

    conn = ble_hs_conn_find(handle);
    TEST_ASSERT_FATAL(conn != NULL);

    ble_hs_test_util_prev_hci_tx_clear();

    return conn;
}

int
ble_hs_test_util_conn_initiate(int addr_type, uint8_t *addr,
                               struct ble_gap_crt_params *params,
                               ble_gap_conn_fn *cb, void *cb_arg,
                               uint8_t ack_status)
{
    int rc;

    ble_hs_test_util_set_ack(
        host_hci_opcode_join(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_CREATE_CONN),
        ack_status);

    rc = ble_gap_conn_initiate(addr_type, addr, params, cb, cb_arg);
    return rc;
}

int
ble_hs_test_util_conn_cancel(uint8_t ack_status)
{
    int rc;

    ble_hs_test_util_set_ack(
        host_hci_opcode_join(BLE_HCI_OGF_LE,
                             BLE_HCI_OCF_LE_CREATE_CONN_CANCEL),
        ack_status);

    rc = ble_gap_cancel();
    return rc;
}

int
ble_hs_test_util_conn_terminate(uint16_t conn_handle, uint8_t hci_status)
{
    int rc;

    ble_hs_test_util_set_ack(
        host_hci_opcode_join(BLE_HCI_OGF_LINK_CTRL,
                             BLE_HCI_OCF_DISCONNECT_CMD),
        hci_status);

    rc = ble_gap_terminate(conn_handle);
    return rc;
}

int
ble_hs_test_util_disc(uint32_t duration_ms, uint8_t discovery_mode,
                      uint8_t scan_type, uint8_t filter_policy,
                      ble_gap_disc_fn *cb, void *cb_arg, int fail_idx,
                      uint8_t fail_status)
{
    int rc;

    ble_hs_test_util_set_ack_seq(((struct ble_hs_test_util_phony_ack[]) {
        {
            BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_SET_SCAN_PARAMS),
            fail_idx == 0 ? fail_status : 0,
        },
        {
            BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_SET_SCAN_ENABLE),
            fail_idx == 1 ? fail_status : 0,
        },

        { 0 }
    }));

    rc = ble_gap_disc(duration_ms, discovery_mode, scan_type, filter_policy,
                      cb, cb_arg);
    return rc;
}

int
ble_hs_test_util_adv_start(uint8_t discoverable_mode,
                           uint8_t connectable_mode,
                           uint8_t *peer_addr, uint8_t peer_addr_type,
                           struct hci_adv_params *adv_params,
                           ble_gap_conn_fn *cb, void *cb_arg,
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

    if (connectable_mode != BLE_GAP_CONN_MODE_DIR) {
        acks[i] = (struct ble_hs_test_util_phony_ack) {
            BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_RD_ADV_CHAN_TXPWR),
            fail_idx == i ? fail_status : 0,
            (uint8_t[]) { 0 },
            1,
        };
        i++;

        acks[i] = (struct ble_hs_test_util_phony_ack) {
            BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_SET_ADV_DATA),
            fail_idx == i ? fail_status : 0,
        };
        i++;

        acks[i] = (struct ble_hs_test_util_phony_ack) {
            BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_SET_SCAN_RSP_DATA),
            fail_idx == i ? fail_status : 0,
        };
        i++;
    }

    acks[i] = (struct ble_hs_test_util_phony_ack) {
        BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_SET_ADV_ENABLE),
        fail_idx == i ? fail_status : 0,
    };
    i++;

    memset(acks + i, 0, sizeof acks[i]);

    ble_hs_test_util_set_ack_seq(acks);
    rc = ble_gap_adv_start(discoverable_mode, connectable_mode, peer_addr,
                           peer_addr_type, adv_params, cb, cb_arg);

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
        fail_idx == cmd_idx ? fail_status : 0,
    };
    cmd_idx++;

    for (i = 0; i < white_list_count; i++) {
        acks[cmd_idx] = (struct ble_hs_test_util_phony_ack) {
            BLE_HS_TEST_UTIL_LE_OPCODE(BLE_HCI_OCF_LE_ADD_WHITE_LIST),
            fail_idx == cmd_idx ? fail_status : 0,
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

void
ble_hs_test_util_rx_ack_param(uint16_t opcode, uint8_t status, void *param,
                              int param_len)
{
    uint8_t buf[256];
    int rc;

    ble_hs_test_util_build_cmd_complete(buf, sizeof buf, param_len + 1, 1,
                                        opcode);
    buf[BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN] = status;
    memcpy(buf + BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN + 1, param, param_len);

    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == 0);
}

void
ble_hs_test_util_rx_ack(uint16_t opcode, uint8_t status)
{
    uint8_t buf[BLE_HCI_EVENT_CMD_STATUS_LEN];
    int rc;

    ble_hs_test_util_build_cmd_status(buf, sizeof buf, status, 1, opcode);
    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == 0);
}

void
ble_hs_test_util_rx_hci_buf_size_ack(uint16_t buf_size)
{
    uint8_t buf[BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN +
                BLE_HCI_RD_BUF_SIZE_RSPLEN + 1];
    int rc;

    ble_hs_test_util_build_cmd_complete(buf, sizeof buf,
                                        BLE_HCI_RD_BUF_SIZE_RSPLEN + 1, 1,
                                        (BLE_HCI_OGF_LE << 10) |
                                            BLE_HCI_OCF_LE_RD_BUF_SIZE);

    buf[BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN + 0] = 0;
    htole16(buf + BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN + 1, buf_size);
    buf[BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN + 3] = 1;

    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == 0);
}

void
ble_hs_test_util_rx_le_ack_param(uint16_t ocf, uint8_t status, void *param,
                                 int param_len)
{
    ble_hs_test_util_rx_ack_param((BLE_HCI_OGF_LE << 10) | ocf, status, param,
                                  param_len);
}

void
ble_hs_test_util_rx_le_ack(uint16_t ocf, uint8_t status)
{
    ble_hs_test_util_rx_ack((BLE_HCI_OGF_LE << 10) | ocf, status);
}

int
ble_hs_test_util_l2cap_rx_first_frag(struct ble_hs_conn *conn, uint16_t cid,
                                     struct hci_data_hdr *hci_hdr,
                                     struct os_mbuf *om)
{
    int rc;

    om = ble_l2cap_prepend_hdr(om, cid, OS_MBUF_PKTLEN(om));
    TEST_ASSERT_FATAL(om != NULL);

    rc = ble_hs_test_util_l2cap_rx(conn, hci_hdr, om);
    return rc;
}

int
ble_hs_test_util_l2cap_rx(struct ble_hs_conn *conn,
                          struct hci_data_hdr *hci_hdr,
                          struct os_mbuf *om)
{
    ble_l2cap_rx_fn *rx_cb;
    struct os_mbuf *rx_buf;
    int rc;

    rc = ble_l2cap_rx(conn, hci_hdr, om, &rx_cb, &rx_buf);
    if (rc == 0) {
        TEST_ASSERT_FATAL(rx_cb != NULL);
        TEST_ASSERT_FATAL(rx_buf != NULL);
        rc = rx_cb(conn->bhc_handle, &rx_buf);
        os_mbuf_free_chain(rx_buf);
    } else if (rc == BLE_HS_EAGAIN) {
        /* More fragments on the way. */
        rc = 0;
    }

    return rc;
}

int
ble_hs_test_util_l2cap_rx_payload_flat(struct ble_hs_conn *conn,
                                       struct ble_l2cap_chan *chan,
                                       const void *data, int len)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om;
    int rc;

    om = ble_hs_misc_pkthdr();
    TEST_ASSERT_FATAL(om != NULL);

    rc = os_mbuf_append(om, data, len);
    TEST_ASSERT_FATAL(rc == 0);

    hci_hdr.hdh_handle_pb_bc =
        host_hci_handle_pb_bc_join(conn->bhc_handle,
                                   BLE_HCI_PB_FIRST_FLUSH, 0);
    hci_hdr.hdh_len = OS_MBUF_PKTHDR(om)->omp_len;

    rc = ble_hs_test_util_l2cap_rx_first_frag(conn, chan->blc_cid, &hci_hdr,
                                              om);
    return rc;
}

void
ble_hs_test_util_rx_att_err_rsp(struct ble_hs_conn *conn, uint8_t req_op,
                                uint8_t error_code, uint16_t err_handle)
{
    struct ble_att_error_rsp rsp;
    struct ble_l2cap_chan *chan;
    uint8_t buf[BLE_ATT_ERROR_RSP_SZ];
    int rc;

    rsp.baep_req_op = req_op;
    rsp.baep_handle = err_handle;
    rsp.baep_error_code = error_code;

    ble_att_error_rsp_write(buf, sizeof buf, &rsp);

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    TEST_ASSERT_FATAL(chan != NULL);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn, chan, buf, sizeof buf);
    TEST_ASSERT(rc == 0);
}

void
ble_hs_test_util_rx_startup_acks(void)
{
    uint8_t supp_feat[8];

    memset(supp_feat, 0, sizeof supp_feat);

    /* Receive acknowledgements for the startup sequence.  We sent the
     * corresponding requests when the host task was started.
     */
    ble_hs_test_util_rx_ack(
        (BLE_HCI_OGF_CTLR_BASEBAND << 10) | BLE_HCI_OCF_CB_RESET, 0);
    ble_hs_test_util_rx_ack(
        (BLE_HCI_OGF_CTLR_BASEBAND << 10) | BLE_HCI_OCF_CB_SET_EVENT_MASK,
        0);
    ble_hs_test_util_rx_le_ack(BLE_HCI_OCF_LE_SET_EVENT_MASK, 0);
    ble_hs_test_util_rx_hci_buf_size_ack(0xffff);
    ble_hs_test_util_rx_le_ack_param(BLE_HCI_OCF_LE_RD_LOC_SUPP_FEAT, 0,
                                     supp_feat, sizeof supp_feat);
}

void
ble_hs_test_util_rx_num_completed_pkts_event(
    struct ble_hs_test_util_num_completed_pkts_entry *entries)
{
    struct ble_hs_test_util_num_completed_pkts_entry *entry;
    uint8_t buf[1024];
    int num_entries;
    int off;
    int rc;
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

    rc = host_hci_event_rx(buf);
    TEST_ASSERT(rc == 0);
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
    ble_gattc_wakeup();
    ble_l2cap_sig_wakeup();
    ble_l2cap_sm_wakeup();
    ble_hs_process_tx_data_queue();
    ble_hci_sched_wakeup();
}

void
ble_hs_test_util_set_public_addr(uint8_t *addr)
{
    memcpy(ble_hs_our_dev.public_addr, addr, 6);
}

void
ble_hs_test_util_init(void)
{
    struct ble_hs_cfg cfg;
    int rc;

    tu_init();

    os_msys_reset();
    stats_module_reset();

    cfg = ble_hs_cfg_dflt;
    cfg.max_connections = 8;
    rc = ble_hs_init(10, &cfg);
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

    ble_hci_block_set_phony_ack_cb(NULL);

    /* Don't limit a connection's ability to transmit; simplify tests. */
    ble_hs_cfg.max_outstanding_pkts_per_conn = 0;

    ble_hs_test_util_prev_tx = NULL;
    ble_hs_test_util_prev_hci_tx_clear();
}
