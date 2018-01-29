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

#ifndef _OS_ARCH_H
#define _OS_ARCH_H

#include <stdint.h>
#include "os/os_atomic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int os_sr_t;
typedef int os_stack_t;

#define OS_TICKS_PER_SEC   (1000)

#define OS_ALIGNMENT       (sizeof(uintptr_t))
#define OS_STACK_ALIGNMENT (16)

#define OS_STACK_ALIGN(__nmemb)					\
	    (OS_ALIGN(((__nmemb) * 16), OS_STACK_ALIGNMENT))

#define OS_ENTER_CRITICAL(unused) do { (void)unused; os_atomic_begin(); } while (0)
#define OS_EXIT_CRITICAL(unused) do { (void)unused; os_atomic_end(); } while (0)

#ifdef __cplusplus
}
#endif

#endif /* _OS_ARCH_H */
