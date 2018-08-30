/* Bluetooth: Mesh Generic OnOff, Generic Level, Lighting & Vendor Models
 *
 * Copyright (c) 2018 Vikrant More
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common.h"
#include "ble_mesh.h"
#include "device_composition.h"
#include "state_binding.h"
#include "transition.h"

u8_t enable_transition;
u8_t default_tt;

struct ble_npl_callout onoff_work;
struct ble_npl_callout level_lightness_work;
struct ble_npl_callout level_temp_work;
struct ble_npl_callout light_lightness_actual_work;
struct ble_npl_callout light_lightness_linear_work;
struct ble_npl_callout light_ctl_work;
struct ble_npl_callout light_ctl_temp_work;

struct ble_npl_callout onoff_transition_timer;
struct ble_npl_callout level_lightness_transition_timer;
struct ble_npl_callout level_temp_transition_timer;
struct ble_npl_callout light_lightness_actual_transition_timer;
struct ble_npl_callout light_lightness_linear_transition_timer;
struct ble_npl_callout light_ctl_transition_timer;
struct ble_npl_callout light_ctl_temp_transition_timer;

static u32_t tt_counter_calculator(u8_t *tt, u32_t *cal_tt)
{
	u8_t steps_multiplier, resolution;
	u32_t tt_counter;

	resolution = ((*tt) >> 6);
	steps_multiplier = (*tt) & 0x3F;

	switch (resolution) {
	case 0:	/* 100ms */
		*cal_tt = steps_multiplier * 100;
		break;
	case 1:	/* 1 second */
		*cal_tt = steps_multiplier * 1000;
		break;
	case 2:	/* 10 seconds */
		*cal_tt = steps_multiplier * 10000;
		break;
	case 3:	/* 10 minutes */
		*cal_tt = steps_multiplier * 600000;
		break;
	}

	tt_counter = ((float) *cal_tt / 100);

	if (tt_counter > DEVICE_SPECIFIC_RESOLUTION) {
		tt_counter = DEVICE_SPECIFIC_RESOLUTION;
	}

	if (tt_counter != 0) {
		*cal_tt = *cal_tt / tt_counter;
	}

	return tt_counter;
}

void onoff_tt_values(struct generic_onoff_state *state)
{
	state->tt_counter = tt_counter_calculator(&state->tt, &state->cal_tt);
}

void level_tt_values(struct generic_level_state *state)
{
	u32_t tt_counter;

	tt_counter = tt_counter_calculator(&state->tt, &state->cal_tt);
	state->tt_counter = tt_counter;

	if (tt_counter == 0) {
		tt_counter = 1;
	}

	state->tt_delta = ((float) (state->level - state->target_level) /
			   tt_counter);
}

void delta_level_tt_values(struct generic_level_state *state)
{
	u32_t tt_counter;

	tt_counter = tt_counter_calculator(&state->tt, &state->cal_tt);
	state->tt_counter_delta = tt_counter;

	if (tt_counter == 0) {
		tt_counter = 1;
	}

	state->tt_delta = ((float) state->last_delta / tt_counter);

	state->tt_delta *= -1;
}

void move_level_tt_values(struct generic_level_state *state)
{
	u32_t tt_counter;

	tt_counter = tt_counter_calculator(&state->tt, &state->cal_tt);
	state->tt_counter_move = tt_counter;

	if (tt_counter == 0) {
		tt_counter = 1;
	}

	state->tt_delta = ((float) state->last_delta / tt_counter);

	state->tt_delta *= -1;
}

void light_lightnes_actual_tt_values(struct light_lightness_state *state)
{
	u32_t tt_counter;

	tt_counter = tt_counter_calculator(&state->tt, &state->cal_tt);
	state->tt_counter_actual = tt_counter;

	if (tt_counter == 0) {
		tt_counter = 1;
	}

	state->tt_delta_actual =
		((float) (state->actual - state->target_actual) /
		 tt_counter);
}

void light_lightnes_linear_tt_values(struct light_lightness_state *state)
{
	u32_t tt_counter;

	tt_counter = tt_counter_calculator(&state->tt, &state->cal_tt);
	state->tt_counter_linear = tt_counter;

	if (tt_counter == 0) {
		tt_counter = 1;
	}

	state->tt_delta_linear =
		((float) (state->linear - state->target_linear) /
		 tt_counter);
}

