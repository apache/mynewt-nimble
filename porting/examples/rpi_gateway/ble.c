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
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "nimble/nimble_port.h"
#include "mesh/mesh.h"
#include "mesh/porting.h"
#include "console/console.h"
#include "bt_mesh_node.h"

#define TASK_DEFAULT_PRIORITY       1
#define TASK_DEFAULT_STACK          NULL
#define TASK_DEFAULT_STACK_SIZE     1024

static void ble_on_reset(int reason)
{
    console_printf("BLE Reset\n");
}

static struct ble_npl_task nimble_mesh_adv_task_h;

static void *adv_thread(void *param)
{
    mesh_adv_thread(param);

    return NULL;
}

static void
ble_adv_task_init(void)
{
    int rc;

    rc = ble_npl_task_init(&nimble_mesh_adv_task_h, "mesh_adv", adv_thread,
                      NULL, TASK_DEFAULT_PRIORITY + 2, BLE_NPL_WAIT_FOREVER,
                      TASK_DEFAULT_STACK, TASK_DEFAULT_STACK_SIZE);
    assert(rc == 0);
}

static void ble_on_sync(void)
{
    console_printf("Bluetooth initialized");

    bt_mesh_node_init();

    ble_adv_task_init();
}

void
nimble_host_task(void *param)
{
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    bt_mesh_register_gatt();

    printf("%s\n", __func__);

    nimble_port_run();
}
