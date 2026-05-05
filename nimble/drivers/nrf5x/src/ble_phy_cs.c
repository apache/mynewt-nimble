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

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <hal/nrf_radio.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_rtc.h>
#include "syscfg/syscfg.h"
#include "os/os.h"
#include "os/os_cputime.h"
#include "nimble/ble.h"
#include "controller/ble_phy.h"
#include "controller/ble_ll.h"
#include "nrfx.h"
#include "phy_priv.h"
#include <controller/ble_ll_pdu.h>
#include <mcu/cmsis_nvic.h>
#include <math.h>

#if MYNEWT_VAL(BLE_CHANNEL_SOUNDING)

/* To disable all radio interrupts */
#define NRF_RADIO_IRQ_MASK_ALL  (RADIO_INTENSET00_READY_Msk    | \
                                 RADIO_INTENSET00_ADDRESS_Msk  | \
                                 RADIO_INTENSET00_PAYLOAD_Msk  | \
                                 RADIO_INTENSET00_END_Msk      | \
                                 RADIO_INTENSET00_PHYEND_Msk   | \
                                 RADIO_INTENSET00_DISABLED_Msk | \
                                 RADIO_INTENSET00_DEVMATCH_Msk | \
                                 RADIO_INTENSET00_DEVMISS_Msk  | \
                                 RADIO_INTENSET00_BCMATCH_Msk  | \
                                 RADIO_INTENSET00_CRCOK_Msk    | \
                                 RADIO_INTENSET00_CRCERROR_Msk)

/* BLE PHY defaults */
#define NRF_LFLEN_BITS          (8)
#define NRF_S0LEN               (1)
#define NRF_S1LEN_BITS          (0)
#define NRF_MAXLEN              (255)
#define NRF_BALEN               (3)     /* For base address of 3 bytes */
#define NRF_PCNF0               (NRF_LFLEN_BITS << RADIO_PCNF0_LFLEN_Pos) | \
                                (RADIO_PCNF0_S1INCL_Include << RADIO_PCNF0_S1INCL_Pos) | \
                                (NRF_S0LEN << RADIO_PCNF0_S0LEN_Pos) | \
                                (NRF_S1LEN_BITS << RADIO_PCNF0_S1LEN_Pos)

/* Various radio timings */
/* Radio ramp-up times in usecs (fast mode) */
#define BLE_PHY_T_TXENFAST      (40)
#define BLE_PHY_T_RXENFAST      (40)

#define CSTONE_EXCLUSION_PERIOD_US (10) // XXXX change to 1us

#define TONE_RESULT_COUNT_MAX (5)

#define BLE_PHY_CS_ROLE_INITIATOR (0)
#define BLE_PHY_CS_ROLE_REFLECTOR (1)

static uint32_t g_ble_phy_tx_buf[(BLE_PHY_MAX_PDU_LEN + 3) / 4];
static uint32_t g_ble_phy_rx_buf[(BLE_PHY_MAX_PDU_LEN + 3) / 4];

struct ble_phy_cs_obj
{
    uint32_t prev_radio_isr_handler;
    uint32_t step_anchor_ticks;
    uint8_t phy_state;
    uint8_t phy_mode;
    uint8_t role;
    uint8_t report_sync_results;
    uint8_t report_tone_results;
    struct ble_phy_cs_transmission *prev_transm;
    struct ble_phy_cs_sync_results sync_results;
    struct ble_phy_cs_tone_results tone_results[TONE_RESULT_COUNT_MAX];
    struct ble_phy_cs_subevent_results subevent_results;
};

static struct ble_phy_cs_obj g_ble_phy_cs;

/* delay between EVENTS_READY and start of tx */
const uint16_t txdelay[BLE_PHY_NUM_MODE] = {
    [BLE_PHY_MODE_1M] = 1, /* ~1.6us */
};
/* delay between EVENTS_ADDRESS and txd access address  */
const uint16_t txaddrdelay_ns[BLE_PHY_NUM_MODE] = {
    [BLE_PHY_MODE_1M] = 3200, /* ~3.2us */
};
/* delay between rxd access address (w/ TERM1 for coded) and EVENTS_ADDRESS */
const uint16_t rxaddrdelay_ns[BLE_PHY_NUM_MODE] = {
    [BLE_PHY_MODE_1M] = 9200, /* ~9.2us */
};

#if MYNEWT_VAL(BLE_PHY_CS_USE_PCT_CORRECTION)

#define LUT_SIZE 256
#define FULL_LUT_SIZE (4 * LUT_SIZE)

/* LUT used for correction of the circuit delays. Must be calibrated per device.
 * The values (-32768, 32767) corresponds to <-2π, 2π).
 */
static int16_t pct_angle_correction_lut[80] = MYNEWT_VAL(BLE_PHY_CS_PCT_CORRECTION_PER_CHANNEL);

