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

#include "wqueue.h"

extern "C" {

typedef wqueue<os_event *> wqueue_t;

static struct os_eventq dflt_evq;

struct os_eventq *
os_eventq_dflt_get(void)
{
    if (!dflt_evq.q)
    {
        dflt_evq.q = new wqueue_t();
    }

    return &dflt_evq;
}

void
os_eventq_init(struct os_eventq *evq)
{
    evq->q = new wqueue_t();
}

int
os_eventq_inited(const struct os_eventq *evq)
{
    return (evq->q != NULL);
}

void
os_eventq_put(struct os_eventq *evq, struct os_event *ev)
{
    wqueue_t *q = static_cast<wqueue_t *>(evq->q);

    if (OS_EVENT_QUEUED(ev))
    {
        return;
    }

    ev->ev_queued = 1;
    q->put(ev);          //    ret = xQueueSendToBack(evq->q, &ev, 0);
}

struct os_event *
os_eventq_get(struct os_eventq *evq)
{
    struct os_event *ev;
    wqueue_t *q = static_cast<wqueue_t *>(evq->q);

    ev = q->get();
    ev->ev_queued = 0;

    return ev;
}

/*
====================================================
                NOT IMPLEMENTED
====================================================

struct os_event *
os_eventq_get_no_wait(struct os_eventq *evq)
{
    assert(1);  // Not implemented
    return os_eventq_get(evq);
}

void
os_eventq_remove(struct os_eventq *evq, struct os_event *ev)
{
    assert(1);  // Not implemented
}

====================================================
*/

void
os_eventq_run(struct os_eventq *evq)
{
    struct os_event *ev;

    ev = os_eventq_get(evq);
    assert(ev->ev_cb != NULL);

    ev->ev_cb(ev);
}

}
