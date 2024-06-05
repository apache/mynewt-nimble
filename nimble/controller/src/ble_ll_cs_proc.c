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

#include <syscfg/syscfg.h>
#if MYNEWT_VAL(BLE_LL_CHANNEL_SOUNDING)
#include <stdint.h>
#include "controller/ble_ll_utils.h"
#include "controller/ble_ll_conn.h"
#include "controller/ble_ll_sched.h"
#include "controller/ble_ll_tmr.h"
#include "controller/ble_ll_hci.h"
#include "ble_ll_cs_priv.h"

extern struct ble_ll_cs_supp_cap g_ble_ll_cs_local_cap;
extern uint8_t g_ble_ll_cs_chan_count;
extern uint8_t g_ble_ll_cs_chan_indices[72];
struct ble_ll_cs_sm *g_ble_ll_cs_sm_current;

#define SUBEVENT_STATE_MODE0_STEP      (0)
#define SUBEVENT_STATE_REPETITION_STEP (1)
#define SUBEVENT_STATE_SUBMODE_STEP    (2)
#define SUBEVENT_STATE_MAINMODE_STEP   (3)

#define CS_SCHEDULE_NEW_STEP      (0)
#define CS_SCHEDULE_NEW_SUBEVENT  (1)
#define CS_SCHEDULE_NEW_EVENT     (2)
#define CS_SCHEDULE_NEW_PROCEDURE (3)
#define CS_SCHEDULE_COMPLETED     (4)

#define SUBEVENT_DONE_STATUS_COMPLETED (0x0)
#define SUBEVENT_DONE_STATUS_PARTIAL   (0x1)
#define SUBEVENT_DONE_STATUS_ABORTED   (0xF)

#define PROC_DONE_STATUS_COMPLETED (0x0)
#define PROC_DONE_STATUS_PARTIAL   (0x1)
#define PROC_DONE_STATUS_ABORTED   (0xF)

#define PROC_ABORT_SUCCESS              (0x00)
#define PROC_ABORT_REQUESTED            (0x01)
#define PROC_ABORT_CHANNEL_MAP_CHANNELS (0x02)
#define PROC_ABORT_CHANNEL_MAP_UPDATE   (0x03)
#define PROC_ABORT_UNSPECIFIED          (0x0F)

#define SUBEVENT_ABORT_SUCCESS              (0x00)
#define SUBEVENT_ABORT_REQUESTED            (0x01)
#define SUBEVENT_ABORT_NO_CS_SYNC           (0x02)
#define SUBEVENT_ABORT_LIMITED_RESOURCES    (0x03)
#define SUBEVENT_ABORT_UNSPECIFIED          (0x0F)

#define TIME_DIFF_NOT_AVAILABLE  (0x00008000)

#define BLE_LL_CONN_ITVL_USECS    (1250)

/* The ramp-down window in µs */
#define T_RD (5)
/* The guard time duration in µs */
#define T_GD (10)
/* The requency measurement period in µs */
#define T_FM (80)

/* Complement to full byte (4 bits) + header (16 bits) + CRC (24 bits) */
#define BSIM_PACKET_OVERHEAD 4 + 16 + 24;

static struct ble_ll_cs_aci aci_table[] = {
    {1, 1, 1}, {2, 2, 1}, {3, 3, 1}, {4, 4, 1},
    {2, 1, 2}, {3, 1, 3}, {4, 1, 4}, {4, 2, 2}
};
static const uint8_t rtt_seq_len[] = {0, 4, 12, 4, 8, 12, 16};
static uint32_t tifs_table[6][6];

/* A pattern containing the states and transitions of the current step */
static struct ble_ll_cs_step_transmission transmission_pattern[100];
static struct ble_ll_cs_step subevent_steps[40];

/* For queueing the HCI Subevent Result (Continue) events */
static struct ble_npl_event subevent_pool[MYNEWT_VAL(BLE_LL_CHANNEL_SOUNDING_SUBEVENT_EV_MAX_CNT)];

void ble_ll_cs_sync_rx_end(struct ble_ll_cs_sm *cssm, uint8_t *rxbuf, struct ble_mbuf_hdr *rxhdr);
void ble_ll_cs_tone_rx_end_cb(struct ble_ll_cs_sm *cssm);

static int
ble_ll_cs_generate_channel(struct ble_ll_cs_sm *cssm, struct ble_ll_cs_step *step,
                           uint16_t steps_in_procedure_count)
{
    int rc;
    uint8_t *channel_array;
    uint8_t *next_channel_id;
    uint8_t transaction_id;

    if (step->mode == BLE_LL_CS_MODE0) {
        transaction_id = BLE_LL_CS_DRBG_HOP_CHAN_MODE0;
        channel_array = cssm->mode0_channels;
        next_channel_id = &cssm->mode0_next_chan_id;
    } else {
        transaction_id = BLE_LL_CS_DRBG_HOP_CHAN_NON_MODE0;
        channel_array = cssm->non_mode0_channels;
        next_channel_id = &cssm->non_mode0_next_chan_id;
    }

    if (g_ble_ll_cs_chan_count <= *next_channel_id) {
        rc = ble_ll_cs_drbg_shuffle_cr1(&cssm->drbg_ctx, steps_in_procedure_count,
                                        transaction_id, g_ble_ll_cs_chan_indices,
                                        channel_array, g_ble_ll_cs_chan_count);
        if (rc) {
            return rc;
        }

        *next_channel_id = 0;
    }

    step->channel = channel_array[(*next_channel_id)++];

    return 0;
}

static int
ble_ll_cs_backtracking_resistance(struct ble_ll_cs_sm *cssm)
{
    cssm->drbg_ctx.nonce_v[1] += BLE_LL_CS_DRBG_BACKTRACKING_RESISTANCE;

    return ble_ll_cs_drbg_f9(0, cssm->drbg_ctx.key, cssm->drbg_ctx.nonce_v);
}

/**
 * Called when scheduled event needs to be halted. This normally should not be called
 * and is only called when a scheduled item executes but scanning for sync/chain
 * is stil ongoing
 * Context: Interrupt
 */
void
ble_ll_cs_proc_halt(void)
{
}

/**
 * Called when a scheduled event has been removed from the scheduler
 * without being run.
 */
void
ble_ll_cs_proc_rm_from_sched(void *cb_args)
{
}

static int
ble_ll_cs_init_subevent(struct ble_ll_cs_subevent *subevent,
                        struct ble_ll_cs_sm *cssm)
{
    struct ble_hci_ev_le_subev_cs_subevent_result *ev;
    struct ble_hci_ev *hci_ev;
    const struct ble_ll_cs_proc_params *params = &cssm->active_config->proc_params;

    if (!ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_CS_SUBEVENT_RESULT)) {
        return 0;
    }

    hci_ev = ble_transport_alloc_evt(0);
    if (!hci_ev) {
        return BLE_ERR_MEM_CAPACITY;
    }

    hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
    hci_ev->length = sizeof(*ev);
    ev = (void *)hci_ev->data;

    ev->subev_code = BLE_HCI_LE_SUBEV_CS_SUBEVENT_RESULT;
    ev->conn_handle = htole16(cssm->connsm->conn_handle);
    ev->config_id = cssm->active_config_id;
    ev->start_acl_conn_event_counter = params->anchor_conn_event_cntr +
                                       cssm->events_in_procedure_count *
                                       params->event_interval;
    ev->procedure_counter = cssm->procedure_count;
    ev->frequency_compensation = 0xC000;
    ev->reference_power_level = 0x7F;
    ev->num_antenna_paths = cssm->n_ap;
    ev->num_steps_reported = 0;

    memset(subevent, 0, sizeof(*subevent));
    subevent->hci_ev = hci_ev;
    subevent->subev = BLE_HCI_LE_SUBEV_CS_SUBEVENT_RESULT;

    return 0;
}

