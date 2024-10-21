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

#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "../../host/src/ble_gatt_priv.h"
#include "../../host/src/ble_att_priv.h"
#include "services/bass/ble_audio_svc_bass.h"
#include "../../../src/ble_audio_priv.h"

#define BLE_SVC_AUDIO_BASS_CHR_LEN_UNLIMITED                    (-1)
#define BLE_SVC_AUDIO_BASS_RECEIVE_STATE_SRC_ID_NONE            0xFF

const ble_uuid_t * bass_receive_state_uuid =
    BLE_UUID16_DECLARE(BLE_SVC_AUDIO_BASS_CHR_UUID16_BROADCAST_RECEIVE_STATE);
const ble_uuid_t * bass_cp_uuid =
    BLE_UUID16_DECLARE(BLE_SVC_AUDIO_BASS_CHR_UUID16_BASS_CP);

enum ble_svc_audio_bass_ctrl_point_op_code {
    BLE_AUDIO_SVC_BASS_REMOTE_SCAN_STOPPED,
    BLE_AUDIO_SVC_BASS_REMOTE_SCAN_STARTED,
    BLE_AUDIO_SVC_BASS_ADD_SOURCE,
    BLE_AUDIO_SVC_BASS_MODIFY_SOURCE,
    BLE_AUDIO_SVC_BASS_SET_BROADCAST_CODE,
    BLE_AUDIO_SVC_BASS_REMOVE_SOURCE
};

typedef int ble_svc_audio_bass_ctrl_point_handler_cb(uint8_t *data,
                                                     uint16_t data_len,
                                                     uint16_t conn_handle);

static struct ble_svc_audio_bass_ctrl_point_ev {
    ble_svc_audio_bass_accept_fn *ctrl_point_ev_fn;
    void *arg;
} accept_fn;

struct ble_svc_audio_bass_rcv_state_entry {
    uint8_t source_id;
    uint16_t chr_val;
    struct ble_svc_audio_bass_receiver_state state;
};

static struct ble_svc_audio_bass_rcv_state_entry
    receiver_states[MYNEWT_VAL(BLE_SVC_AUDIO_BASS_RECEIVE_STATE_MAX)] = {
    [0 ... MYNEWT_VAL(BLE_SVC_AUDIO_BASS_RECEIVE_STATE_MAX) - 1] = {
        .source_id = BLE_SVC_AUDIO_BASS_RECEIVE_STATE_SRC_ID_NONE
    }
};

static struct os_mempool ble_audio_svc_bass_metadata_pool;
static os_membuf_t ble_audio_svc_bass_metadata_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_SVC_AUDIO_BASS_RECEIVE_STATE_MAX) *
                    BLE_SVC_AUDIO_BASS_SUB_NUM_MAX,
                    MYNEWT_VAL(BLE_SVC_AUDIO_BASS_METADATA_MAX_SZ))];

static int
ble_svc_audio_bass_remote_scan_stopped(uint8_t *data, uint16_t data_len, uint16_t conn_handle);
static int
ble_svc_audio_bass_remote_scan_started(uint8_t *data, uint16_t data_len, uint16_t conn_handle);
static int
ble_svc_audio_bass_add_source(uint8_t *data, uint16_t data_len, uint16_t conn_handle);
static int
ble_svc_audio_bass_modify_source(uint8_t *data, uint16_t data_len, uint16_t conn_handle);
static int
ble_svc_audio_bass_set_broadcast_code(uint8_t *data, uint16_t data_len, uint16_t conn_handle);
static int
ble_svc_audio_bass_remove_source(uint8_t *data, uint16_t data_len, uint16_t conn_handle);

static int
ble_svc_audio_bass_ctrl_point_write_access(struct ble_gatt_access_ctxt *ctxt, uint16_t conn_handle);
static int
ble_svc_audio_bass_rcv_state_read_access(struct ble_gatt_access_ctxt *ctxt, void *arg);

