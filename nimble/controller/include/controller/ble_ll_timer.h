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

static inline void
ble_ll_timer_wrap_usecs(uint32_t *ticks, uint8_t *usecs)
{
    if (*usecs >= 31) {
        *usecs -= 31;
        *ticks = *ticks + 1;
    }
}

static inline void
ble_ll_timer_add(uint32_t *ticks_d, uint8_t *usecs_d, uint32_t ticks_n,
                 uint8_t usecs_n)
{
    *ticks_d += ticks_n;
    *usecs_d += usecs_n;

    ble_ll_timer_wrap_usecs(ticks_d, usecs_d);
}

static inline uint32_t
ble_ll_timer_t2u(uint32_t ticks)
{
    return os_cputime_ticks_to_usecs(ticks);
}

static inline uint32_t
ble_ll_timer_u2t_fast(uint32_t usecs, uint8_t *rem_usecs)
{
    uint32_t ticks;

    if (usecs <= 31249) {
        ticks = (usecs * 137439) / 4194304;
    } else {
        ticks = os_cputime_usecs_to_ticks(usecs);
    }

    if (rem_usecs) {
        usecs -= os_cputime_ticks_to_usecs(ticks);
        *rem_usecs = usecs;
        ble_ll_timer_wrap_usecs(&ticks, rem_usecs);
    }

    return ticks;
}

static inline uint32_t
ble_ll_timer_u2t_rem(uint32_t usecs, uint8_t *rem_usecs)
{
#if MYNEWT_VAL(BLE_LL_TIMER_FAST_U2T)
    return ble_ll_timer_u2t_fast(usecs, rem_usecs);
#else
    uint32_t ticks;

    ticks = os_cputime_usecs_to_ticks(usecs);

    usecs -= os_cputime_ticks_to_usecs(ticks);
    *rem_usecs = usecs;
    ble_ll_timer_wrap_usecs(&ticks, rem_usecs);

    return ticks;
#endif
}


static inline uint32_t
ble_ll_timer_u2t(uint32_t usecs)
{
    return os_cputime_usecs_to_ticks(usecs);
}

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_LL_TIMER_ */
