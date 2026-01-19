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

#ifndef H_PHY_HW_
#define H_PHY_HW_

#include <hal/nrf_ccm.h>
#include <hal/nrf_timer.h>

struct nrf_ccm_data {
    uint8_t key[16];
    uint64_t pkt_counter;
    uint8_t dir_bit;
    uint8_t iv[8];
} __attribute__((packed));

#define NRF_TIMER0 NRF_TIMER10
#define NRF_DPPIC NRF_DPPIC10
#define NRF_RTC0 NRF_RTC10
#define NRF_AAR NRF_AAR00
#define NRF_CCM NRF_CCM00
#define NRF_AAR NRF_AAR00
#define NRF_GPIOTE NRF_GPIOTE20

#define RADIO_IRQn RADIO_0_IRQn
#define RADIO_INTENSET_ADDRESS_Msk RADIO_INTENSET00_ADDRESS_Msk
#define RADIO_INTENCLR_ADDRESS_Msk RADIO_INTENCLR00_ADDRESS_Msk
#define RADIO_INTENSET_DISABLED_Msk RADIO_INTENSET00_DISABLED_Msk
#define RADIO_INTENCLR_DISABLED_Msk RADIO_INTENCLR00_DISABLED_Msk

#define NRF_RADIO_INTENSET NRF_RADIO->INTENSET00

/* To disable all radio interrupts */
#define NRF_RADIO_IRQ_MASK_ALL  (RADIO_INTENSET00_READY_Msk    | \
                                 RADIO_INTENSET00_ADDRESS_Msk  | \
                                 RADIO_INTENSET00_PAYLOAD_Msk  | \
                                 RADIO_INTENSET00_PHYEND_Msk   | \
                                 RADIO_INTENSET00_DISABLED_Msk | \
                                 RADIO_INTENSET00_DEVMATCH_Msk | \
                                 RADIO_INTENSET00_DEVMISS_Msk  | \
                                 RADIO_INTENSET00_BCMATCH_Msk  | \
                                 RADIO_INTENSET00_CRCOK_Msk    | \
                                 RADIO_INTENSET00_CRCERROR_Msk)

#define NRF_AAR_NIRK NRF_AAR->MAXRESOLVED
#define NRF_AAR_IRKPTR NRF_AAR->IN.PTR
#define NRF_AAR_ADDRPTR NRF_AAR->IN.PTR
#define NRF_AAR_STATUS NRF_AAR->ERRORSTATUS

#define CCM_MODE_DATARATE_125Kbps CCM_MODE_DATARATE_125Kbit
#define CCM_MODE_DATARATE_500Kbps CCM_MODE_DATARATE_500Kbit

#define NRF_CCM_STATUS NRF_CCM->MACSTATUS
#define NRF_CCM_EVENTS_END NRF_CCM->EVENTS_END

uint32_t ble_phy_get_ccm_datarate(void);

static inline void
phy_hw_ccm_init(void)
{
}

static inline void
phy_hw_ccm_setup_tx(uint8_t *in_ptr, uint8_t *out_ptr,
                    uint8_t *scratch_ptr, struct nrf_ccm_data *ccm_data)
{
    NRF_CCM->IN.PTR = (uint32_t)in_ptr;
    NRF_CCM->OUT.PTR = (uint32_t)out_ptr;
    NRF_CCM->EVENTS_ERROR = 0;
    NRF_CCM->MODE = CCM_MODE_MACLEN_Pos | ble_phy_get_ccm_datarate();
    memcpy((uint8_t *) NRF_CCM->KEY.VALUE, &ccm_data->key, sizeof(ccm_data->key));
}

static inline void
phy_hw_ccm_setup_rx(uint8_t *in_ptr, uint8_t *out_ptr,
                    uint8_t *scratch_ptr, struct nrf_ccm_data *ccm_data)
{
    NRF_CCM->IN.PTR = (uint32_t)in_ptr;
    NRF_CCM->OUT.PTR = (uint32_t)out_ptr;
    NRF_CCM->MODE = CCM_MODE_MACLEN_Pos | CCM_MODE_MODE_Decryption |
                    ble_phy_get_ccm_datarate();
    memcpy((uint8_t *) NRF_CCM->KEY.VALUE, &ccm_data->key,
           sizeof(ccm_data->key));
    NRF_CCM->EVENTS_ERROR = 0;
    NRF_CCM->EVENTS_END = 0;
}

static inline void
phy_hw_ccm_start(void)
{
    nrf_ccm_task_trigger(NRF_CCM, NRF_CCM_TASK_START);
}

static inline void
phy_hw_radio_fast_ru_setup(void)
{
    NRF_RADIO->TIMING = (RADIO_TIMING_RU_Fast << RADIO_TIMING_RU_Pos) &
                        RADIO_TIMING_RU_Msk;
}

static inline void
phy_hw_radio_events_clear(void)
{
    NRF_RADIO->EVENTS_READY = 0;
    NRF_RADIO->EVENTS_PHYEND = 0;
    NRF_RADIO->EVENTS_END = 0;
    NRF_RADIO->EVENTS_DISABLED = 0;
}

static inline void
phy_hw_radio_shorts_setup_tx(void)
{
    NRF_RADIO->SHORTS = RADIO_SHORTS_PHYEND_DISABLE_Msk |
                        RADIO_SHORTS_READY_START_Msk;
}

static inline void
phy_hw_radio_shorts_setup_rx(void)
{
    NRF_RADIO->SHORTS = RADIO_SHORTS_PHYEND_DISABLE_Msk |
                        RADIO_SHORTS_READY_START_Msk |
                        RADIO_SHORTS_ADDRESS_BCSTART_Msk |
                        RADIO_SHORTS_ADDRESS_RSSISTART_Msk;
}

static inline void
phy_hw_radio_datawhite_set(uint8_t chan)
{
    NRF_RADIO->DATAWHITE = RADIO_DATAWHITE_ResetValue | chan;
}

static inline void
phy_hw_timer_configure(void)
{
    NRF_TIMER0->PRESCALER = 5;
}

static inline void
phy_hw_radio_timer_task_stop(void)
{
    nrf_timer_task_trigger(NRF_TIMER0, NRF_TIMER_TASK_STOP);
}

static inline void
phy_hw_aar_irk_setup(uint32_t *irk_ptr, uint32_t *scratch_ptr)
{
    /* TODO */
}

static inline void
phy_hw_aar_addrptr_set(uint8_t *dptr)
{
    /* TODO */
}

#endif /* H_PHY_HW_ */
