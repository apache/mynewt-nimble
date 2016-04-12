/**
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
#include <assert.h>
#include "os/os.h"
#include "bsp/cmsis_nvic.h"
#include "nimble/ble.h"
#include "controller/ble_phy.h"
#include "controller/ble_ll.h"
#include "mcu/nrf52_bitfields.h"

/* XXX: 4) Make sure RF is higher priority interrupt than schedule */

/* To disable all radio interrupts */
#define NRF_RADIO_IRQ_MASK_ALL  (0x34FF)

/*
 * We configure the nrf52 with a 1 byte S0 field, 8 bit length field, and
 * zero bit S1 field. The preamble is 8 bits long.
 */
#define NRF_LFLEN_BITS          (8)
#define NRF_S0_LEN              (1)

/* Maximum length of frames */
#define NRF_MAXLEN              (255)
#define NRF_BALEN               (3)     /* For base address of 3 bytes */

/* Maximum tx power */
#define NRF_TX_PWR_MAX_DBM      (4)
#define NRF_TX_PWR_MIN_DBM      (-40)

/* BLE PHY data structure */
struct ble_phy_obj
{
    uint8_t phy_stats_initialized;
    int8_t  phy_txpwr_dbm;
    uint8_t phy_chan;
    uint8_t phy_state;
    uint8_t phy_transition;
    uint8_t phy_rx_started;
    uint32_t phy_access_address;
    struct os_mbuf *rxpdu;
    void *txend_arg;
    ble_phy_tx_end_func txend_cb;
};
struct ble_phy_obj g_ble_phy_data;

/* Global transmit/receive buffer */
static uint32_t g_ble_phy_txrx_buf[(BLE_PHY_MAX_PDU_LEN + 3) / 4];

/* Statistics */
STATS_SECT_START(ble_phy_stats)
    STATS_SECT_ENTRY(phy_isrs)
    STATS_SECT_ENTRY(tx_good)
    STATS_SECT_ENTRY(tx_fail)
    STATS_SECT_ENTRY(tx_late)
    STATS_SECT_ENTRY(tx_bytes)
    STATS_SECT_ENTRY(rx_starts)
    STATS_SECT_ENTRY(rx_aborts)
    STATS_SECT_ENTRY(rx_valid)
    STATS_SECT_ENTRY(rx_crc_err)
    STATS_SECT_ENTRY(no_bufs)
    STATS_SECT_ENTRY(radio_state_errs)
    STATS_SECT_ENTRY(rx_hw_err)
    STATS_SECT_ENTRY(tx_hw_err)
STATS_SECT_END
STATS_SECT_DECL(ble_phy_stats) ble_phy_stats;

STATS_NAME_START(ble_phy_stats)
    STATS_NAME(ble_phy_stats, phy_isrs)
    STATS_NAME(ble_phy_stats, tx_good)
    STATS_NAME(ble_phy_stats, tx_fail)
    STATS_NAME(ble_phy_stats, tx_late)
    STATS_NAME(ble_phy_stats, tx_bytes)
    STATS_NAME(ble_phy_stats, rx_starts)
    STATS_NAME(ble_phy_stats, rx_aborts)
    STATS_NAME(ble_phy_stats, rx_valid)
    STATS_NAME(ble_phy_stats, rx_crc_err)
    STATS_NAME(ble_phy_stats, no_bufs)
    STATS_NAME(ble_phy_stats, radio_state_errs)
    STATS_NAME(ble_phy_stats, rx_hw_err)
    STATS_NAME(ble_phy_stats, tx_hw_err)
STATS_NAME_END(ble_phy_stats)