/* sin approximation for <0, π/2> angles */
static const int16_t sin0_pi2_table[LUT_SIZE] = {
    0, 202, 404, 605, 807, 1009, 1211, 1412,
    1614, 1816, 2017, 2219, 2420, 2621, 2822, 3023,
    3224, 3425, 3626, 3826, 4027, 4227, 4427, 4627,
    4827, 5026, 5226, 5425, 5624, 5822, 6021, 6219,
    6417, 6615, 6813, 7010, 7207, 7404, 7600, 7796,
    7992, 8188, 8383, 8578, 8773, 8967, 9161, 9355,
    9548, 9741, 9933, 10126, 10317, 10509, 10700, 10890,
    11080, 11270, 11459, 11648, 11837, 12025, 12212, 12399,
    12586, 12772, 12958, 13143, 13328, 13512, 13695, 13878,
    14061, 14243, 14425, 14606, 14786, 14966, 15145, 15324,
    15502, 15679, 15856, 16033, 16208, 16383, 16558, 16732,
    16905, 17078, 17250, 17421, 17592, 17761, 17931, 18099,
    18267, 18434, 18601, 18767, 18932, 19096, 19260, 19423,
    19585, 19747, 19907, 20067, 20226, 20385, 20542, 20699,
    20855, 21011, 21165, 21319, 21472, 21624, 21775, 21925,
    22075, 22224, 22372, 22519, 22665, 22810, 22955, 23098,
    23241, 23383, 23524, 23664, 23803, 23941, 24079, 24215,
    24351, 24485, 24619, 24752, 24883, 25014, 25144, 25273,
    25401, 25528, 25654, 25779, 25903, 26026, 26149, 26270,
    26390, 26509, 26627, 26744, 26860, 26976, 27090, 27203,
    27315, 27426, 27536, 27644, 27752, 27859, 27965, 28069,
    28173, 28276, 28377, 28477, 28577, 28675, 28772, 28868,
    28963, 29057, 29150, 29241, 29332, 29421, 29510, 29597,
    29683, 29768, 29851, 29934, 30016, 30096, 30175, 30253,
    30330, 30406, 30481, 30554, 30627, 30698, 30768, 30837,
    30904, 30971, 31036, 31100, 31163, 31225, 31286, 31345,
    31403, 31460, 31516, 31571, 31624, 31676, 31728, 31777,
    31826, 31873, 31920, 31965, 32008, 32051, 32092, 32132,
    32171, 32209, 32246, 32281, 32315, 32348, 32379, 32410,
    32439, 32467, 32493, 32519, 32543, 32566, 32587, 32608,
    32627, 32645, 32662, 32678, 32692, 32705, 32717, 32727,
    32737, 32745, 32751, 32757, 32761, 32765, 32766, 32767,
};

static inline int16_t
sin_rad(int32_t angle)
{
    uint32_t index;
    int16_t sign = 1;

    angle %= FULL_LUT_SIZE;

    if (angle < 0) angle += FULL_LUT_SIZE;

    if (angle < LUT_SIZE) {
        /* 0..π/2, already a good angle */
    } else if (angle < 2 * LUT_SIZE) {
        /* π/2..π */
        angle = 2 * LUT_SIZE - angle;
    } else if (angle < 3 * LUT_SIZE) {
        /* π..3π/2 */
        angle -= 2 * LUT_SIZE;
        sign = -1;
    } else {
        /* 3π/2..2π */
        angle = FULL_LUT_SIZE - angle;
        sign = -1;
    }

    index = ((uint32_t)angle * (LUT_SIZE - 1)) / LUT_SIZE;

    return sign * sin0_pi2_table[index];
}

static inline int16_t
cos_rad(int32_t angle)
{
    /* cos(x) = sin(x + π/2) */
    return sin_rad(angle + LUT_SIZE);
}

static inline void
ble_phy_cs_pct_correct_ciruit_delay(int16_t *PCT_I16, int16_t *PCT_Q16, uint8_t channel)
{
    int16_t I16, Q16;
    int32_t I_corr, Q_corr;
    int32_t angle;
    int16_t cos_phi, sin_phi;

    I16 = *PCT_I16;
    Q16 = *PCT_Q16;

    /* Angle is Q1.31 */
    angle = pct_angle_correction_lut[channel];

    /* angle_lut_idx = (angle/2^15) * FULL_LUT_SIZE
     * Q1.31 * Q0.11 = Q1.42, so we have to >> 11 (out of 15) before
     * multiplication to get max precision Q1.31. After dividing by
     * remaining >> 4 we end up with Q1.17.
     */
    angle = ((angle >> 11) * FULL_LUT_SIZE) >> 4;

    /* Because of the modulo 2e10 the result is Q1.10 */
    angle = angle % FULL_LUT_SIZE;
    if (angle < 0) angle += FULL_LUT_SIZE;

    /* Read sin/cos from LUT */
    cos_phi = cos_rad(angle);
    sin_phi = sin_rad(angle);

    /* PCT correction. I16, Q16, cos_phi and sin_phi are formatted in Q1.15.
     * The multiplication results in Q1.30 format, so it should be scaled
     * back in the last step.
     */
    I_corr = (int32_t)I16 * cos_phi + (int32_t)Q16 * sin_phi;
    Q_corr = -(int32_t)I16 * sin_phi + (int32_t)Q16 * cos_phi;

    *PCT_I16 = (int16_t)(I_corr >> 15);
    *PCT_Q16 = (int16_t)(Q_corr >> 15);
}
#endif /* BLE_PHY_CS_USE_PCT_CORRECTION */

