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

#include <string.h>
#include <stddef.h>

#include "host/ble_hs.h"
#include "audio/ble_audio.h"

#include "ble_audio_priv.h"

static struct ble_gap_event_listener ble_audio_gap_event_listener;
static SLIST_HEAD(, ble_audio_event_listener) ble_audio_event_listener_list =
    SLIST_HEAD_INITIALIZER(ble_audio_event_listener_list);

struct ble_audio_adv_parse_broadcast_announcement_data {
    struct ble_audio_event event;
    struct ble_audio_pub_broadcast_announcement pub;
    struct ble_audio_broadcast_name name;
    bool success;
};

static int
ble_audio_adv_parse_broadcast_announcement(const struct ble_hs_adv_field *field, void *user_data)
{
    struct ble_audio_adv_parse_broadcast_announcement_data *data = user_data;
    struct ble_audio_event_broadcast_announcement *event;
    const uint8_t value_len = field->length - sizeof(field->length);
    ble_uuid16_t uuid16 = BLE_UUID16_INIT(0);
    uint8_t offset = 0;

    event = &data->event.broadcast_announcement;

    switch (field->type) {
    case BLE_HS_ADV_TYPE_SVC_DATA_UUID16:
        if (value_len < 2) {
            break;
        }

        uuid16.value = get_le16(&field->value[offset]);
        offset += 2;

        switch (uuid16.value) {
        case BLE_BROADCAST_AUDIO_ANNOUNCEMENT_SVC_UUID:
            if ((value_len - offset) < 3) {
                /* Stop parsing */
                data->success = false;
                return 0;
            }

            event->broadcast_id = get_le24(&field->value[offset]);
            offset += 3;

            if (value_len > offset) {
                event->svc_data = &field->value[offset];
                event->svc_data_len = value_len - offset;
            }

            data->success = true;
            break;

        case BLE_BROADCAST_PUB_ANNOUNCEMENT_SVC_UUID:
            if (event->pub_announcement_data != NULL) {
                /* Ignore */
                break;
            }

            if ((value_len - offset) < 2) {
                /* Stop parsing */
                data->success = false;
                return 0;
            }

            data->pub.features = field->value[offset++];
            data->pub.metadata_len = field->value[offset++];

            if ((value_len - offset) < data->pub.metadata_len) {
                break;
            }

            data->pub.metadata = &field->value[offset];

            event->pub_announcement_data = &data->pub;
            break;

        default:
            break;
        }

        break;

    case BLE_HS_ADV_TYPE_BROADCAST_NAME:
        if (event->name != NULL) {
            /* Ignore */
            break;
        }

        if (value_len < 4 || value_len > 32) {
            /* Stop parsing */
            data->success = false;
            return 0;
        }

        data->name.name = (char *)field->value;
        data->name.name_len = value_len;

        event->name = &data->name;
        break;

    default:
        break;
    }

    /* Continue parsing */
    return BLE_HS_ENOENT;
}

static int
ble_audio_gap_event(struct ble_gap_event *gap_event, void *arg)
{
    switch (gap_event->type) {
    case BLE_GAP_EVENT_EXT_DISC: {
        struct ble_audio_adv_parse_broadcast_announcement_data data = {
            .success = false,
        };
        int rc;

        rc = ble_hs_adv_parse(gap_event->ext_disc.data,
                              gap_event->ext_disc.length_data,
                              ble_audio_adv_parse_broadcast_announcement, &data);
        if (rc == 0 && data.success) {
            data.event.type = BLE_AUDIO_EVENT_BROADCAST_ANNOUNCEMENT;
            data.event.broadcast_announcement.ext_disc = &gap_event->ext_disc;

            (void)ble_audio_event_listener_call(&data.event);
        }
        break;
    }

    default:
        break;
    }

    return 0;
}

int
ble_audio_event_listener_register(struct ble_audio_event_listener *listener,
                                  ble_audio_event_fn *fn, void *arg)
{
    struct ble_audio_event_listener *evl = NULL;
    int rc;

    if (listener == NULL) {
        BLE_HS_LOG_ERROR("NULL listener!\n");
        return BLE_HS_EINVAL;
    }

    if (fn == NULL) {
        BLE_HS_LOG_ERROR("NULL fn!\n");
        return BLE_HS_EINVAL;
    }

    SLIST_FOREACH(evl, &ble_audio_event_listener_list, next) {
        if (evl == listener) {
            break;
        }
    }

    if (evl) {
        return BLE_HS_EALREADY;
    }

    if (SLIST_EMPTY(&ble_audio_event_listener_list)) {
        rc = ble_gap_event_listener_register(
                &ble_audio_gap_event_listener,
                ble_audio_gap_event, NULL);
        if (rc != 0) {
            return rc;
        }
    }

    memset(listener, 0, sizeof(*listener));
    listener->fn = fn;
    listener->arg = arg;
    SLIST_INSERT_HEAD(&ble_audio_event_listener_list, listener, next);

    return 0;
}

