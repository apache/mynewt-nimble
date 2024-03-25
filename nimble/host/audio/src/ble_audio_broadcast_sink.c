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

#include "os/util.h"
#include "sysinit/sysinit.h"

#include "audio/ble_audio.h"
#include "audio/ble_audio_broadcast_sink.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_iso.h"
#include "host/ble_uuid.h"
#include "services/bass/ble_audio_svc_bass.h"

#include "ble_audio_priv.h"

#if MYNEWT_VAL(BLE_ISO_BROADCAST_SINK)
enum pa_sync_state_internal {
    PA_SYNC_STATE_IDLE,
    PA_SYNC_STATE_PENDING_SCAN,
    PA_SYNC_STATE_PENDING_PAST,
    PA_SYNC_STATE_ACTIVE,
    PA_SYNC_STATE_ERROR,
    PA_SYNC_STATE_TIMEOUT,
};

enum big_sync_state_internal {
    BIG_SYNC_STATE_IDLE,
    BIG_SYNC_STATE_PENDING_BIG_INFO,
    BIG_SYNC_STATE_PENDING_CODE,
    BIG_SYNC_STATE_PENDING_SYNC,
    BIG_SYNC_STATE_ACTIVE,
    BIG_SYNC_STATE_ERROR,
    BIG_SYNC_STATE_TIMEOUT,
};

struct ble_audio_broadcast_sink {
    /** Instance ID, same as BASS Source ID */
    uint8_t instance_id;

    /** Internal PA sync state */
    enum pa_sync_state_internal pa_sync_state;

    /** Internal BIG sync state */
    enum big_sync_state_internal big_sync_state;

    /** Periodic sync handle */
    uint16_t pa_sync_handle;

    /** Connection handle or @ref BLE_HS_CONN_HANDLE_NONE */
    uint16_t past_conn_handle;

    /** ISO Interval */
    uint16_t iso_interval;

    /** BIG Handle */
    uint8_t big_handle;

    /** Broadcast is encrypted */
    uint8_t encrypted : 1;

    /** Callback function */
    ble_audio_event_fn *cb;

    /** The optional argument to pass to the callback function. */
    void *cb_arg;

    /** BIG Sync Parameters */
    struct ble_audio_broadcast_sink_big_sync_params *big_sync_params;

    /** Singly-linked list entry. */
    SLIST_ENTRY(ble_audio_broadcast_sink) next;
};

static SLIST_HEAD(, ble_audio_broadcast_sink) ble_audio_broadcast_sink_list;
static struct os_mempool ble_audio_broadcast_sink_pool;
static os_membuf_t ble_audio_broadcast_sink_mem[
        OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_AUDIO_BROADCAST_SINK_MAX),
                        sizeof(struct ble_audio_broadcast_sink))];

static struct ble_audio_broadcast_sink *
broadcast_sink_lookup_pa_sync_handle(uint16_t sync_handle)
{
    struct ble_audio_broadcast_sink *sink;

    SLIST_FOREACH(sink, &ble_audio_broadcast_sink_list, next) {
        if (sync_handle == sink->pa_sync_handle) {
            return sink;
        }
    }

    return NULL;
}

static struct ble_audio_broadcast_sink *
broadcast_sink_lookup_instance_id(uint8_t instance_id)
{
    struct ble_audio_broadcast_sink *sink;

    SLIST_FOREACH(sink, &ble_audio_broadcast_sink_list, next) {
        if (instance_id == sink->instance_id) {
            return sink;
        }
    }

    return NULL;
}

struct broadcast_sink_base_parse_arg {
    /** BASE length */
    uint8_t *base_length;

    /** BASE */
    const uint8_t **base;
};

static int
broadcast_sink_base_parse(
        const struct ble_hs_adv_field *field, void *user_data)
{
    struct broadcast_sink_base_parse_arg *data = user_data;
    const uint8_t value_len = field->length - sizeof(field->length);
    ble_uuid16_t uuid16 = BLE_UUID16_INIT(0);
    uint8_t offset = 0;

    switch (field->type) {
    case BLE_HS_ADV_TYPE_SVC_DATA_UUID16:
        if (value_len < 2) {
            break;
        }

        uuid16.value = get_le16(&field->value[offset]);
        offset += 2;

        switch (uuid16.value) {
        case BLE_BASIC_AUDIO_ANNOUNCEMENT_SVC_UUID: {
            *data->base = &field->value[offset];
            *data->base_length = value_len - offset;
            break;
        }

        default:
            break;
        }

        break;

    default:
        break;
    }

    return 0;
}

