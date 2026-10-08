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
#include <syscfg/syscfg.h>
#include <testutil/testutil.h>
#include <controller/ble_ll_conn.h>
#include <controller/ble_ll_ctrl.h>
#include "ble_ll_conn_priv.h"

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_PING)

/* 50 ms connection interval and 1 s authenticated payload timeout */
#define TEST_CONN_ITVL     (40)
#define TEST_AUTH_PYLD_TMO (100)

static struct ble_npl_eventq g_test_evq;
static struct ble_ll_conn_sm g_test_connsm;

static ble_npl_time_t
test_ms_to_ticks(uint32_t ms)
{
    return ble_npl_time_ms_to_ticks32(ms);
}

static void
test_le_ping_setup(struct ble_ll_conn_sm *connsm)
{
    memset(connsm, 0, sizeof(*connsm));

    ble_npl_eventq_init(&g_test_evq);
    ble_npl_callout_init(&connsm->auth_pyld_timer, &g_test_evq,
                         ble_ll_conn_auth_pyld_timer_cb, connsm);

    connsm->conn_itvl = TEST_CONN_ITVL;
    connsm->periph_latency = 0;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_ENHANCED_CONN_UPDATE)
    connsm->subrate_factor = 1;
#endif
    connsm->auth_pyld_tmo = TEST_AUTH_PYLD_TMO;
    connsm->enc_data.enc_state = CONN_ENC_S_ENCRYPTED;

    /* Pretend other procedure is active so that starting LE Ping only marks
     * it as pending and does not try to send anything.
     */
    connsm->cur_ctrl_proc = BLE_LL_CTRL_PROC_FEATURE_XCHG;
    connsm->pending_ctrl_procs = 0;
}

static void
test_le_ping_teardown(struct ble_ll_conn_sm *connsm)
{
    ble_npl_callout_stop(&connsm->auth_pyld_timer);
}

/* Runs timer callback with last packet with valid MIC received elapsed_ms
 * ago and returns ticks until timer fires again.
 */
static ble_npl_time_t
test_le_ping_run_cb(struct ble_ll_conn_sm *connsm, uint32_t elapsed_ms)
{
    struct ble_npl_event ev;
    ble_npl_time_t now;

    now = ble_npl_time_get();
    connsm->auth_pyld_rx_time = now - test_ms_to_ticks(elapsed_ms);

    ble_npl_event_init(&ev, ble_ll_conn_auth_pyld_timer_cb, connsm);
    ble_ll_conn_auth_pyld_timer_cb(&ev);

    TEST_ASSERT_FATAL(ble_npl_callout_is_active(&connsm->auth_pyld_timer));

    return ble_npl_callout_remaining_ticks(&connsm->auth_pyld_timer, now);
}

static void
test_le_ping_assert_ticks(ble_npl_time_t ticks, uint32_t expected_ms)
{
    ble_npl_time_t expected;

    expected = test_ms_to_ticks(expected_ms);

    /* allow 1 tick for rounding and time passing during test */
    TEST_ASSERT((ticks + 1 >= expected) && (ticks <= expected + 1),
                "ticks %u expected %u", (unsigned)ticks, (unsigned)expected);
}

TEST_CASE_SELF(test_ll_le_ping_delay)
{
    /* 3 events margin */
    TEST_ASSERT(ble_ll_conn_auth_pyld_ping_delay(30000, 50) == 29850);
    TEST_ASSERT(ble_ll_conn_auth_pyld_ping_delay(1000, 50) == 850);

    /* timeout is exactly twice the margin */
    TEST_ASSERT(ble_ll_conn_auth_pyld_ping_delay(300, 50) == 150);

    /* timeout too short for margin, ping after half of timeout */
    TEST_ASSERT(ble_ll_conn_auth_pyld_ping_delay(299, 50) == 149);
    TEST_ASSERT(ble_ll_conn_auth_pyld_ping_delay(100, 100) == 50);

    /* large values (max interval with subrating and latency) */
    TEST_ASSERT(ble_ll_conn_auth_pyld_ping_delay(655350, 2000000) == 327675);
}

