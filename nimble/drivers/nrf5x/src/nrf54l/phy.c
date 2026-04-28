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
#include <hal/nrf_ecb.h>
#include <controller/ble_fem.h>
#include "phy_ppi.h"

/* AAR output status — resolved IRK index written by the nRF54L AAR output
 * job list.  The hardware writes exactly 2 bytes per resolved IRK; if the
 * job-list entry length exceeds that the EasyDMA silently drops the write. */
static uint16_t g_nrf_aar_out_status;

/* CCM scatter/gather job lists and state — private to this file */
static struct sg_job_entry g_ccm_in_jl[5];
static struct sg_job_entry g_ccm_out_jl[5];
static uint16_t g_ccm_alen;
static uint16_t g_ccm_mlen;
static uint16_t g_ccm_out_alen;
static uint8_t g_ccm_out_adata;
static uint8_t g_ccm_adata_in;
static uint16_t g_ccm_out_mlen;
static uint8_t *g_ccm_in_ptr;
static uint8_t *g_ccm_out_ptr;
static uint8_t g_ccm_decrypt;
static struct nrf_ccm_data *g_ccm_data_ptr;

/* AAR scatter/gather job lists */
static struct sg_job_entry g_aar_in_jl[19];
static struct sg_job_entry g_aar_out_jl[2];

/* Create PPIB links between RADIO and PERI power domain. */
#define PPIB_RADIO_PERI(_ch, _src, _dst)                                      \
    NRF_PPIB11->SUBSCRIBE_SEND[_ch] = DPPI_CH_SUB(_src);                      \
    NRF_PPIB21->PUBLISH_RECEIVE[_ch] = DPPI_CH_PUB(_dst);                     \
    NRF_DPPIC10->CHENSET |= 1 << DPPI_CH_##_src;                              \
    NRF_DPPIC20->CHENSET |= 1 << DPPI_CH_##_dst;

/* Create PPIB links between RADIO and MCU power domain. */
#define PPIB_RADIO_MCU(_ch, _src, _dst)                                       \
    NRF_PPIB10->SUBSCRIBE_SEND[_ch] = DPPI_CH_SUB(_src);                      \
    NRF_PPIB00->PUBLISH_RECEIVE[_ch] = DPPI_CH_PUB(_dst);                     \
    NRF_DPPIC10->CHENSET |= 1 << DPPI_CH_##_src;                              \
    NRF_DPPIC00->CHENSET |= 1 << DPPI_CH_##_dst;

/* Create PPIB links between PERI and RADIO power domain. */
#define PPIB_PERI_RADIO(_ch, _src, _dst)                                      \
    NRF_PPIB21->SUBSCRIBE_SEND[_ch] = DPPI_CH_SUB(_src);                      \
    NRF_PPIB11->PUBLISH_RECEIVE[_ch] = DPPI_CH_PUB(_dst);                     \
    NRF_DPPIC20->CHENSET |= 1 << DPPI_CH_##_src;                              \
    NRF_DPPIC10->CHENSET |= 1 << DPPI_CH_##_dst;

#define PPIB_RADIO_PERI_0(_src, _dst) PPIB_RADIO_PERI(0, _src, _dst)
#define PPIB_RADIO_PERI_1(_src, _dst) PPIB_RADIO_PERI(1, _src, _dst)
#define PPIB_RADIO_PERI_2(_src, _dst) PPIB_RADIO_PERI(2, _src, _dst)
#define PPIB_RADIO_PERI_3(_src, _dst) PPIB_RADIO_PERI(3, _src, _dst)

#define PPIB_RADIO_MCU_0(_src, _dst)  PPIB_RADIO_MCU(0, _src, _dst)
#define PPIB_RADIO_MCU_1(_src, _dst)  PPIB_RADIO_MCU(1, _src, _dst)
#define PPIB_PERI_RADIO_4(_src, _dst) PPIB_PERI_RADIO(4, _src, _dst)