static struct ble_svc_audio_bass_ctrl_point_handler {
    uint8_t op_code;
    uint8_t length_min;
    uint8_t length_max;
    ble_svc_audio_bass_ctrl_point_handler_cb *handler_cb;
} ble_svc_audio_bass_ctrl_point_handlers[] = {
    {
        .op_code = BLE_AUDIO_SVC_BASS_REMOTE_SCAN_STOPPED,
        .length_min = 0,
        .length_max = 0,
        .handler_cb = ble_svc_audio_bass_remote_scan_stopped
    }, {
        .op_code = BLE_AUDIO_SVC_BASS_REMOTE_SCAN_STARTED,
        .length_min = 0,
        .length_max = 0,
        .handler_cb = ble_svc_audio_bass_remote_scan_started
    }, {
        .op_code = BLE_AUDIO_SVC_BASS_ADD_SOURCE,
        .length_min = 15,
        .length_max = BLE_SVC_AUDIO_BASS_CHR_LEN_UNLIMITED,
        .handler_cb = ble_svc_audio_bass_add_source
    }, {
        .op_code = BLE_AUDIO_SVC_BASS_MODIFY_SOURCE,
        .length_min = 5,
        .length_max = BLE_SVC_AUDIO_BASS_CHR_LEN_UNLIMITED,
        .handler_cb = ble_svc_audio_bass_modify_source
    }, {
        .op_code = BLE_AUDIO_SVC_BASS_SET_BROADCAST_CODE,
        .length_min = 17,
        .length_max = 17,
        .handler_cb = ble_svc_audio_bass_set_broadcast_code
    }, {
        .op_code = BLE_AUDIO_SVC_BASS_REMOVE_SOURCE,
        .length_min = 1,
        .length_max = 1,
        .handler_cb = ble_svc_audio_bass_remove_source
    }
};

static struct ble_gatt_chr_def ble_svc_audio_bass_chrs[MYNEWT_VAL(BLE_SVC_AUDIO_BASS_RECEIVE_STATE_MAX) + 2];

static const struct ble_gatt_svc_def ble_svc_audio_bass_defs[MYNEWT_VAL(BLE_SVC_AUDIO_BASS_RECEIVE_STATE_MAX) + 2] = {
    { /*** Service: Published Audio Capabilities Service (bass) */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_AUDIO_BASS_UUID16),
        .characteristics = ble_svc_audio_bass_chrs,
    },
    {
        0, /* No more services. */
    },
};

static uint8_t free_source_id = 0;

static int
ble_svc_audio_bass_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);
    int rc;

    switch (uuid16) {
    case BLE_SVC_AUDIO_BASS_CHR_UUID16_BASS_CP:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            rc = ble_svc_audio_bass_ctrl_point_write_access(ctxt, conn_handle);
        } else {
            assert(0);
        }
        return rc;
    case BLE_SVC_AUDIO_BASS_CHR_UUID16_BROADCAST_RECEIVE_STATE:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = ble_svc_audio_bass_rcv_state_read_access(ctxt, arg);
        } else {
            assert(0);
        }
        return rc;
    default:
        assert(0);
    }
}

static bool
ble_svc_audio_bass_source_id_free(uint8_t source_id)
{
    int i;
    for (i = 0; i < MYNEWT_VAL(BLE_SVC_AUDIO_BASS_RECEIVE_STATE_MAX); i++) {
        if (receiver_states[i].source_id == source_id) {
            return false;
        }
    }

    return true;
};

static uint8_t
ble_svc_audio_bass_get_new_source_id(void)
{
    /** Wrap around after all Source IDs were used */
    if (free_source_id == BLE_SVC_AUDIO_BASS_RECEIVE_STATE_SRC_ID_NONE) {
        free_source_id = 0;
    }

    while (!ble_svc_audio_bass_source_id_free(free_source_id)) {
        free_source_id++;
    }

    return free_source_id;
}

static int
ble_svc_audio_bass_receive_state_notify(struct ble_svc_audio_bass_rcv_state_entry *state)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(ble_svc_audio_bass_chrs); i++) {
        if (ble_svc_audio_bass_chrs[i].arg == state) {
            ble_gatts_chr_updated(*ble_svc_audio_bass_chrs[i].val_handle);
            return 0;
        }
    }

    return BLE_HS_ENOENT;
}

static int
ble_svc_audio_bass_receive_state_find_by_source_id(struct ble_svc_audio_bass_rcv_state_entry **out_state,
                                                   uint8_t source_id)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(receiver_states); i++) {
        if (receiver_states[i].source_id == source_id) {
            *out_state = &receiver_states[i];
            return 0;
        }
    }

    return BLE_HS_ENOMEM;
}

