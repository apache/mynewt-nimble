/**
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

#ifndef H_BLE_GAP_
#define H_BLE_GAP_

#include <inttypes.h>
#include "host/ble_hs.h"
struct hci_le_conn_complete;
struct hci_conn_update;

/** 30 ms. */
#define BLE_GAP_ADV_FAST_INTERVAL1_MIN      (30 * 1000 / BLE_HCI_ADV_ITVL)

/** 60 ms. */
#define BLE_GAP_ADV_FAST_INTERVAL1_MAX      (60 * 1000 / BLE_HCI_ADV_ITVL)

/** 100 ms. */
#define BLE_GAP_ADV_FAST_INTERVAL2_MIN      (100 * 1000 / BLE_HCI_ADV_ITVL)

/** 150 ms. */
#define BLE_GAP_ADV_FAST_INTERVAL2_MAX      (150 * 1000 / BLE_HCI_ADV_ITVL)

/** 30 ms; active scanning. */
#define BLE_GAP_SCAN_FAST_INTERVAL_MIN      (30 * 1000 / BLE_HCI_ADV_ITVL)

/** 60 ms; active scanning. */
#define BLE_GAP_SCAN_FAST_INTERVAL_MAX      (60 * 1000 / BLE_HCI_ADV_ITVL)

/** 11.25 ms; limited discovery interval. */
#define BLE_GAP_LIM_DISC_SCAN_INT           (11.25 * 1000 / BLE_HCI_SCAN_ITVL)

/** 11.25 ms; limited discovery window (not from the spec). */
#define BLE_GAP_LIM_DISC_SCAN_WINDOW        (11.25 * 1000 / BLE_HCI_SCAN_ITVL)

/** 30 ms; active scanning. */
#define BLE_GAP_SCAN_FAST_WINDOW            (30 * 1000 / BLE_HCI_SCAN_ITVL)

/* 30.72 seconds; active scanning. */
#define BLE_GAP_SCAN_FAST_PERIOD            (30.72 * 1000)

/** 1.28 seconds; background scanning. */
#define BLE_GAP_SCAN_SLOW_INTERVAL1         (1280 * 1000 / BLE_HCI_SCAN_ITVL)

/** 11.25 ms; background scanning. */
#define BLE_GAP_SCAN_SLOW_WINDOW1           (11.25 * 1000 / BLE_HCI_SCAN_ITVL)

/** 10.24 seconds. */
#define BLE_GAP_DISC_DUR_DFLT               (10.24 * 1000)

/** 1 second. */
#define BLE_GAP_CONN_PAUSE_CENTRAL          (1 * 1000)

/** 5 seconds. */
#define BLE_GAP_CONN_PAUSE_PERIPHERAL       (5 * 1000)

/* 30 ms. */
#define BLE_GAP_INITIAL_CONN_ITVL_MIN       (30 * 1000 / BLE_HCI_CONN_ITVL)

/* 50 ms. */
#define BLE_GAP_INITIAL_CONN_ITVL_MAX       (50 * 1000 / BLE_HCI_CONN_ITVL)

#define BLE_GAP_ADV_DFLT_CHANNEL_MAP        0x07 /* All three channels. */

#define BLE_GAP_INITIAL_CONN_LATENCY        0
#define BLE_GAP_INITIAL_SUPERVISION_TIMEOUT 0x0100
#define BLE_GAP_INITIAL_CONN_MIN_CE_LEN     0x0010
#define BLE_GAP_INITIAL_CONN_MAX_CE_LEN     0x0300

#define BLE_GAP_SVC_UUID16                              0x1800
#define BLE_GAP_CHR_UUID16_DEVICE_NAME                  0x2a00
#define BLE_GAP_CHR_UUID16_APPEARANCE                   0x2a01
#define BLE_GAP_CHR_UUID16_PERIPH_PRIV_FLAG             0x2a02
#define BLE_GAP_CHR_UUID16_RECONNECT_ADDR               0x2a03
#define BLE_GAP_CHR_UUID16_PERIPH_PREF_CONN_PARAMS      0x2a04

