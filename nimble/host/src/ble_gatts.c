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
#include <string.h>
#include "console/console.h"
#include "nimble/ble.h"
#include "host/ble_uuid.h"
#include "host/ble_store.h"
#include "ble_hs_priv.h"

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

struct ble_gatts_clt_cfg {
    uint16_t chr_val_handle;
    uint8_t flags;
    uint8_t allowed;
};

/** A cached array of handles for the configurable characteristics. */
static struct ble_gatts_clt_cfg *ble_gatts_clt_cfgs;
static int ble_gatts_num_cfgable_chrs;

STATS_SECT_DECL(ble_gatts_stats) ble_gatts_stats;
STATS_NAME_START(ble_gatts_stats)
    STATS_NAME(ble_gatts_stats, svcs)
    STATS_NAME(ble_gatts_stats, chrs)
    STATS_NAME(ble_gatts_stats, dscs)
    STATS_NAME(ble_gatts_stats, svc_def_reads)
    STATS_NAME(ble_gatts_stats, svc_inc_reads)
    STATS_NAME(ble_gatts_stats, chr_def_reads)
    STATS_NAME(ble_gatts_stats, chr_val_reads)
    STATS_NAME(ble_gatts_stats, chr_val_writes)
    STATS_NAME(ble_gatts_stats, dsc_reads)
    STATS_NAME(ble_gatts_stats, dsc_writes)
STATS_NAME_END(ble_gatts_stats)

static int
ble_gatts_svc_access(uint16_t conn_handle, uint16_t attr_handle,
                     uint8_t *uuid128, uint8_t op,
                     struct ble_att_svr_access_ctxt *ctxt, void *arg)
{
    const struct ble_gatt_svc_def *svc;
    static uint16_t uuid16;

    STATS_INC(ble_gatts_stats, svc_def_reads);

    BLE_HS_DBG_ASSERT(op == BLE_ATT_ACCESS_OP_READ);

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

static int
ble_gatts_inc_access(uint16_t conn_handle, uint16_t attr_handle,
                     uint8_t *uuid128, uint8_t op,
                     struct ble_att_svr_access_ctxt *ctxt, void *arg)
{
    static uint8_t buf[BLE_GATTS_INCLUDE_SZ];

    const struct ble_gatts_svc_entry *entry;
    uint16_t uuid16;

    STATS_INC(ble_gatts_stats, svc_inc_reads);

    BLE_HS_DBG_ASSERT(op == BLE_ATT_ACCESS_OP_READ);

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

static uint8_t
ble_gatts_att_flags_from_chr_flags(ble_gatt_chr_flags chr_flags)
{
    uint8_t att_flags;

    att_flags = 0;
    if (chr_flags & BLE_GATT_CHR_F_READ) {
        att_flags |= BLE_ATT_F_READ;
    }
    if (chr_flags & (BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE)) {
        att_flags |= BLE_ATT_F_WRITE;
    }
    if (chr_flags & BLE_GATT_CHR_F_READ_ENC) {
        att_flags |= BLE_ATT_F_READ_ENC;
    }
    if (chr_flags & BLE_GATT_CHR_F_READ_AUTHEN) {
        att_flags |= BLE_ATT_F_READ_AUTHEN;
    }
    if (chr_flags & BLE_GATT_CHR_F_READ_AUTHOR) {
        att_flags |= BLE_ATT_F_READ_AUTHOR;
    }
    if (chr_flags & BLE_GATT_CHR_F_WRITE_ENC) {
        att_flags |= BLE_ATT_F_WRITE_ENC;
    }
    if (chr_flags & BLE_GATT_CHR_F_WRITE_AUTHEN) {
        att_flags |= BLE_ATT_F_WRITE_AUTHEN;
    }
    if (chr_flags & BLE_GATT_CHR_F_WRITE_AUTHOR) {
        att_flags |= BLE_ATT_F_WRITE_AUTHOR;
    }

    return att_flags;
}

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

static int
ble_gatts_chr_def_access(uint16_t conn_handle, uint16_t attr_handle,
                         uint8_t *uuid128, uint8_t op,
                         struct ble_att_svr_access_ctxt *ctxt, void *arg)
{
    static uint8_t buf[BLE_GATTS_CHR_MAX_SZ];
    const struct ble_gatt_chr_def *chr;
    uint16_t uuid16;

    STATS_INC(ble_gatts_stats, chr_def_reads);