int
ble_svc_audio_bass_receive_state_find_free(struct ble_svc_audio_bass_rcv_state_entry **out_state)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(receiver_states); i++) {
        if (receiver_states[i].source_id == BLE_SVC_AUDIO_BASS_RECEIVE_STATE_SRC_ID_NONE) {
            *out_state = &receiver_states[i];
            return 0;
        }
    }

    return BLE_HS_ENOMEM;
}

static void
ble_svc_audio_bass_receive_state_free(struct ble_svc_audio_bass_rcv_state_entry *state)
{
    state->source_id = BLE_SVC_AUDIO_BASS_RECEIVE_STATE_SRC_ID_NONE;
}

static int
ble_svc_audio_bass_remote_scan_stopped(uint8_t *data, uint16_t data_len, uint16_t conn_handle)
{
    if (data_len > 1) {
        return BLE_ATT_ERR_WRITE_REQ_REJECTED;
    }

    struct ble_audio_event ev = {
        .type = BLE_AUDIO_EVENT_BASS_REMOTE_SCAN_STOPPED
    };

    ev.remote_scan_stopped.conn_handle = conn_handle;
    ble_audio_event_listener_call(&ev);

    return 0;
}

static int
ble_svc_audio_bass_remote_scan_started(uint8_t *data, uint16_t data_len, uint16_t conn_handle)
{
    if (data_len > 1) {
        return BLE_ATT_ERR_WRITE_REQ_REJECTED;
    }

    struct ble_audio_event ev = {
        .type = BLE_AUDIO_EVENT_BASS_REMOTE_SCAN_STARTED
    };

    ev.remote_scan_started.conn_handle = conn_handle;
    ble_audio_event_listener_call(&ev);

    return 0;
}

