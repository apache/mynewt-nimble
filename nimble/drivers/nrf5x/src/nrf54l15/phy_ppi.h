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

#ifndef H_PHY_PPI_
#define H_PHY_PPI_

#define DPPI_CH_PUB(_ch)        (((DPPI_CH_ ## _ch) & 0xff) | (1 << 31))
#define DPPI_CH_SUB(_ch)        (((DPPI_CH_ ## _ch) & 0xff) | (1 << 31))
#define DPPI_CH_UNSUB(_ch)      (((DPPI_CH_ ## _ch) & 0xff) | (0 << 31))
#define DPPI_CH_MASK(_ch)       (1 << (DPPI_CH_ ## _ch))

/* DPPIC00 [0:7] */
#define DPPI_CH_DPPIC00_RADIO_EVENTS_PAYLOAD_CCM  0
#if MYNEWT_VAL(BLE_CHANNEL_SOUNDING)
#define DPPI_CH_DPPIC00_RADIO_EVENTS_ADDRESS      1
#define DPPI_CH_DPPIC00_RTC0_EVENTS_COMPARE_0     2
#define DPPI_CH_DPPIC00_TIMER00_EVENTS_COMPARE_0  3
#define DPPI_CH_DPPIC00_TIMER00_EVENTS_COMPARE_2  4
#define DPPI_CH_DPPIC00_TIMER00_EVENTS_COMPARE_3  5
#define DPPI_CH_DPPIC00_TIMER00_EVENTS_COMPARE_4  6
#define DPPI_CH_DPPIC00_RADIO_EVENTS_CSTONES_END  7
#endif

/* DPPIC10 [0:23] */
#define DPPI_CH_TIMER0_EVENTS_COMPARE_0         0
#define DPPI_CH_TIMER0_EVENTS_COMPARE_3         1
#define DPPI_CH_RADIO_EVENTS_END                2
#define DPPI_CH_RADIO_EVENTS_BCMATCH            3
#define DPPI_CH_RADIO_EVENTS_ADDRESS            4
#define DPPI_CH_RTC0_EVENTS_COMPARE_0           5
#define DPPI_CH_TIMER0_EVENTS_COMPARE_2         6
#define DPPI_CH_RADIO_EVENTS_DISABLED           7
#define DPPI_CH_RADIO_EVENTS_READY              8
#define DPPI_CH_RADIO_EVENTS_RXREADY            9
#define DPPI_CH_RADIO_EVENTS_PAYLOAD_RADIO      10
#if MYNEWT_VAL(BLE_CHANNEL_SOUNDING)
#define DPPI_CH_RADIO_EVENTS_CSTONES_END        11
#define DPPI_CH_TIMER00_EVENTS_COMPARE_0        12
#define DPPI_CH_TIMER00_EVENTS_COMPARE_2        13
#define DPPI_CH_TIMER00_EVENTS_COMPARE_3        14
#define DPPI_CH_TIMER00_EVENTS_COMPARE_4        15
#endif

/* DPPIC20 [0:15] */
#define DPPI_CH_GPIOTE20_TASKS_SET_0            0
#define DPPI_CH_GPIOTE20_TASKS_CLR_0            1
#define DPPI_CH_GPIOTE20_TASKS_SET_1            2
#define DPPI_CH_GPIOTE20_TASKS_CLR_1            3
#define DPPI_CH_GPIOTE20_TASKS_SET_2            4
#define DPPI_CH_GPIOTE20_TASKS_CLR_2            5

#define DPPI_CH_ENABLE_ALL  (DPPIC_CHEN_CH0_Msk | DPPIC_CHEN_CH1_Msk | \
                             DPPIC_CHEN_CH2_Msk | DPPIC_CHEN_CH3_Msk | \
                             DPPIC_CHEN_CH4_Msk | DPPIC_CHEN_CH5_Msk | \
                             DPPIC_CHEN_CH10_Msk)

#define DPPI_CH_MASK_FEM    (DPPI_CH_MASK(TIMER0_EVENTS_COMPARE_2) | \
                             DPPI_CH_MASK(RADIO_EVENTS_DISABLED))

/* nrfx not updated yet */
#define RADIO_TASKS_CSTONESSTART (*(volatile uint32_t*)((uint8_t*)NRF_RADIO + 0x0A0))
#define RADIO_SUBSCRIBE_CSTONESSTART (*(volatile uint32_t*)((uint8_t*)NRF_RADIO + 0x1A0))
#define RADIO_EVENTS_CSTONESEND (*(volatile uint32_t*)((uint8_t*)NRF_RADIO + 0x2C8))
#define RADIO_PUBLISH_CSTONESEND (*(volatile uint32_t*)((uint8_t*)NRF_RADIO + 0x3C8))
#define RADIO_INTENSET01 (*(volatile uint32_t*)((uint8_t*)NRF_RADIO + 0x48C))
#define RADIO_INTENCLR01 (*(volatile uint32_t*)((uint8_t*)NRF_RADIO + 0x494))
#define RADIO_INTENSET01_CSTONESSEND_Msk (0b1000)

