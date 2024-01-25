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

#include "sysinit/sysinit.h"

#if MYNEWT_VAL(BLE_AUDIO_BROADCAST_SINK)
#include "stdlib.h"

#include "audio/ble_audio.h"
#include "audio/ble_audio_broadcast_sink.h"
#include "audio/ble_audio_scan_delegator.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_iso.h"
#include "host/ble_uuid.h"

#include "ble_audio_priv.h"
#include "ble_audio_scan_delegator_priv.h"

#define CLAMP(_n, _min, _max)                   (MAX(_min, MIN(_n, _max)))
#define BROADCAST_ID_INVALID                    0xFFFFFFFF
#define BIS_INDEX_TEST(_bis_sync, _index)       (((_bis_sync) & (1 << ((_index) - 1))) > 0)
#define BIS_INDEX_SET(_bis_sync, _index)        ((_bis_sync) |= (1 << ((_index) - 1)))
#define BIS_INDEX_CLEAR(_bis_sync, _index)      ((_bis_sync) &= ~(1 << ((_index) - 1)))

static struct {
    ble_audio_broadcast_sink_action_fn *fn;
    void *arg;
} action_cb;

enum pa_sync_state_internal {
    PA_SYNC_STATE_IDLE,
    PA_SYNC_STATE_PENDING_DISC,
    PA_SYNC_STATE_PENDING_PAST,
    PA_SYNC_STATE_PENDING_SYNC,
    PA_SYNC_STATE_ACTIVE,
    PA_SYNC_STATE_ERROR,
    PA_SYNC_STATE_TIMEOUT,
};

enum big_sync_state_internal {
    BIG_SYNC_STATE_IDLE,
    BIG_SYNC_STATE_PENDING_BIG_INFO,
    BIG_SYNC_STATE_PENDING_CODE,
    BIG_SYNC_STATE_PENDING_BASE,
    BIG_SYNC_STATE_PENDING_SYNC,
    BIG_SYNC_STATE_FAILED,
    BIG_SYNC_STATE_ACTIVE,
};

struct ble_audio_broadcast_sink {
    /** Instance ID, same as BASS Source ID */
    uint8_t source_id;

    /** Internal PA sync state */
    enum pa_sync_state_internal pa_sync_state;

    /** Internal BIG sync state */
    enum big_sync_state_internal big_sync_state;

    /** Periodic sync handle */
    uint16_t pa_sync_handle;

    /** Connection handle or @ref BLE_HS_CONN_HANDLE_NONE */
    uint16_t past_conn_handle;

    /** BIG Handle */
    uint8_t big_handle;

    /** ISO Interval */
    uint16_t iso_interval;

    /** Burst Number */
    uint8_t bn;

    /** Number of SubEvents */
    uint8_t nse;

    /** Callback function */
    ble_audio_event_fn *cb;

    /** Broadcast code */
    uint8_t broadcast_code[BLE_AUDIO_BROADCAST_CODE_SIZE];

    /** The optional argument to pass to the callback function. */
    void *cb_arg;

    /** BIG Sync Parameters */
    struct ble_audio_broadcast_sink_big_sync_params *big_sync_params;

    /** If true, the broadcast is encrypted and requires broadcast_code */
    uint8_t is_encrypted : 1;
    /** If true, the broadcast_code value is valid */
    uint8_t broadcast_code_is_valid : 1;

    /** Internal subgroups state */
    uint8_t num_subgroups;
    struct {
        uint32_t bis_sync;
    } subgroups[MYNEWT_VAL(BLE_AUDIO_SCAN_DELEGATOR_SUBGROUP_MAX)];

    /** Singly-linked list entry. */
    SLIST_ENTRY(ble_audio_broadcast_sink) next;
};

static SLIST_HEAD(, ble_audio_broadcast_sink) ble_audio_broadcast_sink_list;
static struct os_mempool ble_audio_broadcast_sink_pool;
static os_membuf_t ble_audio_broadcast_sink_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_AUDIO_BROADCAST_SINK_MAX),
                    sizeof(struct ble_audio_broadcast_sink))];

/** If true, the discovery was started by us, otherwise by someone else */
static bool disc_self_initiated;

/** If true, the Periodic Advertising Sync is pending */
static bool periodic_adv_sync_in_progress;

static int gap_event_handler(struct ble_gap_event *event, void *arg);
static int iso_event_handler(struct ble_iso_event *event, void *arg);
static void big_sync_state_set(struct ble_audio_broadcast_sink *sink, enum big_sync_state_internal state_internal);
static void pa_sync_state_set(struct ble_audio_broadcast_sink *sink, enum pa_sync_state_internal state_internal);

static struct ble_audio_broadcast_sink *
broadcast_sink_get(uint8_t source_id)
{
    struct ble_audio_broadcast_sink *sink;

    SLIST_FOREACH(sink, &ble_audio_broadcast_sink_list, next) {
        if (source_id == sink->source_id) {
            return sink;
        }
    }

    return NULL;
}

static struct ble_audio_broadcast_sink *
broadcast_lookup_pa_sync_handle(uint16_t pa_sync_handle)
{
    struct ble_audio_broadcast_sink *sink;

    SLIST_FOREACH(sink, &ble_audio_broadcast_sink_list, next) {
        if (pa_sync_handle == sink->pa_sync_handle) {
            return sink;
        }
    }

    return NULL;
}

static struct ble_audio_broadcast_sink *
broadcast_sink_lookup_adv_sid_broadcast_id_pair(uint8_t adv_sid, uint32_t broadcast_id)
{
    struct ble_audio_scan_delegator_source_desc source_desc;
    struct ble_audio_broadcast_sink *sink;
    int rc;

    SLIST_FOREACH(sink, &ble_audio_broadcast_sink_list, next) {
        rc = ble_audio_scan_delegator_source_desc_get(sink->source_id, &source_desc);
        if (rc != 0) {
            BLE_HS_LOG_ERROR("source desc get failed (%d)\n", rc);
            continue;
        }

        if (source_desc.adv_sid == adv_sid && source_desc.broadcast_id == broadcast_id) {
            return sink;
        }
    }

    return NULL;
}

static struct ble_audio_broadcast_sink *
broadcast_sink_lookup_addr_adv_sid_pair(const ble_addr_t *addr, uint8_t adv_sid)
{
    struct ble_audio_scan_delegator_source_desc source_desc;
    struct ble_audio_broadcast_sink *sink;
    int rc;

    SLIST_FOREACH(sink, &ble_audio_broadcast_sink_list, next) {
        rc = ble_audio_scan_delegator_source_desc_get(sink->source_id, &source_desc);
        if (rc != 0) {
            BLE_HS_LOG_ERROR("source desc get failed (%d)\n", rc);
            continue;
        }

        if (source_desc.adv_sid == adv_sid && ble_addr_cmp(&source_desc.addr, addr) == 0) {
            return sink;
        }
    }

    return NULL;
}

static struct ble_audio_broadcast_sink *
broadcast_sink_lookup_pa_sync_state(enum pa_sync_state_internal state_internal)
{
    struct ble_audio_broadcast_sink *sink;

    SLIST_FOREACH(sink, &ble_audio_broadcast_sink_list, next) {
        if (sink->pa_sync_state == state_internal) {
            return sink;
        }
    }

    return NULL;
}

