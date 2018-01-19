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

#ifndef H_SYSINIT_
#define H_SYSINIT_

#include <inttypes.h>
#include <assert.h>
#include "syscfg/syscfg.h"

#if MYNEWT_VAL(SPLIT_APPLICATION)
#include "split/split.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t sysinit_active;

void sysinit_start(void);
void sysinit_end(void);

typedef void sysinit_panic_fn(const char *file, int line, const char *func,
                              const char *expr, const char *msg);

extern sysinit_panic_fn *sysinit_panic_cb;

void sysinit_panic_set(sysinit_panic_fn *panic_fn);

#if MYNEWT_VAL(SYSINIT_PANIC_MESSAGE)

#if MYNEWT_VAL(SYSINIT_PANIC_FILE_LINE)
#define SYSINIT_PANIC_MSG(msg) sysinit_panic_cb(__FILE__, __LINE__, 0, 0, msg)
#else
#define SYSINIT_PANIC_MSG(msg) sysinit_panic_cb(0, 0, 0, 0, msg)
#endif

#else

#if MYNEWT_VAL(SYSINIT_PANIC_FILE_LINE)
#define SYSINIT_PANIC_MSG(msg) sysinit_panic_cb(__FILE__, __LINE__, 0, 0, 0)
#else
#define SYSINIT_PANIC_MSG(msg) sysinit_panic_cb(0, 0, 0, 0, 0)
#endif

#endif

#define SYSINIT_PANIC() SYSINIT_PANIC_MSG(NULL)

#define SYSINIT_PANIC_ASSERT_MSG(rc, msg) do \
{                                            \
    if (!(rc)) {                             \
        SYSINIT_PANIC_MSG(msg);              \
    }                                        \
} while (0)

#define SYSINIT_PANIC_ASSERT(rc) SYSINIT_PANIC_ASSERT_MSG(rc, NULL)

/**
 * Asserts that system initialization is in progress.  This macro is used to
 * ensure packages don't get initialized a second time after system
 * initialization has completed.
 */
#if MYNEWT_VAL(SYSINIT_CONSTRAIN_INIT)
#define SYSINIT_ASSERT_ACTIVE() assert(sysinit_active)
#else
#define SYSINIT_ASSERT_ACTIVE()
#endif

#if MYNEWT_VAL(SPLIT_LOADER)

/*** System initialization for loader (first stage of split image). */
void sysinit_loader(void);
#define sysinit() do                                                        \
{                                                                           \
    sysinit_start();                                                        \
    sysinit_loader();                                                       \
    sysinit_end();                                                          \
} while (0)

#elif MYNEWT_VAL(SPLIT_APPLICATION)

/*** System initialization for split-app (second stage of split image). */
void sysinit_app(void);
#define sysinit() do                                                        \
{                                                                           \
    /* Record that a split app is running; imgmgt needs to know this. */    \
    split_app_active_set(1);                                                \
    sysinit_start();                                                        \
    sysinit_app();                                                          \
    sysinit_end();                                                          \
} while (0)

#else

/*** System initialization for a unified image (no split). */
void sysinit_app(void);
#define sysinit() do                                                        \
{                                                                           \
    sysinit_start();                                                        \
    sysinit_app();                                                          \
    sysinit_end();                                                          \
} while (0)

#endif

#ifdef __cplusplus
}
#endif

#endif