static int
ble_svc_audio_bass_add_source(uint8_t *data, uint16_t data_len, uint16_t conn_handle)
{
    struct ble_audio_event ev = {
        .type = BLE_AUDIO_EVENT_BASS_OPERATION_STATUS,
        .bass_operation_status = {
            .op = BLE_AUDIO_EVENT_BASS_SOURCE_ADDED,
            .status = 0
        }
    };
    struct ble_svc_audio_bass_operation operation;
    struct ble_svc_audio_bass_rcv_state_entry *rcv_state = NULL;
    uint8_t offset = 0;
    uint8_t *metadata_ptr;
    uint8_t source_id_new;
    uint8_t source_id_to_remove = BLE_SVC_AUDIO_BASS_RECEIVE_STATE_SRC_ID_NONE;
    int rc = 0;
    int i;

    memset(&operation, 0, sizeof(operation));

    operation.op = BLE_SVC_AUDIO_BASS_OPERATION_ADD_SOURCE;
    operation.conn_handle = conn_handle;

    operation.add_source.adv_addr.type = data[offset++];
    memcpy(operation.add_source.adv_addr.val, &data[offset], 6);
    offset += 6;
    operation.add_source.adv_sid = data[offset++];
    operation.add_source.broadcast_id = get_le24(&data[offset]);
    offset += 3;
    operation.add_source.pa_sync = data[offset++];
    if (operation.add_source.pa_sync >= BLE_SVC_AUDIO_BASS_PA_SYNC_RFU) {
        rc = BLE_HS_EINVAL;
        ev.bass_operation_status.status = BLE_HS_EINVAL;
        goto done;
    }

    operation.add_source.pa_interval = get_le16(&data[offset]);
    offset += 2;
    operation.add_source.num_subgroups = data[offset++];

    /**
     * Previous data was checked for it's size in `ble_svc_audio_bass_ctrl_point_write_access`.
     * As bis_sync_state array may be of variable length, we need to check it separately
     */
    data_len -= offset;
    for (i = 0; i < operation.add_source.num_subgroups; i++) {
        if (data_len < sizeof(uint32_t)) {
            rc = BLE_ATT_ERR_WRITE_REQ_REJECTED;
            ev.bass_operation_status.status = BLE_HS_EREJECT;
            goto done;
        }
        operation.add_source.subgroups[i].bis_sync_state = get_le32(&data[offset]);
        offset += 4;
        operation.add_source.subgroups[i].metadata_length = data[offset++];
        data_len -= 5;
        if (data_len < operation.add_source.subgroups[i].metadata_length) {
            rc = BLE_ATT_ERR_WRITE_REQ_REJECTED;
            ev.bass_operation_status.status = BLE_HS_EREJECT;
            goto done;
        }
        operation.add_source.subgroups[i].metadata = &data[offset];
        offset += operation.add_source.subgroups[i].metadata_length;
        data_len -= operation.add_source.subgroups[i].metadata_length;
    }

    source_id_new = ble_svc_audio_bass_get_new_source_id();
    operation.add_source.source_id = source_id_new;

    ble_svc_audio_bass_receive_state_find_free(&rcv_state);
    if (rcv_state == NULL) {
        operation.add_source.out_source_id_to_swap = &source_id_to_remove;
    } else {
        operation.add_source.out_source_id_to_swap = NULL;
        rcv_state->source_id = operation.add_source.source_id;
    }

    if (accept_fn.ctrl_point_ev_fn) {
        rc = accept_fn.ctrl_point_ev_fn(&operation, accept_fn.arg);
        if (rc != 0) {
            if (rcv_state != NULL) {
                ble_svc_audio_bass_receive_state_free(rcv_state);
            }
            rc = BLE_ATT_ERR_WRITE_REQ_REJECTED;
            ev.bass_operation_status.status = BLE_HS_EREJECT;
            goto done;
        }
    }

    if (rcv_state == NULL) {
        if (source_id_to_remove != BLE_SVC_AUDIO_BASS_RECEIVE_STATE_SRC_ID_NONE) {
            ble_svc_audio_bass_receive_state_find_by_source_id(&rcv_state, source_id_to_remove);
            if (rcv_state == NULL) {
                rc = BLE_HS_EAPP;
                ev.bass_operation_status.status = BLE_HS_EAPP;
                goto done;
            }

            /* Swap Source ID */
            rcv_state->source_id = operation.add_source.source_id;
        } else {
            rc = BLE_HS_ENOMEM;
            ev.bass_operation_status.status = BLE_HS_ENOMEM;
            goto done;
        }
    } else {
        rcv_state->source_id = operation.add_source.source_id;
    }

    ev.bass_operation_status.source_id = rcv_state->source_id;
    rcv_state->state.source_addr.type = operation.add_source.adv_addr.type;
    memcpy(&rcv_state->state.source_addr.type, operation.add_source.adv_addr.val, 6);
    rcv_state->state.source_adv_sid = operation.add_source.adv_sid;
    rcv_state->state.broadcast_id = operation.add_source.broadcast_id;

    for (i = 0; i < operation.add_source.num_subgroups; i++) {
        metadata_ptr = os_memblock_get(&ble_audio_svc_bass_metadata_pool);
        if (!metadata_ptr) {
            rc = BLE_HS_ENOMEM;
            ev.bass_operation_status.status = BLE_HS_ENOMEM;
            goto done;
        }
        rcv_state->state.subgroups[i].metadata_length = operation.add_source.subgroups[i].metadata_length;
        memcpy(metadata_ptr, operation.add_source.subgroups[i].metadata,
               min(operation.add_source.subgroups[i].metadata_length,
                   MYNEWT_VAL(BLE_SVC_AUDIO_BASS_METADATA_MAX_SZ)));

        rcv_state->state.subgroups[i].metadata = metadata_ptr;
    }

done:
    if (!rc) {
        rc = ble_svc_audio_bass_receive_state_notify(rcv_state);
        ev.bass_operation_status.status = rc;
    }

    ble_audio_event_listener_call(&ev);

    return rc;
}

static int
check_bis_sync(uint16_t num_subgroups, struct ble_svc_audio_bass_subgroup *subgroups)
{
    uint32_t bis_sync_mask = 0;
    int i;
    int j;

    for (i = 0; i < num_subgroups; i++) {
        if (subgroups[i].bis_sync != 0xFFFFFFFF) {
            for (j = 0; j < num_subgroups; j++) {
                if (subgroups[i].bis_sync & bis_sync_mask) {
                    return BLE_HS_EINVAL;
                }

                bis_sync_mask |= subgroups[i].bis_sync;
            }
        }
    }

    return 0;
}