#if PHY_USE_DEBUG
void
phy_debug_init(void)
{
#if PHY_USE_DEBUG_1
    nrf_gpio_cfg_output(MYNEWT_VAL(BLE_PHY_DBG_TIME_TXRXEN_READY_PIN));
    nrf_gpiote_task_configure(NRF_GPIOTE20, PHY_GPIOTE_DEBUG_1,
                              MYNEWT_VAL(BLE_PHY_DBG_TIME_TXRXEN_READY_PIN),
                              NRF_GPIOTE_POLARITY_NONE, NRF_GPIOTE_INITIAL_VALUE_LOW);
    nrf_gpiote_task_enable(NRF_GPIOTE20, PHY_GPIOTE_DEBUG_1);

    PPIB_RADIO_PERI_0(TIMER0_EVENTS_COMPARE_0, GPIOTE20_TASKS_SET_0);
    NRF_GPIOTE20->SUBSCRIBE_SET[PHY_GPIOTE_DEBUG_1] =
        DPPI_CH_SUB(GPIOTE20_TASKS_SET_0);

    NRF_RADIO->PUBLISH_READY = DPPI_CH_PUB(RADIO_EVENTS_READY);
    PPIB_RADIO_PERI_1(RADIO_EVENTS_READY, GPIOTE20_TASKS_CLR_0);
    NRF_GPIOTE20->SUBSCRIBE_CLR[PHY_GPIOTE_DEBUG_1] =
        DPPI_CH_SUB(GPIOTE20_TASKS_CLR_0);
#endif

#if PHY_USE_DEBUG_2
    nrf_gpio_cfg_output(MYNEWT_VAL(BLE_PHY_DBG_TIME_ADDRESS_END_PIN));
    nrf_gpiote_task_configure(NRF_GPIOTE20, PHY_GPIOTE_DEBUG_2,
                              MYNEWT_VAL(BLE_PHY_DBG_TIME_ADDRESS_END_PIN),
                              NRF_GPIOTE_POLARITY_NONE, NRF_GPIOTE_INITIAL_VALUE_LOW);
    nrf_gpiote_task_enable(NRF_GPIOTE20, PHY_GPIOTE_DEBUG_2);

    PPIB_RADIO_PERI_2(RADIO_EVENTS_ADDRESS, GPIOTE20_TASKS_SET_1);
    NRF_GPIOTE20->SUBSCRIBE_SET[PHY_GPIOTE_DEBUG_2] =
        DPPI_CH_SUB(GPIOTE20_TASKS_SET_1);

    PPIB_RADIO_PERI_3(RADIO_EVENTS_PHYEND, GPIOTE20_TASKS_CLR_1);
    NRF_GPIOTE20->SUBSCRIBE_CLR[PHY_GPIOTE_DEBUG_2] =
        DPPI_CH_SUB(GPIOTE20_TASKS_CLR_1);
#endif
}
#endif /* PHY_USE_DEBUG */

/*
 * nRF54L KEY.VALUE byte order is reversed vs nRF52/nRF53.
 * KEY.VALUE[0] gets the last 4 bytes of the key (word-reversed +
 * byte-swapped). See datasheet Section 8.4.2.
 */
static void
phy_hw_ccm_set_key(const uint8_t *key)
{
    const uint32_t *kp = (const uint32_t *)key;

    NRF_CCM->KEY.VALUE[0] = __builtin_bswap32(kp[3]);
    NRF_CCM->KEY.VALUE[1] = __builtin_bswap32(kp[2]);
    NRF_CCM->KEY.VALUE[2] = __builtin_bswap32(kp[1]);
    NRF_CCM->KEY.VALUE[3] = __builtin_bswap32(kp[0]);
}

/*
 * Build BLE CCM nonce and write to NONCE.VALUE[0..3].
 *
 * nRF54L stores the nonce in reversed byte order vs nRF52/53.
 * Standard BLE nonce (13 bytes): counter(5) | dir<<7|counter_msb | IV(8)
 * nRF54L register layout:        IV(8) | dir<<7|counter_msb(1) | counter(4,BE)
 *
 * The reversed nonce is stored directly as LE words (no bswap).
 * See datasheet v0.8, Section 8.4.2, page 234 — NONCE.VALUE example.
 *
 * NOTE: KEY and NONCE use DIFFERENT register conventions despite both
 * being described as "reversed byte order".  KEY uses bswap32 + reversed
 * word order; NONCE uses manually reversed bytes stored as plain LE words.
 * Verified against datasheet NONCE example (IV=DEAFBABEBADCAB24, dir=1, ctr=1).
 */
