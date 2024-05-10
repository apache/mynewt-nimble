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

#include <assert.h>
#include <string.h>
#include <syscfg/syscfg.h>
#include <os/os.h>
#include <common/tusb_fifo.h>
#include <class/audio/audio_device.h>
#include <usb_audio.h>
#include "console/console.h"

#include <lc3.h>
#include <samplerate.h>
#include <nrf_clock.h>

#include "host/ble_gap.h"
#include "os/os_cputime.h"

#include "app_priv.h"

#define AUDIO_BUF_SIZE      1024

static uint8_t g_usb_enabled;

static void usb_data_func(struct os_event *ev);

struct chan chans[AUDIO_CHANNELS];

static struct os_event usb_data_ev = {
    .ev_cb = usb_data_func,
};

static uint32_t frame_bytes_lc3;
static uint16_t big_sdu;

static int16_t out_buf[AUDIO_BUF_SIZE];
/* Reserve twice the size of input, so we'll always have space for resampler output */
static uint8_t encoded_frame[155];
static int out_idx = 0;
#if MYNEWT_VAL(ISO_HCI_FEEDBACK)
static int samples_idx = 0;
static int16_t samples_read[AUDIO_BUF_SIZE];
/* 155 is maximum value Octets Per Codec Frame described in Table 3.5 of BAP specification */
static float samples_read_float[AUDIO_BUF_SIZE];
static float resampled_float[AUDIO_BUF_SIZE];
float resampler_in_rate = MYNEWT_VAL(USB_AUDIO_OUT_SAMPLE_RATE);
float resampler_out_rate = LC3_SAMPLING_FREQ;
float resampler_ratio;
SRC_STATE *resampler_state;
static struct ble_gap_event_listener feedback_listener;
#endif
static uint32_t pkt_counter = 0;


#if MYNEWT_VAL(ISO_HCI_FEEDBACK)
static int
ble_hs_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    struct ble_hci_vs_subev_iso_hci_feedback *feedback_pkt;
    float adjust = 0;

    if (event->type == BLE_GAP_EVENT_UNHANDLED_HCI_EVENT &&
        event->unhandled_hci.is_le_meta == false &&
        event->unhandled_hci.is_vs == true) {
        const struct ble_hci_ev_vs *ev = event->unhandled_hci.ev;

        if (ev->id == BLE_HCI_VS_SUBEV_ISO_HCI_FEEDBACK) {
            feedback_pkt = (struct ble_hci_vs_subev_iso_hci_feedback *) ev->data;
            assert(feedback_pkt->count == MYNEWT_VAL(BLE_ISO_MAX_BIGS));
            /* There is only one BIG in this sample */
            if (feedback_pkt->feedback[0].diff > 0) {
                adjust += 10;
            } else if (feedback_pkt->feedback[0].diff < 0) {
                adjust -= 10;
            }
            resampler_ratio = (resampler_out_rate + adjust) / resampler_in_rate;
        }
    }

    return 0;
}

static void
resample(void)
{
    static int resampled_len;
    int samples_consumed;
    int samples_left;
    SRC_DATA sd;
    int resample_avail = ARRAY_SIZE(out_buf) - out_idx;
    int rc;

    src_short_to_float_array(samples_read, samples_read_float, samples_idx);

    sd.data_in = samples_read_float;
    sd.data_out = resampled_float;
    sd.input_frames = samples_idx / AUDIO_CHANNELS;
    sd.output_frames = resample_avail / AUDIO_CHANNELS;
    sd.end_of_input = 0;
    sd.input_frames_used = 0;
    sd.output_frames_gen = 0;
    sd.src_ratio = resampler_ratio;

    rc = src_process(resampler_state, &sd);
    assert(rc == 0);

    resampled_len = sd.output_frames_gen * AUDIO_CHANNELS;

    assert(resampled_len <= resample_avail);

    src_float_to_short_array(resampled_float,
                             &out_buf[out_idx],
                             resampled_len);

    out_idx += resampled_len;

    samples_consumed = sd.input_frames_used * AUDIO_CHANNELS;
    samples_left = samples_idx - samples_consumed;
    memmove(samples_read, &samples_read[samples_consumed], samples_left);
    samples_idx -= samples_consumed;
}
#endif

