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

/**
 * This file contains code that is shared by both sim implementations (signals
 * and no-signals).
 */

#include "os/mynewt.h"

#include <hal/hal_bsp.h>

#ifdef __APPLE__
#define _XOPEN_SOURCE
#endif

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>
#include <assert.h>
#include <hal/hal_os_tick.h>
#include "os/sim.h"
#include "sim_priv.h"

#define sim_setjmp(__jb) sigsetjmp(__jb, 0)
#define sim_longjmp(__jb, __ret) siglongjmp(__jb, __ret)

pid_t sim_pid;

void
sim_switch_tasks(void)
{
    struct os_task *t, *next_t;
    struct stack_frame *sf;
    int rc;

    OS_ASSERT_CRITICAL();

    t = os_sched_get_current_task();
    next_t = os_sched_next_task();
    if (t == next_t) {
        /*
         * Context switch not needed - just return.
         */
        return;
    }

    if (t) {
        sf = (struct stack_frame *) t->t_stackptr;

        rc = sim_setjmp(sf->sf_jb);
        if (rc != 0) {
            OS_ASSERT_CRITICAL();
            return;
        }
    }

    os_sched_ctx_sw_hook(next_t);

    os_sched_set_current_task(next_t);

    sf = (struct stack_frame *) next_t->t_stackptr;
    sim_longjmp(sf->sf_jb, 1);
}

void
sim_tick(void)
{
    struct timeval time_now, time_diff;
    int ticks;

    static struct timeval time_last;
    static int time_inited;

    OS_ASSERT_CRITICAL();

    if (!time_inited) {
        gettimeofday(&time_last, NULL);
        time_inited = 1;
    }

    gettimeofday(&time_now, NULL);
    if (timercmp(&time_now, &time_last, <)) {
        /*
         * System time going backwards.
         */
        time_last = time_now;
    } else {
        timersub(&time_now, &time_last, &time_diff);

        ticks = time_diff.tv_sec * OS_TICKS_PER_SEC;
        ticks += time_diff.tv_usec / OS_USEC_PER_TICK;

        /*
         * Update 'time_last' but account for the remainder usecs that did not
         * contribute towards whole 'ticks'.
         */
        time_diff.tv_sec = 0;
        time_diff.tv_usec %= OS_USEC_PER_TICK;
        timersub(&time_now, &time_diff, &time_last);

        os_time_advance(ticks);
    }
}

#define OS_TICK_PRIO 7

static void
sim_start_timer(void)
{
    /* Intitialize and start system clock timer */
    os_tick_init(OS_TICKS_PER_SEC, OS_TICK_PRIO);
}

static void
sim_stop_timer(void)
{
    struct itimerval it;
    int rc;

    memset(&it, 0, sizeof(it));

    rc = setitimer(ITIMER_REAL, &it, NULL);
    assert(rc == 0);
}

/*
 * Called from 'os_arch_frame_init()' when setjmp returns indirectly via
 * longjmp. The return value of setjmp is passed to this function as 'rc'.
 */
void
sim_task_start(struct stack_frame *sf, int rc)
{
    struct os_task *task;

    /*
     * Interrupts are disabled when a task starts executing. This happens in
     * two different ways:
     * - via sim_os_start() for the first task.
     * - via os_sched() for all other tasks.
     *
     * Enable interrupts before starting the task.
     */
    OS_EXIT_CRITICAL(0);

    task = sf->sf_task;
    task->t_func(task->t_arg);

    /* A task handler should never return. */
    assert(0);
}

os_stack_t *
sim_task_stack_init(struct os_task *t, os_stack_t *stack_top, int size)
{
    struct stack_frame *sf;

    sf = (struct stack_frame *) ((uint8_t *) stack_top - sizeof(*sf));
    sf->sf_task = t;

    os_arch_frame_init(sf);

    return ((os_stack_t *)sf);
}

os_error_t
sim_os_start(void)
{
    struct stack_frame *sf;
    struct os_task *t;
    os_sr_t sr;

    /*
     * Disable interrupts before enabling any interrupt sources. Pending
     * interrupts will be recognized when the first task starts executing.
     */
    OS_ENTER_CRITICAL(sr);
    assert(sr == 0);

    /* Enable the interrupt sources */
    sim_start_timer();

    t = os_sched_next_task();
    os_sched_set_current_task(t);

    g_os_started = 1;

    sf = (struct stack_frame *) t->t_stackptr;
    sim_longjmp(sf->sf_jb, 1);

    return 0;
}

/**
 * Stops the tick timer and clears the "started" flag.  This function is only
 * implemented for sim.
 */
void
sim_os_stop(void)
{
    sim_stop_timer();
    sim_signals_cleanup();
    g_os_started = 0;
}

os_error_t
sim_os_init(void)
{
    sim_pid = getpid();
    g_current_task = NULL;

    STAILQ_INIT(&g_os_task_list);
    TAILQ_INIT(&g_os_run_list);
    TAILQ_INIT(&g_os_sleep_list);

    sim_signals_init();

    os_init_idle_task();

    return OS_OK;
}
