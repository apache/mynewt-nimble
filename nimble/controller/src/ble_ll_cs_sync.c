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

int
ble_ll_cs_sync_tx_sched_cb(struct ble_ll_sched_item *sch)
{
    /* TODO: Start TX of CS SYNC */

    return BLE_LL_SCHED_STATE_RUNNING;
}

int
ble_ll_cs_sync_rx_sched_cb(struct ble_ll_sched_item *sch)
{
    /* TODO: Start RX of CS SYNC */

    return BLE_LL_SCHED_STATE_RUNNING;
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
    /* TODO: Verify CS Access Address */

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

    assert(cssm != NULL);
    ble_ll_cs_proc_set_now_as_anchor_point(cssm);
    ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);

    /* Packet type was verified in isr_start */

    rxpdu = ble_ll_rxpdu_alloc(rxbuf[1] + BLE_LL_PDU_HDR_LEN);
    if (rxpdu) {
        ble_phy_rxpdu_copy(rxbuf, rxpdu);

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
