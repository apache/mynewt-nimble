/**
 * Copyright (c) 2015 Stack Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdint.h>
#include <assert.h>
#include "os/os.h"
#include "bsp/cmsis_nvic.h"
#include "nimble/ble.h"
#include "controller/ble_phy.h"
#include "controller/ble_ll.h"
#include "hal/hal_cputime.h"
#include "mcu/nrf52.h"
#include "mcu/nrf52_bitfields.h"

/* To disable all radio interrupts */
#define NRF52_RADIO_IRQ_MASK_ALL    (0x34FF)

/* 
 * We configure the nrf52 with a 1 byte S0 field, 8 bit length field, and
 * zero bit S1 field. The preamble is 8 bits long.
 */
#define NRF52_LFLEN_BITS        (8)
#define NRF52_S0_LEN            (1)

/* Maximum length of frames */
#define NRF52_MAXLEN            (255)
#define NRF52_BALEN             (3)     /* For base address of 3 bytes */

/* Maximum tx power */
#define NRF52_TX_PWR_MAX_DBM    (4)
#define NRF52_TX_PWR_MIN_DBM    (-40)

/* BLE PHY data structure */
struct ble_phy_obj
{
    int8_t  phy_txpwr_dbm;
    uint8_t phy_chan;
    uint8_t phy_state;
    uint8_t phy_transition;
    struct os_mbuf *rxpdu;
};
struct ble_phy_obj g_ble_phy_data;

/* Statistics */
struct ble_phy_statistics
{
    uint32_t tx_good;
    uint32_t tx_fail;
    uint32_t tx_bytes;
    uint32_t rx_starts;
    uint32_t rx_valid;
    uint32_t rx_crc_err;
    uint32_t phy_isrs;
    uint32_t radio_state_errs;
    uint32_t no_bufs;
};

struct ble_phy_statistics g_ble_phy_stats;

/* XXX: TODO:
 
 * 1) Test the following to make sure it works: suppose an event is already
 * set to 1 and the interrupt is not enabled. What happens if you enable the
 * interrupt with the event bit already set to 1  
 */

/**
 * ble phy rxpdu get
 *  
 * Gets a mbuf for PDU reception. 
 * 
 * @return struct os_mbuf* Pointer to retrieved mbuf or NULL if none available
 */
static struct os_mbuf *
ble_phy_rxpdu_get(void)
{
    struct os_mbuf *m;

    m = g_ble_phy_data.rxpdu;
    if (m == NULL) {
        m = os_mbuf_get_pkthdr(&g_mbuf_pool);
        if (!m) {
            ++g_ble_phy_stats.no_bufs;
        } else {
            g_ble_phy_data.rxpdu = m;
        }
    }

    return m;
}

static void
nrf52_wait_disabled(void)
{
    uint32_t state;

    state = NRF_RADIO->STATE;
    if (state != RADIO_STATE_STATE_Disabled) {
        if ((state == RADIO_STATE_STATE_RxDisable) ||
            (state == RADIO_STATE_STATE_TxDisable)) {
            /* This will end within a short time (6 usecs). Just poll */
            while (NRF_RADIO->STATE == state) {
                /* If this fails, something is really wrong. Should last
                 * no more than 6 usecs */
                /* XXX: should I have a way to bail out? */
            }
        }
    }
}