static int
ble_ll_cs_init_subevent_continue(struct ble_ll_cs_subevent *subevent,
                                 struct ble_ll_cs_sm *cssm)
{
    struct ble_hci_ev_le_subev_cs_subevent_result_continue *ev;
    struct ble_hci_ev *hci_ev;

    if (!ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_CS_SUBEVENT_RESULT_CONTINUE)) {
        return 0;
    }

    hci_ev = ble_transport_alloc_evt(0);
    if (!hci_ev) {
        return BLE_ERR_MEM_CAPACITY;
    }

    hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
    hci_ev->length = sizeof(*ev);
    ev = (void *)hci_ev->data;

    ev->subev_code = BLE_HCI_LE_SUBEV_CS_SUBEVENT_RESULT_CONTINUE;
    ev->conn_handle = htole16(cssm->connsm->conn_handle);
    ev->config_id = cssm->active_config_id;
    ev->abort_reason = 0x00;
    ev->num_antenna_paths = cssm->n_ap;
    ev->num_steps_reported = 0;

    memset(subevent, 0, sizeof(*subevent));
    subevent->hci_ev = hci_ev;
    subevent->subev = BLE_HCI_LE_SUBEV_CS_SUBEVENT_RESULT_CONTINUE;

    return 0;
}

/**
 * Send HCI_LE_CS_Subevent_Result (or _Continue)
 *
 * Context: Link Layer task.
 *
 */
static void
ble_ll_cs_proc_send_subevent(struct ble_npl_event *ev)
{
    struct ble_ll_cs_subevent *subevent;
    struct ble_hci_ev *hci_ev;

    hci_ev = ble_npl_event_get_arg(ev);
    BLE_LL_ASSERT(hci_ev);

    ble_ll_hci_event_send(hci_ev);

    ble_npl_event_set_arg(ev, NULL);
}

static int
ble_ll_cs_proc_queue_subevent(struct ble_ll_cs_subevent *subevent,
                              uint8_t cs_schedule_status,
                              uint8_t proc_abort_reason,
                              uint8_t subev_abort_reason)
{
    uint8_t i;
    struct ble_hci_ev *hci_ev = subevent->hci_ev;
    struct ble_npl_event *ev;
    uint8_t abort_reason = 0x00;
    uint8_t procedure_done;
    uint8_t subevent_done;

    switch (cs_schedule_status) {
    case CS_SCHEDULE_NEW_STEP:
        procedure_done = PROC_DONE_STATUS_PARTIAL;
        subevent_done = SUBEVENT_DONE_STATUS_PARTIAL;
        break;
    case CS_SCHEDULE_NEW_SUBEVENT:
    case CS_SCHEDULE_NEW_EVENT:
        procedure_done = PROC_DONE_STATUS_PARTIAL;
        subevent_done = SUBEVENT_DONE_STATUS_COMPLETED;
        break;
    case CS_SCHEDULE_NEW_PROCEDURE:
    case CS_SCHEDULE_COMPLETED:
        if (proc_abort_reason == PROC_ABORT_SUCCESS) {
            procedure_done = PROC_DONE_STATUS_COMPLETED;
        } else {
            procedure_done = PROC_DONE_STATUS_ABORTED;
        }

        if (subev_abort_reason == SUBEVENT_ABORT_SUCCESS) {
            subevent_done = SUBEVENT_DONE_STATUS_COMPLETED;
        } else {
            subevent_done = SUBEVENT_DONE_STATUS_ABORTED;
        }

        abort_reason = subev_abort_reason << 4 | proc_abort_reason;
        break;
    default:
        BLE_LL_ASSERT(0);
    }

    if (subevent->subev == BLE_HCI_LE_SUBEV_CS_SUBEVENT_RESULT) {
        struct ble_hci_ev_le_subev_cs_subevent_result *ev =
            (struct ble_hci_ev_le_subev_cs_subevent_result *)hci_ev->data;

        ev->procedure_done_status = procedure_done;
        ev->subevent_done_status = subevent_done;
        ev->abort_reason = abort_reason;
        ev->num_steps_reported = subevent->num_steps_reported;
    } else {
        struct ble_hci_ev_le_subev_cs_subevent_result_continue *ev =
            (struct ble_hci_ev_le_subev_cs_subevent_result_continue *)hci_ev->data;

        ev->procedure_done_status = procedure_done;
        ev->subevent_done_status = subevent_done;
        ev->abort_reason = abort_reason;
        ev->num_steps_reported = subevent->num_steps_reported;
    }

    memset(subevent, 0, sizeof(*subevent));

    for (i = 0; i < ARRAY_SIZE(subevent_pool); ++i) {
        ev = &subevent_pool[i];

        if (ble_npl_event_get_arg(ev) == NULL) {
            break;
        }

        ev = NULL;
    }

    if (!ev) {
        ble_transport_free(hci_ev);
        return 1;
    }

    ble_npl_event_init(ev, ble_ll_cs_proc_send_subevent, hci_ev);
    ble_ll_event_add(ev);

    return 0;
}

static uint32_t
ble_ll_cs_proc_get_time_diff(struct ble_ll_cs_sm *cssm)
{
    struct ble_ll_cs_step* step = cssm->current_step;
    struct ble_ll_cs_step_result *result = &cssm->step_result;
    uint32_t arrival_us = result->time_of_arrival_us;
    uint32_t departure_us = result->time_of_departure_us;
    uint32_t arrival_ns = result->time_of_arrival_ns;
    uint32_t departure_ns = result->time_of_departure_ns;
    uint32_t time_diff_us;
    uint32_t time_diff_ns;
    uint32_t time_diff;
    uint32_t nominal_offset_us;
    bool invalid_time;
    uint8_t role = cssm->active_config->role;

    if (step->mode == BLE_LL_CS_MODE1) {
        nominal_offset_us = cssm->t_sy + cssm->t_sy_seq + T_RD + cssm->active_config->t_ip1;
    } else if (step->mode == BLE_LL_CS_MODE3) {
        /* TODO: Include CS tones into offset */
        nominal_offset_us = cssm->t_sy + cssm->t_sy_seq + T_RD + cssm->active_config->t_ip2;
    } else {
        return TIME_DIFF_NOT_AVAILABLE;
    }

    invalid_time = (role == BLE_LL_CS_ROLE_INITIATOR)
        ? (arrival_us < departure_us || (arrival_us == departure_us && arrival_ns <= departure_ns))
        : (arrival_us > departure_us || (arrival_us == departure_us && arrival_ns >= departure_ns));

    if (invalid_time) {
        return TIME_DIFF_NOT_AVAILABLE;
    }

    time_diff_us = (role == BLE_LL_CS_ROLE_INITIATOR)
        ? arrival_us - departure_us
        : departure_us - arrival_us;

    /* The nominal offsets have to be removed (i.e., the interlude time
     * between packets and the length of the packet itself)
     */
    if (time_diff_us < nominal_offset_us) {
        return TIME_DIFF_NOT_AVAILABLE;
    }

    time_diff_ns = (time_diff_us - nominal_offset_us) * 1000 +
                   ((role == BLE_LL_CS_ROLE_INITIATOR) ?
                    arrival_ns - departure_ns : departure_ns - arrival_ns);

    /* Convert to units of 0.5ns */
    time_diff = time_diff_ns * 2;
    return (time_diff > 0x7FFF) ? TIME_DIFF_NOT_AVAILABLE : time_diff;
}

