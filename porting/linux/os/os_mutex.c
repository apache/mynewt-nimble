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
#include <stdint.h>
#include <string.h>
#include "os/os.h"

#include <pthread.h>

os_error_t
os_mutex_init(struct os_mutex *mu)
{
    if (!mu) {
        return OS_INVALID_PARM;
    }

    pthread_mutexattr_t muAttr;
    pthread_mutexattr_settype(&muAttr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&mu->lock, &muAttr);

    return OS_OK;
}

os_error_t
os_mutex_release(struct os_mutex *mu)
{
    if (!mu) return OS_INVALID_PARM;

    if (pthread_mutex_unlock(&mu->lock)) {
        return OS_BAD_MUTEX;
    }

    return OS_OK;
}

os_error_t
os_mutex_pend(struct os_mutex *mu, uint32_t timeout)
{
    if (!mu) return OS_INVALID_PARM;

    assert(&mu->lock);

    struct timespec wait;
    wait.tv_sec  = timeout / 1000;
    wait.tv_nsec = (timeout % 1000) * 1000000;

    if (pthread_mutex_timedlock(&mu->lock, &wait)) {
        return OS_TIMEOUT;
    }

    return OS_OK;
}
