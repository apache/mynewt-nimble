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

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>


os_error_t
os_sem_init(struct os_sem *sem, uint16_t tokens)
{
    if (!sem)
    {
        return OS_INVALID_PARM;
    }

    sem_init(&sem->lock, 0, tokens);

    return OS_OK;
}

os_error_t
os_sem_release(struct os_sem *sem)
{
    int err;

    if (!sem)
    {
        return OS_INVALID_PARM;
    }

    err = sem_post(&sem->lock);

    return (err) ? OS_ERROR : OS_OK;
}

os_error_t
os_sem_pend(struct os_sem *sem, uint32_t timeout)
{
    if (!sem) return OS_INVALID_PARM;

    int err = 0;
    struct timespec wait;
    err = clock_gettime(CLOCK_REALTIME, &wait);
    if (err) return OS_ERROR;

    wait.tv_sec  += timeout / 1000;
    wait.tv_nsec += (timeout % 1000) * 1000000;

    if (timeout == OS_WAIT_FOREVER)
    {
        err = sem_wait(&sem->lock);
    }
    else
    {
        if (sem_timedwait(&sem->lock, &wait))
        {
	    assert(errno == ETIMEDOUT);
	    return OS_TIMEOUT;
	}
    }

    return (err) ? OS_ERROR : OS_OK;
}

uint16_t
os_sem_get_count(struct os_sem *sem)
{
    int count;
    assert(sem);
    assert(&sem->lock);
    sem_getvalue(&sem->lock, &count);
    return count;
}
