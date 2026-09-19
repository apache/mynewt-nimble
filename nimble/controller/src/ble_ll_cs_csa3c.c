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

#include <syscfg/syscfg.h>
#if MYNEWT_VAL(BLE_LL_CHANNEL_SOUNDING) && MYNEWT_VAL(BLE_LL_CHANNEL_SOUNDING_CSA3C)
#include <stdint.h>
#include <string.h>
#include "controller/ble_ll.h"
#include "ble_ll_cs_priv.h"

static uint8_t hash3c_parameters[][4] = {
    /* [CSChannelJump] = {seq1StartCh, seq2StartCh, maxRepsAllowed, saltRate} */
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {1, 76, 1, 2},
    {77, 0, 1, 2},
    {78, 0, 2, 2},
    {78, 0, 2, 2},
    {76, 1, 3, 2},
    {74, 1, 3, 2},
    {76, 0, 3, 2}
};

#define MAX_CHANNEL_ID (78)
#define MAX_CHANNEL_COUNT (79)
#define CS3C_N_INITIAL_SALT (9)
#define CS3C_N_FINAL_SALT (4)

typedef struct {
    /* 80-bits mask */
    uint8_t bits[10];
} ch_bitmap_t;

typedef struct {
    uint8_t *data;
    uint16_t len;
    uint16_t max_len;
} seq_t;

static uint8_t temp_buf[BLE_LL_CS_CSA3C_CHAN_COUNT_MAX];
static uint8_t shape_buf[158];
static uint8_t unused_buf[MAX_CHANNEL_COUNT];
static uint8_t salted_shape_buf[BLE_LL_CS_CSA3C_CHAN_COUNT_MAX];
static uint8_t fae_salt_buf[80];
static uint8_t m_salt_buf[80];
static seq_t temp = { .data = temp_buf,
                      .len = 0,
                      .max_len = sizeof(temp_buf)
                    };
static seq_t shape_seq = { .data = shape_buf,
                           .len = 0,
                           .max_len = sizeof(shape_buf)
                         };
static seq_t unused_seq = { .data = unused_buf,
                            .len = 0,
                            .max_len = sizeof(unused_buf)
                          };
static seq_t salted_shape_seq = { .data = salted_shape_buf,
                                  .len = 0,
                                  .max_len = sizeof(salted_shape_buf)
                                };
static seq_t fae_salt_seq = { .data = fae_salt_buf,
                              .len = 0,
                              .max_len = sizeof(fae_salt_buf)
                            };
static seq_t m_salt_seq = { .data = m_salt_buf,
                            .len = 0,
                            .max_len = sizeof(m_salt_buf)
                          };

static inline void
seq_reset(seq_t *s)
{
    memset(s->data, 0, s->max_len);
    s->len = 0;
}

static inline void
seq_append(seq_t *s, uint8_t ch)
{
    BLE_LL_ASSERT(s->len < s->max_len);
    s->data[s->len++] = ch;
}

static inline void
bitmap_clear(ch_bitmap_t *bm)
{
    memset(bm->bits, 0, sizeof(bm->bits));
}

static inline void
bitmap_set(ch_bitmap_t *bm, uint8_t ch)
{
    bm->bits[ch >> 3] |= (1u << (ch & 7));
}

static inline uint8_t
bitmap_get(const ch_bitmap_t *bm, uint8_t ch)
{
    return (bm->bits[ch >> 3] & (1u << (ch & 7))) != 0;
}

static inline uint8_t
is_chan_allowed(const uint8_t *chm, uint8_t ch)
{
    return (chm[ch >> 3] & (1u << (ch & 7))) != 0;
}