void light_ctl_tt_values(struct light_ctl_state *state)
{
	u32_t tt_counter;

	tt_counter = tt_counter_calculator(&state->tt, &state->cal_tt);
	state->tt_counter = tt_counter;

	if (tt_counter == 0) {
		tt_counter = 1;
	}

	state->tt_lightness_delta =
		((float) (state->lightness - state->target_lightness) /
		 tt_counter);

	state->tt_temp_delta =
		((float) (state->temp - state->target_temp) /
		 tt_counter);

	state->tt_duv_delta =
		((float) (state->delta_uv - state->target_delta_uv) /
		 tt_counter);
}

void light_ctl_temp_tt_values(struct light_ctl_state *state)
{
	u32_t tt_counter;

	tt_counter = tt_counter_calculator(&state->tt, &state->cal_tt);
	state->tt_counter_temp = tt_counter;

	if (tt_counter == 0) {
		tt_counter = 1;
	}

	state->tt_temp_delta = ((float) (state->temp - state->target_temp) /
				tt_counter);

	state->tt_duv_delta =
		((float) (state->delta_uv - state->target_delta_uv) /
		 tt_counter);
}

/* Timers related handlers & threads (Start) */
static void onoff_work_handler(struct ble_npl_event *work)
{
	struct generic_onoff_state *state = &gen_onoff_srv_root_user_data;

	if (enable_transition != ONOFF_TT) {
		ble_npl_callout_stop(&onoff_transition_timer);
		return;
	}

	if (state->tt_counter != 0) {
		state->tt_counter--;

		if (state->target_onoff == STATE_ON) {
			state->onoff = STATE_ON;

			state_binding(ONOFF, IGNORE_TEMP);
			update_light_state();

			enable_transition = DISABLE_TRANSITION;
		}
	}

	if (state->tt_counter == 0) {
		state->onoff = state->target_onoff;

		state_binding(ONOFF, IGNORE_TEMP);
		update_light_state();

		ble_npl_callout_stop(&onoff_transition_timer);
	}
}

static void level_lightness_work_handler(struct ble_npl_event *work)
{
	u32_t *tt_counter;
	struct generic_level_state *state = &gen_level_srv_root_user_data;

	tt_counter = NULL;

	switch (enable_transition) {
	case LEVEL_TT:
		tt_counter = &state->tt_counter;
		break;
	case LEVEL_TT_DELTA:
		tt_counter = &state->tt_counter_delta;
		break;
	case LEVEL_TT_MOVE:
		tt_counter = &state->tt_counter_move;
		break;
	default:
		ble_npl_callout_stop(&level_lightness_transition_timer);
		return;
	}

	if (*tt_counter != 0) {
		s32_t lightness;

		(*tt_counter)--;

		lightness = state->level - state->tt_delta;

		if (lightness > INT16_MAX) {
			lightness = INT16_MAX;
		} else if (lightness < INT16_MIN) {
			lightness = INT16_MIN;
		}

		if (state->level != lightness) {
			state->level = lightness;

			state_binding(LEVEL, IGNORE_TEMP);
			update_light_state();
		} else {
			enable_transition = DISABLE_TRANSITION;
		}
	}

	if (*tt_counter == 0) {
		state->level = state->target_level;

		state_binding(LEVEL, IGNORE_TEMP);
		update_light_state();

		ble_npl_callout_stop(&level_lightness_transition_timer);
	}
}

static void level_temp_work_handler(struct ble_npl_event *work)
{
	u32_t *tt_counter;
	struct generic_level_state *state = &gen_level_srv_s0_user_data;

	tt_counter = NULL;

	switch (enable_transition) {
	case LEVEL_TT:
		tt_counter = &state->tt_counter;
		break;
	case LEVEL_TT_DELTA:
		tt_counter = &state->tt_counter_delta;
		break;
	case LEVEL_TT_MOVE:
		tt_counter = &state->tt_counter_move;
		break;
	default:
		ble_npl_callout_stop(&level_temp_transition_timer);
		return;
	}

	if (*tt_counter != 0) {
		s32_t temp;

		(*tt_counter)--;

		temp = state->level - state->tt_delta;

		if (temp > INT16_MAX) {
			temp = INT16_MAX;
		} else if (temp < INT16_MIN) {
			temp = INT16_MIN;
		}

		if (state->level != temp) {
			state->level = temp;

			state_binding(IGNORE, LEVEL_TEMP);
			update_light_state();
		} else {
			enable_transition = DISABLE_TRANSITION;
		}
	}

	if (*tt_counter == 0) {
		state->level = state->target_level;

		state_binding(IGNORE, LEVEL_TEMP);
		update_light_state();

		ble_npl_callout_stop(&level_temp_transition_timer);
	}
}