static void
phy_hw_ccm_set_nonce(struct nrf_ccm_data *ccm_data)
{
    uint8_t nonce[16];
    const uint32_t *np;

    /*
     * IV bytes must be reversed — the nRF54L nonce register stores the
     * entire 13-byte BLE nonce in reversed byte order, packed as LE words.
     * The counter and dir fields are already in the correct (reversed =
     * big-endian) order below; the IV must also be byte-reversed.
     */
    nonce[0] = ccm_data->iv[7];
    nonce[1] = ccm_data->iv[6];
    nonce[2] = ccm_data->iv[5];
    nonce[3] = ccm_data->iv[4];
    nonce[4] = ccm_data->iv[3];
    nonce[5] = ccm_data->iv[2];
    nonce[6] = ccm_data->iv[1];
    nonce[7] = ccm_data->iv[0];
    /*
     * The controller already passes the BLE packet direction bit in the
     * convention used by the other NimBLE PHY drivers:
     *   - TX uses CONN_IS_CENTRAL(connsm)
     *   - RX uses !CONN_IS_CENTRAL(connsm)
     *
     * The nRF54L datasheet documents a plain nonce direction bit and the
     * working upstream drivers use the controller-provided value directly, so
     * do not apply any nRF54L-specific inversion here.
     */
    nonce[8] = (ccm_data->dir_bit & 1) << 7 | ((ccm_data->pkt_counter >> 32) & 0x7F);
    nonce[9] = (ccm_data->pkt_counter >> 24) & 0xFF;
    nonce[10] = (ccm_data->pkt_counter >> 16) & 0xFF;
    nonce[11] = (ccm_data->pkt_counter >> 8) & 0xFF;
    nonce[12] = ccm_data->pkt_counter & 0xFF;
    nonce[13] = 0;
    nonce[14] = 0;
    nonce[15] = 0;

    np = (const uint32_t *)nonce;

    NRF_CCM->NONCE.VALUE[0] = np[0];
    NRF_CCM->NONCE.VALUE[1] = np[1];
    NRF_CCM->NONCE.VALUE[2] = np[2];
    NRF_CCM->NONCE.VALUE[3] = np[3];
}

/*
 * Build CCM scatter/gather job lists for BLE packet format.
 * BLE RADIO packet in RAM: [S0=hdr][LEN][S1=0][payload...][MIC if encrypted]
 * CCM Adata = S0 (1 byte), Mdata starts at offset 3 (after S0/LEN/S1).
 */