static void
ble_ll_cs_csa3c_filter_and_shuffle(struct ble_ll_cs_drbg_ctx *drbg_ctx, const seq_t *salted,
                                   const uint8_t *filter_mask, seq_t *channels_out,
                                   seq_t *temp, uint16_t step_count, uint8_t csa3c_iter)
{
    seq_t *filtered_salted = temp;
    uint16_t len;
    uint16_t n_steps_in_block;
    uint16_t n_blocks_to_shuffle;
    uint16_t start;
    uint16_t end;
    uint16_t i;
    uint8_t ch;

    seq_reset(filtered_salted);

    /* Reset output only on first iteration */
    if (csa3c_iter == 0) {
        seq_reset(channels_out);
    }

    /* Filter */
    for (i = 0; i < salted->len; i++) {
        ch = salted->data[i];

        if (is_chan_allowed(filter_mask, ch)) {
            seq_append(filtered_salted, ch);
        }
    }

    /* Empty after filtering */
    if (filtered_salted->len == 0) {
        return;
    }

    /* Block setup */
    n_steps_in_block = filtered_salted->len / 4;
    if (n_steps_in_block < 10) {
        n_steps_in_block = 10;
    }

    n_blocks_to_shuffle = filtered_salted->len / n_steps_in_block;
    if (n_blocks_to_shuffle < 1) {
        n_blocks_to_shuffle = 1;
    }

    /* Shuffle per block */
    for (i = 0; i < n_blocks_to_shuffle; i++) {
        start = i * n_steps_in_block;

        /* Last block takes remaining elements */
        if (i < (n_blocks_to_shuffle - 1)) {
            end = start + n_steps_in_block;
        } else {
            end = filtered_salted->len;
        }

        len = end - start;

        BLE_LL_ASSERT(channels_out->len + len < channels_out->max_len);

        ble_ll_cs_drbg_shuffle_cr1(drbg_ctx, step_count, BLE_LL_CS_DRBG_HOP_CHAN_NON_MODE0,
                                   filtered_salted->data + start,
                                   channels_out->data + channels_out->len, len);

        channels_out->len += len;
    }
}

static void
build_used_bitmap(const seq_t *shape_seq, ch_bitmap_t *used)
{
    bitmap_clear(used);

    for (uint8_t i = 0; i < shape_seq->len; i++) {
        bitmap_set(used, shape_seq->data[i]);
    }
}

static void
append_unused_range(seq_t *dst, const ch_bitmap_t *used, uint8_t start, uint8_t end)
{
    for (uint8_t ch = start; ch <= end; ch++) {
        if (!bitmap_get(used, ch)) {
            seq_append(dst, ch);
        }
    }
}

static void
append_all_range(seq_t *dst, uint8_t start, uint8_t end)
{
    for (uint8_t ch = start; ch <= end; ch++) {
        seq_append(dst, ch);
    }
}

static inline uint8_t
get_j(uint8_t ch)
{
    return (ch / 20) + 1;
}

static inline uint8_t
use_first_pool(uint8_t j, uint8_t is_x_pattern)
{
    if (is_x_pattern) {
        return (j == 1 || j == 4);
    } else {
        return (j == 1 || j == 2);
    }
}

static int
ble_ll_cs_csa3c_salt_channel_insertion(struct ble_ll_cs_drbg_ctx *drbg_ctx, const seq_t *shape_seq,
                                       const seq_t *fae_salt, const seq_t *m_salt, seq_t *salted,
                                       uint16_t step_count, uint8_t salt_rate, uint8_t csa3c_iter,
                                       uint8_t num_repetitions, uint8_t is_x_pattern)
{
    int rc;
    uint16_t fe_i = 0;
    uint16_t m_i  = 0;
    uint16_t i;
    uint8_t j;
    uint8_t ch;
    uint8_t n_initial_salt;
    uint8_t n_final_salt;
    uint8_t use_first;
    uint16_t deficit;

    seq_reset(salted);

    /* Initial salt (only first iteration) */
    if (csa3c_iter == 0) {
        rc = ble_ll_cs_drbg_rand_hr1(drbg_ctx, step_count, BLE_LL_CS_DRBG_HOP_CHAN_NON_MODE0,
                                     CS3C_N_INITIAL_SALT + 1, &n_initial_salt);

        if (rc) {
            return -1;
        }

        use_first = 1;

        for (i = 0; i < n_initial_salt; i++) {
            if (use_first && fe_i < fae_salt->len) {
                seq_append(salted, fae_salt->data[fe_i++]);
            } else if (m_i < m_salt->len) {
                seq_append(salted, m_salt->data[m_i++]);
            }

            use_first = !use_first;
        }
    }

    /* Main shape + salt mixing */
    for (i = 0; i < shape_seq->len; i++) {
        /* Salt step */
        if (salt_rate > 0 && (i % salt_rate) == 0) {
            ch = shape_seq->data[i];
            j  = get_j(ch);

            use_first = use_first_pool(j, is_x_pattern);

            if (use_first) {
                if (fe_i < fae_salt->len) {
                    seq_append(salted, fae_salt->data[fe_i++]);
                }
            } else {
                if (m_i < m_salt->len) {
                    seq_append(salted, m_salt->data[m_i++]);
                }
            }
        }

        /* Shape step */
        seq_append(salted, shape_seq->data[i]);
    }

    /* Final salt (only last iteration) */
    if (csa3c_iter == (num_repetitions - 1)) {
        rc = ble_ll_cs_drbg_rand_hr1(drbg_ctx, step_count, BLE_LL_CS_DRBG_HOP_CHAN_NON_MODE0,
                                     CS3C_N_FINAL_SALT + 1, &n_final_salt);
        if (rc) {
            return -1;
        }

        use_first = 1;

        for (i = 0; i < n_final_salt; i++) {
            if (use_first && fe_i < fae_salt->len) {
                seq_append(salted, fae_salt->data[fe_i++]);
            } else if (m_i < m_salt->len) {
                seq_append(salted, m_salt->data[m_i++]);
            }

            use_first = !use_first;
        }

        /* Imbalance fix */
        if (fe_i > m_i) {
            deficit = fe_i - m_i;

            for (i = 0; i < deficit && m_i < m_salt->len; i++) {
                seq_append(salted, m_salt->data[m_i++]);
            }
        } else if (m_i > fe_i) {
            deficit = m_i - fe_i;

            for (i = 0; i < deficit && fe_i < fae_salt->len; i++) {
                seq_append(salted, fae_salt->data[fe_i++]);
            }
        }

        /* Any remaining entries discarded */
    }

    return 0;
}

