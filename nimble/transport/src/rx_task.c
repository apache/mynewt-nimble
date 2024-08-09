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

#include <assert.h>
#include <syscfg/syscfg.h>
#if MYNEWT
#include <os/os_task.h>
#endif
#include <os/os_mbuf.h>
#include <nimble/transport.h>
#include <nimble/nimble_npl.h>

#if !defined(MYNEWT) || MYNEWT_VAL(BLE_TRANSPORT_RX_TASK_STACK_SIZE)

static ble_transport_rx_func_t *rx_func;
static void *rx_func_arg;

#if MYNEWT
OS_TASK_STACK_DEFINE(rx_stack, MYNEWT_VAL(BLE_TRANSPORT_RX_TASK_STACK_SIZE));
static struct os_task rx_task;
#endif

static struct ble_npl_eventq rx_eventq;
static struct ble_npl_event rx_event;

static void
rx_event_func(struct ble_npl_event *ev)
{
    rx_func(rx_func_arg);
}

static void
rx_task_func(void *arg)
{
    struct ble_npl_event *ev;

    ble_npl_eventq_init(&rx_eventq);

    while (1) {
        ev = ble_npl_eventq_get(&rx_eventq, BLE_NPL_TIME_FOREVER);
        ble_npl_event_run(ev);
    }
}

void
ble_transport_rx_register(ble_transport_rx_func_t *func, void *arg)
{
    assert(func && !rx_func);

    rx_func = func;
    rx_func_arg = arg;

    ble_npl_event_init(&rx_event, rx_event_func, NULL);

#ifdef MYNEWT
    os_task_init(&rx_task, "hci_rx_task", rx_task_func, NULL,
                 MYNEWT_VAL(BLE_TRANSPORT_RX_TASK_PRIO), OS_WAIT_FOREVER,
                 rx_stack, MYNEWT_VAL(BLE_TRANSPORT_RX_TASK_STACK_SIZE));
#endif
}

void
ble_transport_rx(void)
{
    ble_npl_eventq_put(&rx_eventq, &rx_event);
}

#endif
