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

#include "audio/ble_audio.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "audio/ble_audio_codec.h"
#include "services/pacs/ble_audio_svc_pacs.h"

static uint32_t ble_svc_audio_pacs_sink_audio_locations;
static uint32_t ble_svc_audio_pacs_source_audio_locations;
static struct available_ctx {
    uint16_t conn_handle;
    uint16_t ble_svc_audio_pacs_avail_sink_contexts;
    uint16_t ble_svc_audio_pacs_avail_source_contexts;
}  ble_svc_audio_pacs_avail_contexts[MYNEWT_VAL(BLE_MAX_CONNECTIONS)] = {
    [0 ... MYNEWT_VAL(BLE_MAX_CONNECTIONS) - 1] = {
        .conn_handle = BLE_HS_CONN_HANDLE_NONE,
        .ble_svc_audio_pacs_avail_sink_contexts = 0,
        .ble_svc_audio_pacs_avail_source_contexts = 0
    }
};
static uint16_t ble_svc_audio_pacs_sup_sink_contexts;
static uint16_t ble_svc_audio_pacs_sup_source_contexts;

static struct ble_gap_event_listener ble_pacs_listener;
static struct ble_audio_event_listener ble_audio_listener;

struct pac_read_cb_arg {
    struct os_mbuf *om;
    uint8_t pac_count;
};

static int
ble_svc_audio_pacs_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def ble_svc_audio_pacs_defs[] = {
    { /*** Service: Published Audio Capabilities Service (PACS) */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_AUDIO_PACS_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) {
#if MYNEWT_VAL(BLE_SVC_AUDIO_PACS_SINK_PAC)
            {
                .uuid = BLE_UUID16_DECLARE(BLE_SVC_AUDIO_PACS_CHR_UUID16_SINK_PAC),
                .access_cb = ble_svc_audio_pacs_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC
#if MYNEWT_VAL(BLE_SVC_AUDIO_PACS_SINK_PAC_NOTIFY)
                         | BLE_GATT_CHR_F_NOTIFY
#endif
            },
#endif
#if MYNEWT_VAL(BLE_SVC_AUDIO_PACS_SINK_AUDIO_LOCATIONS)
            {
                .uuid = BLE_UUID16_DECLARE(
                    BLE_SVC_AUDIO_PACS_CHR_UUID16_SINK_AUDIO_LOCATIONS),
                .access_cb = ble_svc_audio_pacs_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC
#if MYNEWT_VAL(BLE_SVC_AUDIO_PACS_SINK_AUDIO_LOCATIONS_NOTIFY)
                         | BLE_GATT_CHR_F_NOTIFY
#endif
            },
#endif
#if MYNEWT_VAL(BLE_SVC_AUDIO_PACS_SOURCE_PAC)
            {
                .uuid = BLE_UUID16_DECLARE(
                    BLE_SVC_AUDIO_PACS_CHR_UUID16_SOURCE_PAC),
                .access_cb = ble_svc_audio_pacs_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC
#if MYNEWT_VAL(BLE_SVC_AUDIO_PACS_SOURCE_PAC_NOTIFY)
                         | BLE_GATT_CHR_F_NOTIFY
#endif
            },
#endif
#if MYNEWT_VAL(BLE_SVC_AUDIO_PACS_SOURCE_AUDIO_LOCATIONS)
            {
                .uuid = BLE_UUID16_DECLARE(
                    BLE_SVC_AUDIO_PACS_CHR_UUID16_SOURCE_AUDIO_LOCATIONS),
                .access_cb = ble_svc_audio_pacs_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC
#if MYNEWT_VAL(BLE_SVC_AUDIO_PACS_SOURCE_AUDIO_LOCATIONS_NOTIFY)
                         | BLE_GATT_CHR_F_NOTIFY
#endif
            },
#endif
            {
                .uuid = BLE_UUID16_DECLARE(
                    BLE_SVC_AUDIO_PACS_CHR_UUID16_AVAILABLE_AUDIO_CONTEXTS),
                .access_cb = ble_svc_audio_pacs_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
            }, {
                .uuid = BLE_UUID16_DECLARE(
                    BLE_SVC_AUDIO_PACS_CHR_UUID16_SUPPORTED_AUDIO_CONTEXTS),
                .access_cb = ble_svc_audio_pacs_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC
#if MYNEWT_VAL(BLE_SVC_AUDIO_PACS_SUP_AUDIO_CTX_NOTIFY)
                         | BLE_GATT_CHR_F_NOTIFY
#endif
            }, {
                0, /* No more characteristics in this service */
            }
        }
    },
    {
        0, /* No more services. */
    },
};

