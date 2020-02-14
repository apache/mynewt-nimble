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
#include "syscfg/syscfg.h"
#include <inttypes.h>

/** ISO event: BIG Create Completed */
#define BLE_ISO_EVENT_BIG_CREATE_COMPLETE                  0

/** ISO event: BIG Terminate Completed */
#define BLE_ISO_EVENT_BIG_TERMINATE_COMPLETE               1

/** ISO event: Connection Request */
#define BLE_ISO_EVENT_CONNECT_REQUEST                      2

/** ISO event: CIS Connection Established */
#define BLE_ISO_EVENT_CIS_ESTABLISHED                      3

/** ISO event: CIS Connection Terminated */
#define BLE_ISO_EVENT_CIS_DISCONNECTED                     4

/** ISO event: ISO Data Received */
#define BLE_ISO_EVENT_DATA                                 5

struct ble_iso_big_desc
{
    uint8_t big_handle;
    uint32_t big_sync_delay;
    uint32_t transport_latency_big;
    uint8_t phy;
    uint8_t nse;
    uint8_t bn;
    uint8_t pto;
    uint8_t irc;
    uint16_t max_pdu;
    uint16_t iso_interval;
    uint8_t num_bis;
    uint16_t conn_handle[MYNEWT_VAL(BLE_MAX_BIS)];
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
        } big_created;

        /**
         * Represents a completion of BIG termination. Valid for the following
         * event types:
         *     o BLE_ISO_EVENT_BIG_TERMINATE_COMPLETE
         */
        struct {
            uint16_t big_handle;
            uint8_t reason;
        } big_terminated;

        /* Represents a reception of connection request to CIS. Valid for the
         * following event types:
         *     o BLE_ISO_CONNECT_REQUEST_EVENT
         */
        struct {
            uint16_t conn_handle;
            uint16_t cis_handle;
        } cis_connect_req;

        /* Represents an establishment of connection to CIS. Valid for the
         * following event types:
         *     o BLE_ISO_CIS_ESTABLISHED_EVENT
         */
        struct {
            uint8_t status;
            uint16_t cis_handle;
        } cis_established;

        /* Represents a termination of connection to CIS. Valid for the
         * following event types:
         *     o BLE_ISO_CIS_DISCONNECTED_EVENT
         */
        struct {
            uint8_t status;
            uint16_t cis_handle;
        } cis_disconnected;

        /* Represents sync established with BIG. Valid for the following
         * event types:
         *     o BLE_ISO_BIG_ESTABLISHED_EVENT
         */
        struct {
            uint8_t status;
            uint8_t big_handle;
            uint8_t bis_cnt;
            uint16_t bis[0];
        } big_complete;

        /* Represents sync lost with BIG. Valid for the following
         * event types:
         *     o BLE_ISO_BIG_SYNC_LOST_EVENT
         */
        struct {
            uint8_t big_handle;
            uint8_t reason;
        } big_sync_lost;

        /* BLE_ISO_DATA_EVENT - for both cis/bis_handles */
        struct {
            struct os_mbuf *om;
            uint16_t handle;
        } iso_data;
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

struct ble_iso_cig_params {
    uint32_t sdu_c_to_p_itvl;
    uint32_t sdu_p_to_c_itvl;
    uint8_t sca;
    uint8_t packing;
    uint8_t framing;
    uint8_t max_c_to_p_latency;
    uint8_t max_p_to_c_latency;
#if MYNEWT_VAL(BLE_ISO_TEST)
    uint8_t ft_c_to_p;
    uint8_t ft_p_to_c;
    uint16_t iso_itvl;
#endif
};

struct ble_iso_cis_params {
    uint16_t max_sdu_c_to_p;
    uint16_t max_sdu_p_to_c;
    uint8_t phy_c_to_p;
    uint8_t phy_p_to_c;
    uint8_t rnt_c_to_p;
    uint8_t rnt_p_to_c;
};

int ble_iso_create_big(const struct ble_iso_create_big_params *create_params,
                       const struct ble_iso_big_params *big_params);

int ble_iso_terminate_big(uint8_t big_id);

void
ble_gap_rx_create_big_complete(const struct
                               ble_hci_ev_le_subev_create_big_complete *ev);
void
ble_gap_rx_terminate_big_complete(const struct
                                  ble_hci_ev_le_subev_terminate_big_complete
                                  *ev);

int ble_iso_client_create_cig(struct ble_iso_cig_params *cig_params,
                              uint8_t cis_cnt,
                              struct ble_iso_cis_params *cis_params,
                              uint8_t *cig, uint8_t *cis_handles);

int ble_iso_client_remove_group(uint8_t cig_id);

int ble_iso_client_create_cis(uint8_t cig, uint8_t cis_cnt,
                              struct ble_hci_le_create_cis_params *params,
                              ble_iso_event_fn *cb, void *cb_arg);

int ble_iso_tx(uint16_t conn_handle, void *data, uint16_t data_len);

int ble_iso_init(void);

#endif
