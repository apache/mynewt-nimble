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
#include <stdlib.h>
#include "os/mynewt.h"
#include "hal/hal_os_tick.h"
#include "nct_if.h"
#include "irq_ctrl.h"
#include "bs_tracing.h"
#include "mcu/nrf52833_bsim.h"

/*
 * Each Mynewt task runs in its own pthread managed by native simulator
 * threading emulation (NCT) which makes sure only one of them runs at a time.
 */

struct bsim_task {
    int th_idx;
    void *arg;
};

static void *g_nct_st;
static int g_pend_ctx_sw;

static void
bsim_task_entry(void *payload)
{
    struct os_task *t = payload;
    struct bsim_task *bt = t->t_arg;

    t->t_func(bt->arg);

    assert(0);
}

void
bsim_os_arch_init(void)
{
    g_nct_st = nct_init(bsim_task_entry);
}

void
bsim_os_arch_cleanup(void)
{
    nct_clean_up(g_nct_st);
}

static inline int
task_th_idx(struct os_task *t)
{
    return ((struct bsim_task *)t->t_arg)->th_idx;
}

os_stack_t *
os_arch_task_stack_init(struct os_task *t, os_stack_t *stack_top, int size)
{
    struct bsim_task *bt;

    bt = calloc(1, sizeof(*bt));
    assert(bt);

    bt->arg = t->t_arg;
    t->t_arg = bt;

    bt->th_idx = nct_new_thread(g_nct_st, t);
    nct_thread_name_set(g_nct_st, bt->th_idx, t->t_name);

    return stack_top;
}

os_error_t
os_arch_os_init(void)
{
    TAILQ_INIT(&g_os_run_list);
    TAILQ_INIT(&g_os_sleep_list);

    os_init_idle_task();

    return OS_OK;
}

os_error_t
os_arch_os_start(void)
{
    struct os_task *t;

    os_tick_init(OS_TICKS_PER_SEC, (1 << __NVIC_PRIO_BITS) - 1);

    t = os_sched_next_task();
    assert(t);
    os_sched_set_current_task(t);

    g_os_started = 1;

    /* This terminates calling thread */
    nct_first_thread_start(g_nct_st, task_th_idx(t));

    return OS_OK;
}

static void
bsim_ctx_sw(void)
{
    struct os_task *t;

    g_pend_ctx_sw = 0;

    t = os_sched_next_task();
    assert(t);

    if (t == g_current_task) {
        return;
    }

    os_sched_set_current_task(t);
    nct_swap_threads(g_nct_st, task_th_idx(t));
}

void
os_arch_ctx_sw(struct os_task *t)
{
    os_sched_ctx_sw_hook(t);
    g_pend_ctx_sw = 1;
}

void
bsim_os_arch_ctx_sw_check(void)
{
    if (g_pend_ctx_sw && !hw_irq_ctrl_get_current_lock(BSIM_CPU_N) &&
        !os_arch_in_isr()) {
        bsim_ctx_sw();
    }
}

os_sr_t
os_arch_save_sr(void)
{
    return hw_irq_ctrl_change_lock(BSIM_CPU_N, 1);
}

void
os_arch_restore_sr(os_sr_t osr)
{
    hw_irq_ctrl_change_lock(BSIM_CPU_N, osr);

    if (!osr) {
        bsim_os_arch_ctx_sw_check();
    }
}

int
os_arch_in_critical(void)
{
    return hw_irq_ctrl_get_current_lock(BSIM_CPU_N);
}

static void __attribute__((noreturn))
bsim_assert(const char *file, unsigned int line, const char *func, const char *e)
{
#if MYNEWT_VAL(OS_ASSERT_CB)
    os_assert_cb();
#endif
    /* This terminates simulation */
    bs_trace_error_time_line("Assertion failed: %s:%u %s%s%s\n", file ? file : "?",
                             line, func ? func : "", e ? " " : "", e ? e : "");
    abort();
}

/* Used by assert() from host libc (glibc) */
void
__assert_fail(const char *assertion, const char *file, unsigned int line,
              const char *function)
{
    bsim_assert(file, line, function, assertion);
}

/* Used by assert() from newlib */
void
__assert_func(const char *file, int line, const char *func, const char *e)
{
    bsim_assert(file, line, func, e);
}