static int
codec_record_to_pacs_entry(const struct ble_audio_codec_record *record, void *arg)
{
    struct pac_read_cb_arg *cb_arg = (struct pac_read_cb_arg *)arg;
    uint8_t *buf;
    int rc;

    rc = os_mbuf_append(cb_arg->om, &record->codec_id.format, sizeof(uint8_t));
    if (rc) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    buf = os_mbuf_extend(cb_arg->om, 4);
    if (buf == NULL) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    put_le16(buf + 0, record->codec_id.company_id);
    put_le16(buf + 2, record->codec_id.vendor_specific);

    rc = os_mbuf_append(cb_arg->om, &record->codec_spec_caps_len, sizeof(uint8_t));
    if (rc) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    rc = os_mbuf_append(cb_arg->om, record->codec_spec_caps, record->codec_spec_caps_len);
    if (rc) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    rc = os_mbuf_append(cb_arg->om, &record->metadata_len, sizeof(uint8_t));
    if (rc) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    rc = os_mbuf_append(cb_arg->om, record->metadata, record->metadata_len);
    if (rc) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    cb_arg->pac_count++;

    return 0;
}

static int
ble_svc_audio_pacs_sink_pac_read_access(struct ble_gatt_access_ctxt *ctxt)
{
    struct pac_read_cb_arg cb_arg = {
        .om = ctxt->om,
        .pac_count = 0
    };
    int rc;
    uint8_t *pac_count;

    pac_count = os_mbuf_extend(ctxt->om, sizeof(uint8_t));
    rc = ble_audio_codec_foreach(BLE_AUDIO_CODEC_DIR_SINK_BIT,
                                 codec_record_to_pacs_entry, (void *)&cb_arg);

    *pac_count = cb_arg.pac_count;

    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int
ble_svc_audio_pacs_source_pac_read_access(struct ble_gatt_access_ctxt *ctxt)
{
    struct pac_read_cb_arg cb_arg = {
        .om = ctxt->om,
        .pac_count = 0
    };
    int rc;
    uint8_t *pac_count;

    pac_count = os_mbuf_extend(ctxt->om, sizeof(uint8_t));
    rc = ble_audio_codec_foreach(BLE_AUDIO_CODEC_DIR_SOURCE_BIT,
                                 codec_record_to_pacs_entry, (void *)&cb_arg);

    *pac_count = cb_arg.pac_count;

    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int
ble_svc_audio_pacs_sink_audio_loc_read_access(struct ble_gatt_access_ctxt *
                                              ctxt)
{
    uint8_t *buf;

    buf = os_mbuf_extend(ctxt->om, 4);
    if (buf == NULL) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    put_le32(buf + 0, ble_svc_audio_pacs_sink_audio_locations);

    return 0;
}

static int
ble_svc_audio_pacs_source_audio_loc_read_access(struct ble_gatt_access_ctxt *
                                                ctxt)
{
    uint8_t *buf;

    buf = os_mbuf_extend(ctxt->om, 4);
    if (buf == NULL) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    put_le32(buf + 0, ble_svc_audio_pacs_source_audio_locations);

    return 0;
}

static struct available_ctx *
ble_svc_audio_pacs_find_avail_ctx(uint16_t conn_handle)
{
    int i;

    for (i = 0; i < MYNEWT_VAL(BLE_MAX_CONNECTIONS); i++) {
        if (ble_svc_audio_pacs_avail_contexts[i].conn_handle == conn_handle) {
            return &ble_svc_audio_pacs_avail_contexts[i];
        }
    }
    return NULL;
}

static int
ble_svc_audio_pacs_avail_audio_ctx_read_access(uint16_t conn_handle,
                                               struct ble_gatt_access_ctxt *ctxt)
{
    struct available_ctx *avail_ctx;
    uint8_t *buf;

    avail_ctx = ble_svc_audio_pacs_find_avail_ctx(conn_handle);

    buf = os_mbuf_extend(ctxt->om, 4);
    if (buf == NULL) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    put_le16(buf + 0, avail_ctx->ble_svc_audio_pacs_avail_sink_contexts);
    put_le16(buf + 2, avail_ctx->ble_svc_audio_pacs_avail_source_contexts);

    return 0;
}

static int
ble_svc_audio_pacs_sup_audio_ctx_read_access(struct ble_gatt_access_ctxt
                                             *ctxt)
{
    uint8_t *buf;

    buf = os_mbuf_extend(ctxt->om, 4);
    if (buf == NULL) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    put_le16(buf + 0, ble_svc_audio_pacs_sup_sink_contexts);
    put_le16(buf + 2, ble_svc_audio_pacs_sup_source_contexts);

    return 0;
}

static int
ble_svc_audio_pacs_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);
    int rc;

    switch (uuid16) {
    case BLE_SVC_AUDIO_PACS_CHR_UUID16_SINK_PAC:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = ble_svc_audio_pacs_sink_pac_read_access(ctxt);
        } else {
            assert(0);
            rc = BLE_ATT_ERR_UNLIKELY;
        }
        return rc;
    case BLE_SVC_AUDIO_PACS_CHR_UUID16_SINK_AUDIO_LOCATIONS:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = ble_svc_audio_pacs_sink_audio_loc_read_access(ctxt);
        } else {
            rc = BLE_ATT_ERR_REQ_NOT_SUPPORTED;
        }
        return rc;
    case BLE_SVC_AUDIO_PACS_CHR_UUID16_SOURCE_PAC:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = ble_svc_audio_pacs_source_pac_read_access(ctxt);
        } else {
            assert(0);
            rc = BLE_ATT_ERR_UNLIKELY;
        }
        return rc;
    case BLE_SVC_AUDIO_PACS_CHR_UUID16_SOURCE_AUDIO_LOCATIONS:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = ble_svc_audio_pacs_source_audio_loc_read_access(ctxt);
        } else {
            rc = BLE_ATT_ERR_REQ_NOT_SUPPORTED;
        }
        return rc;
    case BLE_SVC_AUDIO_PACS_CHR_UUID16_AVAILABLE_AUDIO_CONTEXTS:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = ble_svc_audio_pacs_avail_audio_ctx_read_access(conn_handle,
                                                                ctxt);
        } else {
            assert(0);
            rc = BLE_ATT_ERR_UNLIKELY;
        }
        return rc;
    case BLE_SVC_AUDIO_PACS_CHR_UUID16_SUPPORTED_AUDIO_CONTEXTS:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = ble_svc_audio_pacs_sup_audio_ctx_read_access(ctxt);
        } else {
            assert(0);
            rc = BLE_ATT_ERR_UNLIKELY;
        }
        return rc;
    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int