#define BLE_GAP_APPEARANCE_GEN_COMPUTER                 128

#define BLE_GAP_ADDR_TYPE_WL                0xff
#define BLE_GAP_ADDR_TYPE_NONE              0xfe

#define BLE_GAP_ROLE_MASTER                 0
#define BLE_GAP_ROLE_SLAVE                  1

#define BLE_GAP_EVENT_CONNECT               0
#define BLE_GAP_EVENT_DISCONNECT            1
#define BLE_GAP_EVENT_CONN_CANCEL           2
#define BLE_GAP_EVENT_CONN_UPDATE           3
#define BLE_GAP_EVENT_CONN_UPDATE_REQ       4
#define BLE_GAP_EVENT_L2CAP_UPDATE_REQ      5
#define BLE_GAP_EVENT_TERM_FAILURE          6
#define BLE_GAP_EVENT_DISC                  7
#define BLE_GAP_EVENT_DISC_COMPLETE         8
#define BLE_GAP_EVENT_ADV_COMPLETE          9
#define BLE_GAP_EVENT_ENC_CHANGE            10
#define BLE_GAP_EVENT_PASSKEY_ACTION        11
#define BLE_GAP_EVENT_NOTIFY                12

struct ble_gap_sec_state {
    unsigned encrypted:1;
    unsigned authenticated:1;
    unsigned bonded:1;
};

/**
 * @param discoverable_mode     One of the following constants:
 *                                  o BLE_GAP_DISC_MODE_NON
 *                                      (non-discoverable; 3.C.9.2.2).
 *                                  o BLE_GAP_DISC_MODE_LTD
 *                                      (limited-discoverable; 3.C.9.2.3).
 *                                  o BLE_GAP_DISC_MODE_GEN
 *                                      (general-discoverable; 3.C.9.2.4).
 * @param connectable_mode      One of the following constants:
 *                                  o BLE_GAP_CONN_MODE_NON
 *                                      (non-connectable; 3.C.9.3.2).
 *                                  o BLE_GAP_CONN_MODE_DIR
 *                                      (directed-connectable; 3.C.9.3.3).
 *                                  o BLE_GAP_CONN_MODE_UND
 *                                      (undirected-connectable; 3.C.9.3.4).
 */
struct ble_gap_adv_params {
    /*** Mandatory fields. */
    uint8_t conn_mode;
    uint8_t disc_mode;

    /*** Optional fields; assign 0 to make the stack calculate them. */
    uint16_t itvl_min;
    uint16_t itvl_max;
    uint8_t channel_map;
    uint8_t filter_policy;
    uint8_t high_duty_cycle:1;
};

struct ble_gap_conn_desc {
    struct ble_gap_sec_state sec_state;
    uint8_t peer_ota_addr[6];
    uint8_t peer_id_addr[6];
    uint8_t our_id_addr[6];
    uint8_t our_ota_addr[6];
    uint16_t conn_handle;
    uint16_t conn_itvl;
    uint16_t conn_latency;
    uint16_t supervision_timeout;
    uint8_t peer_ota_addr_type;
    uint8_t peer_id_addr_type;
    uint8_t our_id_addr_type;
    uint8_t our_ota_addr_type;
    uint8_t role;
    uint8_t master_clock_accuracy;
};

struct ble_gap_conn_params {
    uint16_t scan_itvl;
    uint16_t scan_window;
    uint16_t itvl_min;
    uint16_t itvl_max;
    uint16_t latency;
    uint16_t supervision_timeout;
    uint16_t min_ce_len;
    uint16_t max_ce_len;
};

struct ble_gap_disc_params {
    uint16_t itvl;
    uint16_t window;
    uint8_t filter_policy;
    uint8_t limited:1;
    uint8_t passive:1;
    uint8_t filter_duplicates:1;
};

struct ble_gap_upd_params {
    uint16_t itvl_min;
    uint16_t itvl_max;
    uint16_t latency;
    uint16_t supervision_timeout;
    uint16_t min_ce_len;
    uint16_t max_ce_len;
};