static void
ble_ll_cs_proc_add_step_result(struct ble_ll_cs_sm *cssm)
{
    int rc;
    struct ble_hci_ev *hci_ev;
    struct ble_ll_cs_step *step = cssm->current_step;
    struct ble_ll_cs_config *conf = cssm->active_config;
    struct ble_ll_cs_step_result *result = &cssm->step_result;
    struct cs_steps_data *step_data;
    uint8_t *data;
    int32_t t_sy_center_delta_us = 0;
    uint32_t time_diff;
    uint8_t t_ip1 = conf->t_ip1;
    uint8_t t_ip2 = conf->t_ip2;
    uint8_t t_sw = cssm->t_sw;
    uint8_t t_pm = conf->t_pm;
    uint8_t n_ap = cssm->n_ap;
    uint8_t t_sy = cssm->t_sy + cssm->t_sy_seq;
    uint8_t data_len = 0;
    uint8_t role = cssm->active_config->role;
    uint8_t i;

    /* Estimate the size of the step results */
    if (step->mode == BLE_LL_CS_MODE0) {
        data_len = 3;
        if (role == BLE_LL_CS_ROLE_INITIATOR) {
            data_len += 2;
        }
    } else if (step->mode == BLE_LL_CS_MODE1) {
        data_len = 6;
        if (result->sounding_pct_estimate) {
            data_len += 8;
        }
        t_sy_center_delta_us = t_sy + T_RD + t_ip1;
    } else if (step->mode == BLE_LL_CS_MODE2) {
        data_len = 1 + (n_ap + 1) * 3 * 2;
    } else if (step->mode == BLE_LL_CS_MODE3) {
        data_len = 6 + 1 + (n_ap + 1) * 3 * 2;
        if (result->sounding_pct_estimate) {
            data_len += 8;
        }
        t_sy_center_delta_us = t_sy + T_RD + 2 * (t_sw + t_pm) * (n_ap + 1) + t_ip2;
    }

    BLE_LL_ASSERT(cssm->buffered_subevent.hci_ev);
    hci_ev = cssm->buffered_subevent.hci_ev;

    /* Validate if the step results will fit into the current hci event
     * buffer or mark it as pending and create a new buffer.
     */
    if (hci_ev->length + data_len <= BLE_HCI_MAX_DATA_LEN) {
        ++cssm->buffered_subevent.num_steps_reported;
    } else {
        BLE_LL_ASSERT(cssm->steps_in_subevent_count != 0);

        cssm->cs_schedule_status = CS_SCHEDULE_NEW_STEP;
        rc = ble_ll_cs_proc_queue_subevent(&cssm->buffered_subevent, cssm->cs_schedule_status,
                                           PROC_ABORT_SUCCESS, SUBEVENT_ABORT_SUCCESS);
        BLE_LL_ASSERT(rc == 0);

        ble_ll_cs_init_subevent_continue(&cssm->buffered_subevent, cssm);
        hci_ev = cssm->buffered_subevent.hci_ev;

        BLE_LL_ASSERT(hci_ev);
        BLE_LL_ASSERT(hci_ev->length + data_len <= BLE_HCI_MAX_DATA_LEN);

        cssm->buffered_subevent.hci_ev = hci_ev;
        cssm->buffered_subevent.num_steps_reported = 1;
    }

    step_data = (struct cs_steps_data *)(hci_ev->data + hci_ev->length);
    step_data->mode = step->mode;
    step_data->channel = step->channel;
    data = step_data->data;

    /* Get ToA_ToD_Initiator/ToD_ToA_Reflector */
    time_diff = ble_ll_cs_proc_get_time_diff(cssm);
    /* TODO: Use this */
    (void)t_sy_center_delta_us;

    /* Pack the step results into the buffered hci event */
    if (step->mode == BLE_LL_CS_MODE0) {
        data[0] = result->packet_quality;
        data[1] = result->packet_rssi;
        data[2] = cssm->cs_sync_antenna;
        data_len = 3;

        if (role == BLE_LL_CS_ROLE_INITIATOR) {
            put_le16(data + 3, result->measured_freq_offset);
            data_len += 2;
        }

    } else if (step->mode == BLE_LL_CS_MODE1) {
        data[0] = result->packet_quality;
        data[1] = result->packet_nadm;
        data[2] = result->packet_rssi;
        put_le16(data + 3, time_diff);
        data[5] = cssm->cs_sync_antenna;
        data_len = 6;

        if (result->sounding_pct_estimate) {
            put_le32(data + 6, result->packet_pct1);
            put_le32(data + 10, result->packet_pct2);
            data_len += 8;
        }

    } else if (step->mode == BLE_LL_CS_MODE2) {
        data[0] = cssm->active_config->proc_params.aci;
        data_len = 1;

        for (i = 0; i < cssm->n_ap + 1; ++i) {
            put_le24(data + data_len, result->tone_pct[i]);
            data_len += 3;
        }

        for (i = 0; i < cssm->n_ap + 1; ++i) {
            data[data_len] = result->tone_quality_ind[i];
            ++data_len;
        }

    } else if (step->mode == BLE_LL_CS_MODE3) {
        data[0] = result->packet_quality;
        data[1] = result->packet_nadm;
        data[2] = result->packet_rssi;
        put_le16(data + 3, time_diff);
        data[5] = cssm->cs_sync_antenna;
        data_len = 6;

        if (result->sounding_pct_estimate) {
            put_le32(data + 6, result->packet_pct1);
            put_le32(data + 10, result->packet_pct2);
            data_len += 8;
        }

        data[data_len] = cssm->active_config->proc_params.aci;
        data_len += 1;

        for (i = 0; i < cssm->n_ap + 1; ++i) {
            put_le24(data + data_len, result->tone_pct[i]);
            data_len += 3;
        }

        for (i = 0; i < cssm->n_ap + 1; ++i) {
            data[data_len++] = result->tone_quality_ind[i];
        }
    }

    hci_ev->length += data_len;
    step_data->data_len = data_len;
    memset(result, 0, sizeof(*result));
}

