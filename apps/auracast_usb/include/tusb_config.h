/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#include "syscfg/syscfg.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * COMMON CONFIGURATION
 */

#include <tusb_hw.h>

/* defined by compiler flags for flexibility */
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#define CFG_TUSB_RHPORT0_MODE       OPT_MODE_DEVICE

#define CFG_TUSB_OS                 OPT_OS_MYNEWT
#define CFG_TUSB_DEBUG              1

#define CFG_TUD_EP_MAX              9

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

/**
 * DEVICE CONFIGURATION
 */
#define CFG_TUD_ENDPOINT0_SIZE   MYNEWT_VAL(USBD_EP0_SIZE)

/* ------------- CLASS ------------- */
#define CFG_TUD_CDC              MYNEWT_VAL(USBD_CDC)
#define CFG_TUD_HID              MYNEWT_VAL(USBD_HID)
#define CFG_TUD_MSC              0
#define CFG_TUD_MIDI             0
#define CFG_TUD_VENDOR           0
#define CFG_TUD_USBTMC           0
#define CFG_TUD_DFU_RT           0
#define CFG_TUD_ECM_RNDIS        0
#define CFG_TUD_BTH              MYNEWT_VAL(USBD_BTH)
#define CFG_TUD_AUDIO_IN         MYNEWT_VAL(USBD_AUDIO_IN)
#define CFG_TUD_AUDIO_OUT        MYNEWT_VAL(USBD_AUDIO_OUT)
#define CFG_TUD_AUDIO_IN_OUT     MYNEWT_VAL(USBD_AUDIO_IN_OUT)
#define CFG_TUD_AUDIO            (MYNEWT_VAL(USBD_AUDIO_IN) || MYNEWT_VAL(USBD_AUDIO_OUT) || \
                                  MYNEWT_VAL(USBD_AUDIO_IN_OUT))

/* Audio format type */
#define CFG_TUD_AUDIO_FORMAT_TYPE_TX        AUDIO_FORMAT_TYPE_I
#define CFG_TUD_AUDIO_FORMAT_TYPE_RX        AUDIO_FORMAT_TYPE_I

/* Audio format type I specifications */
#define CFG_TUD_AUDIO_FORMAT_TYPE_I_TX      AUDIO_DATA_FORMAT_TYPE_I_PCM
#define CFG_TUD_AUDIO_FORMAT_TYPE_I_RX      AUDIO_DATA_FORMAT_TYPE_I_PCM
#define CFG_TUD_AUDIO_N_CHANNELS_TX         2
#define CFG_TUD_AUDIO_N_BYTES_PER_SAMPLE_TX 2
#define CFG_TUD_AUDIO_N_CHANNELS_RX         MYNEWT_VAL(USB_AUDIO_OUT_CHANNELS)
#define CFG_TUD_AUDIO_N_BYTES_PER_SAMPLE_RX 2
#define CFG_TUD_AUDIO_RX_ITEMSIZE           2
#define CFG_TUD_AUDIO_SAMPLE_RATE           MYNEWT_VAL(USB_AUDIO_OUT_SAMPLE_RATE)
#define SAMPLES_PER_PACKET                  ((((CFG_TUD_AUDIO_SAMPLE_RATE) -1) / 1000) + 1)

/* EP and buffer size - for isochronous EP´s, the buffer and EP size are equal (different sizes would not make sense) */
#define CFG_TUD_AUDIO_EPSIZE_IN           (CFG_TUD_AUDIO_IN * SAMPLES_PER_PACKET * \
                                           (CFG_TUD_AUDIO_N_BYTES_PER_SAMPLE_TX) *(CFG_TUD_AUDIO_N_CHANNELS_TX))                                          /* 48 Samples (48 kHz) x 2 Bytes/Sample x n Channels */
#define CFG_TUD_AUDIO_TX_FIFO_COUNT       (CFG_TUD_AUDIO_IN * 1)
#define CFG_TUD_AUDIO_TX_FIFO_SIZE        (CFG_TUD_AUDIO_IN ? ((CFG_TUD_AUDIO_EPSIZE_IN)) : 0)

