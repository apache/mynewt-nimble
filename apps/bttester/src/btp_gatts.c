/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.    You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/* btp_gatts.c - Bluetooth GATT Server Service Tester */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <errno.h>
#include <assert.h>

#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "console/console.h"
#include "services/gatt/ble_svc_gatt.h"
#include "../../../nimble/host/src/ble_att_priv.h"
#include "../../../nimble/host/src/ble_gatt_priv.h"

#include "btp/btp.h"

#define CONTROLLER_INDEX 0
#define MAX_BUFFER_SIZE 2048

/* 0000xxxx-8c26-476f-89a7-a108033a69c7 */
#define PTS_UUID_DECLARE(uuid16)                                    \
    ((const ble_uuid_t *) (&(ble_uuid128_t) BLE_UUID128_INIT(   \
    0xc7, 0x69, 0x3a, 0x03, 0x08, 0xa1, 0xa7, 0x89,             \
    0x6f, 0x47, 0x26, 0x8c, uuid16, uuid16 >> 8, 0x00, 0x00     \
    )))

/* 0000xxxx-8c26-476f-89a7-a108033a69c6 */
#define PTS_UUID_DECLARE_ALT(uuid16)                            \
    ((const ble_uuid_t *) (&(ble_uuid128_t) BLE_UUID128_INIT(   \
    0xc6, 0x69, 0x3a, 0x03, 0x08, 0xa1, 0xa7, 0x89,             \
    0x6f, 0x47, 0x26, 0x8c, uuid16, uuid16 >> 8, 0x00, 0x00     \
    )))

#define  PTS_SVC                           0x0001
#define  PTS_CHR_READ                      0x0002
#define  PTS_CHR_WRITE                     0x0003
#define  PTS_CHR_RELIABLE_WRITE            0x0004
#define  PTS_CHR_WRITE_NO_RSP              0x0005
#define  PTS_CHR_READ_WRITE                0x0006
#define  PTS_CHR_READ_WRITE_ENC            0x0007
#define  PTS_CHR_READ_WRITE_AUTHEN         0x0008
#define  PTS_DSC_READ                      0x0009
#define  PTS_DSC_WRITE                     0x000a
#define  PTS_DSC_READ_WRITE                0x000b
#define  PTS_CHR_NOTIFY                    0x0025
#define  PTS_CHR_NOTIFY_ALT                0x0026
#define  PTS_CHR_READ_WRITE_AUTHOR         0x0027
#define  PTS_LONG_CHR_READ_WRITE           0x0015
#define  PTS_LONG_CHR_READ_WRITE_ALT       0x0016
#define  PTS_LONG_DSC_READ_WRITE           0x001b
#define  PTS_INC_SVC                       0x001e
#define  PTS_CHR_READ_WRITE_ALT            0x001f

static uint8_t gatt_svr_pts_static_long_val[300];
static uint8_t gatt_svr_pts_static_val[30];
static uint8_t gatt_svr_pts_static_short_val;
static uint16_t myconn_handle;
static uint16_t notify_handle;
static uint16_t notify_handle_alt;

struct find_attr_data {
    ble_uuid_any_t *uuid;
    int attr_type;
    void *ptr;
    uint16_t handle;
};

struct notify_mult_cb_data {
    size_t tuple_cnt;
    uint16_t handles[BTP_GATT_HL_MAX_CNT];
};

static int
gatt_svr_read_write_test(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt,
                         void *arg);

static int
gatt_svr_read_write_auth_test(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt,
                              void *arg);

static int
gatt_svr_read_write_author_test(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt,
                                void *arg);

static int
gatt_svr_read_write_enc_test(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg);

static int
gatt_svr_dsc_read_write_test(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg);

static int
gatt_svr_write_no_rsp_test(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt,
                           void *arg);

static int
gatt_svr_rel_write_test(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt,
                        void *arg);

static int
gatt_svr_read_write_long_test(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt,
                              void *arg);

static int
gatt_svr_dsc_read_test(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt,
                       void *arg);

static int
gatt_svr_dsc_read_write_long_test(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt,
                                  void *arg);

