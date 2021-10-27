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

#ifndef H_BLE_LL_TIMER_
#define H_BLE_LL_TIMER_

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t
ble_ll_timer_get(void)
{
    return os_cputime_get32();
}

static inline uint32_t
ble_ll_timer_ticks_to_usecs(uint32_t ticks)
{
    return os_cputime_ticks_to_usecs(ticks);
}

static inline uint32_t
ble_ll_timer_usecs_to_ticks(uint32_t usecs, uint8_t *rem_usecs)
{
    uint32_t ticks;

    if (usecs <= 31249) {
        ticks = (usecs * 137439) / 4194304;
    } else {
        ticks = os_cputime_usecs_to_ticks(usecs);
    }

    if (rem_usecs) {
        usecs -= os_cputime_ticks_to_usecs(ticks);
        if (usecs >= 31) {
            usecs -= 31;
            ticks++;
        }
        *rem_usecs = usecs;
    }

    return ticks;
}

static inline void
ble_ll_timer_wrap_usecs(uint32_t *ticks, uint8_t *usecs)
{
    if (*usecs >= 31) {
        *usecs -= 31;
        *ticks = *ticks + 1;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_LL_TIMER_ */