static void
broadcast_sink_pa_sync_state_update(
        struct ble_audio_broadcast_sink *sink,
        enum pa_sync_state_internal state)
{
    enum pa_sync_state_internal state_old = sink->pa_sync_state;
    struct ble_svc_audio_bass_sync_params bass;

    sink->pa_sync_state = state;

    switch (state) {
    case PA_SYNC_STATE_IDLE:
    case PA_SYNC_STATE_PENDING_SCAN:
        bass.pa_sync_state = BLE_SVC_AUDIO_BASS_PA_SYNC_STATE_NOT_SYNCED;
        break;

    case PA_SYNC_STATE_PENDING_PAST:
        bass.pa_sync_state = BLE_SVC_AUDIO_BASS_PA_SYNC_STATE_SYNC_INFO_REQ;
        break;

    case PA_SYNC_STATE_ACTIVE:
        bass.pa_sync_state = BLE_SVC_AUDIO_BASS_PA_SYNC_STATE_SYNCED;
        break;

    case PA_SYNC_STATE_ERROR:
        bass.pa_sync_state = BLE_SVC_AUDIO_BASS_PA_SYNC_STATE_SYNCED_FAILED;
        sink->pa_sync_state = PA_SYNC_STATE_IDLE;
        break;

    case PA_SYNC_STATE_TIMEOUT:
        switch (state_old) {
        case PA_SYNC_STATE_PENDING_PAST:
            bass.pa_sync_state = BLE_SVC_AUDIO_BASS_PA_SYNC_STATE_NO_PAST;
            break;
        default:
            broadcast_sink_pa_sync_state_update(sink, PA_SYNC_STATE_ERROR);
            return;
        }
        break;
    }

    (void)ble_svc_audio_bass_update_sync(&bass, sink->instance_id);
}

static void
broadcast_sink_big_sync_state_update(struct ble_audio_broadcast_sink *sink,
                                     enum big_sync_state_internal state)
{
    sink->big_sync_state = state;

    switch (state) {
    case BIG_SYNC_STATE_IDLE:
        if (sink->big_sync_params->destroy) {
            sink->big_sync_params->destroy(sink->big_sync_params);
            sink->big_sync_params = NULL;
        }

        break;

    default:
        break;
    }
}

int
broadcast_sink_iso_event(struct ble_iso_event *event, void *arg)
{
    struct ble_audio_broadcast_sink *sink = arg;

    switch (event->type) {
    case BLE_ISO_EVENT_BIG_SYNC_ESTABLISHED: {
        if (event->big_sync_established.status != 0) {
            broadcast_sink_big_sync_state_update(sink, BIG_SYNC_STATE_ERROR);
            break;
        }

        sink->iso_interval = event->big_sync_established.desc.iso_interval;
        broadcast_sink_big_sync_state_update(sink, BIG_SYNC_STATE_ACTIVE);

        break;
    }

    case BLE_ISO_EVENT_BIG_SYNC_TERMINATED:
        broadcast_sink_big_sync_state_update(sink, BIG_SYNC_STATE_IDLE);
        break;

    default:
        break;
    }

    return 0;
}

static void broadcast_sink_big_sync(struct ble_audio_broadcast_sink *sink,
                                    const char *broadcast_code)
{
    struct ble_iso_bis_params bis_params[MYNEWT_VAL(BLE_ISO_MAX_BISES)];
    struct ble_iso_big_sync_create_params big_params;
    int rc;

    BLE_AUDIO_DBG_ASSERT(sink->big_sync_params != NULL);

    for (uint8_t i = 0; i < sink->big_sync_params->num_bis; i++) {
        bis_params[i].bis_index = sink->big_sync_params->bis_params[i].bis_index;
        bis_params[i].cb = sink->big_sync_params->bis_params[i].cb;
        bis_params[i].cb_arg = sink->big_sync_params->bis_params[i].cb_arg;
    }

    big_params.sync_handle = sink->pa_sync_handle;
    big_params.broadcast_code = broadcast_code;
    big_params.mse = 0;
    big_params.sync_timeout = sink->big_sync_params->sync_timeout;
    big_params.cb = broadcast_sink_iso_event;
    big_params.cb_arg = sink;
    big_params.bis_cnt = sink->big_sync_params->num_bis;
    big_params.bis_params = bis_params;

    rc = ble_iso_big_sync_create(&big_params, &sink->big_handle);
    if (rc != 0) {
        broadcast_sink_big_sync_state_update(sink, BIG_SYNC_STATE_ERROR);
    } else {
        broadcast_sink_big_sync_state_update(sink, BIG_SYNC_STATE_PENDING_SYNC);
    }

    if (sink->big_sync_params->destroy) {
        sink->big_sync_params->destroy(sink->big_sync_params);
        sink->big_sync_params = NULL;
    }
}

