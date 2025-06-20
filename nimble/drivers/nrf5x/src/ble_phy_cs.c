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
#include "bs_tracing.h"
#include <mcu/cmsis_nvic.h>

#if MYNEWT_VAL(BLE_CHANNEL_SOUNDING)
/* Various radio timings */
/* Radio ramp-up times in usecs (fast mode) */
#define BLE_PHY_T_TXENFAST      (40)
#define BLE_PHY_T_RXENFAST      (40)

#define BLE_PHY_CS_MAX_SYNC_LEN    (21)

static uint32_t g_ble_phy_tx_buf[(BLE_PHY_MAX_PDU_LEN + 3) / 4];

struct ble_phy_cs_obj
{
    uint32_t prev_isr_handler;
    uint32_t phy_start_cputime;
    uint8_t phy_state;
    uint8_t phy_mode;
    uint8_t phy_transition_late;
    uint8_t phy_rx_started;
    struct ble_phy_cs_transmission *cs_transm;
    struct ble_phy_cs_sync_results cs_sync_results;
    struct ble_phy_cs_tone_results cs_tone_results;
};

static struct ble_phy_cs_obj g_ble_phy_cs;

/* packet start offsets (in usecs) */
static const uint16_t g_ble_phy_mode_aa_end_off[BLE_PHY_NUM_MODE] = {
    [BLE_PHY_MODE_1M] = 40,
    [BLE_PHY_MODE_2M] = 24,
    [BLE_PHY_MODE_CODED_125KBPS] = 376,
    [BLE_PHY_MODE_CODED_500KBPS] = 376
};

/* delay between EVENTS_READY and start of tx */
extern const uint8_t g_ble_phy_t_txdelay[BLE_PHY_NUM_MODE];
/* delay between EVENTS_ADDRESS and txd access address  */
extern const uint8_t g_ble_phy_t_txaddrdelay[BLE_PHY_NUM_MODE];
/* delay between EVENTS_END and end of txd packet */
extern const uint8_t g_ble_phy_t_txenddelay[BLE_PHY_NUM_MODE];
/* delay between rxd access address (w/ TERM1 for coded) and EVENTS_ADDRESS */
extern const uint8_t g_ble_phy_t_rxaddrdelay[BLE_PHY_NUM_MODE];
/* delay between end of rxd packet and EVENTS_END */
extern const uint8_t g_ble_phy_t_rxenddelay[BLE_PHY_NUM_MODE];

extern void nrf_wait_disabled(void);
extern int ble_phy_set_start_time(uint32_t cputime, uint8_t rem_us, bool tx);
extern void ble_phy_wfr_enable_at(uint32_t end_time);

int
ble_phy_cs_tone_end_set(uint32_t duration_usecs, uint16_t end_tifs,
                        uint8_t phy_state, uint8_t phy_mode)
{
    uint32_t end_time;

    end_time = NRF_TIMER0->CC[0] + duration_usecs;
    if (phy_state == BLE_PHY_STATE_TX) {
        end_time += g_ble_phy_t_txdelay[phy_mode];
    }

    nrf_timer_cc_set(NRF_TIMER0, 3, end_time);
    NRF_TIMER0->EVENTS_COMPARE[3] = 0;

    nrf_timer_int_enable(NRF_TIMER0, TIMER_INTENSET_COMPARE3_Msk);
    if (end_tifs >= BLE_LL_IFS) {
        phy_ppi_timer0_compare3_to_radio_disable_enable();
    } else {
        phy_ppi_timer0_compare3_to_radio_stop_enable();
    }

    /* CC[1] is only used as a reference on RX start, we do not need it here so
     * it can be used to read TIMER0 counter.
     */
    nrf_timer_task_trigger(NRF_TIMER0, NRF_TIMER_TASK_CAPTURE1);
    if (NRF_TIMER0->CC[1] > NRF_TIMER0->CC[3]) {
        nrf_timer_int_disable(NRF_TIMER0, TIMER_INTENCLR_COMPARE3_Msk);
        phy_ppi_timer0_compare3_to_radio_disable_disable();
        phy_ppi_timer0_compare3_to_radio_stop_disable();
        nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_DISABLE);
    }

    return 0;
}

