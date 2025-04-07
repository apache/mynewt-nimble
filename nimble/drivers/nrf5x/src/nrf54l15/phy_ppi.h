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
#define DPPI_CH_DPPIC00_RADIO_EVENTS_ADDRESS_2    2
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
#define DPPI_CH_TIMER0_EVENTS_COMPARE_1         11
#define DPPI_CH_TIMER0_EVENTS_COMPARE_4         12
#define DPPI_CH_RADIO_EVENTS_CSTONES_END        13
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
#define RADIO_TASKS_PLLEN (*(volatile uint32_t*)((uint8_t*)NRF_RADIO + 0x06C))
#define RADIO_SUBSCRIBE_PLLEN (*(volatile uint32_t*)((uint8_t*)NRF_RADIO + 0x16C))
#define RADIO_EVENTS_PLLREADY (*(volatile uint32_t*)((uint8_t*)NRF_RADIO + 0x2B0))
#define RADIO_PUBLISH_PLLREADY (*(volatile uint32_t*)((uint8_t*)NRF_RADIO + 0x3B0))
#define RADIO_INTENSET01 (*(volatile uint32_t*)((uint8_t*)NRF_RADIO + 0x48C))
#define RADIO_INTENCLR01 (*(volatile uint32_t*)((uint8_t*)NRF_RADIO + 0x494))
#define RADIO_FREQFINETUNE (*(volatile uint32_t*)((uint8_t*)NRF_RADIO + 0x804))
#define RADIO_INTENSET01_CSTONESEND_Msk (1 << 18UL)
#define RADIO_INTENCLR01_CSTONESEND_Msk (1 << 18UL)

/* Create PPIB links between RADIO and PERI power domain. */
#define PPIB_RADIO_PERI(_ch, _src, _dst)                  \
    NRF_PPIB11->SUBSCRIBE_SEND[_ch] = DPPI_CH_SUB(_src);  \
    NRF_PPIB21->PUBLISH_RECEIVE[_ch] = DPPI_CH_PUB(_dst); \
    NRF_DPPIC10->CHENSET |= 1 << DPPI_CH_ ## _src;        \
    NRF_DPPIC20->CHENSET |= 1 << DPPI_CH_ ## _dst;

/* Create PPIB link from RADIO to MCU power domain. */
#define PPIB_RADIO_MCU(_ch, _src, _dst)                   \
    NRF_PPIB10->SUBSCRIBE_SEND[_ch] = DPPI_CH_SUB(_src);  \
    NRF_PPIB00->PUBLISH_RECEIVE[_ch] = DPPI_CH_PUB(_dst); \
    NRF_DPPIC10->CHENSET |= 1 << DPPI_CH_ ## _src;        \
    NRF_DPPIC00->CHENSET |= 1 << DPPI_CH_ ## _dst;

#define PPIB_RADIO_MCU_0(_src, _dst) PPIB_RADIO_MCU(0, _src, _dst)
#if MYNEWT_VAL(BLE_CHANNEL_SOUNDING)
#define PPIB_RADIO_MCU_1(_src, _dst) PPIB_RADIO_MCU(1, _src, _dst)
#define PPIB_RADIO_MCU_2(_src, _dst) PPIB_RADIO_MCU(2, _src, _dst)
#define PPIB_RADIO_MCU_3(_src, _dst) PPIB_RADIO_MCU(3, _src, _dst)

/* Create PPIB link from MCU to RADIO power domain. */
#define PPIB_MCU_RADIO(_ch, _src, _dst)                   \
    NRF_PPIB00->SUBSCRIBE_SEND[_ch] = DPPI_CH_SUB(_src);  \
    NRF_PPIB10->PUBLISH_RECEIVE[_ch] = DPPI_CH_PUB(_dst); \
    NRF_DPPIC00->CHENSET |= 1 << DPPI_CH_ ## _src;        \
    NRF_DPPIC10->CHENSET |= 1 << DPPI_CH_ ## _dst;

#define PPIB_MCU_RADIO_0(_src, _dst) PPIB_MCU_RADIO(0, _src, _dst)
#define PPIB_MCU_RADIO_1(_src, _dst) PPIB_MCU_RADIO(1, _src, _dst)
#define PPIB_MCU_RADIO_2(_src, _dst) PPIB_MCU_RADIO(2, _src, _dst)
#define PPIB_MCU_RADIO_3(_src, _dst) PPIB_MCU_RADIO(3, _src, _dst)

