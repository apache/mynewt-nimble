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

#include "ble_hs_priv.h"
#include "host/ble_gap.h"
#include "host/ble_uuid.h"
#include "host/ble_audio_broadcast_sink.h"
#include "syscfg/syscfg.h"

#if MYNEWT_VAL(BLE_ISO_BROADCAST_SINK)

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

struct uuid_found_status {
    ble_uuid_any_t uuid_to_find;
    const uint8_t *svc_data;
    uint8_t svc_data_len;
};

static struct {
    uint8_t filter_policy;
    ble_uuid_any_t uuid;
    uint32_t broadcast_id;

    ble_audio_broadcast_fn *cb;
    void *cb_arg;
} broadcast_sink_event_handler;

static int
find_uuid_func(const struct ble_hs_adv_field *field, void *user_data)
{
    struct uuid_found_status *uuid_find_status = user_data;
    uint8_t svc_uuid_type_to_find;
    ble_uuid_any_t svc_uuid;
    int match_result;

    switch (uuid_find_status->uuid_to_find.u.type) {
    case BLE_UUID_TYPE_16:
        if (field->type != BLE_HS_ADV_TYPE_SVC_DATA_UUID16) {
            return BLE_HS_ENOENT;
        }

        match_result = (uuid_find_status->uuid_to_find.u16.value ==
                        get_le16(field->value));
        break;
    case BLE_UUID_TYPE_32:
        if (field->type != BLE_HS_ADV_TYPE_SVC_DATA_UUID32) {
            return BLE_HS_ENOENT;
        }

        match_result = (uuid_find_status->uuid_to_find.u32.value ==
                        get_le32(field->value));
        break;
    case BLE_UUID_TYPE_128:
        if (field->type != BLE_HS_ADV_TYPE_SVC_DATA_UUID128) {
            return BLE_HS_ENOENT;
        }
        match_result = !memcmp(uuid_find_status->uuid_to_find.u128.value,
                               field->value, 16);
        break;
    default:
        return BLE_HS_EINVAL;
    }

    if (match_result) {
        uuid_find_status->svc_data = field->value;
        uuid_find_status->svc_data_len = field->length;
        return 0;
    }

    return BLE_HS_ENOENT;
}

