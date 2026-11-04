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

#include <stdint.h>
#include "os/mynewt.h"
#include "nrfx.h"
#include "hal/nrf_clock.h"
#include "mcu/nrf52_clock.h"

static uint8_t g_hfxo_refcnt;

int
nrf52_clock_hfxo_request(void)
{
    os_sr_t sr;
    int started = 0;

    OS_ENTER_CRITICAL(sr);
    assert(g_hfxo_refcnt < 0xff);
    if (g_hfxo_refcnt == 0) {
        nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_HFCLKSTART);
        started = 1;
    }
    g_hfxo_refcnt++;
    OS_EXIT_CRITICAL(sr);

    return started;
}

int
nrf52_clock_hfxo_release(void)
{
    os_sr_t sr;
    int stopped = 0;

    OS_ENTER_CRITICAL(sr);
    assert(g_hfxo_refcnt != 0);
    g_hfxo_refcnt--;
    if (g_hfxo_refcnt == 0) {
        nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_HFCLKSTOP);
        stopped = 1;
    }
    OS_EXIT_CRITICAL(sr);

    return stopped;
}