/*
 * NOTE:
 * Tested the following to see what would happen:
 *  -> NVIC has radio irq enabled (interrupt # 1, mask 0x2).
 *  -> Set up nrf52 to receive. Clear ADDRESS event register.
 *  -> Enable ADDRESS interrupt on nrf52 by writing to INTENSET.
 *  -> Enable RX.
 *  -> Disable interrupts globally using OS_ENTER_CRITICAL().
 *  -> Wait until a packet is received and the ADDRESS event occurs.
 *  -> Call ble_phy_disable().
 *
 *  At this point I wanted to see the state of the cortex NVIC. The IRQ
 *  pending bit was TRUE for the radio interrupt (as expected) as we never
 *  serviced the radio interrupt (interrupts were disabled).
 *
 *  What was unexpected was this: without clearing the pending IRQ in the NVIC,
 *  when radio interrupts were re-enabled (address event bit in INTENSET set to
 *  1) and the radio ADDRESS event register read 1 (it was never cleared after
 *  the first address event), the radio did not enter the ISR! I would have
 *  expected that if the following were true, an interrupt would occur:
 *      -> NVIC ISER bit set to TRUE
 *      -> NVIC ISPR bit reads TRUE, meaning interrupt is pending.
 *      -> Radio peripheral interrupts are enabled for some event (or events).
 *      -> Corresponding event register(s) in radio peripheral read 1.
 *
 *  Not sure what the end result of all this is. We will clear the pending
 *  bit in the NVIC just to be sure when we disable the PHY.
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
        m = os_msys_get_pkthdr(BLE_MBUF_PAYLOAD_SIZE, sizeof(struct ble_mbuf_hdr));
        if (!m) {
            STATS_INC(ble_phy_stats, no_bufs);
        } else {
            /*
             * NOTE: we add two bytes to the data pointer as we will prepend
             * two bytes if we hand this received pdu up to host.
             */
            m->om_data += 2;
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
            }
        }
    }
}

static void
ble_phy_isr(void)
{
    int rc;
    uint8_t transition;
    uint8_t crcok;
    uint32_t irq_en;
    uint32_t state;
    uint32_t wfr_time;
    struct os_mbuf *rxpdu;
    struct ble_mbuf_hdr *ble_hdr;

    /* Read irq register to determine which interrupts are enabled */
    irq_en = NRF_RADIO->INTENCLR;

    /* Check for disabled event. This only happens for transmits now */
    if ((irq_en & RADIO_INTENCLR_DISABLED_Msk) && NRF_RADIO->EVENTS_DISABLED) {
        /* Better be in TX state! */
        assert(g_ble_phy_data.phy_state == BLE_PHY_STATE_TX);

        ble_ll_log(BLE_LL_LOG_ID_PHY_TXEND, (g_ble_phy_txrx_buf[0] >> 8) & 0xFF,
                   0, NRF_TIMER0->CC[2]);

        /* Clear events and clear interrupt on disabled event */
        NRF_RADIO->EVENTS_DISABLED = 0;
        NRF_RADIO->INTENCLR = RADIO_INTENCLR_DISABLED_Msk;
        NRF_RADIO->EVENTS_END = 0;
        state = NRF_RADIO->SHORTS;

        transition = g_ble_phy_data.phy_transition;
        if (transition == BLE_PHY_TRANSITION_TX_RX) {
            /* Clear the rx started flag */
            g_ble_phy_data.phy_rx_started = 0;

            /* Packet pointer needs to be reset. */
            if (g_ble_phy_data.rxpdu != NULL) {
                NRF_RADIO->PACKETPTR = (uint32_t)g_ble_phy_data.rxpdu->om_data;

                /* I want to know when 1st byte received (after address) */
                NRF_RADIO->BCC = 8; /* in bits */
                NRF_RADIO->EVENTS_ADDRESS = 0;
                NRF_RADIO->EVENTS_DEVMATCH = 0;
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
                STATS_INC(ble_phy_stats, no_bufs);
                ble_phy_disable();
            }

            /*
             * Enable the wait for response timer. Note that cc #2 on
             * timer 0 contains the transmit end time
             */
            wfr_time = NRF_TIMER0->CC[2];
            wfr_time += cputime_usecs_to_ticks(BLE_LL_WFR_USECS);
            ble_ll_wfr_enable(wfr_time);
        } else {
            /* Better not be going from rx to tx! */
            assert(transition == BLE_PHY_TRANSITION_NONE);
        }

        /* Call transmit end callback */
        if (g_ble_phy_data.txend_cb) {
            g_ble_phy_data.txend_cb(g_ble_phy_data.txend_arg);
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
                NRF_RADIO->INTENCLR = NRF_RADIO_IRQ_MASK_ALL;
                NRF_RADIO->SHORTS = 0;
                goto phy_isr_exit;
            }
        }

        /* Initialize flags, channel and state in ble header at rx start */
        ble_hdr = BLE_MBUF_HDR_PTR(g_ble_phy_data.rxpdu);
        ble_hdr->rxinfo.flags = ble_ll_state_get();
        ble_hdr->rxinfo.channel = g_ble_phy_data.phy_chan;
        ble_hdr->rxinfo.handle = 0;

        /* Call Link Layer receive start function */
        rc = ble_ll_rx_start(g_ble_phy_data.rxpdu, g_ble_phy_data.phy_chan);
        if (rc >= 0) {
            g_ble_phy_data.phy_rx_started = 1;
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
            STATS_INC(ble_phy_stats, rx_aborts);
        }

        /* Count rx starts */
        STATS_INC(ble_phy_stats, rx_starts);
    }

    /* Receive packet end (we dont enable this for transmit) */
    if ((irq_en & RADIO_INTENCLR_END_Msk) && NRF_RADIO->EVENTS_END) {
        /* Clear events and clear interrupt */
        NRF_RADIO->EVENTS_END = 0;
        NRF_RADIO->INTENCLR = RADIO_INTENCLR_END_Msk;

        /* Set RSSI and CRC status flag in header */
        ble_hdr = BLE_MBUF_HDR_PTR(g_ble_phy_data.rxpdu);
        assert(NRF_RADIO->EVENTS_RSSIEND != 0);
        ble_hdr->rxinfo.rssi = -1 * NRF_RADIO->RSSISAMPLE;
        ble_hdr->end_cputime = NRF_TIMER0->CC[2];

        /* Count PHY crc errors and valid packets */
        crcok = (uint8_t)NRF_RADIO->CRCSTATUS;
        if (!crcok) {
            STATS_INC(ble_phy_stats, rx_crc_err);
        } else {
            STATS_INC(ble_phy_stats, rx_valid);
            ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_CRC_OK;
        }

        /* Call Link Layer receive payload function */
        rxpdu = g_ble_phy_data.rxpdu;
        g_ble_phy_data.rxpdu = NULL;
        rc = ble_ll_rx_end(rxpdu, ble_hdr);
        if (rc < 0) {
            /* Disable the PHY. */
            ble_phy_disable();
        }
    }

