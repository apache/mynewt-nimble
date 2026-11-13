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

/**
 * @struct ble_ll_iso_data_path_setup_params
 *
 * Parameters of the LE Setup ISO Data Path command passed to a data path.
 */
struct ble_ll_iso_data_path_setup_params {
    /** Connection handle of the CIS or BIS. */
    uint16_t conn_handle;

    /**
     * Data path direction:
     * - `BLE_HCI_ISO_DATA_PATH_DIR_INPUT`: Host to Controller,
     * - `BLE_HCI_ISO_DATA_PATH_DIR_OUTPUT`: Controller to Host.
     */
    uint8_t dir;

    /** Codec ID: coding format, company ID and vendor specific codec ID. */
    uint8_t codec_id[5];

    /** Controller delay in microseconds. */
    uint32_t controller_delay;

    /** Length of the codec configuration in octets. */
    uint8_t codec_config_len;

    /**
     * Codec configuration. Valid only during the setup callback, so it shall
     * be copied if needed later.
     */
    const uint8_t *codec_config;
};

/**
 * @struct ble_ll_iso_data_path_ops
 *
 * Interface structure for ISO data path callbacks.
 */
struct ble_ll_iso_data_path_ops {
    /**
     * @brief Callback function for setting up the data path (optional).
     *
     * Called on LE Setup ISO Data Path for a direction of a CIS or BIS.
     *
     * @param data_path_id The data path ID.
     * @param params Parameters of the LE Setup ISO Data Path command.
     *
     * @return 0 on success, HCI error code (BLE_ERR_*) returned for the
     *         command otherwise.
     */
    int (*setup)(uint8_t data_path_id,
                 const struct ble_ll_iso_data_path_setup_params *params);

    /**
     * @brief Callback function for removing the data path (optional).
     *
     * Called on LE Remove ISO Data Path, and when the CIS or BIS is
     * terminated. In the latter case the connection handle can no longer be
     * looked up.
     *
     * @param data_path_id The data path ID.
     * @param conn_handle The connection handle of the CIS or BIS.
     * @param dir The data path direction.
     */
    void (*remove)(uint8_t data_path_id, uint16_t conn_handle, uint8_t dir);

    /**
     * @brief Callback function for ISO SDU (Service Data Unit) output.
     *
     * Mandatory for the Controller to Host direction.
     *
     * @param data_path_id The data path ID.
     * @param conn_handle The connection handle associated with the received SDU.
     * @param om Pointer to the `os_mbuf` structure containing the SDU data.
     *           Can be `NULL` if the SDU is considered as lost.
     * @param timestamp Timestamp associated with the received SDU.
     * @param seq_num Sequence number of the SDU.
     * @param valid Status of the SDU reception.
     *              - `true`: SDU was received successfully, and `om` contains valid data.
     *              - `false`: An error occurred during processing, but partial or corrupted
     *                         SDU data may be available in `om`.
     */
    void (*sdu_out)(uint8_t data_path_id, uint16_t conn_handle,
                    const struct os_mbuf *om, uint32_t timestamp,
                    uint16_t seq_num, bool valid);
};

/**
 * @struct ble_ll_iso_data_path
 *
 * ISO data path, identified by its data path ID.
 */
struct ble_ll_iso_data_path {
    /**
     * Data path ID: 0x00 is the HCI data path, registered by the controller,
     * 0x01-0xFE are vendor specific.
     */
    uint8_t id;

    /** Data path callbacks. */
    const struct ble_ll_iso_data_path_ops *ops;

    STAILQ_ENTRY(ble_ll_iso_data_path) next;
};

/**
 * @brief Registers an ISO data path.
 *
 * The data path can then be used by LE Setup ISO Data Path with its ID. Data
 * paths shall be registered after the controller is initialized. The data
 * path structure shall stay valid as long as the controller runs.
 *
 * @param dp The data path to register.
 *
 * @return 0 on success;
 *         -EINVAL if the data path ID is reserved or the callbacks are missing;
 *         -EALREADY if a data path with this ID is already registered.
 */
int ble_ll_iso_data_path_register(struct ble_ll_iso_data_path *dp);

#ifdef __cplusplus
}
#endif

#endif