static int
ble_audio_broadcast_sink_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_audio_broadcast_sink *sink = arg;
    struct ble_audio_event audio_event = { 0 };
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_PERIODIC_SYNC:
        if (event->periodic_sync.status == BLE_ERR_SUCCESS) {
            sink->pa_sync_handle = event->periodic_sync.sync_handle;
            broadcast_sink_pa_sync_state_update(sink, PA_SYNC_STATE_ACTIVE);
        } else if (event->periodic_sync.status == BLE_ERR_OPERATION_CANCELLED) {
            broadcast_sink_pa_sync_state_update(sink, PA_SYNC_STATE_IDLE);
        } else if (event->periodic_sync.status == BLE_ERR_CONN_ESTABLISHMENT) {
            broadcast_sink_pa_sync_state_update(sink, PA_SYNC_STATE_TIMEOUT);
        } else {
            broadcast_sink_pa_sync_state_update(sink, PA_SYNC_STATE_ERROR);
        }
        break;

    case BLE_GAP_EVENT_PERIODIC_SYNC_LOST:
        broadcast_sink_pa_sync_state_update(sink, PA_SYNC_STATE_IDLE);
        break;

    case BLE_GAP_EVENT_PERIODIC_TRANSFER:
        if (event->periodic_transfer.status == BLE_ERR_SUCCESS) {
            sink->pa_sync_handle = event->periodic_transfer.sync_handle;
            broadcast_sink_pa_sync_state_update(sink, PA_SYNC_STATE_ACTIVE);
        } else if (event->periodic_sync.status == BLE_ERR_OPERATION_CANCELLED) {
            broadcast_sink_pa_sync_state_update(sink, PA_SYNC_STATE_IDLE);
        } else if (event->periodic_transfer.status == BLE_ERR_CONN_ESTABLISHMENT) {
            broadcast_sink_pa_sync_state_update(sink, PA_SYNC_STATE_TIMEOUT);
        } else  {
            broadcast_sink_pa_sync_state_update(sink, PA_SYNC_STATE_ERROR);
        }
        break;

    case BLE_GAP_EVENT_BIGINFO_REPORT:
        sink = broadcast_sink_lookup_pa_sync_handle(event->biginfo_report.sync_handle);
        if (sink == NULL) {
            return 0;
        }

        sink->iso_interval = event->biginfo_report.iso_interval;
        sink->encrypted = event->biginfo_report.encryption;

        switch (sink->big_sync_state) {
        case BIG_SYNC_STATE_PENDING_BIG_INFO:
            if (sink->encrypted) {
                broadcast_sink_big_sync_state_update(sink, BIG_SYNC_STATE_PENDING_CODE);
            } else {
                broadcast_sink_big_sync(sink, NULL);
            }
            break;

        default:
            break;
        }

        break;

    case BLE_GAP_EVENT_PERIODIC_REPORT: {
        struct broadcast_sink_base_parse_arg parse_arg = {
                .base = &audio_event.base_report.base,
                .base_length = &audio_event.base_report.base_length
        };

        if (event->periodic_report.data_status != BLE_HCI_PERIODIC_DATA_STATUS_COMPLETE) {
            return 0;
        }

        audio_event.type = BLE_AUDIO_EVENT_BROADCAST_SINK_BASE_REPORT;
        audio_event.base_report.instance_id = sink->instance_id;
        audio_event.base_report.tx_power = event->periodic_report.tx_power;
        audio_event.base_report.rssi = event->periodic_report.rssi;

        rc = ble_hs_adv_parse(event->periodic_report.data,
                              event->periodic_report.data_length,
                              broadcast_sink_base_parse, &parse_arg);
        if (rc != 0 || parse_arg.base_length == 0) {
            /* Incorrectly formatted BASE. Terminate */
            ble_audio_broadcast_sink_pa_sync_term(sink->instance_id);
        } else {
            (void)sink->cb(&audio_event, sink->cb_arg);
        }

        break;
    }

    default:
        break;
    }

    return 0;
}