static void light_lightness_actual_work_handler(struct ble_npl_event *work)
{
	struct light_lightness_state *state = &light_lightness_srv_user_data;

	if (enable_transition != LIGTH_LIGHTNESS_ACTUAL_TT) {
		ble_npl_callout_stop(&light_lightness_actual_transition_timer);
		return;
	}

	if (state->tt_counter_actual != 0) {
		u32_t actual;

		state->tt_counter_actual--;

		actual = state->actual - state->tt_delta_actual;

		if (state->actual != actual) {
			state->actual = actual;

			state_binding(ACTUAL, IGNORE_TEMP);
			update_light_state();
		} else {
			enable_transition = DISABLE_TRANSITION;
		}
	}

	if (state->tt_counter_actual == 0) {
		state->actual = state->target_actual;

		state_binding(ACTUAL, IGNORE_TEMP);
		update_light_state();

		ble_npl_callout_stop(&light_lightness_actual_transition_timer);
	}
}

static void light_lightness_linear_work_handler(struct ble_npl_event *work)
{
	struct light_lightness_state *state = &light_lightness_srv_user_data;

	if (enable_transition != LIGTH_LIGHTNESS_LINEAR_TT) {
		ble_npl_callout_stop(&light_lightness_linear_transition_timer);
		return;
	}

	if (state->tt_counter_linear != 0) {
		u32_t linear;

		state->tt_counter_linear--;

		linear = state->linear - state->tt_delta_linear;

		if (state->linear != linear) {
			state->linear = linear;

			state_binding(LINEAR, IGNORE_TEMP);
			update_light_state();
		} else {
			enable_transition = DISABLE_TRANSITION;
		}
	}

	if (state->tt_counter_linear == 0) {
		state->linear = state->target_linear;

		state_binding(LINEAR, IGNORE_TEMP);
		update_light_state();

		ble_npl_callout_stop(&light_lightness_linear_transition_timer);
	}
}

static void light_ctl_work_handler(struct ble_npl_event *work)
{
	struct light_ctl_state *state = &light_ctl_srv_user_data;

	if (enable_transition != LIGTH_CTL_TT) {
		ble_npl_callout_stop(&light_ctl_transition_timer);
		return;
	}

	if (state->tt_counter != 0) {
		u32_t lightness, temp;
		s32_t delta_uv;

		state->tt_counter--;

		/* Lightness */
		lightness = state->lightness - state->tt_lightness_delta;

		/* Temperature */
		temp = state->temp - state->tt_temp_delta;

		/* Delta_UV */
		delta_uv = state->delta_uv - state->tt_duv_delta;

		if (delta_uv > INT16_MAX) {
			delta_uv = INT16_MAX;
		} else if (delta_uv < INT16_MIN) {
			delta_uv = INT16_MIN;
		}

		if (state->lightness != lightness || state->temp != temp ||
		    state->delta_uv != delta_uv) {
			state->lightness = lightness;
			state->temp = temp;
			state->delta_uv = delta_uv;

			state_binding(CTL, CTL_TEMP);
			update_light_state();
		} else {
			enable_transition = DISABLE_TRANSITION;
		}
	}

	if (state->tt_counter == 0) {
		state->lightness = state->target_lightness;
		state->temp = state->target_temp;
		state->delta_uv = state->target_delta_uv;

		state_binding(CTL, CTL_TEMP);
		update_light_state();

		ble_npl_callout_stop(&light_ctl_transition_timer);
	}
}

