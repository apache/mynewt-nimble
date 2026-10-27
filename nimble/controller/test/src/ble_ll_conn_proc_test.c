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
#include <nimble/hci_common.h>
#include <testutil/testutil.h>
#include <controller/ble_ll.h>
#include <controller/ble_ll_conn.h>
#include <controller/ble_ll_ctrl.h>
#include <controller/ble_ll_sched.h>
#include <controller/ble_ll_tmr.h>
#include "ble_ll_conn_priv.h"

#if MYNEWT_VAL(BLE_LL_ROLE_PERIPHERAL)

#define TEST_CONN_ITVL    (0x0028)
#define TEST_CONN_TIMEOUT (0x0100)
#define TEST_EVENT_CNTR   (10)

static struct ble_ll_conn_sm *
test_periph_conn_create(void)
{
    struct ble_ll_conn_sm *connsm;

    connsm = ble_ll_conn_sm_get();
    TEST_ASSERT_FATAL(connsm != NULL);

    connsm->conn_role = BLE_LL_CONN_ROLE_PERIPHERAL;
    ble_ll_conn_sm_new(connsm);

    connsm->conn_state = BLE_LL_CONN_STATE_ESTABLISHED;
    connsm->conn_features |= BLE_LL_FEAT_CONN_PARM_REQ;
    connsm->conn_itvl = TEST_CONN_ITVL;
    ble_ll_conn_itvl_to_ticks(connsm->conn_itvl, &connsm->conn_itvl_ticks,
                              &connsm->conn_itvl_usecs);
    connsm->periph_latency = 0;
    connsm->supervision_tmo = TEST_CONN_TIMEOUT;
    connsm->event_cntr = TEST_EVENT_CNTR;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_ENHANCED_CONN_UPDATE)
    connsm->subrate_base_event = TEST_EVENT_CNTR;
#endif
    memset(connsm->chan_map, 0xff, BLE_LL_CHAN_MAP_LEN - 1);
    connsm->chan_map[BLE_LL_CHAN_MAP_LEN - 1] = 0x1f;
    connsm->chan_map_used = BLE_PHY_NUM_DATA_CHANS;
    connsm->hop_inc = 5;
    connsm->anchor_point = ble_ll_tmr_get();
    connsm->anchor_point_usecs = 0;
    connsm->last_rxd_pdu_cputime = connsm->anchor_point;

    return connsm;
}

static void
test_rx_ctrl_pdu(struct ble_ll_conn_sm *connsm, uint8_t opcode,
                 const uint8_t *data, uint8_t len)
{
    struct os_mbuf *om;
    uint8_t *dptr;

    om = os_msys_get_pkthdr(BLE_LL_CTRL_MAX_PDU_LEN, sizeof(struct ble_mbuf_hdr));
    TEST_ASSERT_FATAL(om != NULL);

    dptr = om->om_data;
    dptr[0] = BLE_LL_LLID_CTRL;
    dptr[1] = len + 1;
    dptr[2] = opcode;
    memcpy(&dptr[3], data, len);
    om->om_len = BLE_LL_PDU_HDR_LEN + 1 + len;
    OS_MBUF_PKTHDR(om)->omp_len = om->om_len;

    ble_ll_ctrl_rx_pdu(connsm, om);
}

static void
test_rx_conn_update_ind(struct ble_ll_conn_sm *connsm, uint16_t interval, uint16_t instant)
{
    uint8_t data[BLE_LL_CTRL_CONN_UPD_REQ_LEN];

    data[0] = 1;           /* WinSize */
    put_le16(&data[1], 0); /* WinOffset */
    put_le16(&data[3], interval);
    put_le16(&data[5], 0); /* Latency */
    put_le16(&data[7], TEST_CONN_TIMEOUT);
    put_le16(&data[9], instant);

    test_rx_ctrl_pdu(connsm, BLE_LL_CTRL_CONN_UPDATE_IND, data, sizeof(data));
}

static void
test_rx_chan_map_ind(struct ble_ll_conn_sm *connsm, uint16_t instant)
{
    uint8_t data[BLE_LL_CTRL_CHAN_MAP_LEN];

    memset(data, 0xff, BLE_LL_CHAN_MAP_LEN - 1);
    data[BLE_LL_CHAN_MAP_LEN - 1] = 0x1e;
    put_le16(&data[BLE_LL_CHAN_MAP_LEN], instant);

    test_rx_ctrl_pdu(connsm, BLE_LL_CTRL_CHANNEL_MAP_REQ, data, sizeof(data));
}

