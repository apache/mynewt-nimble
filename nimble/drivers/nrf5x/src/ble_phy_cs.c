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

#if MYNEWT_VAL(BLE_CHANNEL_SOUNDING)

#if BABBLESIM
#define NRF_TIMER00 NRF_TIMER0
#define TIMER00_IRQn TIMER0_IRQn
#endif

/* Various radio timings */
/* Radio ramp-up times in usecs (fast mode) */
#define BLE_PHY_T_TXENFAST      (40)
#define BLE_PHY_T_RXENFAST      (40)

#define BLE_PHY_CS_MAX_SYNC_LEN    (21)

#define CSSYNC_TRAILER_NS  (4000)

#define PENDING_TRANSM_COUNT_MAX (6)
#define TONE_RESULT_COUNT_MAX (5)

#define BLE_PHY_CS_ROLE_INITIATOR (0)
#define BLE_PHY_CS_ROLE_REFLECTOR (1)

static uint32_t g_ble_phy_tx_buf[(BLE_PHY_MAX_PDU_LEN + 3) / 4];
static uint32_t g_ble_phy_rx_buf[(BLE_PHY_MAX_PDU_LEN + 3) / 4];

struct ble_phy_cs_obj
{
    uint32_t prev_isr_handler;
    uint32_t phy_start_cputime;
    uint32_t tone_end_anchor;
    uint8_t phy_state;
    uint8_t phy_mode;
    uint16_t phase_shift;
    uint8_t role;
    uint8_t tone_offset_usecs;
    struct ble_phy_cs_transmission *pending_transm[PENDING_TRANSM_COUNT_MAX];
    struct ble_phy_cs_sync_results sync_results;
    struct ble_phy_cs_tone_results tone_results[TONE_RESULT_COUNT_MAX];
    struct ble_phy_cs_subevent_results subevent_results;
};

static struct ble_phy_cs_obj g_ble_phy_cs;
extern void nrf_wait_disabled(void);

/* delay between EVENTS_READY and start of tx */
const uint16_t txdelay_ns[BLE_PHY_NUM_MODE] = {
#if BABBLESIM
    [BLE_PHY_MODE_1M] = 1000,
#else
    [BLE_PHY_MODE_1M] = 1600, /* ~1.6us */
#endif
};
/* delay between EVENTS_ADDRESS and txd access address  */
const uint16_t txaddrdelay_ns[BLE_PHY_NUM_MODE] = {
#if BABBLESIM
    [BLE_PHY_MODE_1M] = 1000,
#else
    [BLE_PHY_MODE_1M] = 3200, /* ~3.2us */
#endif
};
/* delay between rxd access address (w/ TERM1 for coded) and EVENTS_ADDRESS */
const uint16_t rxaddrdelay_ns[BLE_PHY_NUM_MODE] = {
#if BABBLESIM
    [BLE_PHY_MODE_1M] = 9000,
#else
    [BLE_PHY_MODE_1M] = 9200, /* ~9.2us */
#endif
};

static uint32_t
ticks_to_ns(uint32_t ticks) {
#if BABBLESIM
    return ticks * 1000;
#else
    return (uint32_t)(((uint64_t)ticks * 1000000000ULL) / 128000000ULL);
#endif
}

static uint32_t
ns_to_ticks(uint32_t ns) {
#if BABBLESIM
    return ns / 1000;
#else
    return (uint32_t)(((uint64_t)ns * 128) / 1000);
#endif
}

static uint32_t
usecs_to_ticks(uint32_t usecs) {
#if BABBLESIM
    return usecs;
#else
    return usecs * 128;
#endif
}

static uint32_t
ticks_to_usecs(uint32_t ticks) {
#if BABBLESIM
    return ticks;
#else
    return (uint32_t)(((uint64_t)ticks * 1000000ULL) / 128000000ULL);
#endif
}

static uint32_t
usecs_to_ns(uint32_t usecs) {
    return usecs * 1000;
}

static uint32_t
ns_to_usecs(uint32_t ns) {
    return ns / 1000;
}

