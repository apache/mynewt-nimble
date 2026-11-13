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

#ifndef H_BLE_LL_ISO_
#define H_BLE_LL_ISO_

#include <stdint.h>
#include <os/os_mbuf.h>
#include <syscfg/syscfg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HCI command handlers */
int ble_ll_iso_read_tx_sync(const uint8_t *cmdbuf, uint8_t len, uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_iso_read_link_quality(const uint8_t *cmdbuf, uint8_t len,
                                 uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_iso_set_cig_param(const uint8_t *cmdbuf, uint8_t len, uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_iso_set_cig_param_test(const uint8_t *cmdbuf, uint8_t len, uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_iso_create_cis(const uint8_t *cmdbuf, uint8_t len);
int ble_ll_iso_remove_cig(const uint8_t *cmdbuf, uint8_t len, uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_iso_accept_cis_req(const uint8_t *cmdbuf, uint8_t len);
int ble_ll_iso_reject_cis_req(const uint8_t *cmdbuf, uint8_t len);
int ble_ll_iso_create_big(const uint8_t *cmdbuf, uint8_t len);
int ble_ll_iso_create_big_test(const uint8_t *cmdbuf, uint8_t len);
int ble_ll_iso_terminate_big(const uint8_t *cmdbuf, uint8_t len);
int ble_ll_iso_big_create_sync(const uint8_t *cmdbuf, uint8_t len,
                               uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_iso_big_terminate_sync(const uint8_t *cmdbuf, uint8_t len);
int ble_ll_iso_setup_iso_data_path(const uint8_t *cmdbuf, uint8_t len, uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_iso_remove_iso_data_path(const uint8_t *cmdbuf, uint8_t len, uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_iso_transmit_test(const uint8_t *cmdbuf, uint8_t len, uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_iso_receive_test(const uint8_t *cmdbuf, uint8_t len,
                            uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_iso_read_counters_test(const uint8_t *cmdbuf, uint8_t len,
                                  uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_iso_end_test(const uint8_t *cmdbuf, uint8_t len, uint8_t *rspbuf, uint8_t *rsplen);

void ble_ll_iso_init(void);
void ble_ll_iso_reset(void);

/* HCI ISO Data SDU handler */
int ble_ll_hci_iso_data_in(struct os_mbuf *om);

#if MYNEWT_VAL(BLE_LL_ISO)

/** Parameters of LE Setup ISO Data Path passed to a data path. */
struct ble_ll_iso_data_path_setup_params {
    /** CIS or BIS connection handle. */
    uint16_t conn_handle;
    /** Data path direction (BLE_HCI_ISO_DATA_PATH_DIR_*). */
    uint8_t dir;
    /** Codec ID. */
    uint8_t codec_id[5];
    /** Controller delay in microseconds. */
    uint32_t controller_delay;
    /** Codec configuration length. */
    uint8_t codec_config_len;
    /** Codec configuration, valid only during the setup callback. */
    const uint8_t *codec_config;
};

/** ISO data path callbacks. */
struct ble_ll_iso_data_path_ops {
    /**
     * @brief Sets up the data path (optional).
     *
     * @param data_path_id Data path ID.
     * @param params LE Setup ISO Data Path parameters.
     *
     * @return 0 on success, HCI error code otherwise.
     */
    int (*setup)(uint8_t data_path_id,
                 const struct ble_ll_iso_data_path_setup_params *params);

    /**
     * @brief Removes the data path (optional).
     *
     * Called also when the CIS or BIS is terminated.
     *
     * @param data_path_id Data path ID.
     * @param conn_handle CIS or BIS connection handle.
     * @param dir Data path direction.
     */
    void (*remove)(uint8_t data_path_id, uint16_t conn_handle, uint8_t dir);

    /**
     * @brief Delivers a received SDU, mandatory for the output direction.
     *
     * @param data_path_id Data path ID.
     * @param conn_handle CIS or BIS connection handle.
     * @param om SDU data, NULL if the SDU is lost.
     * @param timestamp SDU timestamp.
     * @param seq_num SDU sequence number.
     * @param valid false if the SDU was received with errors.
     */
    void (*sdu_out)(uint8_t data_path_id, uint16_t conn_handle,
                    const struct os_mbuf *om, uint32_t timestamp,
                    uint16_t seq_num, bool valid);
};

/**
 * ISO data path. In the input direction its user passes SDUs to the
 * controller with ble_ll_hci_iso_data_in().
 */
struct ble_ll_iso_data_path {
    /** Data path ID, 0x01-0xFE for vendor specific data paths. */
    uint8_t id;
    /** Data path callbacks. */
    const struct ble_ll_iso_data_path_ops *ops;

    STAILQ_ENTRY(ble_ll_iso_data_path) next;
};

/**
 * @brief Registers an ISO data path.
 *
 * The data path shall stay valid as long as the controller runs.
 *
 * @param dp Data path to register.
 *
 * @return 0 on success or if already registered, -EINVAL if the ID is
 *         reserved or ops is NULL, -EALREADY if the ID is taken.
 */
int ble_ll_iso_data_path_register(struct ble_ll_iso_data_path *dp);

#endif /* BLE_LL_ISO */

#ifdef __cplusplus
}
#endif

#endif
