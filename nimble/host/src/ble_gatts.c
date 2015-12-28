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
#include "host/ble_uuid.h"
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
    uuid16 = ble_uuid_128_to_16(entry->svc->uuid128);
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
ble_gatts_chr_def_access(uint16_t handle_id, uint8_t *uuid128, uint8_t op,
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

    uuid16 = ble_uuid_128_to_16(chr->uuid128);
    if (uuid16 != 0) {
        htole16(buf + 3, uuid16);
        ctxt->ahc_read.attr_len = 5;
    } else {
        memcpy(buf + 3, chr->uuid128, 16);
        ctxt->ahc_read.attr_len = 19;
    }
    ctxt->ahc_read.attr_data = buf;

    return 0;
}

static void
ble_gatts_ctxt_from_att_ctxt(uint8_t att_op,
                             union ble_att_svr_access_ctxt *att_ctxt,
                             uint8_t *out_gatt_op,
                             union ble_gatt_access_ctxt *out_gatt_ctxt,
                             void *att_arg)
{
    switch (att_op) {
    case BLE_ATT_ACCESS_OP_READ:
        *out_gatt_op = BLE_GATT_ACCESS_OP_READ_CHR;
        out_gatt_ctxt->bgc_read.chr = att_arg;
        out_gatt_ctxt->bgc_read.chr_data = att_ctxt->ahc_read.attr_data;
        out_gatt_ctxt->bgc_read.chr_len = att_ctxt->ahc_read.attr_len;
        break;

    case BLE_ATT_ACCESS_OP_WRITE:
        *out_gatt_op = BLE_GATT_ACCESS_OP_WRITE_CHR;
        out_gatt_ctxt->bgc_write.chr = att_arg;
        out_gatt_ctxt->bgc_write.chr_data = att_ctxt->ahc_write.attr_data;
        out_gatt_ctxt->bgc_write.chr_len = att_ctxt->ahc_write.attr_len;
        break;

    default:
        assert(0);
        break;
    }
}

static void
ble_gatts_ctxt_to_att_ctxt(uint8_t gatt_op,
                           union ble_gatt_access_ctxt *gatt_ctxt,
                           union ble_att_svr_access_ctxt *out_att_ctxt)
{
    switch (gatt_op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        out_att_ctxt->ahc_read.attr_data = gatt_ctxt->bgc_read.chr_data;
        out_att_ctxt->ahc_read.attr_len = gatt_ctxt->bgc_read.chr_len;
        break;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        out_att_ctxt->ahc_write.attr_data = gatt_ctxt->bgc_write.chr_data;
        out_att_ctxt->ahc_write.attr_len = gatt_ctxt->bgc_write.chr_len;
        break;

    default:
        assert(0);
        break;
    }
}

static int
ble_gatts_chr_val_access(uint16_t handle_id, uint8_t *uuid128, uint8_t att_op,
                         union ble_att_svr_access_ctxt *att_ctxt, void *arg)
{
    const struct ble_gatt_chr_def *chr;
    union ble_gatt_access_ctxt gatt_ctxt;
    uint8_t gatt_op;
    int rc;

    ble_gatts_ctxt_from_att_ctxt(att_op, att_ctxt, &gatt_op, &gatt_ctxt, arg);

    chr = arg;
    assert(chr != NULL && chr->access_cb != NULL);

    rc = chr->access_cb(handle_id, gatt_op, &gatt_ctxt, chr->arg);
    if (rc != 0) {
        return rc;
    }

