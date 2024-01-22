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

#if MYNEWT_VAL(BLE_AUDIO_MAX_CODEC_RECORDS)
#include "os/os.h"
#include "audio/ble_audio.h"
#include "audio/ble_audio_codec.h"
#include "ble_audio_priv.h"
#include "host/ble_hs.h"
#include "sysinit/sysinit.h"

static STAILQ_HEAD(, ble_audio_codec_record) ble_audio_codec_records;
static struct os_mempool ble_audio_codec_pool;

static os_membuf_t ble_audio_svc_pacs_pac_elem_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_AUDIO_MAX_CODEC_RECORDS),
                    sizeof (struct ble_audio_codec_record))
];

int
ble_audio_codec_register(const struct ble_audio_codec_register_params *params,
                         struct ble_audio_codec_record *out_record)
{
    struct ble_audio_event codec_event = {
        .type = BLE_AUDIO_EVENT_CODEC_REGISTERED
    };

    struct ble_audio_codec_record *record =
        os_memblock_get(&ble_audio_codec_pool);
    if (!record) {
        return BLE_HS_ENOMEM;
    }

    record->codec_id = params->codec_id;
    record->codec_spec_caps_len = params->codec_spec_caps_len;
    record->codec_spec_caps = params->codec_spec_caps;
    record->metadata_len = params->metadata_len;
    record->metadata = params->metadata;
    record->direction = params->direction;

    if (STAILQ_EMPTY(&ble_audio_codec_records)) {
        STAILQ_INSERT_HEAD(&ble_audio_codec_records, record, next);
    } else {
        STAILQ_INSERT_TAIL(&ble_audio_codec_records, record, next);
    }

    out_record = record;

    codec_event.codec_registered.record = record;
    (void)ble_audio_event_listener_call(&codec_event);

    return 0;
}

int
ble_audio_codec_unregister(struct ble_audio_codec_record *codec_record)
{
    struct ble_audio_event codec_event = {
        .type = BLE_AUDIO_EVENT_CODEC_UNREGISTERED
    };

    STAILQ_REMOVE(&ble_audio_codec_records, codec_record,
                  ble_audio_codec_record, next);

    codec_event.codec_unregistered.record = codec_record;
    (void)ble_audio_event_listener_call(&codec_event);

    return 0;
}

int
ble_audio_codec_foreach(uint8_t flags, ble_audio_codec_foreach_fn *cb, void *arg)
{
    struct ble_audio_codec_record *record;
    int rc;

    STAILQ_FOREACH(record, &ble_audio_codec_records, next) {
        if (record->direction & flags) {
            rc = cb(record, arg);
            if (rc != 0) {
                return rc;
            }
        }
    }
    return 0;
}

struct ble_audio_codec_record *
ble_audio_codec_find(struct ble_audio_codec_id codec_id, uint8_t flag)
{
    struct ble_audio_codec_record *record;

    STAILQ_FOREACH(record, &ble_audio_codec_records, next) {
        if (!memcmp(&record->codec_id, &codec_id,
                    sizeof(struct ble_audio_codec_id)) &&
            (flag ? (record->direction & flag) : 1)) {
            return record;
        }
    }

    return NULL;
}

int
ble_audio_codec_init(void)
{
    int rc;

    STAILQ_INIT(&ble_audio_codec_records);

    rc = os_mempool_init(&ble_audio_codec_pool,
                         MYNEWT_VAL(BLE_AUDIO_MAX_CODEC_RECORDS),
                         sizeof(struct ble_audio_codec_record),
                         ble_audio_svc_pacs_pac_elem_mem,
                         "ble_audio_codec_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    return 0;
}
#endif
