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
#include "controller/ble_ll_conn.h"
#include "controller/ble_ll_sched.h"
#include "controller/ble_ll_tmr.h"
#include "controller/ble_hw.h"
#include "controller/ble_ll_hci.h"
#include "mbedtls/aes.h"
#include "ble_ll_priv.h"
#include "ble_ll_cs_priv.h"

extern struct ble_ll_cs_sm g_ble_ll_cs_sm[MYNEWT_VAL(BLE_MAX_CONNECTIONS)];
extern struct ble_ll_cs_sm *g_ble_ll_cs_sm_current;
extern int8_t g_ble_ll_tx_power;

static uint8_t
ble_ll_cs_sync_make(struct ble_ll_cs_sm *cssm, uint8_t *buf)
{
    int i;
    int rc;
    uint32_t aa;
    struct ble_ll_cs_config *conf = cssm->active_config;
    struct ble_ll_cs_step *step = cssm->current_step;
    uint8_t sequence_len = 0;
    uint8_t bits;

    if (conf->rtt_type != BLE_LL_CS_RTT_AA_ONLY && step->mode != BLE_LL_CS_MODE0) {
        rc = ble_ll_cs_drbg_generate_sync_sequence(
            &cssm->drbg_ctx, cssm->steps_in_procedure_count, conf->rtt_type,
            buf, &sequence_len);

        if (rc) {
            return 0;
        }
    }

    /* Shift by 4 bits to make space for trailer bits */
    if (sequence_len > 0) {
        BLE_LL_ASSERT(sequence_len < BLE_PHY_MAX_PDU_LEN);
        buf[sequence_len] = 0;
        bits = 0;
        for (i = sequence_len - 1; i >= 0; --i) {
            bits = buf[i] >> 4;
            buf[i] = (buf[i] << 4) & 0xF0;
            buf[i + 1] = (buf[i + 1] & 0xF0) | bits;
        }
    }

    if (conf->role == BLE_LL_CS_ROLE_INITIATOR) {
        aa = step->initiator_aa;
    } else {
        aa = step->reflector_aa;
    }

    /* Set the trailer bits */
    if (aa & 0x80000000) {
        buf[0] |= 0x0A;
    } else {
        buf[0] |= 0x05;
    }
    ++sequence_len;

    return sequence_len;
}

static void
ble_ll_cs_sync_tx_end_cb(void *arg)
{
    struct ble_ll_cs_sm *cssm = (struct ble_ll_cs_sm *)arg;
    struct ble_ll_cs_step_transmission *transm = cssm->current_step->next_transm;
    uint32_t cputime;
    uint32_t rem_us;
    uint32_t rem_ns;
    uint32_t end_anchor_usecs;

    BLE_LL_ASSERT(cssm != NULL);

    ble_phy_get_txend_time(&cputime, &rem_us, &rem_ns);
    end_anchor_usecs = ble_ll_tmr_t2u(cputime) + rem_us;

    cssm->anchor_usecs = end_anchor_usecs + transm->end_tifs;
    ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
}

static uint8_t
ble_ll_cs_sync_tx_make(uint8_t *dptr, void *arg, uint8_t *hdr_byte)
{
    uint8_t pdu_len;
    struct ble_ll_cs_sm *cssm = arg;

    /* TODO: Unused fields in CS Sync packet */
    pdu_len = ble_ll_cs_sync_make(cssm, dptr);
    *hdr_byte = 0;

    return pdu_len;
}

