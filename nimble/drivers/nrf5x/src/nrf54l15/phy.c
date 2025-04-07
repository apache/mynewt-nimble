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

#include <nrfx_timer.h>
#include <nrfx_gpiote.h>
#include <helpers/nrfx_gppi.h>

#include <stdint.h>
#include <nrfx.h>
#include <controller/ble_fem.h>
#include "phy_priv.h"

/* Create PPIB links between RADIO and PERI power domain. */
#define PPIB_RADIO_PERI(_ch, _src, _dst)                  \
    NRF_PPIB11->SUBSCRIBE_SEND[_ch] = DPPI_CH_SUB(_src);  \
    NRF_PPIB21->PUBLISH_RECEIVE[_ch] = DPPI_CH_PUB(_dst); \
    NRF_DPPIC10->CHENSET |= 1 << DPPI_CH_ ## _src;        \
    NRF_DPPIC20->CHENSET |= 1 << DPPI_CH_ ## _dst;

#define PPIB_RADIO_PERI_0(_src, _dst) PPIB_RADIO_PERI(0, _src, _dst)
#define PPIB_RADIO_PERI_1(_src, _dst) PPIB_RADIO_PERI(1, _src, _dst)
#define PPIB_RADIO_PERI_2(_src, _dst) PPIB_RADIO_PERI(2, _src, _dst)
#define PPIB_RADIO_PERI_3(_src, _dst) PPIB_RADIO_PERI(3, _src, _dst)

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

