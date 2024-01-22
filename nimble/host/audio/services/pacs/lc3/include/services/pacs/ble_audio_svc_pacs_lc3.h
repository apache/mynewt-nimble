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

#ifndef H_BLE_AUDIO_SVC_PACS_LC3_
#define H_BLE_AUDIO_SVC_PACS_LC3_


/**
 * @file ble_audio_svc_pacs_lc3.h
 *
 * @brief Bluetooth PAC Service for LC3 Codec
 *
 * This header file provides the public API for interacting with the PACS LC3 package, that
 * registers PAC entry for LC3 codec with configurations contained in system configuration file
 *
 * @defgroup ble_audio_svc_pacs_lc3 Bluetooth LE Audio PACS LC3 package
 * @ingroup ble_audio_svc_pacs
 * @{
 *
 * This package is an example how to register codec entry that PACS can use to construct its entries
 * for GATT database. This is high level package that can be used to construct basic PACS setup for
 * LC3 codec. This package creates only single PAC entry per source and sink. If more PAC entries
 * need to be created, with more advanced setup, @ref ble_audio_svc_pacs service shall be used in
 * combination with @ref ble_audio_codec API.
 *
 */

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
int ble_svc_audio_pacs_lc3_set_avail_contexts(uint16_t conn_handle,
                                              uint16_t sink_contexts,
                                              uint16_t source_contexts);

/**
 * @}
 */

#endif /* H_BLE_AUDIO_SVC_PACS_LC3_ */
