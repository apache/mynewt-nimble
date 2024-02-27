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
#include <syscfg/syscfg.h>
#include <bsp/bsp.h>
#include <string.h>
#include <tusb.h>
#include <device/usbd.h>
#include <class/audio/audio.h>
#include <os/util.h>
#include <usb_audio.h>

#define USBD_PRODUCT_RELEASE_NUMBER MYNEWT_VAL(USBD_PRODUCT_RELEASE_NUMBER)

#ifndef CONFIG_NUM
#define CONFIG_NUM 1
#endif

typedef enum {
    USB_STRING_DESCRIPTOR_LANG          = 0,
    USB_STRING_DESCRIPTOR_MANUFACTURER  = 1,
    USB_STRING_DESCRIPTOR_PRODUCT       = 2,
    USB_STRING_DESCRIPTOR_INTERFACE     = 3,
    USB_STRING_DESCRIPTOR_CDC           = 4,
    USB_STRING_DESCRIPTOR_SERIAL        = 16,
    USB_STRING_DESCRIPTOR_MICROSOFT_OS  = 0xEE,
} usb_string_descriptor_ix_t;

#define CDC_IF_STR_IX (MYNEWT_VAL(USBD_CDC_DESCRIPTOR_STRING) == NULL ? 0 : 4)

const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,

    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,

    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = MYNEWT_VAL(USBD_VID),
    .idProduct = MYNEWT_VAL(USBD_PID),
    .bcdDevice = USBD_PRODUCT_RELEASE_NUMBER,

    .iManufacturer = USB_STRING_DESCRIPTOR_MANUFACTURER,
    .iProduct = USB_STRING_DESCRIPTOR_PRODUCT,
    .iSerialNumber = USB_STRING_DESCRIPTOR_SERIAL,

    .bNumConfigurations = 0x01
};

/*
 * Invoked when received GET DEVICE DESCRIPTOR
 * Application return pointer to descriptor
 */
const uint8_t *
tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&desc_device;
}

#if MYNEWT_VAL_CHOICE(MCU_TARGET, nRF5340_APP) || MYNEWT_VAL_CHOICE(MCU_TARGET, nRF52840)
#define ISO_EP  8
#else
#error MCU not supported
#endif

/*
 * Configuration Descriptor
 */

enum {
#if CFG_TUD_BTH
    ITF_NUM_BTH,
#if CFG_TUD_BTH_ISO_ALT_COUNT > 0
    ITF_NUM_BTH_VOICE,
#endif
#endif

#if CFG_TUD_CDC
    ITF_NUM_CDC,
    ITF_NUM_CDC_DATA,
#endif

#if CFG_TUD_MSC
    ITF_NUM_MSC,
#endif

#if CFG_TUD_HID
    ITF_NUM_HID,
#endif

#if CFG_TUD_AUDIO_IN
    ITF_NUM_AUDIO_AC,
    ITF_NUM_AUDIO_AS_IN,
#elif CFG_TUD_AUDIO_OUT
    ITF_NUM_AUDIO_AC,
    ITF_NUM_AUDIO_AS_OUT,
#elif CFG_TUD_AUDIO_IN_OUT
    ITF_NUM_AUDIO_AC,
    ITF_NUM_AUDIO_AS_IN,
    ITF_NUM_AUDIO_AS_OUT,
#endif

    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + \
                             CFG_TUD_CDC * TUD_CDC_DESC_LEN + \
                             CFG_TUD_MSC * TUD_MSC_DESC_LEN + \
                             CFG_TUD_HID * TUD_HID_DESC_LEN + \
                             CFG_TUD_BTH * TUD_BTH_DESC_LEN + \
                             CFG_TUD_AUDIO_IN * TUD_AUDIO_MIC_ONE_CH_DESC_LEN + \
                             (1 + CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP) * CFG_TUD_AUDIO_OUT * \
                             (TUD_AUDIO_SPEAKER_MONO_DESC_LEN * (CFG_TUD_AUDIO_N_CHANNELS_RX == 1 ? 1 : 0)) + \
                             (1 - CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP) * CFG_TUD_AUDIO_OUT * \
                             (TUD_AUDIO_SPEAKER_STEREO_DESC_LEN * (CFG_TUD_AUDIO_N_CHANNELS_RX == 2 ? 1 : 0)) + \
                             0)