static void
usb_data_func(struct os_event *ev)
{
    int ch_idx;
    unsigned int num_bytes;
    unsigned int num_samples;
    unsigned int num_frames;
    int read;
    int skip;

    if (!g_usb_enabled) {
        tud_audio_clear_ep_out_ff();
        return;
    }

    while ((num_bytes = tud_audio_available()) > 0) {
        num_frames = num_bytes / (AUDIO_CHANNELS * AUDIO_SAMPLE_SIZE);
        num_samples = num_frames * AUDIO_CHANNELS;
        if (out_idx + num_samples >= ARRAY_SIZE(out_buf)) {
            num_samples = ARRAY_SIZE(out_buf) - out_idx - 1;
        }
        num_frames = num_samples / AUDIO_CHANNELS;
        num_bytes = num_frames * AUDIO_SAMPLE_SIZE * AUDIO_CHANNELS;

#if MYNEWT_VAL(ISO_HCI_FEEDBACK)
        assert(samples_idx + num_samples < ARRAY_SIZE(samples_read));
        read = tud_audio_read(&samples_read[samples_idx], num_bytes);
        samples_idx += num_samples;
        resample();
#else
        assert(out_idx + num_samples < ARRAY_SIZE(out_buf));
        read = tud_audio_read(&out_buf[out_idx], num_bytes);
        out_idx += num_samples;
#endif
        assert(read == num_bytes);

        assert((out_idx & 0x80000000) == 0);
        if (out_idx / AUDIO_CHANNELS >= LC3_FPDT) {
            pkt_counter++;
            skip = 0;

            for (ch_idx = 0; ch_idx < AUDIO_CHANNELS; ch_idx++) {
                if (chans[ch_idx].handle == 0) {
                    skip = 1;
                    continue;
                }
            }

            if (!skip) {
                memset(encoded_frame, 0, sizeof(encoded_frame));
                lc3_encode(chans[0].encoder, LC3_PCM_FORMAT_S16, out_buf + 0,
                           AUDIO_CHANNELS, (int) frame_bytes_lc3,
                           encoded_frame);
                if (AUDIO_CHANNELS == 2) {
                    if (MYNEWT_VAL(BIG_NUM_BIS) == 1) {
                        lc3_encode(chans[0].encoder, LC3_PCM_FORMAT_S16,
                                   out_buf + 1, AUDIO_CHANNELS,
                                   (int) frame_bytes_lc3, encoded_frame + big_sdu / 2);
                        ble_iso_tx(chans[0].handle, encoded_frame, big_sdu);
                    } else {
                        ble_iso_tx(chans[0].handle, encoded_frame, big_sdu);
                        memset(encoded_frame, 0, sizeof(encoded_frame));
                        lc3_encode(chans[1].encoder, LC3_PCM_FORMAT_S16, out_buf + 1,
                                   AUDIO_CHANNELS, (int) frame_bytes_lc3,
                                   encoded_frame);
                        ble_iso_tx(chans[1].handle, encoded_frame, big_sdu);
                    }
                } else {
                    ble_iso_tx(chans[0].handle, encoded_frame, big_sdu);
                }

                if (out_idx / AUDIO_CHANNELS >= LC3_FPDT) {
                    out_idx -= LC3_FPDT * AUDIO_CHANNELS;
                    memmove(out_buf, &out_buf[LC3_FPDT * AUDIO_CHANNELS],
                            out_idx * AUDIO_SAMPLE_SIZE);
                } else {
                    out_idx = 0;
                }
            }
        }
    }
}

bool
tud_audio_rx_done_post_read_cb(uint8_t rhport, uint16_t n_bytes_received,
                               uint8_t func_id, uint8_t ep_out,
                               uint8_t cur_alt_setting)
{
    (void)rhport;
    (void)n_bytes_received;
    (void)func_id;
    (void)ep_out;
    (void)cur_alt_setting;

    if (!usb_data_ev.ev_queued) {
        os_eventq_put(os_eventq_dflt_get(), &usb_data_ev);
    }

    return true;
}

void
audio_usb_init(void)
{
    /* Need to reference those explicitly, so they are always pulled by linker
     * instead of weak symbols in tinyusb.
     */
    (void)tud_audio_rx_done_post_read_cb;

    usb_desc_sample_rate_set(AUDIO_PCM_SAMPLE_RATE);

    assert(LC3_FPDT == lc3_frame_samples(LC3_FRAME_DURATION,
                                         AUDIO_PCM_SAMPLE_RATE));

    unsigned esize = lc3_encoder_size(LC3_FRAME_DURATION,
                                      AUDIO_PCM_SAMPLE_RATE);
    for (int i = 0; i < AUDIO_CHANNELS; i++) {
        chans[i].encoder = calloc(1, esize);
        lc3_setup_encoder(LC3_FRAME_DURATION, LC3_SAMPLING_FREQ,
                          AUDIO_PCM_SAMPLE_RATE, chans[i].encoder);
    }

    g_usb_enabled = 1;

    frame_bytes_lc3 = lc3_frame_bytes(LC3_FRAME_DURATION, LC3_BITRATE);
    big_sdu = frame_bytes_lc3 *
              (1 + ((AUDIO_CHANNELS == 2) && (BIG_NUM_BIS == 1)));

#if MYNEWT_VAL(ISO_HCI_FEEDBACK)
    int rc;

    assert(resampler_state == NULL);
    resampler_state = src_new(SRC_SINC_FASTEST, AUDIO_CHANNELS, NULL);
    assert(resampler_state != NULL);

    rc = ble_gap_event_listener_register(&feedback_listener,
                                         ble_hs_gap_event_handler, NULL);
    assert(rc == 0);

    resampler_ratio = resampler_out_rate / resampler_in_rate;
#endif
}
