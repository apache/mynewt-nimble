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

#include "host/ble_uuid.h"
#include "host/ble_audio_broadcast.h"

#include "os/util.h"

#if MYNEWT_VAL(BLE_ISO_BROADCASTER)
struct ble_audio_broadcast {
    SLIST_ENTRY(ble_audio_broadcast) next;
    uint8_t adv_instance;
    struct ble_audio_base *base;
    struct ble_iso_big_params *big_params;
    ble_audio_broadcast_destroy_fn *destroy_cb;
    void *args;
};

static SLIST_HEAD(, ble_audio_broadcast) ble_audio_broadcasts;
static struct os_mempool ble_audio_broadcast_pool;
static os_membuf_t ble_audio_broadcast_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_MAX_BIG),
                    sizeof(struct ble_audio_broadcast))];

static bool
broadcast_bis_idx_ok(uint8_t bis_idx, struct ble_audio_base *base)
{
    struct ble_audio_big_subgroup *big;
    struct ble_audio_bis *bis;
    uint8_t idx_cnt = 0;

    STAILQ_FOREACH(big, &base->subs, next) {
        STAILQ_FOREACH(bis, &big->bises, next) {
            if (bis_idx == bis->idx) {
                idx_cnt++;
            }
        }
    }

    return idx_cnt == 1;
}

static bool
broadcast_ltv_ok(uint8_t total_len, uint8_t *data)
{
    if (total_len == 0 && data == NULL) {
        return true;
    }
    uint8_t len = data[0];
    uint8_t sum_len = 0;

    while (total_len > sum_len) {
        sum_len += len + 1;
        len = data[sum_len];
    }

    return total_len == sum_len;
}

static struct ble_audio_broadcast *
ble_audio_broadcast_find(uint8_t adv_instance)
{
    struct ble_audio_broadcast *broadcast;

    SLIST_FOREACH(broadcast, &ble_audio_broadcasts, next) {
        if (broadcast->adv_instance == adv_instance) {
            return broadcast;
        }
    }

    return NULL;
}

static struct ble_audio_broadcast *
ble_audio_broadcast_find_last()
{
    struct ble_audio_broadcast *broadcast;

    SLIST_FOREACH(broadcast, &ble_audio_broadcasts, next) {
    }

    return broadcast;
}