static const struct ble_gatt_svc_def gatt_svr_inc_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(PTS_INC_SVC),
        .characteristics = (struct ble_gatt_chr_def[]) {{
                                                            .uuid = PTS_UUID_DECLARE(PTS_CHR_READ_WRITE_ALT),
                                                            .access_cb = gatt_svr_read_write_test,
                                                            .flags = BLE_GATT_CHR_F_WRITE |
                                                                     BLE_GATT_CHR_F_READ,
                                                        }, {
                                                            0,
                                                        }},

    },

    {
        0, /* No more services. */
    },
};

static const struct ble_gatt_svc_def *inc_svcs[] = {
    &gatt_svr_inc_svcs[0],
    NULL,
};

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        /*** Service: PTS test. */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = PTS_UUID_DECLARE(PTS_SVC),
        .includes = inc_svcs,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = PTS_UUID_DECLARE(PTS_CHR_READ_WRITE),
                .access_cb = gatt_svr_read_write_test,
                .flags = BLE_GATT_CHR_F_READ |
                         BLE_GATT_CHR_F_WRITE,
                .descriptors = (struct ble_gatt_dsc_def[]) {{
                                                                .uuid = PTS_UUID_DECLARE(PTS_DSC_READ_WRITE),
                                                                .access_cb = gatt_svr_dsc_read_write_test,
                                                                .att_flags = BLE_ATT_F_READ |
                                                                             BLE_ATT_F_WRITE,
                                                            }, {
                                                                .uuid = PTS_UUID_DECLARE(PTS_LONG_DSC_READ_WRITE),
                                                                .access_cb = gatt_svr_dsc_read_write_long_test,
                                                                .att_flags = BLE_ATT_F_READ |
                                                                             BLE_ATT_F_WRITE,
                                                            }, {
                                                                .uuid = PTS_UUID_DECLARE(PTS_DSC_READ),
                                                                .access_cb = gatt_svr_dsc_read_test,
                                                                .att_flags = BLE_ATT_F_READ,
                                                            }, {
                                                                0, /* No more descriptors in this characteristic */
                                                            }}
            }, {
                .uuid = PTS_UUID_DECLARE(PTS_CHR_WRITE_NO_RSP),
                .access_cb = gatt_svr_write_no_rsp_test,
                .flags = BLE_GATT_CHR_F_READ |
                         BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_WRITE_NO_RSP,
            }, {
                .uuid = PTS_UUID_DECLARE(PTS_CHR_READ_WRITE_AUTHEN),
                .access_cb = gatt_svr_read_write_auth_test,
                .flags = BLE_GATT_CHR_F_READ_AUTHEN |
                         BLE_GATT_CHR_F_READ |
                         BLE_GATT_CHR_F_WRITE_AUTHEN |
                         BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_WRITE_AUTHEN,
            }, {
                .uuid = PTS_UUID_DECLARE(PTS_CHR_RELIABLE_WRITE),
                .access_cb = gatt_svr_rel_write_test,
                .flags = BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_RELIABLE_WRITE,
            }, {
                .uuid = PTS_UUID_DECLARE(PTS_CHR_READ_WRITE_ENC),
                .access_cb = gatt_svr_read_write_enc_test,
                .flags = BLE_GATT_CHR_F_READ_ENC |
                         BLE_GATT_CHR_F_READ |
                         BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_WRITE_ENC,
                .min_key_size = 16,
            }, {
                .uuid = PTS_UUID_DECLARE(PTS_LONG_CHR_READ_WRITE),
                .access_cb = gatt_svr_read_write_long_test,
                .flags = BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_READ,
            }, {
                .uuid = PTS_UUID_DECLARE(PTS_LONG_CHR_READ_WRITE_ALT),
                .access_cb = gatt_svr_read_write_long_test,
                .flags = BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_READ,
            }, {
                .uuid = PTS_UUID_DECLARE(PTS_CHR_NOTIFY),
                .access_cb = gatt_svr_read_write_test,
                .val_handle = &notify_handle,
                .flags = BLE_GATT_CHR_F_READ |
                         BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_NOTIFY |
                         BLE_GATT_CHR_F_INDICATE,
            }, {
                .uuid = PTS_UUID_DECLARE(PTS_CHR_NOTIFY_ALT),
                .access_cb = gatt_svr_read_write_test,
                .val_handle = &notify_handle_alt,
                .flags = BLE_GATT_CHR_F_READ |
                         BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_NOTIFY |
                         BLE_GATT_CHR_F_INDICATE,
            },
            {
                .uuid = PTS_UUID_DECLARE(PTS_CHR_READ_WRITE_AUTHOR),
                .access_cb = gatt_svr_read_write_author_test,
                .flags = BLE_GATT_CHR_F_READ_AUTHOR |
                         BLE_GATT_CHR_F_READ |
                         BLE_GATT_CHR_F_WRITE_AUTHOR |
                         BLE_GATT_CHR_F_WRITE,
            }, {
                0, /* No more characteristics in this service. */
            }
        },
    }, {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = PTS_UUID_DECLARE_ALT(PTS_SVC),
        .characteristics = (struct ble_gatt_chr_def[]) {{
                                                            .uuid = PTS_UUID_DECLARE_ALT(PTS_CHR_READ_WRITE),
                                                            .access_cb = gatt_svr_read_write_test,
                                                            .flags = BLE_GATT_CHR_F_WRITE |
                                                                     BLE_GATT_CHR_F_READ,
                                                        }, {
                                                            0, /* No more characteristics in this service */
                                                        }},
    }, {
        0, /* No more services. */
    },
};

