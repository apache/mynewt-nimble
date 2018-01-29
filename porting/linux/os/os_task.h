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

#ifndef _OS_TASK_H
#define _OS_TASK_H

#include "os/os.h"

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The highest and lowest task priorities */
#define OS_TASK_PRI_HIGHEST (sched_get_priority_max(SCHED_RR))
#define OS_TASK_PRI_LOWEST  (sched_get_priority_min(SCHED_RR))

/* Task states */
typedef enum os_task_state {
    OS_TASK_READY = 1,
    OS_TASK_SLEEP = 2,
} os_task_state_t;

/* Task flags */
#define OS_TASK_FLAG_NO_TIMEOUT     (0x01U)
#define OS_TASK_FLAG_SEM_WAIT       (0x02U)
#define OS_TASK_FLAG_MUTEX_WAIT     (0x04U)
#define OS_TASK_FLAG_EVQ_WAIT       (0x08U)

typedef void (*os_task_func_t)(void *);

// #define OS_TASK_MAX_NAME_LEN (32)

struct os_task {
    pthread_t              handle;
    const char*            name;
};

int os_task_init(struct os_task *t, const char *name, os_task_func_t func,
		 void *arg, uint8_t prio, os_time_t sanity_itvl,
		 os_stack_t *stack_bottom, uint16_t stack_size);

int os_task_remove(struct os_task *t);

uint8_t os_task_count(void);

  /*
struct os_task_info {
    uint8_t oti_prio;
    uint8_t oti_taskid;
    uint8_t oti_state;
    uint16_t oti_stkusage;
    uint16_t oti_stksize;
    uint32_t oti_cswcnt;
    uint32_t oti_runtime;
    os_time_t oti_last_checkin;
    os_time_t oti_next_checkin;

    char oti_name[OS_TASK_MAX_NAME_LEN];
};

struct os_task *os_task_info_get_next(const struct os_task *,
        struct os_task_info *);
  */

#ifdef __cplusplus
}
#endif

#endif /* _OS_TASK_H */
