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

#include <inttypes.h>
#include <string.h>

#include "hal/hal_bsp.h"

#include "hal_native_priv.h"

#ifndef min
#define min(a, b) ((a)<(b)?(a):(b))
#endif

static uint8_t hal_hw_id[HAL_BSP_MAX_ID_LEN];
static uint8_t hal_hw_id_len;

int
hal_bsp_hw_id_len(void)
{
    if (hal_hw_id_len != 0) {
        return hal_hw_id_len;
    } else {
        return HAL_BSP_MAX_ID_LEN;
    }
}

/*
 * This can be used as the unique hardware identifier for the platform, as
 * it's supposed to be unique for this particular MCU.
 */
int
hal_bsp_hw_id(uint8_t *id, int max_len)
{
    if (hal_hw_id_len) {
        if (max_len > hal_hw_id_len) {
            max_len = hal_hw_id_len;
        }
        memcpy(id, hal_hw_id, max_len);
        return max_len;
    }
    if (max_len > HAL_BSP_MAX_ID_LEN) {
        max_len = HAL_BSP_MAX_ID_LEN;
    }
    memset(id, 0x42, max_len);
    return max_len;
}

void
hal_bsp_set_hw_id(const uint8_t *id, int len)
{
    if (len > HAL_BSP_MAX_ID_LEN) {
        len = HAL_BSP_MAX_ID_LEN;
    }
    hal_hw_id_len = len;
    memcpy(hal_hw_id, id, len);
}