static inline void
ble_phy_cs_rtt_set(uint8_t *rtt_sequence, uint8_t rtt_sequence_len)
{
    /* TODO: Filling only RTT.SEGMENTxx does not work, waiting for the documentation to be updated */
    memcpy(&((uint8_t *)&g_ble_phy_tx_buf)[1], rtt_sequence, rtt_sequence_len);
    memcpy(&((uint8_t *)&g_ble_phy_rx_buf)[1], rtt_sequence, rtt_sequence_len);

    NRF_RADIO->RTT.SEGMENT01 = get_le32(&rtt_sequence[0]);
    NRF_RADIO->RTT.SEGMENT23 = get_le32(&rtt_sequence[4]);
    NRF_RADIO->RTT.SEGMENT45 = get_le32(&rtt_sequence[8]);
    NRF_RADIO->RTT.SEGMENT67 = get_le32(&rtt_sequence[12]);
    NRF_RADIO->RTT.CONFIG =
        ((rtt_sequence_len >> 1) << RADIO_RTT_CONFIG_NUMSEGMENTS_Pos) |
        (RADIO_RTT_CONFIG_EN_Enabled << RADIO_RTT_CONFIG_EN_Pos) |
        ((g_ble_phy_cs.role == BLE_PHY_CS_ROLE_INITIATOR) ?
        RADIO_RTT_CONFIG_ROLE_Initiator << RADIO_RTT_CONFIG_ROLE_Pos :
        RADIO_RTT_CONFIG_ROLE_Reflector << RADIO_RTT_CONFIG_ROLE_Pos);

    NRF_RADIO->PCNF1 &= ~(RADIO_PCNF1_MAXLEN_Msk | RADIO_PCNF1_STATLEN_Msk);
    NRF_RADIO->PCNF1 |= (rtt_sequence_len << RADIO_PCNF1_MAXLEN_Pos) |
                        (rtt_sequence_len << RADIO_PCNF1_STATLEN_Pos);
}

static inline int16_t
ble_phy_cs_ffoest_get(void)
{
    return  ((int16_t)(NRF_RADIO->CSTONES.FFOEST << 4)) >> 4;
}

static int
ble_phy_cs_radio_timer_start(uint32_t cputime, uint8_t rem_us, bool tx)
{
    uint32_t next_cc;
    uint32_t cur_cc;
    uint32_t cntr;
    uint32_t delta;
    uint32_t anchor;
    int radio_rem;
    int rem_us_corr;

    /* Calculate rem_us for radio and FEM enable. The result may be a negative
     * value, but we'll adjust later.
     */
    anchor = rem_us;
    if (tx) {
        radio_rem = anchor - BLE_PHY_T_TXENFAST - txdelay[g_ble_phy_cs.phy_mode];
    } else {
        radio_rem = anchor - BLE_PHY_T_RXENFAST;
    }

    /* We need to adjust rem_us values, so they are >=1 for TIMER10 compare
     * event to be triggered.
     */
    if (radio_rem <= -30) {
        /* rem_us is -60..-30 */
        cputime -= 2;
        rem_us_corr = 61;
    } else {
        /* rem_us is -29..0 */
        cputime -= 1;
        rem_us_corr = 30;
    }

    /*
     * Can we set the RTC compare to start TIMER0? We can do it if:
     *      a) Current compare value is not N+1 or N+2 ticks from current
     *      counter.
     *      b) The value we want to set is not at least N+2 from current
     *      counter.
     *
     * NOTE: since the counter can tick 1 while we do these calculations we
     * need to account for it.
     */
    next_cc = cputime & 0xffffff;
    cur_cc = NRF_RTC0->CC[0];
    cntr = NRF_RTC0->COUNTER;

    delta = (cur_cc - cntr) & 0xffffff;
    if ((delta <= 3) && (delta != 0)) {
        return -1;
    }
    delta = (next_cc - cntr) & 0xffffff;
    if ((delta & 0x800000) || (delta < 3)) {
        return -1;
    }

    /* Clear and set TIMER0 to fire off at proper time */
    nrf_timer_task_trigger(NRF_TIMER10, NRF_TIMER_TASK_CLEAR);
    g_ble_phy_cs.step_anchor_ticks = anchor + rem_us_corr;

    /* Set RTC compare to start TIMER0 */
    NRF_RTC0->EVENTS_COMPARE[0] = 0;
    nrf_rtc_cc_set(NRF_RTC0, 0, next_cc);
    nrf_rtc_event_enable(NRF_RTC0, RTC_EVTENSET_COMPARE0_Msk);

    phy_ppi_rtc0_compare0_to_timer0_start_enable();

    return 0;
}