static void
ble_ll_cs_csa3c_build_salt_sequences(struct ble_ll_cs_drbg_ctx *drbg_ctx, const seq_t *shape_seq,
                                     seq_t *fae_salt, seq_t *m_salt, seq_t *temp, uint16_t step_count,
                                     uint8_t is_x_pattern)
{
    ch_bitmap_t used;

    build_used_bitmap(shape_seq, &used);

    seq_reset(fae_salt);
    seq_reset(m_salt);

    /* Build FirstAndEndUnusedChSeq */

    seq_reset(temp);

    if (is_x_pattern) {
        append_unused_range(temp, &used, 20, 39);
        append_unused_range(temp, &used, 40, 59);
    } else {
        append_unused_range(temp, &used, 40, 59);
        append_unused_range(temp, &used, 60, 78);
    }

    ble_ll_cs_drbg_shuffle_cr1(drbg_ctx, step_count, BLE_LL_CS_DRBG_HOP_CHAN_NON_MODE0, temp->data,
                               fae_salt->data, temp->len);

    fae_salt->len = temp->len;

    /* Build FirstAndEndAllChSeq */

    seq_reset(temp);

    if (is_x_pattern) {
        append_all_range(temp, 20, 39);
        append_all_range(temp, 40, 59);
    } else {
        append_all_range(temp, 40, 59);
        append_all_range(temp, 60, 78);
    }

    ble_ll_cs_drbg_shuffle_cr1(drbg_ctx, step_count, BLE_LL_CS_DRBG_HOP_CHAN_NON_MODE0, temp->data,
                               fae_salt->data + fae_salt->len, temp->len);

    fae_salt->len += temp->len;

    /* Build MiddleUnusedChSeq */

    seq_reset(temp);

    if (is_x_pattern) {
        append_unused_range(temp, &used, 0, 19);
        append_unused_range(temp, &used, 60, 78);
    } else {
        append_unused_range(temp, &used, 0, 19);
        append_unused_range(temp, &used, 20, 39);
    }

    ble_ll_cs_drbg_shuffle_cr1(drbg_ctx, step_count, BLE_LL_CS_DRBG_HOP_CHAN_NON_MODE0, temp->data,
                               m_salt->data, temp->len);

    m_salt->len = temp->len;

    /* Build MiddleAllChSeq */

    seq_reset(temp);

    if (is_x_pattern) {
        append_all_range(temp, 0, 19);
        append_all_range(temp, 60, 78);
    } else {
        append_all_range(temp, 0, 19);
        append_all_range(temp, 20, 39);
    }

    ble_ll_cs_drbg_shuffle_cr1(drbg_ctx, step_count, BLE_LL_CS_DRBG_HOP_CHAN_NON_MODE0, temp->data,
                               m_salt->data + m_salt->len, temp->len);

    m_salt->len += temp->len;
}

static void
ble_ll_cs_csa3c_generate_unused_ch_seq(const seq_t *shape_seq, seq_t *unused_seq)
{
    uint16_t i;
    uint8_t ch;
    uint8_t used;

    seq_reset(unused_seq);

    for (ch = 0; ch <= MAX_CHANNEL_ID; ch++) {
        used = 0;

        for (i = 0; i < shape_seq->len; i++) {
            if (shape_seq->data[i] == ch) {
                used = 1;
                break;
            }
        }

        if (!used) {
            seq_append(unused_seq, ch);
        }
    }
}