int
ble_audio_broadcast_create(const struct ble_broadcast_create_params *params,
                           ble_audio_broadcast_destroy_fn *destroy_cb,
                           void *args,
                           ble_gap_event_fn *gap_cb)
{
    int rc;
    uint8_t offset = 2;
    uint8_t service_data[5] = {0x52, 0x18 };
    uint8_t per_svc_data[MYNEWT_VAL(BLE_EXT_ADV_MAX_SIZE)] = { 0x51, 0x18 };
    struct ble_audio_big_subgroup *subgroup;
    struct ble_audio_bis *bis;
    struct ble_hs_adv_fields adv_fields = {
        .broadcast_name = (uint8_t *) params->name,
        .broadcast_name_len = params->name != NULL ? strlen(params->name) : 0,
        .svc_data_uuid16 = service_data,
        .svc_data_uuid16_len = sizeof(service_data),
    };
    struct ble_hs_adv_fields per_adv_fields = { 0 };
    struct os_mbuf *adv_data;
    ble_uuid16_t audio_announcement_uuid[1];
    uint8_t broadcast_id[3];
    struct ble_audio_broadcast *broadcast;

    if (strlen(params->name) < 4 || strlen(params->name) > 32) {
        return BLE_HS_EINVAL;
    }

    broadcast = ble_audio_broadcast_find(params->adv_instance);
    if (broadcast) {
        return BLE_HS_EALREADY;
    }

    ble_hs_hci_rand(broadcast_id, 3);
    params->base->broadcast_id = get_le24(broadcast_id);

    broadcast = os_memblock_get(&ble_audio_broadcast_pool);

    broadcast->adv_instance = params->adv_instance;
    broadcast->base = params->base;
    broadcast->big_params = params->big_params;
    broadcast->destroy_cb = destroy_cb;
    broadcast->args = args;

    if (SLIST_EMPTY(&ble_audio_broadcasts)) {
        SLIST_INSERT_HEAD(&ble_audio_broadcasts, broadcast, next);
    } else {
        SLIST_INSERT_AFTER(ble_audio_broadcast_find_last(), broadcast, next);
    }

    if (params->base->num_subgroups == 0) {
        /**
         * BAP specification 3.7.2.2 Basic Audio Announcements:
         * Rule 1: There shall be at least one subgroup.
         */
        BLE_HS_LOG_ERROR("No subgroups in BASE!\n");
        return BLE_HS_EINVAL;
    }

    put_le24(&service_data[2], broadcast->base->broadcast_id);
    put_le24(&per_svc_data[offset], params->base->presentation_delay);
    offset += 3;
    per_svc_data[offset] = params->base->num_subgroups;
    offset++;

    STAILQ_FOREACH(subgroup, &params->base->subs, next) {
        if (subgroup->bis_cnt == 0) {
            /**
             * BAP specification 3.7.2.2 Basic Audio Announcements:
             * Rule 2: There shall be at least one BIS per subgroup.
             */
            BLE_HS_LOG_ERROR("No BIS in BIG!\n");
            return BLE_HS_EINVAL;
        }

        if (!broadcast_ltv_ok(subgroup->metadata_len, subgroup->metadata)) {
            BLE_HS_LOG_ERROR("Invalid Metadata!\n");
            return BLE_HS_EINVAL;
        }

        if (!broadcast_ltv_ok(subgroup->codec_spec_config_len, subgroup->codec_spec_config)) {
            BLE_HS_LOG_ERROR("Invalid Codec Configuration in subgroup!\n");
            return BLE_HS_EINVAL;
        }

        per_svc_data[offset] = subgroup->bis_cnt;
        offset++;
        memcpy(&per_svc_data[offset], &subgroup->codec_id, 5);
        offset += 5;
        per_svc_data[offset] = subgroup->codec_spec_config_len;
        offset++;
        memcpy(&per_svc_data[offset], &subgroup->codec_spec_config,
               subgroup->codec_spec_config_len);
        offset += subgroup->codec_spec_config_len;
        per_svc_data[offset] = subgroup->metadata_len;
        offset++;
        memcpy(&per_svc_data[offset], &subgroup->metadata,
               subgroup->metadata_len);
        offset += subgroup->metadata_len;
        STAILQ_FOREACH(bis, &subgroup->bises, next) {
            if (!broadcast_bis_idx_ok(bis->idx, params->base)) {
                /**
                 * BAP specification 3.7.2.2 Basic Audio Announcements:
                 * Rule 3: Every BIS in the BIG, denoted by its BIS_index
                 * value, shall only be present in one subgroup.
                 */
                BLE_HS_LOG_ERROR("Duplicated BIS index!\n");
                return BLE_HS_EINVAL;
            }

            if (!broadcast_ltv_ok(bis->codec_spec_config_len,
                                  bis->codec_spec_config)) {
                BLE_HS_LOG_ERROR("Invalid Codec Configuration in BIS!\n");
                return BLE_HS_EINVAL;
            }
            per_svc_data[offset] = bis->idx;
            offset++;
            per_svc_data[offset] = bis->codec_spec_config_len;
            offset++;
            memcpy(&per_svc_data[offset], bis->codec_spec_config,
                   bis->codec_spec_config_len);
            offset += bis->codec_spec_config_len;
        }
    }

    audio_announcement_uuid[0] = (ble_uuid16_t) BLE_UUID16_INIT(BLE_BROADCAST_AUDIO_ANNOUNCEMENT_SVC_UUID);
    per_adv_fields.uuids16 = audio_announcement_uuid;
    per_adv_fields.num_uuids16 = 1;
    per_adv_fields.uuids16_is_complete = 1;
    per_adv_fields.svc_data_uuid16 = per_svc_data;
    per_adv_fields.svc_data_uuid16_len = offset;

    rc = ble_gap_ext_adv_configure(params->adv_instance,
                                   params->extended_params, 0,
                                   gap_cb, NULL);
    if (rc) {
        BLE_HS_LOG_ERROR("Could not configure the broadcast (rc=%d)\n", rc);
        return 0;
    }

    adv_data = os_msys_get_pkthdr(BLE_HCI_MAX_EXT_ADV_DATA_LEN, 0);
    if (!adv_data) {
        BLE_HS_LOG_ERROR("No memory\n");
        return BLE_HS_ENOMEM;
    }

    /* Set ext advertising data */
    rc = ble_hs_adv_set_fields_mbuf(&adv_fields, adv_data);
    if (rc) {
        BLE_HS_LOG_ERROR("Failed to set extended advertising fields"
                         "(rc=%d)\n", rc);
        return rc;
    }

    os_mbuf_append(adv_data, params->svc_data, params->svc_data_len);

    rc = ble_gap_ext_adv_set_data(params->adv_instance, adv_data);
    if (rc) {
        BLE_HS_LOG_ERROR("Failed to set extended advertising data"
                         "(rc=%d)\n", rc);
        return rc;
    }

    /* Clear buffer */
    adv_data = os_msys_get_pkthdr(BLE_HCI_MAX_EXT_ADV_DATA_LEN, 0);
    if (!adv_data) {
        BLE_HS_LOG_ERROR("No memory\n");
        return BLE_HS_ENOMEM;
    }

    rc = ble_gap_periodic_adv_configure(params->adv_instance, params->periodic_params);
    if (rc) {
        BLE_HS_LOG_ERROR("failed to configure periodic advertising"
                         "(rc=%d)\n", rc);
        return rc;
    }

    rc = ble_hs_adv_set_fields_mbuf(&per_adv_fields, adv_data);
    if (rc) {
        BLE_HS_LOG_ERROR("Failed to set periodic advertising fields"
                         "(rc=%d)\n", rc);
        return rc;
    }

    rc = ble_gap_periodic_adv_set_data(params->adv_instance, adv_data, NULL);
    if (rc) {
        BLE_HS_LOG_ERROR("Failed to set periodic advertising data"
                         "(rc=%d)\n", rc);
    }

    return rc;
}

