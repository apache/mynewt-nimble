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

#include <stddef.h>
#include <assert.h>
#include <string.h>
#include "console/console.h"
#include "nimble/ble.h"
#include "host/ble_uuid.h"
#include "ble_hs_priv.h"
#include "ble_hs_conn.h"
#include "ble_att_priv.h"
#include "ble_gatt_priv.h"

#define BLE_GATTS_INCLUDE_SZ    6
#define BLE_GATTS_CHR_MAX_SZ    19

struct ble_gatts_svc_entry {
    const struct ble_gatt_svc_def *svc;
    uint16_t handle;            /* 0 means unregistered. */
    uint16_t end_group_handle;  /* 0xffff means unset. */
};

static struct ble_gatts_svc_entry *ble_gatts_svc_entries;
static int ble_gatts_num_svc_entries;

static os_membuf_t *ble_gatts_clt_cfg_mem;
static struct os_mempool ble_gatts_clt_cfg_pool;
static uint8_t ble_gatts_clt_cfg_inited;

struct ble_gatts_clt_cfg {
    uint16_t chr_def_handle;
    uint8_t flags;
    uint8_t allowed;
};

/** A cached array of handles for the configurable characteristics. */
static struct ble_gatts_clt_cfg *ble_gatts_clt_cfgs;
static int ble_gatts_num_cfgable_chrs;

STATS_SECT_DECL(ble_gatts_stats) ble_gatts_stats;

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_svc_access(uint16_t conn_handle, uint16_t attr_handle,
                     uint8_t *uuid128, uint8_t op,
                     struct ble_att_svr_access_ctxt *ctxt, void *arg)
{
    const struct ble_gatt_svc_def *svc;
    static uint16_t uuid16;

    STATS_INC(ble_gatts_stats, svc_def_reads);

    assert(op == BLE_ATT_ACCESS_OP_READ);

    svc = arg;

    uuid16 = ble_uuid_128_to_16(svc->uuid128);
    if (uuid16 != 0) {
        htole16(&uuid16, uuid16);
        ctxt->attr_data = &uuid16;
        ctxt->data_len = 2;
    } else {
        ctxt->attr_data = svc->uuid128;
        ctxt->data_len = 16;
    }

    return 0;
}

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_inc_access(uint16_t conn_handle, uint16_t attr_handle,
                     uint8_t *uuid128, uint8_t op,
                     struct ble_att_svr_access_ctxt *ctxt, void *arg)
{
    static uint8_t buf[BLE_GATTS_INCLUDE_SZ];

    const struct ble_gatts_svc_entry *entry;
    uint16_t uuid16;

    STATS_INC(ble_gatts_stats, svc_inc_reads);

    assert(op == BLE_ATT_ACCESS_OP_READ);

    entry = arg;

    htole16(buf + 0, entry->handle);
    htole16(buf + 2, entry->end_group_handle);

    /* Only include the service UUID if it has a 16-bit representation. */
    uuid16 = ble_uuid_128_to_16(entry->svc->uuid128);
    if (uuid16 != 0) {
        htole16(buf + 4, uuid16);
        ctxt->data_len = 6;
    } else {
        ctxt->data_len = 4;
    }
    ctxt->attr_data = buf;

    return 0;
}

/**
 * Lock restrictions: None.
 */
static uint16_t
ble_gatts_chr_clt_cfg_allowed(const struct ble_gatt_chr_def *chr)
{
    uint16_t flags;

    flags = 0;
    if (chr->flags & BLE_GATT_CHR_F_NOTIFY) {
        flags |= BLE_GATTS_CLT_CFG_F_NOTIFY;
    }
    if (chr->flags & BLE_GATT_CHR_F_INDICATE) {
        flags |= BLE_GATTS_CLT_CFG_F_INDICATE;
    }

    return flags;
}

/**
 * Lock restrictions: None.
 */
static uint8_t
ble_gatts_att_flags_from_chr_flags(ble_gatt_chr_flags chr_flags)
{
    uint8_t att_flags;

    att_flags = 0;
    if (chr_flags & BLE_GATT_CHR_F_READ) {
        att_flags |= HA_FLAG_PERM_READ;
    }
    if (chr_flags & (BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE)) {
        att_flags |= HA_FLAG_PERM_WRITE;
    }

    return att_flags;
}

