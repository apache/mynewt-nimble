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

#include <nrf_gpio.h>
#include <nrf_gpiote.h>
#include <hal/nrf_ccm.h>
#include <hal/nrf_timer.h>

struct nrf_ccm_data {
    uint8_t key[16];
    uint64_t pkt_counter;
    uint8_t dir_bit;
    uint8_t iv[8];
} __attribute__((packed));

/* To disable all radio interrupts */
#define NRF_RADIO_IRQ_MASK_ALL  (0x34FF)

#define NRF_RADIO_INTENSET NRF_RADIO->INTENSET

#define NRF_AAR_NIRK NRF_AAR->NIRK
#define NRF_AAR_IRKPTR NRF_AAR->IRKPTR
#define NRF_AAR_ADDRPTR NRF_AAR->ADDRPTR
#define NRF_AAR_STATUS NRF_AAR->STATUS

#define NRF_CCM_STATUS NRF_CCM->MICSTATUS
#define NRF_CCM_EVENTS_END NRF_CCM->EVENTS_ENDCRYPT

uint32_t ble_phy_get_ccm_datarate(void);

static inline void
phy_hw_ccm_init(void)
{
    NRF_CCM->SHORTS = CCM_SHORTS_ENDKSGEN_CRYPT_Msk;
}

static inline void
phy_hw_ccm_setup_tx(uint8_t *in_ptr, uint8_t *out_ptr,
                    uint8_t *scratch_ptr, struct nrf_ccm_data *ccm_data)
{
    NRF_CCM->SHORTS = CCM_SHORTS_ENDKSGEN_CRYPT_Msk;
    NRF_CCM->INPTR = (uint32_t)in_ptr;
    NRF_CCM->OUTPTR = (uint32_t)out_ptr;
    NRF_CCM->SCRATCHPTR = (uint32_t)scratch_ptr;
    NRF_CCM->EVENTS_ERROR = 0;
    NRF_CCM->MODE = CCM_MODE_LENGTH_Msk | ble_phy_get_ccm_datarate();
    NRF_CCM->CNFPTR = (uint32_t)ccm_data;
}

static inline void
phy_hw_ccm_setup_rx(uint8_t *in_ptr, uint8_t *out_ptr,
                    uint8_t *scratch_ptr, struct nrf_ccm_data *ccm_data)
{
    NRF_CCM->INPTR = (uint32_t)in_ptr;
    NRF_CCM->OUTPTR = (uint32_t)out_ptr;
    NRF_CCM->SCRATCHPTR = (uint32_t)scratch_ptr;
    NRF_CCM->MODE = CCM_MODE_LENGTH_Msk | CCM_MODE_MODE_Decryption |
                    ble_phy_get_ccm_datarate();
    NRF_CCM->CNFPTR = (uint32_t)ccm_data;
    NRF_CCM->SHORTS = 0;
    NRF_CCM->EVENTS_ERROR = 0;
    NRF_CCM->EVENTS_ENDCRYPT = 0;
}

static inline void
phy_hw_ccm_start(void)
{
    nrf_ccm_task_trigger(NRF_CCM, NRF_CCM_TASK_KSGEN);
}

static inline void
phy_hw_radio_fast_ru_setup(void)
{
    NRF_RADIO->MODECNF0 |= (RADIO_MODECNF0_RU_Fast << RADIO_MODECNF0_RU_Pos) &
                           RADIO_MODECNF0_RU_Msk;
}

static inline void
phy_hw_radio_events_clear(void)
{
    NRF_RADIO->EVENTS_READY = 0;
    NRF_RADIO->EVENTS_END = 0;
    NRF_RADIO->EVENTS_DISABLED = 0;
}

static inline void
phy_hw_radio_shorts_setup_tx(void)
{
    NRF_RADIO->SHORTS = RADIO_SHORTS_END_DISABLE_Msk |
                        RADIO_SHORTS_READY_START_Msk;
}

static inline void
phy_hw_radio_shorts_setup_rx(void)
{
    NRF_RADIO->EVENTS_RSSIEND = 0;
    NRF_RADIO->SHORTS = RADIO_SHORTS_END_DISABLE_Msk |
                        RADIO_SHORTS_READY_START_Msk |
                        RADIO_SHORTS_ADDRESS_BCSTART_Msk |
                        RADIO_SHORTS_ADDRESS_RSSISTART_Msk |
                        RADIO_SHORTS_DISABLED_RSSISTOP_Msk;
}

static inline void
phy_hw_radio_datawhite_set(uint8_t chan)
{
    NRF_RADIO->DATAWHITEIV = chan;
}

static inline void
phy_hw_timer_configure(void)
{
    NRF_TIMER0->PRESCALER = 4;
}

static inline void
phy_hw_radio_timer_task_stop(void)
{
    nrf_timer_task_trigger(NRF_TIMER0, NRF_TIMER_TASK_STOP);
    NRF_TIMER0->TASKS_SHUTDOWN = 1;
}

static inline void
phy_hw_aar_irk_setup(uint32_t *irk_ptr, uint32_t *scratch_ptr)
{
    NRF_AAR->IRKPTR = (uint32_t)irk_ptr;
    NRF_AAR->SCRATCHPTR = (uint32_t)scratch_ptr;
}

static inline void
phy_hw_aar_addrptr_set(uint8_t *dptr)
{
    NRF_AAR->ADDRPTR = (uint32_t)dptr;
}

static inline void
phy_gpiote_configure(int idx, int pin)
{
    nrf_gpio_cfg_output(pin);
    nrf_gpiote_task_configure(NRF_GPIOTE, idx, pin, NRF_GPIOTE_POLARITY_NONE,
                              NRF_GPIOTE_INITIAL_VALUE_LOW);
    nrf_gpiote_task_enable(NRF_GPIOTE, idx);
}

#endif /* H_PHY_HW_ */