/* Create PPIB link from MCU to PERI power domain. */
#define PPIB_MCU_PERI(_ch, _src, _dst)                    \
    NRF_PPIB01->SUBSCRIBE_SEND[_ch] = DPPI_CH_SUB(_src);  \
    NRF_PPIB20->PUBLISH_RECEIVE[_ch] = DPPI_CH_PUB(_dst); \
    NRF_DPPIC00->CHENSET |= 1 << DPPI_CH_ ## _src;        \
    NRF_DPPIC20->CHENSET |= 1 << DPPI_CH_ ## _dst;
#endif

#define PPIB_RADIO_PERI_0(_src, _dst) PPIB_RADIO_PERI(0, _src, _dst)
#define PPIB_RADIO_PERI_1(_src, _dst) PPIB_RADIO_PERI(1, _src, _dst)
#define PPIB_RADIO_PERI_2(_src, _dst) PPIB_RADIO_PERI(2, _src, _dst)
#define PPIB_RADIO_PERI_3(_src, _dst) PPIB_RADIO_PERI(3, _src, _dst)
#define PPIB_RADIO_PERI_4(_src, _dst) PPIB_RADIO_PERI(4, _src, _dst)
#define PPIB_RADIO_PERI_5(_src, _dst) PPIB_RADIO_PERI(5, _src, _dst)
#define PPIB_RADIO_PERI_6(_src, _dst) PPIB_RADIO_PERI(6, _src, _dst)
#define PPIB_RADIO_PERI_7(_src, _dst) PPIB_RADIO_PERI(7, _src, _dst)
#define PPIB_RADIO_PERI_8(_src, _dst) PPIB_RADIO_PERI(8, _src, _dst)

#define PPIB_MCU_PERI_0(_src, _dst) PPIB_MCU_PERI(0, _src, _dst)
#define PPIB_MCU_PERI_1(_src, _dst) PPIB_MCU_PERI(1, _src, _dst)
#define PPIB_MCU_PERI_2(_src, _dst) PPIB_MCU_PERI(2, _src, _dst)
#define PPIB_MCU_PERI_3(_src, _dst) PPIB_MCU_PERI(3, _src, _dst)

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

#if MYNEWT_VAL(BLE_CHANNEL_SOUNDING)
static inline void
phy_ppi_timer0_compare1_to_radio_rxen_enable(void)
{
    NRF_RADIO->SUBSCRIBE_RXEN = DPPI_CH_SUB(TIMER0_EVENTS_COMPARE_1);
}

static inline void
phy_ppi_timer0_compare1_to_radio_rxen_disable(void)
{
    NRF_RADIO->SUBSCRIBE_RXEN = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_1);
}

static inline void
phy_ppi_timer0_compare2_to_radio_start_enable(void)
{
    NRF_RADIO->SUBSCRIBE_START = DPPI_CH_SUB(TIMER0_EVENTS_COMPARE_2);
}

static inline void
phy_ppi_timer0_compare2_to_radio_start_disable(void)
{
    NRF_RADIO->SUBSCRIBE_START = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_2);
}

static inline void
phy_ppi_timer0_compare3_to_radio_disable_enable(void)
{
    NRF_RADIO->SUBSCRIBE_DISABLE = DPPI_CH_SUB(TIMER0_EVENTS_COMPARE_3);
}

static inline void
phy_ppi_timer0_compare3_to_radio_disable_disable(void)
{
    NRF_RADIO->SUBSCRIBE_DISABLE = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_3);
}

static inline void
phy_ppi_timer0_compare4_to_radio_cstonesstart_enable(void)
{
    RADIO_SUBSCRIBE_CSTONESSTART = DPPI_CH_SUB(TIMER0_EVENTS_COMPARE_4);
}

static inline void
phy_ppi_timer0_compare4_to_radio_cstonesstart_disable(void)
{
    RADIO_SUBSCRIBE_CSTONESSTART = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_4);
}

