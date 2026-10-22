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
#include <string.h>
#include "os/mynewt.h"
#include "config/config.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "pwm/pwm.h"
#include "ble_servo.h"
#include "os/os.h"
#include "sysinit/sysinit.h"
#include "host/util/util.h"

static int bleservo_gap_event(struct ble_gap_event *event, void *arg);

static uint16_t conn_handle;
static uint8_t own_addr_type;
static const char *device_name = "ble_servo";

struct servo_sm {
    struct pwm_dev *pwm_dev;
    uint16_t pwm_top_val;
    uint16_t frac_max_val;
    uint16_t frac_min_val;
    uint16_t pwm_pulse_duration;
    uint16_t angle;
    uint16_t frac;
} servo;

/* A flag that determines if user passed angle value or pwm pulse duration value. */
static bool was_angle_passed;

/* PWM servo_frac update event and callback declaration*/
static void pwm_frac_update_ev_cb(struct os_event *);
struct os_event pwm_frac_update_ev = {
    .ev_cb = pwm_frac_update_ev_cb,
};

uint16_t
get_servo_angle(void)
{
    return servo.angle;
}

uint16_t
get_servo_pwm_pulse_duration(void)
{
    return servo.pwm_pulse_duration;
}

uint16_t
angle_to_frac(uint16_t angle)
{
    return servo.frac_min_val +
           (angle * (servo.frac_max_val - servo.frac_min_val) / SERVO_MAX_ANGLE_VAL);
}

uint16_t
us_to_frac(uint16_t us)
{
    return (us * servo.pwm_top_val) / SERVO_PWM_FULL_CYCLE_DURATION;
}

uint16_t
frac_to_angle(uint16_t frac_)
{
    return (SERVO_MAX_ANGLE_VAL * frac_ - SERVO_MAX_ANGLE_VAL * servo.frac_min_val) /
           (servo.frac_max_val - servo.frac_min_val);
}

uint16_t
frac_to_us(uint16_t frac_)
{
    return (SERVO_PWM_FULL_CYCLE_DURATION * frac_) / servo.pwm_top_val;
}

/* Servo angle setter. Used in gatt_svr.c after receiving new angle value. */
void
servo_angle_setter(uint16_t gatt_value)
{
    if (gatt_value > SERVO_MAX_ANGLE_VAL) {
        servo.angle = SERVO_MAX_ANGLE_VAL;
    } else if (gatt_value < SERVO_MIN_ANGLE_VAL) {
        servo.angle = SERVO_MIN_ANGLE_VAL;
    } else {
        servo.angle = gatt_value;
    }

    was_angle_passed = 1;

    /*
     * After changing servo angle value an event
     * to update PWM fraction is put to the queue.
     */
    os_eventq_put(os_eventq_dflt_get(), &pwm_frac_update_ev);
}

/*
 * Servo PWM pulse duration setter.
 * Called in gatt_svr.c after receiving new pulse duration value.
 */
void
servo_pwm_pulse_duration_setter(uint16_t gatt_value)
{
    if (gatt_value > SERVO_MAX_PULSE_DURATION_US) {
        servo.pwm_pulse_duration = SERVO_MAX_PULSE_DURATION_US;
    } else if (gatt_value < SERVO_MIN_PULSE_DURATION_US) {
        servo.pwm_pulse_duration = SERVO_MIN_PULSE_DURATION_US;
    } else {
        servo.pwm_pulse_duration = gatt_value;
    }

    was_angle_passed = 0;

    /*
     * After changing servo PWM pulse duration value
     * an event to update PWM fraction is put to the queue.
     */
    os_eventq_put(os_eventq_dflt_get(), &pwm_frac_update_ev);
}


static void
pwm_frac_update_ev_cb(struct os_event *ev)
{
    struct os_mbuf * om;

    if (was_angle_passed == true) {
        servo.frac = angle_to_frac(servo.angle);
        servo.pwm_pulse_duration = frac_to_us(servo.frac);
    } else if (was_angle_passed == false) {
        servo.frac = us_to_frac(servo.pwm_pulse_duration);
        servo.angle = frac_to_angle(servo.frac);
    }

    pwm_set_duty_cycle(servo.pwm_dev, 0, servo.frac);
    pwm_enable(servo.pwm_dev);

    /*
     * Notifications are sent to keep client's info
     * about pwm pulse duration and angle values up to date
     */
    om = ble_hs_mbuf_from_flat(&servo.angle, sizeof(servo.angle));
    ble_gatts_notify_custom(conn_handle, get_gatt_angle_val_handle(), om);

    om = ble_hs_mbuf_from_flat(&servo.pwm_pulse_duration, sizeof(servo.pwm_pulse_duration));
    ble_gatts_notify_custom(conn_handle, get_gatt_pulse_duration_val_handle(), om);
}

