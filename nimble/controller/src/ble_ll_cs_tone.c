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

#define TONE_QUALITY_HIGH    0
#define TONE_QUALITY_MEDIUM  1
#define TONE_QUALITY_LOW     2
#define TONE_QUALITY_UNKNOWN 3

#define CS_IQ_MAX_12BIT 2047

extern struct ble_ll_cs_sm *g_ble_ll_cs_sm_current;

static uint8_t
ble_ll_cs_tone_quality_indicator(int16_t I12, int16_t Q12)
{
    uint32_t amplitude;

    /* A simplification instead of amplitude sqrt(I^2 + Q^2) */
    amplitude = abs(I12) + abs(Q12);

    if (amplitude < 10) return TONE_QUALITY_UNKNOWN;
    if (amplitude > 1500) return TONE_QUALITY_HIGH;
    if (amplitude > 800) return TONE_QUALITY_MEDIUM;
    return TONE_QUALITY_LOW;
}

void
ble_ll_cs_tone_tx_end(struct ble_phy_cs_transmission *transm,
                      struct ble_phy_cs_tone_results *results)
{
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;

    BLE_LL_ASSERT(cssm != NULL);

    ble_ll_cs_proc_next_state(cssm, transm);
}

void
ble_ll_cs_tone_rx_end(struct ble_phy_cs_transmission *transm,
                      struct ble_phy_cs_tone_results *results)
{
    struct ble_ll_cs_sm *cssm = g_ble_ll_cs_sm_current;
    struct ble_ll_cs_step *step;
    int32_t freq_offset;
    int16_t I12;
    int16_t Q12;
    int16_t max_val;
    uint8_t shift;
    uint8_t i;

    BLE_LL_ASSERT(cssm != NULL);

    step = cssm->current_step;

    if (step->mode == BLE_LL_CS_MODE0) {
        /* Convert 62.5ppb units to units of 0.01 ppm */
        freq_offset = (results->measured_freq_offset * 625) / 100;
        if (-10000 <= freq_offset && freq_offset <= 10000) {
            /* 15-bit signed integer */
            cssm->step_result.measured_freq_offset = freq_offset & 0x7FFF;
        } else {
            cssm->step_result.measured_freq_offset = 0xC000;
        }
    } else if (step->mode == BLE_LL_CS_MODE2 ||
               step->mode == BLE_LL_CS_MODE3) {
        i = cssm->step_result.tone_count++;
        BLE_LL_ASSERT(i < cssm->n_ap + 1);

        max_val = abs(results->I16);
        if (abs(results->Q16) > max_val) max_val = abs(results->Q16);

        shift = 0;
        while (max_val > CS_IQ_MAX_12BIT) {
            max_val >>= 1;
            shift++;
        }

        if (shift > 0) {
            I12 = (int16_t)(results->I16 >> shift);
            Q12 = (int16_t)(results->Q16 >> shift);
        } else {
            I12 = results->I16;
            Q12 = results->Q16;
        }

        /* 24 bit PCT: 12 LSB = I, 12 MSB = Q */
        cssm->step_result.tone_pct[i] = ((uint32_t)(Q12 & 0x0FFF) << 12) | ((uint32_t)(I12 & 0x0FFF));

        cssm->step_result.tone_quality_ind[i] = ble_ll_cs_tone_quality_indicator(I12, Q12);

        for (i = cssm->step_result.tone_count; i < ARRAY_SIZE(cssm->step_result.tone_pct); ++i) {
            cssm->step_result.tone_pct[i] = 0xFFFFFFFF;
            cssm->step_result.tone_quality_ind[i] = 0xFF;
        }
    }

    ble_ll_cs_proc_next_state(cssm, transm);
}

#endif /* BLE_LL_CHANNEL_SOUNDING */