static void
test_rx_reject_ext_ind(struct ble_ll_conn_sm *connsm, uint8_t rej_opcode, uint8_t err)
{
    uint8_t data[BLE_LL_CTRL_REJECT_IND_EXT_LEN];

    data[0] = rej_opcode;
    data[1] = err;

    test_rx_ctrl_pdu(connsm, BLE_LL_CTRL_REJECT_IND_EXT, data, sizeof(data));
}

static int
test_hci_conn_update(struct ble_ll_conn_sm *connsm, uint16_t interval)
{
    struct ble_hci_le_conn_update_cp cmd = {
        .conn_handle = htole16(connsm->conn_handle),
        .conn_itvl_min = htole16(interval),
        .conn_itvl_max = htole16(interval),
        .conn_latency = htole16(0),
        .supervision_timeout = htole16(TEST_CONN_TIMEOUT),
        .min_ce_len = htole16(0),
        .max_ce_len = htole16(0),
    };

    return ble_ll_conn_hci_update((uint8_t *)&cmd, sizeof(cmd));
}

static void
test_conn_event_end(struct ble_ll_conn_sm *connsm)
{
    ble_npl_event_run(&connsm->conn_ev_end);

    /* Next connection event is scheduled, drop it as it is not run here */
    ble_ll_sched_rmv_elem(&connsm->conn_sch);
}

/*
 * Peripheral host requests new connection parameters while a channel map
 * update is pending. The request is sent once the instant has passed.
 */
TEST_CASE_SELF(test_ll_conn_param_req_deferred_by_chan_map)
{
    struct ble_ll_conn_sm *connsm;
    int rc;

    connsm = test_periph_conn_create();

    test_rx_chan_map_ind(connsm, TEST_EVENT_CNTR + 1);
    TEST_ASSERT_FATAL(connsm->flags.chanmap_update_sched);

    rc = test_hci_conn_update(connsm, 0x0050);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_IDLE);
    TEST_ASSERT(IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CONN_PARAM_REQ));

    /* End of connection event, next one is at the instant */
    test_conn_event_end(connsm);
    TEST_ASSERT(!connsm->flags.chanmap_update_sched);
    TEST_ASSERT(connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_CONN_PARAM_REQ);

    ble_ll_conn_end(connsm, BLE_ERR_CONN_TERM_LOCAL);
}

/*
 * Peripheral host requests new connection parameters while a connection
 * update initiated by the central is pending. The request is rejected until
 * completion of the central's update was reported, so that the host does
 * not take it as completion of its request.
 */
TEST_CASE_SELF(test_ll_conn_param_req_rejected_by_conn_update)
{
    struct ble_ll_conn_sm *connsm;
    int rc;

    connsm = test_periph_conn_create();

    test_rx_conn_update_ind(connsm, 0x0030, TEST_EVENT_CNTR + 1);
    TEST_ASSERT_FATAL(connsm->flags.conn_update_sched);

    rc = test_hci_conn_update(connsm, 0x0050);
    TEST_ASSERT(rc == BLE_ERR_LMP_COLLISION);

    /* End of connection event, next one is at the instant */
    test_conn_event_end(connsm);
    TEST_ASSERT(connsm->conn_itvl == 0x0030);
    TEST_ASSERT(connsm->flags.conn_update_host_w4event);

    rc = test_hci_conn_update(connsm, 0x0050);
    TEST_ASSERT(rc == BLE_ERR_LMP_COLLISION);

    /* Completion of central's update is reported, request is accepted */
    test_conn_event_end(connsm);
    TEST_ASSERT(!connsm->flags.conn_update_host_w4event);

    rc = test_hci_conn_update(connsm, 0x0050);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_CONN_PARAM_REQ);

    ble_ll_conn_end(connsm, BLE_ERR_CONN_TERM_LOCAL);
}

/*
 * Central schedules a connection update while the peripheral's request waits
 * for a channel map update instant. The request fails when the central's
 * update is applied instead of being taken as completed by it.
 */