static int
disc_start(void)
{
    struct ble_audio_broadcast_sink_action action;
    struct ble_gap_ext_disc_params disc_params;
    int rc;

    if (ble_gap_disc_active()) {
        return 0;
    }

    disc_params.itvl = BLE_GAP_SCAN_FAST_INTERVAL_MIN;
    disc_params.window = BLE_GAP_SCAN_FAST_WINDOW;
    disc_params.passive = true;

    action.type = BLE_AUDIO_BROADCAST_SINK_ACTION_DISC_START;
    action.disc_start.params_preferred = &disc_params;

    rc = action_cb.fn(&action, action_cb.arg);
    if (rc != 0) {
        BLE_HS_LOG_WARN("disc start rejected by user (%d)\n", rc);
    } else {
        disc_self_initiated = true;
    }

    return rc;
}

static void
disc_stop(void)
{
    struct ble_audio_broadcast_sink_action action;
    int rc;

    if (!disc_self_initiated || !ble_gap_disc_active()) {
        return;
    }

    action.type = BLE_AUDIO_BROADCAST_SINK_ACTION_DISC_STOP;

    rc = action_cb.fn(&action, action_cb.arg);
    if (rc != 0) {
        BLE_HS_LOG_WARN("disc stop rejected by user (%d)\n", rc);
    }

    disc_self_initiated = false;
}

struct basic_audio_announcement_svc_data {
    /** BASE length */
    uint8_t length;

    /** BASE */
    const uint8_t *base;
};

struct service_data_uuid16 {
    struct basic_audio_announcement_svc_data basic_audio_announcement;
};

static void
service_data_uuid16_parse(const uint16_t uuid16, const uint8_t * const value, const uint8_t value_len,
                          void *user_data)
{
    struct service_data_uuid16 *data = user_data;

    if (uuid16 == BLE_BASIC_AUDIO_ANNOUNCEMENT_SVC_UUID) {
        data->basic_audio_announcement.base = value;
        data->basic_audio_announcement.length = value_len;
    }
}

struct periodic_report {
    struct service_data_uuid16 uuid16;
};

static int
periodic_report_parse(const struct ble_hs_adv_field *field, void *user_data)
{
    struct periodic_report *report = user_data;
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

        service_data_uuid16_parse(uuid16.value, &field->value[offset], value_len - offset, &report->uuid16);
        break;

    default:
        /* Continue */
        return BLE_HS_ENOENT;
    }

    /* Stop */
    return 0;
}

static uint32_t
subgroup_bis_sync_get(struct ble_audio_broadcast_sink *sink, uint8_t subgroup_index)
{
    if (subgroup_index > sink->num_subgroups) {
        return 0;
    }

    return sink->subgroups[subgroup_index].bis_sync;
}

static void
bass_big_state_update(struct ble_audio_broadcast_sink *sink, enum big_sync_state_internal from,
                      enum big_sync_state_internal to)
{
    struct ble_audio_scan_delegator_receive_state receive_state = {0};
    int rc;

    rc = ble_audio_scan_delegator_receive_state_get(sink->source_id, &receive_state);
    if (rc != 0) {
        BLE_HS_LOG_ERROR("receive state get failed (%d)\n", rc);
        return;
    }

    switch (to) {
    case BIG_SYNC_STATE_PENDING_CODE:
        receive_state.big_enc = BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_BROADCAST_CODE_MISSING;
        break;

    case BIG_SYNC_STATE_IDLE:
        if (from == BIG_SYNC_STATE_PENDING_CODE) {
            /* FIXME: this does not seem to be right */
            receive_state.big_enc = BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_BROADCAST_CODE_INVALID;
            memcpy(receive_state.bad_code, sink->broadcast_code, sizeof(receive_state.bad_code));
        } else if (from == BIG_SYNC_STATE_ACTIVE || from == BIG_SYNC_STATE_FAILED) {
            /* Iterate subgroup indexes to update the BIS Sync state */
            for (uint8_t index = 0; index < receive_state.num_subgroups; index++) {
                receive_state.subgroups[index].bis_sync = 0;
            }
        }
    /* fallthrough */

    case BIG_SYNC_STATE_PENDING_BIG_INFO:
    case BIG_SYNC_STATE_PENDING_BASE:
    case BIG_SYNC_STATE_PENDING_SYNC:
        receive_state.big_enc = BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_NONE;
        break;

    case BIG_SYNC_STATE_FAILED:
        receive_state.big_enc = BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_NONE;

        /* BASS v1.0 3.1.1.4 Add Source operation
         * "(...) if the server fails to synchronize to the BIG, the server shall write a value of
         * 0xFFFFFFFF (Failed to synchronize to BIG) to the BIS_Sync_State field
         */
        for (uint8_t index = 0; index < receive_state.num_subgroups; index++) {
            receive_state.subgroups[index].bis_sync = BLE_AUDIO_SCAN_DELEGATOR_BIS_SYNC_ANY;
        }
        break;

    case BIG_SYNC_STATE_ACTIVE:
        if (sink->is_encrypted) {
            receive_state.big_enc = BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_DECRYPTING;
        } else {
            receive_state.big_enc = BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_NONE;
        }

        /* Iterate subgroup indexes to update the BIS Sync state */
        for (uint8_t index = 0; index < receive_state.num_subgroups; index++) {
            receive_state.subgroups[index].bis_sync = subgroup_bis_sync_get(sink, index);
        }
        break;
    }

    rc = ble_audio_scan_delegator_receive_state_set(sink->source_id, &receive_state);
    if (rc != 0) {
        BLE_HS_LOG_ERROR("receive state update failed (%d)\n", rc);
    }
}

static enum ble_audio_broadcast_sink_sync_state
big_state_internal_to_api(enum big_sync_state_internal state_internal)
{
    switch (state_internal) {
    case BIG_SYNC_STATE_PENDING_CODE:
    case BIG_SYNC_STATE_PENDING_BIG_INFO:
    case BIG_SYNC_STATE_PENDING_BASE:
    case BIG_SYNC_STATE_PENDING_SYNC:
        return BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_INITIATED;

    case BIG_SYNC_STATE_ACTIVE:
        return BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_ESTABLISHED;

    default:
        return BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_NOT_SYNCED;
    }
}

static void
api_bis_state_update(struct ble_audio_broadcast_sink *sink, enum big_sync_state_internal from,
                     enum big_sync_state_internal to)
{
    struct ble_audio_event event;

    if (to == BIG_SYNC_STATE_ACTIVE) {
        /* special case. event already sent from big_sync_established_handler() */
        return;
    }

    event.type = BLE_AUDIO_EVENT_BROADCAST_SINK_BIS_SYNC_STATE;
    event.broadcast_sink_bis_sync_state.source_id = sink->source_id;
    event.broadcast_sink_bis_sync_state.state = big_state_internal_to_api(to);
    event.broadcast_sink_bis_sync_state.conn_handle = BLE_HS_CONN_HANDLE_NONE;