TEST_CASE_SELF(test_ll_le_ping_timer_start)
{
    struct ble_ll_conn_sm *connsm = &g_test_connsm;
    ble_npl_time_t ticks;

    test_le_ping_setup(connsm);

    ble_ll_conn_auth_pyld_timer_start(connsm);
    TEST_ASSERT(ble_npl_callout_is_active(&connsm->auth_pyld_timer));
    ticks = ble_npl_callout_remaining_ticks(&connsm->auth_pyld_timer,
                                            ble_npl_time_get());

    /* 1000 ms timeout, 3 * 50 ms margin */
    test_le_ping_assert_ticks(ticks, 850);
    TEST_ASSERT(!IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_LE_PING));

    test_le_ping_teardown(connsm);
}

TEST_CASE_SELF(test_ll_le_ping_rx_before_ping)
{
    struct ble_ll_conn_sm *connsm = &g_test_connsm;
    ble_npl_time_t ticks;

    test_le_ping_setup(connsm);

    /* packet with valid MIC received meanwhile, re-arm for ping */
    ticks = test_le_ping_run_cb(connsm, 300);
    test_le_ping_assert_ticks(ticks, 550);
    TEST_ASSERT(!IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_LE_PING));

    test_le_ping_teardown(connsm);
}

TEST_CASE_SELF(test_ll_le_ping_before_timeout)
{
    struct ble_ll_conn_sm *connsm = &g_test_connsm;
    ble_npl_time_t ticks;

    test_le_ping_setup(connsm);

    /* idle link, ping is started and timer re-armed for timeout */
    ticks = test_le_ping_run_cb(connsm, 850);
    test_le_ping_assert_ticks(ticks, 150);
    TEST_ASSERT(IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_LE_PING));

    test_le_ping_teardown(connsm);
}

TEST_CASE_SELF(test_ll_le_ping_not_encrypted)
{
    struct ble_ll_conn_sm *connsm = &g_test_connsm;
    ble_npl_time_t ticks;

    test_le_ping_setup(connsm);

    /* encryption paused, no ping */
    connsm->enc_data.enc_state = CONN_ENC_S_PAUSED;
    ticks = test_le_ping_run_cb(connsm, 850);
    test_le_ping_assert_ticks(ticks, 150);
    TEST_ASSERT(!IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_LE_PING));

    test_le_ping_teardown(connsm);
}

TEST_CASE_SELF(test_ll_le_ping_timeout)
{
    struct ble_ll_conn_sm *connsm = &g_test_connsm;
    ble_npl_time_t ticks;
    ble_npl_time_t now;

    test_le_ping_setup(connsm);

    /* timeout expired, ping is started and timer restarted */
    now = ble_npl_time_get();
    ticks = test_le_ping_run_cb(connsm, 1000);
    test_le_ping_assert_ticks(ticks, 850);
    TEST_ASSERT(IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_LE_PING));
    TEST_ASSERT((ble_npl_stime_t)(connsm->auth_pyld_rx_time - now) >= 0);

    test_le_ping_teardown(connsm);
}

TEST_CASE_SELF(test_ll_le_ping_latency)
{
    struct ble_ll_conn_sm *connsm = &g_test_connsm;
    ble_npl_time_t ticks;

    test_le_ping_setup(connsm);

    /* 50 ms interval with latency 1 gives 100 ms between events */
    connsm->periph_latency = 1;
    ble_ll_conn_auth_pyld_timer_start(connsm);
    ticks = ble_npl_callout_remaining_ticks(&connsm->auth_pyld_timer,
                                            ble_npl_time_get());
    test_le_ping_assert_ticks(ticks, 700);

    /* latency 3 gives 200 ms between events, margin too big for 1 s
     * timeout so ping after half of timeout
     */
    connsm->periph_latency = 3;
    ble_ll_conn_auth_pyld_timer_start(connsm);
    ticks = ble_npl_callout_remaining_ticks(&connsm->auth_pyld_timer,
                                            ble_npl_time_get());
    test_le_ping_assert_ticks(ticks, 500);

    test_le_ping_teardown(connsm);
}

TEST_SUITE(ble_ll_le_ping_test_suite)
{
    test_ll_le_ping_delay();
    test_ll_le_ping_timer_start();
    test_ll_le_ping_rx_before_ping();
    test_ll_le_ping_before_timeout();
    test_ll_le_ping_not_encrypted();
    test_ll_le_ping_timeout();
    test_ll_le_ping_latency();
}

#else

TEST_SUITE(ble_ll_le_ping_test_suite)
{
}

#endif