int
ble_audio_broadcast_start(uint8_t adv_instance,
                          ble_iso_event_fn *cb, void *cb_arg)
{
    struct ble_audio_big_subgroup *subgroup;
    struct ble_iso_create_big_params create_big_params = { 0 };
    struct ble_audio_broadcast *broadcast;
    int rc;

    broadcast = ble_audio_broadcast_find(adv_instance);
    if (!broadcast) {
        return BLE_HS_ENOENT;
    }

    STAILQ_FOREACH(subgroup, &broadcast->base->subs, next) {
        create_big_params.bis_cnt += subgroup->bis_cnt;
    }

    create_big_params.adv_handle = broadcast->adv_instance;
    create_big_params.cb = cb;
    create_big_params.cb_arg = cb_arg;

    rc = ble_gap_ext_adv_start(broadcast->adv_instance, 0, 0);
    if (rc) {
        BLE_HS_LOG_ERROR("Failed to start extended advertising (rc=%d)\n", rc);
        return rc;
    }

    rc = ble_gap_periodic_adv_start(broadcast->adv_instance, NULL);
    if (rc) {
        BLE_HS_LOG_ERROR("Failed to start periodic advertising (rc=%d)\n", rc);
        return rc;
    }

    rc = ble_iso_create_big(&create_big_params, broadcast->big_params);
    if (rc) {
        BLE_HS_LOG_ERROR("Failed to create BIG (rc=%d)\n", rc);
        return rc;
    }

    return 0;
}

int
ble_audio_broadcast_stop(uint8_t adv_instance)
{
    int rc;

    rc = ble_gap_ext_adv_stop(adv_instance);
    if (rc) {
        BLE_HS_LOG_ERROR("Failed to stop extended advertising (rc=%d)\n", rc);
        return rc;
    }

    rc = ble_gap_periodic_adv_stop(adv_instance);
    if (rc) {
        BLE_HS_LOG_ERROR("Failed to stop periodic advertising (rc=%d)\n", rc);
        return rc;
    }

    return 0;
}

