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
#ifndef __CONSOLE_H__
#define __CONSOLE_H__

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

struct os_eventq;

struct console_input {
    char line[0];
};

typedef void (*console_rx_cb)(void);
typedef int (*console_append_char_cb)(char *line, uint8_t byte);
typedef void (*completion_cb)(char *str, console_append_char_cb cb);

static int inline
console_is_init(void)
{
    return 0;
}

static int inline
console_init(console_rx_cb rx_cb)
{
    return 0;
}

static void inline
console_write(const char *str, int cnt)
{
}

static int inline
console_read(char *str, int cnt, int *newline)
{
    *newline = 0;
    return 0;
}

static void inline
console_blocking_mode(void)
{
}

static void inline
console_non_blocking_mode(void)
{
}

static void inline
console_echo(int on)
{
}

static int inline console_printf(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)));

static int inline
console_printf(const char *fmt, ...)
{
    return 0;
}

static void inline
console_set_queues(struct os_eventq *avail_queue,
                   struct os_eventq *cmd_queue)
{
}

static void inline
console_set_completion_cb(void (*completion)(char *str, console_append_char_cb cb))
{
}

static int inline
console_handle_char(uint8_t byte)
{
    return 0;
}

static int inline
console_out(int character)
{
    return 0;
}

#define console_is_midline  (0)

#ifdef __cplusplus
}
#endif

#endif /* __CONSOLE__ */

