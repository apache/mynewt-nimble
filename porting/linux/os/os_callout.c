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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "os/os.h"

#include <time.h>
#include <signal.h>


static void
os_callout_timer_cb(union sigval sv)
{
    struct os_callout *c = (struct os_callout *)sv.sival_ptr;
    assert(c);

    if (c->c_evq) {
        os_eventq_put(c->c_evq, &c->c_ev);
    } else {
        c->c_ev.ev_cb(&c->c_ev);
    }
}


void
os_callout_init(struct os_callout *c, struct os_eventq *evq,
                os_event_fn *ev_cb, void *ev_arg)
{
    struct sigevent         event;

    // Initialize the callout.
    memset(c, 0, sizeof(*c));
    c->c_ev.ev_cb = ev_cb;
    c->c_ev.ev_arg = ev_arg;
    c->c_evq = evq;

    event.sigev_notify = SIGEV_THREAD;
    event.sigev_value.sival_ptr = c;     // put callout obj in signal args
    event.sigev_notify_function = os_callout_timer_cb;
    event.sigev_notify_attributes = NULL;

    timer_create(CLOCK_REALTIME, &event, &c->c_timer);
}

int
os_callout_inited(struct os_callout *c)
{
    return (c->c_timer != NULL);
}

int
os_callout_reset(struct os_callout *c, int32_t ticks)
{
    struct itimerspec       its;

    if (ticks < 0) {
        return OS_EINVAL;
    }

    if (ticks == 0) {
        ticks = 1;
    }

    c->c_ticks = os_time_get() + ticks;

    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;                     // one shot
    its.it_value.tv_sec = (ticks / 1000);
    its.it_value.tv_nsec = (ticks % 1000) * 1000000; // expiration
    timer_settime(c->c_timer, 0, &its, NULL);

    return OS_OK;
}

int
os_callout_queued(struct os_callout *c)
{
    struct itimerspec its;
    timer_gettime(c->c_timer, &its);

    return ((its.it_value.tv_sec > 0) ||
            (its.it_value.tv_nsec > 0));
}

void
os_callout_stop(struct os_callout *c)
{
    if (!os_callout_inited(c)) return;

    struct itimerspec its;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 0;
    timer_settime(c->c_timer, 0, &its, NULL);
}