static inline int
ble_phy_cs_channel_set(uint8_t chan)
{
    assert(!(chan <= 1 || (23 <= chan && chan <= 25) || 77 <= chan));

    /* Check for valid channel range */
    if (chan <= 1 || (23 <= chan && chan <= 25) || 77 <= chan) {
        return BLE_PHY_ERR_INV_PARAM;
    }

    if (NRF_RADIO->FREQUENCY != 2 + chan) {
        NRF_RADIO->FREQUENCY = 2 + chan;
    }

    return 0;
}

static inline void
ble_phy_cs_tune_set(void)
{
    NRF_RADIO->CSTONES.FFOIN = RADIO_CSTONES_FFOIN_ResetValue;
    NRF_RADIO->CSTONES.FFOSOURCE = RADIO_CSTONES_FFOSOURCE_ResetValue;
    NRF_RADIO->CSTONES.PHASESHIFT = 0;
    RADIO_FREQFINETUNE = MYNEWT_VAL(BLE_PHY_CS_FREQFINETUNE) & 0x1FFFUL;
}

static inline void
ble_phy_cs_tone_configure(uint32_t duration_usecs, uint8_t tone_mode)
{
    uint8_t numsamples;

    assert(duration_usecs <= 80);
    numsamples = (duration_usecs - 2 * CSTONE_EXCLUSION_PERIOD_US) * 160 / 80;
    NRF_RADIO->CSTONES.NUMSAMPLES = numsamples;
    NRF_RADIO->CSTONES.NUMSAMPLESCOEFF = (1 << 20) / numsamples;

    if (tone_mode == BLE_PHY_CS_TONE_MODE_PM) {
        NRF_RADIO->CSTONES.DOWNSAMPLE = 1;
        NRF_RADIO->CSTONES.MODE = RADIO_CSTONES_MODE_TPM_Msk | RADIO_CSTONES_MODE_TFM_Msk;
    } else {
        NRF_RADIO->CSTONES.MODE = RADIO_CSTONES_MODE_TFM_Msk;
    }
}

static void
ble_phy_cs_disable(void)
{
#ifdef NRF54L_SERIES
    nrf_timer_task_trigger(NRF_TIMER10, NRF_TIMER_TASK_STOP);
#endif
    phy_ppi_timer0_compare3_to_radio_disable_disable();
    phy_ppi_timer0_compare2_to_radio_start_disable();
    phy_ppi_timer0_compare4_to_radio_cstonesstart_disable();
    phy_ppi_cs_mode_disable();

    nrf_timer_task_trigger(NRF_TIMER10, NRF_TIMER_TASK_STOP);
    phy_ppi_timer0_compare0_to_radio_txen_disable();
    phy_ppi_timer0_compare1_to_radio_rxen_disable();
    phy_ppi_rtc0_compare0_to_timer0_start_disable();
    phy_ppi_timer00_radio_address_to_capture_disable();
    ble_phy_disable();

    NRF_RADIO->CSTONES.MODE = 0;
    NRF_RADIO->RTT.CONFIG = 0;

    /* Configure back the registers */
    NRF_RADIO->CRCCNF = (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos) | RADIO_CRCCNF_LEN_Three;
    NRF_RADIO->PCNF0 = NRF_PCNF0;
    NRF_RADIO->PCNF1 = NRF_MAXLEN |
                       (RADIO_PCNF1_ENDIAN_Little <<  RADIO_PCNF1_ENDIAN_Pos) |
                       (NRF_BALEN << RADIO_PCNF1_BALEN_Pos) |
                       RADIO_PCNF1_WHITEEN_Msk;
    NRF_RADIO->RXADDRESSES  = (1 << 0);

    if (g_ble_phy_cs.prev_radio_isr_handler != 0) {
        NVIC_SetVector(RADIO_IRQn, (uint32_t)g_ble_phy_cs.prev_radio_isr_handler);
    }
}

static void
ble_phy_cs_subevent_end(int status)
{
    ble_phy_cs_disable();
    g_ble_phy_cs.subevent_results.status = status;
    ble_ll_cs_subevent_end(&g_ble_phy_cs.subevent_results);
}

