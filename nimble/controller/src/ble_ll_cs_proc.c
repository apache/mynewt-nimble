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

/* States within step */
#define STEP_STATE_INIT          (0)
#define STEP_STATE_CS_SYNC_I     (1)
#define STEP_STATE_CS_SYNC_R     (2)
#define STEP_STATE_CS_TONE_I     (3)
#define STEP_STATE_CS_TONE_R     (4)
#define STEP_STATE_CS_TONE_EXT_I (5)
#define STEP_STATE_CS_TONE_EXT_R (6)
#define STEP_STATE_COMPLETE      (7)

static struct ble_ll_cs_aci aci_table[] = {
    {1, 1, 1}, {2, 2, 1}, {3, 3, 1}, {4, 4, 1},
    {2, 1, 2}, {3, 1, 3}, {4, 1, 4}, {4, 2, 2}
};

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
ble_ll_cs_setup_next_step(struct ble_ll_cs_sm *cssm)
{
    /* TODO: Setup new CS step */

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
        BLE_LL_ASSERT(0);
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
        BLE_LL_ASSERT(0);
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
            state = STEP_STATE_CS_TONE_EXT_I;
        }
        break;
    case STEP_STATE_CS_TONE_EXT_I:
        state = STEP_STATE_CS_TONE_R;
        cssm->antenna_path_count = cssm->n_ap;
        break;
    case STEP_STATE_CS_TONE_R:
        if (cssm->antenna_path_count != 0) {
            --cssm->antenna_path_count;
        } else {
            state = STEP_STATE_CS_TONE_EXT_R;
        }
        break;
    case STEP_STATE_CS_TONE_EXT_R:
        state = STEP_STATE_COMPLETE;
        cssm->antenna_path_count = cssm->n_ap;
        break;
    default:
        BLE_LL_ASSERT(0);
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
            state = STEP_STATE_CS_TONE_EXT_I;
        }
        break;
    case STEP_STATE_CS_TONE_EXT_I:
        state = STEP_STATE_CS_TONE_R;
        cssm->antenna_path_count = cssm->n_ap;
        break;
    case STEP_STATE_CS_TONE_R:
        if (cssm->antenna_path_count != 0) {
            --cssm->antenna_path_count;
        } else {
            state = STEP_STATE_CS_TONE_EXT_R;
        }
        break;
    case STEP_STATE_CS_TONE_EXT_R:
        state = STEP_STATE_CS_SYNC_R;
        cssm->antenna_path_count = cssm->n_ap;
        break;
    case STEP_STATE_CS_SYNC_R:
        state = STEP_STATE_COMPLETE;
        break;
    default:
        BLE_LL_ASSERT(0);
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
    ble_ll_state_set(BLE_LL_STATE_STANDBY);

    ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
}
#endif /* BLE_LL_CHANNEL_SOUNDING */