    for (uint8_t subgroup_index = 0; subgroup_index < sink->num_subgroups; subgroup_index++) {
        uint32_t bis_sync = sink->subgroups[subgroup_index].bis_sync;

        if (bis_sync != BLE_AUDIO_SCAN_DELEGATOR_BIS_SYNC_ANY) {
            for (uint8_t bis_index = 0; bis_sync > 0; bis_index++) {
                if (BIS_INDEX_TEST(bis_sync, bis_index)) {
                    event.broadcast_sink_bis_sync_state.bis_index = bis_index;
                    ble_audio_event_listener_call(&event);
                    BIS_INDEX_CLEAR(bis_sync, bis_index);
                }
            }
        }
    }
}

static void
big_sync_state_set(struct ble_audio_broadcast_sink *sink, enum big_sync_state_internal state_internal)
{
    enum big_sync_state_internal state_internal_old = sink->big_sync_state;

    if (state_internal == state_internal_old) {
        return;
    }

    sink->big_sync_state = state_internal;

    api_bis_state_update(sink, state_internal_old, state_internal);
    bass_big_state_update(sink, state_internal_old, state_internal);
}

static void
bass_pa_state_update(struct ble_audio_broadcast_sink *sink, enum pa_sync_state_internal from,
                     enum pa_sync_state_internal to)
{
    struct ble_audio_scan_delegator_receive_state receive_state;
    int rc;

    rc = ble_audio_scan_delegator_receive_state_get(sink->source_id, &receive_state);
    if (rc != 0) {
        BLE_HS_LOG_ERROR("receive state get failed (%d)\n", rc);
        return;
    }

    switch (to) {
    case PA_SYNC_STATE_IDLE:
    case PA_SYNC_STATE_PENDING_DISC:
    case PA_SYNC_STATE_PENDING_SYNC:
        receive_state.pa_sync_state = BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_NOT_SYNCED;
        break;

    case PA_SYNC_STATE_PENDING_PAST:
        receive_state.pa_sync_state = BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_SYNC_INFO_REQ;
        break;

    case PA_SYNC_STATE_ACTIVE:
        receive_state.pa_sync_state = BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_SYNCED;
        break;

    case PA_SYNC_STATE_ERROR:
        receive_state.pa_sync_state = BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_ERROR;
        break;

    case PA_SYNC_STATE_TIMEOUT:
        if (from == PA_SYNC_STATE_PENDING_PAST) {
            receive_state.pa_sync_state = BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_NO_PAST;
        } else {
            receive_state.pa_sync_state = BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_ERROR;
        }
        break;
    }

    rc = ble_audio_scan_delegator_receive_state_set(sink->source_id, &receive_state);
    if (rc != 0) {
        BLE_HS_LOG_ERROR("receive state set failed (%d)\n", rc);
    }
}

static enum ble_audio_broadcast_sink_sync_state
pa_state_internal_to_api(enum pa_sync_state_internal state_internal)
{
    switch (state_internal) {
    case PA_SYNC_STATE_PENDING_DISC:
    case PA_SYNC_STATE_PENDING_SYNC:
    case PA_SYNC_STATE_PENDING_PAST:
        return BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_INITIATED;

    case PA_SYNC_STATE_ACTIVE:
        return BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_ESTABLISHED;

    default:
        return BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_NOT_SYNCED;
    }
}

static void
api_pa_state_update(struct ble_audio_broadcast_sink *sink, enum pa_sync_state_internal from,
                    enum pa_sync_state_internal to)
{
    struct ble_audio_event event;

    event.type = BLE_AUDIO_EVENT_BROADCAST_SINK_PA_SYNC_STATE;
    event.broadcast_sink_pa_sync_state.source_id = sink->source_id;
    event.broadcast_sink_pa_sync_state.state = pa_state_internal_to_api(to);

    if (event.broadcast_sink_pa_sync_state.state != pa_state_internal_to_api(from)) {
        ble_audio_event_listener_call(&event);
    }
}

static void
pa_sync_state_set(struct ble_audio_broadcast_sink *sink, enum pa_sync_state_internal state_internal)
{
    enum pa_sync_state_internal state_internal_old = sink->pa_sync_state;

    if (state_internal == state_internal_old) {
        return;
    }

    sink->pa_sync_state = state_internal;
    api_pa_state_update(sink, state_internal_old, state_internal);
    bass_pa_state_update(sink, state_internal_old, state_internal);
}

static uint32_t
group_bis_sync_get(struct ble_audio_broadcast_sink *sink)
{
    uint32_t bis_sync_flat = 0;

    for (uint8_t subgroup_index = 0; subgroup_index < sink->num_subgroups; subgroup_index++) {
        bis_sync_flat |= sink->subgroups[subgroup_index].bis_sync;
    }

    return bis_sync_flat;
}

static void
pa_sync_create(void)
{
    struct ble_gap_periodic_sync_params periodic_sync_params = {0};
    struct ble_audio_broadcast_sink_action action;
    int rc;

    if (periodic_adv_sync_in_progress) {
        return;
    }

    action.type = BLE_AUDIO_BROADCAST_SINK_ACTION_PA_SYNC;
    action.pa_sync.out_params = &periodic_sync_params;

    rc = action_cb.fn(&action, action_cb.arg);
    if (rc != 0) {
        BLE_HS_LOG_WARN("rejected by user (%d)\n", rc);
        return;
    }

    rc = ble_gap_periodic_adv_sync_create(NULL, 0, &periodic_sync_params,
                                          gap_event_handler, NULL);
    if (rc != 0) {
        BLE_HS_LOG_ERROR("adv sync create failed (%d)\n", rc);
    } else {
        periodic_adv_sync_in_progress = true;
    }
}