static inline uint32_t
ble_phy_cs_calc_tone_chain_offset(struct ble_phy_cs_transmission *first_tone)
{
    struct ble_phy_cs_transmission *t;
    uint32_t offset_us = 0;

    t = first_tone;
    while (t && t->mode == BLE_PHY_CS_TRANSM_MODE_TONE) {
        offset_us += t->duration_usecs;
        offset_us += t->end_tifs;
        t = t->next;
    }

    return offset_us;
}

static inline void
ble_phy_cs_radio_events_clear(void)
{
    NRF_RADIO->EVENTS_PHYEND = 0;
    NRF_RADIO->EVENTS_READY = 0;
    NRF_RADIO->EVENTS_TXREADY = 0;
    NRF_RADIO->EVENTS_RXREADY = 0;
    NRF_RADIO->EVENTS_ADDRESS = 0;
    NRF_RADIO->EVENTS_END = 0;
    NRF_RADIO->EVENTS_DISABLED = 0;
    RADIO_EVENTS_CSTONESEND = 0;
    NRF_TIMER10->EVENTS_COMPARE[3] = 0;
}

static int
ble_phy_cs_schedule_step(uint32_t anchor_us, struct ble_phy_cs_transmission *next_transm,
                         uint8_t role)
{
    struct ble_phy_cs_transmission *t = next_transm;
    struct ble_phy_cs_transmission *next;
    uint32_t start_us;
    uint32_t offset_us = 0;
    uint32_t end_us = anchor_us;
    uint32_t total_duration;
    uint8_t current_phase = 0xFF;

    g_ble_phy_cs.report_sync_results = 0;
    g_ble_phy_cs.report_tone_results = 0;
    NRF_RADIO->SHORTS = 0;
    NRF_RADIO->CSTONES.MODE = 0;
    NRF_RADIO->RTT.CONFIG = 0;
    NRF_RADIO->PCNF1 &= ~(RADIO_PCNF1_MAXLEN_Msk | RADIO_PCNF1_STATLEN_Msk);
    nrf_radio_int_enable(NRF_RADIO, RADIO_INTENSET_DISABLED_Msk);
    ble_phy_cs_channel_set(t->channel);
    nrf_timer_cc_set(NRF_TIMER10, 2, 0xffffffff);
    nrf_timer_cc_set(NRF_TIMER10, 3, 0xffffffff);
    nrf_timer_cc_set(NRF_TIMER10, 4, 0xffffffff);
    nrf_timer_cc_set(NRF_TIMER10, 5, 0xffffffff);

    if (t->is_tx) {
        NRF_RADIO->PACKETPTR = (uint32_t)&g_ble_phy_tx_buf[0];
    } else {
        NRF_RADIO->PACKETPTR = (uint32_t)&g_ble_phy_rx_buf[0];
    }

    while (t) {
        start_us = anchor_us + offset_us;

        /* Detect if TX or RX phase */
        if (t->is_tx != current_phase) {
            if (t->is_tx == 1) {
                /* Schedule TXEN */
                NRF_TIMER10->EVENTS_COMPARE[0] = 0;
                nrf_timer_cc_set(NRF_TIMER10, 0, start_us - BLE_PHY_T_TXENFAST - txdelay[g_ble_phy_cs.phy_mode]);
                phy_ppi_timer0_compare0_to_radio_txen_enable();
            } else {
                /* Schedule RXEN */
                NRF_TIMER10->EVENTS_COMPARE[1] = 0;
                nrf_timer_cc_set(NRF_TIMER10, 1, start_us - BLE_PHY_T_RXENFAST);
                phy_ppi_timer0_compare1_to_radio_rxen_enable();
            }

            current_phase = t->is_tx;
        }

        /* Schedule CS_SYNC TX or RX */
        if (t->mode == BLE_PHY_CS_TRANSM_MODE_SYNC) {
            phy_ppi_timer00_radio_address_to_capture_enable();
            nrf_radio_int_enable(NRF_RADIO, RADIO_INTENSET_ADDRESS_Msk);
            g_ble_phy_cs.report_sync_results = 1;
            if (t->is_tx) {
                if (t->rtt_sequence) {
                    ble_phy_cs_rtt_set(t->rtt_sequence, t->rtt_sequence_len);
                }
                /* TX SYNC */
                NRF_RADIO->BASE0 = (t->aa << 8);
                NRF_RADIO->PREFIX0 = (NRF_RADIO->PREFIX0 & 0xFFFFFF00) | (t->aa >> 24);
                /* Set the CS trailer */
                if (t->aa & (1 << 31)) {
                    /* Filling RX buffer too, because if we missed a CS_SYNC RX,
                     * there will be no EVENTS_ADDRESS interrupt,
                     * therefore no time to update the NRF_RADIO->PACKETPTR.
                     */
                    g_ble_phy_tx_buf[0] &= 0xFFFFFFF0;
                    g_ble_phy_tx_buf[0] |= 0b1010;
                    g_ble_phy_rx_buf[0] &= 0xFFFFFFF0;
                    g_ble_phy_rx_buf[0] |= 0b1010;
                } else {
                    g_ble_phy_tx_buf[0] &= 0xFFFFFFF0;
                    g_ble_phy_tx_buf[0] |= 0b00000101;
                    g_ble_phy_rx_buf[0] &= 0xFFFFFFF0;
                    g_ble_phy_rx_buf[0] |= 0b00000101;
                }
                /* Schedule RADIO START for TX */
                nrf_timer_cc_set(NRF_TIMER10, 2, start_us);
                phy_ppi_timer0_compare2_to_radio_start_enable();
            } else {
                /* RX SYNC */
                NRF_RADIO->BASE1 = (t->aa << 8);
                NRF_RADIO->PREFIX0 = (NRF_RADIO->PREFIX0 & 0xFFFF00FF) | (((t->aa >> 24) & 0xFF) << 8);
                /* Schedule RADIO START for RX */
                nrf_timer_cc_set(NRF_TIMER10, 6, start_us);
                phy_ppi_timer0_compare2_to_radio_start_enable();
            }

            end_us = start_us + t->duration_usecs;
            offset_us += t->duration_usecs + t->end_tifs;
            g_ble_phy_cs.prev_transm = t;
            t = t->next;
        } else if (t->mode == BLE_PHY_CS_TRANSM_MODE_TONE) {
            NRF_RADIO->RTT.CONFIG = (RADIO_RTT_CONFIG_EN_Enabled << RADIO_RTT_CONFIG_EN_Pos) |
                ((g_ble_phy_cs.role == BLE_PHY_CS_ROLE_INITIATOR) ?
                RADIO_RTT_CONFIG_ROLE_Initiator << RADIO_RTT_CONFIG_ROLE_Pos :
                RADIO_RTT_CONFIG_ROLE_Reflector << RADIO_RTT_CONFIG_ROLE_Pos);
            g_ble_phy_cs.report_tone_results = 1;
            total_duration = t->duration_usecs;
            next = t->next;
            if (next && next->is_tx == t->is_tx && next->mode == t->mode) {
                total_duration += t->end_tifs + next->duration_usecs;
                offset_us += next->end_tifs;
                g_ble_phy_cs.prev_transm = next;
                next = next->next;
            } else {
                g_ble_phy_cs.prev_transm = t;
            }
            offset_us += total_duration + t->end_tifs;
            end_us = start_us + total_duration;

            if (t->is_tx) {
                if (t->tone_mode == BLE_PHY_CS_TONE_MODE_PM) {
                    NRF_RADIO->CSTONES.MODE = RADIO_CSTONES_MODE_TPM_Msk | RADIO_CSTONES_MODE_TFM_Msk;
                } else {
                    NRF_RADIO->CSTONES.MODE = RADIO_CSTONES_MODE_TFM_Msk;
                }
            } else {
                ble_phy_cs_tone_configure(t->duration_usecs, t->tone_mode);
                /* Schedule CSTONESSTART for RX */
                nrf_timer_cc_set(NRF_TIMER10, 4, start_us + CSTONE_EXCLUSION_PERIOD_US);
                phy_ppi_timer0_compare4_to_radio_cstonesstart_enable();
            }

            t = next;
        }

        /* Schedule radio disable if the end of the step */
        if (t == NULL || (t->is_tx != current_phase &&
            ((g_ble_phy_cs.role == BLE_PHY_CS_ROLE_INITIATOR && current_phase == 0) ||
             (g_ble_phy_cs.role == BLE_PHY_CS_ROLE_REFLECTOR && current_phase == 1)))) {
            nrf_timer_cc_set(NRF_TIMER10, 3, end_us + 20);
            phy_ppi_timer0_compare3_to_radio_disable_enable();
            g_ble_phy_cs.step_anchor_ticks = end_us + g_ble_phy_cs.prev_transm->end_tifs;
            break;
        }
    }

    return 0;
}

