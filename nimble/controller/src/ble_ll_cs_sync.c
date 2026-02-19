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
#include "ble_ll_cs_priv.h"

extern struct ble_ll_cs_supp_cap g_ble_ll_cs_local_cap;
extern struct ble_ll_cs_sm *g_ble_ll_cs_sm_current;

int
ble_ll_cs_rtt_generate(struct ble_ll_cs_drbg_ctx *drbg_ctx, struct ble_ll_cs_step *step,
                       uint8_t *buf, uint8_t *out_rtt_len, uint32_t buf_len,
                       uint16_t steps_in_procedure_count, uint8_t rtt_type, uint8_t role)
{
    int i;
    int rc;
    uint32_t aa;
    uint8_t sequence_len = 0;
    uint8_t bits;

    *out_rtt_len = 0;
    buf[0] = 0;

    if (rtt_type != BLE_LL_CS_RTT_AA_ONLY && step->mode != BLE_LL_CS_MODE0) {
        rc = ble_ll_cs_drbg_generate_sync_sequence(drbg_ctx, steps_in_procedure_count,
                                                   rtt_type, buf, &sequence_len);

        if (rc) {
            return 0;
        }
    }

    /* Shift by 4 bits to make space for trailer bits */
    if (sequence_len > 0) {
        /* If assert, increase the rtt_buffer or limit the CS subevent length */
        BLE_LL_ASSERT(sequence_len < buf_len && sequence_len < BLE_PHY_MAX_PDU_LEN);

        buf[sequence_len] = 0;
        bits = 0;
        for (i = sequence_len - 1; i >= 0; --i) {
            bits = buf[i] >> 4;
            buf[i] = (buf[i] << 4) & 0xF0;
            buf[i + 1] = (buf[i + 1] & 0xF0) | bits;
        }
    }

    if (role == BLE_LL_CS_ROLE_INITIATOR) {
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

    *out_rtt_len = sequence_len;

    return 0;
}

void
ble_ll_cs_sync_tx_end(struct ble_phy_cs_transmission *transm,
                      struct ble_phy_cs_sync_results *results)
{
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;

    BLE_LL_ASSERT(cssm != NULL);

    cssm->step_result.time_of_departure_ns = results->rem_ns;

    ble_ll_cs_proc_next_state(cssm, transm);
}

uint8_t
ble_ll_cs_rtt_tx_make(uint8_t *dptr, uint8_t *hdr_byte)
{
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;
    struct ble_ll_cs_step *step;

    BLE_LL_ASSERT(cssm != NULL);

    step = cssm->current_step;
    memcpy(dptr, step->rtt_tx, step->rtt_tx_len);
    *hdr_byte = 0;

    return step->rtt_tx_len;
}

void
ble_ll_cs_sync_rx_end(struct ble_phy_cs_transmission *transm,
                      struct ble_phy_cs_sync_results *results)
{
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;

    BLE_LL_ASSERT(cssm != NULL);

    cssm->step_result.time_of_arrival_ns = results->rem_ns;
    cssm->step_result.packet_rssi = results->rssi;
    cssm->step_result.packet_quality = 0;
    cssm->step_result.packet_nadm = 0xFF;

    if (g_ble_ll_cs_local_cap.sounding_pct_estimate &&
        cssm->active_config->rtt_type != BLE_LL_CS_RTT_AA_ONLY) {
        /* TODO: Read PCT estimates from sounding sequence.
         * For now set "Phase Correction Term is not available".
         */
        cssm->step_result.packet_pct1 = 0xFFFFFFFF;
        cssm->step_result.packet_pct2 = 0xFFFFFFFF;
    }

    ble_ll_cs_proc_next_state(cssm, transm);
}

#endif /* BLE_LL_CHANNEL_SOUNDING */