static int
ble_phy_cs_set_start_time(uint32_t cputime, uint8_t rem_us, bool tx)
{
    uint32_t next_cc;
    uint32_t cur_cc;
    uint32_t cntr;
    uint32_t delta;
    int radio_rem_ns;
    int rem_ns_corr;

    /* Calculate rem_us for radio and FEM enable. The result may be a negative
     * value, but we'll adjust later.
     */
    if (tx) {
        radio_rem_ns = usecs_to_ns(rem_us - BLE_PHY_T_TXENFAST) -
                       txdelay_ns[g_ble_phy_cs.phy_mode];
    } else {
        radio_rem_ns = usecs_to_ns(rem_us - BLE_PHY_T_TXENFAST);
    }

    /* We need to adjust rem_us values, so they are >=1 for TIMER0 compare
     * event to be triggered.
     */
    if (radio_rem_ns <= -30518) {
        /* rem_ns is -61035..-30518 */
        cputime -= 2;
        rem_ns_corr = 61035;
    } else {
        /* rem_us is -30517..0 */
        cputime -= 1;
        rem_ns_corr = 30517;
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
    nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_CLEAR);
    nrf_timer_cc_set(NRF_TIMER00, 0, ns_to_ticks(radio_rem_ns + rem_ns_corr));
    NRF_TIMER00->EVENTS_COMPARE[0] = 0;

    /* Set RTC compare to start TIMER0 */
    NRF_RTC0->EVENTS_COMPARE[0] = 0;
    nrf_rtc_cc_set(NRF_RTC0, 0, next_cc);
    nrf_rtc_event_enable(NRF_RTC0, RTC_EVTENSET_COMPARE0_Msk);

    phy_ppi_rtc0_compare0_to_timer00_start_enable();

    /* Store the cputime at which we set the RTC */
    g_ble_phy_cs.phy_start_cputime = cputime;
    g_ble_phy_cs.subevent_results.cputime = cputime;

    return 0;
}

static int
ble_phy_cs_wfr_enable_at(uint32_t end_time)
{
    /* wfr_secs is the time from rxen until timeout */
    nrf_timer_cc_set(NRF_TIMER00, 3, end_time);
    NRF_TIMER00->EVENTS_COMPARE[3] = 0;

    /* Enable wait for response PPI */
    phy_ppi_cs_wfr_enable();

    /* CC[1] is only used as a reference on ADDRESS event, we do not need it here. */
    nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_CAPTURE1);
    if (NRF_TIMER00->CC[1] > NRF_TIMER00->CC[3]) {
        phy_ppi_cs_wfr_disable();
        nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_DISABLE);
        return 1;
    }

    return 0;
}

static int
ble_phy_cs_channel_configure(uint32_t aa, uint8_t chan)
{
#if BABBLESIM
    ble_phy_setchan(chan % 40, aa, 0);
#else
    assert(!(chan <= 1 || (23 <= chan && chan <= 25) || 77 <= chan));

    /* Check for valid channel range */
    if (chan <= 1 || (23 <= chan && chan <= 25) || 77 <= chan) {
        return BLE_PHY_ERR_INV_PARAM;
    }

    NRF_RADIO->BASE0 = (aa << 8);
    NRF_RADIO->PREFIX0 = (NRF_RADIO->PREFIX0 & 0xFFFFFF00) | (aa >> 24);
    NRF_RADIO->FREQUENCY = 2 + chan;
#endif

    return 0;
}

//#define NRF_S1LEN_BITS 4

static inline int
ble_phy_cs_sync_configure(struct ble_phy_cs_transmission *transm)
{
    uint8_t *dptr;
    uint8_t hdr_byte;

#if !BABBLESIM
    /* CS SYNC packet has no PDU or CRC */
    NRF_RADIO->CRCCNF = 0; //(RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos);
    /* CS_SYNC needs only PAYLOAD field, so do not transmit S0, LENGTH and S1 fields. */
    NRF_RADIO->PCNF0 = 0;
//            (4 << RADIO_PCNF0_S1LEN_Pos) |
//                       (RADIO_PCNF0_S1INCL_Include << RADIO_PCNF0_S1INCL_Pos);
    /* Disable whitening */
    NRF_RADIO->PCNF1 =
//            (RADIO_PCNF1_ENDIAN_Little <<  RADIO_PCNF1_ENDIAN_Pos) |
                       (NRF_BALEN << RADIO_PCNF1_BALEN_Pos);
//    NRF_RADIO->RTT.CONFIG = RADIO_RTT_CONFIG_EN_Enabled << RADIO_RTT_CONFIG_EN_Pos |
//                            RADIO_RTT_CONFIG_ENFULLAA_Enabled << RADIO_RTT_CONFIG_ENFULLAA_Pos |
//                            RADIO_RTT_CONFIG_ROLE_Reflector << RADIO_RTT_CONFIG_ROLE_Pos |
//                            (256UL) << RADIO_RTT_CONFIG_EFSDELAY_Pos;
#endif

    if (transm->is_tx) {
        dptr = (uint8_t *)&g_ble_phy_tx_buf[0];
#if BABBLESIM
        memset(dptr, 0, 4);
#else
        if (NRF_RADIO->PREFIX0 & 0x80) {
            dptr[0] = 0b10101010;
            dptr[1] = 0b10101010;
            dptr[2] = 0b10101010;
            dptr[3] = 0b10101010;
        } else {
            dptr[0] = 0b01010101;
            dptr[1] = 0b01010101;
            dptr[2] = 0b01010101;
            dptr[3] = 0b01010101;
        }
#endif
    } else {
        dptr = (uint8_t *)&g_ble_phy_rx_buf[0];
    }

    NRF_RADIO->PACKETPTR = (uint32_t)dptr;

    return 0;
}