static uint32_t
ticks00_to_ns(uint32_t ticks) {
    return (uint32_t)(((uint64_t)ticks * 1000000000ULL) / 128000000ULL);
}

static void
ble_phy_cs_isr(void)
{
    int rc = 0;
    struct ble_phy_cs_transmission *prev_transm = g_ble_phy_cs.prev_transm;
    struct ble_phy_cs_transmission *next_transm = NULL;
    struct ble_phy_cs_tone_results *tone_result = NULL;
    struct ble_phy_cs_sync_results *sync_result = NULL;
    uint32_t irq00_en;
    uint32_t pct16_reg;
    uint32_t tod_ticks00, toa_ticks00;
    int16_t I16, Q16;
    int16_t measured_freq_offset;
    uint8_t last_half_step;
    uint8_t i;

    os_trace_isr_enter();

    irq00_en = NRF_RADIO->INTENSET00;

    if ((irq00_en & RADIO_INTENSET00_ADDRESS_Msk) && NRF_RADIO->EVENTS_ADDRESS) {
        NRF_RADIO->INTENCLR00 = RADIO_INTENCLR00_ADDRESS_Msk;

        if (NRF_RADIO->EVENTS_DISABLED == 0) {
            if (g_ble_phy_cs.role == BLE_PHY_CS_ROLE_INITIATOR) {
                NRF_RADIO->PACKETPTR = (uint32_t)&g_ble_phy_rx_buf[0];
            } else {
                NRF_RADIO->PACKETPTR = (uint32_t)&g_ble_phy_tx_buf[0];
            }

            os_trace_isr_exit();

            return;
        }
    }

    /* Disable PPI */
    phy_ppi_timer0_compare3_to_radio_disable_disable();
    phy_ppi_rtc0_compare0_to_timer0_start_disable();
    phy_ppi_timer0_compare2_to_radio_start_disable();
    phy_ppi_timer0_compare0_to_radio_txen_disable();
    phy_ppi_timer0_compare1_to_radio_rxen_disable();
    phy_ppi_timer0_compare4_to_radio_cstonesstart_disable();

    /* Make sure all interrupts are disabled */
    NRF_RADIO->INTENCLR00 = NRF_RADIO_IRQ_MASK_ALL;
    RADIO_INTENCLR01 = RADIO_INTENCLR01_CSTONESEND_Msk;
    NRF_TIMER10->INTENCLR = TIMER_INTENSET_COMPARE3_Msk;
    NRF_RADIO->SHORTS = 0;

    BLE_LL_ASSERT((irq00_en & RADIO_INTENSET00_DISABLED_Msk) && NRF_RADIO->EVENTS_DISABLED);

    assert(prev_transm != NULL);

    next_transm = prev_transm->next;

    if(g_ble_phy_cs.report_sync_results) {
        if (NRF_RADIO->EVENTS_ADDRESS == 0) {
            /* Out of sync */
            ble_phy_cs_subevent_end(BLE_PHY_CS_STATUS_SYNC_LOST);
            return;
        }

        sync_result = &g_ble_phy_cs.sync_results;

        if (g_ble_phy_cs.role == BLE_PHY_CS_ROLE_INITIATOR) {
            tod_ticks00 = NRF_TIMER00->CC[0];
            toa_ticks00 = NRF_TIMER00->CC[1];
        } else {
            toa_ticks00 = NRF_TIMER00->CC[0];
            tod_ticks00 = NRF_TIMER00->CC[1];
        }
        sync_result->time_of_departure_ns = ticks00_to_ns(tod_ticks00) +
                txaddrdelay_ns[g_ble_phy_cs.phy_mode];
        sync_result->time_of_arrival_ns = ticks00_to_ns(toa_ticks00) -
                rxaddrdelay_ns[g_ble_phy_cs.phy_mode];
    }

    ble_phy_cs_radio_events_clear();

    if(g_ble_phy_cs.report_tone_results) {
        pct16_reg = NRF_RADIO->CSTONES.PCT16;
        I16 = (int16_t)(pct16_reg & 0xFFFF);
        Q16 = (int16_t)((pct16_reg >> 16) & 0xFFFF);

        measured_freq_offset = ble_phy_cs_ffoest_get();
#if MYNEWT_VAL(BLE_PHY_CS_USE_PCT_CORRECTION)
        ble_phy_cs_pct_correct_ciruit_delay(&I16, &Q16, prev_transm->channel);
#endif

        tone_result = &g_ble_phy_cs.tone_results[0];
        tone_result->I16 = I16;
        tone_result->Q16 = Q16;
        tone_result->measured_freq_offset = measured_freq_offset;
    }

    /* The step has been completed. */
    rc = ble_ll_cs_step_end(prev_transm, tone_result, sync_result);
    if (rc) {
        ble_phy_cs_subevent_end(BLE_PHY_CS_STATUS_SYNC_LOST);
    }

    if (next_transm) {
        ble_phy_cs_schedule_step(g_ble_phy_cs.step_anchor_ticks, next_transm, g_ble_phy_cs.role);
    } else {
        ble_phy_cs_subevent_end(BLE_PHY_CS_STATUS_COMPLETE);
    }

    os_trace_isr_exit();
}

