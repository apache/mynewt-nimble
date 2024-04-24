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
    uint8_t sequence_len;
    uint8_t bits;

    if (conf->rtt_type != BLE_LL_CS_RTT_AA_ONLY) {
        rc = ble_ll_cs_drbg_generate_sync_sequence(
            &cssm->drbg_ctx, cssm->steps_in_procedure_count, conf->rtt_type,
            buf, &sequence_len);

        if (rc) {
            return 0;
        }
    }

    /* Shift by 4 bits to make space for trailer bits */
    if (sequence_len > 0) {
        assert(sequence_len < BLE_PHY_MAX_PDU_LEN);
        buf[sequence_len] = 0;
        bits = 0;
        for (i = sequence_len - 1; i >= 0; --i) {
            bits = buf[i] >> 4;
            buf[i] = (buf[i] << 4) & 0xF0;
            buf[i + 1] = (buf[i + 1] & 0xF0) | bits;
        }
    }

    if (conf->role == BLE_LL_CS_ROLE_INITIATOR) {
        aa = cssm->initiator_aa;
    } else {
        aa = cssm->reflector_aa;
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

/**
 * Called when a CS pkt has been transmitted.
 * Schedule next CS pkt reception.
 *
 * Context: interrupt
 */
void
ble_ll_cs_sync_tx_end_cb(void *arg)
{
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;

    assert(cssm != NULL);
    ble_ll_cs_proc_set_now_as_anchor_point(cssm);
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
ble_ll_cs_sync_tx_sched_cb(struct ble_ll_sched_item *sch)
{
    int rc;
    struct ble_ll_cs_sm *cssm = sch->cb_arg;

    BLE_LL_ASSERT(ble_ll_state_get() == BLE_LL_STATE_STANDBY);

    /* TODO: Add to PHY a support CS mode.
     * Adjustments for nrf:
     * - Turn off CRC
     * - set 4 bits in S1
     *
     * ble_ll_phy_to_cs_sync_mode(cssm->active_config->cs_sync_phy, 0);
     */

    ble_ll_tx_power_set(g_ble_ll_tx_power);

    rc = ble_phy_tx_set_start_time(sch->start_time + g_ble_ll_sched_offset_ticks,
                                   sch->remainder);
    if (rc) {
        return BLE_LL_SCHED_STATE_DONE;
    }

    ble_phy_set_txend_cb(ble_ll_cs_sync_tx_end_cb, cssm);

    /* TODO: Add to PHY a support for 72 RF channels for CS exchanges.
     * ble_phy_cs_setchan(cssm->channel, cssm->tx_aa, 0);
     */

    /* TODO: Add to PHY a support for CS_SYNC transmission.
     * rc = ble_phy_tx_cs_sync(ble_ll_cs_sync_tx_make, cssm,
     *                         BLE_PHY_TRANSITION_TX_RX);
     */
    (void) ble_ll_cs_sync_tx_make;
    if (rc) {
        ble_phy_disable();
        return BLE_LL_SCHED_STATE_DONE;
    }

    ble_ll_state_set(BLE_LL_STATE_CS);

    return BLE_LL_SCHED_STATE_RUNNING;
}

int
ble_ll_cs_sync_rx_sched_cb(struct ble_ll_sched_item *sch)
{
    int rc;
    uint32_t wfr_usecs;
    struct ble_ll_cs_sm *cssm = sch->cb_arg;

    BLE_LL_ASSERT(ble_ll_state_get() == BLE_LL_STATE_STANDBY);

    /* TODO: Add to PHY a support CS mode.
     * Adjustments for nrf:
     * - Turn off CRC
     * - set 4 bits in S1
     *
     * ble_ll_phy_to_cs_sync_mode(cssm->active_config->cs_sync_phy, 0);
     */

    /* TODO: Add to PHY a support for 72 RF channels for CS exchanges.
     * ble_phy_cs_setchan(cssm->channel, cssm->tx_aa, 0);
     */

    rc = ble_phy_rx_set_start_time(sch->start_time + g_ble_ll_sched_offset_ticks,
                                   sch->remainder);
    if (rc) {
        return BLE_LL_SCHED_STATE_DONE;
    }

    wfr_usecs = cssm->duration_usecs;
    ble_phy_wfr_enable(BLE_PHY_WFR_ENABLE_RX, 0, wfr_usecs);

    ble_ll_state_set(BLE_LL_STATE_CS);

    return BLE_LL_SCHED_STATE_RUNNING;
}

static struct ble_ll_cs_sm *
ble_ll_cs_sync_find_sm_match_aa(uint32_t aa)
{
    int i;
    struct ble_ll_cs_sm *cssm;

    for (i = 0; i < ARRAY_SIZE(g_ble_ll_cs_sm); ++i) {
        cssm = &g_ble_ll_cs_sm[i];

        if (cssm->rx_aa == aa) {
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
ble_ll_cs_sync_rx_isr_start(struct ble_mbuf_hdr *rxhdr, uint32_t aa)
{
    struct ble_ll_cs_sm *cssm;

    cssm = ble_ll_cs_sync_find_sm_match_aa(aa);

    if (cssm == NULL) {
        /* This is not the expected packet. Skip the frame. */
        return -1;
    }

    return 0;
}

/**
 * Called when received a complete CS_SYNC packet.
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
ble_ll_cs_sync_rx_isr_end(uint8_t *rxbuf, struct ble_mbuf_hdr *rxhdr)
{
    struct os_mbuf *rxpdu;
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;

    /* Packet type was verified in isr_start */

    rxpdu = ble_ll_rxpdu_alloc(rxbuf[1] + BLE_LL_PDU_HDR_LEN);
    if (rxpdu) {
        ble_phy_rxpdu_copy(rxbuf, rxpdu);

        assert(cssm != NULL);
        ble_ll_cs_proc_set_now_as_anchor_point(cssm);
        ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);

        /* Send the packet to Link Layer context */
        ble_ll_rx_pdu_in(rxpdu);
    }

    /* PHY is disabled here */
    ble_ll_cs_proc_current_sm_over();

    return 1;
}

/**
 * Process a received PDU.
 *
 * Context: Link Layer task.
 *
 * @param pdu_type
 * @param rxbuf
 */
void
ble_ll_cs_sync_rx_pkt_in(struct os_mbuf *rxpdu, struct ble_mbuf_hdr *rxhdr)
{
}

/**
 * Called when the wait for response timer expires while in the sync state.
 *
 * Context: Interrupt.
 */
void
ble_ll_cs_sync_wfr_timer_exp(void)
{
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;

    assert(cssm != NULL);
    ble_ll_cs_proc_set_now_as_anchor_point(cssm);
    ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
}

#endif /* BLE_LL_CHANNEL_SOUNDING */