#if PHY_USE_DEBUG
void
phy_debug_init(void)
{
#if PHY_USE_DEBUG_1
    nrf_gpio_cfg_output(MYNEWT_VAL(BLE_PHY_DBG_TIME_TXRXEN_READY_PIN));
    nrf_gpiote_task_configure(NRF_GPIOTE20, PHY_GPIOTE_DEBUG_1,
                              MYNEWT_VAL(BLE_PHY_DBG_TIME_TXRXEN_READY_PIN),
                              NRF_GPIOTE_POLARITY_NONE,
                              NRF_GPIOTE_INITIAL_VALUE_LOW);
    nrf_gpiote_task_enable(NRF_GPIOTE20, PHY_GPIOTE_DEBUG_1);

//    PPIB_RADIO_PERI_0(TIMER0_EVENTS_COMPARE_0, GPIOTE20_TASKS_SET_0);
    PPIB_MCU_PERI_2(DPPIC00_TIMER00_EVENTS_COMPARE_0, GPIOTE20_TASKS_SET_0);
    NRF_GPIOTE20->SUBSCRIBE_SET[PHY_GPIOTE_DEBUG_1] = DPPI_CH_SUB(GPIOTE20_TASKS_SET_0);

    NRF_RADIO->PUBLISH_READY = DPPI_CH_PUB(RADIO_EVENTS_READY);
    NRF_RADIO->PUBLISH_DISABLED = DPPI_CH_PUB(RADIO_EVENTS_DISABLED);
    PPIB_RADIO_PERI_1(RADIO_EVENTS_READY, GPIOTE20_TASKS_CLR_0);
    PPIB_RADIO_PERI_4(RADIO_EVENTS_DISABLED, GPIOTE20_TASKS_CLR_0);
//    PPIB_RADIO_PERI_5(TIMER0_EVENTS_COMPARE_3, GPIOTE20_TASKS_CLR_0);
    PPIB_MCU_PERI_3(DPPIC00_TIMER00_EVENTS_COMPARE_3, GPIOTE20_TASKS_CLR_0);
    NRF_GPIOTE20->SUBSCRIBE_CLR[PHY_GPIOTE_DEBUG_1] = DPPI_CH_SUB(GPIOTE20_TASKS_CLR_0);
#endif

#if PHY_USE_DEBUG_2
    nrf_gpio_cfg_output(MYNEWT_VAL(BLE_PHY_DBG_TIME_ADDRESS_END_PIN));
    nrf_gpiote_task_configure(NRF_GPIOTE20, PHY_GPIOTE_DEBUG_2,
                              MYNEWT_VAL(BLE_PHY_DBG_TIME_ADDRESS_END_PIN),
                              NRF_GPIOTE_POLARITY_NONE,
                              NRF_GPIOTE_INITIAL_VALUE_LOW);
    nrf_gpiote_task_enable(NRF_GPIOTE20, PHY_GPIOTE_DEBUG_2);

    PPIB_RADIO_PERI_2(RADIO_EVENTS_ADDRESS, GPIOTE20_TASKS_SET_1);
    NRF_GPIOTE20->SUBSCRIBE_SET[PHY_GPIOTE_DEBUG_2] = DPPI_CH_SUB(GPIOTE20_TASKS_SET_1);

//    PPIB_RADIO_PERI_8(RADIO_EVENTS_STOP, GPIOTE20_TASKS_CLR_1);
    PPIB_RADIO_PERI_3(RADIO_EVENTS_DISABLED, GPIOTE20_TASKS_CLR_1);
    NRF_GPIOTE20->SUBSCRIBE_CLR[PHY_GPIOTE_DEBUG_2] = DPPI_CH_SUB(GPIOTE20_TASKS_CLR_1);
#endif

#if PHY_USE_DEBUG_3
    nrf_gpio_cfg_output(MYNEWT_VAL(BLE_PHY_DBG_TIME_WFR_PIN));
    nrf_gpiote_task_configure(NRF_GPIOTE20, PHY_GPIOTE_DEBUG_3,
                              MYNEWT_VAL(BLE_PHY_DBG_TIME_WFR_PIN),
                              NRF_GPIOTE_POLARITY_NONE,
                              NRF_GPIOTE_INITIAL_VALUE_LOW);
    nrf_gpiote_task_enable(NRF_GPIOTE20, PHY_GPIOTE_DEBUG_3);

//    PPIB_RADIO_PERI_6(TIMER0_EVENTS_COMPARE_0, GPIOTE20_TASKS_SET_2);
    PPIB_MCU_PERI_0(DPPIC00_TIMER00_EVENTS_COMPARE_3, GPIOTE20_TASKS_SET_2);
    NRF_GPIOTE20->SUBSCRIBE_SET[PHY_GPIOTE_DEBUG_3] = DPPI_CH_SUB(GPIOTE20_TASKS_SET_2);

//    PPIB_RADIO_PERI_7(TIMER0_EVENTS_COMPARE_3, GPIOTE20_TASKS_CLR_2);
    PPIB_MCU_PERI_1(DPPIC00_TIMER00_EVENTS_COMPARE_3, GPIOTE20_TASKS_CLR_2);
    NRF_GPIOTE20->SUBSCRIBE_CLR[PHY_GPIOTE_DEBUG_3] = DPPI_CH_SUB(GPIOTE20_TASKS_CLR_2);
#endif
    phy_ppi_debug_enable();
}
#endif /* PHY_USE_DEBUG */

