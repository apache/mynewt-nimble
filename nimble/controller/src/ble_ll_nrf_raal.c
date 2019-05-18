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
#include <stdbool.h>
#include <stdint.h>
#include "syscfg/syscfg.h"
#include "hal/hal_timer.h"
#include "controller/ble_ll.h"
#include "controller/ble_ll_sched.h"
#include "controller/ble_ll_nrf_raal.h"

#if MYNEWT_VAL(BLE_LL_NRF_RAAL_ENABLE)

/* Extra time reserved at the end of slot for cleanup */
#define SLOT_CLEANUP_TIME_TICKS     ble_ll_usecs_to_ticks_round_up(91)

#define SLOT_DURATION \
    (ble_ll_usecs_to_ticks_round_up(MYNEWT_VAL(BLE_LL_NRF_RAAL_SLOT_LENGTH)) + \
     (SLOT_CLEANUP_TIME_TICKS))

#define PENDING_F_SLOT_ENTER      (0x01)
#define PENDING_F_SLOT_EXIT       (0x02)

struct ble_ll_nrf_raal_state {
    bool in_continuous;
    bool in_critical;
    bool in_slot;
    uint8_t pending;
};

/* Global RAAL state */
static struct ble_ll_nrf_raal_state g_ble_ll_nrf_raal_state;

/* Scheduler item for RAAL items */
static struct ble_ll_sched_item g_ble_ll_nrf_raal_slot_sched;

/* Timer to stop slot on time */
static struct hal_timer g_ble_ll_nrf_raal_slot_end_tmr;

/* Event to schedule next slot */
static struct ble_npl_event g_ble_ll_nrf_raal_reschedule_ev;

extern void MYNEWT_VAL(BLE_LL_NRF_RAAL_ISR_HANDLER_NAME)(void);
extern void nrf_raal_timeslot_started(void);
extern void nrf_raal_timeslot_ended(void);

/* Schedule first slot in continuous mode */
static void
ble_ll_nrf_raal_slot_schedule(void)
{
    struct ble_ll_sched_item *sch;
    int rc;

    sch = &g_ble_ll_nrf_raal_slot_sched;

    assert(g_ble_ll_nrf_raal_state.in_continuous);
    assert(!sch->enqueued);

    /* Need to schedule a bit later since HFXO needs to be settled */
    sch->start_time = os_cputime_get32() + g_ble_ll_data.ll_xtal_ticks;
    sch->end_time = sch->start_time + SLOT_DURATION;

    rc = ble_ll_sched_nrf_raal(&g_ble_ll_nrf_raal_slot_sched);
    assert(rc == 0);
}

/* Reschedule slot in continuous mode */
static void
ble_ll_nrf_raal_slot_reschedule(void)
{
    struct ble_ll_sched_item *sch;
    int rc;

    sch = &g_ble_ll_nrf_raal_slot_sched;

    assert(g_ble_ll_nrf_raal_state.in_continuous);
    assert(!sch->enqueued);

    sch->start_time = sch->end_time;
    sch->end_time = sch->start_time + SLOT_DURATION;

    rc = ble_ll_sched_nrf_raal(&g_ble_ll_nrf_raal_slot_sched);
    assert(rc == 0);
}

static void
ble_ll_nrf_raal_slot_enter(void)
{
    /* XXX make sure HFXO is settled */
    assert(NRF_CLOCK->HFCLKSTAT & CLOCK_HFCLKSTAT_SRC_Msk);

    if (g_ble_ll_nrf_raal_state.in_critical) {
        if (g_ble_ll_nrf_raal_state.pending & PENDING_F_SLOT_EXIT) {
            /*
             * XXX not sure if this is ok since we were out of slot for some
             *     time but client was not notified as we re-entered slot now
             */
            g_ble_ll_nrf_raal_state.pending = 0;
            assert(0);
        } else {
            g_ble_ll_nrf_raal_state.pending |= PENDING_F_SLOT_ENTER;
        }
        return;
    }

    ble_phy_nrf_raal_slot_enter();
    ble_npl_hw_set_isr(RADIO_IRQn, (uint32_t)MYNEWT_VAL(BLE_LL_NRF_RAAL_ISR_HANDLER_NAME));

    g_ble_ll_nrf_raal_state.in_slot = 1;
    g_ble_ll_nrf_raal_state.pending &= ~PENDING_F_SLOT_ENTER;

    nrf_raal_timeslot_started();
}

static void
ble_ll_nrf_raal_slot_exit(void)
{
    if (g_ble_ll_nrf_raal_state.in_critical) {
        if (g_ble_ll_nrf_raal_state.pending & PENDING_F_SLOT_ENTER) {
            g_ble_ll_nrf_raal_state.pending = 0;
        } else {
            g_ble_ll_nrf_raal_state.pending |= PENDING_F_SLOT_EXIT;
        }
        return;
    }

    g_ble_ll_nrf_raal_state.in_slot = 0;
    g_ble_ll_nrf_raal_state.pending &= ~PENDING_F_SLOT_EXIT;

    nrf_raal_timeslot_ended();

    ble_phy_nrf_raal_slot_exit();

    assert(ble_ll_state_get() == BLE_LL_STATE_RAAL);
    ble_ll_state_set(BLE_LL_STATE_STANDBY);
}

static void
ble_ll_nrf_raal_slot_end_tmr_cb(void *arg)
{
    ble_ll_nrf_raal_slot_exit();

    ble_npl_eventq_put(&g_ble_ll_data.ll_evq, &g_ble_ll_nrf_raal_reschedule_ev);
}

