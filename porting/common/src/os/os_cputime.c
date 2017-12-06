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

#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "os/os.h"
#include "os/os_cputime.h"

/**
 * @addtogroup OSKernel Operating System Kernel
 * @{
 *   @defgroup OSCPUTime High Resolution Timers
 *   @{
 */

#if defined(OS_CPUTIME_FREQ_HIGH)
struct os_cputime_data g_os_cputime;
#endif

/**
 * os cputime init
 *
 * Initialize the cputime module. This must be called after os_init is called
 * and before any other timer API are used. This should be called only once
 * and should be called before the hardware timer is used.
 *
 * @param clock_freq The desired cputime frequency, in hertz (Hz).
 *
 * @return int 0 on success; -1 on error.
 */
int
os_cputime_init(uint32_t clock_freq)
{
    int rc;

    /* Set the ticks per microsecond. */
#if defined(OS_CPUTIME_FREQ_HIGH)
    g_os_cputime.ticks_per_usec = clock_freq / 1000000U;
#endif
    rc = hal_timer_config(MYNEWT_VAL(OS_CPUTIME_TIMER_NUM), clock_freq);
    return rc;
}

/**
 * os cputime delay ticks
 *
 * Wait until the number of ticks has elapsed. This is a blocking delay.
 *
 * @param ticks The number of ticks to wait.
 */
void
os_cputime_delay_ticks(uint32_t ticks)
{
    uint32_t until;

    until = os_cputime_get32() + ticks;
    while ((int32_t)(os_cputime_get32() - until) < 0) {
        /* Loop here till finished */
    }
}

#if !defined(OS_CPUTIME_FREQ_PWR2)
/**
 * os cputime delay nsecs
 *
 * Wait until 'nsecs' nanoseconds has elapsed. This is a blocking delay.
 *
 * @param nsecs The number of nanoseconds to wait.
 */
void
os_cputime_delay_nsecs(uint32_t nsecs)
{
    uint32_t ticks;

    ticks = os_cputime_nsecs_to_ticks(nsecs);
    os_cputime_delay_ticks(ticks);
}
#endif

/**
 * os cputime delay usecs
 *
 * Wait until 'usecs' microseconds has elapsed. This is a blocking delay.
 *
 * @param usecs The number of usecs to wait.
 */
void
os_cputime_delay_usecs(uint32_t usecs)
{
    uint32_t ticks;

    ticks = os_cputime_usecs_to_ticks(usecs);
    os_cputime_delay_ticks(ticks);
}

/**
 * os cputime timer init
 *
 * @param timer The timer to initialize. Cannot be NULL.
 * @param fp    The timer callback function. Cannot be NULL.
 * @param arg   Pointer to data object to pass to timer.
 */
void
os_cputime_timer_init(struct hal_timer *timer, hal_timer_cb fp, void *arg)
{
    assert(timer != NULL);
    assert(fp != NULL);

    hal_timer_set_cb(MYNEWT_VAL(OS_CPUTIME_TIMER_NUM), timer, fp, arg);
}

/**
 * os cputime timer start
 *
 * Start a cputimer that will expire at 'cputime'. If cputime has already
 * passed, the timer callback will still be called (at interrupt context).
 *
 * NOTE: This must be called when the timer is stopped.
 *
 * @param timer     Pointer to timer to start. Cannot be NULL.
 * @param cputime   The cputime at which the timer should expire.
 *
 * @return int 0 on success; EINVAL if timer already started or timer struct
 *         invalid
 *
 */
int
os_cputime_timer_start(struct hal_timer *timer, uint32_t cputime)
{
    int rc;

    rc = hal_timer_start_at(timer, cputime);
    return rc;
}

/**
 * os cputimer timer relative
 *
 * Sets a cpu timer that will expire 'usecs' microseconds from the current
 * cputime.
 *
 * NOTE: This must be called when the timer is stopped.
 *
 * @param timer Pointer to timer. Cannot be NULL.
 * @param usecs The number of usecs from now at which the timer will expire.
 *
 * @return int 0 on success; EINVAL if timer already started or timer struct
 *         invalid
 */
int
os_cputime_timer_relative(struct hal_timer *timer, uint32_t usecs)
{
    int rc;
    uint32_t cputime;

    assert(timer != NULL);

    cputime = os_cputime_get32() + os_cputime_usecs_to_ticks(usecs);
    rc = hal_timer_start_at(timer, cputime);

    return rc;
}

/**
 * os cputime timer stop
 *
 * Stops a cputimer from running. The timer is removed from the timer queue
 * and interrupts are disabled if no timers are left on the queue. Can be
 * called even if timer is not running.
 *
 * @param timer Pointer to cputimer to stop. Cannot be NULL.
 */
void
os_cputime_timer_stop(struct hal_timer *timer)
{
    hal_timer_stop(timer);
}

/**
 * os cputime get32
 *
 * Returns current value of cputime.
 *
 * @return uint32_t cputime
 */
uint32_t
os_cputime_get32(void)
{
    uint32_t cpu_time;

    cpu_time = hal_timer_read(MYNEWT_VAL(OS_CPUTIME_TIMER_NUM));
    return cpu_time;
}

/**
 *   @} OSCPUTime
 * @} OSKernel
 */
