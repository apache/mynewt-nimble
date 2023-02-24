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

#define _GNU_SOURCE
#include <pthread.h>
#include <syscfg/syscfg.h>
#include <os/os_task.h>
#include <hal/hal_os_tick.h>
#include <irq_ctrl.h>

static pthread_mutex_t bsim_ctx_sw_mutex = PTHREAD_MUTEX_INITIALIZER;
static int bsim_pend_sv;

struct task_info {
    pthread_t tid;
    pthread_cond_t cond;
    void *arg;
};

static void *
task_wrapper(void *arg)
{
    struct os_task *me = arg;
    struct task_info *ti = me->t_arg;

    pthread_mutex_lock(&bsim_ctx_sw_mutex);
    if (g_current_task != me) {
        pthread_cond_wait(&ti->cond, &bsim_ctx_sw_mutex);
        assert(g_current_task == me);
    }

    me->t_func(ti->arg);

    assert(0);
}

os_stack_t *
os_arch_task_stack_init(struct os_task *t, os_stack_t *stack_top, int size)
{
    struct task_info *ti;
    int err;

    ti = calloc(1, sizeof(*ti));

    pthread_cond_init(&ti->cond, NULL);
    ti->arg = t->t_arg;
    t->t_arg = ti;

    err = pthread_create(&ti->tid, NULL, task_wrapper, t);
    assert(err == 0);

    pthread_setname_np(ti->tid, t->t_name);

    return stack_top;
}

os_error_t
os_arch_os_start(void)
{
    struct os_task *next_t;
    struct task_info *ti;

    os_tick_init(OS_TICKS_PER_SEC, 7);

    next_t = os_sched_next_task();
    assert(next_t);
    os_sched_set_current_task(next_t);

    g_os_started = 1;

    ti = next_t->t_arg;
    pthread_cond_signal(&ti->cond);

    return 0;
}

os_error_t
os_arch_os_init(void)
{
    STAILQ_INIT(&g_os_task_list);
    TAILQ_INIT(&g_os_run_list);
    TAILQ_INIT(&g_os_sleep_list);

    os_init_idle_task();

    return OS_OK;
}

void
os_arch_ctx_sw(struct os_task *next_t)
{
    os_sched_ctx_sw_hook(next_t);
    bsim_pend_sv = 1;
}

static void
do_ctx_sw(void)
{
    struct os_task *next_t;
    struct os_task *me;
    struct task_info *ti, *next_ti;

    next_t = os_sched_next_task();
    assert(next_t);

    bsim_pend_sv = 0;

    assert(g_current_task);
    me = g_current_task;
    ti = me->t_arg;

    if (me == next_t) {
        return;
    }

    g_current_task = next_t;
    next_ti = g_current_task->t_arg;

    pthread_cond_signal(&next_ti->cond);
    pthread_cond_wait(&ti->cond, &bsim_ctx_sw_mutex);
    assert(g_current_task == me);
}

os_sr_t
os_arch_save_sr(void)
{
    return hw_irq_ctrl_change_lock(1);
}

void
os_arch_restore_sr(os_sr_t osr)
{
    hw_irq_ctrl_change_lock(osr);

    if (!osr && bsim_pend_sv && !os_arch_in_isr()) {
        do_ctx_sw();
    }
}

int
os_arch_in_critical(void)
{
    return hw_irq_ctrl_get_current_lock();
}

void
__assert_func(const char *file, int line, const char *func, const char *e)
{
#if MYNEWT_VAL(OS_ASSERT_CB)
    os_assert_cb();
#endif
    _Exit(1);
}