static void
ble_phy_isr(void)
{
    int rc;
    uint8_t transition;
    uint32_t irq_en;
    uint32_t state;
    uint32_t shortcuts;
    struct os_mbuf *rxpdu;
    struct ble_mbuf_hdr *ble_hdr;

    /* Check for disabled event. This only happens for transmits now */
    irq_en = NRF_RADIO->INTENCLR;
    if ((irq_en & RADIO_INTENCLR_DISABLED_Msk) && NRF_RADIO->EVENTS_DISABLED) {
        /* Better be in TX state! */
        assert(g_ble_phy_data.phy_state == BLE_PHY_STATE_TX);

        /* Clear events and clear interrupt on disabled event */
        NRF_RADIO->EVENTS_DISABLED = 0;
        NRF_RADIO->INTENCLR = RADIO_INTENCLR_DISABLED_Msk;
        NRF_RADIO->EVENTS_END = 0;
        shortcuts = NRF_RADIO->SHORTS;

        transition = g_ble_phy_data.phy_transition;
        if (transition == BLE_PHY_TRANSITION_TX_RX) {
            /* XXX: for now, assume we receive on logical address 0 */
            NRF_RADIO->RXADDRESSES = 1;

            /* Debug check to make sure we go from tx to rx */
            assert((shortcuts & RADIO_SHORTS_DISABLED_RXEN_Msk) != 0);

            /* Packet pointer needs to be reset. */
            if (g_ble_phy_data.rxpdu != NULL) {
                NRF_RADIO->PACKETPTR = (uint32_t)g_ble_phy_data.rxpdu->om_data;

                /* I want to know when 1st byte received (after address) */
                NRF_RADIO->BCC = 8; /* in bits */
                NRF_RADIO->EVENTS_ADDRESS = 0;
                NRF_RADIO->EVENTS_BCMATCH = 0;
                NRF_RADIO->EVENTS_RSSIEND = 0;
                NRF_RADIO->SHORTS = RADIO_SHORTS_END_DISABLE_Msk | 
                                    RADIO_SHORTS_READY_START_Msk |
                                    RADIO_SHORTS_ADDRESS_BCSTART_Msk |
                                    RADIO_SHORTS_ADDRESS_RSSISTART_Msk |
                                    RADIO_SHORTS_DISABLED_RSSISTOP_Msk;

                NRF_RADIO->INTENSET = RADIO_INTENSET_ADDRESS_Msk;
                g_ble_phy_data.phy_state = BLE_PHY_STATE_RX;
            } else {
                /* Disable the phy */
                /* XXX: count no bufs? */
                ble_phy_disable();
            }
        } else {
            /* Better not be going from rx to tx! */
            assert(transition == BLE_PHY_TRANSITION_NONE);
        }
    }

    /* We get this if we have started to receive a frame */
    if ((irq_en & RADIO_INTENCLR_ADDRESS_Msk) && NRF_RADIO->EVENTS_ADDRESS) {
        /* Clear events and clear interrupt */
        NRF_RADIO->EVENTS_ADDRESS = 0;
        NRF_RADIO->INTENCLR = RADIO_INTENCLR_ADDRESS_Msk;

        assert(g_ble_phy_data.rxpdu != NULL);

        /* Wait to get 1st byte of frame */
        while (1) {
            state = NRF_RADIO->STATE;
            if (NRF_RADIO->EVENTS_BCMATCH != 0) {
                break;
            }

            /* 
             * If state is disabled, we should have the BCMATCH. If not,
             * something is wrong!
             */ 
            if (state == RADIO_STATE_STATE_Disabled) {
                NRF_RADIO->INTENCLR = NRF52_RADIO_IRQ_MASK_ALL;
                NRF_RADIO->SHORTS = 0;
                goto phy_isr_exit;
            }
        }

        /* Call Link Layer receive start function */
        rc = ble_ll_rx_start(g_ble_phy_data.rxpdu);
        if (rc >= 0) {
            if (rc > 0) {
                /* We need to go from disabled to TXEN */
                NRF_RADIO->SHORTS = RADIO_SHORTS_END_DISABLE_Msk | 
                                    RADIO_SHORTS_READY_START_Msk |
                                    RADIO_SHORTS_DISABLED_TXEN_Msk;
            } else {
                NRF_RADIO->SHORTS = RADIO_SHORTS_END_DISABLE_Msk | 
                                    RADIO_SHORTS_READY_START_Msk;
            }

            /* Set rx end ISR enable */
            NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;
        } else {
            /* Disable PHY */
            ble_phy_disable();
            irq_en = 0;
        }

        /* Count rx starts */
        ++g_ble_phy_stats.rx_starts;
    }

    /* Receive packet end (we dont enable this for transmit) */
    if ((irq_en & RADIO_INTENCLR_END_Msk) && NRF_RADIO->EVENTS_END) {
        /* Clear events and clear interrupt */
        NRF_RADIO->EVENTS_END = 0;
        NRF_RADIO->INTENCLR = RADIO_INTENCLR_END_Msk;

        /* Construct BLE header before handing up */
        ble_hdr = BLE_MBUF_HDR_PTR(g_ble_phy_data.rxpdu);
        ble_hdr->flags = 0;
        assert(NRF_RADIO->EVENTS_RSSIEND != 0);
        ble_hdr->rssi = -1 * NRF_RADIO->RSSISAMPLE;
        ble_hdr->channel = g_ble_phy_data.phy_chan;
        ble_hdr->crcok = (uint8_t)NRF_RADIO->CRCSTATUS;

        /* Count PHY crc errors and valid packets */
        if (ble_hdr->crcok == 0) {
            ++g_ble_phy_stats.rx_crc_err;
        } else {
            ++g_ble_phy_stats.rx_valid;
        }

        /* Call Link Layer receive payload function */
        rxpdu = g_ble_phy_data.rxpdu;
        g_ble_phy_data.rxpdu = NULL;
        rc = ble_ll_rx_end(rxpdu, ble_hdr->crcok);
        if (rc < 0) {
            /* Disable the PHY. */
            ble_phy_disable();
        }
    }

phy_isr_exit:
    /* Ensures IRQ is cleared */
    shortcuts = NRF_RADIO->SHORTS;

    /* Count # of interrupts */
    ++g_ble_phy_stats.phy_isrs;
}