static void light_ctl_temp_work_handler(struct ble_npl_event *work)
{
	struct light_ctl_state *state = &light_ctl_srv_user_data;

	if (enable_transition != LIGHT_CTL_TEMP_TT) {
		ble_npl_callout_stop(&light_ctl_temp_transition_timer);
		return;
	}

	if (state->tt_counter_temp != 0) {
		s32_t delta_uv;
		u32_t temp;

		state->tt_counter_temp--;

		/* Temperature */
		temp = state->temp - state->tt_temp_delta;

		/* Delta UV */
		delta_uv = state->delta_uv - state->tt_duv_delta;

		if (delta_uv > INT16_MAX) {
			delta_uv = INT16_MAX;
		} else if (delta_uv < INT16_MIN) {
			delta_uv = INT16_MIN;
		}

		if (state->temp != temp || state->delta_uv != delta_uv) {
			state->temp = temp;
			state->delta_uv = delta_uv;

			state_binding(IGNORE, CTL_TEMP);
			update_light_state();
		} else {
			enable_transition = DISABLE_TRANSITION;
		}
	}

	if (state->tt_counter_temp == 0) {
		state->temp = state->target_temp;
		state->delta_uv = state->target_delta_uv;

		state_binding(IGNORE, CTL_TEMP);
		update_light_state();

		ble_npl_callout_stop(&light_ctl_temp_transition_timer);
	}
}

static void onoff_tt_handler(struct ble_npl_event *ev)
{
	struct generic_onoff_state *state = ble_npl_event_get_arg(ev);

	assert(state != NULL);
	ble_npl_callout_reset(&onoff_work, 0);
	ble_npl_callout_reset(&onoff_transition_timer, K_MSEC(state->cal_tt));
}

static void level_lightness_tt_handler(struct ble_npl_event *ev)
{
	struct generic_level_state *state = ble_npl_event_get_arg(ev);

	assert(state != NULL);
	ble_npl_callout_reset(&level_lightness_work, 0);
	ble_npl_callout_reset(&level_lightness_transition_timer, K_MSEC(state->cal_tt));
}

static void level_temp_tt_handler(struct ble_npl_event *ev)
{
	struct generic_level_state *state = ble_npl_event_get_arg(ev);

	assert(state != NULL);
	ble_npl_callout_reset(&level_temp_work, 0);
	ble_npl_callout_reset(&level_temp_transition_timer, K_MSEC(state->cal_tt));
}

static void light_lightness_actual_tt_handler(struct ble_npl_event *ev)
{
	struct light_lightness_state *state = ble_npl_event_get_arg(ev);

	assert(state != NULL);
	ble_npl_callout_reset(&light_lightness_actual_work, 0);
	ble_npl_callout_reset(&light_lightness_actual_transition_timer, K_MSEC(state->cal_tt));
}

static void light_lightness_linear_tt_handler(struct ble_npl_event *ev)
{
	struct light_lightness_state *state = ble_npl_event_get_arg(ev);

	assert(state != NULL);
	ble_npl_callout_reset(&light_lightness_linear_work, 0);
	ble_npl_callout_reset(&light_lightness_linear_transition_timer, K_MSEC(state->cal_tt));
}

static void light_ctl_tt_handler(struct ble_npl_event *ev)
{
	struct light_ctl_state *state = ble_npl_event_get_arg(ev);

	assert(state != NULL);
	ble_npl_callout_reset(&light_ctl_work, 0);
	ble_npl_callout_reset(&light_ctl_transition_timer, K_MSEC(state->cal_tt));
}

static void light_ctl_temp_tt_handler(struct ble_npl_event *ev)
{
	struct light_ctl_state *state = ble_npl_event_get_arg(ev);

	assert(state != NULL);
	ble_npl_callout_reset(&light_ctl_temp_work, 0);
	ble_npl_callout_reset(&light_ctl_temp_transition_timer, K_MSEC(state->cal_tt));
}
/* Timers related handlers & threads (End) */

/* Messages handlers (Start) */
void onoff_handler(struct generic_onoff_state *state)
{
	enable_transition = ONOFF_TT;

	ble_npl_callout_set_arg(&onoff_transition_timer, state);
	ble_npl_callout_reset(&onoff_transition_timer, K_MSEC(5 * state->delay));
}

