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

/* States within step */
#define STEP_STATE_INIT          (0)
#define STEP_STATE_CS_SYNC_I     (1)
#define STEP_STATE_CS_SYNC_R     (2)
#define STEP_STATE_CS_TONE_I     (3)
#define STEP_STATE_CS_TONE_R     (4)
#define STEP_STATE_CS_TONE_EXT_I (5)
#define STEP_STATE_CS_TONE_EXT_R (6)
#define STEP_STATE_COMPLETE      (7)

#define SUBEVENT_STATE_MODE0_STEP      (0)
#define SUBEVENT_STATE_REPETITION_STEP (1)
#define SUBEVENT_STATE_SUBMODE_STEP    (2)
#define SUBEVENT_STATE_MAINMODE_STEP   (3)

#define CS_SCHEDULE_NEW_STEP      (0)
#define CS_SCHEDULE_NEW_SUBEVENT  (1)
#define CS_SCHEDULE_NEW_EVENT     (2)
#define CS_SCHEDULE_NEW_PROCEDURE (3)
#define CS_SCHEDULE_COMPLETED     (4)

#define BLE_LL_CONN_ITVL_USECS    (1250)

/* The ramp-down window in µs */
#define T_RD (5)
/* The guard time duration in µs */
#define T_GD (10)
/* The requency measurement period in µs */
#define T_FM (80)

#define TIME_DIFF_NOT_AVAILABLE  (0x00008000)
/* TODO: Hardcorded for 16MHz timer, should be configurable. */
/* units of 0.5 nanoseconds */
#define GET_TIME_DIFF(_diff_ticks, _center_delta_us) (_diff_ticks * 125 - _center_delta_us * 2000);

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

static struct ble_ll_cs_aci aci_table[] = {
    {1, 1, 1}, {2, 2, 1}, {3, 3, 1}, {4, 4, 1},
    {2, 1, 2}, {3, 1, 3}, {4, 1, 4}, {4, 2, 2}
};
static const uint8_t rtt_seq_len[] = {0, 4, 12, 4, 8, 12, 16};

/* For queueing the HCI Subevent Result (Continue) events */
static struct ble_npl_event subevent_pool[MYNEWT_VAL(BLE_LL_CHANNEL_SOUNDING_SUBEVENT_EV_MAX_CNT)];

