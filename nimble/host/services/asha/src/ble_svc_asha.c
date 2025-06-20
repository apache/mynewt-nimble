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
#include <stdio.h>
#include <string.h>

#include "sysinit/sysinit.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/asha/ble_svc_asha.h"

#define CONTROL_POINT_OP_START        0x01
#define CONTROL_POINT_OP_STOP         0x02
#define READ_ONLY_BUFFER_SIZE         17

static ble_scv_asha_audio_status_t audio_status;
static struct ble_svc_asha_audio_control audio_control_point;
/* Byte                 Descreption
 * -------------------------------------
 *   0                     Version - must be 0x01.
 *   1                    Left or right side of the hearing aid.
 *   2-9                  ID of the manufacturer.
 *   10                   LE CoC audio output streaming supported.
 *   11-12                RenderDelay.
 *   13-14                PreparationDelay.
 *   15-16                Supported Codec IDs G722 16KHZ or G722_24KHZ .
 */
static const uint8_t read_only_buffer[READ_ONLY_BUFFER_SIZE] = { 0x01, DEVICE_SIDE,
        0x31, 0x00,0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x00, 0x00, 0x00, 0x07, 0x00, 0x07,0x00 };
static const uint16_t psm_handle = MYNEWT_VAL(BLE_SVC_ASHA_PSM_HANDLE);
static uint16_t audio_status_handle;

/* ASHA Characteristics UUIDS. */
/*6333651e-c481-4a3e-9169-7c902aad37bb*/
const ble_uuid128_t asha_chr_device_prop =
    BLE_UUID128_INIT(0xbb, 0x37, 0xad, 0x2a, 0x90, 0x7c, 0x69, 0x91
            ,0x3e,  0x4a, 0x81, 0xc4, 0x1e, 0x65, 0x33, 0x63);
/* f0d4de7e-4a88-476c-9d9f-1937b0996cc0 */
const ble_uuid128_t asha_chr_control_audio =
    BLE_UUID128_INIT(0xc0, 0x6c, 0x99, 0xb0, 0x37, 0x19, 0x9f, 0x9d,
            0x6c, 0x47, 0x88, 0x4a, 0x7e, 0xde, 0xd4, 0xf0);
/* 38663f1a-e711-4cac-b641-326b56404837 */
const ble_uuid128_t asha_chr_audio_status =
    BLE_UUID128_INIT(0x37, 0x48, 0x40, 0x56, 0x6b, 0x32, 0x41, 0xb6,
            0xac, 0x4c, 0x11, 0xe7, 0x1a, 0x3f, 0x66, 0x38);
/* 00e4ca9e-ab14-41e4-8823-f9e70c7e91df */
const ble_uuid128_t asha_chr_set_volume =
    BLE_UUID128_INIT(0xdf, 0x91, 0x7e, 0x0c, 0xe7, 0xf9, 0x23, 0x88,
            0xe4, 0x41, 0x14, 0xab, 0x9e, 0xca, 0xe4, 0x00);
/* 2d410339-82b6-42aa-b34e-e2e01df8cc1a */
const ble_uuid128_t asha_chr_read_l2cap_psm =
    BLE_UUID128_INIT(0x1a, 0xcc, 0xf8, 0x1d, 0xe0, 0xe2, 0x4e, 0xb3,
              0xaa, 0x42, 0xb6, 0x82, 0x39, 0x03, 0x41, 0x2d);

static int
read_device_prop(uint16_t conn_handle, uint16_t attr_handle,
        struct ble_gatt_access_ctxt *ctxt, void *arg);
static int
control_audio(uint16_t conn_handle, uint16_t attr_handle,
        struct ble_gatt_access_ctxt *ctxt, void *arg);
static int
read_audio_status(uint16_t conn_handle, uint16_t attr_handle,
        struct ble_gatt_access_ctxt *ctxt, void *arg);
static int
set_volume(uint16_t conn_handle, uint16_t attr_handle,
        struct ble_gatt_access_ctxt *ctxt, void *arg);