const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(CONFIG_NUM, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
                          MYNEWT_VAL(USBD_CONFIGURATION_MAX_POWER)),

#if CFG_TUD_BTH
    TUD_BTH_DESCRIPTOR(ITF_NUM_BTH, BTH_IF_STR_IX, USBD_BTH_EVENT_EP, USBD_BTH_EVENT_EP_SIZE,
                       USBD_BTH_EVENT_EP_INTERVAL, USBD_BTH_DATA_IN_EP, USBD_BTH_DATA_OUT_EP, USBD_BTH_DATA_EP_SIZE,
                       0, 9, 17, 25, 33, 49),
#endif


#if CFG_TUD_CDC
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, CDC_IF_STR_IX, USBD_CDC_NOTIFY_EP, USBD_CDC_NOTIFY_EP_SIZE,
                       USBD_CDC_DATA_OUT_EP, USBD_CDC_DATA_IN_EP, USBD_CDC_DATA_EP_SIZE),
#endif

#if CFG_TUD_MSC
    /* TODO: MSC not handled yet */
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, MSC_IF_STR_IX, EPNUM_MSC_OUT, EPNUM_MSC_IN,
                       (CFG_TUSB_RHPORT0_MODE & OPT_MODE_HIGH_SPEED) ? 512 : 64),
#endif

#if CFG_TUD_HID
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, HID_IF_STR_IX, HID_PROTOCOL_NONE, sizeof(desc_hid_report),
                       USBD_HID_REPORT_EP, USBD_HID_REPORT_EP_SIZE, USBD_HID_REPORT_EP_INTERVAL),
#endif

#if CFG_TUD_AUDIO_IN_OUT
#elif CFG_TUD_AUDIO_IN
    TUD_AUDIO_MIC2_DESCRIPTOR(ITF_NUM_AUDIO_AC_IN, 0, 2, 16, 0x80 | ISO_EP, 192),
#elif CFG_TUD_AUDIO_OUT && CFG_TUD_AUDIO_N_CHANNELS_RX == 2
#if CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP == 1
    TUD_AUDIO_SPEAKER_STEREO_FB_DESCRIPTOR(ITF_NUM_AUDIO_AC, 0, CFG_TUD_AUDIO_N_BYTES_PER_SAMPLE_RX,
    8 * CFG_TUD_AUDIO_N_BYTES_PER_SAMPLE_RX, ISO_EP, CFG_TUD_AUDIO_EPSIZE_OUT, 0x80 | ISO_EP, 1),
#else
    TUD_AUDIO_SPEAKER_STEREO_DESCRIPTOR(ITF_NUM_AUDIO_AC, 0, CFG_TUD_AUDIO_N_BYTES_PER_SAMPLE_RX,
                                        8 * CFG_TUD_AUDIO_N_BYTES_PER_SAMPLE_RX, ISO_EP, CFG_TUD_AUDIO_EPSIZE_OUT),
#endif
#elif CFG_TUD_AUDIO_OUT && CFG_TUD_AUDIO_N_CHANNELS_RX == 1
    #if CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP
    TUD_AUDIO_SPEAKER_MONO_FB_DESCRIPTOR(ITF_NUM_AUDIO_AC, 0, 2, 16, ISO_EP, 100, 0x80 | ISO_EP),
#else
    TUD_AUDIO_SPEAKER_MONO_DESCRIPTOR(ITF_NUM_AUDIO_AC, 0, CFG_TUD_AUDIO_N_BYTES_PER_SAMPLE_RX,
    8 * CFG_TUD_AUDIO_N_BYTES_PER_SAMPLE_RX, ISO_EP, CFG_TUD_AUDIO_EPSIZE_OUT),
#endif
#endif
};