static void
big_sync_established_handler(uint8_t source_id, uint8_t status, const struct ble_iso_big_desc *desc)
{
    struct ble_audio_broadcast_sink *sink;
    uint32_t bis_sync_state = 0;
    uint32_t group_bis_sync;
    uint8_t bis_index;

    /* Restart scan if needed */
    if (broadcast_sink_lookup_pa_sync_state(PA_SYNC_STATE_PENDING_SYNC) != NULL) {
        pa_sync_create();
        disc_start();
    } else if (broadcast_sink_lookup_pa_sync_state(PA_SYNC_STATE_PENDING_DISC) != NULL) {
        disc_start();
    }

    sink = broadcast_sink_get(source_id);
    if (sink == NULL) {
        BLE_HS_LOG_DEBUG("Unknown source_id %u\n", source_id);
        return;
    }

    if (status != 0) {
        big_sync_state_set(sink, BIG_SYNC_STATE_FAILED);
        return;
    }

    bis_index = 0;
    group_bis_sync = group_bis_sync_get(sink);

    for (uint8_t i = 0; i < desc->num_bis; i++) {
        uint16_t conn_handle = desc->conn_handle[i];

        for (; group_bis_sync > 0; bis_index++) {
            if (BIS_INDEX_TEST(group_bis_sync, bis_index)) {
                struct ble_audio_event event;

                event.type = BLE_AUDIO_EVENT_BROADCAST_SINK_BIS_SYNC_STATE;
                event.broadcast_sink_bis_sync_state.source_id = sink->source_id;
                event.broadcast_sink_bis_sync_state.bis_index = bis_index;
                event.broadcast_sink_bis_sync_state.state = BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_ESTABLISHED;
                event.broadcast_sink_bis_sync_state.conn_handle = conn_handle;

                ble_audio_event_listener_call(&event);

                BIS_INDEX_SET(bis_sync_state, bis_index);
                BIS_INDEX_CLEAR(group_bis_sync, bis_index);
                break;
            }
        }
    }

    /* Check whether all BISes got conn handle */
    group_bis_sync = group_bis_sync_get(sink);
    if (bis_sync_state != group_bis_sync) {
        BLE_HS_LOG_WARN("not all BISes synced");

        for (uint8_t subgroup_index = 0; subgroup_index < sink->num_subgroups; subgroup_index++) {
            uint32_t bis_sync_missing;

            bis_sync_missing = sink->subgroups[subgroup_index].bis_sync & ~bis_sync_state;
            if (bis_sync_missing == 0) {
                continue;
            }

            for (bis_index = 0; bis_sync_missing > 0; bis_index++) {
                if (BIS_INDEX_TEST(bis_sync_missing, bis_index)) {
                    struct ble_audio_event event;

                    event.type = BLE_AUDIO_EVENT_BROADCAST_SINK_BIS_SYNC_STATE;
                    event.broadcast_sink_bis_sync_state.source_id = sink->source_id;
                    event.broadcast_sink_bis_sync_state.bis_index = bis_index;
                    event.broadcast_sink_bis_sync_state.state = BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_NOT_SYNCED;
                    event.broadcast_sink_bis_sync_state.conn_handle = BLE_HS_CONN_HANDLE_NONE;

                    ble_audio_event_listener_call(&event);

                    BIS_INDEX_CLEAR(bis_sync_missing, bis_index);
                }
            }

            sink->subgroups[subgroup_index].bis_sync &= bis_sync_state;
        }
    }

    big_sync_state_set(sink, BIG_SYNC_STATE_ACTIVE);
}

static void
big_sync_terminated_handler(uint8_t source_id, uint8_t reason)
{
    struct ble_audio_broadcast_sink *sink;

    sink = broadcast_sink_get(source_id);
    if (sink == NULL) {
        BLE_HS_LOG_DEBUG("Unknown source_id %u\n", source_id);
        return;
    }

    big_sync_state_set(sink, BIG_SYNC_STATE_IDLE);
}

static int
iso_event_handler(struct ble_iso_event *event, void *arg)
{
    switch (event->type) {
    case BLE_ISO_EVENT_BIG_SYNC_ESTABLISHED:
        big_sync_established_handler(POINTER_TO_UINT(arg), event->big_sync_established.status,
                                     &event->big_sync_established.desc);
        break;

    case BLE_ISO_EVENT_BIG_SYNC_TERMINATED:
        big_sync_terminated_handler(POINTER_TO_UINT(arg), event->big_terminated.reason);
        break;

    default:
        break;
    }

    return 0;
}

static int
pa_sync_create_cancel(void)
{
    int rc;

    if (!periodic_adv_sync_in_progress) {
        return 0;
    }

    rc = ble_gap_periodic_adv_sync_create_cancel();
    if (rc != 0) {
        BLE_HS_LOG_ERROR("adv sync create cancel failed (%d)\n", rc);
    } else {
        periodic_adv_sync_in_progress = false;
    }

    return rc;
}

static int
pa_sync_add(const ble_addr_t *addr, uint8_t adv_sid)
{
    int rc;

    if (periodic_adv_sync_in_progress) {
        rc = pa_sync_create_cancel();
        if (rc != 0) {
            return rc;
        }
    }

    rc = ble_gap_add_dev_to_periodic_adv_list(addr, adv_sid);
    if (rc != 0) {
        BLE_HS_LOG_ERROR("add dev to periodic adv list failed (%d)\n", rc);
        /* TODO: destroy sink */
        return rc;
    }

    (void)pa_sync_create();

    return rc;
}

static int
pa_sync_remove(const ble_addr_t *addr, uint8_t adv_sid)
{
    int rc;

    if (periodic_adv_sync_in_progress) {
        rc = pa_sync_create_cancel();
        if (rc != 0) {
            return rc;
        }
    }

    rc = ble_gap_rem_dev_from_periodic_adv_list(addr, adv_sid);
    if (rc != 0) {
        BLE_HS_LOG_ERROR("rem dev from periodic adv list failed (%d)\n", rc);
    }

    return rc;
}

static void
periodic_sync_handler(const ble_addr_t *addr, uint8_t adv_sid, uint8_t status, uint16_t pa_sync_handle)
{
    struct ble_audio_broadcast_sink *sink;

    periodic_adv_sync_in_progress = false;

    sink = broadcast_sink_lookup_addr_adv_sid_pair(addr, adv_sid);
    if (sink == NULL) {
        BLE_HS_LOG_DEBUG("sink not found\n");
    } else {
        (void)pa_sync_remove(addr, adv_sid);

        if (status == BLE_ERR_SUCCESS) {
            sink->pa_sync_handle = pa_sync_handle;
            pa_sync_state_set(sink, PA_SYNC_STATE_ACTIVE);
        } else if (status == BLE_ERR_OPERATION_CANCELLED) {
            pa_sync_state_set(sink, PA_SYNC_STATE_IDLE);
        } else if (status == BLE_ERR_CONN_ESTABLISHMENT) {
            pa_sync_state_set(sink, PA_SYNC_STATE_TIMEOUT);
        } else {
            pa_sync_state_set(sink, PA_SYNC_STATE_ERROR);
        }
    }

    sink = broadcast_sink_lookup_pa_sync_state(PA_SYNC_STATE_PENDING_SYNC);
    if (sink != NULL) {
        pa_sync_create();
        disc_start();
    } else {
        disc_stop();
    }
}

static void
periodic_sync_lost_handler(uint16_t sync_handle, int reason)
{
    struct ble_audio_broadcast_sink *sink;

    sink = broadcast_lookup_pa_sync_handle(sync_handle);
    if (sink == NULL) {
        BLE_HS_LOG_DEBUG("Unknown sync_handle %u\n", sync_handle);
        return;
    }

    if (reason == BLE_HS_EDONE) {
        pa_sync_state_set(sink, PA_SYNC_STATE_IDLE);
    } else if (reason == BLE_HS_ETIMEOUT) {
        pa_sync_state_set(sink, PA_SYNC_STATE_TIMEOUT);
    } else {
        pa_sync_state_set(sink, PA_SYNC_STATE_ERROR);
    }
}

