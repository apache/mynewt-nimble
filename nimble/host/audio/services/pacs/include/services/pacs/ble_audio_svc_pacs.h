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

#ifndef H_BLE_AUDIO_SVC_PACS_
#define H_BLE_AUDIO_SVC_PACS_

/**
 * @file ble_audio_svc_pacs.h
 *
 * @brief Bluetooth LE Audio PAC Service
 *
 * This header file provides the public API for interacting with the PACS package.
 *
 * @defgroup ble_audio_svc_pacs Bluetooth LE Audio PACS package
 * @ingroup bt_host
 * @{
 *
 * This package is used to setup PACS for codecs registered with @ref ble_audio_codec.
 * To register a codec create it's definition as `ble_audio_codec_record` structure and register it
 * using `ble_audio_codec_register()`. Up to BLE_AUDIO_MAX_CODEC_RECORDS entries may be registered.
 * Registering and unregistering codecs, as well as setting PACS parameters will trigger sending
 * notifications, if their support is enabled (see pacs/syscfg.yml).
 *
 */

#define BLE_SVC_AUDIO_PACS_UUID16                                   0x1850
#define BLE_SVC_AUDIO_PACS_CHR_UUID16_SINK_PAC                      0x2BC9
#define BLE_SVC_AUDIO_PACS_CHR_UUID16_SINK_AUDIO_LOCATIONS          0x2BCA
#define BLE_SVC_AUDIO_PACS_CHR_UUID16_SOURCE_PAC                    0x2BCB
#define BLE_SVC_AUDIO_PACS_CHR_UUID16_SOURCE_AUDIO_LOCATIONS        0x2BCC
#define BLE_SVC_AUDIO_PACS_CHR_UUID16_AVAILABLE_AUDIO_CONTEXTS      0x2BCD
#define BLE_SVC_AUDIO_PACS_CHR_UUID16_SUPPORTED_AUDIO_CONTEXTS      0x2BCE


struct ble_svc_audio_pacs_set_param {
    /* Supported Audio Locations */
    uint32_t audio_locations;

    /* Supported Contexts */
    uint16_t supported_contexts;
};

/**
 * @brief Set PACS params.
 *
 * Set device capabilities reported in Published Audio Capabilities Service.
 *
 * @param[in] flags                     Flags that define if capabilities being set are for
 *                                      Sink or Source. Valid values are either
 *                                      `BLE_AUDIO_CODEC_FLAG_SOURCE` or `BLE_AUDIO_CODEC_FLAG_SINK`
 * @param[in] param                     Pointer to a `ble_svc_audio_pacs_set_param`
 *                                      structure that defines capabilities supported by
 *                                      device.
 *
 * @return                              0 on success;
 *                                      A non-zero value on failure.
 */
int ble_svc_audio_pacs_set(uint8_t flags, struct ble_svc_audio_pacs_set_param *param);

/**
 * @brief Set available context types.
 *
 * @param[in] conn_handle               Connection handle identifying connection for which contexts
 *                                      being set
 * @param[in] sink_contexts             Available Sink Contexts
 * @param[in] source_contexts           Available Source Contexts
 *
 * @return                              0 on success;
 *                                      A non-zero value on failure.
 */
int ble_svc_audio_pacs_avail_contexts_set(uint16_t conn_handle,
                                          uint16_t sink_contexts,
                                          uint16_t source_contexts);

/**
 * @}
 */

#endif /* H_BLE_AUDIO_SVC_PACS_ */