int
ble_audio_broadcast_destroy(uint8_t adv_instance)
{
    struct ble_audio_broadcast *broadcast;
    int rc;

    broadcast = ble_audio_broadcast_find(adv_instance);
    if (!broadcast) {
        return BLE_HS_ENOENT;
    }

    rc = ble_gap_ext_adv_remove(adv_instance);
    if (rc) {
        BLE_HS_LOG_ERROR("Failed to remove extended advertising\n");
        return rc;
    }

    rc = ble_iso_terminate_big(adv_instance);
    if (rc) {
        BLE_HS_LOG_ERROR("Failed to terminate BIG\n");
        return rc;
    }

    os_memblock_put(&ble_audio_broadcast_pool, broadcast);
    SLIST_REMOVE(&ble_audio_broadcasts, broadcast, ble_audio_broadcast, next);

    return 0;
}

int
ble_audio_broadcast_update(const struct ble_broadcast_update_params *params)
{
    uint8_t service_data[5] = {0x52, 0x18 };
    struct ble_hs_adv_fields adv_fields = {
        .broadcast_name = (uint8_t *) params->name,
        .broadcast_name_len = params->name != NULL ? strlen(params->name) : 0,
        .svc_data_uuid16 = service_data,
        .svc_data_uuid16_len = sizeof(service_data)
    };
    struct os_mbuf *adv_data;
    int rc;

    if (strlen(params->name) < 4 || strlen(params->name) > 32) {
        return BLE_HS_EINVAL;
    }

    memcpy(&service_data[2], &params->broadcast_id, 3);

    adv_data = os_msys_get_pkthdr(BLE_HCI_MAX_EXT_ADV_DATA_LEN, 0);

    os_mbuf_append(adv_data, params->svc_data, params->svc_data_len);

    /* Set ext advertising data */
    rc = ble_hs_adv_set_fields_mbuf(&adv_fields, adv_data);
    if (rc) {
        BLE_HS_LOG_ERROR("Failed to set extended advertising fields\n");
        return rc;
    }

    rc = ble_gap_ext_adv_set_data(params->adv_instance, adv_data);
    if (rc) {
        BLE_HS_LOG_ERROR("Failed to set extended advertising data\n");
        return rc;
    }

    return 0;
}

int
ble_audio_broadcast_build_sub(struct ble_audio_base *base,
                              struct ble_audio_big_subgroup *sub,
                              const struct ble_broadcast_subgroup_params
                              *params)
{
    sub->codec_id = *params->codec_id;
    memcpy(sub->codec_spec_config, params->codec_spec_config,
           params->codec_spec_config_len);
    sub->codec_spec_config_len = params->codec_spec_config_len;
    memcpy(sub->metadata, params->metadata,
           params->metadata_len);
    sub->metadata_len = params->metadata_len;

    if (STAILQ_EMPTY(&base->subs)) {
        STAILQ_INSERT_HEAD(&base->subs, sub, next);
    } else {
        STAILQ_INSERT_TAIL(&base->subs, sub, next);
    }

    base->num_subgroups++;
    return 0;
}

int
ble_audio_broadcast_build_bis(struct ble_audio_big_subgroup *sub,
                              struct ble_audio_bis *bis,
                              const struct ble_broadcast_bis_params
                              *params)
{
    bis->idx = params->idx;
    memcpy(bis->codec_spec_config, params->codec_spec_config,
           params->codec_spec_config_len);
    bis->codec_spec_config_len = params->codec_spec_config_len;

    if (STAILQ_EMPTY(&sub->bises)) {
        STAILQ_INSERT_HEAD(&sub->bises, bis, next);
    } else {
        STAILQ_INSERT_TAIL(&sub->bises, bis, next);
    }

    sub->bis_cnt++;
    return 0;
}


int
ble_audio_broadcast_init(void)
{
    int rc;

    SLIST_INIT(&ble_audio_broadcasts);

    rc = os_mempool_init(&ble_audio_broadcast_pool,
                         MYNEWT_VAL(BLE_MAX_BIG),
                         sizeof(struct ble_audio_broadcast),
                         ble_audio_broadcast_mem, "ble_audio_broadcast_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    return 0;
}
#endif