static void
phy_hw_ccm_build_ble_job_lists(uint8_t *in_buf, uint8_t *out_buf,
                               uint16_t payload_len, uint8_t decrypt)
{
    uint16_t mdata_in_len;
    uint16_t mdata_out_len;

    if (decrypt) {
        mdata_in_len = payload_len + 4; /* ciphertext + MIC */
        mdata_out_len = payload_len;    /* plaintext only */
    } else {
        mdata_in_len = payload_len;      /* plaintext only */
        mdata_out_len = payload_len + 4; /* ciphertext + MIC */
    }

    g_ccm_alen = 1;
    /*
     * Per datasheet Fig. 45/46: input MLEN = l(m) for encrypt, l(c) for
     * decrypt.  MLEN tells the CCM how many bytes are in the MDATA field.
     */
    g_ccm_mlen = mdata_in_len;

    /*
     * BLE Data Channel PDU header mask (0xE3) zeroes NESN, SN, MD bits
     * for CCM authentication.  These bits change hop-to-hop and must not
     * be included in the MIC calculation.  The CCM ADATAMASK register
     * should do this automatically (reset value 0xE3), but we pre-mask
     * here to be safe — double-masking is idempotent.
     */
    g_ccm_adata_in = in_buf[0] & 0xE3;

    /* Input job list */
    g_ccm_in_jl[0].ptr = (uint8_t *)&g_ccm_alen;
    g_ccm_in_jl[0].attr_and_length = (CCM_ATTR_ALEN << 24) | 2;
    g_ccm_in_jl[1].ptr = (uint8_t *)&g_ccm_mlen;
    g_ccm_in_jl[1].attr_and_length = (CCM_ATTR_MLEN << 24) | 2;
    g_ccm_in_jl[2].ptr = &g_ccm_adata_in; /* pre-masked S0 byte */
    g_ccm_in_jl[2].attr_and_length = (CCM_ATTR_ADATA << 24) | 1;
    g_ccm_in_jl[3].ptr = in_buf + 3; /* payload after S0/LEN/S1 */
    g_ccm_in_jl[3].attr_and_length = (CCM_ATTR_MDATA << 24) | mdata_in_len;
    g_ccm_in_jl[4].ptr = NULL;
    g_ccm_in_jl[4].attr_and_length = 0;

    /* Output job list — must include ALEN + MLEN per datasheet Fig. 45/46 */
    g_ccm_out_jl[0].ptr = (uint8_t *)&g_ccm_out_alen;
    g_ccm_out_jl[0].attr_and_length = (CCM_ATTR_ALEN << 24) | 2;
    /*
     * MLEN output must NOT point at out_buf[1] (BLE LENGTH byte).
     * The CCM hardware writes the plaintext message length here, which
     * overwrites the BLE LENGTH field.  Redirect to a dummy so the
     * caller-set LENGTH is preserved.
     */
    g_ccm_out_jl[1].ptr = (uint8_t *)&g_ccm_out_mlen;
    g_ccm_out_jl[1].attr_and_length = (CCM_ATTR_MLEN << 24) | 2;
    /*
     * CCM ADATA output may be masked by ADATAMASK (0xE3), which zeroes
     * NESN/SN/MD bits.  Write it to a dummy so the pre-copied original
     * header in out_buf[0] is preserved (see phy_hw_ccm_post_rx_decrypt).
     */
    g_ccm_out_jl[2].ptr = &g_ccm_out_adata;
    g_ccm_out_jl[2].attr_and_length = (CCM_ATTR_ADATA << 24) | 1;
    g_ccm_out_jl[3].ptr = out_buf + 3; /* payload after header slot */
    g_ccm_out_jl[3].attr_and_length = (CCM_ATTR_MDATA << 24) | mdata_out_len;
    g_ccm_out_jl[4].ptr = NULL;
    g_ccm_out_jl[4].attr_and_length = 0;

    NRF_CCM->IN.PTR = (uint32_t)g_ccm_in_jl;
    NRF_CCM->OUT.PTR = (uint32_t)g_ccm_out_jl;
}

void
phy_hw_ccm_setup_tx(uint8_t *in_ptr, uint8_t *out_ptr, uint8_t *scratch_ptr,
                    struct nrf_ccm_data *ccm_data)
{
    g_ccm_in_ptr = in_ptr;
    g_ccm_out_ptr = out_ptr;
    g_ccm_decrypt = 0;

    /*
     * Re-arm CCM explicitly for each TX packet. On nRF54L the RX path
     * toggles ENABLE around deferred decrypt setup, so relying on stale
     * peripheral state here can leave the next encrypted TX packet using an
     * undefined configuration window.
     */
    NRF_CCM->ENABLE = CCM_ENABLE_ENABLE_Disabled;
    NRF_CCM->EVENTS_ERROR = 0;
    NRF_CCM->EVENTS_END = 0;
    NRF_CCM->MODE = (CCM_MODE_MACLEN_M4 << CCM_MODE_MACLEN_Pos) |
                    ble_phy_get_ccm_datarate();
    NRF_CCM->ENABLE = CCM_ENABLE_ENABLE_Enabled;
    phy_hw_ccm_set_key(ccm_data->key);
    phy_hw_ccm_set_nonce(ccm_data);
}

void
phy_hw_ccm_setup_rx(uint8_t *in_ptr, uint8_t *out_ptr, uint8_t *scratch_ptr,
                    struct nrf_ccm_data *ccm_data)
{
    g_ccm_in_ptr = in_ptr;
    g_ccm_out_ptr = out_ptr;
    g_ccm_decrypt = 1;
    g_ccm_data_ptr = ccm_data;