phy_isr_exit:
    /* Ensures IRQ is cleared */
    state = NRF_RADIO->SHORTS;

    /* Count # of interrupts */
    STATS_INC(ble_phy_stats, phy_isrs);
}

/**
 * ble phy init
 *
 * Initialize the PHY.
 *
 * @return int 0: success; PHY error code otherwise
 */
int
ble_phy_init(void)
{
    int rc;
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
    NRF_RADIO->INTENCLR = NRF_RADIO_IRQ_MASK_ALL;

    /* Set configuration registers */
    NRF_RADIO->MODE = RADIO_MODE_MODE_Ble_1Mbit;
    NRF_RADIO->PCNF0 = (NRF_LFLEN_BITS << RADIO_PCNF0_LFLEN_Pos)    |
                       (NRF_S0_LEN << RADIO_PCNF0_S0LEN_Pos)        |
                       (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos);
    NRF_RADIO->PCNF1 = NRF_MAXLEN |
                       (RADIO_PCNF1_ENDIAN_Little <<  RADIO_PCNF1_ENDIAN_Pos) |
                       (NRF_BALEN << RADIO_PCNF1_BALEN_Pos) |
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

    /*
     * Enable the pre-programmed PPI to capture the time when a receive
     * or transmit ends
     */
    NRF_PPI->CHENSET = PPI_CHEN_CH27_Msk;

    /* Set isr in vector table and enable interrupt */
    NVIC_SetPriority(RADIO_IRQn, 0);
    NVIC_SetVector(RADIO_IRQn, (uint32_t)ble_phy_isr);
    NVIC_EnableIRQ(RADIO_IRQn);

    /* Register phy statistics */
    if (!g_ble_phy_data.phy_stats_initialized) {
        rc = stats_init_and_reg(STATS_HDR(ble_phy_stats),
                                STATS_SIZE_INIT_PARMS(ble_phy_stats,
                                                      STATS_SIZE_32),
                                STATS_NAME_INIT_PARMS(ble_phy_stats),
                                "ble_phy");
        assert(rc == 0);

        g_ble_phy_data.phy_stats_initialized  = 1;
    }

    return 0;
}

