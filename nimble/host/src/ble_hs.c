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

#include <assert.h>
#include <errno.h>
#include <string.h>
#include "sysinit/sysinit.h"
#include "syscfg/syscfg.h"
#include "bsp/bsp.h"
#include "stats/stats.h"
#include "os/os.h"
#include "console/console.h"
#include "nimble/ble_hci_trans.h"
#include "ble_hs_priv.h"
#include "ble_monitor_priv.h"

#define BLE_HS_HCI_EVT_COUNT                    \
    (MYNEWT_VAL(BLE_HCI_EVT_HI_BUF_COUNT) +     \
     MYNEWT_VAL(BLE_HCI_EVT_LO_BUF_COUNT))

static void ble_hs_event_rx_hci_ev(struct os_event *ev);
static void ble_hs_event_tx_notify(struct os_event *ev);
static void ble_hs_event_reset(struct os_event *ev);
static void ble_hs_event_start(struct os_event *ev);
static void ble_hs_timer_sched(int32_t ticks_from_now);

struct os_mempool ble_hs_hci_ev_pool;
static os_membuf_t ble_hs_hci_os_event_buf[
    OS_MEMPOOL_SIZE(BLE_HS_HCI_EVT_COUNT, sizeof (struct os_event))
];

/** OS event - triggers tx of pending notifications and indications. */
static struct os_event ble_hs_ev_tx_notifications = {
    .ev_cb = ble_hs_event_tx_notify,
};

/** OS event - triggers a full reset. */
static struct os_event ble_hs_ev_reset = {
    .ev_cb = ble_hs_event_reset,
};

static struct os_event ble_hs_ev_start = {
    .ev_cb = ble_hs_event_start,
};

uint8_t ble_hs_sync_state;
static int ble_hs_reset_reason;

#define BLE_HS_SYNC_RETRY_RATE          (OS_TICKS_PER_SEC / 10)

static struct os_task *ble_hs_parent_task;

/**
 * Handles unresponsive timeouts and periodic retries in case of resource
 * shortage.
 */
static struct os_callout ble_hs_timer_timer;

/* Shared queue that the host uses for work items. */
static struct os_eventq *ble_hs_evq;

static struct os_mqueue ble_hs_rx_q;

static struct os_mutex ble_hs_mutex;

/** These values keep track of required ATT and GATT resources counts.  They
 * increase as services are added, and are read when the ATT server and GATT
 * server are started.
 */
uint16_t ble_hs_max_attrs;
uint16_t ble_hs_max_services;
uint16_t ble_hs_max_client_configs;

#if MYNEWT_VAL(BLE_HS_DEBUG)
static uint8_t ble_hs_dbg_mutex_locked;
#endif

STATS_SECT_DECL(ble_hs_stats) ble_hs_stats;
STATS_NAME_START(ble_hs_stats)
    STATS_NAME(ble_hs_stats, conn_create)
    STATS_NAME(ble_hs_stats, conn_delete)
    STATS_NAME(ble_hs_stats, hci_cmd)
    STATS_NAME(ble_hs_stats, hci_event)
    STATS_NAME(ble_hs_stats, hci_invalid_ack)
    STATS_NAME(ble_hs_stats, hci_unknown_event)
    STATS_NAME(ble_hs_stats, hci_timeout)
    STATS_NAME(ble_hs_stats, reset)
    STATS_NAME(ble_hs_stats, sync)
    STATS_NAME(ble_hs_stats, pvcy_add_entry)
    STATS_NAME(ble_hs_stats, pvcy_add_entry_fail)
STATS_NAME_END(ble_hs_stats)

struct os_eventq *
ble_hs_evq_get(void)
{
    return ble_hs_evq;
}

/**
 * Designates the specified event queue for NimBLE host work.  By default, the
 * host uses the default event queue and runs in the main task.  This function
 * is useful if you want the host to run in a different task.
 *
 * @param evq                   The event queue to use for host work.
 */
