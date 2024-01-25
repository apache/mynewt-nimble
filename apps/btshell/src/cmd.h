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

#ifndef CMD_H
#define CMD_H

#include <inttypes.h>
#include "host/ble_uuid.h"
#include <parse_arg/parse_arg.h>

int parse_eddystone_url(char *full_url, uint8_t *out_scheme, char *out_body,
                        uint8_t *out_body_len, uint8_t *out_suffix);

int cmd_parse_conn_start_end(uint16_t *out_conn, uint16_t *out_start,
                             uint16_t *out_end);

int parse_dev_addr(const char *prefix, const struct parse_arg_kv_pair *addr_types,
                   ble_addr_t *addr);

int cmd_parse_addr(const char *prefix, ble_addr_t *addr);

const char *cmd_addr_type_str(uint8_t type);

void cmd_init(void);

#endif
