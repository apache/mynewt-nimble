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
#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "nrf_drv_clock.h"

#include "nimble/nimble_port.h"

static TaskHandle_t nimble_host_task_h;
static TaskHandle_t nimble_ctrl_task_h;

static uint32_t radio_isr_addr;
static uint32_t rng_isr_addr;
static uint32_t rtc0_isr_addr;

/* Some interrupt handlers required for NimBLE radio driver */

void
RADIO_IRQHandler(void)
{
    ((void (*)(void))radio_isr_addr)();
}

void
RNG_IRQHandler(void)
{
    ((void (*)(void))rng_isr_addr)();
}

void
RTC0_IRQHandler(void)
{
    ((void (*)(void))rtc0_isr_addr)();
}

/* This is called by NimBLE radio driver to set interrupt handlers */
void
npl_freertos_hw_set_isr(int irqn, uint32_t addr)
{
    switch (irqn) {
    case RADIO_IRQn:
        radio_isr_addr = addr;
        break;
    case RNG_IRQn:
        rng_isr_addr = addr;
        break;
    case RTC0_IRQn:
        rtc0_isr_addr = addr;
        break;
    }
}

uint32_t
npl_freertos_hw_enter_critical(void)
{
    uint32_t ctx = __get_PRIMASK();
    __disable_irq();
    return (ctx & 0x01);
}

void
npl_freertos_hw_exit_critical(uint32_t ctx)
{
    if (!ctx) {
        __enable_irq();
    }
}

int
main(void)
{
    extern void nimble_host_task(void *arg);

    ret_code_t err_code;

    /* Initialize clock driver for better time accuracy in FREERTOS */
    err_code = nrf_drv_clock_init();
    APP_ERROR_CHECK(err_code);

    /* Initialize NimBLE porting layer */
    nimble_port_init();

    /*
     * Create task where NimBLE host will run. It is not strictly necessary to
     * have separate task for NimBLE host, but since something needs to handle
     * default queue it is just easier to make separate task which does this.
     */
    xTaskCreate(nimble_host_task, "nh", configMINIMAL_STACK_SIZE + 400,
                NULL, tskIDLE_PRIORITY + 1, &nimble_host_task_h);

    /*
     * Create task where NimBLE LL will run. This one is required as LL has its
     * own event queue and should have highest priority. The task function is
     * provided by NimBLE and in case of FreeRTOS it does not need to be wrapped
     * since it has compatible prototype.
     */
    xTaskCreate(nimble_port_ll_task_func, "nc", configMINIMAL_STACK_SIZE + 400,
                NULL, configMAX_PRIORITIES - 1, &nimble_ctrl_task_h);

    vTaskStartScheduler();

    /* We should never reach this code */
    assert(0);

    while (true) {
    }
}