static const struct ble_gatt_svc_def *pts_db[] = {
    gatt_svr_svcs, NULL};

static void
attr_value_changed_ev(uint16_t handle, struct os_mbuf *data)
{
    struct btp_gatts_attr_value_changed_ev *ev;
    struct os_mbuf *buf = os_msys_get(0, 0);

    SYS_LOG_DBG("");

    ev = os_mbuf_extend(buf, sizeof(*ev));
    if (!ev) {
        return;
    }

    ev->handle = htole16(handle);
    ev->data_length = htole16(os_mbuf_len(data));
    os_mbuf_appendfrom(buf, data, 0, os_mbuf_len(data));

    tester_event(BTP_SERVICE_ID_GATTS, BTP_GATTS_EV_ATTR_VALUE_CHANGED,
                 buf->om_data, buf->om_len);
}

static int
gatt_svr_chr_write(uint16_t conn_handle, uint16_t attr_handle,
                   struct os_mbuf *om, uint16_t min_len, uint16_t max_len,
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

    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return 0;
    }

    attr_value_changed_ev(attr_handle, om);

    return 0;
}

static uint16_t
extract_uuid16_from_pts_uuid128(const ble_uuid_t *uuid)
{
    const uint8_t *u8ptr;
    uint16_t uuid16;

    u8ptr = BLE_UUID128(uuid)->value;
    uuid16 = u8ptr[12];
    uuid16 |= (uint16_t) u8ptr[13] << 8;

    return uuid16;
}

static int
gatt_svr_read_write_test(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt,
                         void *arg)
{
    uint16_t uuid16;
    int rc;

    uuid16 = extract_uuid16_from_pts_uuid128(ctxt->chr->uuid);
    assert(uuid16 != 0);

    switch (uuid16) {
    case PTS_CHR_READ_WRITE:
    case PTS_CHR_READ_WRITE_ALT:
    case PTS_CHR_NOTIFY:
    case PTS_CHR_NOTIFY_ALT:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            rc = gatt_svr_chr_write(conn_handle, attr_handle,
                                    ctxt->om, 0,
                                    sizeof gatt_svr_pts_static_short_val,
                                    &gatt_svr_pts_static_short_val, NULL);
            return rc;
        } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, &gatt_svr_pts_static_short_val,
                                sizeof gatt_svr_pts_static_short_val);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int
gatt_svr_read_write_long_test(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt,
                              void *arg)
{
    uint16_t uuid16;
    int rc;

    uuid16 = extract_uuid16_from_pts_uuid128(ctxt->chr->uuid);
    assert(uuid16 != 0);

    switch (uuid16) {
    case PTS_LONG_CHR_READ_WRITE:
    case PTS_LONG_CHR_READ_WRITE_ALT:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            rc = gatt_svr_chr_write(conn_handle, attr_handle,
                                    ctxt->om, 0,
                                    sizeof gatt_svr_pts_static_long_val,
                                    &gatt_svr_pts_static_long_val, NULL);
            return rc;
        } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, &gatt_svr_pts_static_long_val,
                                sizeof gatt_svr_pts_static_long_val);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int
