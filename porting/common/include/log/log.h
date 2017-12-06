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
#ifndef __SYS_LOG_STUB_H__
#define __SYS_LOG_STUB_H__

#include <inttypes.h>
#include "syscfg/syscfg.h"
#include "log/ignore.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_DEBUG(__l, __mod, ...) IGNORE(__VA_ARGS__)
#define LOG_INFO(__l, __mod, ...) IGNORE(__VA_ARGS__)
#define LOG_WARN(__l, __mod, ...) IGNORE(__VA_ARGS__)
#define LOG_ERROR(__l, __mod, ...) IGNORE(__VA_ARGS__)
#define LOG_CRITICAL(__l, __mod, ...) IGNORE(__VA_ARGS__)

#define LOG_LEVEL_DEBUG         0
#define LOG_LEVEL_INFO          1
#define LOG_LEVEL_WARN          2
#define LOG_LEVEL_ERROR         3
#define LOG_LEVEL_CRITICAL      4
/* Up to 7 custom log levels. */
#define LOG_LEVEL_MAX           UINT8_MAX
#define LOG_SYSLEVEL            UINT8_MAX

/* Logging medium */
#define LOG_STORE_CONSOLE    1
#define LOG_STORE_CBMEM      2
#define LOG_STORE_FCB        3

/* Global log info */
struct log_info {
    uint32_t li_next_index;
    uint8_t li_version;
};

struct log_info g_log_info;

struct log {
};

struct log_handler {
};

static inline int
log_register(char *name, struct log *log, const struct log_handler *h,
             void *arg, uint8_t level)
{
    return 0;
}

static inline void
log_init(void)
{
}

#define log_printf(...)

/*
 * Dummy handler exports.
 */
const struct log_handler log_console_handler;
const struct log_handler log_cbmem_handler;
const struct log_handler log_fcb_handler;

#ifdef __cplusplus
}
#endif

#endif /* __SYS_LOG_STUB_H__ */