/**
 * ble phy init 
 *  
 * Initialize the PHY. This is expected to be called once. 
 * 
 * @return int 0: success; PHY error code otherwise
 */
int
ble_phy_init(void)
{
    uint32_t os_tmo;

    /* Make sure HFXO is started */
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;
    os_tmo = os_time_get() + (5 * (1000 / OS_TICKS_PER_SEC));
    while (1) {
        if (NRF_CLOCK->EVENTS_HFCLKSTARTED) {
            break;
        }
        if ((int32_t)(os_time_get() - os_tmo) > 0) {
            return BLE_PHY_ERR_INIT;
        }
    }

    /* Set phy channel to an invalid channel so first set channel works */
    g_ble_phy_data.phy_chan = BLE_PHY_NUM_CHANS;

    /* Toggle peripheral power to reset (just in case) */
    NRF_RADIO->POWER = 0;
    NRF_RADIO->POWER = 1;

    /* Disable all interrupts */
    NRF_RADIO->INTENCLR = NRF52_RADIO_IRQ_MASK_ALL;

    /* Set configuration registers */
    NRF_RADIO->MODE = RADIO_MODE_MODE_Ble_1Mbit;
    NRF_RADIO->PCNF0 = (NRF52_LFLEN_BITS << RADIO_PCNF0_LFLEN_Pos) |
                       (NRF52_S0_LEN << RADIO_PCNF0_S0LEN_Pos) |
                       (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos);
    NRF_RADIO->PCNF1 = NRF52_MAXLEN | 
                       (RADIO_PCNF1_ENDIAN_Little <<  RADIO_PCNF1_ENDIAN_Pos) |
                       (NRF52_BALEN << RADIO_PCNF1_BALEN_Pos) |
                       RADIO_PCNF1_WHITEEN_Msk;

    /* Set base0 with the advertising access address */
    NRF_RADIO->BASE0 = (BLE_ACCESS_ADDR_ADV << 8) & 0xFFFFFF00;
    NRF_RADIO->PREFIX0 = (BLE_ACCESS_ADDR_ADV >> 24) & 0xFF;

    /* Configure the CRC registers */
    NRF_RADIO->CRCCNF = RADIO_CRCCNF_SKIPADDR_Msk | RADIO_CRCCNF_LEN_Three;

    /* Configure BLE poly */
    NRF_RADIO->CRCPOLY = 0x0100065B;

    /* Configure IFS */
    NRF_RADIO->TIFS = BLE_LL_IFS;

    /* Set isr in vector table and enable interrupt */
    NVIC_SetPriority(RADIO_IRQn, 0);
    NVIC_SetVector(RADIO_IRQn, (uint32_t)ble_phy_isr);
    NVIC_EnableIRQ(RADIO_IRQn);

    return 0;
}

