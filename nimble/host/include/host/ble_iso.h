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

#ifndef H_BLE_ISO_
#define H_BLE_ISO_
#include <inttypes.h>

#include "nimble/hci_common.h"
#include "syscfg/syscfg.h"

/** ISO event: BIG Create Completed */
#define BLE_ISO_EVENT_BIG_CREATE_COMPLETE                   0

/** ISO event: BIG Terminate Completed */
#define BLE_ISO_EVENT_BIG_TERMINATE_COMPLETE                1

/** ISO event: BIG Sync Established */
#define BLE_ISO_EVENT_BIG_SYNC_ESTABLISHED                  2

/** ISO event: BIG Sync Terminated */
#define BLE_ISO_EVENT_BIG_SYNC_TERMINATED                   3

/** ISO event: ISO Data received */
#define BLE_ISO_EVENT_ISO_RX                                4

/** @brief Broadcast Isochronous Group (BIG) description */
struct ble_iso_big_desc {
    uint8_t big_handle;
    uint32_t big_sync_delay;
    uint32_t transport_latency_big;
    uint8_t nse;
    uint8_t bn;
    uint8_t pto;
    uint8_t irc;
    uint16_t max_pdu;
    uint16_t iso_interval;
    uint8_t num_bis;
    uint16_t conn_handle[MYNEWT_VAL(BLE_MAX_BIS)];
};

/** @brief Received ISO Data status possible values */
enum ble_iso_rx_data_status {
    /** The complete SDU was received correctly. */
    BLE_ISO_DATA_STATUS_VALID = BLE_HCI_ISO_PKT_STATUS_VALID,

    /** May contain errors or part of the SDU may be missing. */
    BLE_ISO_DATA_STATUS_ERROR = BLE_HCI_ISO_PKT_STATUS_INVALID,

    /** Part(s) of the SDU were not received correctly */
    BLE_ISO_DATA_STATUS_LOST = BLE_HCI_ISO_PKT_STATUS_LOST,
};

/** @brief Received ISO data info structure */
struct ble_iso_rx_data_info {
    /** ISO Data timestamp. Valid if @ref ble_iso_data_info.ts_valid is set */
    uint32_t ts;

    /** Packet sequence number */
    uint16_t seq_num;

    /** SDU length */
    uint16_t sdu_len : 12;

    /** ISO Data status. See @ref ble_iso_data_status */
    uint16_t status : 2;

    /** Timestamp is valid */
    uint16_t ts_valid : 1;
};

/**
 * Represents a ISO-related event.  When such an event occurs, the host
 * notifies the application by passing an instance of this structure to an
 * application-specified callback.
 */
struct ble_iso_event {
    /**
     * Indicates the type of ISO event that occurred.  This is one of the
     * BLE_ISO_EVENT codes.
     */
    uint8_t type;

    /**
     * A discriminated union containing additional details concerning the ISO
     * event.  The 'type' field indicates which member of the union is valid.
     */
    union {
        /**
         * Represents a completion of BIG creation. Valid for the following
         * event types:
         *     o BLE_ISO_EVENT_BIG_CREATE_COMPLETE
         */
        struct {
            struct ble_iso_big_desc desc;
            uint8_t status;
            uint8_t phy;
        } big_created;

        /**
         * Represents a completion of BIG termination. Valid for the following
         * event types:
         *     o BLE_ISO_EVENT_BIG_TERMINATE_COMPLETE
         *     o BLE_ISO_EVENT_BIG_SYNC_TERMINATED
         */
        struct {
            uint16_t big_handle;
            uint8_t reason;
        } big_terminated;

        /**
         * Represents a completion of BIG synchronization. Valid for the following
         * event types:
         *     o BLE_ISO_EVENT_BIG_SYNC_ESTABLISHED
         */
        struct {
            struct ble_iso_big_desc desc;
            uint8_t status;
        } big_sync_established;

        /**
         * Represents a reception of ISO Data. Valid for the following
         * event types:
         *     o BLE_ISO_EVENT_ISO_RX
         */
        struct {
            uint16_t conn_handle;
            const struct ble_iso_rx_data_info *info;
            struct os_mbuf *om;
        } iso_rx;
    };
};

typedef int ble_iso_event_fn(struct ble_iso_event *event, void *arg);

struct ble_iso_big_params {
    uint32_t sdu_interval;
    uint16_t max_sdu;
    uint16_t max_transport_latency;
    uint8_t rtn;
    uint8_t phy;
    uint8_t packing;
    uint8_t framing;
    uint8_t encryption;
    const char *broadcast_code;
};

struct ble_iso_create_big_params {
    uint8_t adv_handle;
    uint8_t bis_cnt;
    ble_iso_event_fn *cb;
    void *cb_arg;
};

