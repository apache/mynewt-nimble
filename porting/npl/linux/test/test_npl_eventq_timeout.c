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

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "nimble/nimble_npl.h"
#include "test_util.h"

static struct ble_npl_eventq eventq;
static struct ble_npl_event event;

static void
watchdog(int signo)
{
    static const char message[] = "FAILED: event queue wait did not finish\n";
    ssize_t written;

    (void)signo;
    written = write(STDERR_FILENO, message, sizeof(message) - 1);
    (void)written;
    _exit(EXIT_FAILURE);
}

static uint64_t
monotonic_ns(void)
{
    struct timespec now;

    SuccessOrQuit(clock_gettime(CLOCK_MONOTONIC, &now), "clock_gettime failed");
    return (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}

static void
test_ready(ble_npl_time_t timeout)
{
    ble_npl_eventq_put(&eventq, &event);
    VerifyOrQuit(ble_npl_event_is_queued(&event), "event not queued");
    VerifyOrQuit(ble_npl_eventq_get(&eventq, timeout) == &event,
                 "queued event not returned");
    VerifyOrQuit(!ble_npl_event_is_queued(&event), "event still marked queued");
    VerifyOrQuit(ble_npl_eventq_get(&eventq, 0) == NULL,
                 "event returned more than once");
}

static void
test_timeout(ble_npl_time_t timeout)
{
    uint64_t start;
    uint64_t elapsed;

    start = monotonic_ns();
    VerifyOrQuit(ble_npl_eventq_get(&eventq, timeout) == NULL,
                 "empty queue returned an event");
    elapsed = monotonic_ns() - start;
    VerifyOrQuit(elapsed >= (uint64_t)timeout * 1000000,
                 "queue wait returned before its deadline");

    /* A timeout must leave the queue unlocked and usable. */
    test_ready(0);
}

static void *
delayed_put(void *arg)
{
    struct timespec delay = { .tv_sec = 0, .tv_nsec = 20000000 };
    int rc;

    (void)arg;
    do {
        rc = nanosleep(&delay, &delay);
    } while (rc == -1 && errno == EINTR);
    SuccessOrQuit(rc, "nanosleep failed");

    ble_npl_eventq_put(&eventq, &event);
    return NULL;
}

static void
test_wakeup(ble_npl_time_t timeout)
{
    pthread_t producer;
    uint64_t start;

    SuccessOrQuit(pthread_create(&producer, NULL, delayed_put, NULL),
                  "producer creation failed");
    start = monotonic_ns();
    VerifyOrQuit(ble_npl_eventq_get(&eventq, timeout) == &event,
                 "waiting consumer did not receive event");
    if (timeout != BLE_NPL_TIME_FOREVER) {
        VerifyOrQuit(monotonic_ns() - start < (uint64_t)timeout * 1000000,
                     "consumer waited until deadline despite queued event");
    }
    SuccessOrQuit(pthread_join(producer, NULL), "producer join failed");
    VerifyOrQuit(!ble_npl_event_is_queued(&event), "event still marked queued");
    VerifyOrQuit(ble_npl_eventq_get(&eventq, 0) == NULL,
                 "event returned more than once");
}

int
main(void)
{
    struct sigaction action = { 0 };

    /* Bound failures even when a finite timeout incorrectly waits forever. */
    action.sa_handler = watchdog;
    SuccessOrQuit(sigemptyset(&action.sa_mask), "sigemptyset failed");
    SuccessOrQuit(sigaction(SIGALRM, &action, NULL), "sigaction failed");
    alarm(10);

    ble_npl_eventq_init(&eventq);
    ble_npl_event_init(&event, NULL, NULL);

    VerifyOrQuit(ble_npl_eventq_get(&eventq, 0) == NULL,
                 "nonblocking read of empty queue failed");
    test_ready(0);
    test_ready(20);
    test_ready(BLE_NPL_TIME_FOREVER);
    test_timeout(20);
    test_timeout(1250);
    test_wakeup(5000);
    test_wakeup(BLE_NPL_TIME_FOREVER);

    alarm(0);
    printf("All event queue timeout tests passed\n");
    return PASS;
}