static int
ble_svc_audio_bass_modify_source(uint8_t *data, uint16_t data_len, uint16_t conn_handle)
{
    struct ble_svc_audio_bass_operation operation;
    struct ble_svc_audio_bass_rcv_state_entry *rcv_state = NULL;
    struct ble_audio_event ev = {
        .type = BLE_AUDIO_EVENT_BASS_OPERATION_STATUS,
        .bass_operation_status = {
            .op = BLE_AUDIO_EVENT_BASS_SOURCE_MODIFIED,
            .status = 0
        }
    };
    uint8_t offset = 0;
    int rc = 0;
    int i;

    memset(&operation, 0, sizeof(operation));

    operation.op = BLE_SVC_AUDIO_BASS_OPERATION_MODIFY_SOURCE;
    operation.conn_handle = conn_handle;

    operation.modify_source.source_id = data[offset++];

    ble_svc_audio_bass_receive_state_find_by_source_id(&rcv_state,
                                                       operation.modify_source.source_id);
    if (rcv_state == NULL) {
        rc = BLE_SVC_AUDIO_BASS_ERR_INVALID_SOURCE_ID;
        ev.bass_operation_status.status = BLE_HS_EINVAL;
        goto done;
    }

    operation.modify_source.pa_sync = data[offset++];
    if (operation.modify_source.pa_sync >= BLE_SVC_AUDIO_BASS_PA_SYNC_RFU) {
        rc = BLE_HS_EINVAL;
        ev.bass_operation_status.status = BLE_HS_EREJECT;
        goto done;
    }

    operation.modify_source.pa_interval = get_le16(&data[offset]);
    offset += 2;
    operation.modify_source.num_subgroups = get_le16(&data[offset]);
    offset += 2;

    data_len -= offset;
    if (data_len < operation.modify_source.num_subgroups * sizeof(uint32_t)) {
        rc = BLE_ATT_ERR_WRITE_REQ_REJECTED;
        ev.bass_operation_status.status = BLE_HS_EREJECT;
        goto done;
    }

    for (i = 0; i < operation.modify_source.num_subgroups; i++) {
        operation.modify_source.subgroups.bis_sync[i] = get_le32(&data[offset]);
        offset += 4;
    }

    if (check_bis_sync(operation.modify_source.num_subgroups,
                       operation.modify_source.subgroups)) {
        rc = BLE_HS_EINVAL;
        ev.bass_operation_status.status = BLE_HS_EREJECT;
        goto done;
    }

    if (accept_fn.ctrl_point_ev_fn) {
        rc = accept_fn.ctrl_point_ev_fn(&operation, accept_fn.arg);
        if (rc != 0) {
            rc = BLE_ATT_ERR_WRITE_REQ_REJECTED;
            ev.bass_operation_status.status = BLE_HS_EREJECT;
            goto done;
        }
    }

    ev.bass_operation_status.source_id = operation.modify_source.source_id;

done:
    if (!rc) {
        rc = ble_svc_audio_bass_receive_state_notify(rcv_state);
        ev.bass_operation_status.status = rc;
        goto done;
    }

    ble_audio_event_listener_call(&ev);

    return rc;
}

static int
ble_svc_audio_bass_set_broadcast_code(uint8_t *data, uint16_t data_len, uint16_t conn_handle)
{
    struct ble_svc_audio_bass_rcv_state_entry *rcv_state = NULL;
    struct ble_audio_event ev = {
        .type = BLE_AUDIO_EVENT_BASS_BROADCAST_CODE_SET,
    };

    ev.bass_set_broadcast_code.source_id = data[0];

    ble_svc_audio_bass_receive_state_find_by_source_id(&rcv_state,
                                                       ev.bass_set_broadcast_code.source_id);
    if (rcv_state == NULL) {
        return BLE_SVC_AUDIO_BASS_ERR_INVALID_SOURCE_ID;
    }

    memcpy(ev.bass_set_broadcast_code.broadcast_code, &data[1], BLE_AUDIO_BROADCAST_CODE_SIZE);

    ble_audio_event_listener_call(&ev);

    return 0;
}

