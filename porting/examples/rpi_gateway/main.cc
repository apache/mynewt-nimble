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
#include <stdbool.h>
#include <stdint.h>

#include <pthread.h>
#include <nimble/nimble_npl.h>
#include "nimble/nimble_port.h"
#include "console/console.h"

#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/ans/ble_svc_ans.h"
#include "services/ias/ble_svc_ias.h"
#include "services/lls/ble_svc_lls.h"
#include "services/tps/ble_svc_tps.h"

static struct ble_npl_task s_task_host;
static struct ble_npl_task s_task_hci;
static struct ble_npl_task s_task_mqtt;

extern void mosquitto_mqtt_init(void *param);

extern "C" {
	void nimble_host_task(void *param);
	void ble_hci_sock_ack_handler(void *param);
	void ble_hci_sock_init(void);
	void ble_store_ram_init(void);
}

#define TASK_DEFAULT_PRIORITY       1
#define TASK_DEFAULT_STACK          NULL
#define TASK_DEFAULT_STACK_SIZE     1024

static void *ble_hci_sock_task(void *param)
{
    console_printf("%s\n", "ble_hci_sock_task");
    ble_hci_sock_ack_handler(param);
    return NULL;
}

static void *ble_host_task(void *param)
{
    console_printf("%s\n", "ble_host_task");
    nimble_host_task(param);
    return NULL;
}

static void *mqtt_task(void *param)
{
	mosquitto_mqtt_init(param);
	return NULL;
}

//int main(int argc,char **argv)
int main(void)
{
    ble_hci_sock_init();
    nimble_port_init();

    /* This example provides GATT Alert service */
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_lls_init();

    /* XXX Need to have template for store */
    ble_store_ram_init();

    console_printf("%s\n", "init done");

    ble_npl_task_init(&s_task_hci, "hci_sock", ble_hci_sock_task,
                      NULL, TASK_DEFAULT_PRIORITY, BLE_NPL_WAIT_FOREVER,
                      TASK_DEFAULT_STACK, TASK_DEFAULT_STACK_SIZE);

    /* Create task which handles default event queue for host stack. */
    ble_npl_task_init(&s_task_host, "ble_host", ble_host_task,
                      NULL, TASK_DEFAULT_PRIORITY + 1, BLE_NPL_WAIT_FOREVER,
                      TASK_DEFAULT_STACK, TASK_DEFAULT_STACK_SIZE);

    /* Create task which handles default event queue for host stack. */
	ble_npl_task_init(&s_task_mqtt, "mqtt", mqtt_task,
					  NULL, TASK_DEFAULT_PRIORITY + 2, BLE_NPL_WAIT_FOREVER,
					  TASK_DEFAULT_STACK, TASK_DEFAULT_STACK_SIZE);

    int ret = 0;
    pthread_exit(&ret);

    while (true)
    {
        pthread_yield();
    }
}
