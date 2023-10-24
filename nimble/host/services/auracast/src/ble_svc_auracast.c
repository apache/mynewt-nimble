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

#include "syscfg/syscfg.h"

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_audio_broadcast.h"
#include "services/auracast/ble_svc_auracast.h"

int
ble_svc_auracast_create(const struct ble_svc_auracast_create_params *params,
                        uint8_t *auracast_instance,
                        ble_audio_broadcast_destroy_fn *destroy_cb,
                        void *args, ble_gap_event_fn *gap_cb)
{
    struct ble_broadcast_create_params create_params;
    struct ble_gap_periodic_adv_params periodic_params = { 0 };
    struct ble_gap_ext_adv_params extended_params = {
        .scannable = 0,
        .connectable = 0,
        .primary_phy = BLE_HCI_LE_PHY_1M,
    };
    uint8_t adv_instance;
    uint8_t auracast_svc_data[251] = { 0 };
    uint8_t data_offset = 1;

    uint8_t features = 0;

    features |= params->big_params->encryption;

    if ((params->frame_duration == 10000) &&
        (((params->sampling_frequency == 16000) &&
          (params->bitrate == 32000)) ||
         ((params->sampling_frequency == 24000) &&
          (params->bitrate == 48000)))) {
        features |= 0x02;
    }

    if (params->sampling_frequency == 48000) {
        features |= 0x04;
    }


    auracast_svc_data[data_offset] = BLE_HS_ADV_TYPE_SVC_DATA_UUID16;
    data_offset++;
    put_le16(&auracast_svc_data[data_offset], 0x1856);
    data_offset += 2;
    auracast_svc_data[data_offset] = features;
    data_offset++;
    /** Metadata */
    if (params->program_info != NULL) {
        auracast_svc_data[data_offset] = strlen(params->program_info) + 2;
        data_offset++;
        auracast_svc_data[data_offset] = strlen(params->program_info) + 1;
        data_offset++;
        auracast_svc_data[data_offset] = 3;
        data_offset++;
        memcpy(&auracast_svc_data[data_offset], params->program_info,
               strlen(params->program_info));
        data_offset += strlen(params->program_info);
    }

    auracast_svc_data[0] = data_offset - 1;

    adv_instance = ble_gap_adv_get_free_instance();
    if (adv_instance < 0) {
        return BLE_HS_ENOENT;
    }

    if (params->secondary_phy == BLE_HCI_LE_PHY_2M &&
        !MYNEWT_VAL(BLE_PHY_2M)) {
        return BLE_HS_EINVAL;
    }

    extended_params.own_addr_type = params->own_addr_type;
    extended_params.sid = params->sid;
    extended_params.secondary_phy = params->secondary_phy;

    create_params.base = params->base;
    create_params.extended_params = &extended_params;
    create_params.periodic_params = &periodic_params;
    create_params.name = params->name;
    create_params.adv_instance = adv_instance;
    create_params.big_params = params->big_params;
    create_params.svc_data = auracast_svc_data;
    create_params.svc_data_len = data_offset;

    *auracast_instance = adv_instance;

    return ble_audio_broadcast_create(&create_params,
                                      destroy_cb, args, gap_cb);
}

int
ble_svc_auracast_terminate(uint8_t auracast_instance)
{
    return ble_audio_broadcast_destroy(auracast_instance);
}

int
ble_svc_auracast_start(uint8_t auracast_instance, ble_iso_event_fn *cb, void *cb_arg)
{

    return ble_audio_broadcast_start(auracast_instance, cb, cb_arg);
}

int
ble_svc_auracast_stop(uint8_t auracast_instance)
{
    return ble_audio_broadcast_stop(auracast_instance);
}