int
ble_audio_broadcast_sink_create(
        const struct ble_audio_broadcast_sink_create_params *params,
        uint8_t *instance_id)
{
    struct ble_svc_audio_bass_receiver_state receiver_state = { 0 };
    struct ble_audio_broadcast_sink *sink;
    int rc;

    if (params == NULL) {
        BLE_HS_LOG_ERROR("NULL params!\n");
        return BLE_HS_EINVAL;
    }

    if (instance_id == NULL) {
        BLE_HS_LOG_ERROR("NULL instance_id!\n");
        return BLE_HS_EINVAL;
    }

    if (params->adv_addr == NULL) {
        BLE_HS_LOG_ERROR("NULL adv_addr!\n");
        return BLE_HS_EINVAL;
    }

    if (params->cb == NULL) {
        BLE_HS_LOG_ERROR("NULL cb!\n");
        return BLE_HS_EINVAL;
    }

    /* FIXME: Magic number */
    if (params->adv_sid > 0x0f) {
        BLE_HS_LOG_ERROR("Invalid adv_sid!\n");
        return BLE_HS_EINVAL;
    }

    memcpy(&receiver_state.source_addr, params->adv_addr, sizeof(receiver_state.source_addr));
    receiver_state.source_adv_sid = params->adv_sid;
    receiver_state.broadcast_id = params->broadcast_id;

    rc = ble_svc_audio_bass_receive_state_add(&receiver_state, instance_id);
    if (rc != 0) {
        return rc;
    }

    sink = os_memblock_get(&ble_audio_broadcast_sink_pool);
    if (sink == NULL) {
        return BLE_HS_ENOMEM;
    }

    memset(sink, 0, sizeof(*sink));

    sink->instance_id = *instance_id;
    sink->cb = params->cb;
    sink->cb_arg = params->cb_arg;

    SLIST_INSERT_HEAD(&ble_audio_broadcast_sink_list, sink, next);

    return 0;
}

int
ble_audio_broadcast_sink_destroy(uint8_t instance_id)
{
    struct ble_audio_broadcast_sink *sink;
    int rc;

    sink = broadcast_sink_lookup_instance_id(instance_id);
    if (sink == NULL) {
        BLE_HS_LOG_ERROR("NULL sink!\n");
        return BLE_HS_ENOENT;
    }

    rc = ble_svc_audio_bass_receive_state_remove(sink->instance_id);
    if (rc != 0) {
        BLE_HS_LOG_DEBUG("Failed to remove receive state (rc %d)\n", rc);
    }

    os_memblock_put(&ble_audio_broadcast_sink_pool, sink);
    SLIST_REMOVE(&ble_audio_broadcast_sink_list, sink, ble_audio_broadcast_sink, next);

    return 0;
}

int
ble_audio_broadcast_sink_pa_sync(
        uint8_t instance_id,
        const struct ble_gap_periodic_sync_params *params)
{
    struct ble_svc_audio_bass_receiver_state *receiver_state;
    struct ble_audio_broadcast_sink *sink;
    int rc;

    if (params == NULL) {
        BLE_HS_LOG_ERROR("NULL params!\n");
        return BLE_HS_EINVAL;
    }

    sink = broadcast_sink_lookup_instance_id(instance_id);
    if (sink == NULL) {
        BLE_HS_LOG_ERROR("NULL sink!\n");
        return BLE_HS_ENOENT;
    }

    rc = ble_svc_audio_bass_receiver_state_get(instance_id, &receiver_state);
    BLE_AUDIO_DBG_ASSERT(rc == 0);

    switch (sink->pa_sync_state) {
        case PA_SYNC_STATE_PENDING_SCAN:
        case PA_SYNC_STATE_PENDING_PAST:
            return BLE_HS_EBUSY;
        case PA_SYNC_STATE_ACTIVE:
            return BLE_HS_EALREADY;
        default:
            break;
    }

    rc = ble_gap_periodic_adv_sync_create(&receiver_state->source_addr,
                                          receiver_state->source_adv_sid,
                                          params,
                                          ble_audio_broadcast_sink_gap_event,
                                          sink);
    if (rc != 0) {
        broadcast_sink_pa_sync_state_update(sink, PA_SYNC_STATE_ERROR);
    } else {
        broadcast_sink_pa_sync_state_update(sink, PA_SYNC_STATE_PENDING_SCAN);
    }

    return rc;
}