static inline void
phy_ppi_rtc0_compare0_to_timer0_start_enable(void)
{
    NRF_TIMER0->SUBSCRIBE_START = DPPI_CH_SUB(RTC0_EVENTS_COMPARE_0);
}

static inline void
phy_ppi_rtc0_compare0_to_timer0_start_disable(void)
{
    NRF_TIMER0->SUBSCRIBE_START = DPPI_CH_UNSUB(RTC0_EVENTS_COMPARE_0);
    NRF_TIMER0->SUBSCRIBE_CAPTURE[3] = DPPI_CH_UNSUB(RADIO_EVENTS_ADDRESS);
    NRF_RADIO->SUBSCRIBE_DISABLE = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_3);
}

static inline void
phy_ppi_timer0_compare0_to_radio_txen_enable(void)
{
    NRF_RADIO->SUBSCRIBE_TXEN = DPPI_CH_SUB(TIMER0_EVENTS_COMPARE_0);
}

static inline void
phy_ppi_timer0_compare0_to_radio_txen_disable(void)
{
    NRF_RADIO->SUBSCRIBE_TXEN = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_0);
}

static inline void
phy_ppi_timer0_compare0_to_radio_rxen_enable(void)
{
    NRF_RADIO->SUBSCRIBE_RXEN = DPPI_CH_SUB(TIMER0_EVENTS_COMPARE_0);
}

static inline void
phy_ppi_timer0_compare0_to_radio_rxen_disable(void)
{
    NRF_RADIO->SUBSCRIBE_RXEN = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_0);
}

static inline void
phy_ppi_radio_address_to_ccm_crypt_enable(void)
{
    NRF_CCM->SUBSCRIBE_START = DPPI_CH_SUB(DPPIC00_RADIO_EVENTS_PAYLOAD_CCM);
}

static inline void
phy_ppi_radio_address_to_ccm_crypt_disable(void)
{
    NRF_CCM->SUBSCRIBE_START = DPPI_CH_UNSUB(DPPIC00_RADIO_EVENTS_PAYLOAD_CCM);
}

static inline void
phy_ppi_radio_bcmatch_to_aar_start_enable(void)
{
    NRF_AAR->SUBSCRIBE_START = DPPI_CH_SUB(RADIO_EVENTS_BCMATCH);
}

static inline void
phy_ppi_radio_bcmatch_to_aar_start_disable(void)
{
    NRF_AAR->SUBSCRIBE_START = DPPI_CH_UNSUB(RADIO_EVENTS_BCMATCH);
}

static inline void
phy_ppi_wfr_enable(void)
{
    NRF_TIMER0->SUBSCRIBE_CAPTURE[3] = DPPI_CH_SUB(RADIO_EVENTS_ADDRESS);
    NRF_RADIO->SUBSCRIBE_DISABLE = DPPI_CH_SUB(TIMER0_EVENTS_COMPARE_3);
}

static inline void
phy_ppi_wfr_disable(void)
{
    NRF_TIMER0->SUBSCRIBE_CAPTURE[3] = DPPI_CH_UNSUB(RADIO_EVENTS_ADDRESS);
    NRF_RADIO->SUBSCRIBE_DISABLE = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_3);
}

static inline void
phy_ppi_cs_wfr_enable(void)
{
    NRF_TIMER00->SUBSCRIBE_CAPTURE[3] = DPPI_CH_SUB(DPPIC00_RADIO_EVENTS_ADDRESS);
    NRF_RADIO->SUBSCRIBE_DISABLE = DPPI_CH_SUB(TIMER00_EVENTS_COMPARE_3);
}

static inline void
phy_ppi_cs_wfr_disable(void)
{
    NRF_TIMER00->SUBSCRIBE_CAPTURE[3] = DPPI_CH_UNSUB(DPPIC00_RADIO_EVENTS_ADDRESS);
    NRF_RADIO->SUBSCRIBE_DISABLE = DPPI_CH_UNSUB(TIMER00_EVENTS_COMPARE_3);
}

#if MYNEWT_VAL(BLE_CHANNEL_SOUNDING)
static inline void
phy_ppi_rtc0_compare0_to_timer00_start_enable(void)
{
    NRF_TIMER00->SUBSCRIBE_START = DPPI_CH_SUB(DPPIC00_RTC0_EVENTS_COMPARE_0);
}

