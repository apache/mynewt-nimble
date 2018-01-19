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

#include "os/os.h"
#include "os/os_trace_api.h"
#include "os/queue.h"
#include <assert.h>
#include "../os/os_priv.h"

/**
 * @addtogroup OSKernel
 * @{
 *   @defgroup OSSched Scheduler
 *   @{
 */

struct os_task_list g_os_run_list = TAILQ_HEAD_INITIALIZER(g_os_run_list);
struct os_task_list g_os_sleep_list = TAILQ_HEAD_INITIALIZER(g_os_sleep_list);

struct os_task *g_current_task;

extern os_time_t g_os_time;
os_time_t g_os_last_ctx_sw_time;

/**
 * os sched insert
 *
 * Insert a task into the scheduler list. This causes the task to be evaluated
 * for running when os_sched is called.
 *
 * @param t     Pointer to task to insert in run list
 *
 * @return int  OS_OK: task was inserted into run list
 *              OS_EINVAL: Task was not in ready state.
 */
os_error_t
os_sched_insert(struct os_task *t)
{
    struct os_task *entry;
    os_sr_t sr;
    os_error_t rc;

    if (t->t_state != OS_TASK_READY) {
        rc = OS_EINVAL;
        goto err;
    }

    entry = NULL;
    OS_ENTER_CRITICAL(sr);
    TAILQ_FOREACH(entry, &g_os_run_list, t_os_list) {
        if (t->t_prio < entry->t_prio) {
            break;
        }
    }
    if (entry) {
        TAILQ_INSERT_BEFORE(entry, (struct os_task *) t, t_os_list);
    } else {
        TAILQ_INSERT_TAIL(&g_os_run_list, (struct os_task *) t, t_os_list);
    }
    OS_EXIT_CRITICAL(sr);

    return (0);
err:
    return (rc);
}

void
os_sched_ctx_sw_hook(struct os_task *next_t)
{
#if MYNEWT_VAL(OS_CTX_SW_STACK_CHECK)
    os_stack_t *top;
    int i;

    top = g_current_task->t_stacktop - g_current_task->t_stacksize;
    for (i = 0; i < MYNEWT_VAL(OS_CTX_SW_STACK_GUARD); i++) {
        assert(top[i] == OS_STACK_PATTERN);
    }
#endif
    os_trace_task_start_exec(next_t->t_taskid);
    next_t->t_ctx_sw_cnt++;
    g_current_task->t_run_time += g_os_time - g_os_last_ctx_sw_time;
    g_os_last_ctx_sw_time = g_os_time;
}

/**
 * os sched get current task
 *
 * Returns the currently running task. Note that this task may or may not be
 * the highest priority task ready to run.
 *
 *
 * @return struct os_task*
 */
struct os_task *
os_sched_get_current_task(void)
{
    return (g_current_task);
}

/**
 * os sched set current task
 *
 * Sets the currently running task to 't'. Note that this function simply sets
 * the global variable holding the currently running task. It does not perform
 * a context switch or change the os run or sleep list.
 *
 * @param t Pointer to currently running task.
 */
void
os_sched_set_current_task(struct os_task *t)
{
    g_current_task = t;
}

/**
 * os sched
 *
 * Performs a context switch. When called, it will either find the highest
 * priority task ready to run if next_t is NULL (i.e. the head of the os run
 * list) or will schedule next_t as the task to run.
 *
 * @param next_t Task to run
 */
void
os_sched(struct os_task *next_t)
{
    os_sr_t sr;

    OS_ENTER_CRITICAL(sr);

    if (!next_t) {
        next_t = os_sched_next_task();
    }

    os_arch_ctx_sw(next_t);

    OS_EXIT_CRITICAL(sr);
}

/**
 * os sched sleep
 *
 * Removes the task from the run list and puts it on the sleep list.
 *
 * @param t Task to put to sleep
 * @param nticks Number of ticks to put task to sleep
 *
 * @return int
 *
 * NOTE: must be called with interrupts disabled! This function does not call
 * the scheduler
 */
int
os_sched_sleep(struct os_task *t, os_time_t nticks)
{
    struct os_task *entry;

    entry = NULL;

    TAILQ_REMOVE(&g_os_run_list, t, t_os_list);
    t->t_state = OS_TASK_SLEEP;
    t->t_next_wakeup = os_time_get() + nticks;
    if (nticks == OS_TIMEOUT_NEVER) {
        t->t_flags |= OS_TASK_FLAG_NO_TIMEOUT;
        TAILQ_INSERT_TAIL(&g_os_sleep_list, t, t_os_list);
    } else {
        TAILQ_FOREACH(entry, &g_os_sleep_list, t_os_list) {
            if ((entry->t_flags & OS_TASK_FLAG_NO_TIMEOUT) ||
                    OS_TIME_TICK_GT(entry->t_next_wakeup, t->t_next_wakeup)) {
                break;
            }
        }
        if (entry) {
            TAILQ_INSERT_BEFORE(entry, t, t_os_list);
        } else {
            TAILQ_INSERT_TAIL(&g_os_sleep_list, t, t_os_list);
        }
    }

    os_trace_task_stop_ready(t->t_taskid, OS_TASK_SLEEP);
    return (0);
}