static int
ble_phy_cs_tone_configure(struct ble_phy_cs_transmission *transm)
{
#if !BABBLESIM
    uint16_t phase_shift = 0;
    uint8_t numsamples;

    NRF_RADIO->CSTONES.NEXTFREQUENCY = 2 + transm->channel;
    NRF_RADIO->CSTONES.FFOIN = RADIO_CSTONES_FFOIN_ResetValue;
    NRF_RADIO->CSTONES.FFOSOURCE = RADIO_CSTONES_FFOSOURCE_ResetValue;

    if (transm->is_tx) {
        NRF_RADIO->CSTONES.NUMSAMPLES = RADIO_CSTONES_NUMSAMPLES_ResetValue;
    } else {
        assert(transm->duration_usecs <= 80);
        g_ble_phy_cs.tone_offset_usecs = 1;
        numsamples = (transm->duration_usecs - 2 * g_ble_phy_cs.tone_offset_usecs) * 160 / 80;
        NRF_RADIO->CSTONES.NUMSAMPLES = numsamples;
        NRF_RADIO->CSTONES.NUMSAMPLESCOEFF = (65536 * 16) / numsamples;
    }
    NRF_RADIO->CSTONES.DOWNSAMPLE = 0;

    if (transm->tone_mode == BLE_PHY_CS_TONE_MODE_FM) {
        NRF_RADIO->CSTONES.MODE = RADIO_CSTONES_MODE_TFM_Msk;
    } else {
        NRF_RADIO->CSTONES.MODE = RADIO_CSTONES_MODE_TPM_Msk;
        if (transm->is_tx && g_ble_phy_cs.role == BLE_PHY_CS_ROLE_REFLECTOR) {
            phase_shift = g_ble_phy_cs.phase_shift;
        }
    }

    NRF_RADIO->CSTONES.PHASESHIFT = phase_shift;
#endif

    return 0;
}

static void
ble_phy_cs_disable(void)
{
#ifdef NRF54L_SERIES
    nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_STOP);
#endif
    phy_ppi_timer00_compare3_to_radio_disable_disable();
    phy_ppi_timer00_compare2_to_radio_start_disable();
    phy_ppi_timer00_compare4_to_radio_cstonesstart_disable();
    phy_ppi_cs_mode_disable();

    nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_STOP);
    phy_ppi_cs_wfr_disable();
    phy_ppi_timer00_compare0_to_radio_txen_disable();
    phy_ppi_timer00_compare0_to_radio_rxen_disable();
    phy_ppi_rtc0_compare0_to_timer00_start_disable();
    ble_phy_disable();

    ble_phy_transition_set(BLE_PHY_TRANSITION_NONE, 0);

#if !BABBLESIM
    NRF_RADIO->CSTONES.MODE = 0;
    NRF_RADIO->RTT.CONFIG = 0;

    /* Configure back the registers */
    NRF_RADIO->CRCCNF = (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos) | RADIO_CRCCNF_LEN_Three;
    NRF_RADIO->PCNF0 = NRF_PCNF0;
    NRF_RADIO->PCNF1 = NRF_MAXLEN |
                       (RADIO_PCNF1_ENDIAN_Little <<  RADIO_PCNF1_ENDIAN_Pos) |
                       (NRF_BALEN << RADIO_PCNF1_BALEN_Pos) |
                       RADIO_PCNF1_WHITEEN_Msk;
//    phy_ppi_debug_disable();
#endif

    NVIC_SetVector(RADIO_IRQn, (uint32_t)g_ble_phy_cs.prev_isr_handler);

    memset(g_ble_phy_cs.pending_transm, 0, sizeof(g_ble_phy_cs.pending_transm));
    g_ble_phy_cs.phase_shift = 0;
}

static void
ble_phy_cs_subevent_end(int status)
{
    ble_phy_cs_disable();
    g_ble_phy_cs.subevent_results.status = status;
    ble_ll_cs_subevent_end(&g_ble_phy_cs.subevent_results);
}