/* EP and buffer size - for isochronous EP´s, the buffer and EP size are equal (different sizes would not make sense) */
#define CFG_TUD_AUDIO_ENABLE_EP_OUT       1
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP  0
#define CFG_TUD_AUDIO_EPSIZE_OUT          (CFG_TUD_AUDIO_OUT * \
                                           ((SAMPLES_PER_PACKET + 1) * \
                                            (CFG_TUD_AUDIO_N_BYTES_PER_SAMPLE_RX) *(CFG_TUD_AUDIO_N_CHANNELS_RX)))                                                 /* N Samples (N kHz) x 2 Bytes/Sample x n Channels */
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX CFG_TUD_AUDIO_EPSIZE_OUT
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ (CFG_TUD_AUDIO_EPSIZE_OUT * 40)
#define CFG_TUD_AUDIO_RX_FIFO_COUNT       (CFG_TUD_AUDIO_OUT * 1)
#define CFG_TUD_AUDIO_RX_FIFO_SIZE        (CFG_TUD_AUDIO_OUT ? (3 * \
                                                                (CFG_TUD_AUDIO_EPSIZE_OUT / \
                                                                 CFG_TUD_AUDIO_RX_FIFO_COUNT)) : 0)

/* Number of Standard AS Interface Descriptors (4.9.1) defined per audio function - this is required to be able to remember the current alternate settings of these interfaces - We restrict us here to have a constant number for all audio functions (which means this has to be the maximum number of AS interfaces an audio function has and a second audio function with less AS interfaces just wastes a few bytes) */
#define CFG_TUD_AUDIO_N_AS_INT            1

/* Size of control request buffer */
#define CFG_TUD_AUDIO_CTRL_BUF_SIZE       64

/* Minimal number for alternative interfaces that is recognized by Windows as Bluetooth radio controller */
#define CFG_TUD_BTH_ISO_ALT_COUNT 2

/*  CDC FIFO size of TX and RX */
#define CFG_TUD_CDC_RX_BUFSIZE   64
#define CFG_TUD_CDC_TX_BUFSIZE   64

/* HID buffer size Should be sufficient to hold ID (if any) + Data */
#define CFG_TUD_HID_BUFSIZE      16

#define TUD_AUDIO_SPEAKER_MONO_DESC_LEN (TUD_AUDIO_DESC_IAD_LEN \
                                         + TUD_AUDIO_DESC_STD_AC_LEN \
                                         + TUD_AUDIO_DESC_CS_AC_LEN \
                                         + TUD_AUDIO_DESC_CLK_SRC_LEN \
                                         + TUD_AUDIO_DESC_INPUT_TERM_LEN \
                                         + TUD_AUDIO_DESC_OUTPUT_TERM_LEN \
                                         + TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL_LEN \
                                         + TUD_AUDIO_DESC_STD_AS_INT_LEN \
                                         + TUD_AUDIO_DESC_STD_AS_INT_LEN \
                                         + TUD_AUDIO_DESC_CS_AS_INT_LEN \
                                         + TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN \
                                         + TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN \
                                         + TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN)