static inline void
phy_ppi_rtc0_compare0_to_timer00_start_disable(void)
{
    NRF_TIMER00->SUBSCRIBE_START = DPPI_CH_UNSUB(DPPIC00_RTC0_EVENTS_COMPARE_0);
}

static inline void
phy_ppi_timer00_compare0_to_radio_txen_enable(void)
{
    NRF_RADIO->SUBSCRIBE_TXEN = DPPI_CH_SUB(TIMER00_EVENTS_COMPARE_0);
}

static inline void
phy_ppi_timer00_compare0_to_radio_txen_disable(void)
{
    NRF_RADIO->SUBSCRIBE_TXEN = DPPI_CH_UNSUB(TIMER00_EVENTS_COMPARE_0);
}

static inline void
phy_ppi_timer00_compare0_to_radio_rxen_enable(void)
{
    NRF_RADIO->SUBSCRIBE_RXEN = DPPI_CH_SUB(TIMER00_EVENTS_COMPARE_0);
}

static inline void
phy_ppi_timer00_compare0_to_radio_rxen_disable(void)
{
    NRF_RADIO->SUBSCRIBE_RXEN = DPPI_CH_UNSUB(TIMER00_EVENTS_COMPARE_0);
}

static inline void
phy_ppi_timer00_compare2_to_radio_start_enable(void)
{
    NRF_RADIO->SUBSCRIBE_START = DPPI_CH_SUB(TIMER00_EVENTS_COMPARE_2);
}

static inline void
phy_ppi_timer00_compare2_to_radio_start_disable(void)
{
    NRF_RADIO->SUBSCRIBE_START = DPPI_CH_UNSUB(TIMER00_EVENTS_COMPARE_2);
}

static inline void
phy_ppi_timer00_compare3_to_radio_disable_enable(void)
{
    NRF_RADIO->SUBSCRIBE_DISABLE = DPPI_CH_SUB(TIMER00_EVENTS_COMPARE_3);
}

static inline void
phy_ppi_timer00_compare3_to_radio_disable_disable(void)
{
    NRF_RADIO->SUBSCRIBE_DISABLE = DPPI_CH_UNSUB(TIMER00_EVENTS_COMPARE_3);
}

static inline void
phy_ppi_timer00_compare4_to_radio_cstonesstart_enable(void)
{
    RADIO_SUBSCRIBE_CSTONESSTART = DPPI_CH_SUB(TIMER00_EVENTS_COMPARE_4);
}

static inline void
phy_ppi_timer00_compare4_to_radio_cstonesstart_disable(void)
{
    RADIO_SUBSCRIBE_CSTONESSTART = DPPI_CH_UNSUB(TIMER00_EVENTS_COMPARE_4);
}

static inline void
phy_ppi_cs_mode_enable(void)
{
    /* We use the fastest timer in CS mode, so disable these */
    NRF_TIMER0->SUBSCRIBE_CAPTURE[1] = DPPI_CH_UNSUB(RADIO_EVENTS_ADDRESS);
    NRF_TIMER0->SUBSCRIBE_CAPTURE[2] = DPPI_CH_UNSUB(RADIO_EVENTS_END);
    /* CC[1] for capturing ADDRESS event time */
    NRF_TIMER00->SUBSCRIBE_CAPTURE[1] = DPPI_CH_SUB(DPPIC00_RADIO_EVENTS_ADDRESS);
    /* CC[5] for capturing CSTONESEND event time */
    NRF_TIMER00->SUBSCRIBE_CAPTURE[5] = DPPI_CH_SUB(DPPIC00_RADIO_EVENTS_CSTONES_END);
}

static inline void
phy_ppi_cs_mode_disable(void)
{
    NRF_TIMER0->SUBSCRIBE_CAPTURE[1] = DPPI_CH_SUB(RADIO_EVENTS_ADDRESS);
    NRF_TIMER0->SUBSCRIBE_CAPTURE[2] = DPPI_CH_SUB(RADIO_EVENTS_END);
    /* CC[1] for capturing ADDRESS event time */
    NRF_TIMER00->SUBSCRIBE_CAPTURE[1] = DPPI_CH_UNSUB(DPPIC00_RADIO_EVENTS_ADDRESS);
    /* CC[5] for capturing CSTONESEND event time */
    NRF_TIMER00->SUBSCRIBE_CAPTURE[5] = DPPI_CH_UNSUB(DPPIC00_RADIO_EVENTS_CSTONES_END);
}