static uint8_t
ble_ll_cs_proc_set_t_sw(struct ble_ll_cs_sm *cssm)
{
    uint8_t t_sw;
    uint8_t t_sw_i;
    uint8_t t_sw_r;
    uint8_t aci = cssm->active_config->proc_params.aci;

    if (cssm->active_config->role == BLE_LL_CS_ROLE_INITIATOR) {
        t_sw_i = g_ble_ll_cs_local_cap.t_sw;
        t_sw_r = cssm->remote_cap.t_sw;
    } else { /* BLE_LL_CS_ROLE_REFLECTOR */
        t_sw_i = cssm->remote_cap.t_sw;
        t_sw_r = g_ble_ll_cs_local_cap.t_sw;
    }

    if (aci == 0) {
        t_sw = 0;
    } else if (IN_RANGE(aci, 1, 3)) {
        t_sw = t_sw_i;
    } else if (IN_RANGE(aci, 4, 6)) {
        t_sw = t_sw_r;
    } else { /* ACI == 7 */
        if (g_ble_ll_cs_local_cap.t_sw > cssm->remote_cap.t_sw) {
            t_sw = g_ble_ll_cs_local_cap.t_sw;
        } else {
            t_sw = cssm->remote_cap.t_sw;
        }
    }

    cssm->t_sw = t_sw;

    return t_sw;
}

static void
ble_ll_cs_proc_tifs_init(uint32_t t_ip1, uint32_t t_ip2, uint32_t t_sw, uint32_t t_fcs)
{
    memset(tifs_table, 0, sizeof(tifs_table));

    tifs_table[STEP_STATE_CS_SYNC_I][STEP_STATE_CS_SYNC_R] = T_RD + t_ip1;
    tifs_table[STEP_STATE_CS_SYNC_I][STEP_STATE_CS_TONE_I] = T_GD;

    tifs_table[STEP_STATE_CS_SYNC_R][STEP_STATE_CS_TONE_R] = T_GD;
    tifs_table[STEP_STATE_CS_SYNC_R][STEP_STATE_COMPLETE] = T_RD + t_fcs;

    tifs_table[STEP_STATE_CS_TONE_I][STEP_STATE_CS_TONE_I] = t_sw;
    tifs_table[STEP_STATE_CS_TONE_I][STEP_STATE_CS_TONE_R] = T_RD + t_ip2;

    tifs_table[STEP_STATE_CS_TONE_R][STEP_STATE_CS_SYNC_R] = T_GD;
    tifs_table[STEP_STATE_CS_TONE_R][STEP_STATE_CS_TONE_R] = t_sw;
    tifs_table[STEP_STATE_CS_TONE_R][STEP_STATE_COMPLETE] = T_RD + t_fcs;
}

static uint32_t
ble_ll_cs_proc_tifs_get(uint8_t old_state, uint8_t new_state)
{
    BLE_LL_ASSERT(old_state < 6 && new_state < 6);
    return tifs_table[old_state][new_state];
}

static int
ble_ll_cs_proc_calculate_timing(struct ble_ll_cs_sm *cssm)
{
    struct ble_ll_cs_config *conf = cssm->active_config;
    const struct ble_ll_cs_proc_params *params = &conf->proc_params;
    uint8_t t_fcs = conf->t_fcs;
    uint8_t t_ip1 = conf->t_ip1;
    uint8_t t_ip2 = conf->t_ip2;
    uint8_t t_pm = conf->t_pm;
    uint8_t t_sw;
    uint8_t n_ap = cssm->n_ap;
    uint8_t t_sy;
    uint8_t t_sy_seq;
    uint8_t sequence_len;

    t_sw = ble_ll_cs_proc_set_t_sw(cssm);

    /* CS packets with no Sounding Sequence or Random Sequence fields take 44 µs
     * to transmit when sent using the LE 1M PHY and 26 µs to transmit when using
     * the LE 2M and the LE 2M 2BT PHYs. CS packets that include a Sounding Sequence
     * or Random Sequence field take proportionally longer to transmit based on
     * the length of the field and the PHY selection.
     */

    sequence_len = rtt_seq_len[conf->rtt_type];

    switch (conf->cs_sync_phy) {
    case BLE_LL_CS_SYNC_PHY_1M:
        t_sy = BLE_LL_CS_SYNC_TIME_1M;
        t_sy_seq = sequence_len;
        break;
    case BLE_LL_CS_SYNC_PHY_2M:
        t_sy = BLE_LL_CS_SYNC_TIME_2M;
        t_sy_seq = sequence_len / 2;
        break;
    default:
        BLE_LL_ASSERT(0);
    }

    cssm->mode_duration_usecs[BLE_LL_CS_MODE0] =
            t_ip1 + T_GD + T_FM + 2 * (t_sy + T_RD) + t_fcs;

    cssm->mode_duration_usecs[BLE_LL_CS_MODE1] =
            t_ip1 + 2 * (t_sy + t_sy_seq + T_RD) + t_fcs;

    cssm->mode_duration_usecs[BLE_LL_CS_MODE2] =
            t_ip2 + 2 * ((t_sw + t_pm) * (n_ap + 1) + T_RD) + t_fcs;

    cssm->mode_duration_usecs[BLE_LL_CS_MODE3] =
            t_ip2 + 2 * ((t_sy + t_sy_seq + T_GD + T_RD) +
            (t_sw + t_pm) * (n_ap + 1)) + t_fcs;

    cssm->t_sy = t_sy;
    cssm->t_sy_seq = t_sy_seq;

    cssm->subevent_interval_usecs = params->subevent_interval * BLE_LL_CS_SUBEVENTS_INTERVAL_UNIT_US;

    cssm->event_interval_usecs = params->event_interval * cssm->connsm->conn_itvl *
                                 BLE_LL_CONN_ITVL_USECS;

    cssm->procedure_interval_usecs = params->procedure_interval * cssm->connsm->conn_itvl *
                                     BLE_LL_CONN_ITVL_USECS;

#if BABBLESIM
    cssm->mode_duration_usecs[BLE_LL_CS_MODE0] += BSIM_PACKET_OVERHEAD;
    cssm->mode_duration_usecs[BLE_LL_CS_MODE1] += BSIM_PACKET_OVERHEAD;
    cssm->mode_duration_usecs[BLE_LL_CS_MODE2] += BSIM_PACKET_OVERHEAD;
    cssm->mode_duration_usecs[BLE_LL_CS_MODE3] += BSIM_PACKET_OVERHEAD;
#endif

    return 0;
}

static uint32_t
ble_ll_cs_proc_step_state_duration_get(uint8_t state, uint8_t mode,
                                       uint8_t t_sy, uint8_t t_sy_seq)
{
    uint32_t duration = 0;

#if BABBLESIM
    t_sy += BSIM_PACKET_OVERHEAD;
#endif

    switch (state) {
    case STEP_STATE_CS_SYNC_I:
    case STEP_STATE_CS_SYNC_R:
        duration = t_sy;
        if (mode != BLE_LL_CS_MODE0) {
            duration += t_sy_seq;
        }
        break;
    case STEP_STATE_CS_TONE_I:
    case STEP_STATE_CS_TONE_R:
        duration = T_FM;
        break;
    case STEP_STATE_INIT:
    case STEP_STATE_COMPLETE:
        duration = 0;
        break;
    default:
        BLE_LL_ASSERT(0);
    }

    return duration;
}

static uint8_t
ble_ll_cs_proc_mode0_next_state(uint8_t state)
{
    switch (state) {
    case STEP_STATE_INIT:
        state = STEP_STATE_CS_SYNC_I;
        break;
    case STEP_STATE_CS_SYNC_I:
        state = STEP_STATE_CS_SYNC_R;
        break;
    case STEP_STATE_CS_SYNC_R:
        state = STEP_STATE_CS_TONE_R;
        break;
    case STEP_STATE_CS_TONE_R:
        state = STEP_STATE_COMPLETE;
        break;
    default:
        BLE_LL_ASSERT(0);
    }

    return state;
}