static inline void
ble_phy_cs_prepare_radio(struct ble_phy_cs_transmission *prev_transm,
                         uint32_t *anchor_ticks,
                         uint32_t *start_offset_ns,
                         uint8_t is_tx)
{
    uint32_t radio_offset_ns;
    uint8_t phy_mode = g_ble_phy_cs.phy_mode;
    uint8_t prev_phy_state = g_ble_phy_cs.phy_state;

    *start_offset_ns = usecs_to_ns(prev_transm->end_tifs);

    if (prev_transm->mode == BLE_PHY_CS_TRANSM_MODE_SYNC) {
        *anchor_ticks = NRF_TIMER00->CC[1];
        *start_offset_ns += (prev_phy_state == BLE_PHY_STATE_TX)
                            ? txaddrdelay_ns[phy_mode]
                            : -rxaddrdelay_ns[phy_mode];
        *start_offset_ns += CSSYNC_TRAILER_NS;
    } else {
        *anchor_ticks = g_ble_phy_cs.tone_end_anchor;
    }

    /* Enable radio based on TX/RX state */
    if (NRF_RADIO->STATE == RADIO_STATE_STATE_Disabled) {
        radio_offset_ns = *start_offset_ns;
        if (is_tx) {
            radio_offset_ns -= usecs_to_ns(BLE_PHY_T_TXENFAST);
            radio_offset_ns -= txdelay_ns[phy_mode];
            phy_ppi_timer00_compare0_to_radio_txen_enable();
        } else {
            radio_offset_ns -= usecs_to_ns(BLE_PHY_T_RXENFAST);
            radio_offset_ns -= 10000;
            phy_ppi_timer00_compare0_to_radio_rxen_enable();
        }

        nrf_timer_cc_set(NRF_TIMER00, 0, *anchor_ticks + ns_to_ticks(radio_offset_ns));
        NRF_TIMER00->EVENTS_COMPARE[0] = 0;
    }

    g_ble_phy_cs.phy_state = is_tx ? BLE_PHY_STATE_TX : BLE_PHY_STATE_RX;
}

static inline uint32_t
ble_phy_cs_calc_tone_chain_offset(struct ble_phy_cs_transmission *first_tone)
{
    struct ble_phy_cs_transmission *t;
    uint32_t offset_ns = 0;

    t = first_tone;
    while (t && t->mode == BLE_PHY_CS_TRANSM_MODE_TONE) {
        offset_ns += usecs_to_ns(t->duration_usecs);
        offset_ns += usecs_to_ns(t->end_tifs);
        t = t->next;
    }

    return offset_ns;
}

static int myiqcount = 0;
static int myphaseshiftcount = 0;
static int16_t my_tone_results_I16[30];
static int16_t my_tone_results_Q16[30];
static uint16_t my_tone_results_PHASE[30];
static uint16_t myphaseshift[30];

