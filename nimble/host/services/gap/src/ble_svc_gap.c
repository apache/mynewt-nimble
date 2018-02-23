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

#include "sysinit/sysinit.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "os/endian.h"

/* XXX: This should be configurable. */
#define BLE_SVC_GAP_NAME_MAX_LEN    31

static char ble_svc_gap_name[BLE_SVC_GAP_NAME_MAX_LEN + 1] =
        MYNEWT_VAL(BLE_SVC_GAP_DEVICE_NAME);
static const uint8_t ble_svc_gap_appearance[2] = {
     (MYNEWT_VAL(BLE_SVC_GAP_APPEARANCE)) & 0xFF,
     ((MYNEWT_VAL(BLE_SVC_GAP_APPEARANCE)) >> 8) & 0xFF
};
static uint8_t ble_svc_gap_pref_conn_params[8];

static int
ble_svc_gap_access(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def ble_svc_gap_defs[] = {
    {
        /*** Service: GAP. */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_GAP_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            /*** Characteristic: Device Name. */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_GAP_CHR_UUID16_DEVICE_NAME),
            .access_cb = ble_svc_gap_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            /*** Characteristic: Appearance. */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_GAP_CHR_UUID16_APPEARANCE),
            .access_cb = ble_svc_gap_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            /*** Characteristic: Peripheral Preferred Connection Parameters. */
            .uuid =
                BLE_UUID16_DECLARE(BLE_SVC_GAP_CHR_UUID16_PERIPH_PREF_CONN_PARAMS),
            .access_cb = ble_svc_gap_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
#if MYNEWT_VAL(BLE_SVC_GAP_CENTRAL_ADDRESS_RESOLUTION) >= 0
            /*** Characteristic: Central Address Resolution. */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_GAP_CHR_UUID16_CENTRAL_ADDRESS_RESOLUTION),
            .access_cb = ble_svc_gap_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
#endif
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
#if MYNEWT_VAL(BLE_SVC_GAP_CENTRAL_ADDRESS_RESOLUTION) >= 0
    uint8_t central_ar = MYNEWT_VAL(BLE_SVC_GAP_CENTRAL_ADDRESS_RESOLUTION);
#endif
    int rc;

    uuid16 = ble_uuid_u16(ctxt->chr->uuid);
    assert(uuid16 != 0);

    switch (uuid16) {
    case BLE_SVC_GAP_CHR_UUID16_DEVICE_NAME:
        assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);
        rc = os_mbuf_append(ctxt->om, ble_svc_gap_name,
                            strlen(ble_svc_gap_name));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    case BLE_SVC_GAP_CHR_UUID16_APPEARANCE:
        assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);
        rc = os_mbuf_append(ctxt->om, &ble_svc_gap_appearance,
                            sizeof ble_svc_gap_appearance);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    case BLE_SVC_GAP_CHR_UUID16_PERIPH_PREF_CONN_PARAMS:
        assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);
        rc = os_mbuf_append(ctxt->om, &ble_svc_gap_pref_conn_params,
                            sizeof ble_svc_gap_pref_conn_params);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

#if MYNEWT_VAL(BLE_SVC_GAP_CENTRAL_ADDRESS_RESOLUTION) >= 0
    case BLE_SVC_GAP_CHR_UUID16_CENTRAL_ADDRESS_RESOLUTION:
        assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);
        rc = os_mbuf_append(ctxt->om, &central_ar, sizeof(central_ar));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
#endif

    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
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

uint16_t
ble_svc_gap_device_appearance(void)
{
  return get_le16(ble_svc_gap_appearance);
}

void
ble_svc_gap_init(void)
{
    int rc;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    rc = ble_gatts_count_cfg(ble_svc_gap_defs);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gatts_add_svcs(ble_svc_gap_defs);
    SYSINIT_PANIC_ASSERT(rc == 0);
}
