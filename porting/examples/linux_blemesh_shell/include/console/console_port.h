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

#ifndef __CONSOLE_PORT_H__
#define __CONSOLE_PORT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "nimble/nimble_npl.h"
struct os_eventq;

void console_set_queues(struct os_eventq *avail, struct os_eventq *lines);

void console_set_event_cb(ble_npl_event_fn *cb);

int console_init();

#ifdef __cplusplus
}
#endif

#endif