#define TUD_AUDIO_SPEAKER_MONO_DESCRIPTOR(_itfnum, _stridx, _nBytesPerSample, _nBitsUsedPerSample, _epout, _epsize) \
    /* Standard Interface Association Descriptor (IAD) */ \
    TUD_AUDIO_DESC_IAD(/*_firstitfs*/ _itfnum, /*_nitfs*/ 0x02, /*_stridx*/ 0x00), \
    /* Standard AC Interface Descriptor(4.7.1) */ \
    TUD_AUDIO_DESC_STD_AC(/*_itfnum*/ _itfnum, /*_nEPs*/ 0x00, /*_stridx*/ _stridx), \
    /* Class-Specific AC Interface Header Descriptor(4.7.2) */ \
    TUD_AUDIO_DESC_CS_AC(/*_bcdADC*/ 0x0200, /*_category*/ AUDIO_FUNC_DESKTOP_SPEAKER, \
                         /*_totallen*/ TUD_AUDIO_DESC_CLK_SRC_LEN+TUD_AUDIO_DESC_INPUT_TERM_LEN+ \
    TUD_AUDIO_DESC_OUTPUT_TERM_LEN+TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL_LEN, \
                         /*_ctrl*/ AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS), \
    /* Clock Source Descriptor(4.7.2.1) */ \
    TUD_AUDIO_DESC_CLK_SRC(/*_clkid*/ 0x04, /*_attr*/ AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK, \
                           /*_ctrl*/ (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS), /*_assocTerm*/ 0x00, \
                           /*_stridx*/ 0x00), \
    /* Input Terminal Descriptor(4.7.2.4) */ \
    TUD_AUDIO_DESC_INPUT_TERM(/*_termid*/ 0x01, /*_termtype*/ AUDIO_TERM_TYPE_USB_STREAMING, /*_assocTerm*/ 0x00, \
                              /*_clkid*/ 0x04, /*_nchannelslogical*/ 0x01, \
                              /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, /*_idxchannelnames*/ 0x00, \
                              /*_ctrl*/ 0 * (AUDIO_CTRL_R << AUDIO_IN_TERM_CTRL_CONNECTOR_POS), /*_stridx*/ 0x00), \
    /* Output Terminal Descriptor(4.7.2.5) */ \
    TUD_AUDIO_DESC_OUTPUT_TERM(/*_termid*/ 0x03, /*_termtype*/ AUDIO_TERM_TYPE_OUT_DESKTOP_SPEAKER, \
                               /*_assocTerm*/ 0x00, /*_srcid*/ 0x02, /*_clkid*/ 0x04, /*_ctrl*/ 0x0000, \
                               /*_stridx*/ 0x00), \
    /* Feature Unit Descriptor(4.7.2.8) */ \
    TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL(/*_unitid*/ 0x02, /*_srcid*/ 0x01, \
                                            /*_ctrlch0master*/ 0 * (AUDIO_CTRL_RW << \
                                                                    AUDIO_FEATURE_UNIT_CTRL_MUTE_POS | \
                                                                    AUDIO_CTRL_RW << \
                                                                    AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS), \
                                            /*_ctrlch1*/ 0 * (AUDIO_CTRL_RW << \
                                                              AUDIO_FEATURE_UNIT_CTRL_MUTE_POS | AUDIO_CTRL_RW << \
                                                              AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS), \
                                            /*_stridx*/ 0x00), \
    /* Standard AS Interface Descriptor(4.9.1) */ \
    /* Interface 1, Alternate 0 - default alternate setting with 0 bandwidth */ \
    TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum) + 1), /*_altset*/ 0x00, /*_nEPs*/ 0x00, \
                              /*_stridx*/ 0x00), \
    /* Standard AS Interface Descriptor(4.9.1) */ \
    /* Interface 1, Alternate 1 - alternate interface for data streaming */ \
    TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum) + 1), /*_altset*/ 0x01, /*_nEPs*/ 0x01, \
                              /*_stridx*/ 0x00), \
    /* Class-Specific AS Interface Descriptor(4.9.2) */ \
    TUD_AUDIO_DESC_CS_AS_INT(/*_termid*/ 0x01, /*_ctrl*/ AUDIO_CTRL_NONE, /*_formattype*/ AUDIO_FORMAT_TYPE_I, \
                             /*_formats*/ AUDIO_DATA_FORMAT_TYPE_I_PCM, /*_nchannelsphysical*/ 0x01, \
                             /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, /*_stridx*/ 0x00), \
    /* Type I Format Type Descriptor(2.3.1.6 - Audio Formats) */ \
    TUD_AUDIO_DESC_TYPE_I_FORMAT(_nBytesPerSample, _nBitsUsedPerSample), \
    /* Standard AS Isochronous Audio Data Endpoint Descriptor(4.10.1.1) */ \
    TUD_AUDIO_DESC_STD_AS_ISO_EP(/*_ep*/ _epout, \
                                 /*_attr*/ (TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_SYNCHRONOUS | \
                                            TUSB_ISO_EP_ATT_DATA), /*_maxEPsize*/ _epsize, \
                                 /*_interval*/ (CFG_TUSB_RHPORT0_MODE & OPT_MODE_HIGH_SPEED) ? 0x04 : 0x01), \
    /* Class-Specific AS Isochronous Audio Data Endpoint Descriptor(4.10.1.2) */ \
    TUD_AUDIO_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, \
                                /*_ctrl*/ AUDIO_CTRL_NONE, \
                                /*_lockdelayunit*/ AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, \
                                /*_lockdelay*/ 0x0000) \