static void
biginfo_report_handler(uint16_t pa_sync_handle, uint16_t iso_interval, uint8_t nse, uint8_t bn, uint8_t encryption)
{
    struct ble_audio_broadcast_sink *sink;

    sink = broadcast_lookup_pa_sync_handle(pa_sync_handle);
    if (sink == NULL) {
        BLE_HS_LOG_DEBUG("Unknown pa_sync_handle %u\n", pa_sync_handle);
        return;
    }

    if (sink->big_sync_state == BIG_SYNC_STATE_PENDING_BIG_INFO) {
        sink->is_encrypted = encryption;
        sink->iso_interval = iso_interval;
        sink->nse = nse;
        sink->bn = bn;

        if (sink->is_encrypted && !sink->broadcast_code_is_valid) {
            big_sync_state_set(sink, BIG_SYNC_STATE_PENDING_CODE);
        } else {
            big_sync_state_set(sink, BIG_SYNC_STATE_PENDING_BASE);
        }
    }
}

static int
bis_params_get(struct ble_audio_broadcast_sink *sink, struct ble_audio_base_group *group,
               struct ble_audio_base_iter *subgroup_iter,
               struct ble_iso_bis_params bis_params[MYNEWT_VAL(BLE_ISO_MAX_BISES)], uint8_t *out_bis_cnt)
{
    uint8_t subgroup_index;
    uint32_t group_bis_sync = 0;
    uint8_t bis_cnt = 0;
    int rc = 0;

    if (action_cb.fn == NULL) {
        BLE_HS_LOG_ERROR("action_cb.fn is NULL\n");
        return BLE_HS_EAPP;
    }

    for (subgroup_index = 0; subgroup_index < group->num_subgroups; subgroup_index++) {
        struct ble_audio_base_subgroup subgroup;
        struct ble_audio_base_iter bis_iter;
        uint32_t subgroup_bis_sync_req;

        rc = ble_audio_base_subgroup_iter(subgroup_iter, &subgroup, &bis_iter);
        if (rc != 0) {
            break;
        }

        subgroup_bis_sync_req = subgroup_bis_sync_get(sink, subgroup_index);
        if (subgroup_bis_sync_req == 0) {
            /* No BISes requested for this subgroup */
            continue;
        }

        sink->subgroups[subgroup_index].bis_sync = 0;

        for (uint8_t i = 0; i < subgroup.num_bis && bis_cnt < MYNEWT_VAL(BLE_ISO_MAX_BISES); i++) {
            struct ble_audio_broadcast_sink_action action = {0};
            struct ble_audio_base_bis bis;

            rc = ble_audio_base_bis_iter(&bis_iter, &bis);
            if (rc != 0) {
                break;
            }

            /* Core 5.4 | Vol 4, Part E; 7.8.106 LE BIG Create Sync command
             * BIS[i]: 0x01 to 0x1F
             */
            if (bis.index < 0x01 || bis.index > 0x7F) {
                continue;
            }

            if (BIS_INDEX_TEST(group_bis_sync, bis.index)) {
                /* BAP_v1.0.1; 3.7.2.2 Basic Audio Announcements
                 *
                 * Rule 3: Every BIS in the BIG, denoted by its BIS_index value,
                 * shall only be present in one subgroup.
                 */
                BLE_HS_LOG_WARN("duplicated bis index 0x%02x", bis.index);
                continue;
            }

            if (!BIS_INDEX_TEST(subgroup_bis_sync_req, bis.index)) {
                continue;
            }

            action.type = BLE_AUDIO_BROADCAST_SINK_ACTION_BIS_SYNC;
            action.bis_sync.source_id = sink->source_id;
            action.bis_sync.subgroup_index = subgroup_index;
            action.bis_sync.bis = &bis;
            action.bis_sync.subgroup = &subgroup;

            rc = action_cb.fn(&action, action_cb.arg);
            if (rc != 0) {
                BLE_HS_LOG_WARN("bis sync rejected by user (%d)\n", rc);
            } else {
                BIS_INDEX_SET(sink->subgroups[subgroup_index].bis_sync, bis.index);
                bis_params[bis_cnt].bis_index = bis.index;
                bis_cnt++;
            }
        }

        group_bis_sync |= sink->subgroups[subgroup_index].bis_sync;
    }

    sink->num_subgroups = subgroup_index;

    *out_bis_cnt = bis_cnt;
    return rc;
}

static int
bis_params_bis_index_cmp(const void *p1, const void *p2)
{
    const struct ble_iso_bis_params *bis_params_1 = p1;
    const struct ble_iso_bis_params *bis_params_2 = p2;

    return bis_params_1->bis_index - bis_params_2->bis_index;
}

static void
big_sync(struct ble_audio_broadcast_sink *sink, const uint8_t *base, uint8_t base_len)
{
    struct ble_audio_broadcast_sink_action action;
    struct ble_audio_base_group group;
    struct ble_audio_base_iter subgroup_iter;
    struct ble_iso_big_sync_create_params big_sync_create_params;
    struct ble_iso_bis_params bis_params[MYNEWT_VAL(BLE_ISO_MAX_BISES)];
    char broadcast_code[BLE_AUDIO_BROADCAST_CODE_SIZE + 1];
    uint8_t bis_cnt = 0;
    int rc;

    rc = ble_audio_base_parse(base, base_len, &group, &subgroup_iter);
    if (rc != 0) {
        BLE_HS_LOG_ERROR("base parse failed (%d)\n", rc);
        ble_audio_broadcast_sink_stop(sink->source_id);
        return;
    }

    /* By default, the controller can schedule reception of any number of subevents up to NSE */
    big_sync_create_params.mse = 0x00;

    /* By default, (6 * ISO_Interval * 1.25) / 10 = 3/4 * ISO_Interval */
    big_sync_create_params.sync_timeout = (uint16_t)((3.0 * sink->iso_interval) / 4.0);
    big_sync_create_params.sync_timeout = CLAMP(big_sync_create_params.sync_timeout, 0x000A, 0x4000);

    action.type = BLE_AUDIO_BROADCAST_SINK_ACTION_BIG_SYNC;
    action.big_sync.source_id = sink->source_id;
    action.big_sync.iso_interval = sink->iso_interval;
    action.big_sync.nse = sink->nse;
    action.big_sync.bn = sink->bn;
    action.big_sync.presentation_delay = group.presentation_delay;
    action.big_sync.out_mse = &big_sync_create_params.mse;
    action.big_sync.out_sync_timeout = &big_sync_create_params.sync_timeout;

    if (action_cb.fn == NULL) {
        BLE_HS_LOG_ERROR("action_cb.fn is NULL\n");
        ble_audio_broadcast_sink_stop(sink->source_id);
        return;
    }

    rc = action_cb.fn(&action, action_cb.arg);
    if (rc != 0) {
        BLE_HS_LOG_WARN("big sync rejected by user (%d)\n", rc);
        ble_audio_broadcast_sink_stop(sink->source_id);
        return;
    }

    if (sink->broadcast_code_is_valid) {
        memcpy(broadcast_code, sink->broadcast_code, BLE_AUDIO_BROADCAST_CODE_SIZE);
        broadcast_code[BLE_AUDIO_BROADCAST_CODE_SIZE] = '\0';
        big_sync_create_params.broadcast_code = broadcast_code;
    } else {
        big_sync_create_params.broadcast_code = NULL;
    }

    big_sync_create_params.sync_handle = sink->pa_sync_handle;
    big_sync_create_params.cb = iso_event_handler;
    big_sync_create_params.cb_arg = UINT_TO_POINTER(sink->source_id);
    big_sync_create_params.bis_params = bis_params;

    rc = bis_params_get(sink, &group, &subgroup_iter, bis_params, &bis_cnt);
    if (rc != 0 || bis_cnt == 0) {
        ble_audio_broadcast_sink_stop(sink->source_id);
        return;
    }

    /* Sort the parameters by BIS index in ascending order. It is required to properly match the
     * BIS index with conn handle in BIG Sync Established event handler.
     */
    qsort(bis_params, bis_cnt, sizeof(*bis_params), bis_params_bis_index_cmp);

    big_sync_create_params.bis_cnt = bis_cnt;

    /* Stop scan first */
    disc_stop();

    rc = ble_iso_big_sync_create(&big_sync_create_params, &sink->big_handle);
    if (rc != 0) {
        BLE_HS_LOG_ERROR("big sync failed (%d)\n", rc);
        return;
    }

    big_sync_state_set(sink, BIG_SYNC_STATE_PENDING_SYNC);
}