static uint8_t
ble_ll_cs_proc_mode1_next_state(uint8_t state)
{
    switch (state) {
    case STEP_STATE_INIT:
        state = STEP_STATE_CS_SYNC_I;
        break;
    case STEP_STATE_CS_SYNC_I:
        state = STEP_STATE_CS_SYNC_R;
        break;
    case STEP_STATE_CS_SYNC_R:
        state = STEP_STATE_COMPLETE;
        break;
    default:
        BLE_LL_ASSERT(0);
    }

    return state;
}

static uint8_t
ble_ll_cs_proc_mode2_next_state(uint8_t state, uint8_t slot_count)
{
    switch (state) {
    case STEP_STATE_INIT:
        state = STEP_STATE_CS_TONE_I;
        break;
    case STEP_STATE_CS_TONE_I:
        if (slot_count == 0) {
            state = STEP_STATE_CS_TONE_R;
        }
        break;
    case STEP_STATE_CS_TONE_R:
        if (slot_count == 0) {
            state = STEP_STATE_COMPLETE;
        }
        break;
    default:
        BLE_LL_ASSERT(0);
    }

    return state;
}

static uint8_t
ble_ll_cs_proc_mode3_next_state(uint8_t state, uint8_t slot_count)
{
    switch (state) {
    case STEP_STATE_INIT:
        state = STEP_STATE_CS_SYNC_I;
        break;
    case STEP_STATE_CS_SYNC_I:
        state = STEP_STATE_CS_TONE_I;
        break;
    case STEP_STATE_CS_TONE_I:
        if (slot_count == 0) {
            state = STEP_STATE_CS_TONE_R;
        }
        break;
    case STEP_STATE_CS_TONE_R:
        if (slot_count == 0) {
            state = STEP_STATE_CS_SYNC_R;
        }
        break;
    case STEP_STATE_CS_SYNC_R:
        state = STEP_STATE_COMPLETE;
        break;
    default:
        BLE_LL_ASSERT(0);
    }

    return state;
}

static uint8_t
ble_ll_cs_proc_next_step_state_get(uint8_t mode, uint8_t state, uint8_t slot_count)
{
    switch (mode) {
    case BLE_LL_CS_MODE0:
        state = ble_ll_cs_proc_mode0_next_state(state);
        break;
    case BLE_LL_CS_MODE1:
        state = ble_ll_cs_proc_mode1_next_state(state);
        break;
#if CS_TONE_SUPPORT
    case BLE_LL_CS_MODE2:
        state = ble_ll_cs_proc_mode2_next_state(state, slot_count);
        break;
    case BLE_LL_CS_MODE3:
        state = ble_ll_cs_proc_mode3_next_state(state, slot_count);
        break;
#endif
    default:
        BLE_LL_ASSERT(0);
    }

    return state;
}

static int
ble_ll_cs_proc_skip_txrx(struct ble_ll_cs_sm *cssm)
{
    struct ble_ll_cs_step_transmission *transm = cssm->current_step->next_transm;

    cssm->anchor_usecs += transm->duration_usecs + transm->end_tifs;
    ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);

    return 0;
}

static ble_ll_cs_sched_cb_func
ble_ll_cs_proc_sched_cb_get(uint8_t role, uint8_t step_state)
{
    ble_ll_cs_sched_cb_func cb;
    bool is_initiator = (role == BLE_LL_CS_ROLE_INITIATOR);

    switch (step_state) {
    case STEP_STATE_CS_SYNC_I:
        cb = is_initiator ? ble_ll_cs_sync_tx_start : ble_ll_cs_sync_rx_start;
        break;
    case STEP_STATE_CS_SYNC_R:
        cb = is_initiator ? ble_ll_cs_sync_rx_start : ble_ll_cs_sync_tx_start;
        break;
    case STEP_STATE_CS_TONE_I:
#if CS_TONE_SUPPORT
        cb = is_initiator ? ble_ll_cs_tone_tx_start : ble_ll_cs_tone_rx_start;
#else
        cb = ble_ll_cs_proc_skip_txrx;
#endif
        break;
    case STEP_STATE_CS_TONE_R:
#if CS_TONE_SUPPORT
        cb = is_initiator ? ble_ll_cs_tone_rx_start : ble_ll_cs_tone_tx_start;
#else
        cb = ble_ll_cs_proc_skip_txrx;
#endif
        break;
    default:
        BLE_LL_ASSERT(0);
    }

#if BABBLESIM
    if (cb == ble_ll_cs_tone_tx_start || cb == ble_ll_cs_tone_rx_start) {
        cb = ble_ll_cs_proc_skip_txrx;
    }
#endif

    return cb;
}

static uint8_t
ble_ll_cs_proc_transition_get(ble_ll_cs_sched_cb_func cb)
{
    uint8_t transition;

    if (cb == ble_ll_cs_sync_tx_start) {
        transition = BLE_PHY_TRANSITION_TO_TX_CS_SYNC;
    } else if (cb == ble_ll_cs_sync_rx_start) {
        transition = BLE_PHY_TRANSITION_TO_RX_CS_SYNC;
    } else if (cb == ble_ll_cs_tone_tx_start) {
        transition = BLE_PHY_TRANSITION_TO_TX_CS_TONE;
    } else if (cb == ble_ll_cs_tone_rx_start) {
        transition = BLE_PHY_TRANSITION_TO_RX_CS_TONE;
    } else {
        transition = BLE_PHY_TRANSITION_NONE;
    }

    return transition;
}

static int
ble_ll_cs_proc_step_transmission_generate(struct ble_ll_cs_sm *cssm,
                                          struct ble_ll_cs_step *step,
                                          struct ble_ll_cs_step_transmission *transm_buf,
                                          uint8_t *buf_free_slots,
                                          struct ble_ll_cs_step_transmission *prev_transm)
{
    struct ble_ll_cs_step_transmission *transm;
    ble_ll_cs_sched_cb_func cb;
    uint32_t duration_usecs;
    uint32_t end_tifs;
    uint8_t prev_state;
    uint8_t next_state;
    uint8_t slot_count;
    uint8_t role = cssm->active_config->role;

    transm = transm_buf;
    prev_transm = (prev_transm == NULL) ? transm : prev_transm;
    step->next_transm = transm;
    slot_count = cssm->n_ap;

    prev_state = STEP_STATE_INIT;
    next_state = ble_ll_cs_proc_next_step_state_get(step->mode, prev_state, slot_count);