static int
ble_phy_cs_channel_configure(struct ble_phy_cs_transmission *transm)
{
    uint8_t chan = transm->channel;

#if BABBLESIM
    ble_phy_setchan(transm->channel % 40, transm->aa, 0);
#else
    assert(!(chan <= 1 || (23 <= chan && chan <= 25) || 77 <= chan));

    /* Check for valid channel range */
    if (chan <= 1 || (23 <= chan && chan <= 25) || 77 <= chan) {
        return BLE_PHY_ERR_INV_PARAM;
    }

    NRF_RADIO->FREQUENCY = 2 + chan;
#endif

    return 0;
}

static int
ble_phy_cs_sync_configure(struct ble_phy_cs_transmission *transm)
{
    uint8_t *dptr;
    uint32_t interrupts;
    uint8_t payload_len;
    uint8_t hdr_byte;

#if !BABBLESIM
    /* CS SYNC packet has no PDU or CRC */
    NRF_RADIO->CRCCNF = (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos);
    /* CS_SYNC needs only PAYLOAD field, so do not transmit S0, LENGTH and S1 fields. */
    NRF_RADIO->PCNF0 = RADIO_PCNF0_S1INCL_Include << RADIO_PCNF0_S1INCL_Pos;
    /* Disable whitening */
    NRF_RADIO->PCNF1 = (RADIO_PCNF1_ENDIAN_Little <<  RADIO_PCNF1_ENDIAN_Pos) |
                       (NRF_BALEN << RADIO_PCNF1_BALEN_Pos);
//    NRF_RADIO->RTT.CONFIG = RADIO_RTT_CONFIG_EN_Enabled << RADIO_RTT_CONFIG_EN_Pos |
//                            RADIO_RTT_CONFIG_ENFULLAA_Enabled << RADIO_RTT_CONFIG_ENFULLAA_Pos |
//                            RADIO_RTT_CONFIG_ROLE_Reflector << RADIO_RTT_CONFIG_ROLE_Pos |
//                            (256UL) << RADIO_RTT_CONFIG_EFSDELAY_Pos;
#endif

    /* No frames in CS tone */
    dptr = (uint8_t *)&g_ble_phy_tx_buf[0];
    if (transm->is_tx) {
        payload_len = ble_ll_cs_rtt_tx_make(&dptr[3], &hdr_byte);
        /* RAM representation has S0, LENGTH and S1 fields. (3 bytes) */
        dptr[0] = hdr_byte;
        dptr[1] = payload_len;
        dptr[2] = 0;
        interrupts = 0;
    } else {
        dptr += 3;
        interrupts = RADIO_INTENSET_ADDRESS_Msk;
    }

    NRF_RADIO->PACKETPTR = (uint32_t)dptr;

    if (transm->end_tifs >= BLE_LL_IFS) {
#ifdef NRF54L_SERIES
        NRF_RADIO->SHORTS |= RADIO_SHORTS_PHYEND_DISABLE_Msk;
#else
        NRF_RADIO->SHORTS |= RADIO_SHORTS_END_DISABLE_Msk;
#endif
        interrupts |= RADIO_INTENSET_DISABLED_Msk;
    } else {
        interrupts |= RADIO_INTENSET_END_Msk;
    }

    nrf_radio_int_enable(NRF_RADIO, interrupts);

    return 0;
}

static int
ble_phy_cs_tone_configure(struct ble_phy_cs_transmission *transm)
{
#if !BABBLESIM
    NRF_RADIO->CSTONES.NEXTFREQUENCY = 2 + transm->channel;
    NRF_RADIO->CSTONES.PHASESHIFT = 0;
    NRF_RADIO->CSTONES.FFOIN = 1;
    NRF_RADIO->CSTONES.FFOSOURCE = 0;
    NRF_RADIO->CSTONES.NUMSAMPLES = 160;
    NRF_RADIO->CSTONES.DOWNSAMPLE = 0;

    if (transm->tone_mode == BLE_PHY_CS_TONE_MODE_FM) {
        NRF_RADIO->CSTONES.MODE = RADIO_CSTONES_MODE_TFM_Msk;
    } else {
        NRF_RADIO->CSTONES.MODE = RADIO_CSTONES_MODE_TPM_Msk;
    }
#endif

    return 0;
}

