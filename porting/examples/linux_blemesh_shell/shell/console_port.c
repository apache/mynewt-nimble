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
#include <stdio.h>
#include "nimble/nimble_npl.h"
#include "shell/shell.h"

static struct ble_npl_task mesh_input_task;
static struct ble_npl_event shell_evt;

struct os_eventq * avail_queue;
static ble_npl_event_fn *shell_ev_cb;

void
console_set_queues(struct os_eventq *avail, struct os_eventq *lines)
{
    avail_queue = avail;
}

void
console_set_event_cb(ble_npl_event_fn *cb)
{
    shell_ev_cb = cb;
}

static void
mesh_shell_evt_add(char *cmd)
{
    struct ble_npl_event *ev = &shell_evt;

    if (ev->ev_queued) {
        printf("Wait last cmd be handled...\n");
        return;
    }

    ble_npl_event_init(ev, shell_ev_cb, cmd);

    ble_npl_eventq_put(avail_queue, ev);
}

static void *
console_input_thread(void *args)
{
    char line[128];

    while (1) {
        printf("\n\nmesh>");
        fgets(line, sizeof(line), stdin);
        int len = strlen(line);
        if (len > 0 && (line[len-1] == '\n')) {
            line[len-1] = '\0';
            --len;
        }

        if (len > 0) {
            mesh_shell_evt_add(line);
        }
    }

    return NULL;
}

int
console_init()
{
    int i;

    ble_npl_task_init(&mesh_input_task, "mesh_input", console_input_thread,
                      NULL, TASK_DEFAULT_PRIORITY, BLE_NPL_TIME_FOREVER,
                      TASK_DEFAULT_STACK, TASK_DEFAULT_STACK_SIZE);

    return 0;
}
