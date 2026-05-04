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
#include <controller/ble_ll.h>
#include "phy_ppi.h"

/* AAR output status — resolved IRK index written by the nRF54L AAR output
 * job list. Width must match what the AAR EasyDMA writes per resolved IRK.
 * Zephyr's nRF54L driver allocates 4 bytes (radio.c:2570); the datasheet
 * (§8.3.5.15) lists PrematureOutptrEnd as the failure mode if the OUT
 * entry is shorter than the chip wants to write. UINT32_MAX is an
 * out-of-range sentinel for "no resolution". */
static uint32_t g_nrf_aar_out_status;

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
 * nRF54L KEY.VALUE storage convention (datasheet section 8.4.2):
 *   - Word index is reversed: KEY.VALUE[0] holds bytes 12..15 of the
 *     16-byte AES key, KEY.VALUE[3] holds bytes 0..3.
 *   - Within each word, bytes are big-endian: byte 12 occupies bits
 *     [31:24] of KEY.VALUE[0], byte 15 occupies bits [7:0].
 * On a little-endian Cortex-M33 this is equivalent to reading the key
 * bytes as native uint32_t and applying bswap32 to each word.
 *
 * Verified against Zephyr's nRF54L driver (radio.c:2225-2228), which
 * uses sys_get_be32(&key[12..0]) — the same byte arrangement.
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
 * Build BLE CCM nonce and write to NONCE.VALUE[0..3] (datasheet section
 * 8.4.2, v0.8 page 234 example: IV=DEAFBABEBADCAB24, dir=1,
 * packet_counter=1).
 *
 * The 13-byte BLE nonce is laid out in memory in reversed byte order:
 *   nonce[0..7]   = IV[7..0]
 *   nonce[8]      = dir << 7 | counter_msb_7bits
 *   nonce[9..12]  = counter[3..0] (big-endian)
 *   nonce[13..15] = 0 (pad to 16 bytes)
 * Then NONCE.VALUE[i] = the i-th 32-bit LE word of nonce[]. No bswap is
 * applied because the reversal is already in the byte arrangement.
 *
 * The KEY (above) uses bswap32 of native LE words; NONCE uses a manual
 * byte permutation followed by plain LE word writes. Both store the
 * same logical "reversed-byte-order" data, but the C-level expressions
 * differ — pick whichever form is cheaper given the source layout.
 *
 * Verified against Zephyr's nRF54L driver (radio.c:2230-2234):
 *   NRF_CCM->NONCE.VALUE[3] = ((uint8_t*)&counter)[0];
 *   NRF_CCM->NONCE.VALUE[2] = sys_get_be32(&counter_bytes[1]) | (dir<<7);
 *   NRF_CCM->NONCE.VALUE[1] = sys_get_be32(&iv[0]);
 *   NRF_CCM->NONCE.VALUE[0] = sys_get_be32(&iv[4]);
 * The arithmetic in this function produces an equivalent bit pattern
 * via the nonce[] intermediate.
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
    /*
     * RX (decrypt) MDATA buffer length follows Zephyr's nRF54L pattern in
     * radio.c:2260: declare PCNF1.MAXLEN + 2 (mitigates sporadic MIC failures
     * on small encrypted PDUs — Zephyr's NRF_CCM_WORKAROUND_XXXX_MDATA_EXTRA)
     * regardless of the actual PDU size. Read MAXLEN from PCNF1 live so any
     * future change to NRF_MAXLEN in ble_phy.c flows through automatically.
     * Per-PDU mdata_in_len + 2 over-reads stale buffer content for small PDUs
     * and corrupts MIC; TX (encrypt) keeps the actual length.
     */
    if (decrypt) {
        uint32_t maxlen = (NRF_RADIO->PCNF1 & RADIO_PCNF1_MAXLEN_Msk) >>
                          RADIO_PCNF1_MAXLEN_Pos;
        g_ccm_in_jl[3].attr_and_length = (CCM_ATTR_MDATA << 24) | (maxlen + 2U);
    } else {
        g_ccm_in_jl[3].attr_and_length = (CCM_ATTR_MDATA << 24) | mdata_in_len;
    }
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