    /* Defer all CCM setup to post_rx_decrypt — writing registers
     * hundreds of µs before START causes MIC failure on nRF54L. */
}

void
phy_hw_ccm_start(void)
{
    if (g_ccm_decrypt) {
        /* RX: don't start yet — triggered post-receive in ISR */
        return;
    }

    /* TX: build job lists now (payload is filled) and start encryption.
     * Set output header — CCM MLEN/ADATA outputs go to dummies, so the
     * RADIO-visible S0/LENGTH/S1 must be set explicitly here. */
    phy_hw_ccm_build_ble_job_lists(g_ccm_in_ptr, g_ccm_out_ptr, g_ccm_in_ptr[1], 0);
    g_ccm_out_ptr[0] = g_ccm_in_ptr[0];     /* S0 (header byte) */
    g_ccm_out_ptr[1] = g_ccm_in_ptr[1] + 4; /* LENGTH = plaintext + MIC */
    g_ccm_out_ptr[2] = 0;                   /* S1 */
    __DSB();
    nrf_ccm_task_trigger(NRF_CCM, NRF_CCM_TASK_START);
}

/*
 * Post-receive CCM FastDecryption for nRF54L.
 * Called from rx_end_isr after full packet received.
 *
 * ALL CCM register setup (MODE, ENABLE, KEY, NONCE, job lists) is done
 * here, immediately before START, to avoid MIC failure caused by a
 * hundreds-of-µs gap between register writes and CCM START on nRF54L.
 *
 * BLE LENGTH field (enc_buf[1]) includes the 4-byte MIC for encrypted
 * packets, so plaintext_len = LENGTH - 4.
 */
void
phy_hw_ccm_post_rx_decrypt(uint8_t *enc_buf, uint8_t *out_buf)
{
    uint16_t ble_len = enc_buf[1];
    uint16_t plaintext_len = (ble_len >= 4) ? (ble_len - 4) : 0;

    /* Copy S0/S1 from encrypted buffer; set LENGTH to plaintext size. */
    out_buf[0] = enc_buf[0];
    out_buf[1] = plaintext_len;
    out_buf[2] = enc_buf[2];

    /* Full CCM setup in required order:
     *   ENABLE=0 → MODE → ENABLE=2 → events → KEY → NONCE →
     *   job lists → IN.PTR/OUT.PTR → DSB → START
     */
    NRF_CCM->ENABLE = CCM_ENABLE_ENABLE_Disabled;
    NRF_CCM->MODE = CCM_MODE_MODE_FastDecryption |
                    (CCM_MODE_MACLEN_M4 << CCM_MODE_MACLEN_Pos) |
                    ble_phy_get_ccm_datarate();
    NRF_CCM->ENABLE = CCM_ENABLE_ENABLE_Enabled;
    NRF_CCM->EVENTS_END = 0;
    NRF_CCM->EVENTS_ERROR = 0;

    phy_hw_ccm_set_key(g_ccm_data_ptr->key);
    phy_hw_ccm_set_nonce(g_ccm_data_ptr);

    phy_hw_ccm_build_ble_job_lists(enc_buf, out_buf, plaintext_len, 1);

    __DSB();
    nrf_ccm_task_trigger(NRF_CCM, NRF_CCM_TASK_START);
}

#if BABBLESIM
extern void tm_tick(void);
#endif

void
phy_hw_ccm_tx_wait_complete(void)
{
    /* CCM always sets EVENTS_END (success or MIC mismatch) or EVENTS_ERROR. */
    while ((NRF_CCM_EVENTS_END == 0) && (NRF_CCM->EVENTS_ERROR == 0)) {
#if BABBLESIM
        tm_tick();
#endif
    }
}

/*
 * nRF54L AAR has no STATUS register for the resolved IRK index.
 * The resolved index is written to the output job list buffer (2 bytes LE).
 */
uint32_t
phy_hw_aar_get_resolved_index(void)
{
    if (NRF_AAR->OUT.AMOUNT >= sizeof(g_nrf_aar_out_status)) {
        return g_nrf_aar_out_status;
    }
    return 0;
}