/**
 * Lock restrictions: None.
 */
static uint8_t
ble_gatts_chr_properties(const struct ble_gatt_chr_def *chr)
{
    uint8_t properties;

    properties = 0;

    if (chr->flags & BLE_GATT_CHR_F_BROADCAST) {
        properties |= BLE_GATT_CHR_PROP_BROADCAST;
    }
    if (chr->flags & BLE_GATT_CHR_F_READ) {
        properties |= BLE_GATT_CHR_PROP_READ;
    }
    if (chr->flags & BLE_GATT_CHR_F_WRITE_NO_RSP) {
        properties |= BLE_GATT_CHR_PROP_WRITE_NO_RSP;
    }
    if (chr->flags & BLE_GATT_CHR_F_WRITE) {
        properties |= BLE_GATT_CHR_PROP_WRITE;
    }
    if (chr->flags & BLE_GATT_CHR_F_NOTIFY) {
        properties |= BLE_GATT_CHR_PROP_NOTIFY;
    }
    if (chr->flags & BLE_GATT_CHR_F_INDICATE) {
        properties |= BLE_GATT_CHR_PROP_INDICATE;
    }
    if (chr->flags & BLE_GATT_CHR_F_AUTH_SIGN_WRITE) {
        properties |= BLE_GATT_CHR_PROP_AUTH_SIGN_WRITE;
    }
    if (chr->flags &
        (BLE_GATT_CHR_F_RELIABLE_WRITE | BLE_GATT_CHR_F_AUX_WRITE)) {

        properties |= BLE_GATT_CHR_PROP_EXTENDED;
    }

    return properties;
}

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_chr_def_access(uint16_t conn_handle, uint16_t attr_handle,
                         uint8_t *uuid128, uint8_t op,
                         struct ble_att_svr_access_ctxt *ctxt, void *arg)
{
    static uint8_t buf[BLE_GATTS_CHR_MAX_SZ];
    const struct ble_gatt_chr_def *chr;
    uint16_t uuid16;

    STATS_INC(ble_gatts_stats, chr_def_reads);

    assert(op == BLE_ATT_ACCESS_OP_READ);

    chr = arg;

    buf[0] = ble_gatts_chr_properties(chr);

    /* The value attribute is always immediately after the declaration. */
    htole16(buf + 1, attr_handle + 1);

    uuid16 = ble_uuid_128_to_16(chr->uuid128);
    if (uuid16 != 0) {
        htole16(buf + 3, uuid16);
        ctxt->data_len = 5;
    } else {
        memcpy(buf + 3, chr->uuid128, 16);
        ctxt->data_len = 19;
    }
    ctxt->attr_data = buf;

    return 0;
}

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_chr_is_sane(const struct ble_gatt_chr_def *chr)
{
    if (chr->uuid128 == NULL) {
        return 0;
    }

    if (chr->access_cb == NULL) {
        return 0;
    }

    /* XXX: Check properties. */

    return 1;
}

/**
 * Lock restrictions: None.
 */
static uint8_t
ble_gatts_chr_op(uint8_t att_op)
{
    switch (att_op) {
    case BLE_ATT_ACCESS_OP_READ:
        return BLE_GATT_ACCESS_OP_READ_CHR;

    case BLE_ATT_ACCESS_OP_WRITE:
        return BLE_GATT_ACCESS_OP_WRITE_CHR;

    default:
        assert(0);
        return BLE_GATT_ACCESS_OP_READ_CHR;
    }
}

static void
ble_gatts_chr_inc_val_stat(uint8_t gatt_op)
{
    switch (gatt_op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        STATS_INC(ble_gatts_stats, chr_val_reads);
        break;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        STATS_INC(ble_gatts_stats, chr_val_writes);
        break;

    default:
        break;
    }
}

/**
 * Lock restrictions: Caller must NOT lock ble_hs_conn mutex.
 */