void
ble_hs_evq_set(struct os_eventq *evq)
{
    ble_hs_evq = evq;
}

int
ble_hs_locked_by_cur_task(void)
{
    struct os_task *owner;

#if MYNEWT_VAL(BLE_HS_DEBUG)
    if (!os_started()) {
        return ble_hs_dbg_mutex_locked;
    }
#endif

    owner = ble_hs_mutex.mu_owner;
    return owner != NULL && owner == os_sched_get_current_task();
}

/**
 * Indicates whether the host's parent task is currently running.
 */
int
ble_hs_is_parent_task(void)
{
    return !os_started() ||
           os_sched_get_current_task() == ble_hs_parent_task;
}

void
ble_hs_lock(void)
{
    int rc;

    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());

#if MYNEWT_VAL(BLE_HS_DEBUG)
    if (!os_started()) {
        ble_hs_dbg_mutex_locked = 1;
        return;
    }
#endif

    rc = os_mutex_pend(&ble_hs_mutex, 0xffffffff);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0 || rc == OS_NOT_STARTED);
}

void
ble_hs_unlock(void)
{
    int rc;

#if MYNEWT_VAL(BLE_HS_DEBUG)
    if (!os_started()) {
        BLE_HS_DBG_ASSERT(ble_hs_dbg_mutex_locked);
        ble_hs_dbg_mutex_locked = 0;
        return;
    }
#endif

    rc = os_mutex_release(&ble_hs_mutex);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0 || rc == OS_NOT_STARTED);
}

void
ble_hs_process_rx_data_queue(void)
{
    struct os_mbuf *om;

    while ((om = os_mqueue_get(&ble_hs_rx_q)) != NULL) {
#if BLE_MONITOR
        ble_monitor_send_om(BLE_MONITOR_OPCODE_ACL_RX_PKT, om);
#endif

        ble_hs_hci_evt_acl_process(om);
    }
}

static int
ble_hs_wakeup_tx_conn(struct ble_hs_conn *conn)
{
    struct os_mbuf_pkthdr *omp;
    struct os_mbuf *om;
    int rc;

    while ((omp = STAILQ_FIRST(&conn->bhc_tx_q)) != NULL) {
        STAILQ_REMOVE_HEAD(&conn->bhc_tx_q, omp_next);

        om = OS_MBUF_PKTHDR_TO_MBUF(omp);
        rc = ble_hs_hci_acl_tx_now(conn, &om);
        if (rc == BLE_HS_EAGAIN) {
            /* Controller is at capacity.  This packet will be the first to
             * get transmitted next time around.
             */
            STAILQ_INSERT_HEAD(&conn->bhc_tx_q, OS_MBUF_PKTHDR(om),
                               omp_next);
            return BLE_HS_EAGAIN;
        }
    }

    return 0;
}

/**
 * Schedules the transmission of all queued ACL data packets to the controller.
 */
void
ble_hs_wakeup_tx(void)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_lock();

    /* If there is a connection with a partially transmitted packet, it has to
     * be serviced first.  The controller is waiting for the remainder so it
     * can reassemble it.
     */
    for (conn = ble_hs_conn_first();
         conn != NULL;
         conn = SLIST_NEXT(conn, bhc_next)) {

        if (conn->bhc_flags && BLE_HS_CONN_F_TX_FRAG) {
            rc = ble_hs_wakeup_tx_conn(conn);
            if (rc != 0) {
                goto done;
            }
            break;
        }
    }

    /* For each connection, transmit queued packets until there are no more
     * packets to send or the controller's buffers are exhausted.
     */
    for (conn = ble_hs_conn_first();
         conn != NULL;
         conn = SLIST_NEXT(conn, bhc_next)) {

        rc = ble_hs_wakeup_tx_conn(conn);
        if (rc != 0) {
            goto done;
        }
    }

done:
    ble_hs_unlock();
}