static inline void
ble_phy_cs_handle_tones(struct ble_phy_cs_transmission *first_tone,
                        uint32_t anchor_ticks, uint32_t tone_offset_ns,
                        uint8_t handle_tx_end)
{
    uint32_t pct16_reg;
    struct ble_phy_cs_transmission *t = first_tone;
    uint8_t is_tx = t->is_tx;
    uint8_t tone_mode = first_tone->tone_mode;
    uint8_t i;

    for (i = 0; i < TONE_RESULT_COUNT_MAX; i++) {
        if (!is_tx) {
            RADIO_EVENTS_CSTONESEND = 0;
            nrf_timer_cc_set(NRF_TIMER00, 4, anchor_ticks + ns_to_ticks(tone_offset_ns) +
                             usecs_to_ticks(g_ble_phy_cs.tone_offset_usecs));
            NRF_TIMER00->EVENTS_COMPARE[4] = 0;

            nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_CAPTURE1);
            if (NRF_TIMER00->CC[1] > NRF_TIMER00->CC[4] && NRF_TIMER00->EVENTS_COMPARE[4] == 0) {
                BLE_LL_ASSERT(0);
                break;
            }

            while (NRF_TIMER00->EVENTS_COMPARE[4] == 0);
            RADIO_TASKS_CSTONESSTART = 1;
            RADIO_TASKS_CSTONESSTART = 1;
            while (RADIO_EVENTS_CSTONESEND == 0);

            if (tone_mode == BLE_PHY_CS_TONE_MODE_FM) {
                /* Made an assumption that FREQOFFSET is in the same units as FFOEST, 62.5ppb
                 * (only FFOEST specified in spec).
                 */
                g_ble_phy_cs.tone_results[i].measured_freq_offset =
                    NRF_RADIO->CSTONES.FREQOFFSET & RADIO_CSTONES_FREQOFFSET_FREQOFFSET_Msk;
            } else {
                pct16_reg = NRF_RADIO->CSTONES.PCT16;
                g_ble_phy_cs.tone_results[i].I16 = (int16_t)(pct16_reg & 0xFFFF);
                g_ble_phy_cs.tone_results[i].Q16 = (int16_t)((pct16_reg >> 16) & 0xFFFF);

                if (myiqcount < 30) {
                    my_tone_results_PHASE[myiqcount] = NRF_RADIO->CSTONES.MAGPHASEMEAN & 0xFFFF;
                    my_tone_results_I16[myiqcount] = g_ble_phy_cs.tone_results[i].I16;
                    my_tone_results_Q16[myiqcount++] = g_ble_phy_cs.tone_results[i].Q16;
                    myphaseshift[myphaseshiftcount++] = NRF_RADIO->CSTONES.MAGPHASEMEAN & 0xFFFF;
                }
            }
        }

        tone_offset_ns += usecs_to_ns(t->duration_usecs);
        first_tone = t;
        t = t->next;

        if (!(t && t->is_tx == is_tx && t->mode == BLE_PHY_CS_TRANSM_MODE_TONE)) {
            break;
        }

        tone_offset_ns += usecs_to_ns(first_tone->end_tifs);
    }

    g_ble_phy_cs.tone_end_anchor = anchor_ticks + ns_to_ticks(tone_offset_ns);

    if (is_tx) {
        nrf_timer_cc_set(NRF_TIMER00, 4, g_ble_phy_cs.tone_end_anchor);
        NRF_TIMER00->EVENTS_COMPARE[4] = 0;

        nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_CAPTURE1);
        if (NRF_TIMER00->CC[1] < NRF_TIMER00->CC[4]) {
            while (handle_tx_end && NRF_TIMER00->EVENTS_COMPARE[4] == 0);
        }
    } else if (tone_mode == BLE_PHY_CS_TONE_MODE_PM && g_ble_phy_cs.role == BLE_PHY_CS_ROLE_REFLECTOR) {
        g_ble_phy_cs.phase_shift = NRF_RADIO->CSTONES.MAGPHASEMEAN & 0xFFFF;
    }

    if (handle_tx_end) {
        NRF_RADIO->TASKS_DISABLE = 1;
    }
}

static volatile uint32_t myconsumed_sync_ns = 0;
static volatile uint32_t mysync_offset_ns = 0;
static volatile uint32_t mytone_offset_ns = 0;

