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

#include "os/os.h"
#include "nimble/nimble_npl.h"

ble_npl_error_t
ble_npl_mutex_init(struct ble_npl_mutex *mu)
{
    if (!mu) {
        return BLE_NPL_INVALID_PARAM;
    }

    pthread_mutexattr_init(&mu->attr);
    pthread_mutexattr_settype(&mu->attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mu->lock, &mu->attr);
    mu->depth = 0;

    return BLE_NPL_OK;
}

ble_npl_error_t
ble_npl_mutex_release(struct ble_npl_mutex *mu)
{
    if (!mu) {
        return BLE_NPL_INVALID_PARAM;
    }

    /* Update depth while mutex is still held by current thread */
    if (ble_npl_mutex_locked_by_cur_task(mu)) {
        __atomic_store_n(&mu->depth, mu->depth - 1, __ATOMIC_RELEASE);
    }

    if (pthread_mutex_unlock(&mu->lock)) {
        return BLE_NPL_BAD_MUTEX;
    }

    return BLE_NPL_OK;
}

ble_npl_error_t
ble_npl_mutex_pend(struct ble_npl_mutex *mu, uint32_t timeout)
{
    int err;

    if (!mu) {
        return BLE_NPL_INVALID_PARAM;
    }

    if (timeout == BLE_NPL_TIME_FOREVER) {
        err = pthread_mutex_lock(&mu->lock);
    } else {
        err = clock_gettime(CLOCK_REALTIME, &mu->wait);
        if (err) {
            return BLE_NPL_ERROR;
        }

        mu->wait.tv_sec  += timeout / 1000;
        mu->wait.tv_nsec += (timeout % 1000) * 1000000;

        /* struct timespec tv_nsec holds nanosecond and allowed range is
         * 0 - 999999999, otherwise pthread_mutex_timedlock returns EINVAL
         */
        if (mu->wait.tv_nsec >= 1000000000) {
            mu->wait.tv_sec += mu->wait.tv_nsec / 1000000000;
            mu->wait.tv_nsec = mu->wait.tv_nsec % 1000000000;
        }

        err = pthread_mutex_timedlock(&mu->lock, &mu->wait);
        if (err == ETIMEDOUT) {
            return BLE_NPL_TIMEOUT;
        }
    }

    if (err) {
        return BLE_NPL_ERROR;
    }

    __atomic_store_n(&mu->owner, pthread_self(), __ATOMIC_RELAXED);
    __atomic_store_n(&mu->depth, mu->depth + 1, __ATOMIC_RELEASE);

    return BLE_NPL_OK;
}

int
ble_npl_mutex_locked_by_cur_task(struct ble_npl_mutex *mu)
{
    /* Owner and depth are modified only by thread holding the mutex. Owner
     * is written before depth so if non-zero depth is observed, owner is
     * up to date and can match current thread only if it holds the mutex.
     * This makes it safe to call from any thread.
     */
    if (__atomic_load_n(&mu->depth, __ATOMIC_ACQUIRE) == 0) {
        return 0;
    }

    return pthread_equal(__atomic_load_n(&mu->owner, __ATOMIC_RELAXED),
                         pthread_self());
}
