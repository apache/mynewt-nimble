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

#ifndef H_PHY_PRIV_
#define H_PHY_PRIV_

#include <stdint.h>
#include <nrf_gpio.h>
#include <nrf_gpiote.h>


/* To disable all radio interrupts */
#ifdef NRF54L_SERIES
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
#else
#define NRF_RADIO_IRQ_MASK_ALL  (0x34FF)
#endif

/*
 * We configure the nrf with a 1 byte S0 field, 8 bit length field, and
 * zero bit S1 field. The preamble is 8 bits long.
 */
#define NRF_LFLEN_BITS          (8)
#define NRF_S0LEN               (1)
#define NRF_S1LEN_BITS          (0)
#define NRF_CILEN_BITS          (2)
#define NRF_TERMLEN_BITS        (3)

/* Maximum length of frames */
#define NRF_MAXLEN              (255)
#define NRF_BALEN               (3)     /* For base address of 3 bytes */

/* NRF_RADIO->PCNF0 configuration values */
#define NRF_PCNF0               (NRF_LFLEN_BITS << RADIO_PCNF0_LFLEN_Pos) | \
                                (RADIO_PCNF0_S1INCL_Include << RADIO_PCNF0_S1INCL_Pos) | \
                                (NRF_S0LEN << RADIO_PCNF0_S0LEN_Pos) | \
                                (NRF_S1LEN_BITS << RADIO_PCNF0_S1LEN_Pos)
#define NRF_PCNF0_1M            (NRF_PCNF0) | \
                                (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos)
#define NRF_PCNF0_2M            (NRF_PCNF0) | \
                                (RADIO_PCNF0_PLEN_16bit << RADIO_PCNF0_PLEN_Pos)
#define NRF_PCNF0_CODED         (NRF_PCNF0) | \
                                (RADIO_PCNF0_PLEN_LongRange << RADIO_PCNF0_PLEN_Pos) | \
                                (NRF_CILEN_BITS << RADIO_PCNF0_CILEN_Pos) | \
                                (NRF_TERMLEN_BITS << RADIO_PCNF0_TERMLEN_Pos)

#define PHY_TRANS_NONE  (0)
#define PHY_TRANS_TO_TX (1)
#define PHY_TRANS_TO_RX (2)

#define PHY_TRANS_ANCHOR_START   (0)
#define PHY_TRANS_ANCHOR_END     (1)

#if defined(NRF52840_XXAA) && MYNEWT_VAL(BLE_PHY_NRF52_HEADERMASK_WORKAROUND)
#define PHY_USE_HEADERMASK_WORKAROUND 1
#endif

#define PHY_USE_DEBUG_1     (MYNEWT_VAL(BLE_PHY_DBG_TIME_TXRXEN_READY_PIN) >= 0)
#define PHY_USE_DEBUG_2     (MYNEWT_VAL(BLE_PHY_DBG_TIME_ADDRESS_END_PIN) >= 0)
#define PHY_USE_DEBUG_3     (MYNEWT_VAL(BLE_PHY_DBG_TIME_WFR_PIN) >= 0)
#define PHY_USE_DEBUG       (PHY_USE_DEBUG_1 || PHY_USE_DEBUG_2 || PHY_USE_DEBUG_3)

#define PHY_USE_FEM_PA      (MYNEWT_VAL(BLE_FEM_PA) != 0)
#define PHY_USE_FEM_LNA     (MYNEWT_VAL(BLE_FEM_LNA) != 0)
#define PHY_USE_FEM         (PHY_USE_FEM_PA || PHY_USE_FEM_LNA)
#define PHY_USE_FEM_SINGLE_GPIO \
    (PHY_USE_FEM && (!PHY_USE_FEM_PA || !PHY_USE_FEM_LNA || \
                     (MYNEWT_VAL(BLE_FEM_PA_GPIO) == \
                      MYNEWT_VAL(BLE_FEM_LNA_GPIO))))

