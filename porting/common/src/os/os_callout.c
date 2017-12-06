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
#include <string.h>

#include "../os/os_priv.h"
#include "os/os.h"

/**
 * @addtogroup OSKernel
 * @{
 *   @defgroup OSCallouts Event Timers (Callouts)
 *   @{
 */
struct os_callout_list g_callout_list;

/**
 * Initialize a callout.
 *
 * Callouts are used to schedule events in the future onto a task's event
 * queue.  Callout timers are scheduled using the os_callout_reset()
 * function.  When the timer expires, an event is posted to the event
 * queue specified in os_callout_init().  The event argument given here
 * is posted in the ev_arg field of that event.
 *
 * @param c The callout to initialize
 * @param evq The event queue to post an OS_EVENT_T_TIMER event to
 * @param timo_func The function to call on this callout for the host task
 *                  used to provide multiple timer events to a task
 *                  (this can be NULL.)
 * @param ev_arg The argument to provide to the event when posting the
 *               timer.
 */
void os_callout_init(struct os_callout *c, struct os_eventq *evq,
                     os_event_fn *ev_cb, void *ev_arg)
{
    memset(c, 0, sizeof(*c));
    c->c_ev.ev_cb = ev_cb;
    c->c_ev.ev_arg = ev_arg;
    c->c_evq = evq;
}

/**
 * Stop the callout from firing off, any pending events will be cleared.
 *
 * @param c The callout to stop
 */
void
os_callout_stop(struct os_callout *c)
{
    os_sr_t sr;

    OS_ENTER_CRITICAL(sr);

    if (os_callout_queued(c)) {
        TAILQ_REMOVE(&g_callout_list, c, c_next);
        c->c_next.tqe_prev = NULL;
    }

    if (c->c_evq) {
        os_eventq_remove(c->c_evq, &c->c_ev);
    }

    OS_EXIT_CRITICAL(sr);
}

/**
 * Reset the callout to fire off in 'ticks' ticks.
 *
 * @param c The callout to reset
 * @param ticks The number of ticks to wait before posting an event
 *
 * @return 0 on success, non-zero on failure
 */
int
os_callout_reset(struct os_callout *c, int32_t ticks)
{
    struct os_callout *entry;
    os_sr_t sr;
    int rc;

    if (ticks < 0) {
        rc = OS_EINVAL;
        goto err;
    }

    OS_ENTER_CRITICAL(sr);

    os_callout_stop(c);

    if (ticks == 0) {
        ticks = 1;
    }

    c->c_ticks = os_time_get() + ticks;

    entry = NULL;
    TAILQ_FOREACH(entry, &g_callout_list, c_next) {
        if (OS_TIME_TICK_LT(c->c_ticks, entry->c_ticks)) {
            break;
        }
    }

    if (entry) {
        TAILQ_INSERT_BEFORE(entry, c, c_next);
    } else {
        TAILQ_INSERT_TAIL(&g_callout_list, c, c_next);
    }

    OS_EXIT_CRITICAL(sr);

    return (0);
err:
    return (rc);
}

/**
 * This function is called by the OS in the time tick.  It searches the list
 * of callouts, and sees if any of them are ready to run.  If they are ready
 * to run, it posts an event for each callout that's ready to run,
 * to the event queue provided to os_callout_init().
 */
void
os_callout_tick(void)
{
    os_sr_t sr;
    struct os_callout *c;
    uint32_t now;

    now = os_time_get();

    while (1) {
        OS_ENTER_CRITICAL(sr);
        c = TAILQ_FIRST(&g_callout_list);
        if (c) {
            if (OS_TIME_TICK_GEQ(now, c->c_ticks)) {
                TAILQ_REMOVE(&g_callout_list, c, c_next);
                c->c_next.tqe_prev = NULL;
            } else {
                c = NULL;
            }
        }
        OS_EXIT_CRITICAL(sr);

        if (c) {
            if (c->c_evq) {
                os_eventq_put(c->c_evq, &c->c_ev);
            } else {
                c->c_ev.ev_cb(&c->c_ev);
            }
        } else {
            break;
        }
    }
}

/*
 * Returns the number of ticks to the first pending callout. If there are no
 * pending callouts then return OS_TIMEOUT_NEVER instead.
 *
 * @param now The time now
 *
 * @return Number of ticks to first pending callout
 */
os_time_t
os_callout_wakeup_ticks(os_time_t now)
{
    os_time_t rt;
    struct os_callout *c;

    OS_ASSERT_CRITICAL();

    c = TAILQ_FIRST(&g_callout_list);
    if (c != NULL) {
        if (OS_TIME_TICK_GEQ(c->c_ticks, now)) {
            rt = c->c_ticks - now;
        } else {
            rt = 0;     /* callout time is in the past */
        }
    } else {
        rt = OS_TIMEOUT_NEVER;
    }

    return (rt);
}

/*
 * Returns the number of ticks which remains to callout..
 *
 * @param c callout
 * @param now The time now
 *
 * @return Number of ticks to first pending callout
 */
os_time_t os_callout_remaining_ticks(struct os_callout *c, os_time_t now)
{
    os_time_t rt;

    OS_ASSERT_CRITICAL();

    if (OS_TIME_TICK_GEQ(c->c_ticks, now)) {
        rt = c->c_ticks - now;
    } else {
        rt = 0;     /* callout time is in the past */
    }

    return rt;
}

/**
 *   @} Callout Timers
 * @} OS Kernel
 */
