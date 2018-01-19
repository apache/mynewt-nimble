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

#ifndef OS_TRACE_API_H
#define OS_TRACE_API_H

#include <stdint.h>

#include "syscfg/syscfg.h"
#include "os/os.h"

#define OS_TRACE_ID_OFFSET                    (32u)

#define OS_TRACE_ID_EVQ_PUT                   (1u + OS_TRACE_ID_OFFSET)
#define OS_TRACE_ID_EVQ_GET                   (2u + OS_TRACE_ID_OFFSET)
#define OS_TRACE_ID_MUTEX_INIT                (3u + OS_TRACE_ID_OFFSET)
#define OS_TRACE_ID_MUTEX_RELEASE             (4u + OS_TRACE_ID_OFFSET)
#define OS_TRACE_ID_MUTEX_PEND                (5u + OS_TRACE_ID_OFFSET)

#if MYNEWT_VAL(OS_SYSVIEW)
void os_trace_enter_isr(void);
void os_trace_exit_isr(void);
void os_trace_exit_isr_to_scheduler(void);
void os_trace_task_info(const struct os_task *p_task);
void os_trace_task_create(uint32_t task_id);
void os_trace_task_start_exec(uint32_t task_id);
void os_trace_task_stop_exec(void);
void os_trace_task_start_ready(uint32_t task_id);
void os_trace_task_stop_ready(uint32_t task_id, unsigned reason);
void os_trace_idle(void);

void os_trace_void(unsigned id);
void os_trace_u32(unsigned id, uint32_t para0);
void os_trace_u32x2(unsigned id, uint32_t para0, uint32_t para1);
void os_trace_u32x3(unsigned id, uint32_t para0, uint32_t para1, uint32_t para2);
void os_trace_u32x4(unsigned id, uint32_t para0, uint32_t para1, uint32_t para2, uint32_t para3);
void os_trace_u32x5(unsigned id, uint32_t para0, uint32_t para1, uint32_t para2, uint32_t para3, uint32_t para4);
void os_trace_enter_timer(uint32_t timer_id);
void os_trace_exit_timer(void);

void os_trace_end_call(unsigned id);
void os_trace_end_call_return_value(unsigned id, uint32_t return_value);

#else

static inline void os_trace_enter_isr(void){}
static inline void os_trace_exit_isr(void){}
static inline void os_trace_exit_isr_to_scheduler(void){}
static inline void os_trace_task_info(const struct os_task *p_task){}
static inline void os_trace_task_create(uint32_t task_id){}
static inline void os_trace_task_start_exec(uint32_t task_id){}
static inline void os_trace_task_stop_exec(void){}
static inline void os_trace_task_start_ready(uint32_t task_id){}
static inline void os_trace_task_stop_ready(uint32_t task_id, unsigned reason){}
static inline void os_trace_idle(void){}
static inline void os_trace_void(unsigned id){}
static inline void os_trace_u32(unsigned id, uint32_t para0){}
static inline void os_trace_u32x2(unsigned id, uint32_t para0, uint32_t para1){}
static inline void os_trace_u32x3(unsigned id, uint32_t para0, uint32_t para1, uint32_t para2){}
static inline void os_trace_u32x4(unsigned id, uint32_t para0, uint32_t para1, uint32_t para2, uint32_t para3){}
static inline void os_trace_u32x5(unsigned id, uint32_t para0, uint32_t para1, uint32_t para2, uint32_t para3, uint32_t para4){}
static inline void os_trace_enter_timer(uint32_t timer_id){}
static inline void os_trace_exit_timer(void){}
static inline void os_trace_end_call(unsigned id){}
static inline void os_trace_end_call_return_value(unsigned id, uint32_t return_value){}
#endif

#endif
