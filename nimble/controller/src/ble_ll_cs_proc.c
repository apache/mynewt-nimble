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

#define SUBEVENT_STATE_MODE0_STEP      (0)
#define SUBEVENT_STATE_REPETITION_STEP (1)
#define SUBEVENT_STATE_SUBMODE_STEP    (2)
#define SUBEVENT_STATE_MAINMODE_STEP   (3)

static struct ble_ll_cs_aci aci_table[] = {
    {1, 1, 1}, {2, 2, 1}, {3, 3, 1}, {4, 4, 1},
    {2, 1, 2}, {3, 1, 3}, {4, 1, 4}, {4, 2, 2}
};

/* A pattern containing the states and transitions of the current step */
static struct ble_ll_cs_step_transmission transmission_pattern[100];
static struct ble_ll_cs_step subevent_steps[40];

void ble_ll_cs_sync_rx_end(struct ble_ll_cs_sm *cssm, uint8_t *rxbuf, struct ble_mbuf_hdr *rxhdr);
void ble_ll_cs_tone_rx_end_cb(struct ble_ll_cs_sm *cssm);

static int
ble_ll_cs_generate_channel(struct ble_ll_cs_sm *cssm, struct ble_ll_cs_step *step,
                           uint16_t steps_in_procedure_count)
{
    /* TODO: step->channel = ? */
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

    if (cssm->procedure_count + 1 >= conf->proc_params.max_procedure_count) {
        return 1;
    }

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

    return 0;
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
        return rc;
    }

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