/* XXXXX
 * 3 iso endpoint alternative for 16,32,48 kHz does not work on windows, works on Linux, on MAC reported freq is 48kHz and mac sends data for 16kHz
 * MAC does not change clock selection like Linux does
 */

#define TUD_AUDIO_SPEAKER3_DESCRIPTOR(_itfnum, _stridx, _nBytesPerSample, _nBitsUsedPerSample, _epout, _epsize1, \
                                      _epsize2, _epsize3) \
    /* Standard Interface Association Descriptor (IAD) */ \
    TUD_AUDIO_DESC_IAD(/*_firstitfs*/ _itfnum, /*_nitfs*/ 0x02, /*_stridx*/ 0x00), \
    /* Standard AC Interface Descriptor(4.7.1) */ \
    TUD_AUDIO_DESC_STD_AC(/*_itfnum*/ _itfnum, /*_nEPs*/ 0x00, /*_stridx*/ _stridx), \
    /* Class-Specific AC Interface Header Descriptor(4.7.2) */ \
    TUD_AUDIO_DESC_CS_AC(/*_bcdADC*/ 0x0200, /*_category*/ AUDIO_FUNC_DESKTOP_SPEAKER, \
                         /*_totallen*/ TUD_AUDIO_DESC_CLK_SRC_LEN+TUD_AUDIO_DESC_INPUT_TERM_LEN+ \
    TUD_AUDIO_DESC_OUTPUT_TERM_LEN+0*TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL_LEN, \
                         /*_ctrl*/ AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS), \
    /* Clock Source Descriptor(4.7.2.1) */ \
    TUD_AUDIO_DESC_CLK_SRC(/*_clkid*/ 0x04, /*_attr*/ AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK, \
                           /*_ctrl*/ (AUDIO_CTRL_RW << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS), /*_assocTerm*/ 0x00, \
                           /*_stridx*/ 0x00), \
    /* Input Terminal Descriptor(4.7.2.4) */ \
    TUD_AUDIO_DESC_INPUT_TERM(/*_termid*/ 0x01, /*_termtype*/ AUDIO_TERM_TYPE_USB_STREAMING, /*_assocTerm*/ 0x00, \
                              /*_clkid*/ 0x04, /*_nchannelslogical*/ 0x02, \
                              /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT, \
                              /*_idxchannelnames*/ 0x00, \
                              /*_ctrl*/ 0 * (AUDIO_CTRL_R << AUDIO_IN_TERM_CTRL_CONNECTOR_POS), /*_stridx*/ 0x00), \
    /* Output Terminal Descriptor(4.7.2.5) */ \
    TUD_AUDIO_DESC_OUTPUT_TERM(/*_termid*/ 0x03, /*_termtype*/ AUDIO_TERM_TYPE_OUT_DESKTOP_SPEAKER, \
                               /*_assocTerm*/ 0x00, /*_srcid*/ 0x01, /*_clkid*/ 0x04, /*_ctrl*/ 0x0000, \
                               /*_stridx*/ 0x00), \
    /* Standard AS Interface Descriptor(4.9.1) */ \
    /* Interface 1, Alternate 0 - default alternate setting with 0 bandwidth */ \
    TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum) + 1), /*_altset*/ 0x00, /*_nEPs*/ 0x00, \
                              /*_stridx*/ 0x00), \
    /* Standard AS Interface Descriptor(4.9.1) */ \
                                                                                                                                 \
    /* Interface 1, Alternate 1 - alternate interface for data streaming */ \
    TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum) + 1), /*_altset*/ 0x01, /*_nEPs*/ 0x01, \
                              /*_stridx*/ 0x00), \
    /* Class-Specific AS Interface Descriptor(4.9.2) */ \
    TUD_AUDIO_DESC_CS_AS_INT(/*_termid*/ 0x01, /*_ctrl*/ AUDIO_CTRL_NONE, /*_formattype*/ AUDIO_FORMAT_TYPE_I, \
                             /*_formats*/ AUDIO_DATA_FORMAT_TYPE_I_PCM, /*_nchannelsphysical*/ 0x02, \
                             /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT, \
                             /*_stridx*/ 0x00), \
    /* Type I Format Type Descriptor(2.3.1.6 - Audio Formats) */ \
    TUD_AUDIO_DESC_TYPE_I_FORMAT(_nBytesPerSample, _nBitsUsedPerSample), \
    /* Standard AS Isochronous Audio Data Endpoint Descriptor(4.10.1.1) */ \
    TUD_AUDIO_DESC_STD_AS_ISO_EP(/*_ep*/ _epout, \
                                 /*_attr*/ (TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_SYNCHRONOUS | \
                                            TUSB_ISO_EP_ATT_DATA), /*_maxEPsize*/ _epsize1, \
                                 /*_interval*/ (CFG_TUSB_RHPORT0_MODE & OPT_MODE_HIGH_SPEED) ? 0x04 : 0x01), \
    /* Class-Specific AS Isochronous Audio Data Endpoint Descriptor(4.10.1.2) */ \
    TUD_AUDIO_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, \
                                /*_ctrl*/ AUDIO_CTRL_NONE, \
                                /*_lockdelayunit*/ AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, \
                                /*_lockdelay*/ 0x0000), \
                                                                                                                                 \
    /* Interface 1, Alternate 2 - alternate interface for data streaming */ \
    TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum) + 1), /*_altset*/ 0x02, /*_nEPs*/ 0x01, \
                              /*_stridx*/ 0x00), \
    /* Class-Specific AS Interface Descriptor(4.9.2) */ \
    TUD_AUDIO_DESC_CS_AS_INT(/*_termid*/ 0x01, /*_ctrl*/ AUDIO_CTRL_NONE, /*_formattype*/ AUDIO_FORMAT_TYPE_I, \
                             /*_formats*/ AUDIO_DATA_FORMAT_TYPE_I_PCM, /*_nchannelsphysical*/ 0x02, \
                             /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT, \
                             /*_stridx*/ 0x00), \
    /* Type I Format Type Descriptor(2.3.1.6 - Audio Formats) */ \
    TUD_AUDIO_DESC_TYPE_I_FORMAT(_nBytesPerSample, _nBitsUsedPerSample), \
    /* Standard AS Isochronous Audio Data Endpoint Descriptor(4.10.1.1) */ \
    TUD_AUDIO_DESC_STD_AS_ISO_EP(/*_ep*/ _epout, \
                                 /*_attr*/ (TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_SYNCHRONOUS | \
                                            TUSB_ISO_EP_ATT_DATA), /*_maxEPsize*/ _epsize2, \
                                 /*_interval*/ (CFG_TUSB_RHPORT0_MODE & OPT_MODE_HIGH_SPEED) ? 0x04 : 0x01), \
    /* Class-Specific AS Isochronous Audio Data Endpoint Descriptor(4.10.1.2) */ \
    TUD_AUDIO_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, \
                                /*_ctrl*/ AUDIO_CTRL_NONE, \
                                /*_lockdelayunit*/ AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, \
                                /*_lockdelay*/ 0x0000), \
                                                                                                                                 \
    /* Interface 1, Alternate 1 - alternate interface for data streaming */ \
    TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum) + 1), /*_altset*/ 0x03, /*_nEPs*/ 0x01, \
                              /*_stridx*/ 0x00), \
    /* Class-Specific AS Interface Descriptor(4.9.2) */ \
    TUD_AUDIO_DESC_CS_AS_INT(/*_termid*/ 0x01, /*_ctrl*/ AUDIO_CTRL_NONE, /*_formattype*/ AUDIO_FORMAT_TYPE_I, \
                             /*_formats*/ AUDIO_DATA_FORMAT_TYPE_I_PCM, /*_nchannelsphysical*/ 0x02, \
                             /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT, \
                             /*_stridx*/ 0x00), \
    /* Type I Format Type Descriptor(2.3.1.6 - Audio Formats) */ \
    TUD_AUDIO_DESC_TYPE_I_FORMAT(_nBytesPerSample, _nBitsUsedPerSample), \
    /* Standard AS Isochronous Audio Data Endpoint Descriptor(4.10.1.1) */ \
    TUD_AUDIO_DESC_STD_AS_ISO_EP(/*_ep*/ _epout, \
                                 /*_attr*/ (TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_SYNCHRONOUS | \
                                            TUSB_ISO_EP_ATT_DATA), /*_maxEPsize*/ _epsize3, \
                                 /*_interval*/ (CFG_TUSB_RHPORT0_MODE & OPT_MODE_HIGH_SPEED) ? 0x04 : 0x01), \
    /* Class-Specific AS Isochronous Audio Data Endpoint Descriptor(4.10.1.2) */ \
    TUD_AUDIO_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, \
                                /*_ctrl*/ AUDIO_CTRL_NONE, \
                                /*_lockdelayunit*/ AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, \
                                /*_lockdelay*/ 0x0000) \

