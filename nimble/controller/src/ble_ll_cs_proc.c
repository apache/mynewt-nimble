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
#include "controller/ble_ll_conn.h"
#include "controller/ble_ll_sched.h"
#include "controller/ble_ll_tmr.h"
#include "ble_ll_cs_priv.h"

struct ble_ll_cs_sm *g_ble_ll_cs_sm_current;

#define BLE_LL_CS_MODE0 (0)
#define BLE_LL_CS_MODE1 (1)
#define BLE_LL_CS_MODE2 (2)
#define BLE_LL_CS_MODE3 (3)

/* States within step */
#define STEP_STATE_INIT       (0)
#define STEP_STATE_CS_SYNC_I  (1)
#define STEP_STATE_CS_SYNC_R  (2)
#define STEP_STATE_CS_TONE_I  (3)
#define STEP_STATE_CS_TONE_R  (4)
#define STEP_STATE_COMPLETE   (5)

#define SUBEVENT_STATE_MODE0_STEP      (0)
#define SUBEVENT_STATE_REPETITION_STEP (1)
#define SUBEVENT_STATE_SUBMODE_STEP    (2)
#define SUBEVENT_STATE_MAINMODE_STEP   (3)

#define CS_SCHEDULE_NEW_STEP      (0)
#define CS_SCHEDULE_NEW_SUBEVENT  (1)
#define CS_SCHEDULE_NEW_EVENT     (2)
#define CS_SCHEDULE_NEW_PROCEDURE (3)

#define BLE_LL_CONN_ITVL_USECS    (1250)

static struct ble_ll_cs_aci aci_table[] = {
    {1, 1, 1}, {2, 2, 1}, {3, 3, 1}, {4, 4, 1},
    {2, 1, 2}, {3, 1, 3}, {4, 1, 4}, {4, 2, 2}
};

static int
ble_ll_cs_generate_channel(struct ble_ll_cs_sm *cssm)
{
    /* TODO: cssm->channel = ? */
    return 0;
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

static void
ble_ll_cs_set_step_duration(struct ble_ll_cs_sm *cssm)
{
    /* TODO: cssm->step_duration_usecs = ? */
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
        cssm->steps_in_procedure_count >= BLE_LL_CS_STEPS_PER_PROCEDURE_MAX) {
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
        cssm->steps_in_subevent_count >= BLE_LL_CS_STEPS_PER_SUBEVENT_MAX) {

        if (cssm->subevents_in_procedure_count >= BLE_LL_CS_SUBEVENTS_PER_PROCEDURE_MAX) {
            return CS_SCHEDULE_NEW_PROCEDURE;
        }

        if (cssm->subevents_in_event_count >= params->subevents_per_event) {
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

    cssm->step_anchor_usecs = cssm->procedure_anchor_usecs +
                              cssm->events_in_procedure_count * params->event_interval +
                              cssm->subevents_in_event_count * params->subevent_interval;
    cssm->subevent_anchor_usecs = cssm->step_anchor_usecs;

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

    cssm->step_anchor_usecs = cssm->procedure_anchor_usecs +
                              cssm->events_in_procedure_count * params->event_interval;
    cssm->subevent_anchor_usecs = cssm->step_anchor_usecs;

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

    cssm->procedure_anchor_usecs += params->procedure_interval *
                                    cssm->connsm->conn_itvl *
                                    BLE_LL_CONN_ITVL_USECS;

    cssm->step_anchor_usecs = cssm->procedure_anchor_usecs;
    cssm->subevent_anchor_usecs = cssm->step_anchor_usecs;

    return 0;
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
            break;
        case CS_SCHEDULE_NEW_EVENT:
            ble_ll_cs_setup_next_event(cssm);
            break;
        case CS_SCHEDULE_NEW_PROCEDURE:
            if (conf->proc_params.max_procedure_count <= cssm->procedure_count + 1) {
                /* All CS procedures have been completed */
                return 1;
            }

            ble_ll_cs_setup_next_procedure(cssm);
            break;
        default:
            assert(0);
        }

        cssm->step_mode = BLE_LL_CS_MODE0;
        ble_ll_cs_set_step_duration(cssm);

        return ble_lL_cs_validate_step_duration(cssm);
    }

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

    ++cssm->steps_in_procedure_count;
    ++cssm->steps_in_subevent_count;

    rc = ble_ll_cs_proc_subevent_next_state(cssm);
    if (rc) {
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
    return 0;
}

static void
ble_ll_cs_proc_mode0_next_state(struct ble_ll_cs_sm *cssm)
{
    uint8_t state = cssm->step_state;

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
        assert(0);
    }

    cssm->step_state = state;
}

static void
ble_ll_cs_proc_mode1_next_state(struct ble_ll_cs_sm *cssm)
{
    uint8_t state = cssm->step_state;

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
        assert(0);
    }

    cssm->step_state = state;
}

static void
ble_ll_cs_proc_mode2_next_state(struct ble_ll_cs_sm *cssm)
{
    uint8_t state = cssm->step_state;

    switch (state) {
    case STEP_STATE_INIT:
        state = STEP_STATE_CS_TONE_I;
        break;
    case STEP_STATE_CS_TONE_I:
        if (cssm->antenna_path_count != 0) {
            --cssm->antenna_path_count;
        } else {
            state = STEP_STATE_CS_TONE_R;
            cssm->antenna_path_count = cssm->n_ap;
        }
        break;
    case STEP_STATE_CS_TONE_R:
        if (cssm->antenna_path_count != 0) {
            --cssm->antenna_path_count;
        } else {
            state = STEP_STATE_COMPLETE;
            cssm->antenna_path_count = cssm->n_ap;
        }
        break;
    default:
        assert(0);
    }

    cssm->step_state = state;
}

