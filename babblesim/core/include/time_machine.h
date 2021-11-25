/*
 * Copyright (c) 2017 Oticon A/S
 * Copyright (c) 2021 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _TIME_MACHINE_H
#define _TIME_MACHINE_H

#include "bs_types.h"

#ifdef __cplusplus
extern "C"{
#endif

extern bs_time_t now;
bs_time_t tm_get_abs_time(void);
bs_time_t tm_get_hw_time(void);
bs_time_t tm_hw_time_to_abs_time(bs_time_t hwtime);
bs_time_t tm_abs_time_to_hw_time(bs_time_t abstime);

void tm_reset_hw_times(void);

void tm_find_next_timer_to_trigger(void);
bs_time_t tm_get_next_timer_abstime(void);

void tm_update_last_phy_sync_time(bs_time_t abs_time);

void tm_set_phy_max_resync_offset(bs_time_t offset_in_us);

void tm_run_forever(void);

void tm_sleep_until_hw_time(bs_time_t hw_time);

void tm_sleep_until_abs_time(bs_time_t time);

void tm_start(void);

void tm_tick(void);

void tm_tick_limited(bs_time_t max_time_diff);

#ifdef __cplusplus
}
#endif

#endif