void
phy_hw_aar_irk_setup(uint32_t *irk_ptr, uint32_t *scratch_ptr)
{
    int i;
    int num_irks = g_nrf_num_irks;
    struct sg_job_entry *entry;

    /* IRK entries in the input job list define how many IRKs AAR scans. */
    entry = &g_aar_in_jl[2];
    for (i = 0; i < num_irks; i++) {
        entry->ptr = (uint8_t *)&irk_ptr[i * 4];
        entry->attr_and_length = (AAR_ATTR_IRK << 24) | 16;
        entry++;
    }
    entry->ptr = NULL;
    entry->attr_and_length = 0;

    /* Output job list stores the first resolved IRK index as a 2-byte value. */
    g_nrf_aar_out_status = UINT16_MAX;
    g_aar_out_jl[0].ptr = (uint8_t *)&g_nrf_aar_out_status;
    g_aar_out_jl[0].attr_and_length =
        (AAR_ATTR_OUTPUT << 24) | sizeof(g_nrf_aar_out_status);
    g_aar_out_jl[1].ptr = NULL;
    g_aar_out_jl[1].attr_and_length = 0;

    NRF_AAR->IN.PTR = (uint32_t)g_aar_in_jl;
    NRF_AAR->OUT.PTR = (uint32_t)g_aar_out_jl;
}

void
phy_hw_aar_addrptr_set(uint8_t *dptr)
{
    /*
     * dptr points to start of device address (6 bytes):
     *   bytes [0..2] = hash (3 bytes, LSB first)
     *   bytes [3..5] = prand (3 bytes, LSB first)
     */
    g_aar_in_jl[0].ptr = dptr;
    g_aar_in_jl[0].attr_and_length = (AAR_ATTR_HASH << 24) | 3;
    g_aar_in_jl[1].ptr = dptr + 3;
    g_aar_in_jl[1].attr_and_length = (AAR_ATTR_PRAND << 24) | 3;
}

void
phy_ppi_init(void)
{
    /* Publish events */
    NRF_TIMER0->PUBLISH_COMPARE[0] = DPPI_CH_PUB(TIMER0_EVENTS_COMPARE_0);
    NRF_TIMER0->PUBLISH_COMPARE[3] = DPPI_CH_PUB(TIMER0_EVENTS_COMPARE_3);
    NRF_RADIO->PUBLISH_PHYEND = DPPI_CH_PUB(RADIO_EVENTS_PHYEND);

    NRF_RADIO->PUBLISH_BCMATCH = DPPI_CH_PUB(RADIO_EVENTS_BCMATCH);
    NRF_RADIO->PUBLISH_ADDRESS = DPPI_CH_PUB(RADIO_EVENTS_ADDRESS);
    NRF_CPUTIME_TIMER->PUBLISH_COMPARE[0] = DPPI_CH_PUB(RTC0_EVENTS_COMPARE_0);

    /* Cross-domain RADIO -> MCU routes for CCM00 and AAR00. */
    PPIB_RADIO_MCU_0(RADIO_EVENTS_ADDRESS, CCM00_SUBSCRIBE_START);
    PPIB_RADIO_MCU_1(RADIO_EVENTS_BCMATCH, AAR00_SUBSCRIBE_START);
    /* Cross-domain PERI -> RADIO route for scheduled start trigger. */
    PPIB_PERI_RADIO_4(RTC0_EVENTS_COMPARE_0, RTC0_EVENTS_COMPARE_0);

    /* Enable channels we publish on */
    NRF_DPPIC->CHENSET = DPPI_CH_ENABLE_ALL;

    /* radio_address_to_timer0_capture1 */
    NRF_TIMER0->SUBSCRIBE_CAPTURE[1] = DPPI_CH_SUB(RADIO_EVENTS_ADDRESS);
    /* radio_end_to_timer0_capture2 */
    NRF_TIMER0->SUBSCRIBE_CAPTURE[2] = DPPI_CH_SUB(RADIO_EVENTS_PHYEND);
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

    if (dbm >= (int8_t)-28) {
        return (int8_t)-28;
    }

    if (dbm >= (int8_t)-40) {
        return (int8_t)-40;
    }

    return (int8_t)-46;
}
