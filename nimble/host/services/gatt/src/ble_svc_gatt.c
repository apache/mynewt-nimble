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

#include "host/ble_hs.h"
#include "services/gatt/ble_svc_gatt.h"

static int
ble_svc_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                    struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def ble_svc_gatt_defs[] = {
    {
        /*** Service: GATT */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(BLE_GATT_SVC_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid128 = BLE_UUID16(BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16),
            .access_cb = ble_svc_gatt_access,
            .flags = BLE_GATT_CHR_F_INDICATE,
        }, {
            0, /* No more characteristics in this service. */
        } },
    },

    {
        0, /* No more services. */
    },
};

static int
ble_svc_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t *u8;

    /* The only operation allowed for this characteristic is indicate.  This
     * access callback gets called by the stack when it needs to read the
     * characteristic value to populate the outgoing indication command.
     * Therefore, this callback should only get called during an attempt to
     * read the characteristic.
     */
    assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);
    assert(ctxt->chr == &ble_svc_gatt_defs[0].characteristics[0]);

    /* XXX: For now, always respond with 0 (unchanged). */
    u8 = os_mbuf_extend(ctxt->om, 1);
    if (u8 == NULL) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    *u8 = 0;

    return 0;
}

int
ble_svc_gatt_init(struct ble_hs_cfg *cfg)
{
    int rc;

    rc = ble_gatts_count_cfg(ble_svc_gatt_defs, cfg);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(ble_svc_gatt_defs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
