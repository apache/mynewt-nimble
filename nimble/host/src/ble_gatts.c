/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stddef.h>
#include <assert.h>
#include "ble_hs_priv.h"
#include "ble_gatt_priv.h"

#define BLE_GATTS_MAX_SERVICES  32 /* XXX: Make this configurable. */

struct ble_gatts_svc_entry {
    const struct ble_gatt_svc_def *svc;
    uint16_t handle;    /* 0 means unregistered. */
};

static struct ble_gatts_svc_entry
    ble_gatts_svc_entries[BLE_GATTS_MAX_SERVICES];
static int ble_gatts_num_svc_entries;

static int
ble_gatts_svc_access(uint16_t handle_id, uint8_t *uuid128, uint8_t op,
                     union ble_att_svr_access_ctxt *ctxt, void *arg)
{
    const struct ble_gatt_svc_def *svc;

    assert(op == BLE_ATT_OP_READ_REQ);

    svc = arg;
    ctxt->ahc_read.attr_data = svc->uuid128;
    ctxt->ahc_read.attr_len = 16;

    return 0;
}

static int
ble_gatts_service_includes_satisfied(const struct ble_gatt_svc_def *svc)
{
    const struct ble_gatt_svc_def *incl;
    int i;

    if (svc->includes == NULL) {
        /* No included services. */
        return 1;
    }

    for (incl = *svc->includes; incl != NULL; incl++) {
        for (i = 0; i < ble_gatts_num_svc_entries; i++) {
            if (ble_gatts_svc_entries[i].handle == 0) {
                return 0;
            } else {
                break;
            }
        }
    }

    return 1;
}

static int
ble_gatts_register_service(const struct ble_gatt_svc_def *svc,
                           uint16_t *out_handle)
{
    int rc;

    if (!ble_gatts_service_includes_satisfied(svc)) {
        return BLE_HS_EAGAIN;
    }

    /* Register service definition attribute (cast away const on callback
     * arg).
     */
    rc = ble_att_svr_register(svc->uuid128, HA_FLAG_PERM_READ, out_handle,
                              ble_gatts_svc_access, (void *)svc);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_gatts_register_round(int *out_num_registered)
{
    struct ble_gatts_svc_entry *entry;
    uint16_t handle;
    int rc;
    int i;

    *out_num_registered = 0;
    for (i = 0; i < ble_gatts_num_svc_entries; i++) {
        entry = ble_gatts_svc_entries + i;

        if (entry->handle == 0) {
            rc = ble_gatts_register_service(entry->svc, &handle);
            switch (rc) {
            case 0:
                /* Service successfully registered. */
                entry->handle = handle;
                (*out_num_registered)++;
                /* XXX: Call callback. */
                break;

            case BLE_HS_EAGAIN:
                /* Service could not be registered due to unsatisfied includes.
                 * Try again on the next itereation.
                 */
                break;

            default:
                return rc;
            }
        }
    }

    if (*out_num_registered == 0) {
        return BLE_HS_EAPP; // XXX
    }

    return 0;
}

int
ble_gatts_register_services(const struct ble_gatt_svc_def *svcs)
{
    int total_registered;
    int cur_registered;
    int rc;
    int i;

    for (i = 0; svcs[i].type != BLE_GATT_SVC_TYPE_END; i++) {
        ble_gatts_svc_entries[i].svc = svcs + i;
        ble_gatts_svc_entries[i].handle = 0;
    }
    ble_gatts_num_svc_entries = i;

    total_registered = 0;
    while (total_registered < ble_gatts_num_svc_entries) {
        rc = ble_gatts_register_round(&cur_registered);
        if (rc != 0) {
            return rc;
        }
        total_registered += cur_registered;
    }

    return 0;
}