/**
 * Invoked when received GET CONFIGURATION DESCRIPTOR
 * Application return pointer to descriptor
 * Descriptor contents must exist long enough for transfer to complete
 */
const uint8_t *
tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;

    return desc_configuration;
}

static uint16_t desc_string[MYNEWT_VAL(USBD_STRING_DESCRIPTOR_MAX_LENGTH) + 1];

#if CFG_TUD_AUDIO

#if CFG_TUD_AUDIO_IN
const uint16_t tud_audio_desc_lengths[] = {TUD_AUDIO_MIC2_DESC_LEN};
#elif CFG_TUD_AUDIO_OUT && CFG_TUD_AUDIO_N_CHANNELS_RX == 1
#if CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP
const uint16_t tud_audio_desc_lengths[] = {TUD_AUDIO_SPEAKER_MONO_FB_DESC_LEN};
#else
const uint16_t tud_audio_desc_lengths[] = {TUD_AUDIO_SPEAKER_MONO_DESC_LEN};
#endif
#elif CFG_TUD_AUDIO_OUT && CFG_TUD_AUDIO_N_CHANNELS_RX == 2
#if CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP
const uint16_t tud_audio_desc_lengths[] = {TUD_AUDIO_SPEAKER_STEREO_FB_DESC_LEN};
#else
const uint16_t tud_audio_desc_lengths[] = {TUD_AUDIO_SPEAKER_STEREO_DESC_LEN};
#endif
#endif

static uint32_t g_sample_rate = CFG_TUD_AUDIO_SAMPLE_RATE;
static usb_audio_sample_rate_cb_t sample_rate_cb;

/* Invoked when audio class specific set request received for an entity */
bool
tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const * p_request, uint8_t *pBuff)
{
    uint32_t new_sample_rate;

    (void) rhport;
    audio_control_request_t *request = (audio_control_request_t *) p_request;

    if (request->bEntityID == 2 && request->bControlSelector == AUDIO_FU_CTRL_VOLUME &&
        request->bRequest == AUDIO_CS_REQ_CUR) {
        /* Ignore value but accept request */
        return true;
    } else if (request->bEntityID == 4 && request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ &&
               request->bRequest == AUDIO_CS_REQ_CUR) {
        /* Ignore value but accept request */
        new_sample_rate = ((audio_control_cur_4_t *)(pBuff))->bCur;
        if (new_sample_rate != g_sample_rate) {
            g_sample_rate = new_sample_rate;
            if (sample_rate_cb) {
                sample_rate_cb(g_sample_rate);
            }
        }
        return true;
    } else {
        __BKPT(1);
    }
    return false;
}

bool
tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
    (void) rhport;
    audio_control_request_t *request = (audio_control_request_t *) p_request;
    if (request->bEntityID == 4 && request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
        if (request->bRequest == AUDIO_CS_REQ_CUR) {
            audio_control_cur_4_t curf = {g_sample_rate};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &curf, sizeof(curf));
        } else if (request->bRequest == AUDIO_CS_REQ_RANGE) {
#if 1
            audio_control_range_4_n_t(1) rangef = {
                .wNumSubRanges = 1,
                .subrange[0] = {g_sample_rate, g_sample_rate, 0},
            };
#else
            audio_control_range_4_n_t(2) rangef = {
                .wNumSubRanges = 2,
                .subrange[0] = {16000, 16000, 0},
                .subrange[1] = {48000, 48000, 0},
            };
#endif
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &rangef, sizeof(rangef));
        }
    } else if (request->bEntityID == 5 && request->bControlSelector == AUDIO_CX_CTRL_CONTROL) {
        if (request->bRequest == AUDIO_CS_REQ_CUR) {
            audio_control_cur_1_t cur_clk = {1};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &cur_clk, sizeof(cur_clk));
        }
    } else if (request->bEntityID == 4 && request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID &&
               request->bRequest == AUDIO_CS_REQ_CUR) {
        audio_control_cur_1_t cur_valid = {.bCur = 1};
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &cur_valid, sizeof(cur_valid));
    } else if (request->bEntityID == 2 && request->bControlSelector == AUDIO_FU_CTRL_MUTE &&
               request->bRequest == AUDIO_CS_REQ_CUR) {
        audio_control_cur_1_t mute = {.bCur = 0};
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &mute, sizeof(mute));
    } else if (request->bEntityID == 2 && request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
        if (request->bRequest == AUDIO_CS_REQ_RANGE) {
            audio_control_range_2_n_t(1) range_vol = {
                .wNumSubRanges = 1,
                .subrange[0] = {0, 1000, 10}
            };
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &range_vol, sizeof(range_vol));
        } else if (request->bRequest == AUDIO_CS_REQ_CUR) {
            audio_control_cur_2_t cur_vol = {.bCur = 1280};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &cur_vol, sizeof(cur_vol));
        }
    } else {
        __BKPT(1);
    }
    return false;
}

