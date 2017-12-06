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

#ifndef _OS_EVENTQ_H
#define _OS_EVENTQ_H

#include <inttypes.h>
#include "os/os_time.h"
#include "os/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

struct os_event;
typedef void os_event_fn(struct os_event *ev);

struct os_event {
    uint8_t ev_queued;
    os_event_fn *ev_cb;
    void *ev_arg;
    STAILQ_ENTRY(os_event) ev_next;
};

#define OS_EVENT_QUEUED(__ev) ((__ev)->ev_queued)

struct os_eventq {
    struct os_task *evq_owner;  /* owner task */
    struct os_task *evq_task;   /* sleeper; must be either NULL, or the owner */
    STAILQ_HEAD(, os_event) evq_list;
};

void os_eventq_init(struct os_eventq *);
int os_eventq_inited(const struct os_eventq *evq);
void os_eventq_put(struct os_eventq *, struct os_event *);
struct os_event *os_eventq_get_no_wait(struct os_eventq *evq);
struct os_event *os_eventq_get(struct os_eventq *);
void os_eventq_run(struct os_eventq *evq);
struct os_event *os_eventq_poll(struct os_eventq **, int, os_time_t);
void os_eventq_remove(struct os_eventq *, struct os_event *);
struct os_eventq *os_eventq_dflt_get(void);

/* [DEPRECATED] */
void os_eventq_designate(struct os_eventq **dst, struct os_eventq *val,
                         struct os_event *start_ev);

#ifdef __cplusplus
}
#endif

#endif /* _OS_EVENTQ_H */