void
phy_ppi_init(void)
{
    NRF_RADIO->PUBLISH_ADDRESS = DPPI_CH_PUB(RADIO_EVENTS_ADDRESS);
    NRF_RADIO->PUBLISH_END = DPPI_CH_PUB(RADIO_EVENTS_END);
    NRF_RADIO->PUBLISH_PAYLOAD = DPPI_CH_PUB(RADIO_EVENTS_PAYLOAD_RADIO);
    NRF_RADIO->PUBLISH_BCMATCH = DPPI_CH_PUB(RADIO_EVENTS_BCMATCH);
    NRF_RTC0->PUBLISH_COMPARE[0] = DPPI_CH_PUB(RTC0_EVENTS_COMPARE_0);

    NRF_TIMER0->PUBLISH_COMPARE[0] = DPPI_CH_PUB(TIMER0_EVENTS_COMPARE_0);
    NRF_TIMER0->PUBLISH_COMPARE[3] = DPPI_CH_PUB(TIMER0_EVENTS_COMPARE_3);
    NRF_TIMER0->SUBSCRIBE_CAPTURE[1] = DPPI_CH_SUB(RADIO_EVENTS_ADDRESS);
    NRF_TIMER0->SUBSCRIBE_CAPTURE[2] = DPPI_CH_SUB(RADIO_EVENTS_END);

    PPIB_RADIO_MCU_0(RADIO_EVENTS_PAYLOAD_RADIO, DPPIC00_RADIO_EVENTS_PAYLOAD_CCM);

#if MYNEWT_VAL(BLE_CHANNEL_SOUNDING)
    RADIO_PUBLISH_CSTONESEND = DPPI_CH_PUB(RADIO_EVENTS_CSTONES_END);
    /* CC[0] for trigerring TXEN and RXEN */
    NRF_TIMER00->PUBLISH_COMPARE[0] = DPPI_CH_PUB(DPPIC00_TIMER00_EVENTS_COMPARE_0);
    /* CC[2] for trigerring START */
    NRF_TIMER00->PUBLISH_COMPARE[2] = DPPI_CH_PUB(DPPIC00_TIMER00_EVENTS_COMPARE_2);
    /* CC[3] for trigerring WFR */
    NRF_TIMER00->PUBLISH_COMPARE[3] = DPPI_CH_PUB(DPPIC00_TIMER00_EVENTS_COMPARE_3);
    /* CC[4] for trigerring CSTONESSTART */
    NRF_TIMER00->PUBLISH_COMPARE[4] = DPPI_CH_PUB(DPPIC00_TIMER00_EVENTS_COMPARE_4);
//    /* CC[1] for capturing ADDRESS event time */
//    NRF_TIMER00->SUBSCRIBE_CAPTURE[1] = DPPI_CH_SUB(DPPIC00_RADIO_EVENTS_ADDRESS);
//    /* CC[5] for capturing CSTONESEND event time */
//    NRF_TIMER00->SUBSCRIBE_CAPTURE[5] = DPPI_CH_SUB(DPPIC00_RADIO_EVENTS_CSTONES_END);
    /* Proxy between domains */
    PPIB_MCU_RADIO_0(DPPIC00_TIMER00_EVENTS_COMPARE_0, TIMER00_EVENTS_COMPARE_0);
    PPIB_MCU_RADIO_1(DPPIC00_TIMER00_EVENTS_COMPARE_2, TIMER00_EVENTS_COMPARE_2);
    PPIB_MCU_RADIO_2(DPPIC00_TIMER00_EVENTS_COMPARE_3, TIMER00_EVENTS_COMPARE_3);
    PPIB_MCU_RADIO_3(DPPIC00_TIMER00_EVENTS_COMPARE_4, TIMER00_EVENTS_COMPARE_4);

    /* Proxy for capturing ADDRESS or CSTONESEND event time */
    PPIB_RADIO_MCU_1(RADIO_EVENTS_ADDRESS, DPPIC00_RADIO_EVENTS_ADDRESS);
    /* Proxy for starting TIMER00 */
    PPIB_RADIO_MCU_2(RTC0_EVENTS_COMPARE_0, DPPIC00_RTC0_EVENTS_COMPARE_0);
    PPIB_RADIO_MCU_3(RADIO_EVENTS_CSTONES_END, DPPIC00_RADIO_EVENTS_CSTONES_END);
#endif

    /* Enable channels we publish on */
    NRF_DPPIC10->CHENSET = DPPI_CH_ENABLE_ALL;
}