int
ble_ll_cs_sync_tx_start(struct ble_ll_cs_sm *cssm)
{
    int rc;
    uint32_t cputime;
    struct ble_ll_cs_step *step = cssm->current_step;
    struct ble_ll_cs_step_transmission *transm = step->next_transm;
    uint8_t rem_us;
    uint8_t ll_state;

    ll_state = ble_ll_state_get();
    BLE_LL_ASSERT(ll_state == BLE_LL_STATE_STANDBY || ll_state == BLE_LL_STATE_CS);

    ble_ll_tx_power_set(g_ble_ll_tx_power);

    rc = ble_phy_cs_sync_configure(step->channel, step->tx_aa);
    if (rc) {
        ble_ll_cs_proc_sync_lost(cssm);
        return 1;
    }

    cputime = ble_ll_tmr_u2t_r(cssm->anchor_usecs, &rem_us);
    ble_phy_transition_set(transm->end_transition, transm->end_tifs);
    ble_phy_transition_wfr_set(transm->wfr_usecs);

    /* At transition the radio is already scheduled to start at the right time */
    if (ll_state != BLE_LL_STATE_CS) {
        rc = ble_phy_tx_set_start_time(cputime, rem_us);
        if (rc) {
            ble_ll_cs_proc_sync_lost(cssm);
            return 1;
        }
    }

    ble_phy_set_txend_cb(ble_ll_cs_sync_tx_end_cb, cssm);

    rc = ble_phy_tx(ble_ll_cs_sync_tx_make, cssm);
    if (rc) {
        ble_ll_cs_proc_sync_lost(cssm);
        return 1;
    }

    ble_ll_state_set(BLE_LL_STATE_CS);

    return 0;
}

int
ble_ll_cs_sync_rx_start(struct ble_ll_cs_sm *cssm)
{
    int rc;
    uint32_t cputime;
    struct ble_ll_cs_step *step = cssm->current_step;
    struct ble_ll_cs_step_transmission *transm = step->next_transm;
    uint8_t ll_state;
    uint8_t rem_us;

    ll_state = ble_ll_state_get();
    BLE_LL_ASSERT(ll_state == BLE_LL_STATE_STANDBY || ll_state == BLE_LL_STATE_CS);

    rc = ble_phy_cs_sync_configure(step->channel, step->rx_aa);
    if (rc) {
        ble_ll_cs_proc_sync_lost(cssm);
        return 1;
    }

    cputime = ble_ll_tmr_u2t_r(cssm->anchor_usecs, &rem_us);
    ble_phy_transition_set(transm->end_transition, transm->end_tifs);
    ble_phy_transition_wfr_set(transm->wfr_usecs);

    if (ll_state == BLE_LL_STATE_CS) {
        /* At transition the radio is already scheduled to start at the right time */
        return 0;
    }

    /* Puts the phy into a receive mode and shedules the radio start */
    rc = ble_phy_rx_set_start_time(cputime, rem_us);
    if (rc) {
        ble_ll_cs_proc_sync_lost(cssm);
        return 1;
    }

    ble_phy_wfr_enable(BLE_PHY_WFR_ENABLE_RX, 0, transm->wfr_usecs);
    ble_ll_state_set(BLE_LL_STATE_CS);
    return 0;
}

static struct ble_ll_cs_sm *
ble_ll_cs_sync_find_sm_match_aa(uint32_t aa)
{
    int i;
    struct ble_ll_cs_sm *cssm;

    for (i = 0; i < ARRAY_SIZE(g_ble_ll_cs_sm); ++i) {
        cssm = &g_ble_ll_cs_sm[i];

        if (cssm->current_step->rx_aa == aa) {
            return cssm;
        }
    }

    return NULL;
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
ble_ll_cs_proc_rx_isr_start(struct ble_mbuf_hdr *rxhdr, uint32_t aa)
{
    struct ble_ll_cs_sm *cssm;

    cssm = ble_ll_cs_sync_find_sm_match_aa(aa);

    if (cssm == NULL) {
        /* This is not the expected packet. Skip the frame. */
        return -1;
    }

    return 0;
}

void
ble_ll_cs_sync_rx_end(struct ble_ll_cs_sm *cssm, uint8_t *rxbuf, struct ble_mbuf_hdr *rxhdr)
{
    struct ble_ll_cs_step_transmission *transm = cssm->current_step->next_transm;
    uint32_t cputime;
    uint32_t rem_us;
    uint32_t rem_ns;
    uint32_t end_anchor_usecs;

    /* Packet type was verified in isr_start */

    ble_phy_get_rxend_time(&cputime, &rem_us, &rem_ns);
    end_anchor_usecs = ble_ll_tmr_t2u(cputime) + rem_us;

    cssm->anchor_usecs = end_anchor_usecs + transm->end_tifs;
    ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
}

#endif /* BLE_LL_CHANNEL_SOUNDING */
