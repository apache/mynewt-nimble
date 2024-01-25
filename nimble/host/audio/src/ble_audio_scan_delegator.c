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

#if MYNEWT_VAL(BLE_AUDIO_SCAN_DELEGATOR)
#include "audio/ble_audio.h"
#include "audio/ble_audio_broadcast_sink.h"
#include "audio/ble_audio_scan_delegator.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"

#include "../services/bass/include/services/bass/ble_audio_svc_bass.h"

#include "ble_audio_priv.h"
#include "ble_audio_broadcast_sink_priv.h"

static ble_audio_scan_delegator_action_fn *action_cb;

int
ble_audio_scan_delegator_source_desc_get(uint8_t source_id, struct ble_audio_scan_delegator_source_desc *source_desc)
{
    struct ble_svc_audio_bass_receiver_state *state;
    int rc;

    rc = ble_svc_audio_bass_receiver_state_get(source_id, &state);
    if (rc != 0) {
        BLE_HS_LOG_ERROR("bass receiver state get failed (%d)\n", rc);
        return rc;
    }

    source_desc->addr = state->source_addr;
    source_desc->adv_sid = state->source_adv_sid;
    source_desc->broadcast_id = state->broadcast_id;

    return 0;
}

int
ble_audio_scan_delegator_receive_state_get(uint8_t source_id, struct ble_audio_scan_delegator_receive_state *out_state)
{
    struct ble_svc_audio_bass_receiver_state *state;
    int rc;

    rc = ble_svc_audio_bass_receiver_state_get(source_id, &state);
    if (rc != 0) {
        BLE_HS_LOG_ERROR("bass receiver state get failed (%d)\n", rc);
        return rc;
    }

    out_state->pa_sync_state = (uint8_t)state->pa_sync_state;
    out_state->big_enc = (uint8_t)state->big_encryption;
    if (out_state->big_enc == BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_BROADCAST_CODE_INVALID) {
        memcpy(out_state->bad_code, state->bad_code, sizeof(out_state->bad_code));
    }
    out_state->num_subgroups = state->num_subgroups;
    for (uint8_t i = 0; i < out_state->num_subgroups; i++) {
        out_state->subgroups[i].bis_sync = state->subgroups[i].bis_sync_state;
        out_state->subgroups[i].metadata = state->subgroups[i].metadata;
        out_state->subgroups[i].metadata_length = state->subgroups[i].metadata_length;
    }

    return 0;
}

int
ble_audio_scan_delegator_metadata_update(uint8_t source_id, uint8_t subgroup_index, const uint8_t *metadata,
                                         uint8_t metadata_length)
{
    struct ble_svc_audio_bass_metadata_params params;

    params.subgroup_idx = subgroup_index;
    params.metadata = metadata;
    params.metadata_length = metadata_length;

    return ble_svc_audio_bass_update_metadata(&params, source_id);
}