void level_lightness_handler(struct generic_level_state *state)
{
	ble_npl_callout_set_arg(&level_lightness_transition_timer, state);
	ble_npl_callout_reset(&level_lightness_transition_timer,
		      K_MSEC(5 * state->delay));
}

void level_temp_handler(struct generic_level_state *state)
{
	ble_npl_callout_set_arg(&level_temp_transition_timer, state);
	ble_npl_callout_reset(&level_temp_transition_timer, K_MSEC(5 * state->delay));
}

void light_lightness_actual_handler(struct light_lightness_state *state)
{
	enable_transition = LIGTH_LIGHTNESS_ACTUAL_TT;

	ble_npl_callout_set_arg(&light_lightness_actual_transition_timer, state);
	ble_npl_callout_reset(&light_lightness_actual_transition_timer,
		      K_MSEC(5 * state->delay));
}

void light_lightness_linear_handler(struct light_lightness_state *state)
{
	enable_transition = LIGTH_LIGHTNESS_LINEAR_TT;

	ble_npl_callout_set_arg(&light_lightness_linear_transition_timer, state);
	ble_npl_callout_reset(&light_lightness_linear_transition_timer,
		      K_MSEC(5 * state->delay));
}

void light_ctl_handler(struct light_ctl_state *state)
{
	enable_transition = LIGTH_CTL_TT;

	ble_npl_callout_set_arg(&light_ctl_transition_timer, state);
	ble_npl_callout_reset(&light_ctl_transition_timer, K_MSEC(5 * state->delay));
}

void light_ctl_temp_handler(struct light_ctl_state *state)
{
	enable_transition = LIGHT_CTL_TEMP_TT;

	ble_npl_callout_set_arg(&light_ctl_temp_transition_timer, state);
	ble_npl_callout_reset(&light_ctl_temp_transition_timer,
		      K_MSEC(5 * state->delay));
}
/* Messages handlers (End) */

void transition_timers_init(void)
{
	ble_npl_callout_init(&onoff_work, ble_npl_eventq_dflt_get(),
			     onoff_work_handler, NULL);

	ble_npl_callout_init(&level_lightness_work, ble_npl_eventq_dflt_get(),
			     level_lightness_work_handler, NULL);
	ble_npl_callout_init(&level_temp_work, ble_npl_eventq_dflt_get(),
			     level_temp_work_handler, NULL);

	ble_npl_callout_init(&light_lightness_actual_work, ble_npl_eventq_dflt_get(),
			     light_lightness_actual_work_handler, NULL);
	ble_npl_callout_init(&light_lightness_linear_work, ble_npl_eventq_dflt_get(),
			     light_lightness_linear_work_handler, NULL);

	ble_npl_callout_init(&light_ctl_work, ble_npl_eventq_dflt_get(),
			     light_ctl_work_handler, NULL);
	ble_npl_callout_init(&light_ctl_temp_work, ble_npl_eventq_dflt_get(),
			     light_ctl_temp_work_handler, NULL);

	ble_npl_callout_init(&onoff_transition_timer, ble_npl_eventq_dflt_get(),
			     onoff_tt_handler, NULL);

	ble_npl_callout_init(&level_lightness_transition_timer, ble_npl_eventq_dflt_get(),
			     level_lightness_tt_handler, NULL);
	ble_npl_callout_init(&level_temp_transition_timer, ble_npl_eventq_dflt_get(),
			     level_temp_tt_handler, NULL);

	ble_npl_callout_init(&light_lightness_actual_transition_timer, ble_npl_eventq_dflt_get(),
			     light_lightness_actual_tt_handler, NULL);
	ble_npl_callout_init(&light_lightness_linear_transition_timer, ble_npl_eventq_dflt_get(),
			     light_lightness_linear_tt_handler, NULL);

	ble_npl_callout_init(&light_ctl_transition_timer, ble_npl_eventq_dflt_get(),
			     light_ctl_tt_handler, NULL);
	ble_npl_callout_init(&light_ctl_temp_transition_timer, ble_npl_eventq_dflt_get(),
			     light_ctl_temp_tt_handler, NULL);

	ble_npl_callout_init(&light_ctl_temp_transition_timer, ble_npl_eventq_dflt_get(),
			     light_ctl_temp_tt_handler, NULL);

}