static void
ble_hs_clear_rx_queue(void)
{
    struct os_mbuf *om;

    while ((om = os_mqueue_get(&ble_hs_rx_q)) != NULL) {
        os_mbuf_free_chain(om);
    }
}

/**
 * Indicates whether the host has synchronized with the controller.
 * Synchronization must occur before any host procedures can be performed.
 *
 * @return                      1 if the host and controller are in sync;
 *                              0 if the host and controller our out of sync.
 */
int
ble_hs_synced(void)
{
    return ble_hs_sync_state == BLE_HS_SYNC_STATE_GOOD;
}

static int
ble_hs_sync(void)
{
    int rc;

    /* Set the sync state to "bringup."  This allows the parent task to send
     * the startup sequence to the controller.  No other tasks are allowed to
     * send any commands.
     */
    ble_hs_sync_state = BLE_HS_SYNC_STATE_BRINGUP;

    rc = ble_hs_startup_go();
    if (rc == 0) {
        ble_hs_sync_state = BLE_HS_SYNC_STATE_GOOD;
        if (ble_hs_cfg.sync_cb != NULL) {
            ble_hs_cfg.sync_cb();
        }
    } else {
        ble_hs_sync_state = BLE_HS_SYNC_STATE_BAD;
    }

    ble_hs_timer_sched(BLE_HS_SYNC_RETRY_RATE);

    if (rc == 0) {
        STATS_INC(ble_hs_stats, sync);
    }

    return rc;
}

static int
ble_hs_reset(void)
{
    uint16_t conn_handle;
    int rc;

    STATS_INC(ble_hs_stats, reset);

    ble_hs_sync_state = 0;

    /* Reset transport.  Assume success; there is nothing we can do in case of
     * failure.  If the transport failed to reset, the host will reset itself
     * again when it fails to sync with the controller.
     */
    (void)ble_hci_trans_reset();

    ble_hs_clear_rx_queue();

    while (1) {
        conn_handle = ble_hs_atomic_first_conn_handle();
        if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            break;
        }

        ble_gap_conn_broken(conn_handle, ble_hs_reset_reason);
    }

    if (ble_hs_cfg.reset_cb != NULL && ble_hs_reset_reason != 0) {
        ble_hs_cfg.reset_cb(ble_hs_reset_reason);
    }
    ble_hs_reset_reason = 0;

    rc = ble_hs_sync();
    return rc;
}

/**
 * Called when the host timer expires.  Handles unresponsive timeouts and
 * periodic retries in case of resource shortage.
 */
static void
ble_hs_timer_exp(struct os_event *ev)
{
    int32_t ticks_until_next;

    if (!ble_hs_sync_state) {
        ble_hs_reset();
        return;
    }

    ticks_until_next = ble_gattc_timer();
    ble_hs_timer_sched(ticks_until_next);

    ticks_until_next = ble_gap_timer();
    ble_hs_timer_sched(ticks_until_next);

    ticks_until_next = ble_l2cap_sig_timer();
    ble_hs_timer_sched(ticks_until_next);

    ticks_until_next = ble_sm_timer();
    ble_hs_timer_sched(ticks_until_next);

    ticks_until_next = ble_hs_conn_timer();
    ble_hs_timer_sched(ticks_until_next);
}

static void
ble_hs_timer_reset(uint32_t ticks)
{
    int rc;

    rc = os_callout_reset(&ble_hs_timer_timer, ticks);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);
}

static void
ble_hs_timer_sched(int32_t ticks_from_now)
{
    os_time_t abs_time;

    if (ticks_from_now == BLE_HS_FOREVER) {
        return;
    }

    /* Reset timer if it is not currently scheduled or if the specified time is
     * sooner than the previous expiration time.
     */
    abs_time = os_time_get() + ticks_from_now;
    if (!os_callout_queued(&ble_hs_timer_timer) ||
        OS_TIME_TICK_LT(abs_time, ble_hs_timer_timer.c_ticks)) {

        ble_hs_timer_reset(ticks_from_now);
    }
}