void
ble_ll_nrf_raal_removed_from_sched(void)
{
    ble_npl_eventq_put(&g_ble_ll_data.ll_evq, &g_ble_ll_nrf_raal_reschedule_ev);
}

void
ble_ll_nrf_raal_halt()
{
    os_sr_t sr;

    assert(g_ble_ll_nrf_raal_state.in_slot);

    OS_ENTER_CRITICAL(sr);
    ble_ll_sched_rmv_elem(&g_ble_ll_nrf_raal_slot_sched);
    os_cputime_timer_stop(&g_ble_ll_nrf_raal_slot_end_tmr);
    ble_npl_eventq_remove(&g_ble_ll_data.ll_evq,
                          &g_ble_ll_nrf_raal_reschedule_ev);
    OS_EXIT_CRITICAL(sr);

    ble_ll_nrf_raal_slot_exit();

    ble_npl_eventq_put(&g_ble_ll_data.ll_evq, &g_ble_ll_nrf_raal_reschedule_ev);
}

static int
ble_ll_nrf_raal_slot_sched_cb(struct ble_ll_sched_item *sch)
{
    ble_ll_state_set(BLE_LL_STATE_RAAL);

    ble_ll_nrf_raal_slot_enter();

    /* Exit slot for cleanup */
    os_cputime_timer_start(&g_ble_ll_nrf_raal_slot_end_tmr,
                           sch->end_time - SLOT_CLEANUP_TIME_TICKS);

    return BLE_LL_SCHED_STATE_RUNNING;
}

static void
ble_ll_nrf_raal_reschedule_ev_func(struct ble_npl_event *ev)
{
    if (g_ble_ll_nrf_raal_state.in_continuous) {
        ble_ll_nrf_raal_slot_reschedule();
    }
}

void
nrf_raal_init(void)
{
    /* XXX nothing to do? */
}

void
nrf_raal_uninit(void)
{
    /* XXX nothing to do? */
}

bool
nrf_raal_timeslot_is_granted(void)
{
    return g_ble_ll_nrf_raal_state.in_slot;
}

void
nrf_raal_continuous_mode_enter(void)
{
    assert(!g_ble_ll_nrf_raal_state.in_continuous);

    g_ble_ll_nrf_raal_state.in_continuous = 1;

    ble_ll_nrf_raal_slot_schedule();
}

void
nrf_raal_continuous_mode_exit(void)
{
    os_sr_t sr;

    assert(g_ble_ll_nrf_raal_state.in_continuous);

    OS_ENTER_CRITICAL(sr);
    ble_ll_sched_rmv_elem(&g_ble_ll_nrf_raal_slot_sched);
    os_cputime_timer_stop(&g_ble_ll_nrf_raal_slot_end_tmr);
    ble_npl_eventq_remove(&g_ble_ll_data.ll_evq,
                          &g_ble_ll_nrf_raal_reschedule_ev);
    OS_EXIT_CRITICAL(sr);

    if (g_ble_ll_nrf_raal_state.in_slot) {
        ble_ll_nrf_raal_slot_exit();
    }

#ifdef BLE_XCVR_RFCLK
    ble_ll_sched_rfclk_chk_restart();
#endif

    g_ble_ll_nrf_raal_state.in_continuous = 0;
}

bool
nrf_raal_timeslot_request(uint32_t length_us)
{
    uint32_t end;

    if (!g_ble_ll_nrf_raal_state.in_slot) {
        return false;
    }

    assert(ble_ll_state_get() == BLE_LL_STATE_RAAL);

    end = os_cputime_get32() + ble_ll_usecs_to_ticks_round_up(length_us);

    return CPUTIME_GEQ(g_ble_ll_nrf_raal_slot_sched.end_time, end);
}

uint32_t
nrf_raal_timeslot_us_left_get(void)
{
    int32_t left;

    if (!g_ble_ll_nrf_raal_state.in_slot) {
        return 0;
    }

    left = (int32_t)(g_ble_ll_nrf_raal_slot_sched.end_time - os_cputime_get32());
    if (left < 0) {
        return 0;
    }

    return os_cputime_ticks_to_usecs(left);
}

void
nrf_raal_critical_section_enter(void)
{
    assert(!g_ble_ll_nrf_raal_state.in_critical);

    g_ble_ll_nrf_raal_state.in_critical = 1;
}

void
nrf_raal_critical_section_exit(void)
{
    /* XXX apparently nrf_raal can call this even without calling enter... */

    if (g_ble_ll_nrf_raal_state.pending & PENDING_F_SLOT_ENTER) {
        ble_ll_nrf_raal_slot_enter();
    } else if (g_ble_ll_nrf_raal_state.pending & PENDING_F_SLOT_EXIT) {
        ble_ll_nrf_raal_slot_exit();
    }

    g_ble_ll_nrf_raal_state.in_critical = 0;

    assert(!g_ble_ll_nrf_raal_state.pending);
}

void
ble_ll_nrf_raal_init(void)
{
    assert(SLOT_DURATION > 0);

    os_cputime_timer_init(&g_ble_ll_nrf_raal_slot_end_tmr,
                          ble_ll_nrf_raal_slot_end_tmr_cb, NULL);

    g_ble_ll_nrf_raal_slot_sched.sched_type = BLE_LL_SCHED_TYPE_NRF_RAAL;
    g_ble_ll_nrf_raal_slot_sched.sched_cb = ble_ll_nrf_raal_slot_sched_cb;

    /* We'll start scheduling form here */
    g_ble_ll_nrf_raal_slot_sched.end_time = os_cputime_get32();

    ble_npl_event_init(&g_ble_ll_nrf_raal_reschedule_ev,
                       ble_ll_nrf_raal_reschedule_ev_func, NULL);
}

#endif