static void
metadata_update(struct ble_audio_broadcast_sink *sink, const uint8_t *base, uint8_t base_len)
{
    struct ble_audio_event event;
    struct ble_audio_base_group group;
    struct ble_audio_base_iter subgroup_iter;
    int rc = 0;

    rc = ble_audio_base_parse(base, base_len, &group, &subgroup_iter);
    if (rc != 0) {
        BLE_HS_LOG_WARN("base parse failed (%d)\n", rc);
        return;
    }

    for (uint8_t subgroup_index = 0; subgroup_index < group.num_subgroups; subgroup_index++) {
        struct ble_audio_base_subgroup subgroup;
        uint32_t bis_sync_state;

        rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, NULL);
        if (rc != 0) {
            break;
        }

        bis_sync_state = subgroup_bis_sync_get(sink, subgroup_index);
        if (bis_sync_state == 0) {
            /* No BISes synced for this subgroup */
            continue;
        }

        event.type = BLE_AUDIO_EVENT_BROADCAST_SINK_METADATA;
        event.broadcast_sink_metadata.source_id = sink->source_id;
        event.broadcast_sink_metadata.bis_sync = bis_sync_state;
        event.broadcast_sink_metadata.metadata = subgroup.metadata;
        event.broadcast_sink_metadata.metadata_length = subgroup.metadata_len;

        /* TODO: filter duplicates */

        ble_audio_event_listener_call(&event);
    }
}

static void
periodic_report_handler(uint16_t pa_sync_handle, const uint8_t *data, uint8_t data_length, uint8_t data_status,
                        int8_t rssi, int8_t tx_power)
{
    struct ble_audio_broadcast_sink *sink;
    struct periodic_report report = {0};
    int rc;

    if (data_status != BLE_HCI_PERIODIC_DATA_STATUS_COMPLETE) {
        return;
    }

    sink = broadcast_lookup_pa_sync_handle(pa_sync_handle);
    if (sink == NULL) {
        BLE_HS_LOG_DEBUG("Unknown pa_sync_handle %u\n", pa_sync_handle);
        return;
    }

    if (sink->big_sync_state == BIG_SYNC_STATE_PENDING_BASE) {
        rc = ble_hs_adv_parse(data, data_length, periodic_report_parse, &report);
        if (rc != 0 || report.uuid16.basic_audio_announcement.length == 0) {
            BLE_HS_LOG_WARN("source_id %u incorrectly formatted BASE\n", sink->source_id);
            ble_audio_broadcast_sink_stop(sink->source_id);
            return;
        }

        big_sync(sink, report.uuid16.basic_audio_announcement.base, report.uuid16.basic_audio_announcement.length);
    } else if (sink->big_sync_state == BIG_SYNC_STATE_ACTIVE) {
        rc = ble_hs_adv_parse(data, data_length, periodic_report_parse, &report);
        if (rc != 0 || report.uuid16.basic_audio_announcement.length == 0) {
            BLE_HS_LOG_WARN("source_id %u incorrectly formatted BASE\n", sink->source_id);
            return;
        }

        metadata_update(sink, report.uuid16.basic_audio_announcement.base,
                        report.uuid16.basic_audio_announcement.length);
    }
}

static int
broadcast_id_parse_from_adv(const struct ble_hs_adv_field *field, void *user_data)
{
    const uint8_t value_len = field->length - sizeof(field->length);
    uint32_t *broadcast_id = user_data;

    if (field->type == BLE_HS_ADV_TYPE_SVC_DATA_UUID16) {
        ble_uuid16_t uuid16 = BLE_UUID16_INIT(0);
        uint8_t offset = 0;

        if (value_len < 2) {
            /* Continue parsing */
            return BLE_HS_ENOENT;
        }

        uuid16.value = get_le16(&field->value[offset]);
        offset += 2;

        if (uuid16.value == BLE_BROADCAST_AUDIO_ANNOUNCEMENT_SVC_UUID) {
            if ((value_len - offset) >= 3) {
                *broadcast_id = get_le24(&field->value[offset]);
            }

            /* stop parsing */
            return 0;
        }
    }

    /* continue parsing */
    return BLE_HS_ENOENT;
}

static void
ext_disc_handler(const ble_addr_t *addr, uint8_t adv_sid, const uint8_t *data, uint8_t data_length)
{
    uint32_t broadcast_id = BROADCAST_ID_INVALID;
    struct ble_audio_broadcast_sink *sink;
    int rc;

    if (broadcast_sink_lookup_pa_sync_state(PA_SYNC_STATE_PENDING_DISC) == NULL) {
        return;
    }

    rc = ble_hs_adv_parse(data, data_length, broadcast_id_parse_from_adv, &broadcast_id);
    if (rc != 0 || broadcast_id == BROADCAST_ID_INVALID) {
        return;
    }

    sink = broadcast_sink_lookup_adv_sid_broadcast_id_pair(adv_sid, broadcast_id);
    if (sink == NULL) {
        return;
    }

    if (sink->pa_sync_state == PA_SYNC_STATE_PENDING_DISC) {
        rc = pa_sync_add(addr, adv_sid);
        if (rc != 0) {
            /* TODO: */
        } else {
            pa_sync_state_set(sink, PA_SYNC_STATE_PENDING_SYNC);
        }
    }
}

static int
gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_PERIODIC_SYNC:
        periodic_sync_handler(&event->periodic_sync.adv_addr, event->periodic_sync.sid,
                              event->periodic_sync.status, event->periodic_sync.sync_handle);
        break;

    case BLE_GAP_EVENT_PERIODIC_SYNC_LOST:
        periodic_sync_lost_handler(event->periodic_sync_lost.sync_handle, event->periodic_sync_lost.reason);
        break;

    case BLE_GAP_EVENT_PERIODIC_TRANSFER:
        periodic_sync_handler(&event->periodic_transfer.adv_addr, event->periodic_transfer.sid,
                              event->periodic_transfer.status, event->periodic_transfer.sync_handle);
        break;

    case BLE_GAP_EVENT_BIGINFO_REPORT:
        biginfo_report_handler(event->biginfo_report.sync_handle, event->biginfo_report.iso_interval,
                               event->biginfo_report.nse, event->biginfo_report.bn, event->biginfo_report.encryption);

        break;

    case BLE_GAP_EVENT_PERIODIC_REPORT:
        periodic_report_handler(event->periodic_report.sync_handle, event->periodic_report.data,
                                event->periodic_report.data_length,
                                event->periodic_report.data_status, event->periodic_report.rssi,
                                event->periodic_report.tx_power);
        break;

    case BLE_GAP_EVENT_EXT_DISC:
        ext_disc_handler(&event->ext_disc.addr, event->ext_disc.sid, event->ext_disc.data,
                         event->ext_disc.length_data);
        break;

    default:
        break;
    }

    return 0;
}