static int
ble_phy_cs_transition(void)
{
    int rc = 0;
    struct ble_phy_cs_transmission *prev_transm;
    struct ble_phy_cs_transmission *first_transm;
    struct ble_phy_cs_transmission *sync_transm = NULL;
    struct ble_phy_cs_transmission *tone_transm = NULL;
    struct ble_phy_cs_transmission *t;
    uint32_t anchor_ticks = 0;
    uint32_t start_offset_ns = 0;
    uint32_t tone_chain_ns = 0;
    uint32_t consumed_sync_ns = 0;
    uint32_t sync_offset_ns = 0;
    uint32_t tone_offset_ns = 0;
    uint32_t wfr_ns;
    uint32_t channel = 0;
    uint8_t aa = 0;
    uint8_t is_tx;
    uint8_t i;

    for (i = PENDING_TRANSM_COUNT_MAX; i > 0; i--) {
        prev_transm = g_ble_phy_cs.pending_transm[i - 1];
        if (prev_transm != NULL) {
            break;
        }
    }
    memset(g_ble_phy_cs.pending_transm, 0, sizeof(g_ble_phy_cs.pending_transm));

    BLE_LL_ASSERT(prev_transm != NULL);

    first_transm = prev_transm->next;
    if (first_transm == NULL) {
        ble_phy_cs_subevent_end(BLE_PHY_CS_STATUS_COMPLETE);
        return 0;
    }

    if (prev_transm->is_tx != first_transm->is_tx) {
        nrf_wait_disabled();
    }

    is_tx = first_transm->is_tx;

    /* Clear radio events */
#ifdef NRF54L_SERIES
    NRF_RADIO->EVENTS_PHYEND = 0;
#endif
    NRF_RADIO->EVENTS_READY = 0;
    NRF_RADIO->EVENTS_TXREADY = 0;
    NRF_RADIO->EVENTS_RXREADY = 0;
    NRF_RADIO->EVENTS_ADDRESS = 0;
    NRF_RADIO->EVENTS_END = 0;
    NRF_RADIO->EVENTS_DISABLED = 0;
    RADIO_EVENTS_CSTONESEND = 0;
    NRF_RADIO->CSTONES.MODE = 0;
    NRF_RADIO->RTT.CONFIG = 0;
    NRF_RADIO->SHORTS = 0;
    RADIO_EVENTS_CSTONESEND = 0;
    nrf_radio_int_enable(NRF_RADIO, RADIO_INTENSET_DISABLED_Msk);

    t = first_transm;
    i = 0;
    /* Find SYNC and first TONE */
    while (t && t->is_tx == is_tx) {
        assert(i < PENDING_TRANSM_COUNT_MAX);
        g_ble_phy_cs.pending_transm[i++] = t;

        if (t->mode == BLE_PHY_CS_TRANSM_MODE_SYNC) {
            sync_transm = t;
        } else if (t->mode == BLE_PHY_CS_TRANSM_MODE_TONE && !tone_transm) {
            tone_transm = t;
        }

        t = t->next;
    }

    if (sync_transm) {
        ble_phy_cs_sync_configure(sync_transm);
        aa = sync_transm->aa;
        channel = sync_transm->channel;
    }

    if (tone_transm) {
        ble_phy_cs_tone_configure(tone_transm);
        channel = tone_transm->channel;
    }

    ble_phy_cs_channel_configure(aa, channel);

    ble_phy_cs_prepare_radio(prev_transm, &anchor_ticks, &start_offset_ns, is_tx);
    tone_offset_ns = start_offset_ns;
    sync_offset_ns = start_offset_ns;

    if (sync_transm) {
        if (first_transm == tone_transm) {
            /* If TONEs are before SYNC, add their total time (ns) to sync offset */
            tone_chain_ns = ble_phy_cs_calc_tone_chain_offset(tone_transm);
            sync_offset_ns += tone_chain_ns;

            nrf_timer_cc_set(NRF_TIMER00, 2, anchor_ticks + ns_to_ticks(sync_offset_ns));
            NRF_TIMER00->EVENTS_COMPARE[2] = 0;
            phy_ppi_timer00_compare2_to_radio_start_enable();

            nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_CAPTURE1);
            if (NRF_TIMER00->CC[1] > NRF_TIMER00->CC[2]) {
                return 1;
            }
        } else {
            NRF_RADIO->SHORTS |= RADIO_SHORTS_READY_START_Msk;
            nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_CAPTURE1);
            if (NRF_TIMER00->CC[1] > NRF_TIMER00->CC[0]) {
                return 1;
            }
        }

        if (!is_tx) {
            wfr_ns = sync_offset_ns
                     + usecs_to_ns(sync_transm->duration_usecs)
                     + rxaddrdelay_ns[g_ble_phy_cs.phy_mode]
                     + 50000;
            rc = ble_phy_cs_wfr_enable_at(anchor_ticks + ns_to_ticks(wfr_ns));
            if (rc) {
                return rc;
            }
        }

        consumed_sync_ns = usecs_to_ns(sync_transm->duration_usecs) + usecs_to_ns(sync_transm->end_tifs);
        myconsumed_sync_ns = consumed_sync_ns;

        if (!sync_transm->next || sync_transm->next->is_tx != is_tx) {
            /* If SYNC is at the end, use SHORTS to disable the radio */
            NRF_RADIO->SHORTS |= RADIO_SHORTS_PHYEND_DISABLE_Msk;
        }

        if (tone_transm && first_transm == sync_transm) {
            /* If TONEs are after SYNC, set tone_transm and tone_offset_ns accordingly */
            tone_offset_ns = sync_offset_ns + consumed_sync_ns;
        }
    }

    tone_offset_ns += 3000;
    mysync_offset_ns = sync_offset_ns;
    mytone_offset_ns = tone_offset_ns;

    if (tone_transm) {
        ble_phy_cs_handle_tones(tone_transm, anchor_ticks, tone_offset_ns,
                                sync_offset_ns <= tone_offset_ns);
    }

    return 0;
}

static void
ble_phy_cs_sync_tx_end_isr(struct ble_phy_cs_transmission *transm,
                           struct ble_phy_cs_sync_results *results)
{
    assert(g_ble_phy_cs.phy_state == BLE_PHY_STATE_TX);

    g_ble_phy_cs.sync_results.cputime = g_ble_phy_cs.phy_start_cputime;
    g_ble_phy_cs.sync_results.rem_ns = ticks_to_ns(NRF_TIMER00->CC[1]) +
        txaddrdelay_ns[g_ble_phy_cs.phy_mode] + CSSYNC_TRAILER_NS;

    g_ble_phy_cs.subevent_results.rem_ns = g_ble_phy_cs.sync_results.rem_ns;

    ble_ll_cs_sync_tx_end(transm, results);
}