int
ble_phy_cs_subevent_start(struct ble_phy_cs_transmission *transm,
                          uint32_t cputime, uint8_t rem_usecs)
{
    int rc;

    /* Disable all PPI */
    phy_ppi_timer0_compare3_to_radio_disable_disable();
    phy_ppi_radio_bcmatch_to_aar_start_disable();
    phy_ppi_radio_address_to_ccm_crypt_disable();
    phy_ppi_timer0_compare2_to_radio_start_disable();
    phy_ppi_timer0_compare0_to_radio_txen_disable();
    phy_ppi_timer0_compare0_to_radio_rxen_disable();
    phy_ppi_timer0_compare1_to_radio_rxen_disable();
    phy_ppi_timer0_compare4_to_radio_cstonesstart_disable();

    g_ble_phy_cs.phy_mode = BLE_PHY_MODE_1M;
    g_ble_phy_cs.prev_transm = NULL;

    g_ble_phy_cs.role = transm->is_tx ? BLE_PHY_CS_ROLE_INITIATOR : BLE_PHY_CS_ROLE_REFLECTOR;

    phy_ppi_cs_mode_enable();

    /* CS SYNC packet has no PDU or CRC */
    NRF_RADIO->CRCCNF = 0;
    /* CS_SYNC needs only PAYLOAD field, so do not transmit S0, LENGTH and S1 fields.
     * Set the preambule length to 8 bits for LE 1M.
     * Set the S1 length to 4 bits to fit a CS trailer.
     */
    NRF_RADIO->PCNF0 = (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos)
                        | (4 << RADIO_PCNF0_S1LEN_Pos);
    /* Disable whitening */
    NRF_RADIO->PCNF1 = (NRF_BALEN << RADIO_PCNF1_BALEN_Pos) |
                       (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos);
    NRF_RADIO->RXADDRESSES = (1 << 1);

    NRF_RADIO->RTT.CONFIG = (RADIO_RTT_CONFIG_EN_Enabled << RADIO_RTT_CONFIG_EN_Pos) |
        ((g_ble_phy_cs.role == BLE_PHY_CS_ROLE_INITIATOR) ?
        RADIO_RTT_CONFIG_ROLE_Initiator << RADIO_RTT_CONFIG_ROLE_Pos :
        RADIO_RTT_CONFIG_ROLE_Reflector << RADIO_RTT_CONFIG_ROLE_Pos);

    NRF_RADIO->SHORTS = 0;

    ble_phy_cs_tune_set();

    /* Make sure all interrupts are disabled */
    nrf_radio_int_disable(NRF_RADIO, NRF_RADIO_IRQ_MASK_ALL);

    /* Set interrupt handler for Channel Sounding api */
    g_ble_phy_cs.prev_radio_isr_handler = NVIC_GetVector(RADIO_IRQn);
    NVIC_SetVector(RADIO_IRQn, (uint32_t)ble_phy_cs_isr);

    /* Clear events */
    ble_phy_cs_radio_events_clear();

    nrf_timer_task_trigger(NRF_TIMER10, NRF_TIMER_TASK_STOP);
    nrf_timer_task_trigger(NRF_TIMER10, NRF_TIMER_TASK_CLEAR);
#if 1
    nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_STOP);
    nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_CLEAR);
    NRF_TIMER00->BITMODE = 3;    /* 32-bit timer */
    NRF_TIMER00->MODE = 0;       /* Timer mode */
    NRF_TIMER00->PRESCALER = 0;  /* gives us 128 MHz */
    nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_START);
