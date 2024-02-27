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

#include <stdint.h>
#include <controller/ble_ll_utils.h>
#include <testutil/testutil.h>
#include "ble_ll_cs_drbg_priv.h"

static void
ble_ll_cs_drbg_e_test(void)
{
    uint8_t key[16] = { 0 };
    uint8_t data[16] = { 0 };
    uint8_t out[16] = { 0 };
    uint8_t expected_out[16] = { 0 };

    /* Sample data from BLUETOOTH CORE SPECIFICATION Version 6.0 | Vol 6,
     * Part C, 1.1 Encrypt Command.
     * Swap because the copy-pasted strings are in leftmost (MSO) to rightmost
     * (LSO) orientation.
     */
    swap_buf(key,
             (uint8_t[16]){ 0x4C, 0x68, 0x38, 0x41, 0x39, 0xF5, 0x74, 0xD8,
                            0x36, 0xBC, 0xF3, 0x4E, 0x9D, 0xFB, 0x01, 0xBF },
             16);

    swap_buf(data,
             (uint8_t[16]){ 0x02, 0x13, 0x24, 0x35, 0x46, 0x57, 0x68, 0x79,
                            0xac, 0xbd, 0xce, 0xdf, 0xe0, 0xf1, 0x02, 0x13 },
             16);

    swap_buf(expected_out,
             (uint8_t[16]){ 0x99, 0xad, 0x1b, 0x52, 0x26, 0xa3, 0x7e, 0x3e,
                            0x05, 0x8e, 0x3b, 0x8e, 0x27, 0xc2, 0xc6, 0x66 },
             16);

    ble_ll_cs_drbg_e(key, data, out);
    TEST_ASSERT(memcmp(out, expected_out, sizeof(out)) == 0);
}