static void
ble_phy_cs_sync_rx_end_isr(struct ble_phy_cs_transmission *transm,
                           struct ble_phy_cs_sync_results *results)
{
    assert(g_ble_phy_cs.phy_state == BLE_PHY_STATE_RX);

    g_ble_phy_cs.sync_results.cputime = g_ble_phy_cs.phy_start_cputime;
    g_ble_phy_cs.sync_results.rem_ns = ticks_to_ns(NRF_TIMER00->CC[1]) -
        rxaddrdelay_ns[g_ble_phy_cs.phy_mode] + CSSYNC_TRAILER_NS;

    g_ble_phy_cs.subevent_results.rem_ns = g_ble_phy_cs.sync_results.rem_ns;

    ble_ll_cs_sync_rx_end(transm, results);
}

static void
ble_phy_cs_tone_tx_end_isr(struct ble_phy_cs_transmission *transm,
                           struct ble_phy_cs_tone_results *results)
{
    g_ble_phy_cs.subevent_results.rem_ns = ticks_to_ns(NRF_TIMER00->CC[5]);

    ble_ll_cs_tone_tx_end(transm, results);
}

static void
ble_phy_cs_tone_rx_end_isr(struct ble_phy_cs_transmission *transm,
                           struct ble_phy_cs_tone_results *results)
{
    g_ble_phy_cs.subevent_results.rem_ns = ticks_to_ns(NRF_TIMER00->CC[5]);

    ble_ll_cs_tone_rx_end(transm, results);
}

static int
ble_phy_cs_sync_end(struct ble_phy_cs_transmission *transm,
                    struct ble_phy_cs_sync_results *results)
{
    switch (g_ble_phy_cs.phy_state) {
    case BLE_PHY_STATE_RX:
        if (NRF_RADIO->EVENTS_ADDRESS) {
            ble_phy_cs_sync_rx_end_isr(transm, results);
        } else {
            g_ble_phy_cs.subevent_results.rem_ns = ticks_to_ns(NRF_TIMER00->CC[3]);
            ble_phy_cs_subevent_end(BLE_PHY_CS_STATUS_SYNC_LOST);
            return 1;
        }
        break;
    case BLE_PHY_STATE_TX:
        ble_phy_cs_sync_tx_end_isr(transm, results);
        break;
    default:
        BLE_LL_ASSERT(0);
    }

    return 0;
}

static int
ble_phy_cs_tone_end(struct ble_phy_cs_transmission *transm,
                    struct ble_phy_cs_tone_results *results)
{
    switch (g_ble_phy_cs.phy_state) {
    case BLE_PHY_STATE_RX:
        if (RADIO_EVENTS_CSTONESEND) {
            ble_phy_cs_tone_rx_end_isr(transm, results);
        } else {
            ble_phy_cs_subevent_end(BLE_PHY_CS_STATUS_SYNC_LOST);
            return 1;
        }
        break;
    case BLE_PHY_STATE_TX:
        ble_phy_cs_tone_tx_end_isr(transm, results);
        break;
    default:
        BLE_LL_ASSERT(0);
    }

    return 0;
}

static void
ble_phy_cs_isr(void)
{
    int rc = 0;
    struct ble_phy_cs_transmission *transm;
    struct ble_phy_cs_tone_results *tone_result;
    uint8_t i;

    os_trace_isr_enter();

    /* Disable PPI */
    phy_ppi_cs_wfr_disable();
    phy_ppi_timer00_compare3_to_radio_disable_disable();
    phy_ppi_timer00_compare2_to_radio_start_disable();
    phy_ppi_timer00_compare0_to_radio_txen_disable();
    phy_ppi_timer00_compare0_to_radio_rxen_disable();
    phy_ppi_timer00_compare4_to_radio_cstonesstart_disable();

    /* Make sure all interrupts are disabled */
    nrf_radio_int_disable(NRF_RADIO, NRF_RADIO_IRQ_MASK_ALL);

    NRF_RADIO->SHORTS = 0;
#ifdef NRF54L_SERIES
    NRF_RADIO->EVENTS_PHYEND = 0;
#endif
    NRF_RADIO->EVENTS_END = 0;
    NRF_RADIO->EVENTS_DISABLED = 0;

    tone_result = &g_ble_phy_cs.tone_results[0];

    for (i = 0; i < PENDING_TRANSM_COUNT_MAX; i++) {
        transm = g_ble_phy_cs.pending_transm[i];
        if (transm == NULL) {
            break;
        }

        if (transm->mode == BLE_PHY_CS_TRANSM_MODE_SYNC) {
            rc = ble_phy_cs_sync_end(transm, &g_ble_phy_cs.sync_results);
        } else {
            rc = ble_phy_cs_tone_end(transm, tone_result++);
        }
    }

    if (rc == 0) {
        rc = ble_phy_cs_transition();
        if (rc) {
            memset(g_ble_phy_cs.pending_transm, 0, sizeof(g_ble_phy_cs.pending_transm));
            ble_phy_cs_subevent_end(BLE_PHY_CS_STATUS_COMPLETE);
        }
    }

    os_trace_isr_exit();
}