int
ble_audio_broadcast_sink_pa_sync_term(uint8_t instance_id)
{
    struct ble_audio_broadcast_sink *sink;
    int rc;

    sink = broadcast_sink_lookup_instance_id(instance_id);
    if (sink == NULL) {
        BLE_HS_LOG_ERROR("NULL sink!\n");
        return BLE_HS_ENOENT;
    }

    switch (sink->pa_sync_state) {
        case PA_SYNC_STATE_PENDING_SCAN:
            rc = ble_gap_periodic_adv_sync_create_cancel();
            break;
        case PA_SYNC_STATE_PENDING_PAST:
            rc = ble_gap_periodic_adv_sync_receive(
                    sink->past_conn_handle, NULL,
                    ble_audio_broadcast_sink_gap_event, sink);
            break;
        case PA_SYNC_STATE_ACTIVE:
            rc = ble_gap_periodic_adv_sync_terminate(sink->pa_sync_handle);
            break;
        default:
            rc = BLE_HS_EALREADY;
            break;
    }

    return rc;
}

int
ble_audio_broadcast_sink_big_sync(uint8_t instance_id,
                                  struct ble_audio_broadcast_sink_big_sync_params *params)
{
    struct ble_audio_broadcast_sink *sink;

    sink = broadcast_sink_lookup_instance_id(instance_id);
    if (sink == NULL) {
        BLE_HS_LOG_ERROR("NULL sink!\n");
        return BLE_HS_ENOENT;
    }

    if (params == NULL) {
        BLE_HS_LOG_ERROR("NULL params!\n");
        return BLE_HS_EINVAL;
    }

    if (params->num_bis > MYNEWT_VAL(BLE_ISO_MAX_BISES)) {
        return BLE_HS_ENOMEM;
    }

    switch (sink->big_sync_state) {
    case BIG_SYNC_STATE_PENDING_CODE:
    case BIG_SYNC_STATE_PENDING_SYNC:
    case BIG_SYNC_STATE_PENDING_BIG_INFO:
        return BLE_HS_EBUSY;
    case BIG_SYNC_STATE_ACTIVE:
        return BLE_HS_EALREADY;
    default:
        break;
    }

    sink->big_sync_params = params;

    broadcast_sink_big_sync_state_update(sink, BIG_SYNC_STATE_PENDING_BIG_INFO);

    return 0;
}

int
ble_audio_broadcast_sink_big_sync_term(uint8_t instance_id)
{
    struct ble_audio_broadcast_sink *sink;
    int rc;

    sink = broadcast_sink_lookup_instance_id(instance_id);
    if (sink == NULL) {
        BLE_HS_LOG_ERROR("NULL sink!\n");
        return BLE_HS_ENOENT;
    }

    switch (sink->big_sync_state) {
        case BIG_SYNC_STATE_PENDING_SYNC:
        case BIG_SYNC_STATE_ACTIVE:
            rc = ble_iso_big_sync_terminate(sink->big_handle);
            if (rc != 0) {
                return rc;
            }

            break;
        default:
            break;
    }

    broadcast_sink_big_sync_state_update(sink, BIG_SYNC_STATE_IDLE);

    return 0;
}

uint16_t
ble_audio_broadcast_sink_sync_timeout_calc(uint16_t interval, uint8_t retry_count)
{
    const uint16_t num_attempts = retry_count + 1;
    double sync_timeout;

    /* TODO: Do not use magic numbers */
    sync_timeout = ((uint32_t)interval * num_attempts) * 0.125;
    sync_timeout = MAX(0x000A, MIN(sync_timeout, 0x4000));

    return (uint16_t)sync_timeout;
}

int
ble_audio_broadcast_sink_init(void)
{
    int rc;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    rc = os_mempool_init(&ble_audio_broadcast_sink_pool,
                         MYNEWT_VAL(BLE_AUDIO_BROADCAST_SINK_MAX),
                         sizeof(struct ble_audio_broadcast_sink),
                         ble_audio_broadcast_sink_mem,
                         "ble_audio_broadcast_sink_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    return 0;
}
#endif /* BLE_ISO_BROADCAST_SINK */