static int
action_call(struct ble_audio_scan_delegator_action *action, void *arg)
{
    int rc;

    if (action_cb == NULL) {
        BLE_HS_LOG_ERROR("callback is NULL\n");
        return BLE_HS_EAPP;
    }

    rc = action_cb(action, arg);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
source_desc_init(struct ble_audio_scan_delegator_source_desc *source_desc, const ble_addr_t *addr, uint8_t adv_sid,
                 uint32_t broadcast_id)
{
    source_desc->addr = *addr;
    source_desc->adv_sid = adv_sid;
    source_desc->broadcast_id = broadcast_id;
}

static void
subgroups_init(struct ble_audio_scan_delegator_subgroup *subgroups,
               const struct ble_svc_audio_bass_subgroup *bass_subgroups, uint8_t num_subgroups)
{
    BLE_AUDIO_DBG_ASSERT(num_subgroups < MYNEWT_VAL(BLE_AUDIO_SCAN_DELEGATOR_SUBGROUP_MAX));

    for (uint8_t i = 0; i < num_subgroups; i++) {
        subgroups[i].bis_sync = bass_subgroups[i].bis_sync_state;
        subgroups[i].metadata_length = bass_subgroups[i].metadata_length;
        subgroups[i].metadata = bass_subgroups[i].metadata;
    }
}

static void
sync_opt_init(struct ble_audio_scan_delegator_sync_opt *sync_opt, enum ble_svc_audio_bass_pa_sync pa_sync,
              uint16_t pa_interval, const struct ble_svc_audio_bass_subgroup *bass_subgroups, uint8_t num_subgroups)
{
    sync_opt->pa_sync = (uint8_t)pa_sync;
    sync_opt->pa_interval = pa_interval;
    sync_opt->num_subgroups = num_subgroups;
    if (sync_opt->num_subgroups > 0) {
        subgroups_init(sync_opt->subgroups, bass_subgroups, num_subgroups);
    }
}

static int
bass_add_source_op_handler(struct ble_svc_audio_bass_operation *op, void *arg)
{
    struct ble_audio_scan_delegator_action action = {0};
    const uint8_t source_id = op->add_source.source_id;
    int rc;

    source_desc_init(&action.source_add.source_desc, &op->add_source.adv_addr,
                     op->add_source.adv_sid, op->add_source.broadcast_id);
    sync_opt_init(&action.source_add.sync_opt, op->add_source.pa_sync, op->add_source.pa_interval,
                  op->add_source.subgroups, op->add_source.num_subgroups);

    action.type = BLE_AUDIO_SCAN_DELEGATOR_ACTION_SOURCE_ADD;
    action.source_add.source_id = source_id;
    action.source_add.out_source_id_to_swap = op->add_source.out_source_id_to_swap;

    rc = action_call(&action, arg);
    if (rc != 0) {
        BLE_HS_LOG_DEBUG("API callback (%d)\n", rc);
        return rc;
    }

#if MYNEWT_VAL(BLE_AUDIO_BROADCAST_SINK)
    if (op->add_source.out_source_id_to_swap != NULL) {
        rc = ble_audio_broadcast_sink_stop(*op->add_source.out_source_id_to_swap);
        if (rc != 0) {
            BLE_HS_LOG_WARN("sink stop failed (%d)\n", rc);
        }
    }

    rc = ble_audio_broadcast_sink_config(source_id, op->conn_handle, &action.source_add.sync_opt);
    if (rc != 0) {
        BLE_HS_LOG_WARN("sink config failed (%d)\n", rc);
    }
#endif /* BLE_AUDIO_BROADCAST_SINK */

    return 0;
}

static int
bass_modify_source_op_handler(struct ble_svc_audio_bass_operation *op, void *arg)
{
    struct ble_audio_scan_delegator_action action = {0};
    struct ble_audio_scan_delegator_sync_opt *sync_opt;
    const uint8_t source_id = op->modify_source.source_id;
    int rc;

    sync_opt = &action.source_modify.sync_opt;
    sync_opt_init(sync_opt, op->modify_source.pa_sync, op->modify_source.pa_interval, NULL, 0);

    BLE_AUDIO_DBG_ASSERT(sync_opt->num_subgroups < ARRAY_SIZE(subgroups));

    for (uint8_t i = 0; i < sync_opt->num_subgroups; i++) {
        sync_opt->subgroups[i].bis_sync = op->modify_source.bis_sync[i];
        /* FIXME: Missing metadata in Modify Source */
    }

    action.type = BLE_AUDIO_SCAN_DELEGATOR_ACTION_SOURCE_MODIFY;
    action.source_modify.source_id = source_id;

    rc = action_call(&action, arg);
    if (rc != 0) {
        return rc;
    }

#if MYNEWT_VAL(BLE_AUDIO_BROADCAST_SINK)
    rc = ble_audio_broadcast_sink_config(source_id, op->conn_handle, sync_opt);
    if (rc != 0) {
        BLE_HS_LOG_WARN("sink config failed (%d)\n", rc);
    }
#endif /* BLE_AUDIO_BROADCAST_SINK */

    return 0;
}

static int
bass_remove_source_op_handler(struct ble_svc_audio_bass_operation *op, void *arg)
{
    struct ble_audio_scan_delegator_action action;
    const uint8_t source_id = op->remove_source.source_id;
    int rc;

    action.type = BLE_AUDIO_SCAN_DELEGATOR_ACTION_SOURCE_REMOVE;
    action.source_remove.source_id = source_id;

    rc = action_call(&action, arg);
    if (rc != 0) {
        return rc;
    }

#if MYNEWT_VAL(BLE_AUDIO_BROADCAST_SINK)
    rc = ble_audio_broadcast_sink_stop(source_id);
    if (rc != 0) {
        BLE_HS_LOG_WARN("sink stop failed (%d)\n", rc);
    }
#endif /* BLE_AUDIO_BROADCAST_SINK */

    return 0;
}

static int
bass_accept_fn(struct ble_svc_audio_bass_operation *op, void *arg)
{
    switch (op->op) {
    case BLE_SVC_AUDIO_BASS_OPERATION_ADD_SOURCE:
        return bass_add_source_op_handler(op, arg);

    case BLE_SVC_AUDIO_BASS_OPERATION_MODIFY_SOURCE:
        return bass_modify_source_op_handler(op, arg);

    case BLE_SVC_AUDIO_BASS_OPERATION_REMOVE_SOURCE:
        return bass_remove_source_op_handler(op, arg);

    default:
        return BLE_HS_ENOTSUP;
    }
}

int
ble_audio_scan_delegator_action_fn_set(ble_audio_scan_delegator_action_fn *fn, void *arg)
{
    int rc;

    if (fn == NULL) {
        BLE_HS_LOG_ERROR("callback is NULL\n");
        return BLE_HS_EINVAL;
    }

    if (action_cb != NULL) {
        return BLE_HS_EALREADY;
    }

    action_cb = fn;

    rc = ble_svc_audio_bass_accept_fn_set(bass_accept_fn, arg);
    if (rc != 0) {
        action_cb = NULL;
    }

    return 0;
}

int
ble_audio_scan_delegator_receive_state_add(const struct ble_audio_scan_delegator_receive_state_add_params *params,
                                           uint8_t *source_id)
{
    struct ble_svc_audio_bass_receiver_state_add_params bass_params = {0};

    if (params == NULL) {
        BLE_HS_LOG_ERROR("NULL params\n");
        return BLE_HS_EINVAL;
    }

    if (source_id == NULL) {
        BLE_HS_LOG_ERROR("NULL source_id\n");
        return BLE_HS_EINVAL;
    }

    bass_params.source_addr = params->source_desc.addr;
    bass_params.source_adv_sid = params->source_desc.adv_sid;
    bass_params.broadcast_id = params->source_desc.broadcast_id;
    bass_params.pa_sync_state = (uint8_t)params->state.pa_sync_state;
    bass_params.big_encryption = (uint8_t)params->state.big_enc;

    if (params->state.big_enc == BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_BROADCAST_CODE_INVALID) {
        bass_params.bad_code = params->state.bad_code;
    } else {
        bass_params.bad_code = NULL;
    }

    bass_params.num_subgroups = params->state.num_subgroups;
    if (bass_params.num_subgroups > BLE_SVC_AUDIO_BASS_SUB_NUM_MAX) {
        BLE_HS_LOG_ERROR("num_subgroups above the limit\n");
        return BLE_HS_ENOMEM;
    }

    for (uint8_t i = 0; i < bass_params.num_subgroups; i++) {
        bass_params.subgroups[i].bis_sync_state = params->state.subgroups->bis_sync;
        bass_params.subgroups[i].metadata_length = params->state.subgroups->metadata_length;
        bass_params.subgroups[i].metadata = params->state.subgroups->metadata;
    }

    return ble_svc_audio_bass_receive_state_add(&bass_params, source_id);
}

int
ble_audio_scan_delegator_receive_state_remove(uint8_t source_id)
{
    int rc;

#if MYNEWT_VAL(BLE_AUDIO_BROADCAST_SINK)
    rc = ble_audio_broadcast_sink_stop(source_id);
    if (rc != 0) {
        BLE_HS_LOG_WARN("sink stop failed (%d)\n", rc);
    }
#endif /* BLE_AUDIO_BROADCAST_SINK */

    return ble_svc_audio_bass_receive_state_remove(source_id);
}

int
ble_audio_scan_delegator_receive_state_set(uint8_t source_id,
                                           const struct ble_audio_scan_delegator_receive_state *state)
{
    struct ble_svc_audio_bass_update_params bass_params;
    int rc;

    if (state == NULL) {
        BLE_HS_LOG_ERROR("NULL state\n");
        return BLE_HS_EINVAL;
    }

    bass_params.pa_sync_state = (uint8_t)state->pa_sync_state;
    bass_params.big_encryption = (uint8_t)state->big_enc;

    if (state->big_enc == BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_BROADCAST_CODE_INVALID) {
        bass_params.bad_code = state->bad_code;
    } else {
        bass_params.bad_code = NULL;
    }

    bass_params.num_subgroups = state->num_subgroups;
    if (bass_params.num_subgroups > BLE_SVC_AUDIO_BASS_SUB_NUM_MAX) {
        BLE_HS_LOG_ERROR("num_subgroups above the limit\n");
        return BLE_HS_ENOMEM;
    }

    for (uint8_t i = 0; i < bass_params.num_subgroups; i++) {
        bass_params.subgroups[i].bis_sync_state = state->subgroups[i].bis_sync;
        bass_params.subgroups[i].metadata_length = state->subgroups[i].metadata_length;
        bass_params.subgroups[i].metadata = state->subgroups[i].metadata;
    }

    rc = ble_svc_audio_bass_receive_state_update(&bass_params, source_id);
    if (rc != 0) {
        BLE_HS_LOG_ERROR("Failed to update receive state (rc %d)\n", rc);
    }

    return 0;
}

void
ble_audio_scan_delegator_receive_state_foreach(ble_audio_scan_delegator_receive_state_foreach_fn *fn, void *arg)
{
    struct ble_audio_scan_delegator_receive_state_entry entry;
    int rc;

    if (fn == NULL) {
        BLE_HS_LOG_ERROR("callback is NULL\n");
        return;
    }

    for (int i = 0; i < MYNEWT_VAL(BLE_AUDIO_SCAN_DELEGATOR_RECEIVE_STATE_MAX); i++) {
        struct ble_svc_audio_bass_receiver_state *state;
        uint8_t source_id;

        rc = ble_svc_audio_bass_source_id_get(i, &source_id);
        if (rc != 0) {
            continue;
        }

        rc = ble_svc_audio_bass_receiver_state_get(source_id, &state);
        if (rc != 0) {
            BLE_HS_LOG_ERROR("Failed to get receiver state (rc %d)\n", rc);
            continue;
        }

        entry.source_id = source_id;
        source_desc_init(&entry.source_desc, &state->source_addr, state->source_adv_sid, state->source_adv_sid);
        entry.state.pa_sync_state = (uint8_t)state->pa_sync_state;
        entry.state.big_enc = (uint8_t)state->big_encryption;
        if (entry.state.big_enc == BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_BROADCAST_CODE_INVALID) {
            memcpy(entry.state.bad_code, state->bad_code, sizeof(entry.state.bad_code));
        }
        entry.state.num_subgroups = state->num_subgroups;
        subgroups_init(entry.state.subgroups, state->subgroups, state->num_subgroups);

        if (fn(&entry, arg) != 0) {
            break;
        }
    }
}

#if MYNEWT_VAL(BLE_AUDIO_BROADCAST_SINK)
static int
audio_event_handler(struct ble_audio_event *event, void *arg)
{
    if (event->type == BLE_AUDIO_EVENT_BASS_BROADCAST_CODE_SET) {
        ble_audio_broadcast_sink_code_set(event->bass_set_broadcast_code.source_id,
                                          event->bass_set_broadcast_code.broadcast_code);
    }

    return 0;
}
#endif /* BLE_AUDIO_BROADCAST_SINK */

int
ble_audio_scan_delegator_init(void)
{
    int rc;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

#if MYNEWT_VAL(BLE_AUDIO_BROADCAST_SINK)
    static struct ble_audio_event_listener listener;

    rc = ble_audio_event_listener_register(&listener, audio_event_handler, NULL);
    SYSINIT_PANIC_ASSERT(rc == 0);
#endif /* BLE_AUDIO_BROADCAST_SINK */

    return rc;
}
#endif /* BLE_AUDIO_SCAN_DELEGATOR */