int
ble_phy_cs_subevent_start(struct ble_phy_cs_transmission *transm,
                          uint32_t cputime, uint8_t rem_usecs)
{
    int rc;
    uint32_t wfr_ns;

    BLE_LL_ASSERT(transm->mode == BLE_PHY_CS_TRANSM_MODE_SYNC);

    g_ble_phy_cs.phase_shift = 0;
    g_ble_phy_cs.phy_mode = BLE_PHY_MODE_1M;
    memset(g_ble_phy_cs.pending_transm, 0, sizeof(g_ble_phy_cs.pending_transm));
    g_ble_phy_cs.pending_transm[0] = transm;

    /* Disable all PPI */
    nrf_wait_disabled();
    phy_ppi_cs_wfr_disable();
    phy_ppi_radio_bcmatch_to_aar_start_disable();
    phy_ppi_radio_address_to_ccm_crypt_disable();
    phy_ppi_timer00_compare3_to_radio_disable_disable();
    phy_ppi_timer00_compare2_to_radio_start_disable();
    phy_ppi_timer00_compare0_to_radio_txen_disable();
    phy_ppi_timer00_compare0_to_radio_rxen_disable();
    phy_ppi_timer00_compare4_to_radio_cstonesstart_disable();

    phy_ppi_cs_mode_enable();

    /* Make sure all interrupts are disabled */
    nrf_radio_int_disable(NRF_RADIO, NRF_RADIO_IRQ_MASK_ALL);

    /* Set interrupt handler for Channel Sounding api */
    g_ble_phy_cs.prev_isr_handler = NVIC_GetVector(RADIO_IRQn);
    NVIC_SetVector(RADIO_IRQn, (uint32_t)ble_phy_cs_isr);

    /* Clear events */
    NRF_RADIO->EVENTS_ADDRESS = 0;
    NRF_RADIO->EVENTS_END = 0;
    NRF_RADIO->EVENTS_DISABLED = 0;
#ifdef NRF54L_SERIES
    NRF_RADIO->EVENTS_PHYEND = 0;
    RADIO_EVENTS_CSTONESEND = 0;
    NRF_RADIO->CSTONES.PHASESHIFT = 0;
#endif

#if !BABBLESIM
    nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_STOP);
    nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_CLEAR);
    NRF_TIMER00->BITMODE = 3;    /* 32-bit timer */
    NRF_TIMER00->MODE = 0;       /* Timer mode */
    NRF_TIMER00->PRESCALER = 0;  /* gives us 128 MHz */
    phy_ppi_debug_enable();
#endif

    ble_phy_cs_channel_configure(transm->aa, transm->channel);

    rc = ble_phy_cs_sync_configure(transm);
    if (rc) {
        ble_phy_cs_disable();
        return 1;
    }

    NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_PHYEND_DISABLE_Msk;

    nrf_radio_int_enable(NRF_RADIO, RADIO_INTENSET_DISABLED_Msk);

    rc = ble_phy_cs_set_start_time(cputime, rem_usecs, false);
    if (rc) {
        ble_phy_cs_disable();
        return 1;
    }

    if (transm->is_tx) {
        g_ble_phy_cs.role = BLE_PHY_CS_ROLE_INITIATOR;
        g_ble_phy_cs.phy_state = BLE_PHY_STATE_TX;
        phy_ppi_timer00_compare0_to_radio_txen_enable();
    } else {
        g_ble_phy_cs.role = BLE_PHY_CS_ROLE_REFLECTOR;
        g_ble_phy_cs.phy_state = BLE_PHY_STATE_RX;
        phy_ppi_timer00_compare0_to_radio_rxen_enable();

        wfr_ns = usecs_to_ns(BLE_PHY_T_RXENFAST + transm->duration_usecs + 50);
        /* Adjust for delay between actual access address RX and EVENT_ADDRESS */
        wfr_ns += rxaddrdelay_ns[g_ble_phy_cs.phy_mode];

        ble_phy_cs_wfr_enable_at(NRF_TIMER00->CC[0] + ns_to_ticks(wfr_ns));
    }

    if (rc) {
        ble_phy_cs_disable();
        return 1;
    }

    return rc;
}
#endif