int
ble_phy_rx(void)
{
    /* Check radio state */
    nrf52_wait_disabled();
    if (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled) {
        ble_phy_disable();
        STATS_INC(ble_phy_stats, radio_state_errs);
        return BLE_PHY_ERR_RADIO_STATE;
    }

    /* If no pdu, get one */
    if (ble_phy_rxpdu_get() == NULL) {
        return BLE_PHY_ERR_NO_BUFS;
    }

    /* Set packet pointer */
    NRF_RADIO->PACKETPTR = (uint32_t)g_ble_phy_data.rxpdu->om_data;

    /* Make sure all interrupts are disabled */
    NRF_RADIO->INTENCLR = NRF_RADIO_IRQ_MASK_ALL;

    /* Clear events prior to enabling receive */
    NRF_RADIO->EVENTS_END = 0;
    NRF_RADIO->EVENTS_ADDRESS = 0;
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->EVENTS_BCMATCH = 0;
    NRF_RADIO->EVENTS_RSSIEND = 0;
    NRF_RADIO->EVENTS_DEVMATCH = 0;

    /* I want to know when 1st byte received (after address) */
    NRF_RADIO->BCC = 8; /* in bits */
    NRF_RADIO->SHORTS = RADIO_SHORTS_END_DISABLE_Msk |
                        RADIO_SHORTS_READY_START_Msk |
                        RADIO_SHORTS_ADDRESS_BCSTART_Msk |
                        RADIO_SHORTS_ADDRESS_RSSISTART_Msk |
                        RADIO_SHORTS_DISABLED_RSSISTOP_Msk;

    NRF_RADIO->INTENSET = RADIO_INTENSET_ADDRESS_Msk;

    /* Reset the rx started flag. Used for the wait for response */
    g_ble_phy_data.phy_rx_started = 0;

    /* Start the receive task in the radio */
    NRF_RADIO->TASKS_RXEN = 1;

    g_ble_phy_data.phy_state = BLE_PHY_STATE_RX;

    return 0;
}

void
ble_phy_set_txend_cb(ble_phy_tx_end_func txend_cb, void *arg)
{
    /* Set transmit end callback and arg */
    g_ble_phy_data.txend_cb = txend_cb;
    g_ble_phy_data.txend_arg = arg;
}

