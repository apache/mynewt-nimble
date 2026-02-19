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

#ifndef H_BLE_SERVO_
#define H_BLE_SERVO_

#include "nimble/ble.h"
#include "modlog/modlog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define  PWM_CH_CFG_PIN  31
#define  PWM_CH_CFG_INV  false
#define  PWM_CH_NUM      0
#define  PWM_IRQ_PRIO    1

#define  SERVO_MAX_PULSE_DURATION_US MYNEWT_VAL(SERVO_MAX_PULSE_DURATION_US)
#define  SERVO_MIN_PULSE_DURATION_US MYNEWT_VAL(SERVO_MIN_PULSE_DURATION_US)
#define  SERVO_PWM_FREQ MYNEWT_VAL(SERVO_PWM_FREQ)
#define  SERVO_PWM_FULL_CYCLE_DURATION MYNEWT_VAL(SERVO_PWM_FULL_CYCLE_DURATION)
#define  SERVO_MAX_ANGLE_VAL MYNEWT_VAL(SERVO_MAX_ANGLE_VAL)
#define  SERVO_MIN_ANGLE_VAL MYNEWT_VAL(SERVO_MIN_ANGLE_VAL)


int gatt_svr_init(void);
void servo_angle_setter(uint16_t);
void servo_pwm_pulse_duration_setter(uint16_t);

uint16_t get_servo_angle(void);
uint16_t get_servo_pwm_pulse_duration(void);
uint16_t get_gatt_angle_val_handle(void);
uint16_t get_gatt_pulse_duration_val_handle(void);


#ifdef __cplusplus
}
#endif

#endif
