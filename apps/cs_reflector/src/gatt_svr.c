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
#include <stdio.h>
#include <string.h>
#include "bsp/bsp.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "cs_reflector.h"

/* 50f89303-7cd9-4e86-9b66-2c7441ac3cd9 */
static const ble_uuid128_t gatt_svc_toa_tod_uuid =
    BLE_UUID128_INIT(0xd9, 0x3c, 0xac, 0x41, 0x74, 0x2c, 0x66, 0x9b,
                     0x86, 0x4e, 0xd9, 0x7c, 0x03, 0x93, 0xf8, 0x50);

/* 50f89303-7cd9-4e86-9b66-2c7441ac3cda */
static const ble_uuid128_t gatt_chr_toa_tod_uuid =
    BLE_UUID128_INIT(0xda, 0x3c, 0xac, 0x41, 0x74, 0x2c, 0x66, 0x9b,
                     0x86, 0x4e, 0xd9, 0x7c, 0x03, 0x93, 0xf8, 0x50);

static uint16_t toa_tod_val_handle;

static int
gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
               struct ble_gatt_access_ctxt *ctxt,
               void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        /*** Service: ToA_ToD samples */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svc_toa_tod_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Characteristic: indicate ToA_ToD samples */
                .uuid = &gatt_chr_toa_tod_uuid.u,
                .access_cb = gatt_access_cb,
                .val_handle = &toa_tod_val_handle,
                .flags = BLE_GATT_CHR_F_INDICATE,
            },
            {
                0, /* No more characteristics in this service. */
            }
        }
    },

    {
        0, /* No more services. */
    },
};


static int
gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const ble_uuid_t *uuid;

    uuid = ctxt->chr->uuid;

    /* Determine which characteristic is being accessed by examining its
     * 128-bit UUID.
     */

    if (ble_uuid_cmp(uuid, &gatt_svc_toa_tod_uuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }

        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        }
    }

    return BLE_ATT_ERR_UNLIKELY;
}

void
gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        MODLOG_DFLT(DEBUG, "registered service %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        MODLOG_DFLT(DEBUG, "registering characteristic %s with "
                           "def_handle=%d val_handle=%d\n",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle,
                    ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        MODLOG_DFLT(DEBUG, "registering descriptor %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                    ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

int
gatt_svr_indicate_toa_tod(uint16_t conn_handle, uint32_t toa_tod_val)
{
    int rc;
    struct os_mbuf *om;
    uint8_t toa_tod_buf[sizeof(toa_tod_val)];

    om = os_msys_get_pkthdr(sizeof(toa_tod_buf), 0);
    if (!om) {
        return 1;
    }

    put_le32(toa_tod_buf, toa_tod_val);

    rc = os_mbuf_append(om, toa_tod_buf, sizeof(toa_tod_buf));
    if (!om) {
        return 1;
    }

    rc = ble_gatts_indicate_custom(conn_handle, toa_tod_val_handle, om);

    return rc;
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
