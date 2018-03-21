
#include "syscfg/syscfg.h"

#if MYNEWT_VAL(BLE_MESH_SHELL_MODELS)

#include "mesh/mesh.h"
#include "bsp.h"
#include "pwm/pwm.h"
#include "light_model.h"

struct pwm_dev *pwm0;
struct pwm_dev *pwm1;
struct pwm_dev *pwm2;
struct pwm_dev *pwm3;
static uint16_t top_val;

static u8_t gen_onoff_state;
static s16_t gen_level_state;

static void light_set_lightness(u8_t percentage)
{
	int rc;

	uint16_t pwm_val = (uint16_t) (percentage * top_val / 100);

	rc = pwm_enable_duty_cycle(pwm0, 0, pwm_val);
	assert(rc == 0);
	rc = pwm_enable_duty_cycle(pwm1, 0, pwm_val);
	assert(rc == 0);
	rc = pwm_enable_duty_cycle(pwm2, 0, pwm_val);
	assert(rc == 0);
	rc = pwm_enable_duty_cycle(pwm3, 0, pwm_val);
	assert(rc == 0);
}

static void update_light_state(void)
{
	int level = gen_level_state;

	if (level > 100) {
		level = 100;
	}
	if (level < 0) {
		level = 0;
	}

	if (gen_onoff_state == 0) {
		level = 0;
	}

	light_set_lightness((uint8_t) level);
}

int light_model_gen_onoff_get(struct bt_mesh_model *model, u8_t *state)
{
	*state = gen_onoff_state;
	return 0;
}

int light_model_gen_onoff_set(struct bt_mesh_model *model, u8_t state)
{
	gen_onoff_state = state;
	update_light_state();
	return 0;
}

int light_model_gen_level_get(struct bt_mesh_model *model, s16_t *level)
{
	*level = gen_level_state;
	return 0;
}

int light_model_gen_level_set(struct bt_mesh_model *model, s16_t level)
{
	gen_level_state = level;
	if (gen_level_state > 0) {
		gen_onoff_state = 1;
	}
	if (gen_level_state <= 0) {
		gen_onoff_state = 0;
	}
	update_light_state();
	return 0;
}

static struct pwm_dev_interrupt_cfg led1_conf = {
	.cfg = {
		.pin = LED_1,
		.inverted = true,
		.n_cycles = 0,
		.interrupts_cfg = true,
	},
	.int_prio = 3,
};

static struct pwm_dev_interrupt_cfg led2_conf = {
	.cfg = {
		.pin = LED_2,
		.inverted = true,
		.n_cycles = 0,
		.interrupts_cfg = true,
	},
	.int_prio = 3,
};

static struct pwm_dev_interrupt_cfg led3_conf = {
	.cfg = {
		.pin = LED_3,
		.inverted = true,
		.n_cycles = 0,
		.interrupts_cfg = true,
	},
	.int_prio = 3,
};

static struct pwm_dev_interrupt_cfg led4_conf = {
	.cfg = {
		.pin = LED_4,
		.inverted = true,
		.n_cycles = 0,
		.interrupts_cfg = true,
	},
	.int_prio = 3,
};

int pwm_init(void)
{
	int rc;

	led1_conf.seq_end_data = &led1_conf;
	led2_conf.seq_end_data = &led2_conf;
	led3_conf.seq_end_data = &led3_conf;
	led4_conf.seq_end_data = &led4_conf;

	pwm0 = (struct pwm_dev *) os_dev_open("pwm0", 0, NULL);
	assert(pwm0);
	pwm1 = (struct pwm_dev *) os_dev_open("pwm1", 0, NULL);
	assert(pwm1);
	pwm2 = (struct pwm_dev *) os_dev_open("pwm2", 0, NULL);
	assert(pwm2);
	pwm3 = (struct pwm_dev *) os_dev_open("pwm3", 0, NULL);
	assert(pwm3);

	/* set the PWM frequency */
	pwm_set_frequency(pwm0, 1000);
	pwm_set_frequency(pwm1, 1000);
	pwm_set_frequency(pwm2, 1000);
	pwm_set_frequency(pwm3, 1000);
	top_val = (uint16_t) pwm_get_top_value(pwm0);

	rc = pwm_chan_config(pwm0, 0, (struct pwm_chan_cfg*) &led1_conf);
	assert(rc == 0);
	rc = pwm_chan_config(pwm1, 0, (struct pwm_chan_cfg*) &led2_conf);
	assert(rc == 0);
	rc = pwm_chan_config(pwm2, 0, (struct pwm_chan_cfg*) &led3_conf);
	assert(rc == 0);
	rc = pwm_chan_config(pwm3, 0, (struct pwm_chan_cfg*) &led4_conf);
	assert(rc == 0);

	update_light_state();

	return rc;
}
#endif

int light_model_init(void)
{
#if MYNEWT_VAL(BLE_MESH_SHELL_MODELS)
	return pwm_init();
#else
	return 0;
#endif
}