pac_notify(uint16_t chrc_uuid)
{
    uint16_t chr_val_handle;
    int rc;

    if (!ble_hs_is_enabled()) {
        /* Do not notify if host has not started yet */
        return 0;
    }

    rc = ble_gatts_find_chr(BLE_UUID16_DECLARE(BLE_SVC_AUDIO_PACS_UUID16),
                            BLE_UUID16_DECLARE(chrc_uuid), NULL, &chr_val_handle);

    if (!rc) {
        ble_gatts_chr_updated(chr_val_handle);
    }

    return rc;
}

int
ble_svc_audio_pacs_set(uint8_t flags, struct ble_svc_audio_pacs_set_param *param)
{
    int rc;

    if (flags & BLE_AUDIO_CODEC_DIR_SOURCE_BIT) {
        ble_svc_audio_pacs_source_audio_locations = param->audio_locations;
        ble_svc_audio_pacs_sup_source_contexts = param->supported_contexts;
        rc = pac_notify(BLE_SVC_AUDIO_PACS_CHR_UUID16_SOURCE_AUDIO_LOCATIONS);
        if (rc != 0) {
            return rc;
        }
    }

    if (flags & BLE_AUDIO_CODEC_DIR_SINK_BIT) {
        ble_svc_audio_pacs_sink_audio_locations = param->audio_locations;
        ble_svc_audio_pacs_sup_sink_contexts = param->supported_contexts;
        rc = pac_notify(BLE_SVC_AUDIO_PACS_CHR_UUID16_SOURCE_AUDIO_LOCATIONS);
        if (rc != 0) {
            return rc;
        }
    }

    return pac_notify(BLE_SVC_AUDIO_PACS_CHR_UUID16_SUPPORTED_AUDIO_CONTEXTS);
}