static int
ble_gatts_chr_val_access(uint16_t conn_handle, uint16_t attr_handle,
                         uint8_t *uuid128, uint8_t att_op,
                         struct ble_att_svr_access_ctxt *att_ctxt, void *arg)
{
    const struct ble_gatt_chr_def *chr;
    union ble_gatt_access_ctxt gatt_ctxt;
    uint8_t gatt_op;
    int rc;

    chr = arg;
    assert(chr != NULL && chr->access_cb != NULL);

    gatt_op = ble_gatts_chr_op(att_op);
    gatt_ctxt.chr_access.chr = chr;
    gatt_ctxt.chr_access.data = att_ctxt->attr_data;
    gatt_ctxt.chr_access.len = att_ctxt->data_len;

    ble_gatts_chr_inc_val_stat(gatt_op);

    rc = chr->access_cb(conn_handle, attr_handle, gatt_op, &gatt_ctxt,
                        chr->arg);
    if (rc != 0) {
        return rc;
    }

    if (att_op == BLE_ATT_ACCESS_OP_WRITE &&
        ble_gatts_chr_clt_cfg_allowed(chr)) {

        ble_gatts_chr_updated(attr_handle - 1);
    }

    att_ctxt->attr_data = gatt_ctxt.chr_access.data;
    att_ctxt->data_len = gatt_ctxt.chr_access.len;

    return 0;
}