/* GPIOTE indexes, start assigning from last one */
#define PHY_GPIOTE_DEBUG_1  (8 - PHY_USE_DEBUG_1)
#define PHY_GPIOTE_DEBUG_2  (PHY_GPIOTE_DEBUG_1 - PHY_USE_DEBUG_2)
#define PHY_GPIOTE_DEBUG_3  (PHY_GPIOTE_DEBUG_2 - PHY_USE_DEBUG_3)
#if PHY_USE_FEM_SINGLE_GPIO
#define PHY_GPIOTE_FEM      (PHY_GPIOTE_DEBUG_3 - PHY_USE_FEM)
#else
#define PHY_GPIOTE_FEM_PA   (PHY_GPIOTE_DEBUG_3 - PHY_USE_FEM_PA)
#define PHY_GPIOTE_FEM_LNA  (PHY_GPIOTE_FEM_PA - PHY_USE_FEM_LNA)
#endif

#ifndef NRF54L_SERIES
static inline void
phy_gpiote_configure(int idx, int pin)
{
    nrf_gpio_cfg_output(pin);
    nrf_gpiote_task_configure(NRF_GPIOTE, idx, pin, NRF_GPIOTE_POLARITY_NONE,
                              NRF_GPIOTE_INITIAL_VALUE_LOW);
    nrf_gpiote_task_enable(NRF_GPIOTE, idx);
}
#endif

#if PHY_USE_DEBUG
void phy_debug_init(void);
#endif

#if PHY_USE_FEM
void phy_fem_init(void);
#if PHY_USE_FEM_PA
void phy_fem_enable_pa(void);
#endif
#if PHY_USE_FEM_LNA
void phy_fem_enable_lna(void);
#endif
void phy_fem_disable(void);
#endif

void phy_ppi_init(void);

#ifdef NRF52_SERIES
#include "nrf52/phy_ppi.h"
#endif

void phy_txpower_set(int8_t dbm);
int8_t phy_txpower_round(int8_t dbm);

#ifdef NRF52_SERIES
#include "nrf52/phy_ppi.h"
#endif
#ifdef NRF53_SERIES
#include "nrf53/phy_ppi.h"
#endif
#ifdef NRF54L_SERIES
#define NRF_TIMER0 NRF_TIMER10
#define TIMER0_IRQn TIMER10_IRQn
#define NRF_DPPIC NRF_DPPIC10
#define NRF_RTC0 NRF_RTC10
#define NRF_AAR NRF_AAR00
#define NRF_CCM NRF_CCM00
#define NRF_AAR NRF_AAR00
#define NRF_GPIOTE NRF_GPIOTE20
#define RADIO_IRQn RADIO_0_IRQn
#define RADIO_INTENCLR_ADDRESS_Msk RADIO_INTENCLR00_ADDRESS_Msk
#define RADIO_INTENSET_DISABLED_Msk RADIO_INTENSET00_DISABLED_Msk
#define RADIO_INTENCLR_DISABLED_Msk RADIO_INTENCLR00_DISABLED_Msk
#define RADIO_INTENSET_END_Msk RADIO_INTENSET00_END_Msk
#define RADIO_INTENCLR_END_Msk RADIO_INTENCLR00_END_Msk
#define RADIO_INTENCLR_PHYEND_Msk RADIO_INTENCLR00_PHYEND_Msk
#define RADIO_INTENSET_ADDRESS_Msk RADIO_INTENSET00_ADDRESS_Msk
#include "nrf54l15/phy_ppi.h"
#endif

#if BABBLESIM
extern void tm_tick(void);
#undef RADIO_STATE_STATE_Tx
#undef RADIO_STATE_STATE_TxDisable
#define RADIO_STATE_STATE_TxStarting (11UL) /* An additional state used in bsim */
#define RADIO_STATE_STATE_Tx (12UL) /* RADIO is in the TX state */
#define RADIO_STATE_STATE_TxDisable (13UL) /* RADIO is in the TXDISABLED state */
static inline uint32_t
ble_ll_tmr_t2u(uint32_t ticks)
{
    return ticks * (1000000.0 / 32768);
}
#endif

#endif /* H_PHY_PRIV_ */
