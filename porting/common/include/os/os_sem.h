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

#ifndef _OS_SEM_H_
#define _OS_SEM_H_

#include "os/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

struct os_sem
{
    SLIST_HEAD(, os_task) sem_head;     /* chain of waiting tasks */
    uint16_t    _pad;
    uint16_t    sem_tokens;             /* # of tokens */
};

/* 
  XXX: NOTES
    -> Should we add a magic number or flag to the semaphore structure so
    that we know that this is a semaphore? We can use it for double checking
    that a proper semaphore was passed in to the API.
    -> What debug information should we add to this structure? Who last
    acquired the semaphore? File/line where it was released?
    -> Should we add a name to the semaphore?
    -> Should we add a "os_sem_inspect() api, like ucos?
*/ 

/* Create a semaphore */
os_error_t os_sem_init(struct os_sem *sem, uint16_t tokens);

/* Release a semaphore */
os_error_t os_sem_release(struct os_sem *sem);

/* Pend (wait) for a semaphore */
os_error_t os_sem_pend(struct os_sem *sem, uint32_t timeout);

/* Get current semaphore's count */
static inline uint16_t os_sem_get_count(struct os_sem *sem)
{
    return sem->sem_tokens;
}

#ifdef __cplusplus
}
#endif

#endif  /* _OS_SEM_H_ */