static int
ble_svc_audio_bass_remove_source(uint8_t *data, uint16_t data_len, uint16_t conn_handle)
{
    struct ble_audio_event ev = {
        .type = BLE_AUDIO_EVENT_BASS_OPERATION_STATUS,
        .bass_operation_status = {
            .op = BLE_AUDIO_EVENT_BASS_SOURCE_REMOVED,
            .status = 0
        }
    };
    struct ble_svc_audio_bass_rcv_state_entry *rcv_state = NULL;
    struct ble_svc_audio_bass_operation operation;
    uint16_t chr_val;
    int rc = 0;
    int i;

    ev.bass_set_broadcast_code.source_id = data[0];
    operation.op = BLE_SVC_AUDIO_BASS_OPERATION_REMOVE_SOURCE;

    ble_svc_audio_bass_receive_state_find_by_source_id(&rcv_state,
                                                       ev.bass_operation_status.source_id);
    if (rcv_state == NULL) {
        rc = BLE_SVC_AUDIO_BASS_ERR_INVALID_SOURCE_ID;
        ev.bass_operation_status.status = BLE_HS_ENOENT;
        goto done;
    }

    operation.remove_source.source_id = ev.bass_operation_status.source_id;
    operation.conn_handle = conn_handle;
    if (accept_fn.ctrl_point_ev_fn) {
        rc = accept_fn.ctrl_point_ev_fn(&operation, accept_fn.arg);
        if (rc != 0) {
            rc = BLE_HS_EREJECT;
            ev.bass_operation_status.status = BLE_HS_EREJECT;
            goto done;
        }
    }

    for (i = 0; i < rcv_state->state.num_subgroups; i++) {
        os_memblock_put(&ble_audio_svc_bass_metadata_pool, rcv_state->state.subgroups[i].metadata);
    }

    memset(rcv_state, 0, sizeof(*rcv_state));
    rcv_state->source_id = BLE_SVC_AUDIO_BASS_RECEIVE_STATE_SRC_ID_NONE;
    rcv_state->chr_val = chr_val;

done:
    if (!rc) {
        rc = ble_svc_audio_bass_receive_state_notify(rcv_state);
        ev.bass_operation_status.status = rc;
    }

    ble_audio_event_listener_call(&ev);

    return rc;
}

static struct ble_svc_audio_bass_ctrl_point_handler *
ble_svc_audio_bass_find_handler(uint8_t opcode)
{
    int i;

    for (i = 0; i < sizeof(ble_svc_audio_bass_ctrl_point_handlers); i++) {
        if (ble_svc_audio_bass_ctrl_point_handlers[i].op_code == opcode) {
            return &ble_svc_audio_bass_ctrl_point_handlers[i];
        }
    }

    return NULL;
}

static int
ble_svc_audio_bass_ctrl_point_write_access(struct ble_gatt_access_ctxt *ctxt, uint16_t conn_handle)
{
    uint8_t opcode;
    struct os_mbuf *om;
    uint16_t mbuf_len = OS_MBUF_PKTLEN(ctxt->om);
    struct ble_svc_audio_bass_ctrl_point_handler *handler;

    opcode = ctxt->om->om_data[0];

    handler = ble_svc_audio_bass_find_handler(opcode);

    if (handler == NULL) {
        return BLE_SVC_AUDIO_BASS_ERR_OPCODE_NOT_SUPPORTED;
    }

    if (ctxt->om->om_len - 1 < handler->length_min &&
        handler->length_max >= 0 ?
        ctxt->om->om_len > handler->length_max : 0) {
        return BLE_ATT_ERR_WRITE_REQ_REJECTED;
    }

    ctxt->om = os_mbuf_pullup(ctxt->om, mbuf_len);
    if (!ctxt->om) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return handler->handler_cb(&om->om_data[1], mbuf_len - 1, conn_handle);
}

static int
ble_svc_audio_bass_rcv_state_read_access(struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    struct ble_svc_audio_bass_rcv_state_entry *state = arg;
    uint8_t *buf;
    int i;

    /* Nothing set, return empty buffer */
    if (state->source_id == BLE_SVC_AUDIO_BASS_RECEIVE_STATE_SRC_ID_NONE) {
        return 0;
    }

    os_mbuf_append(ctxt->om, &state->source_id, 1);
    os_mbuf_append(ctxt->om, &state->state.source_addr.type, 1);
    os_mbuf_append(ctxt->om, &state->state.source_addr.val, 6);
    os_mbuf_append(ctxt->om, &state->state.source_adv_sid, 1);
    buf = os_mbuf_extend(ctxt->om, 3);
    if (buf == NULL) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    put_le24(buf, state->state.broadcast_id);
    os_mbuf_append(ctxt->om, &state->state.pa_sync_state, 1);
    os_mbuf_append(ctxt->om, &state->state.big_encryption, 1);

    if (state->state.big_encryption == BLE_SVC_AUDIO_BASS_BIG_ENC_BAD_CODE) {
        os_mbuf_append(ctxt->om, &state->state.bad_code, BLE_AUDIO_BROADCAST_CODE_SIZE);
    }

    os_mbuf_append(ctxt->om, &state->state.num_subgroups, 1);

    for (i = 0; i < state->state.num_subgroups; i++) {
        buf = os_mbuf_extend(ctxt->om, 4);
        if (buf == NULL) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        put_le32(buf, state->state.subgroups[i].bis_sync_state);
        os_mbuf_append(ctxt->om, &state->state.subgroups[i].metadata_length, 1);
        os_mbuf_append(ctxt->om, state->state.subgroups[i].metadata,
                       state->state.subgroups[i].metadata_length);
    }

    return 0;
}