struct ble_gap_passkey_params {
    uint8_t action;
    uint32_t numcmp;
};

struct ble_gap_disc_desc {
    /*** Common fields. */
    uint8_t event_type;
    uint8_t addr_type;
    uint8_t length_data;
    int8_t rssi;
    uint8_t addr[6];

    /*** LE advertising report fields; both null if no data present. */
    uint8_t *data;
    struct ble_hs_adv_fields *fields;

    /***
     * LE direct advertising report fields; direct_addr_type is
     * BLE_GAP_ADDR_TYPE_NONE if direct address fields are not present.
     */
    uint8_t direct_addr_type;
    uint8_t direct_addr[6];
};

/**
 * Represents a GAP-related event.  When such an event occurs, the host
 * notifies the application by passing an instance of this structure to an
 * application-specified callback.
 */
struct ble_gap_event {
    /**
     * Indicates the type of GAP event that occurred.  This is one of the
     * BLE_GAP_EVENT codes.
     */
    uint8_t type;

    /**
     * A discriminated union containing additional details concerning the GAP
     * event.  The 'type' field indicates which member of the union is valid.
     */
    union {
        /**
         * Represents a connection attempt.  Valid for the following event
         * types:
         *     o BLE_GAP_EVENT_CONNECT
         */
        struct {
            /**
             * The status of the connection attempt;
             *     o 0: the connection was successfully established.
             *     o BLE host error code: the connection attempt failed for
             *       the specified reason.
             */
            int status;

            /**
             * Information about the established connection.  Only valid on
             * success.
             */
            struct ble_gap_conn_desc conn;
        } connect;

        /**
         * Represents a terminated connection.  Valid for the following event
         * types:
         *     o BLE_GAP_EVENT_DISCONNECT
         */
        struct {
            /**
             * A BLE host return code indicating the reason for the
             * disconnect.
             */
            int reason;

            /** Information about the terminated connection. */
            struct ble_gap_conn_desc conn;
        } disconnect;

        /**
         * Represents an advertising report received during a discovery
         * procedure.  Valid for the following event types:
         *     o BLE_GAP_EVENT_DISC
         */
        struct ble_gap_disc_desc disc;

        /**
         * Represents an attempt to update a connection's parameters.  Valid
         * for the following event types:
         *     o BLE_GAP_EVENT_CONN_UPDATE
         */
        struct {
            /**
             * The result of the connection update attempt;
             *     o 0: the connection was successfully updated.
             *     o BLE host error code: the connection update attempt failed
             *       for the specified reason.
             */
            int status;

            /**
             * Information about the relevant connection.  If the connection
             * update attempt was successful, this descriptor contains the
             * updated parameters.
             */
            struct ble_gap_conn_desc conn;
        } conn_update;

        /**
         * Represents a peer's request to update the connection parameters.
         * This event is generated when a peer performs any of the following
         * procedures:
         *     o L2CAP Connection Parameter Update Procedure
         *     o Link-Layer Connection Parameters Request Procedure
         *
         * Valid for the following event types:
         *     o BLE_GAP_EVENT_L2CAP_UPDATE_REQ
         *     o BLE_GAP_EVENT_CONN_UPDATE_REQ
         * 
         */
        struct {
            /**
             * Indicates the connection parameters that the peer would like to
             * use.
             */
            const struct ble_gap_upd_params *peer_params;

            /**
             * Indicates the connection parameters that the local device would
             * like to use.  The application callback should fill this in.  By
             * default, this struct contains the requested parameters (i.e.,
             * it is a copy of 'peer_params').
             */
            struct ble_gap_upd_params *self_params;

            /** Information about the relevant connection. */
            struct ble_gap_conn_desc conn;
        } conn_update_req;

        /**
         * Represents a failed attempt to terminate an established connection.
         * Valid for the following event types:
         *     o BLE_GAP_EVENT_TERM_FAILURE
         */
        struct {
            /**
             * A BLE host return code indicating the reason for the failure.
             */
            int status;

            /** Information about the relevant connection. */
            struct ble_gap_conn_desc conn;
        } term_failure;