static void
ble_ll_cs_drbg_f7_test(void)
{
    uint8_t v_s[80];
    uint8_t k[16];
    uint8_t k2[16] = { 0 };
    uint8_t x[16] = { 0 };
    uint8_t expected_k2[16] = { 0 };
    uint8_t expected_x[16] = { 0 };

    /* Sample data from BLUETOOTH CORE SPECIFICATION Version 6.0 | Vol 6,
     * Part C, 7. Deterministic random bit generator sample data.
     * Swap because the copy-pasted strings are in leftmost (MSO)
     * to rightmost (LSO) orientation.
     */
    swap_buf(k,
             (uint8_t[16]){ 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                            0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F },
             16);

    swap_buf(v_s,
             (uint8_t[80]){
                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28,
                 0x00, 0x00, 0x00, 0x20, 0xE1, 0x0B, 0xC2, 0x8A, 0x0B, 0xFD,
                 0xDF, 0xE9, 0x3E, 0x7F, 0x51, 0x86, 0xE0, 0xCA, 0x0B, 0x3B,
                 0x9F, 0xF4, 0x77, 0xC1, 0x86, 0x73, 0x84, 0x0D, 0xC9, 0x80,
                 0xDE, 0xDF, 0x98, 0x82, 0xED, 0x44, 0x64, 0xA6, 0x74, 0x96,
                 0x78, 0x68, 0xF1, 0x43, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
             80);

    swap_buf(expected_k2,
             (uint8_t[16]){ 0x8B, 0x2B, 0x06, 0xDC, 0x52, 0x2D, 0x3E, 0x0A,
                            0xF0, 0xA5, 0x0C, 0xAF, 0x48, 0x10, 0xE0, 0x35 },
             16);

    TEST_ASSERT(ble_ll_cs_drbg_f7(k, v_s, sizeof(v_s), k2) == 0);
    TEST_ASSERT(memcmp(k2, expected_k2, sizeof(k2)) == 0);

    v_s[76] = 0x01;
    swap_buf(expected_x,
             (uint8_t[16]){ 0xA3, 0x4F, 0xBE, 0x57, 0xF8, 0xF9, 0x7E, 0x34,
                            0x9D, 0x15, 0xA3, 0x76, 0x79, 0x60, 0x74, 0x64 },
             16);

    TEST_ASSERT(ble_ll_cs_drbg_f7(k, v_s, sizeof(v_s), x) == 0);
    TEST_ASSERT(memcmp(x, expected_x, sizeof(x)) == 0);
}

static void
ble_ll_cs_drbg_f8_test(void)
{
    uint8_t input_bit_string[40] = { 0 };
    uint8_t expected_sm[32] = { 0 };
    uint8_t sm[32] = { 0 };

    /* Sample data from BLUETOOTH CORE SPECIFICATION Version 6.0 | Vol 6,
     * Part C, 7. Deterministic random bit generator sample data.
     * Swap because the copy-pasted strings are in leftmost (MSO)
     * to rightmost (LSO) orientation.
     */

    /* 320-bit input bit string created from concatenated vectors
     * CS_IV || CS_IN || CS_PV
     */
    swap_buf(input_bit_string,
             (uint8_t[40]){ 0xE1, 0x0B, 0xC2, 0x8A, 0x0B, 0xFD, 0xDF, 0xE9,
                            0x3E, 0x7F, 0x51, 0x86, 0xE0, 0xCA, 0x0B, 0x3B,
                            0x9F, 0xF4, 0x77, 0xC1, 0x86, 0x73, 0x84, 0x0D,
                            0xC9, 0x80, 0xDE, 0xDF, 0x98, 0x82, 0xED, 0x44,
                            0x64, 0xA6, 0x74, 0x96, 0x78, 0x68, 0xF1, 0x43 },
             40);

    swap_buf(expected_sm,
             (uint8_t[32]){ 0xB6, 0x02, 0xB1, 0xB2, 0x8C, 0x6F, 0x0A, 0x3D,
                            0xDA, 0xE6, 0x37, 0xB4, 0x84, 0x25, 0x08, 0x7D,
                            0xDC, 0x18, 0x8C, 0x89, 0xA1, 0xB0, 0xCD, 0xFD,
                            0xA1, 0xE8, 0xFC, 0x66, 0xC9, 0x99, 0x97, 0x50 },
             32);

    TEST_ASSERT(ble_ll_cs_drbg_f8(input_bit_string, sm) == 0);
    TEST_ASSERT(memcmp(sm, expected_sm, sizeof(sm)) == 0);
}

static void
ble_ll_cs_drbg_f9_test(void)
{
    uint8_t sm[32] = { 0 };
    uint8_t k[16] = { 0 };
    uint8_t v[16] = { 0 };
    uint8_t expected_k[16] = { 0 };
    uint8_t expected_v[16] = { 0 };

    /* First call to f9 from instantiation function h9,
     * K and V vectors filled with zeros.
     *
     * Sample data from BLUETOOTH CORE SPECIFICATION Version 6.0 | Vol 6,
     * Part C, 7. Deterministic random bit generator sample data.
     * Swap because the copy-pasted strings are in leftmost (MSO)
     * to rightmost (LSO) orientation.
     */

    swap_buf(sm, (uint8_t[32]){ 0xB6, 0x02, 0xB1, 0xB2, 0x8C, 0x6F, 0x0A, 0x3D,
                                0xDA, 0xE6, 0x37, 0xB4, 0x84, 0x25, 0x08, 0x7D,
                                0xDC, 0x18, 0x8C, 0x89, 0xA1, 0xB0, 0xCD, 0xFD,
                                0xA1, 0xE8, 0xFC, 0x66, 0xC9, 0x99, 0x97, 0x50 },
             32);

    swap_buf(expected_k,
             (uint8_t[16]){ 0xEE, 0xE0, 0x4D, 0x7C, 0x76, 0x11, 0x3A, 0x5C,
                            0xEC, 0x99, 0x2A, 0xE3, 0x20, 0xC2, 0x4D, 0x27 },
             16);

    swap_buf(expected_v,
             (uint8_t[16]){ 0xDF, 0x90, 0x56, 0x47, 0xC1, 0x06, 0x6E, 0x6F,
                            0x52, 0xC0, 0x3E, 0xDF, 0xB8, 0x2B, 0x69, 0x28 },
             16);

    TEST_ASSERT(ble_ll_cs_drbg_f9(sm, k, v) == 0);
    TEST_ASSERT(memcmp(k, expected_k, sizeof(k)) == 0);
    TEST_ASSERT(memcmp(v, expected_v, sizeof(v)) == 0);
}

static void
cs_drbg_init(struct ble_ll_cs_drbg_ctx *ctx)
{
    memset(ctx, 0, sizeof(*ctx));

    /* CS_IV = CS_IV_P || CS_IV_C */
    swap_buf(ctx->iv,
             (uint8_t[16]){ 0xE1, 0x0B, 0xC2, 0x8A, 0x0B, 0xFD, 0xDF, 0xE9,
                            0x3E, 0x7F, 0x51, 0x86, 0xE0, 0xCA, 0x0B, 0x3B },
             16);

    /* CS_IN = CS_IN_P || CS_IN_C */
    swap_buf(ctx->in,
             (uint8_t[8]){ 0x9F, 0xF4, 0x77, 0xC1, 0x86, 0x73, 0x84, 0x0D }, 8);

    /* CS_PV = CS_PV_P || CS_PV_C */
    swap_buf(ctx->pv,
             (uint8_t[16]){ 0xC9, 0x80, 0xDE, 0xDF, 0x98, 0x82, 0xED, 0x44,
                            0x64, 0xA6, 0x74, 0x96, 0x78, 0x68, 0xF1, 0x43 },
             16);

    ble_ll_cs_drbg_init(ctx);
}

static void
ble_ll_cs_drbg_rand_test(void)
{
    struct ble_ll_cs_drbg_ctx ctx;
    uint8_t output[20] = { 0 };
    uint8_t expected_output[20] = { 0 };

    /* Test if subsequent drgb generator calls returns expected bit sequences. */

    cs_drbg_init(&ctx);

    /* First round - request full 128-bit batch */
    swap_buf(expected_output,
             (uint8_t[16]){ 0x79, 0x74, 0x1F, 0xD1, 0x8F, 0x57, 0x7B, 0x45,
                            0xD0, 0x9A, 0x66, 0x5A, 0x7F, 0x1F, 0x28, 0x58 },
             16);

    TEST_ASSERT(ble_ll_cs_drbg_rand(&ctx, 0x00, BLE_LL_CS_DRBG_HOP_CHAN_NON_MODE0,
                                    output, 16) == 0);
    TEST_ASSERT(memcmp(output, expected_output, 16) == 0);
}

static void
ble_ll_cs_drbg_chan_selection_3b_test(void)
{
    struct ble_ll_cs_drbg_ctx ctx;
    uint8_t filtered_channels[72] = { 0 };
    uint8_t shuffled_channels[72] = { 0 };
    uint8_t expected_shuffled_channels[19] = { 0 };

    cs_drbg_init(&ctx);

    memcpy(filtered_channels,
           (uint8_t[19]){ 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                          17, 18, 19, 20 },
           19);

    memcpy(expected_shuffled_channels,
           (uint8_t[19]){ 11, 7, 14, 18, 9, 19, 10, 8, 5, 2, 4, 15, 16, 13, 12,
                          6, 17, 20, 3 },
           19);
    assert(ble_ll_cs_drbg_shuffle_cr1(&ctx, 0x00, BLE_LL_CS_DRBG_HOP_CHAN_MODE0,
                                      filtered_channels, shuffled_channels, 19) == 0);
    assert(memcmp(shuffled_channels, expected_shuffled_channels, 19) == 0);

    memcpy(expected_shuffled_channels,
           (uint8_t[19]){ 6, 12, 5, 10, 3, 2, 18, 17, 16, 8, 11, 7, 19, 4, 13,
                          20, 9, 15, 14 },
           19);
    assert(ble_ll_cs_drbg_shuffle_cr1(&ctx, 0x03, BLE_LL_CS_DRBG_HOP_CHAN_NON_MODE0,
                                      filtered_channels, shuffled_channels, 19) == 0);
    assert(memcmp(shuffled_channels, expected_shuffled_channels, 19) == 0);
}

static void
ble_ll_cs_drbg_generate_aa_test(void)
{
    struct ble_ll_cs_drbg_ctx ctx;
    uint32_t initiator_aa;
    uint32_t reflector_aa;
    uint32_t expected_initiator_aa;
    uint32_t expected_reflector_aa;

    cs_drbg_init(&ctx);

    /* Step 0 */
    assert(ble_ll_cs_drbg_generate_aa(&ctx, 0, &initiator_aa, &reflector_aa) == 0);
    expected_initiator_aa = get_be32((uint8_t[4]){ 0x6C, 0x37, 0x6A, 0xB8 });
    expected_reflector_aa = get_be32((uint8_t[4]){ 0xF0, 0x79, 0xBC, 0x3A });
    assert(initiator_aa == expected_initiator_aa);
    assert(reflector_aa == expected_reflector_aa);

    /* Step 1 */
    assert(ble_ll_cs_drbg_generate_aa(&ctx, 1, &initiator_aa, &reflector_aa) == 0);
    expected_initiator_aa = get_be32((uint8_t[4]){ 0x01, 0x1C, 0xAE, 0x4E });
    expected_reflector_aa = get_be32((uint8_t[4]){ 0xD0, 0x6A, 0xCD, 0xDA });
    assert(initiator_aa == expected_initiator_aa);
    assert(reflector_aa == expected_reflector_aa);

    /* Step 2 */
    assert(ble_ll_cs_drbg_generate_aa(&ctx, 2, &initiator_aa, &reflector_aa) == 0);
    expected_initiator_aa = get_be32((uint8_t[4]){ 0x64, 0x06, 0x12, 0x14 });
    expected_reflector_aa = get_be32((uint8_t[4]){ 0x28, 0x94, 0x2F, 0x38 });
    assert(initiator_aa == expected_initiator_aa);
    assert(reflector_aa == expected_reflector_aa);

    /* Step 14 */
    assert(ble_ll_cs_drbg_generate_aa(&ctx, 14, &initiator_aa, &reflector_aa) == 0);
    expected_initiator_aa = get_be32((uint8_t[4]){ 0xF7, 0x21, 0x97, 0x86 });
    expected_reflector_aa = get_be32((uint8_t[4]){ 0x57, 0x17, 0x64, 0x70 });
    assert(initiator_aa == expected_initiator_aa);
    assert(reflector_aa == expected_reflector_aa);
}

static void
ble_ll_cs_drbg_rand_marker_position_test(void)
{
    uint8_t position1;
    uint8_t position2;
    struct ble_ll_cs_drbg_ctx ctx;

    cs_drbg_init(&ctx);

    /* Step 9 */
    assert(ble_ll_cs_drbg_rand_marker_position(&ctx, 9, BLE_LL_CS_RTT_32_BIT_SOUNDING_SEQUENCE,
                                               &position1, &position2) == 0);
    assert(position1 == 12 && position2 == 0xFF);

    assert(ble_ll_cs_drbg_rand_marker_position(&ctx, 9, BLE_LL_CS_RTT_32_BIT_SOUNDING_SEQUENCE,
                                               &position1, &position2) == 0);
    assert(position1 == 4 && position2 == 0xFF);

    /* Step 14 */
    assert(ble_ll_cs_drbg_rand_marker_position(&ctx, 14, BLE_LL_CS_RTT_32_BIT_SOUNDING_SEQUENCE,
                                               &position1, &position2) == 0);
    assert(position1 == 8 && position2 == 0xFF);

    assert(ble_ll_cs_drbg_rand_marker_position(&ctx, 14, BLE_LL_CS_RTT_32_BIT_SOUNDING_SEQUENCE,
                                               &position1, &position2) == 0);
    assert(position1 == 11 && position2 == 0xFF);
}

static void
ble_ll_cs_drbg_rand_marker_selection_test(void)
{
    uint8_t marker_selection;
    struct ble_ll_cs_drbg_ctx ctx;

    cs_drbg_init(&ctx);

    /* Step 9 */
    assert(ble_ll_cs_drbg_rand_marker_selection(&ctx, 0x09, &marker_selection) == 0);
    assert(marker_selection == 0x00);

    assert(ble_ll_cs_drbg_rand_marker_selection(&ctx, 0x09, &marker_selection) == 0);
    assert(marker_selection == 0x80);

    memset(ctx.t_cache, 0, sizeof(ctx.t_cache));

    /* Step 14 */
    assert(ble_ll_cs_drbg_rand_marker_selection(&ctx, 14, &marker_selection) == 0);
    assert(marker_selection == 0x80);

    assert(ble_ll_cs_drbg_rand_marker_selection(&ctx, 14, &marker_selection) == 0);
    assert(marker_selection == 0x80);
}

TEST_SUITE(ble_ll_cs_drbg_test_suite)
{
    ble_ll_cs_drbg_e_test();
    ble_ll_cs_drbg_f7_test();
    ble_ll_cs_drbg_f8_test();
    ble_ll_cs_drbg_f9_test();
    ble_ll_cs_drbg_rand_test();
    ble_ll_cs_drbg_chan_selection_3b_test();
    ble_ll_cs_drbg_generate_aa_test();
    ble_ll_cs_drbg_rand_marker_position_test();
    ble_ll_cs_drbg_rand_marker_selection_test();
}