int
ble_audio_broadcast_sink_cb_set(ble_audio_broadcast_sink_action_fn *cb, void *arg)
{
    if (action_cb.fn != NULL) {
        return BLE_HS_EALREADY;
    }

    action_cb.fn = cb;
    action_cb.arg = arg;

    return 0;
}

void
ble_audio_broadcast_sink_code_set(uint8_t source_id, const uint8_t broadcast_code[BLE_AUDIO_BROADCAST_CODE_SIZE])
{
    struct ble_audio_broadcast_sink *sink;

    BLE_AUDIO_DBG_ASSERT(broadcast_code != NULL);

    sink = broadcast_sink_get(source_id);
    if (sink == NULL) {
        BLE_HS_LOG_DEBUG("Unknown source_id %u\n", source_id);
        return;
    }

    if (sink->big_sync_state == BIG_SYNC_STATE_PENDING_CODE) {
        memcpy(sink->broadcast_code, broadcast_code, BLE_AUDIO_BROADCAST_CODE_SIZE);

        big_sync_state_set(sink, BIG_SYNC_STATE_PENDING_BASE);
    }
}

static struct ble_audio_broadcast_sink *
broadcast_sink_new(uint8_t source_id)
{
    struct ble_audio_broadcast_sink *sink;

    sink = os_memblock_get(&ble_audio_broadcast_sink_pool);
    if (sink == NULL) {
        BLE_HS_LOG_WARN("Out of memory\n");
        return NULL;
    }

    memset(sink, 0, sizeof(*sink));

    sink->source_id = source_id;

    SLIST_INSERT_HEAD(&ble_audio_broadcast_sink_list, sink, next);

    return sink;
}

static int
pa_sync_receive(struct ble_audio_broadcast_sink *sink, const uint16_t *conn_handle)
{
    struct ble_audio_broadcast_sink_action action;
    struct ble_audio_scan_delegator_source_desc source_desc;
    struct ble_gap_periodic_sync_params periodic_sync_params = {0};
    int rc;

    rc = ble_audio_scan_delegator_source_desc_get(sink->source_id, &source_desc);
    if (rc != 0) {
        BLE_HS_LOG_ERROR("source desc get failed (%d)\n", rc);
        return rc;
    }

    if (action_cb.fn == NULL) {
        BLE_HS_LOG_ERROR("action_cb.fn is NULL\n");
        return BLE_HS_EAPP;
    }

    action.type = BLE_AUDIO_BROADCAST_SINK_ACTION_PA_SYNC;
    action.pa_sync.out_params = &periodic_sync_params;

    rc = action_cb.fn(&action, action_cb.arg);
    if (rc != 0) {
        BLE_HS_LOG_WARN("pa sync rejected by user (%d)\n", rc);
        return rc;
    }

    rc = ble_gap_periodic_adv_sync_receive(
            *conn_handle, &periodic_sync_params, gap_event_handler, NULL);
    if (rc != 0) {
        BLE_HS_LOG_ERROR("sync receive failed (%d)\n", rc);
        return rc;
    }

    return 0;
}

static int
broadcast_sink_start(uint8_t source_id, const uint8_t *broadcast_code, uint16_t *conn_handle)
{
    struct ble_audio_scan_delegator_source_desc source_desc;
    struct ble_audio_broadcast_sink *sink;
    int rc;

    rc = ble_audio_scan_delegator_source_desc_get(source_id, &source_desc);
    if (rc != 0) {
        BLE_HS_LOG_ERROR("source desc get failed (%d)\n", rc);
        return rc;
    }

    sink = broadcast_sink_get(source_id);
    if (sink == NULL) {
        sink = broadcast_sink_new(source_id);
        if (sink == NULL) {
            return BLE_HS_ENOMEM;
        }
    }

    if (broadcast_code != NULL) {
        memcpy(sink->broadcast_code, broadcast_code, BLE_AUDIO_BROADCAST_CODE_SIZE);
        sink->broadcast_code_is_valid = true;
    }

    /* If not previously set, let the application decide which BISes to sync */
    if (sink->num_subgroups == 0) {
        sink->num_subgroups = ARRAY_SIZE(sink->subgroups);
        for (uint8_t i = 0; i < sink->num_subgroups; i++) {
            sink->subgroups[i].bis_sync = BLE_AUDIO_SCAN_DELEGATOR_BIS_SYNC_ANY;
        }
    }

    switch (sink->pa_sync_state) {
    case PA_SYNC_STATE_PENDING_PAST:
    case PA_SYNC_STATE_PENDING_DISC:
    case PA_SYNC_STATE_PENDING_SYNC:
    case PA_SYNC_STATE_ACTIVE:
        break;

    case PA_SYNC_STATE_IDLE:
    case PA_SYNC_STATE_ERROR:
    case PA_SYNC_STATE_TIMEOUT:
        if (conn_handle != NULL) {
            /* sync using PAST procedure */
            rc = pa_sync_receive(sink, conn_handle);
            if (rc != 0) {
                return rc;
            }
            pa_sync_state_set(sink, PA_SYNC_STATE_PENDING_PAST);
        } else if (source_desc.addr.type == BLE_ADDR_PUBLIC) {
            /* sync to public address (we don't need to scan, as the address won't change) */
            rc = pa_sync_add(&source_desc.addr, source_desc.adv_sid);
            if (rc != 0) {
                return rc;
            }
            pa_sync_state_set(sink, PA_SYNC_STATE_PENDING_SYNC);
        } else {
            /* scan to find broadcaster using Adv SID and Broadcast ID */
            rc = disc_start();
            if (rc != 0) {
                return rc;
            }
            pa_sync_state_set(sink, PA_SYNC_STATE_PENDING_DISC);
        }
        break;

    default:
        BLE_AUDIO_DBG_ASSERT(false);
        ble_audio_broadcast_sink_stop(source_id);
        return BLE_HS_EAGAIN;
    }

    switch (sink->big_sync_state) {
    case BIG_SYNC_STATE_ACTIVE:
    case BIG_SYNC_STATE_PENDING_BIG_INFO:
    case BIG_SYNC_STATE_PENDING_SYNC:
    case BIG_SYNC_STATE_PENDING_BASE:
        break;

    case BIG_SYNC_STATE_PENDING_CODE:
        if (broadcast_code != NULL) {
            big_sync_state_set(sink, BIG_SYNC_STATE_PENDING_BASE);
        }
        break;

    case BIG_SYNC_STATE_FAILED:
    case BIG_SYNC_STATE_IDLE:
        big_sync_state_set(sink, BIG_SYNC_STATE_PENDING_BIG_INFO);
        break;

    default:
        BLE_AUDIO_DBG_ASSERT(false);
        ble_audio_broadcast_sink_stop(source_id);
        return BLE_HS_EAGAIN;
    }

    if (sink->pa_sync_state == PA_SYNC_STATE_ACTIVE &&
        sink->big_sync_state == BIG_SYNC_STATE_ACTIVE) {
        return BLE_HS_EDONE;
    }

    return 0;
}

