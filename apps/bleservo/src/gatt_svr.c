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

#include <string.h>
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "ble_servo.h"

/* 59462f12-9543-9999-12c8-58b459a2712d */
static const ble_uuid128_t gatt_svr_svc_servo_uuid =
    BLE_UUID128_INIT(0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12,
                     0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59);

/* 5c3a659e-897e-45e1-b016-007107c96df6 */
static const ble_uuid128_t gatt_svr_chr_servo_angle_uuid =
    BLE_UUID128_INIT(0xf6, 0x6d, 0xc9, 0x07, 0x71, 0x00, 0x16, 0xb0,
                     0xe1, 0x45, 0x7e, 0x89, 0x9e, 0x65, 0x3a, 0x5c);

/* 6d456567-8995-45e1-b016-007107c96df6 */
static const ble_uuid128_t gatt_svr_chr_servo_pulse_duration_uuid =
    BLE_UUID128_INIT(0xf6, 0x6d, 0xc9, 0x07, 0x71, 0x00, 0x16, 0xb0,
                     0xe1, 0x45, 0x95, 0x89, 0x67, 0x65, 0x45, 0x6d);

/* Values kept in characteristics */
static uint16_t gatt_angle_val;
static uint16_t gatt_pulse_duration_val;

/* GATT attribute handles  */
static uint16_t gatt_angle_val_handle;
static uint16_t gatt_pulse_duration_val_handle;

static int
gatt_svr_chr_access_servo_angle(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);

static int
gatt_svr_chr_access_servo_pulse_duration(uint16_t conn_handle, uint16_t attr_handle,
                                         struct ble_gatt_access_ctxt *ctxt, void *arg);

static int
gatt_svr_chr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len,
                   void *dst, uint16_t *len)
{
    uint16_t om_len;
    int rc;

    om_len = OS_MBUF_PKTLEN(om);
    if (om_len < min_len || om_len > max_len) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    rc = ble_hs_mbuf_to_flat(om, dst, max_len, len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        /* Service: Servo */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_servo_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Characteristic: servo angle*/
                .uuid = &gatt_svr_chr_servo_angle_uuid.u,
                .val_handle = &gatt_angle_val_handle,
                .access_cb = gatt_svr_chr_access_servo_angle,
                .flags = BLE_GATT_CHR_F_READ |
                         BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_NOTIFY
            },

            {
                /* Characteristic: servo PWM pulse duration*/
                .uuid = &gatt_svr_chr_servo_pulse_duration_uuid.u,
                .val_handle = &gatt_pulse_duration_val_handle,
                .access_cb = gatt_svr_chr_access_servo_pulse_duration,
                .flags = BLE_GATT_CHR_F_READ |
                         BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_NOTIFY
            },

            {
                0, /* No more characteristics in this service */
            },
        }
    },

    {
        0, /* No more services */
    },
};

static int
gatt_svr_chr_access_servo_angle(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    if (attr_handle != gatt_angle_val_handle) {
        return BLE_ATT_ERR_INVALID_HANDLE;
    }

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        gatt_angle_val = get_servo_angle();
        rc = os_mbuf_append(ctxt->om, &gatt_angle_val,
                            sizeof gatt_angle_val);

        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        rc = gatt_svr_chr_write(ctxt->om,
                                sizeof gatt_angle_val,
                                sizeof gatt_angle_val,
                                &gatt_angle_val, NULL);
        servo_angle_setter(gatt_angle_val);

        return rc;
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int
gatt_svr_chr_access_servo_pulse_duration(uint16_t conn_handle, uint16_t attr_handle,
                                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    if (attr_handle != gatt_pulse_duration_val_handle) {
        return BLE_ATT_ERR_INVALID_HANDLE;
    }

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        gatt_pulse_duration_val = get_servo_pwm_pulse_duration();
        rc = os_mbuf_append(ctxt->om, &gatt_pulse_duration_val,
                            sizeof gatt_pulse_duration_val);

        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        rc = gatt_svr_chr_write(ctxt->om,
                                sizeof gatt_pulse_duration_val,
                                sizeof gatt_pulse_duration_val,
                                &gatt_pulse_duration_val, NULL);
        servo_pwm_pulse_duration_setter(gatt_pulse_duration_val);

        return rc;
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

int
gatt_svr_init(void)
{
    int rc;

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

uint16_t
get_gatt_angle_val_handle(void)
{
    return gatt_angle_val_handle;
}

uint16_t
get_gatt_pulse_duration_val_handle(void)
{
    return gatt_pulse_duration_val_handle;
}
