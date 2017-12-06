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
#include "syscfg/syscfg.h"

#include <assert.h>
#include "os/os_mutex.h"
#include "os/os_heap.h"

/**
 * @addtogroup OSKernel
 * @{
 *   @defgroup OSGeneral
 *   @{
 */

#if MYNEWT_VAL(OS_SCHEDULING)
static struct os_mutex os_malloc_mutex;
#endif

static void
os_malloc_lock(void)
{
#if MYNEWT_VAL(OS_SCHEDULING)
    int rc;

    if (g_os_started) {
        rc = os_mutex_pend(&os_malloc_mutex, 0xffffffff);
        assert(rc == 0);
    }
#endif
}

static void
os_malloc_unlock(void)
{
#if MYNEWT_VAL(OS_SCHEDULING)
    int rc;

    if (g_os_started) {
        rc = os_mutex_release(&os_malloc_mutex);
        assert(rc == 0);
    }
#endif
}

/**
 * Operating system level malloc().   This ensures that a safe malloc occurs
 * within the context of the OS.  Depending on platform, the OS may rely on
 * libc's malloc() implementation, which is not guaranteed to be thread-safe.
 * This malloc() will always be thread-safe.
 *
 * @param size The number of bytes to allocate
 *
 * @return A pointer to the memory region allocated.
 */
void *
os_malloc(size_t size)
{
    void *ptr;

    os_malloc_lock();
    ptr = malloc(size);
    os_malloc_unlock();

    return ptr;
}

/**
 * Operating system level free().  See description of os_malloc() for reasoning.
 *
 * Free's memory allocated by malloc.
 *
 * @param mem The memory to free.
 */
void
os_free(void *mem)
{
    os_malloc_lock();
    free(mem);
    os_malloc_unlock();
}

/**
 * Operating system level realloc(). See description of os_malloc() for reasoning.
 *
 * Reallocates the memory at ptr, to be size contiguouos bytes.
 *
 * @param ptr A pointer to the memory to allocate
 * @param size The number of contiguouos bytes to allocate at that location
 *
 * @return A pointer to memory of size, or NULL on failure to allocate
 */
void *
os_realloc(void *ptr, size_t size)
{
    void *new_ptr;

    os_malloc_lock();
    new_ptr = realloc(ptr, size);
    os_malloc_unlock();

    return new_ptr;
}

/**
 *   @} OSGeneral
 * @} OS Kernel
 */