    while (next_state != STEP_STATE_COMPLETE) {
        cb = NULL;
        if (next_state == STEP_STATE_CS_TONE_I || next_state == STEP_STATE_CS_TONE_R) {
            if (slot_count == 0) {
                slot_count = cssm->n_ap;

                if ((next_state == STEP_STATE_CS_TONE_I) ? step->tone_ext_presence_i
                                                         : step->tone_ext_presence_r) {
                    cb = ble_ll_cs_proc_skip_txrx;
                }
            } else {
                --slot_count;
            }
        }

        if (!cb) {
            cb = ble_ll_cs_proc_sched_cb_get(cssm->active_config->role, next_state);
        }

        duration_usecs = ble_ll_cs_proc_step_state_duration_get(next_state, step->mode,
                                                                cssm->t_sy, cssm->t_sy_seq);
        end_tifs = ble_ll_cs_proc_tifs_get(prev_state, next_state);

        if (cb == ble_ll_cs_proc_skip_txrx) {
            /* If a transmission slot should be skipped, just add up the T_IFS and its duration */
            prev_transm->end_tifs += duration_usecs + end_tifs;
            prev_transm->end_transition = BLE_PHY_TRANSITION_NONE;
        } else {
            /* Set the end transition for the previous transmission  */
            prev_transm->end_tifs += end_tifs;
            prev_transm->end_transition = ble_ll_cs_proc_transition_get(cb);

            /* Next transmission  */
            transm->state = next_state;
            transm->cb = cb;
            transm->duration_usecs = duration_usecs;
            transm->wfr_usecs = duration_usecs;
            transm->end_tifs = 0;
            transm->end_transition = 0;
            prev_transm = transm++;
            --(*buf_free_slots);
            BLE_LL_ASSERT(*buf_free_slots > 0);
        }

        prev_state = next_state;
        next_state = ble_ll_cs_proc_next_step_state_get(step->mode, prev_state, slot_count);
    }

    BLE_LL_ASSERT(next_state == STEP_STATE_COMPLETE);

    prev_transm->end_tifs += ble_ll_cs_proc_tifs_get(prev_state, next_state);
    prev_transm->end_transition = BLE_PHY_TRANSITION_NONE;
    transm->state = STEP_STATE_COMPLETE;

    return 0;
}

static int
ble_ll_cs_step_generate(struct ble_ll_cs_sm *cssm, struct ble_ll_cs_step *step,
                        struct ble_ll_cs_step_transmission *transm_buf, uint8_t *buf_free_slots,
                        struct ble_ll_cs_step_transmission *prev_transm, uint8_t subevent_state,
                        uint16_t steps_in_procedure_count)
{
    int rc;
    const struct ble_ll_cs_config *conf = cssm->active_config;

    if (conf->sub_mode != 0xFF && subevent_state == SUBEVENT_STATE_MAINMODE_STEP) {
        if (cssm->main_step_count == 0xFF) {
            /* Rand the number of Main_Mode steps to execute
             * before a Sub_Mode insertion.
             */
            rc = ble_ll_cs_drbg_rand_main_mode_steps(
                &cssm->drbg_ctx, steps_in_procedure_count,
                conf->main_mode_min_steps, conf->main_mode_max_steps,
                &cssm->main_step_count);

            if (rc) {
                return rc;
            }
        }

        --cssm->main_step_count;
    }

    if (subevent_state != SUBEVENT_STATE_REPETITION_STEP) {
        rc = ble_ll_cs_generate_channel(cssm, step, steps_in_procedure_count);
        if (rc) {
            return rc;
        }

        /* Update channel cache used in repetition steps */
        if (subevent_state == SUBEVENT_STATE_MAINMODE_STEP &&
            conf->main_mode_repetition) {
            cssm->repetition_channels[0] = cssm->repetition_channels[1];
            cssm->repetition_channels[1] = cssm->repetition_channels[2];
            cssm->repetition_channels[2] = step->channel;
        }
    } else {
        step->channel = cssm->repetition_channels[
            conf->main_mode_repetition - cssm->repetition_count];
    }

    if (step->mode == BLE_LL_CS_MODE0 || step->mode == BLE_LL_CS_MODE1 ||
        step->mode == BLE_LL_CS_MODE3) {
        /* Generate CS Access Address */
        rc = ble_ll_cs_drbg_generate_aa(&cssm->drbg_ctx,
                                        steps_in_procedure_count,
                                        &step->initiator_aa, &step->reflector_aa);
        if (rc) {
            return rc;
        }

        if (conf->role == BLE_LL_CS_ROLE_INITIATOR) {
            step->tx_aa = step->initiator_aa;
            step->rx_aa = step->reflector_aa;
        } else { /* BLE_LL_CS_ROLE_REFLECTOR */
            step->rx_aa = step->initiator_aa;
            step->tx_aa = step->reflector_aa;
        }

        /* TODO: Generate antenna ID if multiple antennas available */
        cssm->cs_sync_antenna = 0x01;
    }

    if (step->mode == BLE_LL_CS_MODE2 || step->mode == BLE_LL_CS_MODE3) {
        rc = ble_ll_cs_drbg_rand_tone_ext_presence(
            &cssm->drbg_ctx, steps_in_procedure_count, &step->tone_ext_presence_i);
        if (rc) {
            return rc;
        }

        rc = ble_ll_cs_drbg_rand_tone_ext_presence(
            &cssm->drbg_ctx, steps_in_procedure_count, &step->tone_ext_presence_r);
        if (rc) {
            return rc;
        }
    }

    ble_ll_cs_proc_step_transmission_generate(cssm, step, transm_buf, buf_free_slots, prev_transm);

    return 0;
}

static int
ble_ll_cs_setup_next_subevent(struct ble_ll_cs_sm *cssm)
{
    cssm->steps_in_subevent_count = 0;

    cssm->subevent_anchor_usecs += cssm->subevent_interval_usecs;
    cssm->step_anchor_usecs = cssm->subevent_anchor_usecs;
    cssm->anchor_usecs = cssm->step_anchor_usecs;

    return 0;
}

static int
ble_ll_cs_setup_next_event(struct ble_ll_cs_sm *cssm)
{
    cssm->cs_schedule_status = CS_SCHEDULE_NEW_EVENT;
    ++cssm->events_in_procedure_count;
    cssm->subevents_in_event_count = 0;
    cssm->steps_in_subevent_count = 0;

    cssm->event_anchor_usecs += cssm->event_interval_usecs;
    cssm->subevent_anchor_usecs = cssm->event_anchor_usecs;
    cssm->step_anchor_usecs = cssm->event_anchor_usecs;
    cssm->anchor_usecs = cssm->step_anchor_usecs;

    return 0;
}

static int
ble_ll_cs_setup_next_procedure(struct ble_ll_cs_sm *cssm)
{
    const struct ble_ll_cs_config *conf = cssm->active_config;

    if (cssm->procedure_count + 1 >= conf->proc_params.max_procedure_count ||
        cssm->terminate_measurement) {
        /* All CS procedures have been completed or
         * the CS procedure repeat series has been terminated.
         */
        cssm->cs_schedule_status = CS_SCHEDULE_COMPLETED;

        if (cssm->terminate_measurement) {
            cssm->proc_abort_reason = PROC_ABORT_REQUESTED;
        }

        return 1;
    }

    cssm->cs_schedule_status = CS_SCHEDULE_NEW_PROCEDURE;
    ++cssm->procedure_count;
    cssm->events_in_procedure_count = 0;
    cssm->subevents_in_procedure_count = 0;
    cssm->subevents_in_event_count = 0;
    cssm->steps_in_procedure_count = 0;
    cssm->steps_in_subevent_count = 0;
    cssm->main_step_count = 0xFF;

    cssm->procedure_anchor_usecs += cssm->procedure_interval_usecs;
    cssm->step_anchor_usecs = cssm->procedure_anchor_usecs;
    cssm->subevent_anchor_usecs = cssm->step_anchor_usecs;
    cssm->anchor_usecs = cssm->step_anchor_usecs;

    return ble_ll_cs_backtracking_resistance(cssm);
}