static int
read_l2cap_chan_psm(uint16_t conn_handle, uint16_t attr_handle,
        struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def ble_svc_asha[] = {
        {
        /* Service: HEARING AID. */
            .type = BLE_GATT_SVC_TYPE_PRIMARY,
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_ASHA_UUID16),
            .characteristics = (struct ble_gatt_chr_def[]) { {
        /* Characteristic: read_device_prop. */
                .uuid = &asha_chr_device_prop.u,
                .access_cb = read_device_prop,
                .flags = BLE_GATT_CHR_F_READ,
            }, {
                /* Characteristic: control_audior. */
                .uuid = &asha_chr_control_audio.u,
                .access_cb = control_audio,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
            }, {
                /* Characteristic: AudioStatus. */
                .uuid = &asha_chr_audio_status.u,
                .access_cb = read_audio_status,
                .val_handle = &audio_status_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            }, {
                /* Characteristic: set_volume. */
                .uuid = &asha_chr_set_volume.u,
                .access_cb = set_volume,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
            }, {
                /* Characteristic: read_l2cap_chan_psm. */
                .uuid =&asha_chr_read_l2cap_psm.u,
                .access_cb = read_l2cap_chan_psm,
                .flags = BLE_GATT_CHR_F_READ,
            }, {
                0, /* No more characteristics in this service. */
            } },
    },

    {
            0, /* No more services. */
    },
};

static int
read_device_prop(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;
    /* Device capabilities ,codec ID ,render delay ,preperation delay */
    rc = os_mbuf_append(ctxt->om, &read_only_buffer, sizeof read_only_buffer);
    return rc;
}

static int
control_audio(uint16_t conn_handle, uint16_t attr_handle,
        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t opcode = ctxt->om->om_data[0];
    uint16_t len = ctxt->om->om_len;
    switch (opcode) {
    case CONTROL_POINT_OP_START:
        /* Start stream audio to the logger. */
        audio_control_point.start_flag = 1;

        if (len != 4) {
            audio_status = BLE_SVC_ASHA_ILLIGAL_PARAMETERS;
            break;
        }
        /* Reset the use codec. */
        audio_control_point.codec_in_use = ctxt->om->om_data[1];
        /*
         0 - Unknown
         1 - Ringtone
         2 - Phonecall
         3 - Media
         */
        audio_control_point.audio_type = ctxt->om->om_data[2];
        /* Volume level*/
        audio_control_point.volume_lvl = ctxt->om->om_data[3];
        audio_status = BLE_SVC_ASHA_STATUS_OK;
        break;
    case CONTROL_POINT_OP_STOP:
        /* Instructs the server to stop rendering audio.*/
        audio_control_point.start_flag = 0;
        audio_status = BLE_SVC_ASHA_STATUS_OK;
        break;
    default:
        audio_status = BLE_SVC_ASHA_ILLIGAL_PARAMETERS;
        break;
    }
    /* TODO Notify audio_status to the client. */
    return 0;
}

static int
read_audio_status(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /*
     0 - Status OK
     1 - Unknown command
     2 - Illegal parameters
     */
    return audio_status;
}

static int
set_volume(uint16_t conn_handle, uint16_t attr_handle,
        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    audio_control_point.volume_lvl = ctxt->om->om_data[0];
    return 0;
}

static int
read_l2cap_chan_psm(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;
    /* PSM to use for connecting the audio channel. To be picked 
     * from the dynamic range. */
    rc = os_mbuf_append(ctxt->om, &psm_handle, sizeof psm_handle);
    return rc;
}

/**
 * Get psm handle that server decides to open coc l2cap channel into..
 *
 *  * @return                      The selected l2cap psm handle.
 */
uint16_t
ble_svc_asha_get_psm_handle(void)
{
    return psm_handle;
}

/**
 * Indicates if client send start audio streaming..
 *
 *  * @return                      The start flag from the control point
 *                                  structure.
 *
 */
uint8_t
ble_svc_asha_is_started(void)
{
    return audio_control_point.start_flag;
}

void
gatt_svc_asha_init(void)
{
    int rc;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    rc = ble_gatts_count_cfg(ble_svc_asha);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gatts_add_svcs(ble_svc_asha);
    SYSINIT_PANIC_ASSERT(rc == 0);
}