int
ble_phy_tx(struct os_mbuf *txpdu, uint8_t beg_trans, uint8_t end_trans)
{
    int rc;
    uint8_t *dptr;
    uint32_t state;
    uint32_t shortcuts;
    struct ble_mbuf_hdr *ble_hdr;

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
        STATS_INC(ble_phy_stats, radio_state_errs);
        return BLE_PHY_ERR_RADIO_STATE;
    }

    /* Write LL header first */
    ble_hdr = BLE_MBUF_HDR_PTR(txpdu);
    dptr = (uint8_t *)&g_ble_phy_txrx_buf[0];
    dptr[0] = ble_hdr->txinfo.hdr_byte;
    dptr[1] = ble_hdr->txinfo.pyld_len;
    dptr += 2;

    /* Set radio transmit data pointer */
    NRF_RADIO->PACKETPTR = (uint32_t)&g_ble_phy_txrx_buf[0];

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
    }
    NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
    NRF_RADIO->SHORTS = shortcuts;

    /* Trigger transmit if our state was disabled */
    if (state == RADIO_STATE_STATE_Disabled) {
        NRF_RADIO->TASKS_TXEN = 1;
    }

    /* Set the PHY transition */
    g_ble_phy_data.phy_transition = end_trans;

    /* Read back radio state. If in TXRU, we are fine */
    state = NRF_RADIO->STATE;
    if (state == RADIO_STATE_STATE_TxRu) {
        /* Copy data from mbuf into transmit buffer */
        os_mbuf_copydata(txpdu, ble_hdr->txinfo.offset,
                         ble_hdr->txinfo.pyld_len, dptr);

        /* Set phy state to transmitting and count packet statistics */
        g_ble_phy_data.phy_state = BLE_PHY_STATE_TX;
        STATS_INC(ble_phy_stats, tx_good);
        STATS_INCN(ble_phy_stats, tx_bytes,
                   ble_hdr->txinfo.pyld_len + BLE_LL_PDU_HDR_LEN);
        rc = BLE_ERR_SUCCESS;
    } else {
        if (state == RADIO_STATE_STATE_Tx) {
            STATS_INC(ble_phy_stats, tx_late);
        } else {
            STATS_INC(ble_phy_stats, tx_fail);
        }

        /* Frame failed to transmit */
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
    if (dbm > NRF_TX_PWR_MAX_DBM) {
        dbm = NRF_TX_PWR_MAX_DBM;
    } else {
        if (dbm < NRF_TX_PWR_MIN_DBM) {
            dbm = NRF_TX_PWR_MIN_DBM;
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
ble_phy_setchan(uint8_t chan, uint32_t access_addr, uint32_t crcinit)
{
    uint8_t freq;
    uint32_t prefix;

    assert(chan < BLE_PHY_NUM_CHANS);

    /* Check for valid channel range */
    if (chan >= BLE_PHY_NUM_CHANS) {
        return BLE_PHY_ERR_INV_PARAM;
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

        /* Set current access address */
        g_ble_phy_data.phy_access_address = access_addr;

        /* Configure logical address 1 and crcinit */
        prefix = NRF_RADIO->PREFIX0;
        prefix &= 0xffff00ff;
        prefix |= ((access_addr >> 24) & 0xFF) << 8;
        NRF_RADIO->BASE1 = (access_addr << 8) & 0xFFFFFF00;
        NRF_RADIO->PREFIX0 = prefix;
        NRF_RADIO->TXADDRESS = 1;
        NRF_RADIO->RXADDRESSES = (1 << 1);
        NRF_RADIO->CRCINIT = crcinit;
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

        /* Logical adddress 0 preconfigured */
        NRF_RADIO->TXADDRESS = 0;
        NRF_RADIO->RXADDRESSES = (1 << 0);
        NRF_RADIO->CRCINIT = BLE_LL_CRCINIT_ADV;

        /* Set current access address */
        g_ble_phy_data.phy_access_address = BLE_ACCESS_ADDR_ADV;
    }

    /* Set the frequency and the data whitening initial value */
    g_ble_phy_data.phy_chan = chan;
    NRF_RADIO->FREQUENCY = freq;
    NRF_RADIO->DATAWHITEIV = chan;

    ble_ll_log(BLE_LL_LOG_ID_PHY_SETCHAN, chan, freq, access_addr);

    return 0;
}

/**
 * Disable the PHY. This will do the following:
 *  -> Turn off all phy interrupts.
 *  -> Disable internal shortcuts.
 *  -> Disable the radio.
 *  -> Sets phy state to idle.
 *  -> Clears any pending irqs in the NVIC. Might not be necessary but we do
 *  it as a precaution.
 */
void
ble_phy_disable(void)
{
    ble_ll_log(BLE_LL_LOG_ID_PHY_DISABLE, g_ble_phy_data.phy_state, 0, 0);

    NRF_RADIO->INTENCLR = NRF_RADIO_IRQ_MASK_ALL;
    NRF_RADIO->SHORTS = 0;
    NRF_RADIO->TASKS_DISABLE = 1;
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    g_ble_phy_data.phy_state = BLE_PHY_STATE_IDLE;
}

/* Gets the current access address */
uint32_t ble_phy_access_addr_get(void)
{
    return g_ble_phy_data.phy_access_address;
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

/**
 * Called to see if a reception has started
 *
 * @return int
 */
int
ble_phy_rx_started(void)
{
    return g_ble_phy_data.phy_rx_started;
}

