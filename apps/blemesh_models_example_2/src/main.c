/* Bluetooth: Mesh Generic OnOff, Generic Level, Lighting & Vendor Models
 *
 * Copyright (c) 2018 Vikrant More
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bsp/bsp.h"
#include "console/console.h"
#include "hal/hal_gpio.h"

#include "common.h"
#include "ble_mesh.h"
#include "device_composition.h"
#include "publisher.h"
#include "state_binding.h"
#include "transition.h"

int button_device[] = {
	BUTTON_1,
	BUTTON_2,
	BUTTON_3,
	BUTTON_4,
};

int led_device[] = {
	LED_1,
	LED_2,
	LED_3,
	LED_4,
};

static struct ble_npl_callout button_work;

static void button_pressed(struct os_event *ev)
{
	k_work_submit(&button_work);
}

static struct os_event button_event;

static void
gpio_irq_handler(void *arg)
{
	button_event.ev_arg = arg;
	os_eventq_put(os_eventq_dflt_get(), &button_event);
}

static void gpio_init(void)
{
	/* LEDs configiuratin & setting */

	hal_gpio_init_out(led_device[0], 1);
	hal_gpio_init_out(led_device[1], 1);
	hal_gpio_init_out(led_device[2], 1);
	hal_gpio_init_out(led_device[3], 1);

	/* Buttons configiuratin & setting */

	k_work_init(&button_work, publish);

	button_event.ev_cb = button_pressed;

	hal_gpio_irq_init(button_device[0], gpio_irq_handler, NULL,
			  HAL_GPIO_TRIG_FALLING, HAL_GPIO_PULL_UP);
	hal_gpio_irq_enable(button_device[0]);

	hal_gpio_irq_init(button_device[1], gpio_irq_handler, NULL,
			  HAL_GPIO_TRIG_FALLING, HAL_GPIO_PULL_UP);
	hal_gpio_irq_enable(button_device[1]);

	hal_gpio_irq_init(button_device[2], gpio_irq_handler, NULL,
			  HAL_GPIO_TRIG_FALLING, HAL_GPIO_PULL_UP);
	hal_gpio_irq_enable(button_device[2]);

	hal_gpio_irq_init(button_device[3], gpio_irq_handler, NULL,
			  HAL_GPIO_TRIG_FALLING, HAL_GPIO_PULL_UP);
	hal_gpio_irq_enable(button_device[3]);
}

void light_default_status_init(void)
{
	/* Assume vaules are retrived from Persistence Storage (Start).
	 * These had saved by respective Setup Servers.
	 */
	gen_power_onoff_srv_user_data.onpowerup = STATE_DEFAULT;

	light_lightness_srv_user_data.light_range_min = LIGHTNESS_MIN;
	light_lightness_srv_user_data.light_range_max = LIGHTNESS_MAX;
	light_lightness_srv_user_data.def = LIGHTNESS_MAX;

	/* Following 2 values are as per specification */
	light_ctl_srv_user_data.temp_range_min = TEMP_MIN;
	light_ctl_srv_user_data.temp_range_max = TEMP_MAX;

	light_ctl_srv_user_data.temp_def = TEMP_MIN;
	/* (End) */

	/* Assume following values are retrived from Persistence
	 * Storage (Start).
	 * These values had saved before power down.
	 */
	light_lightness_srv_user_data.last = LIGHTNESS_MAX;
	light_ctl_srv_user_data.temp_last = TEMP_MIN;
	/* (End) */

	light_ctl_srv_user_data.temp = light_ctl_srv_user_data.temp_def;

	if (gen_power_onoff_srv_user_data.onpowerup == STATE_OFF) {
		gen_onoff_srv_root_user_data.onoff = STATE_OFF;
		state_binding(ONOFF, ONOFF_TEMP);
	} else if (gen_power_onoff_srv_user_data.onpowerup == STATE_DEFAULT) {
		gen_onoff_srv_root_user_data.onoff = STATE_ON;
		state_binding(ONOFF, ONOFF_TEMP);
	} else if (gen_power_onoff_srv_user_data.onpowerup == STATE_RESTORE) {
		/* Assume following values is retrived from Persistence
		 * Storage (Start).
		 * This value had saved before power down.
		 */
		gen_onoff_srv_root_user_data.onoff = STATE_ON;
		/* (End) */

		light_ctl_srv_user_data.temp =
			light_ctl_srv_user_data.temp_last;

		state_binding(ONPOWERUP, ONOFF_TEMP);
	}
}

void update_light_state(void)
{
	u8_t power, color;

	power = 100 * ((float) light_lightness_srv_user_data.actual / 65535);
	color = 100 * ((float) (gen_level_srv_s0_user_data.level + 32768)
		       / 65535);

	printk("power-> %d, color-> %d\n", power, color);

	if (gen_onoff_srv_root_user_data.onoff == STATE_ON) {
		/* LED1 On */
		hal_gpio_write(led_device[0], 0);
	} else {
		/* LED1 Off */
		hal_gpio_write(led_device[0], 1);
	}

	if (power < 50) {
		/* LED3 On */
		hal_gpio_write(led_device[2], 0);
	} else {
		/* LED3 Off */
		hal_gpio_write(led_device[2], 1);
	}

	if (color < 50) {
		/* LED4 On */
		hal_gpio_write(led_device[3], 0);
	} else {
		/* LED4 Off */
		hal_gpio_write(led_device[3], 1);
	}
}

int main(void)
{
#ifdef ARCH_sim
	mcu_sim_parse_args(argc, argv);
#endif

	/* Initialize OS */
	sysinit();

	light_default_status_init();

	transition_timers_init();

	gpio_init();

	update_light_state();

	init_pub();

	printk("Initializing...\n");

	/* Initialize the NimBLE host configuration. */
	ble_hs_cfg.reset_cb = blemesh_on_reset;
	ble_hs_cfg.sync_cb = blemesh_on_sync;
	ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

	randomize_publishers_TID();

	while (1) {
		os_eventq_run(os_eventq_dflt_get());
	}

	return 0;
}