gatt_svr_read_write_auth_test(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt,
                              void *arg)
{
    uint16_t uuid16;
    int rc;

    uuid16 = extract_uuid16_from_pts_uuid128(ctxt->chr->uuid);
    assert(uuid16 != 0);

    switch (uuid16) {
    case PTS_CHR_READ_WRITE_AUTHEN:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            rc = gatt_svr_chr_write(conn_handle, attr_handle,
                                    ctxt->om, 0,
                                    sizeof gatt_svr_pts_static_val,
                                    &gatt_svr_pts_static_val, NULL);
            return rc;
        } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, &gatt_svr_pts_static_val,
                                sizeof gatt_svr_pts_static_val);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int
gatt_svr_read_write_author_test(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt,
                                void *arg)
{
    uint16_t uuid16;

    uuid16 = extract_uuid16_from_pts_uuid128(ctxt->chr->uuid);
    assert(uuid16 != 0);

    switch (uuid16) {
    case PTS_CHR_READ_WRITE_AUTHOR:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
        } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
        }
    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int
gatt_svr_read_write_enc_test(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg)
{
    uint16_t uuid16;
    int rc;

    uuid16 = extract_uuid16_from_pts_uuid128(ctxt->chr->uuid);
    assert(uuid16 != 0);

    switch (uuid16) {
    case PTS_CHR_READ_WRITE_ENC:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, &gatt_svr_pts_static_val,
                                sizeof gatt_svr_pts_static_val);
            return rc;
        } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            rc = gatt_svr_chr_write(conn_handle, attr_handle,
                                    ctxt->om, 0,
                                    sizeof gatt_svr_pts_static_val,
                                    &gatt_svr_pts_static_val, NULL);
            return rc;
        }
    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int
