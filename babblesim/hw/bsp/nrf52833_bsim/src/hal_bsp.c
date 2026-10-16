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
#include <stddef.h>
#include "os/mynewt.h"
#include "hal/hal_bsp.h"
#include "hal/hal_flash.h"
#include "hal/hal_system.h"
#include "bsp/bsp.h"

const struct hal_flash *
hal_bsp_flash_dev(uint8_t id)
{
    return NULL;
}

const struct hal_bsp_mem_dump *
hal_bsp_core_dump(int *area_cnt)
{
    *area_cnt = 0;
    return NULL;
}

int
hal_bsp_power_state(int state)
{
    return 0;
}

uint32_t
hal_bsp_get_nvic_priority(int irq_num, uint32_t pri)
{
    return pri;
}

void
hal_bsp_init(void)
{
    hal_system_clock_start();
}