#define TUD_AUDIO_SPK3_DESC_LEN (TUD_AUDIO_DESC_IAD_LEN \
                                 + TUD_AUDIO_DESC_STD_AC_LEN \
                                 + TUD_AUDIO_DESC_CS_AC_LEN \
                                 + TUD_AUDIO_DESC_CLK_SRC_LEN \
                                 + TUD_AUDIO_DESC_INPUT_TERM_LEN \
                                 + TUD_AUDIO_DESC_OUTPUT_TERM_LEN \
                                 + 0 * TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL_LEN \
                                 + TUD_AUDIO_DESC_STD_AS_INT_LEN \
                                 + (TUD_AUDIO_DESC_STD_AS_INT_LEN \
                                    + TUD_AUDIO_DESC_CS_AS_INT_LEN \
                                    + TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN \
                                    + TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN \
                                    + TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN) * 3)


#define TUD_AUDIO_SPEAKER_DESCRIPTOR_FLOAT(_itfnum, _stridx, _epout, _epsize) \
    /* Standard Interface Association Descriptor (IAD) */ \
    TUD_AUDIO_DESC_IAD(/*_firstitfs*/ _itfnum, /*_nitfs*/ 0x02, /*_stridx*/ 0x00), \
    /* Standard AC Interface Descriptor(4.7.1) */ \
    TUD_AUDIO_DESC_STD_AC(/*_itfnum*/ _itfnum, /*_nEPs*/ 0x00, /*_stridx*/ _stridx), \
    /* Class-Specific AC Interface Header Descriptor(4.7.2) */ \
    TUD_AUDIO_DESC_CS_AC(/*_bcdADC*/ 0x0200, /*_category*/ AUDIO_FUNC_DESKTOP_SPEAKER, \
                         /*_totallen*/ TUD_AUDIO_DESC_CLK_SRC_LEN+TUD_AUDIO_DESC_INPUT_TERM_LEN+ \
    TUD_AUDIO_DESC_OUTPUT_TERM_LEN+0*TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL_LEN, \
                         /*_ctrl*/ AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS), \
    /* Clock Source Descriptor(4.7.2.1) */ \
    TUD_AUDIO_DESC_CLK_SRC(/*_clkid*/ 0x04, /*_attr*/ AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK, \
                           /*_ctrl*/ (AUDIO_CTRL_RW << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS), /*_assocTerm*/ 0x00, \
                           /*_stridx*/ 0x00), \
    /* Input Terminal Descriptor(4.7.2.4) */ \
    TUD_AUDIO_DESC_INPUT_TERM(/*_termid*/ 0x01, /*_termtype*/ AUDIO_TERM_TYPE_USB_STREAMING, /*_assocTerm*/ 0x00, \
                              /*_clkid*/ 0x04, /*_nchannelslogical*/ 0x02, \
                              /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT, \
                              /*_idxchannelnames*/ 0x00, \
                              /*_ctrl*/ 0 * (AUDIO_CTRL_R << AUDIO_IN_TERM_CTRL_CONNECTOR_POS), /*_stridx*/ 0x00), \
    /* Output Terminal Descriptor(4.7.2.5) */ \
    TUD_AUDIO_DESC_OUTPUT_TERM(/*_termid*/ 0x03, /*_termtype*/ AUDIO_TERM_TYPE_OUT_DESKTOP_SPEAKER, \
                               /*_assocTerm*/ 0x00, /*_srcid*/ 0x01, /*_clkid*/ 0x04, /*_ctrl*/ 0x0000, \
                               /*_stridx*/ 0x00), \
    /* Standard AS Interface Descriptor(4.9.1) */ \
    /* Interface 1, Alternate 0 - default alternate setting with 0 bandwidth */ \
    TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum) + 1), /*_altset*/ 0x00, /*_nEPs*/ 0x00, \
                              /*_stridx*/ 0x00), \
    /* Standard AS Interface Descriptor(4.9.1) */ \
    /* Interface 1, Alternate 1 - alternate interface for data streaming */ \
    TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum) + 1), /*_altset*/ 0x01, /*_nEPs*/ 0x01, \
                              /*_stridx*/ 0x00), \
    /* Class-Specific AS Interface Descriptor(4.9.2) */ \
    TUD_AUDIO_DESC_CS_AS_INT(/*_termid*/ 0x01, /*_ctrl*/ AUDIO_CTRL_NONE, /*_formattype*/ AUDIO_FORMAT_TYPE_I, \
                             /*_formats*/ AUDIO_DATA_FORMAT_TYPE_I_IEEE_FLOAT, /*_nchannelsphysical*/ 0x02, \
                             /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT, \
                             /*_stridx*/ 0x00), \
    /* Type I Format Type Descriptor(2.3.1.6 - Audio Formats) */ \
    TUD_AUDIO_DESC_TYPE_I_FORMAT(4, 32), \
    /* Standard AS Isochronous Audio Data Endpoint Descriptor(4.10.1.1) */ \
    TUD_AUDIO_DESC_STD_AS_ISO_EP(/*_ep*/ _epout, \
                                 /*_attr*/ (TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_SYNCHRONOUS | \
                                            TUSB_ISO_EP_ATT_DATA), /*_maxEPsize*/ _epsize, \
                                 /*_interval*/ (CFG_TUSB_RHPORT0_MODE & OPT_MODE_HIGH_SPEED) ? 0x04 : 0x01), \
    /* Class-Specific AS Isochronous Audio Data Endpoint Descriptor(4.10.1.2) */ \
    TUD_AUDIO_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, \
                                /*_ctrl*/ AUDIO_CTRL_NONE, \
                                /*_lockdelayunit*/ AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, \
                                /*_lockdelay*/ 0x0000) \

