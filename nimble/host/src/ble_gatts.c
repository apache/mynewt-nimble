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
#include <string.h>
#include "nimble/ble.h"
#include "host/ble_hs_uuid.h"
#include "ble_hs_priv.h"
#include "ble_gatt_priv.h"

#define BLE_GATTS_INCLUDE_SZ    6
#define BLE_GATTS_CHR_MAX_SZ    19

#define BLE_GATTS_MAX_SERVICES  32 /* XXX: Make this configurable. */

struct ble_gatts_svc_entry {
    const struct ble_gatt_svc_def *svc;
    uint16_t handle;            /* 0 means unregistered. */
    uint16_t end_group_handle;
};

static struct ble_gatts_svc_entry
    ble_gatts_svc_entries[BLE_GATTS_MAX_SERVICES];
static int ble_gatts_num_svc_entries;

static int
ble_gatts_svc_access(uint16_t handle_id, uint8_t *uuid128, uint8_t op,
                     union ble_att_svr_access_ctxt *ctxt, void *arg)
{
    const struct ble_gatt_svc_def *svc;

    assert(op == BLE_ATT_ACCESS_OP_READ);

    svc = arg;
    ctxt->ahc_read.attr_data = svc->uuid128;
    ctxt->ahc_read.attr_len = 16;

    return 0;
}

static int
ble_gatts_inc_access(uint16_t handle_id, uint8_t *uuid128, uint8_t op,
                         union ble_att_svr_access_ctxt *ctxt, void *arg)
{
    static uint8_t buf[BLE_GATTS_INCLUDE_SZ];

    const struct ble_gatts_svc_entry *entry;
    uint16_t uuid16;

    assert(op == BLE_ATT_ACCESS_OP_READ);

    entry = arg;

    htole16(buf + 0, entry->handle);
    htole16(buf + 2, entry->end_group_handle);

    /* Only include the service UUID if it has a 16-bit representation. */
    uuid16 = ble_hs_uuid_16bit(entry->svc->uuid128);
    if (uuid16 != 0) {
        htole16(buf + 4, uuid16);
        ctxt->ahc_read.attr_len = 6;
    } else {
        ctxt->ahc_read.attr_len = 4;
    }
    ctxt->ahc_read.attr_data = buf;

    return 0;
}

static int
ble_gatts_chr_access(uint16_t handle_id, uint8_t *uuid128, uint8_t op,
                     union ble_att_svr_access_ctxt *ctxt, void *arg)
{
    static uint8_t buf[BLE_GATTS_CHR_MAX_SZ];
    const struct ble_gatt_chr_def *chr;
    uint16_t uuid16;

    assert(op == BLE_ATT_ACCESS_OP_READ);

    chr = arg;

    buf[0] = chr->properties;

    /* The value attribute is always immediately after the declaration. */
    htole16(buf + 1, handle_id + 1);

    uuid16 = ble_hs_uuid_16bit(chr->uuid128);
    if (uuid16 != 0) {
        htole16(buf + 3, uuid16);
        ctxt->ahc_read.attr_len = 5;
    } else {
        memcpy(buf + 3, chr->uuid128, 16);
        ctxt->ahc_read.attr_len = 19;
    }

    return 0;
}

static int
ble_gatts_find_svc(const struct ble_gatt_svc_def *svc)
{
    int i;

    for (i = 0; i < ble_gatts_num_svc_entries; i++) {
        if (ble_gatts_svc_entries[i].svc == svc) {
            return i;
        }
    }

    return -1;
}

static int
ble_gatts_svc_incs_satisfied(const struct ble_gatt_svc_def *svc)
{
    const struct ble_gatt_svc_def *incl;
    int idx;

    if (svc->includes == NULL) {
        /* No included services. */
        return 1;
    }

    for (incl = *svc->includes; incl != NULL; incl++) {
        idx = ble_gatts_find_svc(incl);
        if (idx == -1) {
            return 0;
        }
    }

    return 1;
}

static int
ble_gatts_register_inc(struct ble_gatts_svc_entry *entry)
{
    uint16_t handle;
    int rc;

    assert(entry->handle != 0);
    assert(entry->end_group_handle != 0);

    rc = ble_att_svr_register(entry->svc->uuid128, HA_FLAG_PERM_READ,
                              &handle, ble_gatts_inc_access, entry);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_gatts_register_dsc(const struct ble_gatt_dsc_def *dsc)
{
    uint16_t handle;
    int rc;

    rc = ble_att_svr_register(dsc->uuid128, dsc->att_flags, &handle,
                              dsc->access_cb, (void *)dsc->arg);
    if (rc != 0) {
        return rc;
    }

    return 0;

}

static int
ble_gatts_register_characteristic(const struct ble_gatt_chr_def *chr)
{
    struct ble_gatt_dsc_def *dsc;
    uint16_t handle;
    int rc;

    /* Register characteristic declaration attribute (cast away const on
     * callback arg).
     */
    rc = ble_att_svr_register(chr->uuid128, HA_FLAG_PERM_READ, &handle,
                              ble_gatts_chr_access, (void *)chr);
    if (rc != 0) {
        return rc;
    }

    /* Register each descriptor. */
    if (chr->descriptors != NULL) {
        for (dsc = chr->descriptors; dsc->uuid128 != NULL; dsc++) {
            rc = ble_gatts_register_dsc(dsc);
            if (rc != 0) {
                return rc;
            }
        }
    }

    return 0;
}

static int
ble_gatts_register_svc(const struct ble_gatt_svc_def *svc,
                       uint16_t *out_handle)
{
    const struct ble_gatt_svc_def *incl;
    const struct ble_gatt_chr_def *chr;
    int idx;
    int rc;

    if (!ble_gatts_svc_incs_satisfied(svc)) {
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

    /* Register each include. */
    if (svc->includes != NULL) {
        for (incl = *svc->includes; incl != NULL; incl++) {
            idx = ble_gatts_find_svc(incl);
            assert(idx != -1);

            rc = ble_gatts_register_inc(ble_gatts_svc_entries + idx);
            if (rc != 0) {
                return rc;
            }
        }
    }

    /* Register each characteristic. */
    if (svc->characteristics != NULL) {
        for (chr = svc->characteristics; chr->uuid128 != NULL; chr++) {
            rc = ble_gatts_register_characteristic(chr);
            if (rc != 0) {
                return rc;
            }
        }

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
            rc = ble_gatts_register_svc(entry->svc, &handle);
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