static int
ble_ll_cs_csa3c_generate_shape(struct ble_ll_cs_drbg_ctx *drbg_ctx, seq_t *shape_seq,
                               uint8_t *start_jitter, uint16_t step_count,
                               uint8_t seq1_start_ch, uint8_t seq2_start_ch,
                               uint8_t chan_jump, uint8_t csa3c_iter, uint8_t is_x_pattern)
{
    int rc;
    uint8_t offset;
    int8_t s1, s2;
    uint8_t s1_done = 0;
    uint8_t s2_done = 0;
    int8_t inc;
    int8_t rising;
    int8_t falling;

    BLE_LL_ASSERT(chan_jump >= 2);

    seq_reset(shape_seq);

    if (csa3c_iter == 0) {
        rc = ble_ll_cs_drbg_rand_hr1(drbg_ctx, step_count,
                                     BLE_LL_CS_DRBG_HOP_CHAN_NON_MODE0,
                                     chan_jump, start_jitter);
        if (rc) {
            return -1;
        }
    }

    offset = (csa3c_iter + *start_jitter) % chan_jump;

    s1 = seq1_start_ch + offset;
    s2 = seq2_start_ch + offset;

    if (is_x_pattern) {
        inc = (seq1_start_ch < seq2_start_ch) ? chan_jump : -chan_jump;

        while (!s1_done || !s2_done) {
            if (!s1_done) {
                if (s1 >= 0 && s1 < MAX_CHANNEL_ID) {
                    seq_append(shape_seq, s1);
                }

                s1 += inc;

                if ((inc > 0 && s1 >= MAX_CHANNEL_ID) ||
                    (inc < 0 && s1 < 0)) {
                    s1_done = true;
                }
            }

            if (!s2_done) {
                if (s2 >= 0 && s2 < MAX_CHANNEL_ID) {
                    seq_append(shape_seq, s2);
                }

                s2 -= inc;

                if ((inc > 0 && s2 < 0) ||
                    (inc < 0 && s2 >= MAX_CHANNEL_ID)) {
                    s2_done = true;
                }
            }
        }
    } else {
        if (s1 < s2) {
            rising = s1;
            falling = s2;
        } else {
            rising = s2;
            falling = s1;
        }

        while (rising <= MAX_CHANNEL_ID) {
            if (rising >= 0) {
                seq_append(shape_seq, rising);
            }
            rising += chan_jump;
        }

        while (falling >= 0) {
            if (falling <= MAX_CHANNEL_ID) {
                seq_append(shape_seq, falling);
            }
            falling -= chan_jump;
        }
    }

    return 0;
}

int
ble_ll_cs_csa3c(struct ble_ll_cs_drbg_ctx *drbg_ctx, uint8_t *channels_out,
                const uint8_t *filter_mask, uint8_t *start_jitter,
                uint8_t *csa3c_iter, uint16_t step_count, uint8_t shape,
                uint8_t chan_jump, uint8_t num_repetitions)
{
    uint8_t *params = hash3c_parameters[chan_jump];
    seq_t final_seq = { .data = channels_out,
                        .len = 0,
                        .max_len = BLE_LL_CS_CSA3C_CHAN_COUNT_MAX
                      };
    uint8_t is_x_pattern = (shape == BLE_LL_CS_CH3C_SHAPE_X);
    uint8_t seq1_start_ch = params[0];
    uint8_t seq2_start_ch = params[1];
//    uint8_t max_reps_allowed = params[2];
    uint8_t salt_rate = params[3];

    ble_ll_cs_csa3c_generate_shape(drbg_ctx, &shape_seq, start_jitter, step_count,
                                   seq1_start_ch, seq2_start_ch, chan_jump, *csa3c_iter,
                                   is_x_pattern);

    ble_ll_cs_csa3c_generate_unused_ch_seq(&shape_seq, &unused_seq);

    ble_ll_cs_csa3c_build_salt_sequences(drbg_ctx, &shape_seq, &fae_salt_seq, &m_salt_seq, &temp,
                                         step_count, is_x_pattern);

    ble_ll_cs_csa3c_salt_channel_insertion(drbg_ctx, &shape_seq, &fae_salt_seq, &m_salt_seq,
                                           &salted_shape_seq, step_count, salt_rate, *csa3c_iter,
                                           num_repetitions, is_x_pattern);

    ble_ll_cs_csa3c_filter_and_shuffle(drbg_ctx, &salted_shape_seq, filter_mask, &final_seq,
                                       &temp, step_count, *csa3c_iter);

    (*csa3c_iter)++;

    return 0;
}

#endif /* BLE_LL_CHANNEL_SOUNDING && BLE_LL_CHANNEL_SOUNDING_CSA3C */