int
ble_svc_audio_bass_accept_fn_set(ble_svc_audio_bass_accept_fn *fn, void *arg)
{
    if (accept_fn.ctrl_point_ev_fn) {
        return BLE_HS_EALREADY;
    }

    accept_fn.ctrl_point_ev_fn = fn;
    accept_fn.arg = arg;
    return 0;
}

int
ble_svc_audio_bass_receive_state_add(const struct ble_svc_audio_bass_receiver_state_add_params *params,
                                     uint8_t *source_id)
{
    struct ble_svc_audio_bass_rcv_state_entry *rcv_state;
    int i;
    int rc;

    rc = ble_svc_audio_bass_receive_state_find_free(&rcv_state);
    if (rc) {
        return rc;
    }

    rcv_state->source_id = ble_svc_audio_bass_get_new_source_id();
    rcv_state->state.source_addr = params->source_addr;
    rcv_state->state.source_adv_sid = params->source_adv_sid;
    rcv_state->state.broadcast_id = params->broadcast_id;
    rcv_state->state.pa_sync_state = params->pa_sync_state;
    rcv_state->state.big_encryption = params->big_encryption;
    if (rcv_state->state.big_encryption ==
        BLE_SVC_AUDIO_BASS_BIG_ENC_BAD_CODE) {
        memcpy(&rcv_state->state.bad_code, params->bad_code,
               BLE_AUDIO_BROADCAST_CODE_SIZE);
    }
    rcv_state->state.num_subgroups = params->num_subgroups;

    for (i = 0; i < rcv_state->state.num_subgroups; i++) {
        rcv_state->state.subgroups[i].metadata =
            os_memblock_get(&ble_audio_svc_bass_metadata_pool);

        if (!rcv_state->state.subgroups[i].metadata) {
            return 0;
        }

        rcv_state->state.subgroups[i].metadata_length =
            min(params->subgroups[i].metadata_length,
                ble_audio_svc_bass_metadata_pool.mp_block_size);
        memcpy(rcv_state->state.subgroups[i].metadata, params->subgroups[i].metadata,
               rcv_state->state.subgroups[i].metadata_length);
    }

    *source_id = rcv_state->source_id;

    return ble_svc_audio_bass_receive_state_notify(rcv_state);
}

int
ble_svc_audio_bass_receive_state_remove(uint8_t source_id)
{
    struct ble_svc_audio_bass_rcv_state_entry *rcv_state = NULL;
    int rc, i;

    rc = ble_svc_audio_bass_receive_state_find_by_source_id(&rcv_state, source_id);
    if (rc) {
        return rc;
    }

    memset(&rcv_state->state, 0, sizeof(rcv_state->state));
    rcv_state->source_id = BLE_SVC_AUDIO_BASS_RECEIVE_STATE_SRC_ID_NONE;

    for (i = 0; i < rcv_state->state.num_subgroups; i++) {
        os_memblock_put(&ble_audio_svc_bass_metadata_pool, rcv_state->state.subgroups[i].metadata);
    }

    return ble_svc_audio_bass_receive_state_notify(rcv_state);
}

int
ble_svc_audio_bass_update_metadata(const struct ble_svc_audio_bass_metadata_params *params,
                                   uint8_t source_id)
{
    struct ble_svc_audio_bass_rcv_state_entry *rcv_state = NULL;
    int rc;

    rc = ble_svc_audio_bass_receive_state_find_by_source_id(&rcv_state, source_id);
    if (rc) {
        return rc;
    }

    rcv_state->state.subgroups[params->subgroup_idx].metadata_length = params->metadata_length;
    memcpy(rcv_state->state.subgroups[params->subgroup_idx].metadata,
           params->metadata, params->metadata_length);

    return ble_svc_audio_bass_receive_state_notify(rcv_state);
}

