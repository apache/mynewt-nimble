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
#include <stdint.h>
#include <string.h>
#include "os/os.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

static struct os_eventq dflt_evq;

static inline bool in_isr()
{
    return (SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0;
}

int
os_started(void)
{
    return xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
}

struct os_task *
os_sched_get_current_task(void)
{
    return xTaskGetCurrentTaskHandle();
}

os_error_t
os_mutex_init(struct os_mutex *mu)
{
    if (!mu) {
        return OS_INVALID_PARM;
    }

    mu->handle = xSemaphoreCreateRecursiveMutex();
    assert(mu->handle);

    return OS_OK;
}

os_error_t
os_mutex_release(struct os_mutex *mu)
{
    if (!mu) {
        return OS_INVALID_PARM;
    }

    assert(mu->handle);

    if (in_isr()) {
        assert(0);
    } else {
        if (xSemaphoreGiveRecursive(mu->handle) != pdPASS) {
            return OS_BAD_MUTEX;
        }
    }

    return OS_OK;
}

os_error_t
os_mutex_pend(struct os_mutex *mu, uint32_t timeout)
{
    if (!mu) {
        return OS_INVALID_PARM;
    }

    assert(mu->handle);

    if (in_isr()) {
        assert(0);
    } else {
        if (xSemaphoreTakeRecursive(mu->handle, timeout) != pdPASS) {
            return OS_TIMEOUT;
        }
    }

    return OS_OK;
}

os_error_t
os_sem_init(struct os_sem *sem, uint16_t tokens)
{
    if (!sem) {
        return OS_INVALID_PARM;
    }

    sem->handle = xSemaphoreCreateCounting(128, tokens);
    assert(sem->handle);

    return OS_OK;
}

os_error_t
os_sem_release(struct os_sem *sem)
{
    BaseType_t ret;
    BaseType_t woken;

    if (!sem) {
        return OS_INVALID_PARM;
    }

    assert(sem->handle);

    if (in_isr()) {
        ret = xSemaphoreGiveFromISR(sem->handle, &woken);
        assert(ret == pdPASS);

        portYIELD_FROM_ISR(woken);
    } else {
        ret = xSemaphoreGive(sem->handle);
        assert(ret == pdPASS);
    }

    return OS_OK;
}

os_error_t
os_sem_pend(struct os_sem *sem, uint32_t timeout)
{
    BaseType_t woken;

    if (!sem) {
        return OS_INVALID_PARM;
    }

    assert(sem->handle);

    if (in_isr()) {
        assert(timeout == 0);
        if (xSemaphoreTakeFromISR(sem->handle, &woken) != pdPASS) {
            portYIELD_FROM_ISR(woken);
            return OS_TIMEOUT;
        }
        portYIELD_FROM_ISR(woken);
    } else {
        if (xSemaphoreTake(sem->handle, timeout) != pdPASS) {
            return OS_TIMEOUT;
        }
    }

    return OS_OK;
}

uint16_t
os_sem_get_count(struct os_sem *sem)
{
    /* XXX FreeRTOS 9.x added dedicated API for this - it's the same as below */
    return uxQueueMessagesWaiting(sem->handle);
}

struct os_eventq *
os_eventq_dflt_get(void)
{
    if (!dflt_evq.q) {
        dflt_evq.q = xQueueCreate(32, sizeof(struct os_event *));
    }

    return &dflt_evq;
}

void
os_eventq_init(struct os_eventq *evq)
{
    evq->q = xQueueCreate(32, sizeof(struct os_event *));
}

struct os_event *
os_eventq_get(struct os_eventq *evq)
{
    struct os_event *ev;
    BaseType_t ret;

    ret = xQueueReceive(evq->q, &ev, portMAX_DELAY);
    assert(ret == pdPASS);

    ev->ev_queued = 0;

    return ev;
}

void
os_eventq_put(struct os_eventq *evq, struct os_event *ev)
{
    BaseType_t ret;

    if (OS_EVENT_QUEUED(ev)) {
        return;
    }

    ev->ev_queued = 1;

    ret = xQueueSendToBack(evq->q, &ev, 0);
    assert(ret == pdPASS);
}

void
os_eventq_remove(struct os_eventq *evq, struct os_event *ev)
{
    struct os_event *tmp_ev;
    BaseType_t ret;
    int i;
    int count;

    if (!OS_EVENT_QUEUED(ev)) {
        return;
    }

    /*
     * XXX We cannot extract element from inside FreeRTOS queue so as a quick
     * workaround we'll just remove all elements and add them back except the
     * one we need to remove. This is silly, but works for now - we probably
     * better use counting semaphore with os_queue to handle this in future.
     */

    vPortEnterCritical();

    count = uxQueueMessagesWaiting(evq->q);
    for (i = 0; i < count; i++) {
        ret = xQueueReceive(evq->q, &tmp_ev, 0);
        assert(ret == pdPASS);

        if (tmp_ev == ev) {
            continue;
        }

        ret = xQueueSendToBack(evq->q, &tmp_ev, 0);
        assert(ret == pdPASS);
    }

    vPortExitCritical();

    ev->ev_queued = 0;
}

void
os_eventq_run(struct os_eventq *evq)
{
    struct os_event *ev;

    ev = os_eventq_get(evq);
    assert(ev->ev_cb != NULL);

    ev->ev_cb(ev);
}

static void
os_callout_timer_cb(TimerHandle_t timer)
{
    struct os_callout *c;

    c = pvTimerGetTimerID(timer);
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
    memset(c, 0, sizeof(*c));
    c->c_ev.ev_cb = ev_cb;
    c->c_ev.ev_arg = ev_arg;
    c->c_evq = evq;
    c->c_timer = xTimerCreate("co", 1, pdFALSE, c, os_callout_timer_cb);
}

int
os_callout_reset(struct os_callout *c, int32_t ticks)
{
    BaseType_t woken1, woken2, woken3;

    if (ticks < 0) {
        return OS_EINVAL;
    }

    if (ticks == 0) {
        ticks = 1;
    }

    c->c_ticks = os_time_get() + ticks;

    if (in_isr()) {
        xTimerStopFromISR(c->c_timer, &woken1);
        xTimerChangePeriodFromISR(c->c_timer, ticks, &woken2);
        xTimerResetFromISR(c->c_timer, &woken3);

        portYIELD_FROM_ISR(woken1 || woken2 || woken3);
    } else {
        xTimerStop(c->c_timer, portMAX_DELAY);
        xTimerChangePeriod(c->c_timer, ticks, portMAX_DELAY);
        xTimerReset(c->c_timer, portMAX_DELAY);
    }

    return OS_OK;
}

int
os_callout_queued(struct os_callout *c)
{
    return xTimerIsTimerActive(c->c_timer) == pdTRUE;
}

void
os_callout_stop(struct os_callout *c)
{
    xTimerStop(c->c_timer, portMAX_DELAY);
}

os_time_t
os_time_get(void)
{
    return xTaskGetTickCountFromISR();
}

int
os_time_ms_to_ticks(uint32_t ms, uint32_t *out_ticks)
{
    *out_ticks = ms;

    return OS_OK;
}