gatt_svr_dsc_read_write_test(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg)
{
    uint16_t uuid16;
    int rc;

    uuid16 = extract_uuid16_from_pts_uuid128(ctxt->chr->uuid);
    assert(uuid16 != 0);

    switch (uuid16) {
    case PTS_DSC_READ_WRITE:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_DSC) {
            rc = gatt_svr_chr_write(conn_handle, attr_handle,
                                    ctxt->om, 0,
                                    sizeof gatt_svr_pts_static_short_val,
                                    &gatt_svr_pts_static_short_val, NULL);
            return rc;
        } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
            rc = os_mbuf_append(ctxt->om, &gatt_svr_pts_static_short_val,
                                sizeof gatt_svr_pts_static_short_val);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int
gatt_svr_dsc_read_write_long_test(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt,
                                  void *arg)
{
    uint16_t uuid16;
    int rc;

    uuid16 = extract_uuid16_from_pts_uuid128(ctxt->chr->uuid);
    assert(uuid16 != 0);

    switch (uuid16) {
    case PTS_LONG_DSC_READ_WRITE:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_DSC) {
            rc = gatt_svr_chr_write(conn_handle, attr_handle,
                                    ctxt->om, 0,
                                    sizeof gatt_svr_pts_static_long_val,
                                    &gatt_svr_pts_static_long_val, NULL);
            return rc;
        } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
            rc = os_mbuf_append(ctxt->om, &gatt_svr_pts_static_long_val,
                                sizeof gatt_svr_pts_static_long_val);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int
gatt_svr_dsc_read_test(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt,
                       void *arg)
{
    uint16_t uuid16;
    int rc;

    uuid16 = extract_uuid16_from_pts_uuid128(ctxt->chr->uuid);
    assert(uuid16 != 0);

    switch (uuid16) {
    case PTS_DSC_READ:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
            rc = os_mbuf_append(ctxt->om, &gatt_svr_pts_static_long_val,
                                sizeof gatt_svr_pts_static_long_val);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int
gatt_svr_write_no_rsp_test(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt,
                           void *arg)
{
    uint16_t uuid16;
    int rc;

    uuid16 = extract_uuid16_from_pts_uuid128(ctxt->chr->uuid);
    assert(uuid16 != 0);

    switch (uuid16) {
    case PTS_CHR_WRITE_NO_RSP:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            rc = gatt_svr_chr_write(conn_handle, attr_handle,
                                    ctxt->om, 0,
                                    sizeof gatt_svr_pts_static_short_val,
                                    &gatt_svr_pts_static_short_val, NULL);
            return rc;
        } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, &gatt_svr_pts_static_short_val,
                                sizeof gatt_svr_pts_static_short_val);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int
gatt_svr_rel_write_test(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt,
                        void *arg)
{
    uint16_t uuid16;
    int rc;

    uuid16 = extract_uuid16_from_pts_uuid128(ctxt->chr->uuid);
    assert(uuid16 != 0);

    switch (uuid16) {
    case PTS_CHR_RELIABLE_WRITE:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            rc = gatt_svr_chr_write(conn_handle, attr_handle,
                                    ctxt->om, 0,
                                    sizeof gatt_svr_pts_static_val,
                                    &gatt_svr_pts_static_val, NULL);
            return rc;
        }

    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static uint8_t
start_server(const void *cmd, uint16_t cmd_len,
             void *rsp, uint16_t *rsp_len)
{
    struct btp_gatts_start_server_rp *rp = rsp;

    SYS_LOG_DBG("");

    ble_gatts_show_local();

    ble_svc_gatt_changed(0x0001, 0xffff);

    rp->db_attr_off = 0;
    rp->db_attr_cnt = 0;

    *rsp_len = sizeof(*rp);

    return BTP_STATUS_SUCCESS;
}

/* Convert UUID from BTP command to bt_uuid */
static uint8_t
btp2bt_uuid(const uint8_t *uuid, uint8_t len,
            ble_uuid_any_t *bt_uuid)
{
    uint16_t le16;

    switch (len) {
    case 0x02: /* UUID 16 */
        bt_uuid->u.type = BLE_UUID_TYPE_16;
        memcpy(&le16, uuid, sizeof(le16));
        BLE_UUID16(bt_uuid)->value = le16toh(le16);
        break;
    case 0x10: /* UUID 128*/
        bt_uuid->u.type = BLE_UUID_TYPE_128;
        memcpy(BLE_UUID128(bt_uuid)->value, uuid, 16);
        break;
    default:
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

#define BTP_PERM_F_READ                      0x01
#define BTP_PERM_F_WRITE                     0x02
#define BTP_PERM_F_READ_ENC                  0x04
#define BTP_PERM_F_WRITE_ENC                 0x08
#define BTP_PERM_F_READ_AUTHEN               0x10
#define BTP_PERM_F_WRITE_AUTHEN              0x20
#define BTP_PERM_F_READ_AUTHOR               0x40
#define BTP_PERM_F_WRITE_AUTHOR              0x80

static int flags_hs2btp_map[] = {
    BTP_PERM_F_READ,
    BTP_PERM_F_WRITE,
    BTP_PERM_F_READ_ENC,
    BTP_PERM_F_READ_AUTHEN,
    BTP_PERM_F_READ_AUTHOR,
    BTP_PERM_F_WRITE_ENC,
    BTP_PERM_F_WRITE_AUTHEN,
    BTP_PERM_F_WRITE_AUTHOR,
};

static uint8_t
flags_hs2btp(uint8_t flags)
{
    int i;
    uint8_t ret = 0;

    for (i = 0; i < 8; ++i) {
        if (flags & BIT(i)) {
            ret |= flags_hs2btp_map[i];
        }
    }

    return ret;
}

static uint8_t
get_attrs(const void *cmd, uint16_t cmd_len,
          void *rsp, uint16_t *rsp_len)
{
    const struct btp_gatts_get_attributes_cmd *cp = cmd;
    struct btp_gatts_get_attributes_rp *rp = rsp;
    struct btp_gatts_attr *gatt_attr;
    struct os_mbuf *buf = os_msys_get(0, 0);
    uint16_t start_handle, end_handle;
    struct ble_att_svr_entry *entry = NULL;
    ble_uuid_any_t uuid;
    ble_uuid_t *uuid_ptr = NULL;
    uint8_t count = 0;
    char str[BLE_UUID_STR_LEN];
    uint8_t status = BTP_STATUS_SUCCESS;

    SYS_LOG_DBG("");

    if (!buf) {
        return BTP_STATUS_FAILED;
    }

    memset(str, 0, sizeof(str));
    memset(&uuid, 0, sizeof(uuid));
    start_handle = le16toh(cp->start_handle);
    end_handle = le16toh(cp->end_handle);

    if (cp->type_length) {
        if (btp2bt_uuid(cp->type, cp->type_length, &uuid)) {
            status = BTP_STATUS_FAILED;
            goto done;
        }

        ble_uuid_to_str(&uuid.u, str);
        SYS_LOG_DBG("start 0x%04x end 0x%04x, uuid %s", start_handle,
                    end_handle, str);

        uuid_ptr = &uuid.u;
    } else {
        SYS_LOG_DBG("start 0x%04x end 0x%04x", start_handle, end_handle);
    }

    entry = ble_att_svr_find_by_uuid(entry, uuid_ptr, end_handle);
    while (entry) {

        if (entry->ha_handle_id < start_handle) {
            entry = ble_att_svr_find_by_uuid(entry,
                                             uuid_ptr, end_handle);
            continue;
        }

        gatt_attr = os_mbuf_extend(buf, sizeof(*gatt_attr));
        if (!gatt_attr) {
            status = BTP_STATUS_FAILED;
            goto done;
        }
        gatt_attr->handle = htole16(entry->ha_handle_id);
        gatt_attr->permission = flags_hs2btp(entry->ha_flags);

        if (entry->ha_uuid->type == BLE_UUID_TYPE_16) {
            uint16_t uuid_val;

            gatt_attr->type_length = 2;
            uuid_val = htole16(BLE_UUID16(entry->ha_uuid)->value);
            if (os_mbuf_append(buf, &uuid_val, sizeof(uuid_val))) {
                status = BTP_STATUS_FAILED;
                goto done;
            }
        } else {
            gatt_attr->type_length = 16;
            if (os_mbuf_append(buf, BLE_UUID128(entry->ha_uuid)->value,
                               gatt_attr->type_length)) {
                status = BTP_STATUS_FAILED;
                goto done;
            }
        }

        count++;

        entry = ble_att_svr_find_by_uuid(entry, uuid_ptr, end_handle);
    }

    rp->attrs_count = count;
    os_mbuf_copydata(buf, 0, os_mbuf_len(buf), rp->attrs);

    *rsp_len = sizeof(*rp) + os_mbuf_len(buf);

done:
    os_mbuf_free_chain(buf);
    return status;
}

static uint8_t
get_attr_val(const void *cmd, uint16_t cmd_len,
             void *rsp, uint16_t *rsp_len)
{
    const struct btp_gatts_get_attribute_value_cmd *cp = cmd;
    struct btp_gatts_get_attribute_value_rp *rp;
    struct ble_gap_conn_desc conn;
    struct os_mbuf *buf = os_msys_get(0, 0);
    uint16_t handle = le16toh(cp->handle);
    uint8_t out_att_err = 0;
    int conn_status;
    uint8_t status = BTP_STATUS_SUCCESS;

    conn_status = ble_gap_conn_find_by_addr(&cp->address, &conn);

    if (conn_status) {
        rp = os_mbuf_extend(buf, sizeof(*rp));
        if (!rp) {
            status = BTP_STATUS_FAILED;
            goto free;
        }

        ble_att_svr_read_handle(BLE_HS_CONN_HANDLE_NONE,
                                handle, 0, buf,
                                &out_att_err);

        rp->att_response = out_att_err;
        rp->value_length = os_mbuf_len(buf) - sizeof(*rp);

        os_mbuf_copydata(buf, 0, os_mbuf_len(buf), rsp);
        *rsp_len = os_mbuf_len(buf);

        goto free;
    } else {
        rp = os_mbuf_extend(buf, sizeof(*rp));
        if (!rp) {
            status = BTP_STATUS_FAILED;
            goto free;
        }

        ble_att_svr_read_handle(conn.conn_handle,
                                handle, 0, buf,
                                &out_att_err);

        rp->att_response = out_att_err;
        rp->value_length = os_mbuf_len(buf) - sizeof(*rp);

        os_mbuf_copydata(buf, 0, os_mbuf_len(buf), rsp);
        *rsp_len = os_mbuf_len(buf);

        goto free;
    }

free:
    os_mbuf_free_chain(buf);
    return status;
}

static int
notify_multiple(uint16_t conn_handle, void *arg)
{
    struct notify_mult_cb_data *notify_data =
        (struct notify_mult_cb_data *) arg;
    int rc;

    SYS_LOG_DBG("")

    rc = ble_gatts_notify_multiple(conn_handle,
                                   notify_data->tuple_cnt,
                                   notify_data->handles);

    return rc;
}

static uint8_t
set_values(const void *cmd, uint16_t cmd_len,
           void *rsp, uint16_t *rsp_len)
{
    const struct btp_gatts_set_chrc_value_cmd *cp = cmd;
    struct os_mbuf *buf;
    struct notify_mult_cb_data cb_data;
    uint16_t conn_handle;
    uint16_t val_idx = 0;
    uint16_t handle;
    uint16_t len;
    uint8_t cccd_value;
    int i;
    int rc = 0;

    for (i = 0; i < cp->count; i++) {
        buf = ble_hs_mbuf_att_pkt();
        handle = cp->hl[i].handle;
        len = cp->hl[i].len;
        os_mbuf_append(buf, cp->value + val_idx, len);
        ble_att_svr_write_local(handle, buf);
        val_idx += len;
        cb_data.handles[i] = handle;
    }

    if (cp->count == 1) {
        rc = ble_gatts_read_cccd(myconn_handle, handle, &cccd_value);

        if (rc != 0) {
            return BTP_STATUS_FAILED;
        }

        if (cccd_value == BLE_GATT_CCCD_NOTIFY) {
            rc = ble_gatts_notify_custom(myconn_handle, handle, buf);
        }
        if (cccd_value == BLE_GATT_CCCD_INDICATE) {
            rc = ble_gatts_indicate_custom(myconn_handle, handle, buf);
        }
    } else {
        cb_data.tuple_cnt = cp->count;
        if (ble_addr_cmp(&cp->address, BLE_ADDR_ANY)) {
            ble_gap_conn_foreach_handle(notify_multiple, &cb_data);
        } else {
            ble_gap_conn_find_handle_by_addr(&cp->address, &conn_handle);
            rc = notify_multiple(conn_handle, &cb_data);
        }
    }

    if (rc != 0) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
change_database(const void *cmd, uint16_t cmd_len,
                void *rsp, uint16_t *rsp_len)
{
    const struct btp_gatts_change_database_cmd *cp = cmd;

    SYS_LOG_DBG("")

    ble_gatts_show_local();

    ble_svc_gatt_changed(cp->start_handle, cp->end_handle);

    return BTP_STATUS_SUCCESS;
}

uint8_t
set_visibility(uint8_t db_id, int visible)
{
    int i, j, rc;
    uint16_t handle;

    for (i = 0; pts_db[db_id][i].type != 0 ; i++) {
        rc = ble_gatts_find_svc(pts_db[db_id][i].uuid, &handle);
        if (rc != 0) {
            return rc;
        }
        rc = ble_gatts_svc_set_visibility(handle, visible);
        if (rc != 0) {
            return rc;
        }

        if (!pts_db[db_id][i].includes) {
            continue;
        }

        if (pts_db[db_id][i].includes) {
            for (j = 0; pts_db[db_id][i].includes[j] != NULL; j++) {
                rc = ble_gatts_find_svc(pts_db[db_id][i].includes[j]->uuid,
                                        &handle);
                if (rc != 0) {
                    return rc;
                }

                rc = ble_gatts_svc_set_visibility(handle, visible);
                if (rc != 0) {
                    return rc;
                }
            }
        }
    }

    return 0;
}

static uint8_t
initialize_database(const void *cmd, uint16_t cmd_len,
                    void *rsp, uint16_t *rsp_len)
{
    const struct btp_gatts_initialize_database_cmd *cp = cmd;
    int rc;

    rc = set_visibility(cp->id, 1);
    if (rc != 0) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
supported_commands(const void *cmd, uint16_t cmd_len,
                   void *rsp, uint16_t *rsp_len)
{
    struct btp_gatts_read_supported_commands_rp *rp = rsp;

    /* octet 0 */
    tester_set_bit(rp->data, BTP_GATTS_READ_SUPPORTED_COMMANDS);
    tester_set_bit(rp->data, BTP_GATTS_INITIALIZE_DATABASE);
    tester_set_bit(rp->data, BTP_GATTS_GET_ATTRIBUTES);
    tester_set_bit(rp->data, BTP_GATTS_GET_ATTRIBUTE_VALUE);
    tester_set_bit(rp->data, BTP_GATTS_SET_CHRC_VALUE);
    tester_set_bit(rp->data, BTP_GATTS_CHANGE_DATABASE);
    tester_set_bit(rp->data, BTP_GATTS_START_SERVER);
    *rsp_len = sizeof(*rp) + 4;

    return BTP_STATUS_SUCCESS;
}

enum attr_type {
    BLE_GATT_ATTR_SVC = 0,
    BLE_GATT_ATTR_CHR,
    BLE_GATT_ATTR_DSC,
};

static const struct btp_handler handlers[] = {
    {
        .opcode = BTP_GATTS_READ_SUPPORTED_COMMANDS,
        .index = BTP_INDEX_NONE,
        .expect_len = 0,
        .func = supported_commands,
    },
    {
        .opcode = BTP_GATTS_INITIALIZE_DATABASE,
        .expect_len = sizeof(struct btp_gatts_initialize_database_cmd),
        .func = initialize_database,
    },
    {
        .opcode = BTP_GATTS_GET_ATTRIBUTES,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = get_attrs,
    },
    {
        .opcode = BTP_GATTS_GET_ATTRIBUTE_VALUE,
        .expect_len = sizeof(struct btp_gatts_get_attribute_value_cmd),
        .func = get_attr_val,
    },
    {
        .opcode = BTP_GATTS_CHANGE_DATABASE,
        .expect_len = sizeof(struct btp_gatts_change_database_cmd),
        .func = change_database,
    },
    {
        .opcode = BTP_GATTS_SET_CHRC_VALUE,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = set_values,
    },
    {
        .opcode = BTP_GATTS_START_SERVER,
        .expect_len = 0,
        .func = start_server,
    },
};

static struct bt_gatt_subscribe_params {
    uint16_t ccc_handle;
    uint16_t value;
    uint16_t value_handle;
} subscribe_params;

uint8_t
register_database(void)
{
    int rc;
    int i;

    for (i = 0; i < BTP_MAX_PTS_SVCS; i++) {
        rc = set_visibility(i, 0);

        if (rc != 0) {
            return BTP_STATUS_FAILED;
        }
    }

    return BTP_STATUS_SUCCESS;
}

int
tester_gatts_subscribe_ev(uint16_t conn_handle,
                          uint16_t attr_handle,
                          uint8_t reason,
                          uint8_t prev_notify,
                          uint8_t cur_notify,
                          uint8_t prev_indicate,
                          uint8_t cur_indicate)
{
    SYS_LOG_DBG("");
    myconn_handle = conn_handle;

    if (cur_notify == 0 && cur_indicate == 0) {
        SYS_LOG_INF("Unsubscribed; conn_handle=%d\n", conn_handle);
        memset(&subscribe_params, 0, sizeof(subscribe_params));
        return 0;
    }

    if (cur_notify) {
        SYS_LOG_INF("Subscribed to notifications; conn_handle=%d, "
                    "attr_handle=%d\n", conn_handle, attr_handle);
    }

    if (cur_indicate) {
        SYS_LOG_INF("Subscribed to indications; conn_handle=%d, "
                    "attr_handle=%d\n", conn_handle, attr_handle);
    }

    return 0;
}

void
gatts_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
    MODLOG_DFLT(DEBUG,
                "registered service %s with handle=%d\n",
                ble_uuid_to_str(
                    ctxt->svc.svc_def->uuid,
                    buf),
                ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
    MODLOG_DFLT(DEBUG,
                "registering characteristic %s with "
                "def_handle=%d val_handle=%d\n",
                ble_uuid_to_str(
                    ctxt->chr.chr_def->uuid,
                    buf),
                ctxt->chr.def_handle,
                ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
    MODLOG_DFLT(DEBUG,
                "registering descriptor %s with handle=%d\n",
                ble_uuid_to_str(
                    ctxt->dsc.dsc_def->uuid,
                    buf),
                ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

int
gatts_svr_init(void)
{
    int rc;
    int i;

    for (i = 0; i < BTP_MAX_PTS_SVCS; i++) {
        rc = ble_gatts_count_cfg(pts_db[i][0].includes[0]);
        if (rc != 0) {
            return rc;
        }

        rc = ble_gatts_add_svcs(pts_db[i][0].includes[0]);
        if (rc != 0) {
            return rc;
        }

        rc = ble_gatts_count_cfg(pts_db[i]);
        if (rc != 0) {
            return rc;
        }

        rc = ble_gatts_add_svcs(pts_db[i]);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}


uint8_t
tester_init_gatts(void)
{
    tester_register_command_handlers(BTP_SERVICE_ID_GATTS, handlers,
                                     ARRAY_SIZE(handlers));

    return BTP_STATUS_SUCCESS;
}

uint8_t
tester_unregister_gatts(void)
{
    return BTP_STATUS_SUCCESS;
}