static void
ble_ll_cs_proc_mode3_next_state(struct ble_ll_cs_sm *cssm)
{
    uint8_t state = cssm->step_state;

    switch (state) {
    case STEP_STATE_INIT:
        state = STEP_STATE_CS_SYNC_I;
        break;
    case STEP_STATE_CS_SYNC_I:
        state = STEP_STATE_CS_TONE_I;
        break;
    case STEP_STATE_CS_TONE_I:
        if (cssm->antenna_path_count != 0) {
            --cssm->antenna_path_count;
        } else {
            state = STEP_STATE_CS_TONE_R;
            cssm->antenna_path_count = cssm->n_ap;
        }
        break;
    case STEP_STATE_CS_TONE_R:
        if (cssm->antenna_path_count != 0) {
            --cssm->antenna_path_count;
        } else {
            state = STEP_STATE_CS_SYNC_R;
            cssm->antenna_path_count = cssm->n_ap;
        }
        break;
    case STEP_STATE_CS_SYNC_R:
        state = STEP_STATE_COMPLETE;
        break;
    default:
        assert(0);
    }

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
        assert(0);
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

        /* TODO: Send step results */
    }

    /* Setup a new step */

    cssm->step_state = STEP_STATE_INIT;

    rc = ble_ll_cs_setup_next_step(cssm);
    if (rc) {
        return rc;
    }

    ble_ll_cs_proc_step_next_state(cssm);

    return 0;
}

static sched_cb_func
ble_ll_cs_proc_sched_cb_get(struct ble_ll_cs_sm *cssm)
{
    sched_cb_func cb;

    if (cssm->active_config->role == BLE_LL_CS_ROLE_INITIATOR) {
        switch (cssm->step_state) {
        case STEP_STATE_CS_SYNC_I:
            cb = ble_ll_cs_sync_tx_sched_cb;
            break;
        case STEP_STATE_CS_SYNC_R:
            cb = ble_ll_cs_sync_rx_sched_cb;
            break;
        case STEP_STATE_CS_TONE_I:
            cb = ble_ll_cs_tone_tx_sched_cb;
            break;
        case STEP_STATE_CS_TONE_R:
            cb = ble_ll_cs_tone_rx_sched_cb;
            break;
        default:
            assert(0);
        }
    } else { /* BLE_LL_CS_ROLE_REFLECTOR */
        switch (cssm->step_state) {
        case STEP_STATE_CS_SYNC_I:
            cb = ble_ll_cs_sync_rx_sched_cb;
            break;
        case STEP_STATE_CS_SYNC_R:
            cb = ble_ll_cs_sync_tx_sched_cb;
            break;
        case STEP_STATE_CS_TONE_I:
            cb = ble_ll_cs_tone_rx_sched_cb;
            break;
        case STEP_STATE_CS_TONE_R:
            cb = ble_ll_cs_tone_tx_sched_cb;
            break;
        default:
            assert(0);
        }
    }

    return cb;
}

int
ble_ll_cs_proc_schedule_next_tx_or_rx(struct ble_ll_cs_sm *cssm)
{
    int rc;

    rc = ble_ll_cs_proc_next_state(cssm);
    if (rc) {
        return rc;
    }

    cssm->sch.start_time = ble_ll_tmr_u2t_up(cssm->anchor_usecs) - g_ble_ll_sched_offset_ticks;
    cssm->sch.end_time = cssm->sch.start_time + ble_ll_tmr_u2t_up(cssm->duration_usecs);
    cssm->sch.remainder = 0;
    cssm->sch.sched_type = BLE_LL_SCHED_TYPE_CS;
    cssm->sch.cb_arg = cssm;
    cssm->sch.sched_cb = ble_ll_cs_proc_sched_cb_get(cssm);

    rc = ble_ll_sched_cs_proc(&cssm->sch);

    return rc;
}

void
ble_ll_cs_proc_set_now_as_anchor_point(struct ble_ll_cs_sm *cssm)
{
    cssm->anchor_usecs = ble_ll_tmr_t2u(ble_ll_tmr_get());
}

int
ble_ll_cs_proc_scheduling_start(struct ble_ll_conn_sm *connsm,
                                struct ble_ll_cs_config *conf)
{
    int rc;
    struct ble_ll_cs_sm *cssm = connsm->cssm;
    const struct ble_ll_cs_proc_params *params = &conf->proc_params;
    uint32_t anchor_ticks;
    uint8_t anchor_rem_usecs;

    cssm->active_config = conf;

    ble_ll_conn_get_anchor(connsm, conf->proc_params.anchor_conn_event,
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
    cssm->n_ap = aci_table[cssm->active_config->proc_params.aci].n_ap;
    cssm->procedure_anchor_usecs = cssm->anchor_usecs;
    cssm->subevent_anchor_usecs = cssm->anchor_usecs;
    cssm->step_anchor_usecs = cssm->anchor_usecs;

    cssm->steps_in_procedure_count = ~0;
    cssm->steps_in_subevent_count = ~0;

    rc = ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
    if (rc) {
        return BLE_ERR_UNSPECIFIED;
    }

    return BLE_ERR_SUCCESS;
}

void
ble_ll_cs_proc_current_sm_over(void)
{
    /* Disable the PHY */
    ble_phy_disable();

    /* Link-layer is in standby state now */
    ble_ll_state_set(BLE_LL_STATE_STANDBY);

    /* Set current LL sync to NULL */
    g_ble_ll_cs_sm_current = NULL;
}
#endif /* BLE_LL_CHANNEL_SOUNDING */