static int
ble_ll_cs_proc_subevent_generate(struct ble_ll_cs_sm *cssm)
{
    int rc;
    struct ble_ll_cs_step *step;
    struct ble_ll_cs_step_transmission *prev_transm = NULL;
    struct ble_ll_cs_step_transmission *transm_buf = &transmission_pattern[0];
    const struct ble_ll_cs_config *conf = cssm->active_config;
    const struct ble_ll_cs_proc_params *params = &conf->proc_params;

    uint32_t subevent_duration = 0;
    uint32_t new_subevent_duration = 0;
    uint32_t procedure_duration;
    uint32_t max_subevent_duration = params->subevent_len;
    uint32_t max_procedure_duration = params->max_procedure_len * BLE_LL_CS_PROCEDURE_LEN_UNIT_US;

    uint8_t step_mode;
    uint8_t subevent_state;
    uint8_t buf_slots = sizeof(transmission_pattern) / sizeof(transmission_pattern[0]);
    uint8_t buf_free_slots = buf_slots;
    uint8_t steps_in_subevent = 0;
    uint8_t mode0_step_count = conf->mode_0_steps;

    cssm->cs_schedule_status = CS_SCHEDULE_NEW_SUBEVENT;
    ++cssm->subevents_in_procedure_count;
    ++cssm->subevents_in_event_count;

    if (cssm->current_step == NULL) {
        /* Setup the first subevent */
        cssm->steps_in_procedure_count = 0;
        cssm->steps_in_subevent_count = 0;
        cssm->subevents_in_procedure_count = 0;
        cssm->subevents_in_event_count = 0;
        cssm->procedure_count = 0;
    } else if (cssm->subevents_in_procedure_count + 1 > BLE_LL_CS_SUBEVENTS_PER_PROCEDURE_MAX) {
        /* No more subevents will fit inside the current CS procedure */
        rc = ble_ll_cs_setup_next_procedure(cssm);
        if (rc) {
            /* No more procedures can be generated */
            return rc;
        }
    } else if (cssm->subevents_in_event_count + 1 > params->subevents_per_event) {
        /* No more subevents will fit inside the current CS event */
        ble_ll_cs_setup_next_event(cssm);
    } else {
        /* Setup a new subevent */
        ble_ll_cs_setup_next_subevent(cssm);
    }

    procedure_duration = cssm->anchor_usecs - cssm->procedure_anchor_usecs;

    if (cssm->subevents_in_procedure_count == 0) {
        cssm->main_step_count = 0xFF;
    } else {
        cssm->repetition_count = conf->main_mode_repetition;
    }

    ble_ll_cs_drbg_clear_cache(&cssm->drbg_ctx);
    memset(transmission_pattern, 0, sizeof(transmission_pattern));
    memset(subevent_steps, 0, sizeof(subevent_steps));
    step = &subevent_steps[0];
    cssm->current_step = step;
    cssm->last_step = step;

    while (true) {
        ++steps_in_subevent;
        BLE_LL_ASSERT(steps_in_subevent <= sizeof(subevent_steps));

        /* Determine next step mode */
        if (mode0_step_count > 0) {
            subevent_state = SUBEVENT_STATE_MODE0_STEP;
            step_mode = BLE_LL_CS_MODE0;
            --mode0_step_count;

        } else if (cssm->repetition_count > 0) {
            subevent_state = SUBEVENT_STATE_REPETITION_STEP;
            step_mode = conf->main_mode;
            --cssm->repetition_count;

        } else if (cssm->main_step_count == 0) {
            subevent_state = SUBEVENT_STATE_SUBMODE_STEP;
            step_mode = conf->sub_mode;
            cssm->main_step_count = 0xFF;

        } else {
            subevent_state = SUBEVENT_STATE_MAINMODE_STEP;
            step_mode = conf->main_mode;
        }

        new_subevent_duration = subevent_duration + cssm->mode_duration_usecs[step_mode];

        /* Check if this step will fit inside the current subevent/procedure */
        if (cssm->steps_in_procedure_count + steps_in_subevent > BLE_LL_CS_STEPS_PER_PROCEDURE_MAX ||
            procedure_duration + new_subevent_duration > max_procedure_duration ||
            steps_in_subevent > BLE_LL_CS_STEPS_PER_SUBEVENT_MAX ||
            new_subevent_duration > max_subevent_duration) {

            /* TODO: Implement the remaining conditions to complete the procedure:
             * • If Channel Selection Algorithm #3b is used for non-mode-0 steps, and the channel
             *   map generated from CSFilteredChM has been used for CSNumRepetitions cycles
             *   for non-mode‑0 steps including the use for both Main_Mode and Sub_Mode steps.
             * • If Channel Selection Algorithm #3c is used for non-mode-0 steps, and
             *   CSNumRepetitions invocations of Channel Selection Algorithm #3c have been
             *   completed for non-mode‑0 steps including the use for both Main_Mode and
             *   Sub_Mode steps.
             */

            if (steps_in_subevent > 1) {
                /* Compelete generation of the current subevent. It will be the last one
                 * in the current procedure.
                 */
                --steps_in_subevent;

                break;
            }

            /* Skip this subevent and generate a new one in a new procedure */
            rc = ble_ll_cs_setup_next_procedure(cssm);
            if (rc) {
                /* No more procedures can be generated */
                return rc;
            }

            steps_in_subevent = 0;
            mode0_step_count = conf->mode_0_steps;
            cssm->repetition_count = conf->main_mode_repetition;
            procedure_duration = cssm->anchor_usecs - cssm->procedure_anchor_usecs;

            continue;
        }

        subevent_duration = new_subevent_duration;
        step->mode = step_mode;

        ble_ll_cs_step_generate(cssm, step, transm_buf, &buf_free_slots,
                                prev_transm,
                                subevent_state,
                                cssm->steps_in_procedure_count + steps_in_subevent - 1);
        transm_buf = transmission_pattern + buf_slots - buf_free_slots;
        prev_transm = transm_buf - 1;
        step->last_transm = prev_transm;
        cssm->last_step = step;
        ++step;
    }

    return 0;
}

static int
ble_ll_cs_proc_next_state(struct ble_ll_cs_sm *cssm)
{
    int rc;
    struct ble_ll_cs_step *step = cssm->current_step;

    if (step != NULL) {
        if (step->next_transm != step->last_transm) {
            /* Continue with the next transmission of the current step */
            ++step->next_transm;
            return 0;
        }

        /* Save the step results */
        ble_ll_cs_proc_add_step_result(cssm);
        ++cssm->steps_in_procedure_count;
        ++cssm->steps_in_subevent_count;

        if (cssm->current_step != cssm->last_step) {
            /* Continue with the next step */
            ++cssm->current_step;
            return 0;
        }
    }

    /* Generate new subevent steps */
    rc = ble_ll_cs_proc_subevent_generate(cssm);
    if (rc) {
        if (!cssm->proc_abort_reason && cssm->cs_schedule_status != CS_SCHEDULE_COMPLETED) {
            cssm->proc_abort_reason = PROC_ABORT_UNSPECIFIED;
        }

        cssm->cs_schedule_status = CS_SCHEDULE_COMPLETED;
        rc = ble_ll_cs_proc_queue_subevent(&cssm->buffered_subevent, cssm->cs_schedule_status,
                                           cssm->proc_abort_reason, cssm->subev_abort_reason);
        BLE_LL_ASSERT(rc == 0);

        return 1;
    }

    if (step != NULL) {
        rc = ble_ll_cs_proc_queue_subevent(&cssm->buffered_subevent, cssm->cs_schedule_status,
                                           cssm->proc_abort_reason, cssm->subev_abort_reason);
        BLE_LL_ASSERT(rc == 0);
    }

    rc = ble_ll_cs_init_subevent(&cssm->buffered_subevent, cssm);
    BLE_LL_ASSERT(rc == 0);

    return 0;
}