static int
ble_phy_cs_transition_to_tx(void)
{
    struct ble_phy_cs_transmission *prev_transm = g_ble_phy_cs.cs_transm;
    struct ble_phy_cs_transmission *next_transm = prev_transm->next;
    uint32_t anchor_time;
    uint32_t radio_time;
    bool is_late;
    uint8_t phy_mode = g_ble_phy_cs.phy_mode;
    uint8_t prev_phy_state = g_ble_phy_cs.phy_state;

    if (prev_transm->mode == BLE_PHY_CS_TRANSM_MODE_TONE) {
        anchor_time = NRF_TIMER0->CC[3];
    } else {
        /* END timestamp is captured in CC[2] */
        anchor_time = NRF_TIMER0->CC[2];

        /* Adjust for delay between EVENT_END and actual TX/RX end time */
        anchor_time += (prev_phy_state == BLE_PHY_STATE_TX)
                       ? g_ble_phy_t_txenddelay[phy_mode]
                       : -g_ble_phy_t_rxenddelay[phy_mode];
    }

    radio_time = anchor_time + prev_transm->end_tifs;

    if (NRF_RADIO->STATE == RADIO_STATE_STATE_Disabled) {
        /* Adjust for TX rump-up */
        radio_time -= BLE_PHY_T_TXENFAST;
        /* Adjust for delay between EVENT_READY and actual TX start time */
        radio_time -= g_ble_phy_t_txdelay[phy_mode];
        phy_ppi_timer0_compare0_to_radio_txen_enable();
        NRF_RADIO->SHORTS |= RADIO_SHORTS_READY_START_Msk;
    } else {
        /* Fast TX_TX transition */
        BLE_LL_ASSERT(NRF_RADIO->STATE == RADIO_STATE_STATE_TxIdle);
        /* TODO: Adjust for delay between TASK_START and actual TX start time */
        radio_time -= g_ble_phy_t_txdelay[phy_mode];
        phy_ppi_timer0_compare0_to_radio_start_enable();
    }

    g_ble_phy_cs.phy_state = BLE_PHY_STATE_TX;
    nrf_timer_cc_set(NRF_TIMER0, 0, radio_time);
    NRF_TIMER0->EVENTS_COMPARE[0] = 0;

    /* Need to check if TIMER0 did not already count past CC[0] and/or CC[2], so
     * we're not stuck waiting for events in case radio and/or PA was not
     * started. If event was triggered we're fine regardless of timer value.
     *
     * Note: CC[3] is used only for wfr which we do not need here.
     */
    nrf_timer_task_trigger(NRF_TIMER0, NRF_TIMER_TASK_CAPTURE3);
    is_late = (NRF_TIMER0->CC[3] > radio_time) && !NRF_TIMER0->EVENTS_COMPARE[0];
    if (is_late) {
        g_ble_phy_cs.phy_transition_late = 1;

        return 1;
    }

    if (next_transm->mode == BLE_PHY_CS_TRANSM_MODE_TONE) {
        ble_phy_cs_tone_end_set(next_transm->duration_usecs, next_transm->end_tifs,
                                BLE_PHY_STATE_TX, phy_mode);
    }

    g_ble_phy_cs.cs_transm = next_transm;

    return 0;
}

static int
ble_phy_cs_transition_to_rx(void)
{
    struct ble_phy_cs_transmission *prev_transm = g_ble_phy_cs.cs_transm;
    struct ble_phy_cs_transmission *next_transm = prev_transm->next;
    uint32_t anchor_time;
    uint32_t radio_time;
    uint32_t start_time;
    uint32_t wfr_time;
    uint8_t phy_mode = g_ble_phy_cs.phy_mode;
    uint8_t prev_phy_state = g_ble_phy_cs.phy_state;

    if (prev_transm->mode == BLE_PHY_CS_TRANSM_MODE_TONE) {
        anchor_time = NRF_TIMER0->CC[3];
    } else {
        /* END timestamp is captured in CC[2] */
        anchor_time = NRF_TIMER0->CC[2];

        /* Adjust for delay between EVENT_END and actual TX/RX end time */
        anchor_time += (prev_phy_state == BLE_PHY_STATE_TX)
                       ? g_ble_phy_t_txenddelay[phy_mode]
                       : -g_ble_phy_t_rxenddelay[phy_mode];
    }

    start_time = anchor_time + prev_transm->end_tifs;
    radio_time = start_time;

    if (NRF_RADIO->STATE == RADIO_STATE_STATE_Disabled) {
        /* Adjust for RX rump-up */
        radio_time -= BLE_PHY_T_RXENFAST;
        /* Start listening a bit earlier due to allowed active clock accuracy */
        radio_time -= 2;
        phy_ppi_timer0_compare0_to_radio_rxen_enable();
        NRF_RADIO->SHORTS |= RADIO_SHORTS_READY_START_Msk;
    } else {
        /* Fast RX_RX transition */
        BLE_LL_ASSERT(NRF_RADIO->STATE == RADIO_STATE_STATE_RxIdle);
        /* Start listening a bit earlier due to allowed active clock accuracy */
        radio_time -= 2;
        phy_ppi_timer0_compare0_to_radio_start_enable();
    }

    g_ble_phy_cs.phy_state = BLE_PHY_STATE_RX;
    nrf_timer_cc_set(NRF_TIMER0, 0, radio_time);
    NRF_TIMER0->EVENTS_COMPARE[0] = 0;

    if (next_transm->mode == BLE_PHY_CS_TRANSM_MODE_TONE) {
        ble_phy_cs_tone_end_set(next_transm->duration_usecs, next_transm->end_tifs,
                                BLE_PHY_STATE_RX, phy_mode);
    } else {
        /* Setup wfr relative to expected radio/PDU start */
        wfr_time = start_time;
        /* Adjust for receiving access address since this triggers EVENT_ADDRESS */
        wfr_time += g_ble_phy_mode_aa_end_off[phy_mode];
        /* Adjust for delay between actual access address RX and EVENT_ADDRESS */
        wfr_time += g_ble_phy_t_rxaddrdelay[phy_mode];
        /* Wait a bit longer due to allowed active clock accuracy */
        wfr_time += 2;
        /*
         * It's possible that we'll capture PDU start time at the end of timer
         * cycle and since wfr expires at the beginning of calculated timer
         * cycle it can be almost 1 usec too early. Let's compensate for this
         * by waiting 1 usec more.
         */
        wfr_time += 1;
        wfr_time += MYNEWT_VAL(BLE_PHY_EXTENDED_TIFS);
        ble_phy_wfr_enable_at(wfr_time);
    }

    g_ble_phy_cs.cs_transm = next_transm;

    return 0;
}