/*
 * Bounded busy-wait for CCM completion. CCM at 32 MHz core encrypts a
 * worst-case 251-byte BLE PDU in well under 100 us. Set the bound to
 * ~250 us (50000 cycles at 32 MHz/2 per loop iter) to give margin while
 * keeping us comfortably inside the pre-T_IFS budget.
 */
#define PHY_CCM_TX_WAIT_MAX_LOOPS 50000

int
phy_hw_ccm_tx_wait_complete(void)
{
    uint32_t loops = 0;

    while ((NRF_CCM_EVENTS_END == 0) && (NRF_CCM->EVENTS_ERROR == 0)) {
        if (++loops > PHY_CCM_TX_WAIT_MAX_LOOPS) {
            /* Abort: disable CCM so the next packet starts clean. */
            NRF_CCM->ENABLE = CCM_ENABLE_ENABLE_Disabled;
            return -1;
        }
#if BABBLESIM
        tm_tick();
#endif
    }
    return 0;
}

/*
 * Post-RX-decrypt finish: nRF54L FastDecryption is triggered after the
 * full PDU is received, so we kick off CCM and wait, then set S0/LEN/S1
 * in the plaintext output. Returns 1 if LL should treat this as a MIC
 * failure, else 0.
 */
int
phy_hw_ccm_finish_rx_decrypt(uint8_t *enc_buf, uint8_t *out_buf, uint8_t enc_len)
{
    /* Zero-length encrypted payload: skip CCM, mirror header bytes. */
    if (enc_len == 0) {
        out_buf[0] = enc_buf[0];
        out_buf[1] = 0;
        out_buf[2] = enc_buf[2];
        return 0;
    }

    phy_hw_ccm_post_rx_decrypt(enc_buf, out_buf);

    while (NRF_CCM_EVENTS_END == 0) {
        /* spin */
    }

    /*
     * After CCM completes, set S0/LEN/S1 in the plaintext output. S0/S1
     * copied verbatim from the encrypted frame; LENGTH set to plaintext
     * size (encrypted LENGTH minus the 4-byte MIC). See audit F4 — the
     * OUT job list redirects MLEN/ADATA outputs to dummy sinks, but
     * empirical confirmation that CCM never touches out_buf[0..2] is
     * deferred, so we rewrite defensively.
     */
    out_buf[0] = enc_buf[0];
    out_buf[1] = (enc_len >= 4) ? (enc_len - 4) : 0;
    out_buf[2] = enc_buf[2];

    return ((out_buf[1] != 0) && (NRF_CCM_STATUS == 0)) ? 1 : 0;
}

/*
 * nRF54L AAR has no STATUS register for the resolved IRK index.
 * The resolved index is written to the output job list buffer (2 bytes LE).
 */
uint32_t
phy_hw_aar_get_resolved_index(void)
{
    /* Caller (ble_hw_resolv_list_match) already gates on EVENTS_RESOLVED,
     * so reaching here implies AAR wrote the index. Return the sentinel
     * if the chip wrote nothing (defensive). */
    if (NRF_AAR->OUT.AMOUNT >= sizeof(g_nrf_aar_out_status)) {
        return g_nrf_aar_out_status;
    }
    return UINT32_MAX;
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

    /* Output job list stores the resolved IRK index. Width matches what
     * AAR EasyDMA writes per entry (4 bytes per Zephyr radio.c:2570). */
    g_nrf_aar_out_status = UINT32_MAX;
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
    case -22:
    case -20:
        /* MDK maps Neg22dBm and Neg20dBm to identical register value 0x002. */
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
        /* Caller must round to a valid level via phy_txpower_round() first.
         * Hitting this branch means a logic bug, not user input — assert
         * rather than silently produce a 22+ dB radio swing. */
        BLE_LL_ASSERT(0);
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