#endif

/* Function converts ASCII string to USB descriptor format */
static uint16_t *
string_to_usb_desc_string(const char *str, uint16_t *desc, uint8_t desc_size)
{
    int i;
    int char_num = strlen(str);

    assert(char_num < desc_size);
    if (char_num >= desc_size) {
        char_num = desc_size - 1;
    }

    /* Encode length in first byte, type in second byte */
    desc[0] = tu_htole16(tu_u16(TUSB_DESC_STRING, 2 * (char_num + 1)));

    /* Copy characters 8bit to 16 bits */
    for (i = 0; i < char_num; ++i) {
        desc[1 + i] = tu_htole16(str[i]);
    }

    return desc;
}

/* LANGID string descriptors */
static const uint16_t usbd_lang_id[2] = {
    (TUSB_DESC_STRING << 8) + 4, /* Size of this descriptor */
    tu_htole16(MYNEWT_VAL(USBD_LANGID))
};

static struct {
    uint16_t major;
    uint16_t minor;
    uint16_t revision;
    uint32_t build;
} img_version = {
    1, 0, 0, 1
};

static char serial_number[11];

static uint16_t *
serial_to_usb_desc_string(uint16_t *desc, size_t size)
{
    if (serial_number[0] == 0) {
        uint64_t serial = MYNEWT_VAL(USBD_SERIAL_ID);

        snprintf(serial_number, 11, "%010" PRIu64, serial);
    }

    return string_to_usb_desc_string(serial_number, desc, size);
}

/*
 * Invoked when received GET STRING DESCRIPTOR request
 * Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
 */
const uint16_t *
tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    const uint16_t *ret = NULL;
    char interface[100];

#if MYNEWT_VAL(USBD_WINDOWS_COMP_ID)
    if (index == USB_STRING_DESCRIPTOR_MICROSOFT_OS) {
        ret = (const uint16_t *)microsoft_os_string_descriptor;
    }
#endif
    if (index == USB_STRING_DESCRIPTOR_LANG) {
        ret = usbd_lang_id;
    } else if (index == USB_STRING_DESCRIPTOR_SERIAL) {
        ret = serial_to_usb_desc_string(desc_string, ARRAY_SIZE(desc_string));
    } else if (index == USB_STRING_DESCRIPTOR_MANUFACTURER) {
        ret = string_to_usb_desc_string(MYNEWT_VAL(USBD_VENDOR_STRING), desc_string, ARRAY_SIZE(desc_string));
    } else if (index == USB_STRING_DESCRIPTOR_PRODUCT) {
        ret = string_to_usb_desc_string(MYNEWT_VAL(USBD_PRODUCT_STRING), desc_string, ARRAY_SIZE(desc_string));
    } else if (index == USB_STRING_DESCRIPTOR_INTERFACE) {
        snprintf(interface, sizeof(interface), "%s, (%u.%u.%u.%lu)", MYNEWT_VAL(USBD_PRODUCT_STRING),
                 img_version.major, img_version.minor, img_version.revision, img_version.build);
        ret = string_to_usb_desc_string(interface, desc_string, ARRAY_SIZE(desc_string));
    }
    return ret;
}

void
usb_desc_sample_rate_set(uint32_t sample_rate)
{
    g_sample_rate = sample_rate;
}