void
servo_pwm_init(void)
{
    struct pwm_chan_cfg chan_conf = {
        .pin = PWM_CH_CFG_PIN,
        .inverted = PWM_CH_CFG_INV,
        .data = NULL,
    };
    struct pwm_dev_cfg dev_conf = {
        .n_cycles = 0,
        .int_prio = PWM_IRQ_PRIO,
        .cycle_handler = NULL,
        .seq_end_handler = NULL,
        .cycle_data = NULL,
        .seq_end_data = NULL,
        .data = NULL
    };

    int rc;

    servo.pwm_dev = (struct pwm_dev *)os_dev_open("pwm0", 0, NULL);
    if (!servo.pwm_dev) {
        MODLOG_DFLT(ERROR, "Device pwm0 not available\n");
        return;
    }

    pwm_configure_device(servo.pwm_dev, &dev_conf);

    rc = pwm_set_frequency(servo.pwm_dev, SERVO_PWM_FREQ);
    assert(rc > 0);
    rc = pwm_configure_channel(servo.pwm_dev, PWM_CH_NUM, &chan_conf);
    assert(rc == 0);

    /* Calculate minimum PWM fracture value */
    servo.pwm_top_val = (uint16_t) pwm_get_top_value(servo.pwm_dev);
    servo.frac_max_val = us_to_frac(SERVO_MAX_PULSE_DURATION_US);
    servo.frac_min_val = us_to_frac(SERVO_MIN_PULSE_DURATION_US);

    /* At the beginning PWM fracture is set to minimum */
    servo.frac = servo.frac_min_val;
    rc = pwm_set_duty_cycle(servo.pwm_dev, PWM_CH_NUM, servo.frac);
    assert(rc == 0);
    rc = pwm_enable(servo.pwm_dev);
    assert(rc == 0);
}

static void
bleservo_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));

    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;
    fields.flags = BLE_HS_ADV_F_BREDR_UNSUP;
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
        return;
    }

    /* Begin advertising */
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, bleservo_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
        return;
    }
}


static int
bleservo_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed */
        MODLOG_DFLT(INFO, "connection %s; status=%d\n",
                    event->connect.status == 0 ? "established" : "failed",
                    event->connect.status);

        if (event->connect.status != 0) {
            /* Connection failed; resume advertising */
            bleservo_advertise();
            conn_handle = 0;
        } else {
            conn_handle = event->connect.conn_handle;
        }

        break;

    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO, "disconnect; reason=%d\n", event->disconnect.reason);
        conn_handle = BLE_HS_CONN_HANDLE_NONE; /* reset conn_handle */

        /* Connection terminated; resume advertising */
        bleservo_advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        MODLOG_DFLT(INFO, "adv complete\n");
        bleservo_advertise();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        MODLOG_DFLT(INFO, "subscribe event; cur_notify=%d\n value handle; "
                          "val_handle=%d\n",
                    event->subscribe.cur_notify, servo.angle);
        break;

    case BLE_GAP_EVENT_MTU:
        MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.value);
        break;

    }

    return 0;
}

static void
bleservo_on_sync(void)
{
    int rc;

    /* own_addr_type will store type of address our device uses */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    assert(rc == 0);

    /* Start advertising */
    bleservo_advertise();
}

int
main(void)
{
    int rc;

    /* Initialize OS and PWM */
    sysinit();
    servo_pwm_init();

    /* Initialize the NimBLE host configuration */
    ble_hs_cfg.sync_cb = bleservo_on_sync;

    rc = gatt_svr_init();
    assert(rc == 0);

    /* Set the default device name */
    rc = ble_svc_gap_device_name_set(device_name);
    assert(rc == 0);

    /* As the last thing, process events from default event queue */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }
    return 0;
}
