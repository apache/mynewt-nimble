/**
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
#include "host/ble_hs.h"
#include "services/mandatory/ble_svc_gap.h"

/* XXX: This should be configurable. */
#define BLE_SVC_GAP_NAME_MAX_LEN    31

static char ble_svc_gap_name[BLE_SVC_GAP_NAME_MAX_LEN + 1] = "nimble";
static uint16_t ble_svc_gap_appearance;
static uint8_t ble_svc_gap_privacy_flag;
static uint8_t ble_svc_gap_reconnect_addr[6];
static uint8_t ble_svc_gap_pref_conn_params[8];

static int
ble_svc_gap_access(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def ble_svc_gap_defs[] = {
    {
        /*** Service: GAP. */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(BLE_SVC_GAP_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            /*** Characteristic: Device Name. */
            .uuid128 = BLE_UUID16(BLE_SVC_GAP_CHR_UUID16_DEVICE_NAME),
            .access_cb = ble_svc_gap_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            /*** Characteristic: Appearance. */
            .uuid128 = BLE_UUID16(BLE_SVC_GAP_CHR_UUID16_APPEARANCE),
            .access_cb = ble_svc_gap_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            /*** Characteristic: Peripheral Privacy Flag. */
            .uuid128 = BLE_UUID16(BLE_SVC_GAP_CHR_UUID16_PERIPH_PRIV_FLAG),
            .access_cb = ble_svc_gap_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            /*** Characteristic: Reconnection Address. */
            .uuid128 = BLE_UUID16(BLE_SVC_GAP_CHR_UUID16_RECONNECT_ADDR),
            .access_cb = ble_svc_gap_access,
            .flags = BLE_GATT_CHR_F_WRITE,
        }, {
            /*** Characteristic: Peripheral Preferred Connection Parameters. */
            .uuid128 =
                BLE_UUID16(BLE_SVC_GAP_CHR_UUID16_PERIPH_PREF_CONN_PARAMS),
            .access_cb = ble_svc_gap_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            0, /* No more characteristics in this service. */
        } },
    },

    {
        0, /* No more services. */
    },
};

static int
ble_svc_gap_access(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid16;

    uuid16 = ble_uuid_128_to_16(ctxt->chr->uuid128);
    assert(uuid16 != 0);

    switch (uuid16) {
    case BLE_SVC_GAP_CHR_UUID16_DEVICE_NAME:
        assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);
        ctxt->att->read.data = ble_svc_gap_name;
        ctxt->att->read.len = strlen(ble_svc_gap_name);
        break;

    case BLE_SVC_GAP_CHR_UUID16_APPEARANCE:
        assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);
        ctxt->att->read.data = &ble_svc_gap_appearance;
        ctxt->att->read.len = sizeof ble_svc_gap_appearance;
        break;

    case BLE_SVC_GAP_CHR_UUID16_PERIPH_PRIV_FLAG:
        assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);
        ctxt->att->read.data = &ble_svc_gap_privacy_flag;
        ctxt->att->read.len = sizeof ble_svc_gap_privacy_flag;
        break;

    case BLE_SVC_GAP_CHR_UUID16_RECONNECT_ADDR:
        assert(ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR);
        if (ctxt->att->write.len != sizeof ble_svc_gap_reconnect_addr) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        memcpy(ble_svc_gap_reconnect_addr, ctxt->att->write.data,
               sizeof ble_svc_gap_reconnect_addr);
        break;

    case BLE_SVC_GAP_CHR_UUID16_PERIPH_PREF_CONN_PARAMS:
        assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);
        ctxt->att->read.data = &ble_svc_gap_pref_conn_params;
        ctxt->att->read.len = sizeof ble_svc_gap_pref_conn_params;
        break;

    default:
        assert(0);
        break;
    }

    return 0;
}

const char *
ble_svc_gap_device_name(void)
{
    return ble_svc_gap_name;
}

int
ble_svc_gap_device_name_set(const char *name)
{
    int len;

    len = strlen(name);
    if (len > BLE_SVC_GAP_NAME_MAX_LEN) {
        return BLE_HS_EINVAL;
    }

    memcpy(ble_svc_gap_name, name, len);
    ble_svc_gap_name[len] = '\0';

    return 0;
}

int
ble_svc_gap_register(void)
{
    int rc;

    rc = ble_gatts_register_svcs(ble_svc_gap_defs, NULL, NULL);
    return rc;
}

int
ble_svc_gap_init(struct ble_hs_cfg *cfg)
{
    int rc;

    rc = ble_gatts_count_cfg(ble_svc_gap_defs, cfg);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