int
ble_svc_audio_bass_receive_state_update(const struct
                                        ble_svc_audio_bass_update_params *params,
                                        uint8_t source_id)
{
    struct ble_svc_audio_bass_rcv_state_entry *rcv_state = NULL;
    int rc;

    rc = ble_svc_audio_bass_receive_state_find_by_source_id(&rcv_state,
                                                            source_id);
    if (rc) {
        return rc;
    }

    if (rcv_state->state.num_subgroups < params->num_subgroups) {
        return BLE_HS_ENOMEM;
    }

    rcv_state->state.pa_sync_state = params->pa_sync_state;
    rcv_state->state.big_encryption = params->big_encryption;
    if (params->bad_code) {
        memcpy(rcv_state->state.bad_code,
               params->bad_code,
               BLE_AUDIO_BROADCAST_CODE_SIZE);
    }

    int i;
    for (i = 0; i < params->num_subgroups; i++) {
        rcv_state->state.subgroups[i].bis_sync_state =
            params->subgroups[i].bis_sync_state;
        memcpy(rcv_state->state.subgroups[i].metadata,
               params->subgroups[i].metadata,
               params->subgroups[i].metadata_length);
    }

    return ble_svc_audio_bass_receive_state_notify(rcv_state);
}

int
ble_svc_audio_bass_receiver_state_get(uint8_t source_id,
                                      struct ble_svc_audio_bass_receiver_state **state)
{
    struct ble_svc_audio_bass_rcv_state_entry *rcv_state = NULL;

    ble_svc_audio_bass_receive_state_find_by_source_id(&rcv_state, source_id);

    if (!rcv_state) {
        return BLE_HS_ENOENT;
    }

    *state = &rcv_state->state;

    return 0;
}

int
ble_svc_audio_bass_source_id_get(uint8_t index, uint8_t *source_id)
{
    if (index >= ARRAY_SIZE(receiver_states) ||
        receiver_states[index].source_id ==
        BLE_SVC_AUDIO_BASS_RECEIVE_STATE_SRC_ID_NONE) {
        return BLE_HS_ENOENT;
    }

    *source_id = receiver_states[index].source_id;

    return 0;
}

void
ble_svc_audio_bass_init(void)
{
    int rc;
    int i;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    ble_svc_audio_bass_chrs[0].uuid = bass_cp_uuid;
    ble_svc_audio_bass_chrs[0].access_cb = ble_svc_audio_bass_access;
    ble_svc_audio_bass_chrs[0].flags = BLE_GATT_CHR_F_WRITE_NO_RSP |
                                       BLE_GATT_CHR_F_WRITE |
                                       BLE_GATT_CHR_F_WRITE_ENC;

    for (i = 1; i <= MYNEWT_VAL(BLE_SVC_AUDIO_BASS_RECEIVE_STATE_MAX); i++) {
        ble_svc_audio_bass_chrs[i].uuid = bass_receive_state_uuid;
        ble_svc_audio_bass_chrs[i].access_cb = ble_svc_audio_bass_access;
        ble_svc_audio_bass_chrs[i].arg = &receiver_states[i-1];
        ble_svc_audio_bass_chrs[i].val_handle = &receiver_states[i-1].chr_val;
        ble_svc_audio_bass_chrs[i].flags = BLE_GATT_CHR_F_READ |
                                           BLE_GATT_CHR_F_READ_ENC |
                                           BLE_GATT_CHR_F_NOTIFY;
    }

    rc = ble_gatts_count_cfg(ble_svc_audio_bass_defs);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gatts_add_svcs(ble_svc_audio_bass_defs);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = os_mempool_init(&ble_audio_svc_bass_metadata_pool,
                         MYNEWT_VAL(BLE_SVC_AUDIO_BASS_RECEIVE_STATE_MAX) *
                         BLE_SVC_AUDIO_BASS_SUB_NUM_MAX,
                         MYNEWT_VAL(BLE_SVC_AUDIO_BASS_METADATA_MAX_SZ),
                         ble_audio_svc_bass_metadata_mem, "ble_audio_svc_bass_metadata_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    (void)rc;
}