void
ble_hs_timer_resched(void)
{
    /* Reschedule the timer to run immediately.  The timer callback will query
     * each module for an up-to-date expiration time.
     */
    ble_hs_timer_reset(0);
}

static void
ble_hs_event_rx_hci_ev(struct os_event *ev)
{
    uint8_t *hci_evt;
    int rc;

    hci_evt = ev->ev_arg;
    rc = os_memblock_put(&ble_hs_hci_ev_pool, ev);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

#if BLE_MONITOR
    ble_monitor_send(BLE_MONITOR_OPCODE_EVENT_PKT, hci_evt,
                     hci_evt[1] + BLE_HCI_EVENT_HDR_LEN);
#endif

    ble_hs_hci_evt_process(hci_evt);
}

static void
ble_hs_event_tx_notify(struct os_event *ev)
{
    ble_gatts_tx_notifications();
}

static void
ble_hs_event_rx_data(struct os_event *ev)
{
    ble_hs_process_rx_data_queue();
}

static void
ble_hs_event_reset(struct os_event *ev)
{
    ble_hs_reset();
}

static void
ble_hs_event_start(struct os_event *ev)
{
    int rc;

    rc = ble_hs_start();
    assert(rc == 0);
}

void
ble_hs_enqueue_hci_event(uint8_t *hci_evt)
{
    struct os_event *ev;

    ev = os_memblock_get(&ble_hs_hci_ev_pool);
    if (ev == NULL) {
        ble_hci_trans_buf_free(hci_evt);
    } else {
        ev->ev_queued = 0;
        ev->ev_cb = ble_hs_event_rx_hci_ev;
        ev->ev_arg = hci_evt;
        os_eventq_put(ble_hs_evq, ev);
    }
}

/**
 * Schedules for all pending notifications and indications to be sent in the
 * host parent task.
 */
void
ble_hs_notifications_sched(void)
{
#if !MYNEWT_VAL(BLE_HS_REQUIRE_OS)
    if (!os_started()) {
        ble_gatts_tx_notifications();
        return;
    }
#endif

    os_eventq_put(ble_hs_evq, &ble_hs_ev_tx_notifications);
}

/**
 * Causes the host to reset the NimBLE stack as soon as possible.  The
 * application is notified when the reset occurs via the host reset callback.
 *
 * @param reason                The host error code that gets passed to the
 *                                  reset callback.
 */
void
ble_hs_sched_reset(int reason)
{
    BLE_HS_DBG_ASSERT(ble_hs_reset_reason == 0);

    ble_hs_reset_reason = reason;
    os_eventq_put(ble_hs_evq, &ble_hs_ev_reset);
}

void
ble_hs_hw_error(uint8_t hw_code)
{
    ble_hs_sched_reset(BLE_HS_HW_ERR(hw_code));
}

/**
 * Synchronizes the host with the controller by sending a sequence of HCI
 * commands.  This function must be called before any other host functionality
 * is used, but it must be called after both the host and controller are
 * initialized.  Typically, the host-parent-task calls this function at the top
 * of its task routine.
 *
 * If the host fails to synchronize with the controller (if the controller is
 * not fully booted, for example), the host will attempt to resynchronize every
 * 100 ms.  For this reason, an error return code is not necessarily fatal.
 *
 * @return                      0 on success; nonzero on error.
 */
int
ble_hs_start(void)
{
    int rc;

    ble_hs_parent_task = os_sched_get_current_task();

    os_callout_init(&ble_hs_timer_timer, ble_hs_evq,
                    ble_hs_timer_exp, NULL);

    rc = ble_gatts_start();
    if (rc != 0) {
        return rc;
    }

    ble_hs_sync();

    rc = ble_hs_misc_restore_irks();
    assert(rc == 0);

    return 0;
}