static inline void
phy_ppi_timer00_radio_address_to_capture_enable(void)
{
    /* Capture ADDRESS event time of CS_SYNC TX and RX */
    NRF_DPPIC00->CHG[0] = (1 << DPPI_CH_DPPIC00_RADIO_EVENTS_ADDRESS);

    NRF_PPIB10->SUBSCRIBE_SEND[1] = DPPI_CH_SUB(RADIO_EVENTS_ADDRESS);
    NRF_PPIB10->SUBSCRIBE_SEND[2] = DPPI_CH_SUB(RADIO_EVENTS_ADDRESS);
    NRF_PPIB00->PUBLISH_RECEIVE[1] = DPPI_CH_PUB(DPPIC00_RADIO_EVENTS_ADDRESS);
    NRF_PPIB00->PUBLISH_RECEIVE[2] = DPPI_CH_PUB(DPPIC00_RADIO_EVENTS_ADDRESS_2);

    NRF_DPPIC10->CHENSET = 1 << DPPI_CH_RADIO_EVENTS_ADDRESS;
    NRF_DPPIC00->CHENSET = (1 << DPPI_CH_DPPIC00_RADIO_EVENTS_ADDRESS) |
                           (1 << DPPI_CH_DPPIC00_RADIO_EVENTS_ADDRESS_2);

    NRF_TIMER00->SUBSCRIBE_CAPTURE[0] = DPPI_CH_SUB(DPPIC00_RADIO_EVENTS_ADDRESS);
    NRF_TIMER00->SUBSCRIBE_CAPTURE[1] = DPPI_CH_SUB(DPPIC00_RADIO_EVENTS_ADDRESS_2);

    NRF_DPPIC00->SUBSCRIBE_CHG[0].DIS = (1 << DPPI_CH_DPPIC00_RADIO_EVENTS_ADDRESS) | DPPIC_SUBSCRIBE_CHG_DIS_EN_Msk;
    NRF_DPPIC00->TASKS_CHG[0].EN = 1;
}

static inline void
phy_ppi_timer00_radio_address_to_capture_disable(void)
{
    NRF_TIMER00->SUBSCRIBE_CAPTURE[0] = DPPI_CH_UNSUB(DPPIC00_RADIO_EVENTS_ADDRESS);
    NRF_TIMER00->SUBSCRIBE_CAPTURE[1] = DPPI_CH_UNSUB(DPPIC00_RADIO_EVENTS_ADDRESS);
    NRF_PPIB10->SUBSCRIBE_SEND[1] = DPPI_CH_UNSUB(RADIO_EVENTS_ADDRESS);
    NRF_PPIB10->SUBSCRIBE_SEND[2] = DPPI_CH_UNSUB(RADIO_EVENTS_ADDRESS);
    NRF_PPIB00->PUBLISH_RECEIVE[1] = DPPI_CH_UNSUB(DPPIC00_RADIO_EVENTS_ADDRESS);
    NRF_PPIB00->PUBLISH_RECEIVE[2] = DPPI_CH_UNSUB(DPPIC00_RADIO_EVENTS_ADDRESS_2);
    NRF_DPPIC00->CHG[0] = 0;
    NRF_DPPIC00->CHENCLR = (1 << DPPI_CH_DPPIC00_RADIO_EVENTS_ADDRESS) |
                           (1 << DPPI_CH_DPPIC00_RADIO_EVENTS_ADDRESS_2);
}

static inline void
phy_ppi_cs_mode_enable(void)
{
    NRF_TIMER0->SUBSCRIBE_CAPTURE[1] = DPPI_CH_UNSUB(RADIO_EVENTS_ADDRESS);
    NRF_TIMER0->SUBSCRIBE_CAPTURE[2] = DPPI_CH_UNSUB(RADIO_EVENTS_END);
    NRF_TIMER0->SUBSCRIBE_CAPTURE[6] = DPPI_CH_SUB(RADIO_EVENTS_CSTONES_END);

    /* CC[0] and CC[3] already published */
    NRF_TIMER0->PUBLISH_COMPARE[1] = DPPI_CH_PUB(TIMER0_EVENTS_COMPARE_1);
    NRF_TIMER0->PUBLISH_COMPARE[2] = DPPI_CH_PUB(TIMER0_EVENTS_COMPARE_2);
    NRF_TIMER0->PUBLISH_COMPARE[6] = DPPI_CH_PUB(TIMER0_EVENTS_COMPARE_2);
    NRF_TIMER0->PUBLISH_COMPARE[4] = DPPI_CH_PUB(TIMER0_EVENTS_COMPARE_4);
}

static inline void
phy_ppi_cs_mode_disable(void)
{
    NRF_TIMER0->PUBLISH_COMPARE[1] = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_1);
    NRF_TIMER0->PUBLISH_COMPARE[2] = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_2);
    NRF_TIMER0->PUBLISH_COMPARE[6] = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_2);
    NRF_TIMER0->PUBLISH_COMPARE[4] = DPPI_CH_UNSUB(TIMER0_EVENTS_COMPARE_4);

    NRF_TIMER0->SUBSCRIBE_CAPTURE[6] = DPPI_CH_UNSUB(RADIO_EVENTS_CSTONES_END);
    NRF_TIMER0->SUBSCRIBE_CAPTURE[1] = DPPI_CH_SUB(RADIO_EVENTS_ADDRESS);
    NRF_TIMER0->SUBSCRIBE_CAPTURE[2] = DPPI_CH_SUB(RADIO_EVENTS_END);
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