static void
ble_phy_cs_transition_to_none(void)
{
#ifdef NRF54L_SERIES
    nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_STOP);
#endif
    phy_ppi_timer0_compare3_to_radio_disable_disable();
    phy_ppi_timer0_compare3_to_radio_stop_disable();
    phy_ppi_timer0_compare0_to_radio_start_disable();

    nrf_timer_task_trigger(NRF_TIMER0, NRF_TIMER_TASK_STOP);
    phy_ppi_wfr_disable();
    phy_ppi_timer0_compare0_to_radio_txen_disable();
    phy_ppi_rtc0_compare0_to_timer0_start_disable();
    ble_phy_disable();
    phy_ppi_fem_disable();

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
    phy_ppi_debug_disable();
#endif

    NVIC_SetVector(RADIO_IRQn, (uint32_t)g_ble_phy_cs.prev_isr_handler);
}

static int
ble_phy_cs_transition(void)
{
    int rc = 0;
    struct ble_phy_cs_transmission *prev_transm = g_ble_phy_cs.cs_transm;
    struct ble_phy_cs_transmission *next_transm = prev_transm->next;
    uint8_t is_tx;
    uint8_t is_tone;

    /* Disable PPI */
    phy_ppi_wfr_disable();
    phy_ppi_timer0_compare3_to_radio_disable_disable();
    phy_ppi_timer0_compare3_to_radio_stop_disable();
    phy_ppi_timer0_compare0_to_radio_start_disable();
    phy_ppi_timer0_compare0_to_radio_txen_disable();
    phy_ppi_timer0_compare0_to_radio_rxen_disable();

    /* Make sure all interrupts are disabled */
    nrf_radio_int_disable(NRF_RADIO, NRF_RADIO_IRQ_MASK_ALL);

    if (next_transm == NULL) {
        ble_phy_cs_transition_to_none();
        return 0;
    }

    if (prev_transm->is_tx != next_transm->is_tx) {
        nrf_wait_disabled();
    }

    /* Clear events */
#ifdef NRF54L_SERIES
    NRF_RADIO->EVENTS_PHYEND = 0;
#endif
    NRF_RADIO->EVENTS_READY = 0;
    NRF_RADIO->EVENTS_ADDRESS = 0;
    NRF_RADIO->EVENTS_END = 0;
    NRF_RADIO->EVENTS_DISABLED = 0;

    NRF_RADIO->SHORTS = 0;
    g_ble_phy_cs.phy_rx_started = 0;

    ble_phy_cs_channel_configure(next_transm);

    if (next_transm->mode == BLE_PHY_CS_TRANSM_MODE_TONE) {
        rc = ble_phy_cs_tone_configure(next_transm);
    } else {
        rc = ble_phy_cs_sync_configure(next_transm);
    }

    if (rc) {
        ble_phy_cs_transition_to_none();
        return 1;
    }

    if (next_transm->is_tx) {
        rc = ble_phy_cs_transition_to_tx();
    } else {
        rc = ble_phy_cs_transition_to_rx();
    }

    if (rc) {
        ble_phy_cs_transition_to_none();
        return 1;
    }

    return rc;
}