    BLE_HS_DBG_ASSERT(op == BLE_ATT_ACCESS_OP_READ);

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

static uint8_t
ble_gatts_chr_op(uint8_t att_op)
{
    switch (att_op) {
    case BLE_ATT_ACCESS_OP_READ:
        return BLE_GATT_ACCESS_OP_READ_CHR;

    case BLE_ATT_ACCESS_OP_WRITE:
        return BLE_GATT_ACCESS_OP_WRITE_CHR;

    default:
        BLE_HS_DBG_ASSERT(0);
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
    BLE_HS_DBG_ASSERT(chr != NULL && chr->access_cb != NULL);

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

static int
ble_gatts_register_inc(struct ble_gatts_svc_entry *entry)
{
    uint16_t handle;
    int rc;

    BLE_HS_DBG_ASSERT(entry->handle != 0);
    BLE_HS_DBG_ASSERT(entry->end_group_handle != 0xffff);

    rc = ble_att_svr_register_uuid16(BLE_ATT_UUID_INCLUDE, BLE_ATT_F_READ,
                                     &handle, ble_gatts_inc_access, entry);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static uint8_t
ble_gatts_dsc_op(uint8_t att_op)
{
    switch (att_op) {
    case BLE_ATT_ACCESS_OP_READ:
        return BLE_GATT_ACCESS_OP_READ_DSC;

    case BLE_ATT_ACCESS_OP_WRITE:
        return BLE_GATT_ACCESS_OP_WRITE_DSC;

    default:
        BLE_HS_DBG_ASSERT(0);
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
    BLE_HS_DBG_ASSERT(dsc != NULL && dsc->access_cb != NULL);

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

static int
ble_gatts_clt_cfg_find_idx(struct ble_gatts_clt_cfg *cfgs,
                           uint16_t chr_val_handle)
{
    struct ble_gatts_clt_cfg *cfg;
    int i;

    for (i = 0; i < ble_gatts_num_cfgable_chrs; i++) {
        cfg = cfgs + i;
        if (cfg->chr_val_handle == chr_val_handle) {
            return i;
        }
    }

    return -1;
}

static struct ble_gatts_clt_cfg *
ble_gatts_clt_cfg_find(struct ble_gatts_clt_cfg *cfgs,
                       uint16_t chr_val_handle)
{
    int idx;

    idx = ble_gatts_clt_cfg_find_idx(cfgs, chr_val_handle);
    if (idx == -1) {
        return NULL;
    } else {
        return cfgs + idx;
    }
}

/**
 * Performs a read or write access on a client characteritic configuration
 * descriptor (CCCD).
 *
 * @param conn                  The connection of the peer doing the accessing.
 * @apram attr_handle           The handle of the CCCD.
 * @param att_op                The ATT operation being performed (read or
 *                                  write).
 * @param ctxt                  Communication channel between this function and
 *                                  the caller within the nimble stack.
 *                                  Semantics depends on the operation being
 *                                  performed.
 * @param out_cccd              If the CCCD should be persisted as a result of
 *                                  the access, the data-to-be-persisted gets
 *                                  written here.  If no persistence is
 *                                  necessary, out_cccd->chr_val_handle is set
 *                                  to 0.
 *
 * @return                      0 on success; nonzero on failure.
 */
static int
ble_gatts_clt_cfg_access_locked(struct ble_hs_conn *conn, uint16_t attr_handle,
                                uint8_t att_op,
                                struct ble_att_svr_access_ctxt *ctxt,
                                struct ble_store_value_cccd *out_cccd)
{
    struct ble_gatts_clt_cfg *clt_cfg;
    uint16_t chr_val_handle;
    uint16_t flags;
    uint8_t gatt_op;

    static uint8_t buf[2];

    /* Assume nothing needs to be persisted. */
    out_cccd->chr_val_handle = 0;

    /* We always register the client characteristics descriptor with handle
     * (chr_val + 1).
     */
    chr_val_handle = attr_handle - 1;
    if (chr_val_handle > attr_handle) {
        /* Attribute handle wrapped somehow. */
        return BLE_ATT_ERR_UNLIKELY;
    }

    clt_cfg = ble_gatts_clt_cfg_find(conn->bhc_gatt_svr.clt_cfgs,
                                     chr_val_handle);
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

        /* Successful writes get persisted for bonded connections. */
        if (conn->bhc_sec_state.bonded) {
            out_cccd->peer_addr_type = conn->bhc_addr_type;
            memcpy(out_cccd->peer_addr, conn->bhc_addr, 6);
            out_cccd->chr_val_handle = chr_val_handle;
            out_cccd->flags = clt_cfg->flags;
            out_cccd->value_changed = 0;
        }
        break;

    default:
        BLE_HS_DBG_ASSERT(0);
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

static int
ble_gatts_clt_cfg_access(uint16_t conn_handle, uint16_t attr_handle,
                         uint8_t *uuid128, uint8_t op,
                         struct ble_att_svr_access_ctxt *ctxt,
                         void *arg)
{
    struct ble_store_value_cccd cccd_value;
    struct ble_store_key_cccd cccd_key;
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_lock();

    conn = ble_hs_conn_find(conn_handle);
    if (conn == NULL) {
        rc = BLE_ATT_ERR_UNLIKELY;
    } else {
        rc = ble_gatts_clt_cfg_access_locked(conn, attr_handle, op, ctxt,
                                             &cccd_value);
    }

    ble_hs_unlock();

    /* Persist the CCCD if required. */
    if (rc == 0 && cccd_value.chr_val_handle != 0) {
        if (cccd_value.flags == 0) {
            ble_store_key_from_value_cccd(&cccd_key, &cccd_value);
            rc = ble_store_delete_cccd(&cccd_key);
        } else {
            rc = ble_store_write_cccd(&cccd_value);
        }
    }

    return rc;
}

static int
ble_gatts_register_clt_cfg_dsc(uint16_t *att_handle)
{
    uint8_t uuid128[16];
    int rc;

    rc = ble_uuid_16_to_128(BLE_GATT_DSC_CLT_CFG_UUID16, uuid128);
    if (rc != 0) {
        return rc;
    }

    rc = ble_att_svr_register(uuid128, BLE_ATT_F_READ | BLE_ATT_F_WRITE,
                              att_handle, ble_gatts_clt_cfg_access, NULL);
    if (rc != 0) {
        return rc;
    }

    STATS_INC(ble_gatts_stats, dscs);

    return 0;
}

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
                                     BLE_ATT_F_READ, &def_handle,
                                     ble_gatts_chr_def_access, (void *)chr);
    if (rc != 0) {
        return rc;
    }

    /* Register characteristic value attribute (cast away const on callback
     * arg).
     */
    att_flags = ble_gatts_att_flags_from_chr_flags(chr->flags);
    rc = ble_att_svr_register(chr->uuid128, att_flags, &val_handle,
                              ble_gatts_chr_val_access, (void *)chr);
    if (rc != 0) {
        return rc;
    }
    BLE_HS_DBG_ASSERT(val_handle == def_handle + 1);

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
        BLE_HS_DBG_ASSERT(dsc_handle == def_handle + 2);
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

    /* Prevent spurious maybe-uninitialized gcc warning. */
    uuid16 = 0;

    rc = ble_gatts_svc_type_to_uuid(svc->type, &uuid16);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

    /* Register service definition attribute (cast away const on callback
     * arg).
     */
    rc = ble_att_svr_register_uuid16(uuid16, BLE_ATT_F_READ, out_handle,
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
            BLE_HS_DBG_ASSERT_EVAL(idx != -1);

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

void
ble_gatts_conn_deinit(struct ble_gatts_conn *gatts_conn)
{
    int rc;

    if (gatts_conn->clt_cfgs != NULL) {
        rc = os_memblock_put(&ble_gatts_clt_cfg_pool, gatts_conn->clt_cfgs);
        BLE_HS_DBG_ASSERT_EVAL(rc == 0);

        gatts_conn->clt_cfgs = NULL;
    }
}

static int
ble_gatts_clt_cfg_size(void)
{
    return ble_gatts_num_cfgable_chrs * sizeof (struct ble_gatts_clt_cfg);
}

int
ble_gatts_start(void)
{
    struct ble_att_svr_entry *ha;
    struct ble_gatt_chr_def *chr;
    uint16_t allowed_flags;
    uint8_t uuid128[16];
    int num_elems;
    int idx;
    int rc;

    rc = ble_uuid_16_to_128(BLE_ATT_UUID_CHARACTERISTIC, uuid128);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

    /* Count the number of client-configurable characteristics. */
    ble_gatts_num_cfgable_chrs = 0;
    ha = NULL;
    while ((ha = ble_att_svr_find_by_uuid(ha, uuid128)) != NULL) {
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
    while ((ha = ble_att_svr_find_by_uuid(ha, uuid128)) != NULL) {
        chr = ha->ha_cb_arg;
        allowed_flags = ble_gatts_chr_clt_cfg_allowed(chr);
        if (allowed_flags != 0) {
            BLE_HS_DBG_ASSERT_EVAL(idx < ble_gatts_num_cfgable_chrs);

            ble_gatts_clt_cfgs[idx].chr_val_handle = ha->ha_handle_id + 1;
            ble_gatts_clt_cfgs[idx].allowed = allowed_flags;
            ble_gatts_clt_cfgs[idx].flags = 0;
            idx++;
        }
    }

    return 0;
}

int
ble_gatts_conn_can_alloc(void)
{
    return ble_gatts_num_cfgable_chrs == 0 ||
           ble_gatts_clt_cfg_pool.mp_num_free > 0;
}

int
ble_gatts_conn_init(struct ble_gatts_conn *gatts_conn)
{
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
 * Schedules a notification or indication for the specified peer-CCCD pair.  If
 * the udpate should be sent immediately, it is indicated in the return code.
 * If the update can only be sent in the future, the appropriate flags are set
 * to ensure this happens.
 *
 * @param conn                  The connection to schedule the update for.
 * @param clt_cfg               The client config entry corresponding to the
 *                                  peer and affected characteristic.
 *
 * @return                      The att_op of the update to send immediately,
 *                                  if any.  0 if nothing should get sent.
 */
static uint8_t
ble_gatts_schedule_update(struct ble_hs_conn *conn,
                          struct ble_gatts_clt_cfg *clt_cfg)
{
    uint8_t att_op;

    if (clt_cfg->flags & BLE_GATTS_CLT_CFG_F_NOTIFY) {
        /* Notifications always get sent immediately. */
        att_op = BLE_ATT_OP_NOTIFY_REQ;
    } else if (clt_cfg->flags & BLE_GATTS_CLT_CFG_F_INDICATE) {
        /* Only one outstanding indication per peer is allowed.  If we
         * are still awaiting an ack, mark this CCCD as updated so that
         * we know to send the indication upon receiving the expected ack.
         * If there isn't an outstanding indication, send this one now.
         */
        if (conn->bhc_gatt_svr.indicate_val_handle != 0) {
            clt_cfg->flags |= BLE_GATTS_CLT_CFG_F_INDICATE_PENDING;
            att_op = 0;
        } else {
            att_op = BLE_ATT_OP_INDICATE_REQ;
        }
    } else {
        BLE_HS_DBG_ASSERT(0);
        att_op = 0;
    }

    return att_op;
}

int
ble_gatts_send_next_indicate(uint16_t conn_handle)
{
    struct ble_gatts_clt_cfg *clt_cfg;
    struct ble_hs_conn *conn;
    uint16_t chr_val_handle;
    int rc;
    int i;

    /* Assume no pending indications. */
    chr_val_handle = 0;

    ble_hs_lock();

    conn = ble_hs_conn_find(conn_handle);
    if (conn != NULL) {
        for (i = 0; i < conn->bhc_gatt_svr.num_clt_cfgs; i++) {
            clt_cfg = conn->bhc_gatt_svr.clt_cfgs + i;
            if (clt_cfg->flags & BLE_GATTS_CLT_CFG_F_INDICATE_PENDING) {
                BLE_HS_DBG_ASSERT(clt_cfg->flags &
                                  BLE_GATTS_CLT_CFG_F_INDICATE);

                chr_val_handle = clt_cfg->chr_val_handle;

                /* Clear pending flag in anticipation of indication tx. */
                clt_cfg->flags &= ~BLE_GATTS_CLT_CFG_F_INDICATE_PENDING;
                break;
            }
        }
    }

    ble_hs_unlock();

    if (conn == NULL) {
        return BLE_HS_ENOTCONN;
    }

    if (chr_val_handle == 0) {
        return BLE_HS_ENOENT;
    }

    rc = ble_gattc_indicate(conn_handle, chr_val_handle, NULL, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_gatts_rx_indicate_ack(uint16_t conn_handle, uint16_t chr_val_handle)
{
    struct ble_store_value_cccd cccd_value;
    struct ble_gatts_clt_cfg *clt_cfg;
    struct ble_hs_conn *conn;
    int clt_cfg_idx;
    int persist;
    int rc;

    clt_cfg_idx = ble_gatts_clt_cfg_find_idx(ble_gatts_clt_cfgs,
                                             chr_val_handle);
    if (clt_cfg_idx == -1) {
        /* This characteristic does not have a CCCD. */
        return BLE_HS_ENOENT;
    }

    clt_cfg = ble_gatts_clt_cfgs + clt_cfg_idx;
    if (!(clt_cfg->allowed & BLE_GATTS_CLT_CFG_F_INDICATE)) {
        /* This characteristic does not allow indications. */
        return BLE_HS_ENOENT;
    }

    ble_hs_lock();

    conn = ble_hs_conn_find(conn_handle);
    BLE_HS_DBG_ASSERT(conn != NULL);
    if (conn->bhc_gatt_svr.indicate_val_handle == chr_val_handle) {
        /* This acknowledgement is expected. */
        rc = 0;

        /* Mark that there is no longer an outstanding txed indicate. */
        conn->bhc_gatt_svr.indicate_val_handle = 0;

        /* Determine if we need to persist that there is no pending indication
         * for this peer-characteristic pair.  If the characteristic has not
         * been modified since we sent the indication, there is no indication
         * pending.
         */
        BLE_HS_DBG_ASSERT(conn->bhc_gatt_svr.num_clt_cfgs > clt_cfg_idx);
        clt_cfg = conn->bhc_gatt_svr.clt_cfgs + clt_cfg_idx;
        BLE_HS_DBG_ASSERT(clt_cfg->chr_val_handle == chr_val_handle);

        persist = conn->bhc_sec_state.bonded &&
                  !(clt_cfg->flags & BLE_GATTS_CLT_CFG_F_INDICATE_PENDING);
        if (persist) {
            cccd_value.peer_addr_type = conn->bhc_addr_type;
            memcpy(cccd_value.peer_addr, conn->bhc_addr, 6);
            cccd_value.chr_val_handle = chr_val_handle;
            cccd_value.flags = clt_cfg->flags;
            cccd_value.value_changed = 0;
        }
    } else {
        /* This acknowledgement doesn't correspnod to the outstanding
         * indication; ignore it.
         */
        rc = BLE_HS_ENOENT;
    }

    ble_hs_unlock();

    if (rc != 0) {
        return rc;
    }

    if (persist) {
        rc = ble_store_write_cccd(&cccd_value);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

void
ble_gatts_chr_updated(uint16_t chr_val_handle)
{
    struct ble_store_value_cccd cccd_value;
    struct ble_store_key_cccd cccd_key;
    struct ble_gatts_clt_cfg *clt_cfg;
    struct ble_hs_conn *conn;
    uint16_t conn_handle;
    uint8_t att_op;
    int clt_cfg_idx;
    int persist;
    int rc;
    int i;

    /* Determine if notifications / indications are enabled for this
     * characteristic.
     */
    clt_cfg_idx = ble_gatts_clt_cfg_find_idx(ble_gatts_clt_cfgs,
                                             chr_val_handle);
    if (clt_cfg_idx == -1) {
        return;
    }

    /* Handle the connected devices. */
    for (i = 0; ; i++) {
        ble_hs_lock();

        conn = ble_hs_conn_find_by_idx(i);
        if (conn != NULL) {
            BLE_HS_DBG_ASSERT_EVAL(conn->bhc_gatt_svr.num_clt_cfgs >
                                   clt_cfg_idx);
            clt_cfg = conn->bhc_gatt_svr.clt_cfgs + clt_cfg_idx;
            BLE_HS_DBG_ASSERT_EVAL(clt_cfg->chr_val_handle == chr_val_handle);

            /* Determine what kind of update should get sent immediately (if
             * any).
             */
            att_op = ble_gatts_schedule_update(conn, clt_cfg);
            conn_handle = conn->bhc_handle;
        } else {
            /* Silence some spurious gcc warnings. */
            att_op = 0;
            conn_handle = BLE_HS_CONN_HANDLE_NONE;
        }
        ble_hs_unlock();

        if (conn == NULL) {
            /* No more connected devices. */
            break;
        }

        switch (att_op) {
        case 0:
            break;
        case BLE_ATT_OP_NOTIFY_REQ:
            ble_gattc_notify(conn_handle, chr_val_handle);
            break;
        case BLE_ATT_OP_INDICATE_REQ:
            ble_gattc_indicate(conn_handle, chr_val_handle, NULL, NULL);
            break;
        default:
            BLE_HS_DBG_ASSERT(0);
            break;
        }
    }

    /*** Persist updated flag for unconnected and not-yet-bonded devices. */

    /* Retrieve each record corresponding to the modified characteristic. */
    cccd_key.peer_addr_type = BLE_STORE_ADDR_TYPE_NONE,
    cccd_key.chr_val_handle = chr_val_handle,
    cccd_key.idx = 0;

    while (1) {
        rc = ble_store_read_cccd(&cccd_key, &cccd_value);
        if (rc != 0) {
            /* Read error or no more CCCD records. */
            break;
        }

        /* Determine if this record needs to be rewritten. */
        ble_hs_lock();
        conn = ble_hs_conn_find_by_addr(cccd_value.peer_addr_type,
                                        cccd_value.peer_addr);

        if (conn == NULL) {
            /* Device isn't connected; persist the changed flag so that an
             * update can be sent when the device reconnects and rebonds.
             */
            persist = 1;
        } else if (cccd_value.flags & BLE_GATTS_CLT_CFG_F_INDICATE) {
            /* Indication for a connected device; record that the
             * characteristic has changed until we receive the ack.
             */
            persist = 1;
        } else {
            /* Notification for a connected device; we already sent it so there
             * is no need to persist.
             */
            persist = 0;
        }

        ble_hs_unlock();

        /* Only persist if the value changed flag wasn't already sent (i.e.,
         * don't overwrite with identical data).
         */
        if (persist && !cccd_value.value_changed) {
            cccd_value.value_changed = 1;
            ble_store_write_cccd(&cccd_value);
        }

        /* Read the next matching record. */
        cccd_key.idx++;
    }
}

/**
 * Called when bonding has been restored via the encryption procedure.  This
 * function:
 *     o Restores persisted CCCD entries for the connected peer.
 *     o Sends all pending notifications to the connected peer.
 *     o Sends up to one pending indication to the connected peer; schedules
 *       any remaining pending indications.
 */
void
ble_gatts_bonding_restored(uint16_t conn_handle)
{
    struct ble_store_value_cccd cccd_value;
    struct ble_store_key_cccd cccd_key;
    struct ble_gatts_clt_cfg *clt_cfg;
    struct ble_hs_conn *conn;
    uint8_t att_op;
    int rc;

    ble_hs_lock();

    conn = ble_hs_conn_find(conn_handle);
    BLE_HS_DBG_ASSERT(conn != NULL);
    BLE_HS_DBG_ASSERT(conn->bhc_sec_state.bonded);

    cccd_key.peer_addr_type = conn->bhc_addr_type;
    memcpy(cccd_key.peer_addr, conn->bhc_addr, 6);
    cccd_key.chr_val_handle = 0;
    cccd_key.idx = 0;

    ble_hs_unlock();

    while (1) {
        rc = ble_store_read_cccd(&cccd_key, &cccd_value);
        if (rc != 0) {
            break;
        }

        /* Assume no immediate send for this characteristic. */
        att_op = 0;

        ble_hs_lock();

        conn = ble_hs_conn_find(conn_handle);
        BLE_HS_DBG_ASSERT(conn != NULL);

        clt_cfg = ble_gatts_clt_cfg_find(conn->bhc_gatt_svr.clt_cfgs,
                                         cccd_value.chr_val_handle);
        if (clt_cfg != NULL) {
            clt_cfg->flags = cccd_value.flags;
            if (cccd_value.value_changed) {
                att_op = ble_gatts_schedule_update(conn, clt_cfg);
            }
        }

        ble_hs_unlock();

        switch (att_op) {
        case 0:
            break;
        case BLE_ATT_OP_NOTIFY_REQ:
            rc = ble_gattc_notify(conn_handle, cccd_value.chr_val_handle);
            if (rc == 0) {
                cccd_value.value_changed = 0;
                ble_store_write_cccd(&cccd_value);
            }
            break;
        case BLE_ATT_OP_INDICATE_REQ:
            ble_gattc_indicate(conn_handle, cccd_value.chr_val_handle,
                               NULL, NULL);
            break;
        default:
            BLE_HS_DBG_ASSERT(0);
            break;
        }

        cccd_key.idx++;
    }
}

static void
ble_gatts_free_mem(void)
{
    free(ble_gatts_clt_cfg_mem);
    ble_gatts_clt_cfg_mem = NULL;

    free(ble_gatts_svc_entries);
    ble_gatts_svc_entries = NULL;
}

int
ble_gatts_init(void)
{
    int rc;

    ble_gatts_free_mem();
    ble_gatts_num_cfgable_chrs = 0;
    ble_gatts_clt_cfgs = NULL;

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