/**
 * Lock restrictions: None.
 */
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

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_svc_incs_satisfied(const struct ble_gatt_svc_def *svc)
{
    int idx;
    int i;

    if (svc->includes == NULL) {
        /* No included services. */
        return 1;
    }

    for (i = 0; svc->includes[i] != NULL; i++) {
        idx = ble_gatts_find_svc(svc->includes[i]);
        if (idx == -1 || ble_gatts_svc_entries[idx].handle == 0) {
            return 0;
        }
    }

    return 1;
}

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_register_inc(struct ble_gatts_svc_entry *entry)
{
    uint16_t handle;
    int rc;

    assert(entry->handle != 0);
    assert(entry->end_group_handle != 0xffff);

    rc = ble_att_svr_register_uuid16(BLE_ATT_UUID_INCLUDE, HA_FLAG_PERM_READ,
                                     &handle, ble_gatts_inc_access, entry);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Lock restrictions: None.
 */
static uint8_t
ble_gatts_dsc_op(uint8_t att_op)
{
    switch (att_op) {
    case BLE_ATT_ACCESS_OP_READ:
        return BLE_GATT_ACCESS_OP_READ_DSC;

    case BLE_ATT_ACCESS_OP_WRITE:
        return BLE_GATT_ACCESS_OP_WRITE_DSC;

    default:
        assert(0);
        return BLE_GATT_ACCESS_OP_READ_DSC;
    }
}

static void
ble_gatts_dsc_inc_stat(uint8_t gatt_op)
{
    switch (gatt_op) {
    case BLE_GATT_ACCESS_OP_READ_DSC:
        STATS_INC(ble_gatts_stats, dsc_reads);
        break;

    case BLE_GATT_ACCESS_OP_WRITE_DSC:
        STATS_INC(ble_gatts_stats, dsc_writes);
        break;

    default:
        break;
    }
}

/**
 * Lock restrictions: Caller must NOT lock ble_hs_conn mutex.
 */
static int
ble_gatts_dsc_access(uint16_t conn_handle, uint16_t attr_handle,
                     uint8_t *uuid128, uint8_t att_op,
                     struct ble_att_svr_access_ctxt *att_ctxt, void *arg)
{
    const struct ble_gatt_dsc_def *dsc;
    union ble_gatt_access_ctxt gatt_ctxt;
    uint8_t gatt_op;
    int rc;

    dsc = arg;
    assert(dsc != NULL && dsc->access_cb != NULL);

    gatt_op = ble_gatts_dsc_op(att_op);
    gatt_ctxt.dsc_access.dsc = dsc;
    gatt_ctxt.dsc_access.data = att_ctxt->attr_data;
    gatt_ctxt.dsc_access.len = att_ctxt->data_len;

    ble_gatts_dsc_inc_stat(gatt_op);

    rc = dsc->access_cb(conn_handle, attr_handle, gatt_op, &gatt_ctxt,
                        dsc->arg);
    if (rc != 0) {
        return rc;
    }

    att_ctxt->attr_data = gatt_ctxt.dsc_access.data;
    att_ctxt->data_len = gatt_ctxt.dsc_access.len;

    return 0;
}

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_dsc_is_sane(const struct ble_gatt_dsc_def *dsc)
{
    if (dsc->uuid128 == NULL) {
        return 0;
    }

    if (dsc->access_cb == NULL) {
        return 0;
    }

    return 1;
}

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_register_dsc(const struct ble_gatt_dsc_def *dsc,
                       const struct ble_gatt_chr_def *chr,
                       uint16_t chr_def_handle,
                       ble_gatt_register_fn *register_cb, void *cb_arg)
{
    union ble_gatt_register_ctxt register_ctxt;
    uint16_t dsc_handle;
    int rc;

    if (!ble_gatts_dsc_is_sane(dsc)) {
        return BLE_HS_EINVAL;
    }

    rc = ble_att_svr_register(dsc->uuid128, dsc->att_flags, &dsc_handle,
                              ble_gatts_dsc_access, (void *)dsc);
    if (rc != 0) {
        return rc;
    }

    if (register_cb != NULL) {
        register_ctxt.dsc_reg.dsc_handle = dsc_handle;
        register_ctxt.dsc_reg.dsc = dsc;
        register_ctxt.dsc_reg.chr_def_handle = chr_def_handle;
        register_ctxt.dsc_reg.chr = chr;
        register_cb(BLE_GATT_REGISTER_OP_DSC, &register_ctxt, cb_arg);
    }

    STATS_INC(ble_gatts_stats, dscs);

    return 0;

}

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_clt_cfg_find_idx(struct ble_gatts_clt_cfg *cfgs,
                           uint16_t chr_def_handle)
{
    struct ble_gatts_clt_cfg *cfg;
    int i;

    for (i = 0; i < ble_gatts_num_cfgable_chrs; i++) {
        cfg = cfgs + i;
        if (cfg->chr_def_handle == chr_def_handle) {
            return i;
        }
    }

    return -1;
}

/**
 * Lock restrictions: None.
 */
static struct ble_gatts_clt_cfg *
ble_gatts_clt_cfg_find(struct ble_gatts_clt_cfg *cfgs,
                       uint16_t chr_def_handle)
{
    int idx;

    idx = ble_gatts_clt_cfg_find_idx(cfgs, chr_def_handle);
    if (idx == -1) {
        return NULL;
    } else {
        return cfgs + idx;
    }
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
static int
ble_gatts_clt_cfg_access_locked(struct ble_hs_conn *conn, uint16_t attr_handle,
                                uint8_t *uuid128, uint8_t att_op,
                                struct ble_att_svr_access_ctxt *ctxt,
                                void *arg)
{
    struct ble_gatts_clt_cfg *clt_cfg;
    uint16_t chr_def_handle;
    uint16_t flags;
    uint8_t gatt_op;

    static uint8_t buf[2];

    /* We always register the client characteristics descriptor with handle
     * (chr_def + 2).
     */
    chr_def_handle = attr_handle - 2;
    if (chr_def_handle > attr_handle) {
        /* Attribute handle wrapped somehow. */
        return BLE_ATT_ERR_UNLIKELY;
    }

    clt_cfg = ble_gatts_clt_cfg_find(conn->bhc_gatt_svr.clt_cfgs,
                                     attr_handle - 2);
    if (clt_cfg == NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    gatt_op = ble_gatts_dsc_op(att_op);
    ble_gatts_dsc_inc_stat(gatt_op);

    switch (gatt_op) {
    case BLE_GATT_ACCESS_OP_READ_DSC:
        STATS_INC(ble_gatts_stats, dsc_reads);
        htole16(buf, clt_cfg->flags & ~BLE_GATTS_CLT_CFG_F_RESERVED);
        ctxt->attr_data = buf;
        ctxt->data_len = sizeof buf;
        break;

    case BLE_GATT_ACCESS_OP_WRITE_DSC:
        STATS_INC(ble_gatts_stats, dsc_writes);
        if (ctxt->data_len != 2) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        flags = le16toh(ctxt->attr_data);
        if ((flags & ~clt_cfg->allowed) != 0) {
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        }

        clt_cfg->flags = flags;
        break;

    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

/**
 * Lock restrictions: Caller must NOT lock ble_hs_conn mutex.
 */
static int
ble_gatts_clt_cfg_access(uint16_t conn_handle, uint16_t attr_handle,
                         uint8_t *uuid128, uint8_t op,
                         struct ble_att_svr_access_ctxt *ctxt,
                         void *arg)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_conn_lock();

    conn = ble_hs_conn_find(conn_handle);
    if (conn == NULL) {
        rc = BLE_ATT_ERR_UNLIKELY;
    } else {
        rc = ble_gatts_clt_cfg_access_locked(conn, attr_handle, uuid128, op,
                                             ctxt, arg);
    }

    ble_hs_conn_unlock();

    return rc;
}

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_register_clt_cfg_dsc(uint16_t *att_handle)
{
    uint8_t uuid128[16];
    int rc;

    rc = ble_uuid_16_to_128(BLE_GATT_DSC_CLT_CFG_UUID16, uuid128);
    if (rc != 0) {
        return rc;
    }

    rc = ble_att_svr_register(uuid128, HA_FLAG_PERM_READ | HA_FLAG_PERM_WRITE,
                              att_handle, ble_gatts_clt_cfg_access, NULL);
    if (rc != 0) {
        return rc;
    }

    STATS_INC(ble_gatts_stats, dscs);

    return 0;
}

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_register_chr(const struct ble_gatt_chr_def *chr,
                       ble_gatt_register_fn *register_cb, void *cb_arg)
{
    union ble_gatt_register_ctxt register_ctxt;
    struct ble_gatt_dsc_def *dsc;
    uint16_t def_handle;
    uint16_t val_handle;
    uint16_t dsc_handle;
    uint8_t att_flags;
    int rc;

    if (!ble_gatts_chr_is_sane(chr)) {
        return BLE_HS_EINVAL;
    }

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
    att_flags = ble_gatts_att_flags_from_chr_flags(chr->flags);
    rc = ble_att_svr_register(chr->uuid128, att_flags, &val_handle,
                              ble_gatts_chr_val_access, (void *)chr);
    if (rc != 0) {
        return rc;
    }
    assert(val_handle == def_handle + 1);

    if (register_cb != NULL) {
        register_ctxt.chr_reg.def_handle = def_handle;
        register_ctxt.chr_reg.val_handle = val_handle;
        register_ctxt.chr_reg.chr = chr;
        register_cb(BLE_GATT_REGISTER_OP_CHR, &register_ctxt, cb_arg);
    }

    if (ble_gatts_chr_clt_cfg_allowed(chr) != 0) {
        rc = ble_gatts_register_clt_cfg_dsc(&dsc_handle);
        if (rc != 0) {
            return rc;
        }
        assert(dsc_handle == def_handle + 2);
    }

    /* Register each descriptor. */
    if (chr->descriptors != NULL) {
        for (dsc = chr->descriptors; dsc->uuid128 != NULL; dsc++) {
            rc = ble_gatts_register_dsc(dsc, chr, def_handle, register_cb,
                                        cb_arg);
            if (rc != 0) {
                return rc;
            }
        }
    }

    STATS_INC(ble_gatts_stats, chrs);

    return 0;
}

/**
 * Lock restrictions: None.
 */
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

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_svc_is_sane(const struct ble_gatt_svc_def *svc)
{
    if (svc->type != BLE_GATT_SVC_TYPE_PRIMARY &&
        svc->type != BLE_GATT_SVC_TYPE_SECONDARY) {

        return 0;
    }

    if (svc->uuid128 == NULL) {
        return 0;
    }

    return 1;
}

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_register_svc(const struct ble_gatt_svc_def *svc,
                       uint16_t *out_handle,
                       ble_gatt_register_fn *register_cb, void *cb_arg)
{
    const struct ble_gatt_chr_def *chr;
    union ble_gatt_register_ctxt register_ctxt;
    uint16_t uuid16;
    int idx;
    int rc;
    int i;

    if (!ble_gatts_svc_incs_satisfied(svc)) {
        return BLE_HS_EAGAIN;
    }

    if (!ble_gatts_svc_is_sane(svc)) {
        return BLE_HS_EINVAL;
    }

    rc = ble_gatts_svc_type_to_uuid(svc->type, &uuid16);
    assert(rc == 0);

    /* Register service definition attribute (cast away const on callback
     * arg).
     */
    rc = ble_att_svr_register_uuid16(uuid16, HA_FLAG_PERM_READ, out_handle,
                                     ble_gatts_svc_access, (void *)svc);
    if (rc != 0) {
        return rc;
    }

    if (register_cb != NULL) {
        register_ctxt.svc_reg.handle = *out_handle;
        register_ctxt.svc_reg.svc = svc;
        register_cb(BLE_GATT_REGISTER_OP_SVC, &register_ctxt, cb_arg);
    }

    /* Register each include. */
    if (svc->includes != NULL) {
        for (i = 0; svc->includes[i] != NULL; i++) {
            idx = ble_gatts_find_svc(svc->includes[i]);
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
            rc = ble_gatts_register_chr(chr, register_cb, cb_arg);
            if (rc != 0) {
                return rc;
            }
        }
    }

    STATS_INC(ble_gatts_stats, svcs);

    return 0;
}

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_register_round(int *out_num_registered, ble_gatt_register_fn *cb,
                         void *cb_arg)
{
    struct ble_gatts_svc_entry *entry;
    uint16_t handle;
    int rc;
    int i;

    *out_num_registered = 0;
    for (i = 0; i < ble_gatts_num_svc_entries; i++) {
        entry = ble_gatts_svc_entries + i;

        if (entry->handle == 0) {
            rc = ble_gatts_register_svc(entry->svc, &handle, cb, cb_arg);
            switch (rc) {
            case 0:
                /* Service successfully registered. */
                entry->handle = handle;
                entry->end_group_handle = ble_att_svr_prev_handle();
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
        /* There is a circular dependency. */
        return BLE_HS_EINVAL;
    }

    return 0;
}

/**
 * Lock restrictions: None.
 */
int
ble_gatts_register_svcs(const struct ble_gatt_svc_def *svcs,
                        ble_gatt_register_fn *cb, void *cb_arg)
{
    int total_registered;
    int cur_registered;
    int rc;
    int i;

    for (i = 0; svcs[i].type != BLE_GATT_SVC_TYPE_END; i++) {
        ble_gatts_svc_entries[i].svc = svcs + i;
        ble_gatts_svc_entries[i].handle = 0;
        ble_gatts_svc_entries[i].end_group_handle = 0xffff;
    }
    if (i > ble_hs_cfg.max_services) {
        return BLE_HS_ENOMEM;
    }

    ble_gatts_num_svc_entries = i;

    total_registered = 0;
    while (total_registered < ble_gatts_num_svc_entries) {
        rc = ble_gatts_register_round(&cur_registered, cb, cb_arg);
        if (rc != 0) {
            return rc;
        }
        total_registered += cur_registered;
    }

    return 0;
}

/**
 * Lock restrictions: None.
 */
void
ble_gatts_conn_deinit(struct ble_gatts_conn *gatts_conn)
{
    int rc;

    if (gatts_conn->clt_cfgs != NULL) {
        rc = os_memblock_put(&ble_gatts_clt_cfg_pool, gatts_conn->clt_cfgs);
        assert(rc == 0);

        gatts_conn->clt_cfgs = NULL;
    }
}

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_clt_cfg_size(void)
{
    return ble_gatts_num_cfgable_chrs * sizeof (struct ble_gatts_clt_cfg);
}

/**
 * Lock restrictions: None.
 */
static int
ble_gatts_clt_cfg_init(void)
{
    struct ble_att_svr_entry *ha;
    struct ble_gatt_chr_def *chr;
    uint16_t allowed_flags;
    uint8_t uuid128[16];
    int num_elems;
    int idx;
    int rc;

    rc = ble_uuid_16_to_128(BLE_ATT_UUID_CHARACTERISTIC, uuid128);
    assert(rc == 0);

    /* Count the number of client-configurable characteristics. */
    ble_gatts_num_cfgable_chrs = 0;
    ha = NULL;
    while (ble_att_svr_find_by_uuid(uuid128, &ha) == 0) {
        chr = ha->ha_cb_arg;
        if (ble_gatts_chr_clt_cfg_allowed(chr) != 0) {
            ble_gatts_num_cfgable_chrs++;
        }
    }
    if (ble_gatts_num_cfgable_chrs == 0) {
        return 0;
    }

    if (ble_gatts_num_cfgable_chrs > ble_hs_cfg.max_client_configs) {
        return BLE_HS_ENOMEM;
    }

    /* Initialize client-configuration memory pool. */
    num_elems = ble_hs_cfg.max_client_configs / ble_gatts_num_cfgable_chrs;
    rc = os_mempool_init(&ble_gatts_clt_cfg_pool, num_elems,
                         ble_gatts_clt_cfg_size(), ble_gatts_clt_cfg_mem,
                         "ble_gatts_clt_cfg_pool");
    if (rc != 0) {
        return BLE_HS_EOS;
    }

    /* Allocate the cached array of handles for the configuration
     * characteristics.
     */
    ble_gatts_clt_cfgs = os_memblock_get(&ble_gatts_clt_cfg_pool);
    if (ble_gatts_clt_cfgs == NULL) {
        return BLE_HS_ENOMEM;
    }

    /* Fill the cache. */
    idx = 0;
    ha = NULL;
    while (ble_att_svr_find_by_uuid(uuid128, &ha) == 0) {
        chr = ha->ha_cb_arg;
        allowed_flags = ble_gatts_chr_clt_cfg_allowed(chr);
        if (allowed_flags != 0) {
            assert(idx < ble_gatts_num_cfgable_chrs);

            ble_gatts_clt_cfgs[idx].chr_def_handle = ha->ha_handle_id;
            ble_gatts_clt_cfgs[idx].allowed = allowed_flags;
            ble_gatts_clt_cfgs[idx].flags = 0;
            idx++;
        }
    }

    return 0;
}

/**
 * Lock restrictions: None.
 */
int
ble_gatts_conn_init(struct ble_gatts_conn *gatts_conn)
{
    int rc;

    /* Initialize the client configuration memory pool if necessary. */
    if (!ble_gatts_clt_cfg_inited) {
        rc = ble_gatts_clt_cfg_init();
        if (rc != 0) {
            return rc;
        }
        ble_gatts_clt_cfg_inited = 1;
    }

    if (ble_gatts_num_cfgable_chrs > 0) {
        ble_gatts_conn_deinit(gatts_conn);
        gatts_conn->clt_cfgs = os_memblock_get(&ble_gatts_clt_cfg_pool);
        if (gatts_conn->clt_cfgs == NULL) {
            return BLE_HS_ENOMEM;
        }
    }

    /* Initialize the client configuration with a copy of the cache. */
    memcpy(gatts_conn->clt_cfgs, ble_gatts_clt_cfgs, ble_gatts_clt_cfg_size());
    gatts_conn->num_clt_cfgs = ble_gatts_num_cfgable_chrs;

    return 0;
}

/**
 * Lock restrictions: Caller must lock ble_hs_conn mutex.
 */
void
ble_gatts_send_notifications(struct ble_hs_conn *conn)
{
    struct ble_gatts_clt_cfg *clt_cfg;
    int rc;
    int i;

    /* Iterate through each configurable characteristic.  If a characteristic
     * has been updated, try to send an indication or notification
     * (never both).
     */
    for (i = 0; i < conn->bhc_gatt_svr.num_clt_cfgs; i++) {
        clt_cfg = conn->bhc_gatt_svr.clt_cfgs + i;

        if (clt_cfg->flags & BLE_GATTS_CLT_CFG_F_UPDATED) {
            if (clt_cfg->flags & BLE_GATTS_CLT_CFG_F_INDICATE) {
                if (!(conn->bhc_gatt_svr.flags &
                      BLE_GATTS_CONN_F_INDICATION_TXED)) {

                    rc = ble_gattc_indicate(conn->bhc_handle,
                                            clt_cfg->chr_def_handle + 1,
                                            NULL, NULL);
                    if (rc == 0) {
                        conn->bhc_gatt_svr.flags |=
                            BLE_GATTS_CONN_F_INDICATION_TXED;
                        clt_cfg->flags &= ~BLE_GATTS_CLT_CFG_F_UPDATED;
                    }
                }
            } else if (clt_cfg->flags & BLE_GATTS_CLT_CFG_F_NOTIFY) {
                rc = ble_gattc_notify(conn->bhc_handle,
                                      clt_cfg->chr_def_handle + 1);
                if (rc == 0) {
                    clt_cfg->flags &= ~BLE_GATTS_CLT_CFG_F_UPDATED;
                }
            }
        }
    }
}

/**
 * Lock restrictions: Caller must NOT lock ble_hs_conn mutex.
 */
void
ble_gatts_chr_updated(uint16_t chr_def_handle)
{
    struct ble_gatts_clt_cfg *clt_cfg;
    struct ble_hs_conn *conn;
    int idx;

    /* Determine if notifications / indications are enabled for this
     * characteristic, and remember the client config offset.
     */
    idx = ble_gatts_clt_cfg_find_idx(ble_gatts_clt_cfgs, chr_def_handle);
    if (idx == -1) {
        return;
    }

    ble_hs_conn_lock();

    for (conn = ble_hs_conn_first();
         conn != NULL;
         conn = SLIST_NEXT(conn, bhc_next)) {

        assert(conn->bhc_gatt_svr.num_clt_cfgs > idx);
        clt_cfg = conn->bhc_gatt_svr.clt_cfgs + idx;
        assert(clt_cfg->chr_def_handle == chr_def_handle);

        if (clt_cfg->flags &
            (BLE_GATTS_CLT_CFG_F_NOTIFY | BLE_GATTS_CLT_CFG_F_INDICATE)) {

            clt_cfg->flags |= BLE_GATTS_CLT_CFG_F_UPDATED;

            ble_gatts_send_notifications(conn);
        }
    }

    ble_hs_conn_unlock();
}

/**
 * Lock restrictions: None.
 */
static void
ble_gatts_free_mem(void)
{
    free(ble_gatts_clt_cfg_mem);
    ble_gatts_clt_cfg_mem = NULL;

    free(ble_gatts_svc_entries);
    ble_gatts_svc_entries = NULL;
}

/**
 * Lock restrictions: None.
 */
int
ble_gatts_init(void)
{
    int rc;

    ble_gatts_free_mem();
    ble_gatts_num_cfgable_chrs = 0;
    ble_gatts_clt_cfgs = NULL;
    ble_gatts_clt_cfg_inited = 0;

    if (ble_hs_cfg.max_client_configs > 0) {
        ble_gatts_clt_cfg_mem = malloc(
            OS_MEMPOOL_BYTES(ble_hs_cfg.max_client_configs,
                             sizeof (struct ble_gatts_clt_cfg)));
        if (ble_gatts_clt_cfg_mem == NULL) {
            rc = BLE_HS_ENOMEM;
            goto err;
        }
    }

    if (ble_hs_cfg.max_services > 0) {
        ble_gatts_svc_entries =
            malloc(ble_hs_cfg.max_services * sizeof *ble_gatts_svc_entries);
        if (ble_gatts_svc_entries == NULL) {
            rc = BLE_HS_ENOMEM;
            goto err;
        }
    }

    rc = stats_init_and_reg(
        STATS_HDR(ble_gatts_stats), STATS_SIZE_INIT_PARMS(ble_gatts_stats,
        STATS_SIZE_32), STATS_NAME_INIT_PARMS(ble_gatts_stats), "ble_gatts");
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    return 0;

err:
    ble_gatts_free_mem();
    return rc;
}