#endif

    phy_ppi_debug_enable();

    rc = ble_phy_cs_radio_timer_start(cputime, rem_usecs, transm->is_tx);
    if (rc) {
        ble_phy_cs_disable();
        return 1;
    }

    rc = ble_phy_cs_schedule_step(g_ble_phy_cs.step_anchor_ticks, transm, g_ble_phy_cs.role);
    if (rc) {
        ble_phy_cs_disable();
        return 1;
    }

    return 0;
}

#if MYNEWT_VAL(BLE_PHY_CS_TEST_MODE_CONTINUES_TONE)
void
ble_phy_cs_tx_continuous_tone(void)
{
    int rc = 0;

    os_trace_isr_enter();

    ble_phy_rfclk_enable();

    phy_ppi_cs_mode_enable();
    ble_phy_cs_radio_events_clear();

    /* Make sure all interrupts are disabled */
    nrf_radio_int_disable(NRF_RADIO, NRF_RADIO_IRQ_MASK_ALL);

    NRF_RADIO->SHORTS = 0;
    ble_phy_cs_rtt_set(NULL, 0);
    ble_phy_cs_channel_set(MYNEWT_VAL(BLE_PHY_CS_TEST_MODE_CONTINUES_TONE_CHANNEL));
    ble_phy_cs_tone_configure(80, BLE_PHY_CS_TONE_MODE_FM);
    ble_phy_cs_tune_set();

    NRF_RADIO->TASKS_TXEN = 1;

    while (1);

    os_trace_isr_exit();

    return;
}
#endif
#endif