static int
ble_ll_cs_generate_channel(struct ble_ll_cs_sm *cssm)
{
    int rc;
    uint8_t transaction_id;
    uint8_t *channel_array;
    uint8_t *next_channel_id;

    if (cssm->step_mode == BLE_LL_CS_MODE0) {
        transaction_id = BLE_LL_CS_DRBG_HOP_CHAN_MODE0;
        channel_array = cssm->mode0_channels;
        next_channel_id = &cssm->mode0_next_chan_id;
    } else {
        transaction_id = BLE_LL_CS_DRBG_HOP_CHAN_NON_MODE0;
        channel_array = cssm->non_mode0_channels;
        next_channel_id = &cssm->non_mode0_next_chan_id;
    }

    if (g_ble_ll_cs_chan_count <= *next_channel_id) {
        rc = ble_ll_cs_drbg_shuffle_cr1(&cssm->drbg_ctx, cssm->steps_in_procedure_count,
                                        transaction_id, g_ble_ll_cs_chan_indices,
                                        channel_array, g_ble_ll_cs_chan_count);
        if (rc) {
            return rc;
        }

        *next_channel_id = 0;
    }

    cssm->channel = channel_array[(*next_channel_id)++];

#if BABBLESIM
    cssm->channel %= 40;
#endif

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
    ev->start_acl_conn_event_counter = params->anchor_conn_event +
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
    int i;
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

static void
ble_ll_cs_proc_add_step_result(struct ble_ll_cs_sm *cssm)
{
    int rc;
    struct ble_hci_ev *hci_ev;
    struct ble_ll_cs_config *conf = cssm->active_config;
    struct ble_ll_cs_step_result *result = &cssm->step_result;
    struct cs_steps_data *step_data;
    uint8_t *data;
    int32_t t_sy_center_delta_us;
    int32_t time_diff_ticks;
    int32_t time_diff = TIME_DIFF_NOT_AVAILABLE;
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
    if (cssm->step_mode == BLE_LL_CS_MODE0) {
        data_len = 3;
        if (role == BLE_LL_CS_ROLE_INITIATOR) {
            data_len += 2;
        }
    } else if (cssm->step_mode == BLE_LL_CS_MODE1) {
        data_len = 6;
        if (result->sounding_pct_estimate) {
            data_len += 8;
        }
        t_sy_center_delta_us = t_sy + T_RD + t_ip1;
    } else if (cssm->step_mode == BLE_LL_CS_MODE2) {
        data_len = 1 + (n_ap + 1) * 3 * 2;
    } else if (cssm->step_mode == BLE_LL_CS_MODE3) {
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

        rc = ble_ll_cs_proc_queue_subevent(&cssm->buffered_subevent, CS_SCHEDULE_NEW_STEP,
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
    step_data->mode = cssm->step_mode;
    step_data->channel = cssm->channel;
    data = step_data->data;

    if (cssm->step_mode == BLE_LL_CS_MODE1 || cssm->step_mode == BLE_LL_CS_MODE3) {
        if (role == BLE_LL_CS_ROLE_INITIATOR) {
            if (result->time_of_arrival > result->time_of_departure) {
                time_diff_ticks = result->time_of_arrival - result->time_of_departure;
                time_diff = GET_TIME_DIFF(time_diff_ticks, t_sy_center_delta_us);
            }
        } else { /* BLE_LL_CS_ROLE_REFLECTOR */
            if (result->time_of_arrival < result->time_of_departure) {
                time_diff_ticks = result->time_of_departure - result->time_of_arrival;
                time_diff = GET_TIME_DIFF(time_diff_ticks, t_sy_center_delta_us);
            }
        }

        if (time_diff != TIME_DIFF_NOT_AVAILABLE && (time_diff < -0x7FFF || 0x7FFF < time_diff)) {
            time_diff = TIME_DIFF_NOT_AVAILABLE;
        }
    }

    if (cssm->step_mode == BLE_LL_CS_MODE0) {
        data[0] = result->packet_quality;
        data[1] = result->packet_rssi;
        data[2] = cssm->cs_sync_antenna;
        data_len = 3;

        if (role == BLE_LL_CS_ROLE_INITIATOR) {
            put_le16(data + 3, result->measured_freq_offset);
            data_len += 2;
        }

    } else if (cssm->step_mode == BLE_LL_CS_MODE1) {
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

    } else if (cssm->step_mode == BLE_LL_CS_MODE2) {
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

    } else if (cssm->step_mode == BLE_LL_CS_MODE3) {
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
    }

    cssm->mode0_step_duration_usecs = t_ip1 + T_GD + T_FM + 2 * (t_sy + T_RD) + t_fcs;

    cssm->mode1_step_duration_usecs = t_ip1 + 2 * (t_sy + t_sy_seq + T_RD) + t_fcs;

    cssm->mode2_step_duration_usecs = t_ip2 + 2 * ((t_sw + t_pm) * (n_ap + 1) + T_RD) + t_fcs;

    cssm->mode3_step_duration_usecs = t_ip2 + 2 * ((t_sy + t_sy_seq + T_GD + T_RD) +
                                                   (t_sw + t_pm) * (n_ap + 1)) + t_fcs;

    cssm->t_sy = t_sy;
    cssm->t_sy_seq = t_sy_seq;

    cssm->subevent_interval_usecs = params->subevent_interval * BLE_LL_CS_SUBEVENTS_INTERVAL_UNIT_US;

    cssm->event_interval_usecs = params->event_interval * cssm->connsm->conn_itvl *
                                 BLE_LL_CONN_ITVL_USECS;

    cssm->procedure_interval_usecs = params->procedure_interval * cssm->connsm->conn_itvl *
                                     BLE_LL_CONN_ITVL_USECS;
    return 0;
}

static void
ble_ll_cs_set_step_duration(struct ble_ll_cs_sm *cssm)
{
    switch (cssm->step_mode) {
    case BLE_LL_CS_MODE0:
        cssm->step_duration_usecs = cssm->mode0_step_duration_usecs;
        break;
    case BLE_LL_CS_MODE1:
        cssm->step_duration_usecs = cssm->mode1_step_duration_usecs;
        break;
    case BLE_LL_CS_MODE2:
        cssm->step_duration_usecs = cssm->mode2_step_duration_usecs;
        break;
    case BLE_LL_CS_MODE3:
        cssm->step_duration_usecs = cssm->mode3_step_duration_usecs;
        break;
    default:
        BLE_LL_ASSERT(0);
    }
}

static int
ble_lL_cs_validate_step_duration(struct ble_ll_cs_sm *cssm)
{
    const struct ble_ll_cs_config *conf = cssm->active_config;
    const struct ble_ll_cs_proc_params *params = &conf->proc_params;
    uint32_t max_procedure_len = conf->proc_params.max_procedure_len;
    uint32_t max_procedure_len_usecs = max_procedure_len * BLE_LL_CS_PROCEDURE_LEN_UNIT_US;
    uint32_t max_subevent_len_usecs = conf->proc_params.subevent_len;
    uint32_t step_duration_usecs = cssm->step_duration_usecs;
    uint32_t total_subevent_usecs = cssm->step_anchor_usecs - cssm->subevent_anchor_usecs;
    uint32_t total_procedure_usecs = cssm->step_anchor_usecs - cssm->procedure_anchor_usecs;

    if (total_procedure_usecs + step_duration_usecs > max_procedure_len_usecs ||
        cssm->steps_in_procedure_count + 1 >= BLE_LL_CS_STEPS_PER_PROCEDURE_MAX) {
        /* The CS procedure is complete */

        /* A CS procedure is considered complete and closed when at least one of the following
         * conditions occurs:
         * • The execution of the next CS step in its entirety would cause the extent of the CS
         *   procedure to exceed T_MAX_PROCEDURE_LEN.
         * • The combined number of mode-0 steps and non-mode‑0 steps executed is equal to
         *   N_STEPS_MAX as described in [Vol 6] Part B, Section 4.5.18.1.
         * • The number of CS subevents executed is equal to
         *   N_MAX_SUBEVENTS_PER_PROCEDURE.
         * TODO:
         * • If Channel Selection Algorithm #3b is used for non-mode-0 steps, and the channel
         *   map generated from CSFilteredChM has been used for CSNumRepetitions cycles
         *   for non-mode‑0 steps including the use for both Main_Mode and Sub_Mode steps.
         * • If Channel Selection Algorithm #3c is used for non-mode-0 steps, and
         *   CSNumRepetitions invocations of Channel Selection Algorithm #3c have been
         *   completed for non-mode‑0 steps including the use for both Main_Mode and
         *   Sub_Mode steps.
         */

        return CS_SCHEDULE_NEW_PROCEDURE;
    }

    if (total_subevent_usecs + step_duration_usecs > max_subevent_len_usecs ||
        cssm->steps_in_subevent_count + 1 >= BLE_LL_CS_STEPS_PER_SUBEVENT_MAX) {

        if (cssm->subevents_in_procedure_count + 1 >= BLE_LL_CS_SUBEVENTS_PER_PROCEDURE_MAX) {
            return CS_SCHEDULE_NEW_PROCEDURE;
        }

        if (cssm->subevents_in_event_count + 1 >= params->subevents_per_event) {
            total_procedure_usecs = cssm->event_anchor_usecs + cssm->event_interval_usecs +
                                    cssm->mode0_step_duration_usecs - cssm->procedure_anchor_usecs;

            if (total_procedure_usecs > max_procedure_len_usecs) {
                return CS_SCHEDULE_NEW_PROCEDURE;
            }

            return CS_SCHEDULE_NEW_EVENT;
        }

        return CS_SCHEDULE_NEW_SUBEVENT;
    }

    return CS_SCHEDULE_NEW_STEP;
}

static int
ble_ll_cs_setup_next_subevent(struct ble_ll_cs_sm *cssm)
{
    const struct ble_ll_cs_proc_params *params = &cssm->active_config->proc_params;

    ++cssm->subevents_in_procedure_count;
    ++cssm->subevents_in_event_count;
    cssm->steps_in_subevent_count = 0;

    cssm->subevent_anchor_usecs += cssm->event_interval_usecs;
    cssm->step_anchor_usecs = cssm->subevent_anchor_usecs;

    return 0;
}

static int
ble_ll_cs_setup_next_event(struct ble_ll_cs_sm *cssm)
{
    const struct ble_ll_cs_proc_params *params = &cssm->active_config->proc_params;

    ++cssm->events_in_procedure_count;
    ++cssm->subevents_in_procedure_count;
    cssm->subevents_in_event_count = 0;
    cssm->steps_in_subevent_count = 0;

    cssm->event_anchor_usecs += cssm->event_interval_usecs;
    cssm->subevent_anchor_usecs = cssm->event_anchor_usecs;
    cssm->step_anchor_usecs = cssm->event_anchor_usecs;

    return 0;
}

static int
ble_ll_cs_setup_next_procedure(struct ble_ll_cs_sm *cssm)
{
    const struct ble_ll_cs_proc_params *params = &cssm->active_config->proc_params;

    ++cssm->procedure_count;
    cssm->events_in_procedure_count = 0;
    cssm->subevents_in_procedure_count = 0;
    cssm->subevents_in_event_count = 0;
    cssm->steps_in_procedure_count = 0;
    cssm->steps_in_subevent_count = 0;

    cssm->procedure_anchor_usecs += cssm->procedure_anchor_usecs;
    cssm->step_anchor_usecs = cssm->procedure_anchor_usecs;
    cssm->subevent_anchor_usecs = cssm->step_anchor_usecs;

    return ble_ll_cs_backtracking_resistance(cssm);
}

static int
ble_ll_cs_proc_subevent_next_state(struct ble_ll_cs_sm *cssm)
{
    int rc;
    const struct ble_ll_cs_config *conf = cssm->active_config;

    if (cssm->steps_in_subevent_count == 0) {
        ble_ll_cs_drbg_clear_cache(&cssm->drbg_ctx);
        cssm->mode0_step_count = conf->mode_0_steps;

        if (cssm->subevents_in_procedure_count == 0) {
            cssm->main_step_count = 0xFF;
        } else {
            cssm->repetition_count = conf->main_mode_repetition;
        }
    }

    if (cssm->mode0_step_count > 0) {
        cssm->subevent_state = SUBEVENT_STATE_MODE0_STEP;
        cssm->step_mode = BLE_LL_CS_MODE0;
        --cssm->mode0_step_count;

    } else if (cssm->repetition_count > 0) {
        cssm->subevent_state = SUBEVENT_STATE_REPETITION_STEP;
        cssm->step_mode = conf->main_mode;
        --cssm->repetition_count;

    } else if (cssm->main_step_count == 0) {
        cssm->subevent_state = SUBEVENT_STATE_SUBMODE_STEP;
        cssm->step_mode = conf->sub_mode;
        cssm->main_step_count = 0xFF;

    } else {
        cssm->subevent_state = SUBEVENT_STATE_MAINMODE_STEP;
        cssm->step_mode = conf->main_mode;
    }

    ble_ll_cs_set_step_duration(cssm);

    rc = ble_lL_cs_validate_step_duration(cssm);
    if (rc) {
        /* Step does not fit in current subevent. */
        switch (rc) {
        case CS_SCHEDULE_NEW_SUBEVENT:
            ble_ll_cs_setup_next_subevent(cssm);
            cssm->cs_schedule_status = CS_SCHEDULE_NEW_SUBEVENT;
            break;
        case CS_SCHEDULE_NEW_EVENT:
            ble_ll_cs_setup_next_event(cssm);
            cssm->cs_schedule_status = CS_SCHEDULE_NEW_EVENT;
            break;
        case CS_SCHEDULE_NEW_PROCEDURE:
            if (conf->proc_params.max_procedure_count <= cssm->procedure_count + 1 ||
                cssm->terminate_measurement) {
                /* All CS procedures have been completed or
                 * the CS procedure repeat series has been terminated.
                 */
                cssm->cs_schedule_status = CS_SCHEDULE_COMPLETED;

                if (cssm->terminate_measurement) {
                    cssm->proc_abort_reason = PROC_ABORT_REQUESTED;
                }

                return 0;
            }

            ble_ll_cs_setup_next_procedure(cssm);
            cssm->cs_schedule_status = CS_SCHEDULE_NEW_PROCEDURE;
            break;
        default:
            BLE_LL_ASSERT(0);
        }

        cssm->step_mode = BLE_LL_CS_MODE0;
        ble_ll_cs_set_step_duration(cssm);

        if (ble_lL_cs_validate_step_duration(cssm) != CS_SCHEDULE_NEW_STEP) {
            return 1;
        }

        return 0;
    }

    cssm->cs_schedule_status = CS_SCHEDULE_NEW_STEP;

    if (cssm->subevent_state == SUBEVENT_STATE_MAINMODE_STEP &&
        conf->sub_mode != 0xFF) {
        if (cssm->main_step_count == 0xFF) {
            /* Rand the number of Main_Mode steps to execute
             * before a Sub_Mode insertion.
             */
            rc = ble_ll_cs_drbg_rand_main_mode_steps(
                &cssm->drbg_ctx, cssm->steps_in_procedure_count,
                conf->main_mode_min_steps, conf->main_mode_max_steps,
                &cssm->main_step_count);

            if (rc) {
                return rc;
            }
        }

        --cssm->main_step_count;
    }

    return 0;
}

static int
ble_ll_cs_setup_next_step(struct ble_ll_cs_sm *cssm)
{
    int rc;
    const struct ble_ll_cs_config *conf = cssm->active_config;

    cssm->step_anchor_usecs = cssm->anchor_usecs;
    ++cssm->steps_in_procedure_count;
    ++cssm->steps_in_subevent_count;

    rc = ble_ll_cs_proc_subevent_next_state(cssm);
    if (rc || cssm->cs_schedule_status == CS_SCHEDULE_COMPLETED) {
        return rc;
    }

    if (cssm->subevent_state != SUBEVENT_STATE_REPETITION_STEP) {
        rc = ble_ll_cs_generate_channel(cssm);
        if (rc) {
            return rc;
        }

        /* Update channel cache used in repetition steps */
        if (cssm->subevent_state == SUBEVENT_STATE_MAINMODE_STEP &&
            conf->main_mode_repetition) {
            cssm->repetition_channels[0] = cssm->repetition_channels[1];
            cssm->repetition_channels[1] = cssm->repetition_channels[2];
            cssm->repetition_channels[2] = cssm->channel;
        }
    } else {
        cssm->channel = cssm->repetition_channels[
            conf->main_mode_repetition - cssm->repetition_count];
    }

    /* Generate CS Access Address */
    rc = ble_ll_cs_drbg_generate_aa(&cssm->drbg_ctx,
                                    cssm->steps_in_procedure_count,
                                    &cssm->initiator_aa, &cssm->reflector_aa);
    if (rc) {
        return rc;
    }

    if (conf->role == BLE_LL_CS_ROLE_INITIATOR) {
        cssm->tx_aa = cssm->initiator_aa;
        cssm->rx_aa = cssm->reflector_aa;
    } else { /* BLE_LL_CS_ROLE_REFLECTOR */
        cssm->rx_aa = cssm->initiator_aa;
        cssm->tx_aa = cssm->reflector_aa;
    }

    cssm->antenna_path_count = cssm->n_ap;
    cssm->anchor_usecs = cssm->step_anchor_usecs;

    /* TODO: Generate antenna ID if multiple antennas available */
    cssm->cs_sync_antenna = 0x01;

    rc = ble_ll_cs_drbg_rand_tone_ext_presence(
        &cssm->drbg_ctx, cssm->steps_in_procedure_count, &cssm->tone_ext_presence_i);

    if (rc) {
        return rc;
    }

    rc = ble_ll_cs_drbg_rand_tone_ext_presence(
        &cssm->drbg_ctx, cssm->steps_in_procedure_count, &cssm->tone_ext_presence_r);

    return rc;
}

static void
ble_ll_cs_proc_mode0_next_state(struct ble_ll_cs_sm *cssm)
{
    uint32_t duration = 0;
    uint32_t delay = 0;
    uint8_t state = cssm->step_state;
    uint8_t t_ip1 = cssm->active_config->t_ip1;
    uint8_t t_fcs = cssm->active_config->t_fcs;
    uint8_t t_sy = cssm->t_sy;

    switch (state) {
    case STEP_STATE_INIT:
        state = STEP_STATE_CS_SYNC_I;
        duration = t_sy;
        delay = 0;
        break;
    case STEP_STATE_CS_SYNC_I:
        state = STEP_STATE_CS_SYNC_R;
        duration = t_sy;
        delay = T_RD + t_ip1;
        break;
    case STEP_STATE_CS_SYNC_R:
        state = STEP_STATE_CS_TONE_R;
        duration = T_FM;
        delay = T_GD;
        break;
    case STEP_STATE_CS_TONE_R:
        state = STEP_STATE_COMPLETE;
        duration = 0;
        delay = T_RD + t_fcs;
        break;
    default:
        BLE_LL_ASSERT(0);
    }

    cssm->duration_usecs = duration;
    cssm->anchor_usecs += delay;
    cssm->step_state = state;
}

static void
ble_ll_cs_proc_mode1_next_state(struct ble_ll_cs_sm *cssm)
{
    uint32_t duration = 0;
    uint32_t delay = 0;
    uint8_t state = cssm->step_state;
    uint8_t t_ip1 = cssm->active_config->t_ip1;
    uint8_t t_fcs = cssm->active_config->t_fcs;
    uint8_t t_sy = cssm->t_sy + cssm->t_sy_seq;

    switch (state) {
    case STEP_STATE_INIT:
        state = STEP_STATE_CS_SYNC_I;
        duration = t_sy;
        delay = 0;
        break;
    case STEP_STATE_CS_SYNC_I:
        state = STEP_STATE_CS_SYNC_R;
        duration = t_sy;
        delay = T_RD + t_ip1;
        break;
    case STEP_STATE_CS_SYNC_R:
        state = STEP_STATE_COMPLETE;
        duration = 0;
        delay = T_RD + t_fcs;
        break;
    default:
        BLE_LL_ASSERT(0);
    }

    cssm->duration_usecs = duration;
    cssm->anchor_usecs += delay;
    cssm->step_state = state;
}

static void
ble_ll_cs_proc_mode2_next_state(struct ble_ll_cs_sm *cssm)
{
    uint32_t duration = 0;
    uint32_t delay = 0;
    uint8_t state = cssm->step_state;
    uint8_t t_ip2 = cssm->active_config->t_ip2;
    uint8_t t_fcs = cssm->active_config->t_fcs;
    uint8_t t_pm = cssm->active_config->t_pm;

    switch (state) {
    case STEP_STATE_INIT:
        state = STEP_STATE_CS_TONE_I;
        duration = t_pm;
        delay = 0;
        break;
    case STEP_STATE_CS_TONE_I:
        duration = t_pm;
        delay = cssm->t_sw;
        if (cssm->antenna_path_count != 0) {
            --cssm->antenna_path_count;
        } else {
            state = STEP_STATE_CS_TONE_EXT_I;
        }
        break;
    case STEP_STATE_CS_TONE_EXT_I:
        state = STEP_STATE_CS_TONE_R;
        cssm->antenna_path_count = cssm->n_ap;
        delay = T_RD + t_ip2;
        break;
    case STEP_STATE_CS_TONE_R:
        duration = t_pm;
        delay = cssm->t_sw;
        if (cssm->antenna_path_count != 0) {
            --cssm->antenna_path_count;
        } else {
            state = STEP_STATE_CS_TONE_EXT_R;
        }
        break;
    case STEP_STATE_CS_TONE_EXT_R:
        state = STEP_STATE_COMPLETE;
        cssm->antenna_path_count = cssm->n_ap;
        duration = 0;
        delay = T_RD + t_fcs;
        break;
    default:
        BLE_LL_ASSERT(0);
    }

    cssm->duration_usecs = duration;
    cssm->anchor_usecs += delay;
    cssm->step_state = state;
}

static void
ble_ll_cs_proc_mode3_next_state(struct ble_ll_cs_sm *cssm)
{
    uint32_t duration = 0;
    uint32_t delay = 0;
    uint8_t state = cssm->step_state;
    uint8_t t_ip2 = cssm->active_config->t_ip2;
    uint8_t t_fcs = cssm->active_config->t_fcs;
    uint8_t t_sy = cssm->t_sy + cssm->t_sy_seq;
    uint8_t t_pm = cssm->active_config->t_pm;

    switch (state) {
    case STEP_STATE_INIT:
        state = STEP_STATE_CS_SYNC_I;
        duration = t_sy;
        delay = 0;
        break;
    case STEP_STATE_CS_SYNC_I:
        state = STEP_STATE_CS_TONE_I;
        duration = t_pm;
        delay = T_GD;
        break;
    case STEP_STATE_CS_TONE_I:
        duration = t_pm;
        delay = cssm->t_sw;
        if (cssm->antenna_path_count != 0) {
            --cssm->antenna_path_count;
        } else {
            state = STEP_STATE_CS_TONE_EXT_I;
        }
        break;
    case STEP_STATE_CS_TONE_EXT_I:
        state = STEP_STATE_CS_TONE_R;
        cssm->antenna_path_count = cssm->n_ap;
        delay = T_RD + t_ip2;
        break;
    case STEP_STATE_CS_TONE_R:
        duration = t_pm;
        delay = cssm->t_sw;
        if (cssm->antenna_path_count != 0) {
            --cssm->antenna_path_count;
        } else {
            state = STEP_STATE_CS_TONE_EXT_R;
        }
        break;
    case STEP_STATE_CS_TONE_EXT_R:
        state = STEP_STATE_CS_SYNC_R;
        cssm->antenna_path_count = cssm->n_ap;
        duration = t_sy;
        delay = T_RD;
        break;
    case STEP_STATE_CS_SYNC_R:
        state = STEP_STATE_COMPLETE;
        duration = 0;
        delay = T_RD + t_fcs;
        break;
    default:
        BLE_LL_ASSERT(0);
    }

    cssm->duration_usecs = duration;
    cssm->anchor_usecs += delay;
    cssm->step_state = state;
}

static void
ble_ll_cs_proc_step_next_state(struct ble_ll_cs_sm *cssm)
{
    switch (cssm->step_mode) {
    case BLE_LL_CS_MODE0:
        ble_ll_cs_proc_mode0_next_state(cssm);
        break;
    case BLE_LL_CS_MODE1:
        ble_ll_cs_proc_mode1_next_state(cssm);
        break;
    case BLE_LL_CS_MODE2:
        ble_ll_cs_proc_mode2_next_state(cssm);
        break;
    case BLE_LL_CS_MODE3:
        ble_ll_cs_proc_mode3_next_state(cssm);
        break;
    default:
        BLE_LL_ASSERT(0);
    }
}

static int
ble_ll_cs_proc_next_state(struct ble_ll_cs_sm *cssm)
{
    int rc;

    if (cssm->step_state != STEP_STATE_INIT) {
        ble_ll_cs_proc_step_next_state(cssm);

        if (cssm->step_state != STEP_STATE_COMPLETE) {
            /* Continue pending step */
            return 0;
        }

        /* Save step results */
        ble_ll_cs_proc_add_step_result(cssm);
    }

    /* Setup a new step */

    cssm->step_state = STEP_STATE_INIT;

    rc = ble_ll_cs_setup_next_step(cssm);
    if (rc) {
        if (!cssm->proc_abort_reason) {
            cssm->proc_abort_reason = PROC_ABORT_UNSPECIFIED;
        }

        cssm->cs_schedule_status = CS_SCHEDULE_COMPLETED;
    }

    if (cssm->cs_schedule_status == CS_SCHEDULE_COMPLETED) {
        rc = ble_ll_cs_proc_queue_subevent(&cssm->buffered_subevent, cssm->cs_schedule_status,
                                           cssm->proc_abort_reason, cssm->subev_abort_reason);
        BLE_LL_ASSERT(rc == 0);

        return 1;
    }

    ble_ll_cs_proc_step_next_state(cssm);

    if (cssm->cs_schedule_status != CS_SCHEDULE_NEW_STEP) {
        rc = ble_ll_cs_proc_queue_subevent(&cssm->buffered_subevent, cssm->cs_schedule_status,
                                           cssm->proc_abort_reason, cssm->subev_abort_reason);
        BLE_LL_ASSERT(rc == 0);

        rc = ble_ll_cs_init_subevent(&cssm->buffered_subevent, cssm);
        BLE_LL_ASSERT(rc == 0);
    }

    return 0;
}

static int
ble_ll_cs_proc_skip_txrx(struct ble_ll_cs_sm *cssm)
{
    int rc;

    rc = ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
    (void) rc;

    return 0;
}

static ble_ll_cs_sched_cb_func
ble_ll_cs_proc_sched_cb_get(struct ble_ll_cs_sm *cssm)
{
    ble_ll_cs_sched_cb_func cb;

    cssm->rx_window_offset_usecs = 0;

    if (cssm->active_config->role == BLE_LL_CS_ROLE_INITIATOR) {
        switch (cssm->step_state) {
        case STEP_STATE_CS_SYNC_I:
            cb = ble_ll_cs_sync_tx_start;
            break;
        case STEP_STATE_CS_SYNC_R:
            cb = ble_ll_cs_sync_rx_start;
            cssm->rx_window_offset_usecs = 2;
            break;
        case STEP_STATE_CS_TONE_I:
            cb = ble_ll_cs_tone_tx_start;
            break;
        case STEP_STATE_CS_TONE_R:
            cb = ble_ll_cs_tone_rx_start;
            break;
        case STEP_STATE_CS_TONE_EXT_I:
            if (cssm->tone_ext_presence_i) {
                cb = ble_ll_cs_tone_tx_start;
            } else {
                cb = ble_ll_cs_proc_skip_txrx;
            }
            break;
        case STEP_STATE_CS_TONE_EXT_R:
            if (cssm->tone_ext_presence_r) {
                cb = ble_ll_cs_tone_rx_start;
            } else {
                cb = ble_ll_cs_proc_skip_txrx;
            }
            break;
        default:
            BLE_LL_ASSERT(0);
        }
    } else { /* BLE_LL_CS_ROLE_REFLECTOR */
        switch (cssm->step_state) {
        case STEP_STATE_CS_SYNC_I:
            cb = ble_ll_cs_sync_rx_start;
            cssm->rx_window_offset_usecs = 2;
            break;
        case STEP_STATE_CS_SYNC_R:
            cb = ble_ll_cs_sync_tx_start;
            break;
        case STEP_STATE_CS_TONE_I:
            cb = ble_ll_cs_tone_rx_start;
            break;
        case STEP_STATE_CS_TONE_R:
            cb = ble_ll_cs_tone_tx_start;
            break;
        case STEP_STATE_CS_TONE_EXT_I:
            if (cssm->tone_ext_presence_i) {
                cb = ble_ll_cs_tone_rx_start;
            } else {
                cb = ble_ll_cs_proc_skip_txrx;
            }
            break;
        case STEP_STATE_CS_TONE_EXT_R:
            if (cssm->tone_ext_presence_r) {
                cb = ble_ll_cs_tone_tx_start;
            } else {
                cb = ble_ll_cs_proc_skip_txrx;
            }
            break;
        default:
            BLE_LL_ASSERT(0);
        }
    }

    return cb;
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
    ble_ll_cs_sched_cb_func cb;

    rc = ble_ll_cs_proc_next_state(cssm);
    if (rc) {
        return rc;
    }

    cb = ble_ll_cs_proc_sched_cb_get(cssm);
    cssm->anchor_usecs -= cssm->rx_window_offset_usecs;
    cssm->anchor_cputime = ble_ll_tmr_u2t_r(cssm->anchor_usecs, &cssm->anchor_rem_usecs);

    if (cssm->anchor_cputime - g_ble_ll_sched_offset_ticks > ble_ll_tmr_get()) {
        cssm->sch.start_time = cssm->anchor_cputime - g_ble_ll_sched_offset_ticks;
    } else {
        cssm->sch.start_time = ble_ll_tmr_get();
    }

    cssm->sched_cb = cb;
    cssm->sch.end_time = cssm->sch.start_time + ble_ll_tmr_u2t_up(cssm->duration_usecs);
    cssm->sch.remainder = 0;
    cssm->sch.sched_type = BLE_LL_SCHED_TYPE_CS;
    cssm->sch.cb_arg = cssm;
    cssm->sch.sched_cb = ble_ll_cs_proc_sched_cb;

    rc = ble_ll_sched_cs_proc(&cssm->sch);

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

    ble_ll_conn_get_anchor(connsm, params->anchor_conn_event,
                           &anchor_ticks, &anchor_rem_usecs);

    ble_ll_tmr_add(&anchor_ticks, &anchor_rem_usecs, params->event_offset);

    if (anchor_ticks - g_ble_ll_sched_offset_ticks < ble_ll_tmr_get()) {
        /* The start happend too late for the negotiated event counter. */
        return BLE_ERR_INV_LMP_LL_PARM;
    }

    g_ble_ll_cs_sm_current = cssm;
    cssm->anchor_usecs = ble_ll_tmr_t2u(anchor_ticks);
    cssm->step_mode = BLE_LL_CS_MODE0;
    cssm->step_state = STEP_STATE_INIT;
    cssm->n_ap = aci_table[params->aci].n_ap;
    cssm->procedure_anchor_usecs = cssm->anchor_usecs;
    cssm->event_anchor_usecs = cssm->anchor_usecs;
    cssm->subevent_anchor_usecs = cssm->anchor_usecs;
    cssm->step_anchor_usecs = cssm->anchor_usecs;

    cssm->steps_in_procedure_count = ~0;
    cssm->steps_in_subevent_count = ~0;
    cssm->procedure_count = 0;

    cssm->mode0_next_chan_id = 0xFF;
    cssm->non_mode0_next_chan_id = 0xFF;

    if (g_ble_ll_cs_local_cap.sounding_pct_estimate &&
        cssm->active_config->rtt_type != BLE_LL_CS_RTT_AA_ONLY) {
        cssm->step_result.sounding_pct_estimate = 1;
    }

    ble_ll_cs_proc_calculate_timing(cssm);

    rc = ble_ll_cs_init_subevent(&cssm->buffered_subevent, cssm);
    BLE_LL_ASSERT(rc == 0);

    rc = ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
    if (rc) {
        return BLE_ERR_UNSPECIFIED;
    }

    return BLE_ERR_SUCCESS;
}

void
ble_ll_cs_proc_sync_lost(struct ble_ll_cs_sm *cssm)
{
    ble_phy_disable();
    ble_phy_cs_sync_mode_set(0);
    ble_ll_state_set(BLE_LL_STATE_STANDBY);

    ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
}
#endif /* BLE_LL_CHANNEL_SOUNDING */