    ble_gatts_ctxt_to_att_ctxt(gatt_op, &gatt_ctxt, att_ctxt);

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
ble_gatts_register_dsc(const struct ble_gatt_dsc_def *dsc,
                       const struct ble_gatt_chr_def *chr,
                       uint16_t chr_def_handle,
                       ble_gatt_register_fn *register_cb)
{
    union ble_gatt_register_ctxt register_ctxt;
    uint16_t dsc_handle;
    int rc;

    rc = ble_att_svr_register(dsc->uuid128, dsc->att_flags, &dsc_handle,
                              dsc->access_cb, (void *)dsc->arg);
    if (rc != 0) {
        return rc;
    }

    if (register_cb != NULL) {
        register_ctxt.bgr_dsc.dsc_handle = dsc_handle;
        register_ctxt.bgr_dsc.dsc = dsc;
        register_ctxt.bgr_dsc.chr_def_handle = chr_def_handle;
        register_ctxt.bgr_dsc.chr = chr;
        register_cb(BLE_GATT_REGISTER_OP_DSC, &register_ctxt);
    }

    return 0;

}

static int
ble_gatts_register_chr(const struct ble_gatt_chr_def *chr,
                       ble_gatt_register_fn *register_cb)
{
    union ble_gatt_register_ctxt register_ctxt;
    struct ble_gatt_dsc_def *dsc;
    uint16_t def_handle;
    uint16_t val_handle;
    int rc;

    /* Register characteristic declaration attribute (cast away const on
     * callback arg).
     */
    rc = ble_att_svr_register_uuid16(BLE_ATT_UUID_CHARACTERISTIC,
                                     HA_FLAG_PERM_READ, &def_handle,
                                     ble_gatts_chr_def_access, (void *)chr);
    if (rc != 0) {
        return rc;
    }

    /* Register characteristic value attribute  (cast away const on callback
     * arg).
     */
    rc = ble_att_svr_register(chr->uuid128, HA_FLAG_PERM_READ /*XXX*/,
                              &val_handle,
                              ble_gatts_chr_val_access, (void *)chr);
    if (rc != 0) {
        return rc;
    }
    assert(val_handle == def_handle + 1);

    if (register_cb != NULL) {
        register_ctxt.bgr_chr.def_handle = def_handle;
        register_ctxt.bgr_chr.val_handle = val_handle;
        register_ctxt.bgr_chr.chr = chr;
        register_cb(BLE_GATT_REGISTER_OP_CHR, &register_ctxt);
    }

    /* Register each descriptor. */
    if (chr->descriptors != NULL) {
        for (dsc = chr->descriptors; dsc->uuid128 != NULL; dsc++) {
            rc = ble_gatts_register_dsc(dsc, chr, def_handle, register_cb);
            if (rc != 0) {
                return rc;
            }
        }
    }

    return 0;
}

static int
ble_gatts_svc_type_to_uuid(uint8_t svc_type, uint16_t *out_uuid16)
{
    switch (svc_type) {
    case BLE_GATT_SVC_TYPE_PRIMARY:
        *out_uuid16 = BLE_ATT_UUID_PRIMARY_SERVICE;
        return 0;

    case BLE_GATT_SVC_TYPE_SECONDARY:
        *out_uuid16 = BLE_ATT_UUID_SECONDARY_SERVICE;
        return 0;

    default:
        return BLE_HS_EINVAL;
    }
}

static int
ble_gatts_register_svc(const struct ble_gatt_svc_def *svc,
                       uint16_t *out_handle,
                       ble_gatt_register_fn *register_cb)
{
    const struct ble_gatt_svc_def *incl;
    const struct ble_gatt_chr_def *chr;
    union ble_gatt_register_ctxt register_ctxt;
    uint16_t uuid16;
    int idx;
    int rc;

    if (!ble_gatts_svc_incs_satisfied(svc)) {
        return BLE_HS_EAGAIN;
    }

    rc = ble_gatts_svc_type_to_uuid(svc->type, &uuid16);
    if (rc != 0) {
        return rc;
    }

    /* Register service definition attribute (cast away const on callback
     * arg).
     */
    rc = ble_att_svr_register_uuid16(uuid16, HA_FLAG_PERM_READ, out_handle,
                                     ble_gatts_svc_access, (void *)svc);
    if (rc != 0) {
        return rc;
    }

    if (register_cb != NULL) {
        register_ctxt.bgr_svc.handle = *out_handle;
        register_ctxt.bgr_svc.svc = svc;
        register_cb(BLE_GATT_REGISTER_OP_SVC, &register_ctxt);
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
            rc = ble_gatts_register_chr(chr, register_cb);
            if (rc != 0) {
                return rc;
            }
        }

    }

    return 0;
}

static int
ble_gatts_register_round(int *out_num_registered, ble_gatt_register_fn *cb)
{
    struct ble_gatts_svc_entry *entry;
    uint16_t handle;
    int rc;
    int i;

    *out_num_registered = 0;
    for (i = 0; i < ble_gatts_num_svc_entries; i++) {
        entry = ble_gatts_svc_entries + i;

        if (entry->handle == 0) {
            rc = ble_gatts_register_svc(entry->svc, &handle, cb);
            switch (rc) {
            case 0:
                /* Service successfully registered. */
                entry->handle = handle;
                (*out_num_registered)++;
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
ble_gatts_register_services(const struct ble_gatt_svc_def *svcs,
                            ble_gatt_register_fn *cb)
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
        rc = ble_gatts_register_round(&cur_registered, cb);
        if (rc != 0) {
            return rc;
        }
        total_registered += cur_registered;
    }

    return 0;
}