int 
ble_phy_rx(void)
{
    /* Check radio state */
    nrf52_wait_disabled();
    if (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled) {
        ble_phy_disable();
        ++g_ble_phy_stats.radio_state_errs;
        return BLE_PHY_ERR_RADIO_STATE;
    }

    /* If no pdu, get one */
    if (ble_phy_rxpdu_get() == NULL) {
        return BLE_PHY_ERR_NO_BUFS;
    }

    /* XXX: Assume that this is an advertising channel */
    NRF_RADIO->CRCINIT = BLE_LL_CRCINIT_ADV;

    /* XXX: for now, assume we receive on logical address 0 */
    NRF_RADIO->RXADDRESSES = 1;

    /* Set packet pointer */
    NRF_RADIO->PACKETPTR = (uint32_t)g_ble_phy_data.rxpdu->om_data;

    /* Make sure all interrupts are disabled */
    NRF_RADIO->INTENCLR = NRF52_RADIO_IRQ_MASK_ALL;

    /* Clear events prior to enabling receive */
    NRF_RADIO->EVENTS_END = 0;
    NRF_RADIO->EVENTS_ADDRESS = 0;
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->EVENTS_BCMATCH = 0;
    NRF_RADIO->EVENTS_RSSIEND = 0;

    /* I want to know when 1st byte received (after address) */
    NRF_RADIO->BCC = 8; /* in bits */
    NRF_RADIO->SHORTS = RADIO_SHORTS_END_DISABLE_Msk | 
                        RADIO_SHORTS_READY_START_Msk |
                        RADIO_SHORTS_ADDRESS_BCSTART_Msk |
                        RADIO_SHORTS_ADDRESS_RSSISTART_Msk |
                        RADIO_SHORTS_DISABLED_RSSISTOP_Msk;

    NRF_RADIO->INTENSET = RADIO_INTENSET_ADDRESS_Msk;

    /* Start the receive task in the radio */
    NRF_RADIO->TASKS_RXEN = 1;

    g_ble_phy_data.phy_state = BLE_PHY_STATE_RX;

    return 0;
}

int
ble_phy_tx(struct os_mbuf *txpdu, uint8_t beg_trans, uint8_t end_trans)
{
    int rc;
    uint32_t state;
    uint32_t shortcuts;

    /* Better have a pdu! */
    assert(txpdu != NULL);

    /* If radio is not disabled, */
    nrf52_wait_disabled();

    if (beg_trans == BLE_PHY_TRANSITION_RX_TX) {
        if ((NRF_RADIO->SHORTS & RADIO_SHORTS_DISABLED_TXEN_Msk) == 0) {
            assert(0);
        }
        /* Radio better be in TXRU state or we are in bad shape */
        state = RADIO_STATE_STATE_TxRu;
    } else {
        /* Radio should be in disabled state */
        state = RADIO_STATE_STATE_Disabled;
    }

    if (NRF_RADIO->STATE != state) {
        ble_phy_disable();
        ++g_ble_phy_stats.radio_state_errs;
        return BLE_PHY_ERR_RADIO_STATE;
    }

    /* Select tx address */
    if (g_ble_phy_data.phy_chan < BLE_PHY_NUM_DATA_CHANS) {
        /* XXX: fix this */
        assert(0);
        NRF_RADIO->TXADDRESS = 0;
        NRF_RADIO->RXADDRESSES = 0;
        NRF_RADIO->CRCINIT = 0;
    } else {
        NRF_RADIO->TXADDRESS = 0;
        NRF_RADIO->RXADDRESSES = 1;
        NRF_RADIO->CRCINIT = BLE_LL_CRCINIT_ADV;
    }

    /* Set radio transmit data pointer */
    NRF_RADIO->PACKETPTR = (uint32_t)txpdu->om_data;

    /* Clear the ready, end and disabled events */
    NRF_RADIO->EVENTS_READY = 0;
    NRF_RADIO->EVENTS_END = 0;
    NRF_RADIO->EVENTS_DISABLED = 0;

    /* Enable shortcuts for transmit start/end. */
    shortcuts = RADIO_SHORTS_END_DISABLE_Msk | RADIO_SHORTS_READY_START_Msk;
    if (end_trans == BLE_PHY_TRANSITION_TX_RX) {
        /* If we are going into receive after this, try to get a buffer. */
        if (ble_phy_rxpdu_get()) {
            shortcuts |= RADIO_SHORTS_DISABLED_RXEN_Msk;
        }
        NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
    }
    NRF_RADIO->SHORTS = shortcuts;

    /* Trigger transmit if our state was disabled */
    if (state == RADIO_STATE_STATE_Disabled) {
        NRF_RADIO->TASKS_TXEN = 1;
    }

    /* Set the PHY transition */
    g_ble_phy_data.phy_transition = end_trans;

    /* Read back radio state. If in TXRU, we are fine */
    state = NRF_RADIO->STATE;
    if ((state == RADIO_STATE_STATE_TxRu) || (state == RADIO_STATE_STATE_Tx)) {
        /* Set phy state to transmitting and count packet statistics */
        g_ble_phy_data.phy_state = BLE_PHY_STATE_TX;
        ++g_ble_phy_stats.tx_good;
        g_ble_phy_stats.tx_bytes += OS_MBUF_PKTHDR(txpdu)->omp_len;
        rc = BLE_ERR_SUCCESS;
    } else {
        /* Frame failed to transmit */
        ++g_ble_phy_stats.tx_fail;
        ble_phy_disable();
        rc = BLE_PHY_ERR_RADIO_STATE;
    }

    return rc;
}