static bool
ble_phy_cs_sync_rx_start_isr(void)
{
    int rc;

    BLE_LL_ASSERT(g_ble_phy_cs.phy_state == BLE_PHY_STATE_RX);

    /* Clear events and clear interrupt */
    NRF_RADIO->EVENTS_ADDRESS = 0;
    nrf_radio_int_disable(NRF_RADIO, RADIO_INTENCLR_ADDRESS_Msk);

    /* Clear wfr timer channels */
    phy_ppi_wfr_disable();
    phy_ppi_timer0_compare3_to_radio_disable_disable();
    phy_ppi_timer0_compare3_to_radio_stop_disable();
    phy_ppi_timer0_compare0_to_radio_start_disable();
    phy_ppi_timer0_compare0_to_radio_rxen_disable();
    g_ble_phy_cs.phy_rx_started = 1;

    return true;
}

static void
ble_phy_cs_sync_tx_end_isr(void)
{
    assert(g_ble_phy_cs.phy_state == BLE_PHY_STATE_TX);

    g_ble_phy_cs.cs_sync_results.cputime = g_ble_phy_cs.phy_start_cputime;
    g_ble_phy_cs.cs_sync_results.rem_us = NRF_TIMER0->CC[2] +
            g_ble_phy_t_txenddelay[g_ble_phy_cs.phy_mode];
    g_ble_phy_cs.cs_sync_results.rem_ns = 0;

    ble_phy_cs_transition();
    ble_ll_cs_sync_tx_end(&g_ble_phy_cs.cs_sync_results);
}

static void
ble_phy_cs_sync_rx_end_isr(void)
{
    assert(g_ble_phy_cs.phy_state == BLE_PHY_STATE_RX);

    g_ble_phy_cs.cs_sync_results.cputime = g_ble_phy_cs.phy_start_cputime;
    g_ble_phy_cs.cs_sync_results.rem_us = NRF_TIMER0->CC[2] -
            g_ble_phy_t_rxenddelay[g_ble_phy_cs.phy_mode];
    g_ble_phy_cs.cs_sync_results.rem_ns = 0;

    ble_phy_cs_transition();
    ble_ll_cs_sync_rx_end(&g_ble_phy_cs.cs_sync_results);
}

static void
ble_phy_cs_tone_tx_end_isr(void)
{
    ble_phy_cs_transition();
    ble_ll_cs_tone_tx_end(&g_ble_phy_cs.cs_tone_results);
}

static void
ble_phy_cs_tone_rx_end_isr(void)
{
    ble_phy_cs_transition();
    ble_ll_cs_tone_rx_end(&g_ble_phy_cs.cs_tone_results);
}

static void
ble_phy_cs_isr(void)
{
    uint32_t irq_en;

    os_trace_isr_enter();

    /* Read irq register to determine which interrupts are enabled */
#ifdef NRF54L_SERIES
    irq_en = NRF_RADIO->INTENSET00;
#else
    irq_en = NRF_RADIO->INTENSET;
#endif

    if ((irq_en & RADIO_INTENCLR_ADDRESS_Msk) && NRF_RADIO->EVENTS_ADDRESS) {
        /* Access Address received */
        if (ble_phy_cs_sync_rx_start_isr()) {
            irq_en &= ~RADIO_INTENCLR_DISABLED_Msk;
        }
    }

    if (((irq_en & RADIO_INTENCLR_DISABLED_Msk) && NRF_RADIO->EVENTS_DISABLED) ||
        ((irq_en & RADIO_INTENCLR_END_Msk) && NRF_RADIO->EVENTS_END)) {
        BLE_LL_ASSERT(NRF_RADIO->EVENTS_END ||
                      ((g_ble_phy_cs.phy_state == BLE_PHY_STATE_RX) &&
                       !g_ble_phy_cs.phy_rx_started));
#ifdef NRF54L_SERIES
        NRF_RADIO->EVENTS_PHYEND = 0;
#endif
        NRF_RADIO->EVENTS_END = 0;
        NRF_RADIO->EVENTS_DISABLED = 0;
        nrf_radio_int_disable(NRF_RADIO, RADIO_INTENCLR_DISABLED_Msk | RADIO_INTENCLR_END_Msk);

        switch (g_ble_phy_cs.phy_state) {
        case BLE_PHY_STATE_RX:
            if (g_ble_phy_cs.phy_rx_started) {
                ble_phy_cs_sync_rx_end_isr();
            } else {
                ble_ll_cs_sync_wfr_timer_exp();
            }
            break;
        case BLE_PHY_STATE_TX:
            ble_phy_cs_sync_tx_end_isr();
            break;
        default:
            BLE_LL_ASSERT(0);
        }
    }

    g_ble_phy_cs.phy_transition_late = 0;

    os_trace_isr_exit();
}

