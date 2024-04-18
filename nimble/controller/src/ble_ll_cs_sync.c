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

static void
ble_ll_cs_sync_tx_end_cb(void *arg)
{
    struct ble_ll_cs_sm *cssm = (struct ble_ll_cs_sm *)arg;

    BLE_LL_ASSERT(cssm != NULL);
    ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
}

int
ble_ll_cs_sync_tx_start(struct ble_ll_cs_sm *cssm)
{
    /* TODO: Start TX of CS SYNC */

    return 0;
}

int
ble_ll_cs_sync_rx_start(struct ble_ll_cs_sm *cssm)
{
    /* TODO: Start RX of CS SYNC */

    return 0;
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
    /* TODO: Verify CS Access Address */

    return 0;
}

void
ble_ll_cs_sync_rx_end(struct ble_ll_cs_sm *cssm, uint8_t *rxbuf, struct ble_mbuf_hdr *rxhdr)
{
    ble_ll_cs_proc_schedule_next_tx_or_rx(cssm);
}

#endif /* BLE_LL_CHANNEL_SOUNDING */