/**
 * ble phy txpwr set 
 *  
 * Set the transmit output power (in dBm). 
 *  
 * NOTE: If the output power specified is within the BLE limits but outside 
 * the chip limits, we "rail" the power level so we dont exceed the min/max 
 * chip values. 
 * 
 * @param dbm Power output in dBm.
 * 
 * @return int 0: success; anything else is an error
 */
int
ble_phy_txpwr_set(int dbm)
{
    /* Check valid range */
    assert(dbm <= BLE_PHY_MAX_PWR_DBM);

    /* "Rail" power level if outside supported range */
    if (dbm > NRF52_TX_PWR_MAX_DBM) {
        dbm = NRF52_TX_PWR_MAX_DBM;
    } else {
        if (dbm < NRF52_TX_PWR_MIN_DBM) {
            dbm = NRF52_TX_PWR_MIN_DBM;
        }
    }

    NRF_RADIO->TXPOWER = dbm;
    g_ble_phy_data.phy_txpwr_dbm = dbm;

    return 0;
}

/**
 * ble phy txpwr get
 *  
 * Get the transmit power. 
 * 
 * @return int  The current PHY transmit power, in dBm
 */
int
ble_phy_txpwr_get(void)
{
    return g_ble_phy_data.phy_txpwr_dbm;
}

/**
 * ble phy setchan 
 *  
 * Sets the logical frequency of the transceiver. The input parameter is the 
 * BLE channel index (0 to 39, inclusive). The NRF52 frequency register 
 * works like this: logical frequency = 2400 + FREQ (MHz). 
 *  
 * Thus, to get a logical frequency of 2402 MHz, you would program the 
 * FREQUENCY register to 2. 
 * 
 * @param chan This is the Data Channel Index or Advertising Channel index
 * 
 * @return int 0: success; PHY error code otherwise
 */
int
ble_phy_setchan(uint8_t chan)
{
    uint8_t freq;

    assert(chan < BLE_PHY_NUM_CHANS);

    /* Check for valid channel range */
    if (chan >= BLE_PHY_NUM_CHANS) {
        return BLE_PHY_ERR_INV_PARAM;
    }

    /* If the current channel is set, just return */
    if (g_ble_phy_data.phy_chan == chan) {
        return 0;
    }

    /* Get correct nrf52 frequency */
    if (chan < BLE_PHY_NUM_DATA_CHANS) {
        if (chan < 11) {
            /* Data channel 0 starts at 2404. 0 - 10 are contiguous */
            freq = (BLE_PHY_DATA_CHAN0_FREQ_MHZ - 2400) + 
                   (BLE_PHY_CHAN_SPACING_MHZ * chan);
        } else {
            /* Data channel 11 starts at 2428. 0 - 10 are contiguous */
            freq = (BLE_PHY_DATA_CHAN0_FREQ_MHZ - 2400) + 
                   (BLE_PHY_CHAN_SPACING_MHZ * (chan + 1));
        }
    } else {
        if (chan == 37) {
            freq = BLE_PHY_CHAN_SPACING_MHZ;
        } else if (chan == 38) {
            /* This advertising channel is at 2426 MHz */
            freq = BLE_PHY_CHAN_SPACING_MHZ * 13;
        } else {
            /* This advertising channel is at 2480 MHz */
            freq = BLE_PHY_CHAN_SPACING_MHZ * 40;
        }
    }

    /* Set the frequency and the data whitening initial value */
    g_ble_phy_data.phy_chan = chan;
    NRF_RADIO->FREQUENCY = freq;
    NRF_RADIO->DATAWHITEIV = chan;

    return 0;
}

/**
 * Disable the PHY. This will do the following: 
 *  -> Turn off all phy interrupts.
 *  -> Disable internal shortcuts.
 *  -> Disable the radio.
 *  -> Sets phy state to idle.
 */
void
ble_phy_disable(void)
{
    NRF_RADIO->INTENCLR = NRF52_RADIO_IRQ_MASK_ALL;
    NRF_RADIO->SHORTS = 0;
    NRF_RADIO->TASKS_DISABLE = 1;
    g_ble_phy_data.phy_state = BLE_PHY_STATE_IDLE;
}

/**
 * Return the phy state
 * 
 * @return int The current PHY state.
 */
int 
ble_phy_state_get(void)
{
    return g_ble_phy_data.phy_state;
}