int
ble_svc_audio_pacs_avail_contexts_set(uint16_t conn_handle,
                                      uint16_t sink_contexts,
                                      uint16_t source_contexts)
{
    struct available_ctx *avail_ctx = ble_svc_audio_pacs_find_avail_ctx(conn_handle);

    avail_ctx->ble_svc_audio_pacs_avail_sink_contexts = sink_contexts;
    avail_ctx->ble_svc_audio_pacs_avail_source_contexts = source_contexts;

    return pac_notify(BLE_SVC_AUDIO_PACS_CHR_UUID16_AVAILABLE_AUDIO_CONTEXTS);
}

static int
ble_pacs_audio_event(struct ble_audio_event *event, void *arg)
{
    uint8_t codec_dir;

    if (event->type == BLE_AUDIO_EVENT_CODEC_REGISTERED ||
        event->type == BLE_AUDIO_EVENT_CODEC_UNREGISTERED) {
        codec_dir = event->type == BLE_AUDIO_EVENT_CODEC_REGISTERED ?
                    event->codec_registered.record->direction :
                    event->codec_unregistered.record->direction;

        if (codec_dir & BLE_AUDIO_CODEC_DIR_SOURCE_BIT) {
            pac_notify(BLE_SVC_AUDIO_PACS_CHR_UUID16_SOURCE_PAC);
        }

        if (codec_dir & BLE_AUDIO_CODEC_DIR_SINK_BIT) {
            pac_notify(BLE_SVC_AUDIO_PACS_CHR_UUID16_SINK_PAC);
        }
    }

    return 0;
}

static int
ble_pacs_gap_event(struct ble_gap_event *event, void *arg)
{
    struct available_ctx *avail_ctx;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            break;
        }
        avail_ctx = ble_svc_audio_pacs_find_avail_ctx(BLE_HS_CONN_HANDLE_NONE);
        avail_ctx->conn_handle = event->connect.conn_handle;
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        avail_ctx = ble_svc_audio_pacs_find_avail_ctx(event->disconnect.conn.conn_handle);
        if (avail_ctx >= 0) {
            avail_ctx->conn_handle = BLE_HS_CONN_HANDLE_NONE;
        }
        break;
    default:
        break;
    }
    return 0;
}

void
ble_svc_audio_pacs_init(void)
{
    int rc;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    rc = ble_gatts_count_cfg(ble_svc_audio_pacs_defs);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gatts_add_svcs(ble_svc_audio_pacs_defs);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gap_event_listener_register(&ble_pacs_listener,
                                         ble_pacs_gap_event, NULL);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_audio_event_listener_register(&ble_audio_listener, ble_pacs_audio_event, NULL);
    SYSINIT_PANIC_ASSERT(rc == 0);
    (void)rc;
}