static void
ble_phy_radio_timer_isr(void)
{
    os_trace_isr_enter();

    nrf_timer_int_disable(NRF_TIMER0, TIMER_INTENCLR_COMPARE3_Msk);
    phy_ppi_timer0_compare3_to_radio_disable_disable();
    phy_ppi_timer0_compare3_to_radio_stop_disable();
    phy_ppi_timer0_compare0_to_radio_start_disable();

    switch (g_ble_phy_cs.phy_state) {
        case BLE_PHY_STATE_RX:
            ble_phy_cs_tone_rx_end_isr();
            break;
        case BLE_PHY_STATE_TX:
            ble_phy_cs_tone_tx_end_isr();
            break;
        default:
            BLE_LL_ASSERT(0);
    }

    g_ble_phy_cs.phy_transition_late = 0;

    os_trace_isr_exit();
}

int
ble_phy_cs_subevent_start(struct ble_phy_cs_transmission *transm,
                          uint32_t cputime, uint8_t rem_usecs)
{
    int rc;
    struct ble_phy_cs_transmission *next_transm = transm->next;
    uint8_t *dptr;

    BLE_LL_ASSERT(transm->mode == BLE_PHY_CS_TRANSM_MODE_SYNC);

    g_ble_phy_cs.phy_mode = BLE_PHY_MODE_1M;
    g_ble_phy_cs.cs_transm = transm;

    /* Disable all PPI */
    nrf_wait_disabled();
    phy_ppi_wfr_disable();
    phy_ppi_radio_bcmatch_to_aar_start_disable();
    phy_ppi_radio_address_to_ccm_crypt_disable();
    phy_ppi_timer0_compare3_to_radio_disable_disable();
    phy_ppi_timer0_compare3_to_radio_stop_disable();
    phy_ppi_timer0_compare0_to_radio_start_disable();
    phy_ppi_timer0_compare0_to_radio_txen_disable();
    phy_ppi_timer0_compare0_to_radio_rxen_disable();

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
#endif

    nrf_timer_int_disable(NRF_TIMER0, 0xFFFFFFFF);
    NVIC_SetPriority(TIMER0_IRQn, 0);
    NVIC_SetVector(TIMER0_IRQn, (uint32_t)ble_phy_radio_timer_isr);
    NVIC_EnableIRQ(TIMER0_IRQn);

#if !BABBLESIM
    nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_STOP);
    nrf_timer_task_trigger(NRF_TIMER00, NRF_TIMER_TASK_CLEAR);
    NRF_TIMER00->BITMODE = 3;    /* 32-bit timer */
    NRF_TIMER00->MODE = 0;       /* Timer mode */
    NRF_TIMER00->PRESCALER = 0;  /* gives us 128 MHz */
    phy_ppi_debug_enable();
#endif

    /* Enable radio shortcuts */
    NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk;

    ble_phy_cs_channel_configure(transm);

    rc = ble_phy_cs_sync_configure(transm);
    if (rc) {
        ble_phy_cs_transition_to_none();
        return 1;
    }

    rc = ble_phy_set_start_time(cputime, rem_usecs, false);
    if (rc) {
        ble_phy_cs_transition_to_none();
        return 1;
    }

    if (transm->is_tx) {
        g_ble_phy_cs.phy_state = BLE_PHY_STATE_TX;
        phy_ppi_timer0_compare0_to_radio_txen_enable();
    } else {
        g_ble_phy_cs.phy_state = BLE_PHY_STATE_RX;
        phy_ppi_timer0_compare0_to_radio_rxen_enable();
        ble_phy_wfr_enable(BLE_PHY_WFR_ENABLE_RX, 0, transm->duration_usecs);
    }

    if (rc) {
        ble_phy_cs_transition_to_none();
        return 1;
    }

    return rc;
}
#endif