int
ble_audio_broadcast_sink_start(uint8_t source_id, const struct ble_audio_broadcast_sink_add_params *params)
{
    return broadcast_sink_start(source_id,
                                params->broadcast_code_is_valid ? params->broadcast_code : NULL,
                                NULL);
}

static void
ble_audio_broadcast_sink_destroy(struct ble_audio_broadcast_sink *sink)
{
    os_error_t os_error;

    os_error = os_memblock_put(&ble_audio_broadcast_sink_pool, sink);
    if (os_error != OS_OK) {
        BLE_HS_LOG_ERROR("Failed to put memory block (os_error %d)\n", os_error);
        return;
    }

    SLIST_REMOVE(&ble_audio_broadcast_sink_list, sink, ble_audio_broadcast_sink, next);
}

static int
pa_sync_term(struct ble_audio_broadcast_sink *sink)
{
    int rc;

    switch (sink->pa_sync_state) {
    case PA_SYNC_STATE_ACTIVE:
        rc = ble_gap_periodic_adv_sync_terminate(sink->pa_sync_handle);
        if (rc != 0) {
            BLE_HS_LOG_ERROR("adv sync terminate failed (%d)\n", rc);
            return rc;
        }
        break;

    case PA_SYNC_STATE_PENDING_PAST:
        rc = ble_gap_periodic_adv_sync_receive(sink->past_conn_handle, NULL, gap_event_handler, NULL);
        if (rc != 0) {
            BLE_HS_LOG_ERROR("adv sync receive cancel failed (%d)\n", rc);
            return rc;
        }
        break;

    case PA_SYNC_STATE_PENDING_SYNC: {
        struct ble_audio_scan_delegator_source_desc source_desc;

        rc = ble_audio_scan_delegator_source_desc_get(sink->source_id, &source_desc);
        if (rc != 0) {
            BLE_HS_LOG_ERROR("source desc get failed (%d)\n", rc);
            return rc;
        }

        rc = pa_sync_remove(&source_desc.addr, source_desc.adv_sid);
        if (rc != 0) {
            return rc;
        }
        break;
    }

    default:
        break;
    }

    pa_sync_state_set(sink, PA_SYNC_STATE_IDLE);

    return 0;
}

static int
big_sync_term(struct ble_audio_broadcast_sink *sink)
{
    int rc;

    switch (sink->big_sync_state) {
    case BIG_SYNC_STATE_ACTIVE:
    case BIG_SYNC_STATE_PENDING_SYNC:
        rc = ble_iso_big_sync_terminate(sink->big_handle);
        if (rc != 0) {
            BLE_HS_LOG_ERROR("big sync terminate failed (%d)\n", rc);
            return rc;
        }
        break;

    case BIG_SYNC_STATE_IDLE:
    case BIG_SYNC_STATE_FAILED:
    case BIG_SYNC_STATE_PENDING_CODE:
    case BIG_SYNC_STATE_PENDING_BASE:
    case BIG_SYNC_STATE_PENDING_BIG_INFO:
        break;
    }

    big_sync_state_set(sink, BIG_SYNC_STATE_IDLE);

    return 0;
}

int
ble_audio_broadcast_sink_stop(uint8_t source_id)
{
    struct ble_audio_broadcast_sink *sink;
    int rc;

    sink = broadcast_sink_get(source_id);
    if (sink == NULL) {
        BLE_HS_LOG_WARN("no sink with source_id=0x%02x\n", source_id);
        return 0;
    }

    rc = pa_sync_term(sink);
    if (rc != 0) {
        return rc;
    }

    rc = big_sync_term(sink);
    if (rc != 0) {
        return rc;
    }

    ble_audio_broadcast_sink_destroy(sink);

    return 0;
}

int
ble_audio_broadcast_sink_metadata_update(uint8_t source_id,
                                         const struct ble_audio_broadcast_sink_metadata_update_params *params)
{
    struct ble_audio_broadcast_sink *sink;

    sink = broadcast_sink_get(source_id);
    if (sink == NULL) {
        BLE_HS_LOG_WARN("no sink with source_id=0x%02x\n", source_id);
        return 0;
    }

    return ble_audio_scan_delegator_metadata_update(sink->source_id, params->subgroup_index, params->metadata,
                                                    params->metadata_length);
}

int
ble_audio_broadcast_sink_config(uint8_t source_id, uint16_t conn_handle,
                                const struct ble_audio_scan_delegator_sync_opt *sync_opt)
{
    struct ble_audio_broadcast_sink *sink;
    int rc;

    BLE_AUDIO_DBG_ASSERT(sync_opt != NULL);

    sink = broadcast_sink_get(source_id);
    if (sink == NULL) {
        if (sync_opt->pa_sync != BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_DO_NOT_SYNC) {
            sink = broadcast_sink_new(source_id);
            if (sink == NULL) {
                return BLE_HS_ENOMEM;
            }
        } else {
            /* nothing to do */
            return 0;
        }
    }

    if (sync_opt->pa_sync != BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_DO_NOT_SYNC) {
        /* TODO: Skip if the BIS Sync is same */
        if (sink->num_subgroups != 0) {
            rc = big_sync_term(sink);
            if (rc != 0) {
                return rc;
            }
        }

        sink->num_subgroups = sync_opt->num_subgroups;

        for (uint8_t subgroup_index = 0; subgroup_index < sink->num_subgroups; subgroup_index++) {
            sink->subgroups[subgroup_index].bis_sync = sync_opt->subgroups[subgroup_index].bis_sync;
        }

        if (sync_opt->pa_sync == BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_PAST_NOT_AVAILABLE) {
            rc = broadcast_sink_start(source_id, NULL, NULL);
        } else {
            rc = broadcast_sink_start(source_id, NULL, &conn_handle);
        }
    } else {
        rc = pa_sync_term(sink);
    }

    return rc;
}

int
ble_audio_broadcast_sink_init(void)
{
    static struct ble_gap_event_listener gap_event_listener;
    int rc;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    rc = os_mempool_init(&ble_audio_broadcast_sink_pool,
                         MYNEWT_VAL(BLE_AUDIO_BROADCAST_SINK_MAX),
                         sizeof(struct ble_audio_broadcast_sink),
                         ble_audio_broadcast_sink_mem,
                         "ble_audio_broadcast_sink_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gap_event_listener_register(&gap_event_listener, gap_event_handler, NULL);
    SYSINIT_PANIC_ASSERT(rc == 0);

    return 0;
}
#endif /* BLE_AUDIO_BROADCAST_SINK */