TEST_CASE_SELF(test_ll_conn_param_req_cancelled_by_conn_update)
{
    struct ble_ll_conn_sm *connsm;
    int rc;

    connsm = test_periph_conn_create();

    test_rx_chan_map_ind(connsm, TEST_EVENT_CNTR + 2);
    rc = test_hci_conn_update(connsm, 0x0050);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CONN_PARAM_REQ));

    test_rx_conn_update_ind(connsm, 0x0030, TEST_EVENT_CNTR + 1);

    /* Central's update is applied, deferred request fails */
    test_conn_event_end(connsm);
    TEST_ASSERT(connsm->conn_itvl == 0x0030);
    TEST_ASSERT(!IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CONN_PARAM_REQ));
    TEST_ASSERT(!connsm->flags.conn_update_host_initd);

    /* Channel map instant passes, nothing is started */
    test_conn_event_end(connsm);
    TEST_ASSERT(!connsm->flags.chanmap_update_sched);
    TEST_ASSERT(connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_IDLE);

    ble_ll_conn_end(connsm, BLE_ERR_CONN_TERM_LOCAL);
}

/*
 * Central rejects the request with Different Transaction Collision because
 * of its connection update. The request is not retried since completion of
 * the central's update is reported to the host.
 */
TEST_CASE_SELF(test_ll_conn_param_req_no_retry_on_conn_update)
{
    struct ble_ll_conn_sm *connsm;
    int rc;

    connsm = test_periph_conn_create();

    rc = test_hci_conn_update(connsm, 0x0050);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT_FATAL(connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_CONN_PARAM_REQ);

    test_rx_conn_update_ind(connsm, 0x0030, TEST_EVENT_CNTR + 1);
    test_rx_reject_ext_ind(connsm, BLE_LL_CTRL_CONN_PARM_REQ, BLE_ERR_DIFF_TRANS_COLL);
    TEST_ASSERT(connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_IDLE);
    TEST_ASSERT(!IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CONN_PARAM_REQ));

    ble_ll_conn_end(connsm, BLE_ERR_CONN_TERM_LOCAL);
}

/*
 * Central keeps rejecting the connection parameters request with Different
 * Transaction Collision because of its channel map updates. The request is
 * retried after each instant and fails after the retries are exhausted.
 */
TEST_CASE_SELF(test_ll_conn_param_req_retry_limit)
{
    struct ble_ll_conn_sm *connsm;
    int rc;
    int i;

    connsm = test_periph_conn_create();

    rc = test_hci_conn_update(connsm, 0x0050);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT_FATAL(connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_CONN_PARAM_REQ);

    for (i = 0; i < BLE_LL_CTRL_CONN_PARAM_REQ_RETRIES_MAX; i++) {
        test_rx_chan_map_ind(connsm, connsm->event_cntr + 1);
        test_rx_reject_ext_ind(connsm, BLE_LL_CTRL_CONN_PARM_REQ,
                               BLE_ERR_DIFF_TRANS_COLL);
        TEST_ASSERT(connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_IDLE);
        TEST_ASSERT(IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CONN_PARAM_REQ));

        /* Request is sent again once the instant has passed */
        test_conn_event_end(connsm);
        TEST_ASSERT(connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_CONN_PARAM_REQ);
    }

    test_rx_chan_map_ind(connsm, connsm->event_cntr + 1);
    test_rx_reject_ext_ind(connsm, BLE_LL_CTRL_CONN_PARM_REQ, BLE_ERR_DIFF_TRANS_COLL);
    TEST_ASSERT(connsm->cur_ctrl_proc == BLE_LL_CTRL_PROC_IDLE);
    TEST_ASSERT(!IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CONN_PARAM_REQ));

    ble_ll_conn_end(connsm, BLE_ERR_CONN_TERM_LOCAL);
}
#endif

TEST_SUITE(ble_ll_conn_proc_test_suite)
{
#if MYNEWT_VAL(BLE_LL_ROLE_PERIPHERAL)
    test_ll_conn_param_req_deferred_by_chan_map();
    test_ll_conn_param_req_rejected_by_conn_update();
    test_ll_conn_param_req_cancelled_by_conn_update();
    test_ll_conn_param_req_no_retry_on_conn_update();
    test_ll_conn_param_req_retry_limit();
#endif
}
