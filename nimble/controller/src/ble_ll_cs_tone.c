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
#include "controller/ble_ll.h"
#include "controller/ble_ll_sched.h"
#include "controller/ble_ll_tmr.h"
#include "ble_ll_cs_priv.h"

extern struct ble_ll_cs_sm *g_ble_ll_cs_sm_current;

/**
 * Called when a CS tone has been transmitted.
 *
 * Context: interrupt
 */
void
ble_ll_cs_tone_tx_end_cb(void *arg)
{
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;

    assert(cssm != NULL);
    ble_ll_cs_proc_set_now_as_anchor_point(cssm);
    ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
}

int
ble_ll_cs_tone_tx_start(struct ble_ll_cs_sm *cssm)
{
    /* TODO: Start TX of CS tones */

    cssm->anchor_usecs += cssm->duration_usecs;

    return ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
}

int
ble_ll_cs_tone_rx_start(struct ble_ll_cs_sm *cssm)
{
    /* TODO: Start RX of CS tones */

    cssm->anchor_usecs += cssm->duration_usecs;

    return ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
}

/**
 * Called when a receive PDU has started.
 * Check if the frame is the next expected CS_SYNC packet.
 *
 * Context: interrupt
 *
 * @return int
 *   < 0: A frame we dont want to receive.
 *   = 0: Continue to receive frame. Dont go from rx to tx
 */
int
ble_ll_cs_tone_rx_isr_start(void)
{
    return 0;
}

/**
 * Called when received a complete CS_TONE packet.
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
ble_ll_cs_tone_rx_isr_end(void)
{
    uint8_t i;
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;

    assert(cssm != NULL);
    ble_ll_cs_proc_set_now_as_anchor_point(cssm);

    ble_phy_disable();
    ble_ll_state_set(BLE_LL_STATE_STANDBY);

    if (cssm->step_mode == BLE_LL_CS_MODE0) {
        /* TODO: Read measured frequency offset.
         * For now set "Frequency offset is not available".
         */
        cssm->step_result.measured_freq_offset = 0xC000;
    } else if (cssm->step_mode == BLE_LL_CS_MODE2 ||
               cssm->step_mode == BLE_LL_CS_MODE3) {
        for (i = 0; i < cssm->n_ap; ++i) {
            cssm->step_result.tone_pct[i] = 0;
            cssm->step_result.tone_quality_ind[i] = 0;
        }
    }

    ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);

    return 1;
}

/**
 * Called when the wait for response timer expires while in the sync state.
 *
 * Context: Interrupt.
 */
void
ble_ll_cs_tone_wfr_timer_exp(void)
{
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;

    assert(cssm != NULL);
    ble_ll_cs_proc_set_now_as_anchor_point(cssm);

    ble_ll_cs_proc_sync_lost(cssm);
}

#endif /* BLE_LL_CHANNEL_SOUNDING */
