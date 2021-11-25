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
 * This file implements the "signals" version of sim.  This implementation uses
 * signals to perform context switches.  This is the more correct version of
 * sim: the OS tick timer will cause a high-priority task to preempt a
 * low-priority task.  Unfortunately, there are stability issues because a task
 * can be preempted while it is in the middle of a system call, potentially
 * causing deadlock or memory corruption.
 *
 * To use this version of sim, enable the MCU_NATIVE_USE_SIGNALS syscfg
 * setting.
 */

#include "os/mynewt.h"

#if MYNEWT_VAL(MCU_NATIVE_USE_SIGNALS)

#include "sim_priv.h"

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

static bool suspended;      /* process is blocked in sigsuspend() */
static sigset_t suspsigs;   /* signals delivered in sigsuspend() */
static sigset_t allsigs;
static sigset_t nosigs;

static int in_irq;
int counter = 0;

int
sig_block_irq_on()
{
	int error;

	counter++;

	error = sigprocmask(SIG_BLOCK, &allsigs, NULL);

	in_irq = 1;
	assert(error == 0);
	return 1;
}

void
sig_unblock_irq_off()
{
	int error;

	in_irq = 0;

	if (counter > 0) {
	    counter--;
	    return;
	}

	error = sigprocmask(SIG_UNBLOCK, &allsigs, NULL);
	assert(error == 0);

}

void
sim_ctx_sw(struct os_task *next_t)
{
    /*
     * gdb will stop execution of the program on most signals (e.g. SIGUSR1)
     * whereas it passes SIGURG to the process without any special settings.
     */
    kill(sim_pid, SIGURG);
}

static void
ctxsw_handler(int sig)
{
    assert(in_irq==0);
    OS_ASSERT_CRITICAL();

    /*
     * Just record that this handler was called when the process was blocked.
     * The handler will be called after sigsuspend() returns in the correct
     * order.
     */
    if (suspended) {
        sigaddset(&suspsigs, sig);
    } else {
        sim_switch_tasks();
    }
}

/*
 * Disable signals and enter a critical section.
 *
 * Returns 1 if signals were already blocked and 0 otherwise.
 */
os_sr_t
sim_save_sr(void)
{
    int error;
    sigset_t omask;

    counter++;

    error = sigprocmask(SIG_BLOCK, &allsigs, &omask);
    assert(error == 0);

    /*
     * If any one of the signals in 'allsigs' is present in 'omask' then
     * we are already inside a critical section.
     */
    return (sigismember(&omask, SIGURG));
}

void
sim_restore_sr(os_sr_t osr)
{
    int error;

    OS_ASSERT_CRITICAL();
    assert(osr == 0 || osr == 1);

    //assert(counter > 0);
    if (counter > 0) {
    	counter--;
    }

    if (osr == 1 || in_irq == 1 || counter > 0) {
        /* Exiting a nested critical section */
        return;
    }

    error = sigprocmask(SIG_UNBLOCK, &allsigs, NULL);
    assert(error == 0);
}

int
sim_in_critical(void)
{
    int error;
    sigset_t omask;

    error = sigprocmask(SIG_SETMASK, NULL, &omask);
    assert(error == 0);

    /*
     * If any one of the signals in 'allsigs' is present in 'omask' then
     * we are already inside a critical section.
     */
    return (sigismember(&omask, SIGURG));
}

static struct {
    int num;
    void (*handler)(int sig);
} signals[] = {
//    { SIGALRM, timer_handler },
    { SIGURG, ctxsw_handler },
};

#define NUMSIGS     (sizeof(signals)/sizeof(signals[0]))

void
sim_tick_idle(os_time_t ticks)
{
    int i, rc, sig;
    struct itimerval it;
    void (*handler)(int sig);

    OS_ASSERT_CRITICAL();

    if (ticks > 0) {
        /*
         * Enter tickless regime and set the timer to fire after 'ticks'
         * worth of time has elapsed.
         */
        it.it_value.tv_sec = ticks / OS_TICKS_PER_SEC;
        it.it_value.tv_usec = (ticks % OS_TICKS_PER_SEC) * OS_USEC_PER_TICK;
        it.it_interval.tv_sec = 0;
        it.it_interval.tv_usec = OS_USEC_PER_TICK;
        rc = setitimer(ITIMER_REAL, &it, NULL);
        assert(rc == 0);
    }

    suspended = true;
    sigemptyset(&suspsigs);
    sigsuspend(&nosigs);        /* Wait for a signal to wake us up */
    suspended = false;

    /*
     * Call handlers for signals delivered to the process during sigsuspend().
     * The SIGALRM handler is called before any other handlers to ensure that
     * OS time is always correct.
     */
    if (sigismember(&suspsigs, SIGALRM)) {
        sim_tick();
    }
    for (i = 0; i < NUMSIGS; i++) {
        sig = signals[i].num;
        handler = signals[i].handler;
        if (sig != SIGALRM && sigismember(&suspsigs, sig)) {
            handler(sig);
        }
    }

    if (ticks > 0) {
        /*
         * Enable the periodic timer interrupt.
         */
        it.it_value.tv_sec = 0;
        it.it_value.tv_usec = OS_USEC_PER_TICK;
        it.it_interval.tv_sec = 0;
        it.it_interval.tv_usec = OS_USEC_PER_TICK;
        rc = setitimer(ITIMER_REAL, &it, NULL);
        assert(rc == 0);
    }
}

void
sim_signals_init(void)
{
    int i, error;
    struct sigaction sa;

    sigemptyset(&nosigs);
    sigemptyset(&allsigs);
    for (i = 0; i < NUMSIGS; i++) {
        sigaddset(&allsigs, signals[i].num);
    }

    for (i = 0; i < NUMSIGS; i++) {
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = signals[i].handler;
        sa.sa_mask = allsigs;
        sa.sa_flags = SA_RESTART;
        error = sigaction(signals[i].num, &sa, NULL);
        assert(error == 0);
    }

    /*
     * We use SIGALRM as a proxy for 'allsigs' to check if we are inside
     * a critical section (for e.g. see sim_in_critical()). Make sure
     * that SIGALRM is indeed present in 'allsigs'.
     */
//    assert(sigismember(&allsigs, SIGALRM));
}

void
sim_signals_cleanup(void)
{
    int i, error;
    struct sigaction sa;

    for (i = 0; i < NUMSIGS; i++) {
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = SIG_DFL;
        error = sigaction(signals[i].num, &sa, NULL);
        assert(error == 0);
    }
}

#endif /* MYNEWT_VAL(MCU_NATIVE_USE_SIGNALS) */
