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

#ifndef H_BLE_SRV_ASHA_
#define H_BLE_SRV_ASHA_

#include <stdbool.h>
#include "nimble/ble.h"
#ifdef __cplusplus
extern "C" {
#endif


#define LEFT                         0X02
#define RIGHT                        0X03
#define DEVICE_SIDE                  RIGHT

/********* Hearing aid service. ***********************/
#define BLE_SVC_ASHA_UUID16     0xFDF0

/* Define audio types that can be received from the client. */
typedef enum {
    BLE_SVC_ASHA_UNKNOWN,
    BLE_SVC_ASHA_RINGTONE,
    BLE_SVC_ASHA_PHONE_CALL,
    BLE_SVC_ASHA_MEDIA
} ble_scv_asha_audio_types_t ;

/* Define the current audio status. */
typedef enum {
    BLE_SVC_ASHA_STATUS_OK,
    BLE_SVC_ASHA_UNKNOWN_COMMAND,
    BLE_SVC_ASHA_ILLIGAL_PARAMETERS
} ble_scv_asha_audio_status_t ;

struct ble_svc_asha_audio_control {
    uint8_t codec_in_use;
    int8_t volume_lvl;
    ble_scv_asha_audio_types_t audio_type;
    uint8_t start_flag;
};

uint16_t ble_svc_asha_get_psm_handle(void);
uint8_t ble_svc_asha_is_started(void);
void gatt_svc_asha_init(void);

#ifdef __cplusplus
}
#endif

#endif