#endif

static inline void
phy_ppi_debug_enable(void)
{
    NRF_GPIOTE20->SUBSCRIBE_SET[PHY_GPIOTE_DEBUG_1] = DPPI_CH_SUB(GPIOTE20_TASKS_SET_0);
    NRF_GPIOTE20->SUBSCRIBE_CLR[PHY_GPIOTE_DEBUG_1] = DPPI_CH_SUB(GPIOTE20_TASKS_CLR_0);
    NRF_GPIOTE20->SUBSCRIBE_SET[PHY_GPIOTE_DEBUG_2] = DPPI_CH_SUB(GPIOTE20_TASKS_SET_1);
    NRF_GPIOTE20->SUBSCRIBE_CLR[PHY_GPIOTE_DEBUG_2] = DPPI_CH_SUB(GPIOTE20_TASKS_CLR_1);
    NRF_GPIOTE20->SUBSCRIBE_SET[PHY_GPIOTE_DEBUG_3] = DPPI_CH_SUB(GPIOTE20_TASKS_SET_2);
    NRF_GPIOTE20->SUBSCRIBE_CLR[PHY_GPIOTE_DEBUG_3] = DPPI_CH_SUB(GPIOTE20_TASKS_CLR_2);
}

static inline void
phy_ppi_debug_disable(void)
{
    NRF_GPIOTE20->SUBSCRIBE_SET[PHY_GPIOTE_DEBUG_1] = DPPI_CH_UNSUB(GPIOTE20_TASKS_SET_0);
    NRF_GPIOTE20->SUBSCRIBE_CLR[PHY_GPIOTE_DEBUG_1] = DPPI_CH_UNSUB(GPIOTE20_TASKS_CLR_0);
    NRF_GPIOTE20->SUBSCRIBE_SET[PHY_GPIOTE_DEBUG_2] = DPPI_CH_UNSUB(GPIOTE20_TASKS_SET_1);
    NRF_GPIOTE20->SUBSCRIBE_CLR[PHY_GPIOTE_DEBUG_2] = DPPI_CH_UNSUB(GPIOTE20_TASKS_CLR_1);
    NRF_GPIOTE20->SUBSCRIBE_SET[PHY_GPIOTE_DEBUG_3] = DPPI_CH_UNSUB(GPIOTE20_TASKS_SET_2);
    NRF_GPIOTE20->SUBSCRIBE_CLR[PHY_GPIOTE_DEBUG_3] = DPPI_CH_UNSUB(GPIOTE20_TASKS_CLR_2);
}

static inline void
phy_ppi_fem_disable(void)
{
#if PHY_USE_FEM_SINGLE_GPIO
    NRF_GPIOTE->SUBSCRIBE_SET[PHY_GPIOTE_FEM] =
        DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_3);
#else
#if PHY_USE_FEM_PA
    NRF_GPIOTE->SUBSCRIBE_SET[PHY_GPIOTE_FEM_PA] =
        DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_2);
#endif
#if PHY_USE_FEM_LNA
    NRF_GPIOTE->SUBSCRIBE_SET[PHY_GPIOTE_FEM_LNA] =
        DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_2);
#endif
#endif
}

static inline void
phy_ppi_disable(void)
{
#if MYNEWT_VAL(BLE_CHANNEL_SOUNDING)
    NRF_TIMER00->SUBSCRIBE_START = DPPI_CH_UNSUB(DPPIC00_RTC0_EVENTS_COMPARE_0);
    NRF_TIMER00->SUBSCRIBE_CAPTURE[3] = DPPI_CH_UNSUB(DPPIC00_RADIO_EVENTS_ADDRESS);
#endif
    NRF_TIMER0->SUBSCRIBE_START = DPPI_CH_UNSUB(RTC0_EVENTS_COMPARE_0);
    NRF_TIMER0->SUBSCRIBE_CAPTURE[3] = DPPI_CH_UNSUB(RADIO_EVENTS_ADDRESS);
    NRF_RADIO->SUBSCRIBE_DISABLE = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_3);
    NRF_RADIO->SUBSCRIBE_TXEN = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_0);
    NRF_RADIO->SUBSCRIBE_RXEN = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_0);
    NRF_RADIO->SUBSCRIBE_START = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_0);
    NRF_AAR->SUBSCRIBE_START = DPPI_CH_UNSUB(RADIO_EVENTS_BCMATCH);
    NRF_CCM->SUBSCRIBE_START = DPPI_CH_UNSUB(DPPIC00_RADIO_EVENTS_PAYLOAD_CCM);

    phy_ppi_fem_disable();
}

#endif /* H_PHY_PPI_ */