/**
 * os sched remove
 *
 * XXX
 * NOTE - This routine is currently experimental and not ready for common use
 *
 * Stops a task and removes it from the task list.
 *
 * @return int
 *
 * NOTE: must be called with interrupts disabled! This function does not call
 * the scheduler
 */
int
os_sched_remove(struct os_task *t)
{

    if (t->t_state == OS_TASK_SLEEP) {
        TAILQ_REMOVE(&g_os_sleep_list, t, t_os_list);
    } else if (t->t_state == OS_TASK_READY) {
        TAILQ_REMOVE(&g_os_run_list, t, t_os_list);
    }
    t->t_next_wakeup = 0;
    t->t_flags |= OS_TASK_FLAG_NO_TIMEOUT;

    STAILQ_REMOVE(&g_os_task_list, t, os_task, t_os_task_list);

    os_trace_task_stop_exec();
    return OS_OK;
}

/**
 * os sched wakeup
 *
 * Called to wake up a task. Waking up a task consists of setting the task state
 * to READY and moving it from the sleep list to the run list.
 *
 * @param t     Pointer to task to wake up.
 *
 * @return int
 *
 * NOTE: This function must be called with interrupts disabled.
 */
int
os_sched_wakeup(struct os_task *t)
{
    struct os_task_obj *os_obj;

    assert(t->t_state == OS_TASK_SLEEP);

    /* Remove self from object list if waiting on one */
    if (t->t_obj) {
        os_obj = (struct os_task_obj *)t->t_obj;
        assert(!SLIST_EMPTY(&os_obj->obj_head));
        SLIST_REMOVE(&os_obj->obj_head, t, os_task, t_obj_list);
        SLIST_NEXT(t, t_obj_list) = NULL;
        t->t_obj = NULL;
    }

    /* Remove task from sleep list */
    t->t_state = OS_TASK_READY;
    t->t_next_wakeup = 0;
    t->t_flags &= ~OS_TASK_FLAG_NO_TIMEOUT;
    TAILQ_REMOVE(&g_os_sleep_list, t, t_os_list);
    os_sched_insert(t);

    return (0);
}

/**
 * os sched os timer exp
 *
 * Called when the OS tick timer expires. Search the sleep list for any tasks
 * that need waking up. This occurs when the current OS time exceeds the next
 * wakeup time stored in the task. Any tasks that need waking up will be
 * removed from the sleep list and added to the run list.
 *
 */
void
os_sched_os_timer_exp(void)
{
    struct os_task *t;
    struct os_task *next;
    os_time_t now;
    os_sr_t sr;

    now = os_time_get();

    OS_ENTER_CRITICAL(sr);

    /*
     * Wakeup any tasks that have their sleep timer expired
     */
    t = TAILQ_FIRST(&g_os_sleep_list);
    while (t) {
        /* If task waiting forever, do not check next wakeup time */
        if (t->t_flags & OS_TASK_FLAG_NO_TIMEOUT) {
            break;
        }
        next = TAILQ_NEXT(t, t_os_list);
        if (OS_TIME_TICK_GEQ(now, t->t_next_wakeup)) {
            os_trace_task_start_ready(t->t_taskid);
            os_sched_wakeup(t);
        } else {
            break;
        }
        t = next;
    }

    OS_EXIT_CRITICAL(sr);
}

/*
 * Return the number of ticks until the first sleep timer expires.If there are
 * no such tasks then return OS_TIMEOUT_NEVER instead.
 */
os_time_t
os_sched_wakeup_ticks(os_time_t now)
{
    os_time_t rt;
    struct os_task *t;

    OS_ASSERT_CRITICAL();

    t = TAILQ_FIRST(&g_os_sleep_list);
    if (t == NULL || (t->t_flags & OS_TASK_FLAG_NO_TIMEOUT)) {
        rt = OS_TIMEOUT_NEVER;
    } else if (OS_TIME_TICK_GEQ(t->t_next_wakeup, now)) {
        rt = t->t_next_wakeup - now;
    } else {
        rt = 0;     /* wakeup time was in the past */
    }
    return (rt);
}

/**
 * os sched next task
 *
 * Returns the task that we should be running. This is the task at the head
 * of the run list.
 *
 * NOTE: if you want to guarantee that the os run list does not change after
 * calling this function you have to call it with interrupts disabled.
 *
 * @return struct os_task*
 */
struct os_task *
os_sched_next_task(void)
{
    return (TAILQ_FIRST(&g_os_run_list));
}

/**
 * os sched resort
 *
 * Resort a task that is in the ready list as its priority has
 * changed. If the task is not in the ready state, there is
 * nothing to do.
 *
 * @param t Pointer to task to insert back into ready to run
 *          list.
 *
 * NOTE: this function expects interrupts to be disabled so they
 * are not disabled here.
 */
void
os_sched_resort(struct os_task *t)
{
    if (t->t_state == OS_TASK_READY) {
        TAILQ_REMOVE(&g_os_run_list, t, t_os_list);
        os_sched_insert(t);
    }
}

/**
 *   @} OSSched
 * @} OSKernel
 */