static int
ble_ll_cs_proc_sched_cb(struct ble_ll_sched_item *sch)
{
    int rc;
    struct ble_ll_cs_sm *cssm = sch->cb_arg;

    BLE_LL_ASSERT(cssm != NULL);

    rc = cssm->sched_cb(cssm);
    if (rc) {
        return BLE_LL_SCHED_STATE_DONE;
    }

    return BLE_LL_SCHED_STATE_RUNNING;
}

int
ble_ll_cs_proc_schedule_next_tx_or_rx(struct ble_ll_cs_sm *cssm)
{
    int rc;
    struct ble_ll_cs_step_transmission *transm;
    uint32_t anchor_cputime;
    uint8_t transition;
    uint8_t offset;

    rc = ble_ll_cs_proc_next_state(cssm);
    if (rc) {
        ble_phy_disable();
        ble_ll_state_set(BLE_LL_STATE_STANDBY);

        return rc;
    }

    transm = cssm->current_step->next_transm;
    anchor_cputime = ble_ll_tmr_u2t(cssm->anchor_usecs);

    if (anchor_cputime - g_ble_ll_sched_offset_ticks > ble_ll_tmr_get()) {
        if (ble_ll_state_get() == BLE_LL_STATE_CS) {
            ble_phy_disable();
            ble_ll_state_set(BLE_LL_STATE_STANDBY);
        }

        if (transm->cb == ble_ll_cs_sync_rx_start || transm->cb == ble_ll_cs_tone_rx_start) {
            /* Start RX windows earlier */
            offset = 2;
            cssm->anchor_usecs -= offset;
            transm->duration_usecs += offset;
        }

        cssm->sch.start_time = anchor_cputime - g_ble_ll_sched_offset_ticks;
        cssm->sched_cb = transm->cb;
        cssm->sch.end_time = anchor_cputime + ble_ll_tmr_u2t_up(transm->duration_usecs);
        cssm->sch.remainder = 0;
        cssm->sch.sched_type = BLE_LL_SCHED_TYPE_CS;
        cssm->sch.cb_arg = cssm;
        cssm->sch.sched_cb = ble_ll_cs_proc_sched_cb;
        rc = ble_ll_sched_cs_proc(&cssm->sch);
    } else {
        /* Radio start already scheduled, just configure. */
        rc = transm->cb(cssm);
    }

    return rc;
}

void
ble_ll_cs_proc_set_now_as_anchor_point(struct ble_ll_cs_sm *cssm)
{
    cssm->anchor_usecs = ble_ll_tmr_t2u(ble_ll_tmr_get());
}

int
ble_ll_cs_proc_scheduling_start(struct ble_ll_conn_sm *connsm, uint8_t config_id)
{
    int rc;
    struct ble_ll_cs_sm *cssm = connsm->cssm;
    struct ble_ll_cs_config *conf;
    const struct ble_ll_cs_proc_params *params;
    uint32_t anchor_ticks;
    uint8_t anchor_rem_usecs;

    conf = &cssm->config[config_id];
    cssm->active_config = conf;
    cssm->active_config_id = config_id;
    params = &conf->proc_params;

    ble_ll_conn_anchor_event_cntr_get(connsm, params->anchor_conn_event_cntr,
                                      &anchor_ticks, &anchor_rem_usecs);

    ble_ll_tmr_add(&anchor_ticks, &anchor_rem_usecs, params->event_offset);

    if (anchor_ticks - g_ble_ll_sched_offset_ticks < ble_ll_tmr_get()) {
        /* The start happend too late for the negotiated event counter. */
        return BLE_ERR_INV_LMP_LL_PARM;
    }

    g_ble_ll_cs_sm_current = cssm;
    cssm->anchor_usecs = ble_ll_tmr_t2u(anchor_ticks);
    cssm->n_ap = aci_table[params->aci].n_ap;
    cssm->procedure_anchor_usecs = cssm->anchor_usecs;
    cssm->event_anchor_usecs = cssm->anchor_usecs;
    cssm->subevent_anchor_usecs = cssm->anchor_usecs;
    cssm->step_anchor_usecs = cssm->anchor_usecs;
    cssm->current_step = NULL;
    cssm->last_step = NULL;

    ble_ll_cs_proc_tifs_init(cssm->active_config->t_ip1,
                             cssm->active_config->t_ip2,
                             cssm->t_sw,
                             cssm->active_config->t_fcs);

    ble_ll_cs_proc_calculate_timing(cssm);

    cssm->mode0_next_chan_id = 0xFF;
    cssm->non_mode0_next_chan_id = 0xFF;

    if (g_ble_ll_cs_local_cap.sounding_pct_estimate &&
        cssm->active_config->rtt_type != BLE_LL_CS_RTT_AA_ONLY) {
        cssm->step_result.sounding_pct_estimate = 1;
    }

    rc = ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
    if (rc) {
        return BLE_ERR_UNSPECIFIED;
    }

    return BLE_ERR_SUCCESS;
}

void
ble_ll_cs_proc_sync_lost(struct ble_ll_cs_sm *cssm)
{
    ble_ll_cs_proc_set_now_as_anchor_point(cssm);
    ble_phy_transition_set(BLE_PHY_TRANSITION_NONE, 0);
    ble_phy_disable();
    ble_ll_state_set(BLE_LL_STATE_STANDBY);
    /* TODO: Handle a lost sync */
}

/**
 * Called when the wait for response timer expires while in the sync state.
 *
 * Context: Interrupt.
 */
void
ble_ll_cs_proc_wfr_timer_exp(void)
{
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;

    BLE_LL_ASSERT(cssm != NULL);

    ble_ll_cs_proc_sync_lost(cssm);
}

/**
 * Called when received a complete CS_SYNC packet or CS TONE.
 *
 * Context: Interrupt
 *
 * @param rxpdu
 * @param rxhdr
 *
 * @return int
 *       < 0: Disable the phy after reception.
 *      == 0: Success. Do not disable the PHY.
 *       > 0: Do not disable PHY as that has already been done.
 */
int
ble_ll_cs_proc_rx_isr_end(uint8_t *rxbuf, struct ble_mbuf_hdr *rxhdr)
{
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;

    BLE_LL_ASSERT(cssm != NULL);

    switch (cssm->current_step->next_transm->state) {
    case STEP_STATE_CS_SYNC_I:
    case STEP_STATE_CS_SYNC_R:
        ble_ll_cs_sync_rx_end(cssm, rxbuf, rxhdr);
        break;
    case STEP_STATE_CS_TONE_I:
    case STEP_STATE_CS_TONE_R:
        ble_ll_cs_tone_rx_end_cb(cssm);
        break;
    default:
        BLE_LL_ASSERT(0);
    }

    return 1;
}

#endif /* BLE_LL_CHANNEL_SOUNDING */
