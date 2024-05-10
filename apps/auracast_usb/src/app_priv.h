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

#ifndef H_APP_PRIV_
#define H_APP_PRIV_

#include <syscfg/syscfg.h>

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#define AUDIO_CHANNELS          MYNEWT_VAL(AURACAST_CHAN_NUM)
#define AUDIO_SAMPLE_SIZE       sizeof(int16_t)
#if MYNEWT_VAL(ISO_HCI_FEEDBACK)
#define AUDIO_PCM_SAMPLE_RATE   LC3_SAMPLING_FREQ
#else
#define AUDIO_PCM_SAMPLE_RATE   MYNEWT_VAL(USB_AUDIO_OUT_SAMPLE_RATE)
#endif

#define LC3_FRAME_DURATION      (MYNEWT_VAL(LC3_FRAME_DURATION))
#define LC3_SAMPLING_FREQ       (MYNEWT_VAL(LC3_SAMPLING_FREQ))
#define LC3_BITRATE             (MYNEWT_VAL(LC3_BITRATE))
#define LC3_FPDT                (AUDIO_PCM_SAMPLE_RATE * LC3_FRAME_DURATION / 1000000)
#define BIG_NUM_BIS             (MIN(AUDIO_CHANNELS, MYNEWT_VAL(BIG_NUM_BIS)))

struct chan {
    void *encoder;
    uint16_t handle;
};

extern struct chan chans[AUDIO_CHANNELS];
#endif /* H_APP_PRIV_ */