/**
 * Called when a data packet is received from the controller.  This function
 * consumes the supplied mbuf, regardless of the outcome.
 *
 * @param om                    The incoming data packet, beginning with the
 *                                  HCI ACL data header.
 *
 * @return                      0 on success; nonzero on failure.
 */
static int
ble_hs_rx_data(struct os_mbuf *om, void *arg)
{
    int rc;

    rc = os_mqueue_put(&ble_hs_rx_q, ble_hs_evq, om);
    if (rc != 0) {
        os_mbuf_free_chain(om);
        return BLE_HS_EOS;
    }

    return 0;
}

/**
 * Enqueues an ACL data packet for transmission.  This function consumes the
 * supplied mbuf, regardless of the outcome.
 *
 * @param om                    The outgoing data packet, beginning with the
 *                                  HCI ACL data header.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_hs_tx_data(struct os_mbuf *om)
{
#if BLE_MONITOR
    ble_monitor_send_om(BLE_MONITOR_OPCODE_ACL_TX_PKT, om);
#endif

    ble_hci_trans_hs_acl_tx(om);
    return 0;
}

/**
 * Initializes the NimBLE host.  This function must be called before the OS is
 * started.  The NimBLE stack requires an application task to function.  One
 * application task in particular is designated as the "host parent task".  In
 * addition to application-specific work, the host parent task does work for
 * NimBLE by processing events generated by the host.
 */
void
ble_hs_init(void)
{
    int rc;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    log_init();

    /* Create memory pool of OS events */
    rc = os_mempool_init(&ble_hs_hci_ev_pool, BLE_HS_HCI_EVT_COUNT,
                         sizeof (struct os_event), ble_hs_hci_os_event_buf,
                         "ble_hs_hci_ev_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    /* These get initialized here to allow unit tests to run without a zeroed
     * bss.
     */
    ble_hs_ev_tx_notifications = (struct os_event) {
        .ev_cb = ble_hs_event_tx_notify,
    };
    ble_hs_ev_reset = (struct os_event) {
        .ev_cb = ble_hs_event_reset,
    };
    ble_hs_ev_start = (struct os_event) {
        .ev_cb = ble_hs_event_start,
    };

#if BLE_MONITOR
    rc = ble_monitor_init();
    SYSINIT_PANIC_ASSERT(rc == 0);
#endif

    ble_hs_hci_init();

    rc = ble_hs_conn_init();
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_l2cap_init();
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_att_init();
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_att_svr_init();
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gap_init();
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gattc_init();
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gatts_init();
    SYSINIT_PANIC_ASSERT(rc == 0);

    os_mqueue_init(&ble_hs_rx_q, ble_hs_event_rx_data, NULL);

    rc = stats_init_and_reg(
        STATS_HDR(ble_hs_stats), STATS_SIZE_INIT_PARMS(ble_hs_stats,
        STATS_SIZE_32), STATS_NAME_INIT_PARMS(ble_hs_stats), "ble_hs");
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = os_mutex_init(&ble_hs_mutex);
    SYSINIT_PANIC_ASSERT(rc == 0);

#if MYNEWT_VAL(BLE_HS_DEBUG)
    ble_hs_dbg_mutex_locked = 0;
#endif

    /* Configure the HCI transport to communicate with a host. */
    ble_hci_trans_cfg_hs(ble_hs_hci_rx_evt, NULL, ble_hs_rx_data, NULL);

    ble_hs_evq_set(os_eventq_dflt_get());

    /* Enqueue the start event to the default event queue.  Using the default
     * queue ensures the event won't run until the end of main().  This allows
     * the application to configure this package in the meantime.
     */
    os_eventq_put(os_eventq_dflt_get(), &ble_hs_ev_start);

#if BLE_MONITOR
    ble_monitor_new_index(0, (uint8_t[6]){ }, "nimble0");
#endif
}