#define TUD_AUDIO_SPEAKER_STEREO_DESC_LEN (TUD_AUDIO_DESC_IAD_LEN \
                                           + TUD_AUDIO_DESC_STD_AC_LEN \
                                           + TUD_AUDIO_DESC_CS_AC_LEN \
                                           + TUD_AUDIO_DESC_CLK_SRC_LEN \
                                           + TUD_AUDIO_DESC_INPUT_TERM_LEN \
                                           + TUD_AUDIO_DESC_OUTPUT_TERM_LEN \
                                           + 0 * TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL_LEN \
                                           + TUD_AUDIO_DESC_STD_AS_INT_LEN \
                                           + (TUD_AUDIO_DESC_STD_AS_INT_LEN \
                                              + TUD_AUDIO_DESC_CS_AS_INT_LEN \
                                              + TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN \
                                              + TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN \
                                              + TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN) * 1)

#define TUD_AUDIO_SPEAKER_STEREO_DESCRIPTOR(_itfnum, _stridx, _nBytesPerSample, _nBitsUsedPerSample, _epout, _epsize) \
    /* Standard Interface Association Descriptor (IAD) */ \
    TUD_AUDIO_DESC_IAD(/*_firstitfs*/ _itfnum, /*_nitfs*/ 0x02, /*_stridx*/ 0x00), \
    /* Standard AC Interface Descriptor(4.7.1) */ \
    TUD_AUDIO_DESC_STD_AC(/*_itfnum*/ _itfnum, /*_nEPs*/ 0x00, /*_stridx*/ _stridx), \
    /* Class-Specific AC Interface Header Descriptor(4.7.2) */ \
    TUD_AUDIO_DESC_CS_AC(/*_bcdADC*/ 0x0200, /*_category*/ AUDIO_FUNC_DESKTOP_SPEAKER, \
                         /*_totallen*/ TUD_AUDIO_DESC_CLK_SRC_LEN+TUD_AUDIO_DESC_INPUT_TERM_LEN+ \
    TUD_AUDIO_DESC_OUTPUT_TERM_LEN+0*TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL_LEN, \
                         /*_ctrl*/ AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS), \
    /* Clock Source Descriptor(4.7.2.1) */ \
    TUD_AUDIO_DESC_CLK_SRC(/*_clkid*/ 0x04, /*_attr*/ AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK, \
                           /*_ctrl*/ (AUDIO_CTRL_RW << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS), /*_assocTerm*/ 0x00, \
                           /*_stridx*/ 0x00), \
    /* Input Terminal Descriptor(4.7.2.4) */ \
    TUD_AUDIO_DESC_INPUT_TERM(/*_termid*/ 0x01, /*_termtype*/ AUDIO_TERM_TYPE_USB_STREAMING, /*_assocTerm*/ 0x00, \
                              /*_clkid*/ 0x04, /*_nchannelslogical*/ 0x02, \
                              /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT, \
                              /*_idxchannelnames*/ 0x00, \
                              /*_ctrl*/ 0 * (AUDIO_CTRL_R << AUDIO_IN_TERM_CTRL_CONNECTOR_POS), /*_stridx*/ 0x00), \
    /* Output Terminal Descriptor(4.7.2.5) */ \
    TUD_AUDIO_DESC_OUTPUT_TERM(/*_termid*/ 0x03, /*_termtype*/ AUDIO_TERM_TYPE_OUT_DESKTOP_SPEAKER, \
                               /*_assocTerm*/ 0x00, /*_srcid*/ 0x01, /*_clkid*/ 0x04, /*_ctrl*/ 0x0000, \
                               /*_stridx*/ 0x00), \
    /* Standard AS Interface Descriptor(4.9.1) */ \
    /* Interface 1, Alternate 0 - default alternate setting with 0 bandwidth */ \
    TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum) + 1), /*_altset*/ 0x00, /*_nEPs*/ 0x00, \
                              /*_stridx*/ 0x00), \
    /* Standard AS Interface Descriptor(4.9.1) */ \
    /* Interface 1, Alternate 1 - alternate interface for data streaming */ \
    TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum) + 1), /*_altset*/ 0x01, /*_nEPs*/ 0x01, \
                              /*_stridx*/ 0x00), \
    /* Class-Specific AS Interface Descriptor(4.9.2) */ \
    TUD_AUDIO_DESC_CS_AS_INT(/*_termid*/ 0x01, /*_ctrl*/ AUDIO_CTRL_NONE, /*_formattype*/ AUDIO_FORMAT_TYPE_I, \
                             /*_formats*/ AUDIO_DATA_FORMAT_TYPE_I_PCM, /*_nchannelsphysical*/ 0x02, \
                             /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT, \
                             /*_stridx*/ 0x00), \
    /* Type I Format Type Descriptor(2.3.1.6 - Audio Formats) */ \
    TUD_AUDIO_DESC_TYPE_I_FORMAT(_nBytesPerSample, _nBitsUsedPerSample), \
    /* Standard AS Isochronous Audio Data Endpoint Descriptor(4.10.1.1) */ \
    TUD_AUDIO_DESC_STD_AS_ISO_EP(/*_ep*/ _epout, \
                                 /*_attr*/ (TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_SYNCHRONOUS | \
                                            TUSB_ISO_EP_ATT_DATA), /*_maxEPsize*/ _epsize, \
                                 /*_interval*/ (CFG_TUSB_RHPORT0_MODE & OPT_MODE_HIGH_SPEED) ? 0x04 : 0x01), \
    /* Class-Specific AS Isochronous Audio Data Endpoint Descriptor(4.10.1.2) */ \
    TUD_AUDIO_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, \
                                /*_ctrl*/ AUDIO_CTRL_NONE, \
                                /*_lockdelayunit*/ AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, \
                                /*_lockdelay*/ 0x0000) \

#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN ( \
        (1 - CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP) * CFG_TUD_AUDIO_OUT * \
        (TUD_AUDIO_SPEAKER_MONO_DESC_LEN * (CFG_TUD_AUDIO_N_CHANNELS_RX == 1 ? 1 : 0)) + \
        (0 + CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP) * CFG_TUD_AUDIO_OUT * \
        (TUD_AUDIO_SPEAKER_MONO_FB_DESC_LEN * (CFG_TUD_AUDIO_N_CHANNELS_RX == 1 ? 1 : 0)) + \
        (1 - CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP) * CFG_TUD_AUDIO_OUT * \
        (TUD_AUDIO_SPEAKER_STEREO_DESC_LEN * (CFG_TUD_AUDIO_N_CHANNELS_RX == 2 ? 1 : 0)))
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT 1
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ 64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