int ble_iso_create_big(const struct ble_iso_create_big_params *create_params,
                       const struct ble_iso_big_params *big_params);

int ble_iso_terminate_big(uint8_t big_handle);

/** @brief BIS parameters for @ref ble_iso_big_sync_create */
struct ble_iso_bis_params {
    /** BIS index */
    uint8_t bis_index;

    /** The callback to associate with the BIS.
     *  Received ISO data is reported through this callback.
     */
    ble_iso_event_fn *cb;

    /** The optional argument to pass to the callback function */
    void *cb_arg;
};

/** @brief BIG Sync parameters for @ref ble_iso_big_sync_create */
struct ble_iso_big_sync_create_params {
    /** Periodic advertising train sync handle */
    uint16_t sync_handle;

    /** Null-terminated broadcast code for encrypted BIG or
     *  NULL if the BIG is unencrypted
     */
    const char *broadcast_code;

    /** Maximum Subevents to be used to receive data payloads in each BIS event */
    uint8_t mse;

    /** The maximum permitted time between successful receptions of BIS PDUs */
    uint16_t sync_timeout;

    /** The callback to associate with this sync procedure.
     *  Sync establishment and termination are reported through this callback.
     */
    ble_iso_event_fn *cb;

    /** The optional argument to pass to the callback function */
    void *cb_arg;

    /** Number of a BISes */
    uint8_t bis_cnt;

    /** BIS parameters */
    struct ble_iso_bis_params *bis_params;
};

/**
 * @brief Synchronize to Broadcast Isochronous Group (BIG)
 *
 * This function is used to synchronize to a BIG described in the periodic
 * advertising train specified by the @p param->pa_sync_handle parameter.
 *
 * @param[in] params            BIG synchronization parameters
 * @param[out] big_handle       BIG instance handle
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_iso_big_sync_create(const struct ble_iso_big_sync_create_params *params,
                            uint8_t *big_handle);

/**
 * @brief Terminate Broadcast Isochronous Group (BIG) sync
 *
 * This function is used to stop synchronizing or cancel the process of
 * synchronizing to the BIG identified by the @p big_handle parameter.
 * The command also terminates the reception of BISes in the BIG.
 *
 * @param[in] big_handle        BIG handle
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_iso_big_sync_terminate(uint8_t big_handle);

/** @brief ISO Data direction */
enum ble_iso_data_dir {
    BLE_ISO_DATA_DIR_TX,
    BLE_ISO_DATA_DIR_RX,
};

/** @brief ISO Codec ID */
struct ble_iso_codec_id {
    /** Coding Format */
    uint8_t format;

    /** Company ID */
    uint16_t company_id;

    /** Vendor Specific Codec ID */
    uint16_t vendor_specific;
};

/** @brief Setup ISO Data Path parameters */
struct ble_iso_data_path_setup_params {
    /** Connection handle of the CIS or BIS */
    uint16_t conn_handle;

    /** Data path direction */
    enum ble_iso_data_dir data_path_dir;

    /** Data path ID. 0x00 for HCI */
    uint8_t data_path_id;

    /** Controller delay */
    uint32_t ctrl_delay;

    /** Codec ID */
    struct ble_iso_codec_id codec_id;

    /** Codec Configuration Length */
    uint8_t codec_config_len;

    /** Codec Configuration */
    const uint8_t *codec_config;
};

/**
 * @brief Setup ISO Data Path
 *
 * This function is used to identify and create the isochronous data path
 * between the Host and the Controller for a CIS, CIS configuration, or BIS
 * identified by the @p param->conn_handle parameter.
 *
 * @param[in] params            BIG synchronization parameters
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_iso_data_path_setup(const struct ble_iso_data_path_setup_params *param);

/** @brief @brief Remove ISO Data Path parameters */
struct ble_iso_data_path_remove_params {
    /** Connection handle of the CIS or BIS */
    uint16_t conn_handle;

    /** Data path direction */
    enum ble_iso_data_dir data_path_dir;
};

/**
 * @brief Remove ISO Data Path
 *
 * This function is used to remove the input and/or output data path(s)
 * associated with a CIS, CIS configuration, or BIS identified by the
 * @p param->conn_handle parameter.
 *
 * @param[in] params            BIG synchronization parameters
 *
 * @return                      0 on success;
 *                              A non-zero value on failure.
 */
int ble_iso_data_path_remove(const struct ble_iso_data_path_remove_params *param);

int ble_iso_tx(uint16_t conn_handle, void *data, uint16_t data_len);

int ble_iso_init(void);

#endif /* H_BLE_ISO_ */