int
ble_audio_event_listener_unregister(struct ble_audio_event_listener *listener)
{
    struct ble_audio_event_listener *evl = NULL;
    int rc;

    if (listener == NULL) {
        BLE_HS_LOG_ERROR("NULL listener!\n");
        return BLE_HS_EINVAL;
    }

    /* We check if element exists on the list only for sanity to let caller
     * know whether it registered its listener before.
     */
    SLIST_FOREACH(evl, &ble_audio_event_listener_list, next) {
        if (evl == listener) {
            break;
        }
    }

    if (!evl) {
        return BLE_HS_ENOENT;
    }

    SLIST_REMOVE(&ble_audio_event_listener_list, listener,
                 ble_audio_event_listener, next);

    if (SLIST_EMPTY(&ble_audio_event_listener_list)) {
        rc = ble_gap_event_listener_unregister(
                &ble_audio_gap_event_listener);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

int
ble_audio_event_listener_call(struct ble_audio_event *event)
{
    struct ble_audio_event_listener *evl = NULL;

    SLIST_FOREACH(evl, &ble_audio_event_listener_list, next) {
        evl->fn(event, evl->arg);
    }

    return 0;
}

/* Get the next subgroup data pointer */
static const uint8_t *
ble_audio_base_subgroup_next(uint8_t num_bis, const uint8_t *data,
                             uint8_t data_len)
{
    uint8_t offset = 0;

    for (uint8_t i = 0; i < num_bis; i++) {
        uint8_t codec_specific_config_len;

        /* BIS_index[i[k]] + Codec_Specific_Configuration_Length[i[k]] */
        if ((data_len - offset) < 2) {
            return NULL;
        }

        /* Skip BIS_index[i[k]] */
        offset++;

        codec_specific_config_len = data[offset];
        offset++;

        if ((data_len - offset) < codec_specific_config_len) {
            return NULL;
        }

        offset += codec_specific_config_len;
    }

    return &data[offset];
}

int
ble_audio_base_parse(const uint8_t *data, uint8_t data_len,
                     struct ble_audio_base_group *group,
                     struct ble_audio_base_iter *subgroup_iter)
{
    uint8_t offset = 0;

    if (data == NULL) {
        BLE_HS_LOG_ERROR("NULL data!\n");
        return BLE_HS_EINVAL;
    }

    if (group == NULL) {
        BLE_HS_LOG_ERROR("NULL group!\n");
        return BLE_HS_EINVAL;
    }

    /* Presentation_Delay + Num_Subgroups */
    if (data_len < 4) {
        return BLE_HS_EMSGSIZE;
    }

    group->presentation_delay = get_le24(data);
    offset += 3;

    group->num_subgroups = data[offset];
    offset++;

    if (group->num_subgroups < 1) {
        BLE_HS_LOG_ERROR("Invalid BASE: no subgroups!\n");
        return BLE_HS_EINVAL;
    }

    if (subgroup_iter != NULL) {
        subgroup_iter->data = &data[offset];
        subgroup_iter->buf_len = data_len;
        subgroup_iter->buf = data;
        subgroup_iter->num_elements = group->num_subgroups;
    }

    return 0;
}

int
ble_audio_base_subgroup_iter(struct ble_audio_base_iter *subgroup_iter,
                             struct ble_audio_base_subgroup *subgroup,
                             struct ble_audio_base_iter *bis_iter)
{
    const uint8_t *data;
    uint8_t data_len;
    ptrdiff_t offset;
    uint8_t num_subgroups;

    if (subgroup_iter == NULL) {
        BLE_HS_LOG_ERROR("NULL subgroup_iter!\n");
        return BLE_HS_EINVAL;
    }

    if (subgroup == NULL) {
        BLE_HS_LOG_ERROR("NULL subgroup!\n");
        return BLE_HS_EINVAL;
    }

    data = subgroup_iter->data;
    if (data == NULL) {
        return BLE_HS_ENOENT;
    }

    offset = data - subgroup_iter->buf;
    if (offset < 0 || offset > subgroup_iter->buf_len) {
        return BLE_HS_EINVAL;
    }

    num_subgroups = subgroup_iter->num_elements;
    if (num_subgroups == 0) {
        /* All subgroups have been parsed */
        return BLE_HS_ENOENT;
    }

    data_len = subgroup_iter->buf_len - offset;

    /* Reset the offset */
    offset = 0;

    memset(subgroup, 0, sizeof(*subgroup));

    /* Num_BIS + Codec_ID + Codec_Specific_Configuration_Length[i] */
    if (data_len < 7) {
        return BLE_HS_EMSGSIZE;
    }

    subgroup->num_bis = data[offset];
    offset++;

    if (subgroup->num_bis < 1) {
        BLE_HS_LOG_ERROR("Invalid BASE: no BISes!\n");
        return BLE_HS_EINVAL;
    }

    subgroup->codec_id.format = data[offset];
    offset++;

    subgroup->codec_id.company_id = get_le16(&data[offset]);
    offset += 2;

    subgroup->codec_id.vendor_specific = get_le16(&data[offset]);
    offset += 2;

    subgroup->codec_spec_config_len = data[offset];
    offset++;

    if (subgroup->codec_spec_config_len < 1) {
        BLE_HS_LOG_DEBUG("Rule 4: Codec_Specific_Configuration parameters shall"
                         "be present at Level 2\n");
    }

    if ((data_len - offset) < subgroup->codec_spec_config_len) {
        return BLE_HS_EMSGSIZE;
    }

    subgroup->codec_spec_config = &data[offset];
    offset += subgroup->codec_spec_config_len;

    /* Metadata_Length[i] */
    if ((data_len - offset) < 1) {
        return BLE_HS_EMSGSIZE;
    }

    subgroup->metadata_len = data[offset];
    offset++;

    if (subgroup->metadata_len > 0) {
        if ((data_len - offset) < subgroup->metadata_len) {
            return BLE_HS_EMSGSIZE;
        }

        subgroup->metadata = &data[offset];
        offset += subgroup->metadata_len;
    } else {
        subgroup->metadata = NULL;
    }

    if (bis_iter != 0) {
        bis_iter->data = &data[offset];
        bis_iter->buf_len = subgroup_iter->buf_len;
        bis_iter->buf = subgroup_iter->buf;
        bis_iter->num_elements = subgroup->num_bis;
    }

    num_subgroups--;

    /* Update iterator */
    subgroup_iter->num_elements = num_subgroups;

    if (num_subgroups > 0) {
        subgroup_iter->data = ble_audio_base_subgroup_next(subgroup->num_bis,
                                                           &data[offset],
                                                           data_len - offset);
    } else {
        subgroup_iter->data = NULL;
    }

    return 0;
}

int
ble_audio_base_bis_iter(struct ble_audio_base_iter *bis_iter,
                        struct ble_audio_base_bis *bis)
{
    const uint8_t *data;
    uint8_t data_len;
    ptrdiff_t offset;
    uint8_t num_bis;

    if (bis_iter == NULL) {
        BLE_HS_LOG_ERROR("NULL bis_iter!\n");
        return BLE_HS_EINVAL;
    }

    if (bis == NULL) {
        BLE_HS_LOG_ERROR("NULL bis!\n");
        return BLE_HS_EINVAL;
    }

    data = bis_iter->data;
    if (data == NULL) {
        return BLE_HS_ENOENT;
    }

    offset = data - bis_iter->buf;
    if (offset < 0 || offset > bis_iter->buf_len) {
        return BLE_HS_EINVAL;
    }

    num_bis = bis_iter->num_elements;
    if (num_bis == 0) {
        /* All BISes have been parsed */
        return BLE_HS_ENOENT;
    }

    data_len = bis_iter->buf_len - offset;

    /* Reset the offset */
    offset = 0;

    memset(bis, 0, sizeof(*bis));

    /* BIS_index[i[k]] + Codec_Specific_Configuration_Length[i[k]] */
    if (data_len < 2) {
        return BLE_HS_EMSGSIZE;
    }

    bis->index = data[0];
    offset++;

    bis->codec_spec_config_len = data[offset];
    offset++;

    if (bis->codec_spec_config_len > 0) {
        if ((data_len - offset) < bis->codec_spec_config_len) {
            return BLE_HS_EMSGSIZE;
        }

        bis->codec_spec_config = &data[offset];
        offset += bis->codec_spec_config_len;
    } else {
        bis->codec_spec_config = NULL;
    }

    num_bis--;

    /* Update iterator */
    bis_iter->num_elements = num_bis;

    if (num_bis > 0) {
        bis_iter->data = &data[offset];
    } else {
        bis_iter->data = NULL;
    }

    return 0;
}