        /**
         * Represents an attempt to change the encrypted state of a
         * connection.  Valid for the following event types:
         *     o BLE_GAP_EVENT_ENC_CHANGE
         */
        struct {
            /**
             * Indicates the result of the encryption state change attempt;
             *     o 0: the encrypted state was successfully updated;
             *     o BLE host error code: the encryption state change attempt
             *       failed for the specified reason.
             */
            int status;

            /**
             * Information about the relevant connection.  If the encryption
             * state change attempt was successful, this descriptor reflects
             * the updated state.
             */
            struct ble_gap_conn_desc conn;
        } enc_change;

        /**
         * Represents a passkey query needed to complete a pairing procedure.
         * Valid for the following event types:
         *     o BLE_GAP_EVENT_PASSKEY_ACTION
         */
        struct {
            /** Contains details about the passkey query. */
            struct ble_gap_passkey_params params;

            /** Information about the relevant connection. */
            struct ble_gap_conn_desc conn;
        } passkey;

        /**
         * Represents a received ATT notification or indication.
         * Valid for the following event types:
         *     o BLE_GAP_EVENT_NOTIFY
         */
        struct {
            /** The handle of the relevant ATT attribute. */
            uint16_t attr_handle;

            /** The contents of the notification or indication. */
            void *attr_data;

            /** The number of data bytes contained in the message. */
            uint16_t attr_len;

            /** Information about the relevant connection. */
            struct ble_gap_conn_desc conn;

            /**
             * Whether the received command is a notification or an
             * indication;
             *     o 0: Notification;
             *     o 1: Indication.
             */
            unsigned indication:1;
        } notify;
    };
};

typedef int ble_gap_event_fn(struct ble_gap_event *ctxt, void *arg);

#define BLE_GAP_CONN_MODE_NON               0
#define BLE_GAP_CONN_MODE_DIR               1
#define BLE_GAP_CONN_MODE_UND               2

#define BLE_GAP_DISC_MODE_NON               0
#define BLE_GAP_DISC_MODE_LTD               1
#define BLE_GAP_DISC_MODE_GEN               2

struct ble_gap_white_entry {
    uint8_t addr_type;
    uint8_t addr[6];
};

int ble_gap_conn_find(uint16_t handle, struct ble_gap_conn_desc *out_desc);

int ble_gap_adv_start(uint8_t own_addr_type, uint8_t peer_addr_type,
                      const uint8_t *peer_addr, int32_t duration_ms,
                      const struct ble_gap_adv_params *adv_params,
                      ble_gap_event_fn *cb, void *cb_arg);
int ble_gap_adv_stop(void);
int ble_gap_adv_active(void);
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *adv_fields);
int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *rsp_fields);
int ble_gap_disc(uint8_t own_addr_type, int32_t duration_ms,
                 const struct ble_gap_disc_params *disc_params,
                 ble_gap_event_fn *cb, void *cb_arg);
int ble_gap_disc_cancel(void);
int ble_gap_disc_active(void);
int ble_gap_connect(uint8_t own_addr_type,
                    uint8_t peer_addr_type, const uint8_t *peer_addr,
                    const struct ble_gap_conn_params *params,
                    ble_gap_event_fn *cb, void *cb_arg);
int ble_gap_conn_cancel(void);
int ble_gap_conn_active(void);
int ble_gap_terminate(uint16_t conn_handle, uint8_t hci_reason);
int ble_gap_wl_set(const struct ble_gap_white_entry *white_list,
                   uint8_t white_list_count);
int ble_gap_update_params(uint16_t conn_handle,
                          const struct ble_gap_upd_params *params);
int ble_gap_security_initiate(uint16_t conn_handle);
int ble_gap_pair_initiate(uint16_t conn_handle);
int ble_gap_encryption_initiate(uint16_t conn_handle, const uint8_t *ltk,
                                uint16_t ediv, uint64_t rand_val, int auth);
int ble_gap_conn_rssi(uint16_t conn_handle, int8_t *out_rssi);

#endif
