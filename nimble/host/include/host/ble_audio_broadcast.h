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

#ifndef H_BLE_AUDIO_BROADCAST_
#define H_BLE_AUDIO_BROADCAST_

#include <stdint.h>
#include "host/ble_gap.h"
#include "host/ble_iso.h"
#include "host/ble_audio_common.h"

struct ble_broadcast_create_params {
    /** Broadcast Audio Source Endpoint */
    struct ble_audio_base *base;

    /** Parameters used to configure Extended advertising */
    struct ble_gap_ext_adv_params *extended_params;

    /** Parameters used to configure Periodic advertising */
    struct ble_gap_periodic_adv_params *periodic_params;

    /** Broadcast name - null terminated.
     * Set NULL to not include in advertising.
     * Length must be in range of 4 to 32 chars.
     */
    const char *name;

    /** Advertising instance */
    uint8_t adv_instance;

    /** BIG parameters */
    struct ble_iso_big_params *big_params;

    /** Additional data to include in Extended Advertising  */
    uint8_t *svc_data;

    /** Additional data length  */
    uint16_t svc_data_len;
};

struct ble_broadcast_update_params {
    /** Broadcast name - null terminated.
     * Set NULL to not include in advertising
     */
    const char *name;

    /** Advertising instance */
    uint8_t adv_instance;

    /** Additional data to include in Extended Advertising  */
    uint8_t *svc_data;

    /** Additional data length  */
    uint16_t svc_data_len;

    /** Broadcast ID */
    uint32_t broadcast_id;
};

typedef int ble_audio_broadcast_destroy_fn(struct ble_audio_base *base,
                                           void *args);

/**
 * @brief Create Broadcast Audio Source Endpoint and configure advertising
 * instance
 *
 * This function configures advertising instance for extended and periodic
 * advertisements to be ready for broadcast with BASE configuration.
 *
 * @param[in] params            Pointer to a `ble_broadcast_base_params`
 *                              structure that defines BASE, extended
 *                              advertising and periodic advertising
 *                              configuration.
 * @param[in] destroy_cb        Optional callback to be invoked on
 *                              `ble_audio_broadcast_destroy` call.
 * @param[in] args              Optional arguments to be passed to `destroy_cb`
 * @param[in] gap_cb            GAP event callback to be associated with BASE
 *                              advertisement.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_broadcast_create(const struct ble_broadcast_create_params
                               *params,
                               ble_audio_broadcast_destroy_fn *destroy_cb,
                               void *args,
                               ble_gap_event_fn *gap_cb);

/**
 * @brief Start advertisements for given BASE configuration
 *
 * This function starts BASE advertisement by enabling extended,  periodic
 * and BIGInfo advertisements for this instance.
 *
 * @param[in] adv_instance      Advertising instance used by broadcast.
 * @param[in] cb                Pointer to an ISO event handler.
 * @param[in] cb_arg            Arguments to an ISO event handler.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_broadcast_start(uint8_t adv_instance,
                              ble_iso_event_fn *cb, void *cb_arg);

/**
 * @brief Stop advertisements for given BASE configuration
 *
 * This function stops BASE advertisement by disabling extended and periodic
 * advertising. Advertising instance is still configured and ready for resume.
 *
 * @param[in] adv_instance      Advertising instance used by broadcast.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_broadcast_stop(uint8_t adv_instance);

/**
 * @brief Destroy advertisements for given BASE configuration
 *
 * This function terminates BASE advertisement instance.
 * After return advertising instance is free and must be configured again
 * for future advertisements.
 *
 * @param[in] adv_instance      Advertising instance used by broadcast.
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_broadcast_destroy(uint8_t adv_instance);

/**
 * @brief Update advertisements for given BASE configuration
 *
 * This function updates extended advertisements.
 *
 * @param[in] params            Pointer to structure with new advertising data
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_broadcast_update(const struct ble_broadcast_update_params
                               *params);

/** BIG Subgroup parameters */
struct ble_broadcast_subgroup_params {
    /** Subgroup level Codec information */
    struct ble_audio_codec_id *codec_id;

    /** Subgroup level Codec Specific Configuration */
    uint8_t *codec_spec_config;

    /** Subgroup level Codec Specific Configuration length */
    uint8_t codec_spec_config_len;

    /** Subgroup Metadata */
    uint8_t *metadata;

    /** Subgroup Metadata length*/
    uint8_t metadata_len;
};

/**
 * @brief Build BIG subgroup structure
 *
 * This is a helper function can be used to fill out `ble_audio_big_subgroup`
 * structure. Created subgroup extends subgroup list in provided BASE.
 * This function increases `num_subgroups` in BASE structure.
 *
 * @param[in/out] base          Pointer to a `ble_audio_base` structure,
 *                              that will be extended by the new subgroup.
 *                              In case of error, filled out data may be
 *                              erroneous.
 * @param[out] subgroup         Pointer to a `ble_audio_big_subgroup`
 *                              structure, that will be filled out with
 *                              supplied configuration. In case of error,
 *                              filled out data may be erroneous.
 * @param[in] params            Pointer to a `ble_broadcast_subgroup_params`
 *                              structure, containing information about new
 *                              subgroup
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_broadcast_build_sub(struct ble_audio_base *base,
                                  struct ble_audio_big_subgroup *subgroup,
                                  const struct ble_broadcast_subgroup_params
                                  *params);

/** BIS parameters */
struct ble_broadcast_bis_params {
    /** BIS index */
    uint8_t idx;

    /** BIS level Codec Specific Configuration */
    uint8_t *codec_spec_config;

    /** BIS level Codec Specific Configuration length */
    uint8_t codec_spec_config_len;
};

/**
 * @brief Build BIS structure
 *
 * This is a helper function can be used to fill out `ble_broadcast_bis`
 * structure. Created BIS extends BIS list in provided subgroup.
 * This function increases `bis_cnt` in subgroup structure.
 *
 * @param[in/out] subgroup      Pointer to a updated `ble_audio_big_subgroup`
 *                              structure, that will be extended by the new
 *                              BIS.  In case of error, filled out data may be
 *                              erroneous.
 * @param[out] bis              Pointer to a `ble_broadcast_bis`
 *                              structure, that will be filled out with
 *                              supplied configuration. In case of error,
 *                              filled out data may be erroneous.
 * @param[in] params            Pointer to a `ble_broadcast_bis_params`
 *                              structure, containing information about new
 *                              BIS
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_audio_broadcast_build_bis(struct ble_audio_big_subgroup *subgroup,
                                  struct ble_audio_bis *bis,
                                  const struct ble_broadcast_bis_params
                                  *params);

int ble_audio_broadcast_init(void);
#endif
