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
#ifndef __UTIL_STATS_H__
#define __UTIL_STATS_H__

#include <stdint.h>
#include "syscfg/syscfg.h"
#include "os/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

struct stats_name_map {
    uint16_t snm_off;
    char *snm_name;
} __attribute__((packed));

struct stats_hdr {
    char *s_name;
    uint8_t s_size;
    uint8_t s_cnt;
    uint16_t s_pad1;
#if MYNEWT_VAL(STATS_NAMES)
    const struct stats_name_map *s_map;
    int s_map_cnt;
#endif
    STAILQ_ENTRY(stats_hdr) s_next;
};

#define STATS_SECT_DECL(__name)                                         \
    struct stats_ ## __name
#define STATS_SECT_END };

#define STATS_SECT_START(__name)                                        \
    STATS_SECT_DECL(__name) {
#define STATS_SECT_VAR(__var)

#define STATS_HDR(__sectname)

#define STATS_SECT_ENTRY(__var)
#define STATS_SECT_ENTRY16(__var)
#define STATS_SECT_ENTRY32(__var)
#define STATS_SECT_ENTRY64(__var)
#define STATS_RESET(__var)

#define STATS_SIZE_INIT_PARMS(__sectvarname, __size) 0, 0

#define STATS_INC(__sectvarname, __var)
#define STATS_INCN(__sectvarname, __var, __n)
#define STATS_CLEAR(__sectvarname, __var)

#define STATS_NAME_START(__name)
#define STATS_NAME(__name, __entry)
#define STATS_NAME_END(__name)
#define STATS_NAME_INIT_PARMS(__name) NULL, 0

#define stats_init(...) 0
#define stats_register(name, shdr) 0
#define stats_init_and_reg(...) 0
#define stats_reset(shdr)

#ifdef __cplusplus
}
#endif

#endif /* __UTIL_STATS_H__ */