void
phy_txpower_set(int8_t dbm)
{
    uint16_t val;

    switch (dbm) {
    case 8:
        val = RADIO_TXPOWER_TXPOWER_Pos8dBm;
        break;
    case 7:
        val = RADIO_TXPOWER_TXPOWER_Pos7dBm;
        break;
    case 6:
        val = RADIO_TXPOWER_TXPOWER_Pos6dBm;
        break;
    case 5:
        val = RADIO_TXPOWER_TXPOWER_Pos5dBm;
        break;
    case 4:
        val = RADIO_TXPOWER_TXPOWER_Pos4dBm;
        break;
    case 3:
        val = RADIO_TXPOWER_TXPOWER_Pos3dBm;
        break;
    case 2:
        val = RADIO_TXPOWER_TXPOWER_Pos2dBm;
        break;
    case 1:
        val = RADIO_TXPOWER_TXPOWER_Pos1dBm;
        break;
    case 0:
        val = RADIO_TXPOWER_TXPOWER_0dBm;
        break;
    case -1:
        val = RADIO_TXPOWER_TXPOWER_Neg1dBm;
        break;
    case -2:
        val = RADIO_TXPOWER_TXPOWER_Neg2dBm;
        break;
    case -3:
        val = RADIO_TXPOWER_TXPOWER_Neg3dBm;
        break;
    case -4:
        val = RADIO_TXPOWER_TXPOWER_Neg4dBm;
        break;
    case -5:
        val = RADIO_TXPOWER_TXPOWER_Neg5dBm;
        break;
    case -6:
        val = RADIO_TXPOWER_TXPOWER_Neg6dBm;
        break;
    case -7:
        val = RADIO_TXPOWER_TXPOWER_Neg7dBm;
        break;
    case -8:
        val = RADIO_TXPOWER_TXPOWER_Neg8dBm;
        break;
    case -9:
        val = RADIO_TXPOWER_TXPOWER_Neg9dBm;
        break;
    case -10:
        val = RADIO_TXPOWER_TXPOWER_Neg10dBm;
        break;
    case -12:
        val = RADIO_TXPOWER_TXPOWER_Neg12dBm;
        break;
    case -14:
        val = RADIO_TXPOWER_TXPOWER_Neg14dBm;
        break;
    case -16:
        val = RADIO_TXPOWER_TXPOWER_Neg16dBm;
        break;
    case -18:
        val = RADIO_TXPOWER_TXPOWER_Neg18dBm;
        break;
    case -20:
        val = RADIO_TXPOWER_TXPOWER_Neg20dBm;
        break;
    case -22:
        val = RADIO_TXPOWER_TXPOWER_Neg22dBm;
        break;
    case -28:
        val = RADIO_TXPOWER_TXPOWER_Neg28dBm;
        break;
    case -40:
        val = RADIO_TXPOWER_TXPOWER_Neg40dBm;
        break;
    case -46:
        val = RADIO_TXPOWER_TXPOWER_Neg46dBm;
        break;
    default:
        val = RADIO_TXPOWER_TXPOWER_0dBm;
    }

    NRF_RADIO->TXPOWER = val;
}

int8_t
phy_txpower_round(int8_t dbm)
{
    if (dbm >= (int8_t)8) {
        return (int8_t)8;
    }

    if (dbm >= (int8_t)-10) {
        return (int8_t)dbm;
    }

    if (dbm >= (int8_t)-12) {
        return (int8_t)-12;
    }

    if (dbm >= (int8_t)-14) {
        return (int8_t)-14;
    }

    if (dbm >= (int8_t)-16) {
        return (int8_t)-16;
    }

    if (dbm >= (int8_t)-18) {
        return (int8_t)-18;
    }

    if (dbm >= (int8_t)-20) {
        return (int8_t)-20;
    }

    if (dbm >= (int8_t)-22) {
        return (int8_t)-22;
    }

    if (dbm >= (int8_t)-28) {
        return (int8_t)-28;
    }

    if (dbm >= (int8_t)-40) {
        return (int8_t)-40;
    }

    return (int8_t)-46;
}