static int
ble_audio_broadcast_sink_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_broadcast_event ev;
    const struct ble_hs_adv_field *parsed_ad_field;
    struct uuid_found_status uuid_find_status = {
        .uuid_to_find = broadcast_sink_event_handler.uuid,
        .svc_data = NULL,
        .svc_data_len = 0
    };
    uint8_t data_offset = 0;
    uint8_t subgroup_num_bis;
    uint8_t bis_cnt;
    uint8_t i, j;

    switch (event->type) {
    case BLE_GAP_EVENT_EXT_DISC:
        if ((broadcast_sink_event_handler.filter_policy &
             BLE_BROADCAST_FILT_USE_UUID)) {
            ble_hs_adv_parse(event->ext_disc.data, event->ext_disc.length_data,
                             find_uuid_func, &uuid_find_status);

            if (!uuid_find_status.svc_data) {
                return 0;
            }

            if (uuid_find_status.uuid_to_find.u16.value ==
                BLE_BROADCAST_PUBLIC_BROADCAST_ANNOUNCEMENT_SVC_UUID) {
                ev.announcement_found.desc.pbas_features =
                    uuid_find_status.svc_data[2];
                ev.announcement_found.desc.pbas_metadata_len =
                    uuid_find_status.svc_data[3];
                ev.announcement_found.desc.pbas_metadata =
                    uuid_find_status.svc_data + 4;
            }
        }
        uuid_find_status.uuid_to_find.u16.u.type =
            BLE_HS_ADV_TYPE_SVC_DATA_UUID16;
        uuid_find_status.uuid_to_find.u16.value =
            BLE_BROADCAST_AUDIO_ANNOUNCEMENT_SVC_UUID;
        uuid_find_status.svc_data = NULL;
        uuid_find_status.svc_data_len = 0;

        ble_hs_adv_parse(event->ext_disc.data,
                         event->ext_disc.length_data,
                         find_uuid_func, &uuid_find_status);
        ev.type = BLE_BROADCAST_EVENT_BAA_FOUND;
        ev.announcement_found.desc.broadcast_id = get_le24
            (uuid_find_status.svc_data + 2);

        if ((broadcast_sink_event_handler.filter_policy &
             BLE_BROADCAST_FILT_USE_BROADCAST_ID) &&
            broadcast_sink_event_handler.broadcast_id !=
            ev.announcement_found.desc.broadcast_id) {
            return 0;
        }

        ev.announcement_found.desc.sync_info.addr = event->ext_disc.addr;
        ev.announcement_found.desc.sync_info.adv_sid = event->ext_disc.sid;
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ev.type = BLE_BROADCAST_EVENT_DISC_COMPLETE;
        ev.disc_complete.reason = event->disc_complete.reason;
        break;
    case BLE_GAP_EVENT_PERIODIC_SYNC:
        ev.type = BLE_BROADCAST_EVENT_SYNC_ESTABLISHED;
        ev.sync_established.sync_info.addr = event->periodic_sync.adv_addr;
        ev.sync_established.sync_info.adv_sid = event->periodic_sync.sid;
    case BLE_GAP_EVENT_PERIODIC_SYNC_LOST:
        ev.type = BLE_BROADCAST_EVENT_SYNC_LOST;
        ev.sync_lost.sync_handle = event->periodic_sync_lost.sync_handle;
        ev.sync_lost.reason = event->periodic_sync_lost.reason;
    case BLE_GAP_EVENT_PERIODIC_REPORT:
        uuid_find_status.uuid_to_find.u16.u.type =
            BLE_HS_ADV_TYPE_SVC_DATA_UUID16;
        uuid_find_status.uuid_to_find.u16.value =
            BLE_BROADCAST_AUDIO_ANNOUNCEMENT_SVC_UUID;
        ble_hs_adv_parse(event->periodic_report.data,
                         event->periodic_report.data_length,
                         find_uuid_func, &uuid_find_status);

        ev.type = BLE_BROADCAST_EVENT_BASE_READ;
        data_offset += 2;
        ev.base_read.base.presentation_delay =
            get_le24(uuid_find_status.svc_data + data_offset);
        data_offset += 3;
        ev.base_read.base.num_subgroups = uuid_find_status.svc_data[data_offset];
        data_offset++;
        for (i = 0; i < ev.base_read.base.num_subgroups; i++) {
            subgroup_num_bis = uuid_find_status.svc_data[data_offset];
            data_offset++;
            ev.base_read.base.subgroup[i].num_bis = subgroup_num_bis;
            bis_cnt += subgroup_num_bis;

            if (bis_cnt > MYNEWT_VAL(BLE_MAX_BIS)) {
                return BLE_HS_ENOMEM;
            }

            memcpy(&ev.base_read.base.subgroup[i].codec_id,
                   &uuid_find_status.svc_data[data_offset], 5);
            data_offset += 5;
            ev.base_read.base.subgroup[i].codec_specific_conf_len =
                uuid_find_status.svc_data[data_offset];
            data_offset++;

            if (ev.base_read.base.subgroup[i].codec_specific_conf_len >
                BLE_AUDIO_BROADCAST_CODEC_SPEC_CONF_MAX_SZ) {
                return BLE_HS_EINVAL;
            }

            memcpy(ev.base_read.base.subgroup[i].codec_specific_conf,
                   &uuid_find_status.svc_data[data_offset],
                   ev.base_read.base.subgroup[i].codec_specific_conf_len);
            data_offset +=
                ev.base_read.base.subgroup[i].codec_specific_conf_len;

            ev.base_read.base.subgroup[i].metadata_len =
                uuid_find_status.svc_data[data_offset];
            data_offset++;

            if (ev.base_read.base.subgroup[i].metadata_len >
                MYNEWT_VAL(BLE_AUDIO_BROADCAST_PBA_METADATA_MAX_SZ)) {
                return BLE_HS_EINVAL;
            }

            memcpy(ev.base_read.base.subgroup[i].metadata,
                   &uuid_find_status.svc_data[data_offset],
                   ev.base_read.base.subgroup[i].metadata_len);
            data_offset += ev.base_read.base.subgroup[i].metadata_len;

            for (j = 0; j < subgroup_num_bis; j++) {
                ev.base_read.base.subgroup[i].bis[j].subgroup = i;
                ev.base_read.base.subgroup[i].bis[j].bis_idx =
                    uuid_find_status.svc_data[data_offset];
                data_offset++;
                ev.base_read.base.subgroup[i].bis[j].codec_specific_conf_len =
                    min(uuid_find_status.svc_data[data_offset],
                        BLE_AUDIO_BROADCAST_CODEC_SPEC_CONF_MAX_SZ);
                data_offset++;
                memcpy(&ev.base_read.base.subgroup[i].bis[j].codec_specific_conf,
                       &uuid_find_status.svc_data[data_offset],
                       min(ev.base_read.base.subgroup[i].bis[j].codec_specific_conf_len,
                           BLE_AUDIO_BROADCAST_CODEC_SPEC_CONF_MAX_SZ));
                data_offset += 5;
            }
        }
    default:
        return 0;
    }
    broadcast_sink_event_handler.cb(&ev, broadcast_sink_event_handler.cb_arg);
    return 0;
}

int
ble_audio_broadcast_baa_disc(uint8_t own_addr_type, uint16_t duration,
                             uint8_t filter_policy,
                             const struct ble_gap_ext_disc_params *uncoded_params,
                             const struct ble_gap_ext_disc_params *coded_params,
                             const ble_uuid_t *uuid,
                             uint32_t broadcast_id,
                             ble_audio_broadcast_fn *cb, void *cb_arg)
{
    if (filter_policy == BLE_BROADCAST_FILT_USE_UUID && !uuid) {
        return BLE_HS_EINVAL;
    }

    broadcast_sink_event_handler.cb = cb;
    broadcast_sink_event_handler.cb_arg = cb_arg;
    broadcast_sink_event_handler.filter_policy = filter_policy;

    if (uuid) {
        ble_uuid_copy(&broadcast_sink_event_handler.uuid, uuid);
    }

    broadcast_sink_event_handler.broadcast_id = broadcast_id;

    return ble_gap_ext_disc(own_addr_type, duration, 0, 1,
                            BLE_HCI_CONN_FILT_NO_WL, 0, uncoded_params,
                            coded_params, ble_audio_broadcast_sink_gap_event,
                            NULL);
}

#endif
