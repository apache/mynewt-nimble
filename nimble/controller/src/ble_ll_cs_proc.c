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

int
ble_ll_cs_proc_schedule_next_tx_or_rx(struct ble_ll_cs_sm *cssm)
{
    int rc;

    /* TODO: Setup CS step TX or RX */

    cssm->sch.start_time = ble_ll_tmr_u2t_up(cssm->anchor_usecs) - g_ble_ll_sched_offset_ticks;
    cssm->sch.end_time = cssm->sch.start_time + ble_ll_tmr_u2t_up(cssm->duration_usecs);
    cssm->sch.remainder = 0;
    cssm->sch.sched_type = BLE_LL_SCHED_TYPE_CS;
    cssm->sch.cb_arg = cssm;
    /* TODO: cssm->sch.sched_cb = */

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
