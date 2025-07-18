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

#ifndef H_BLE_LL_ISO_PRIV_
#define H_BLE_LL_ISO_PRIV_

#include <stdint.h>
#include <controller/ble_ll_isoal.h>
#include <sys/queue.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ISO Parameters */
struct ble_ll_iso_params {
    /* SDU Interval */
    uint32_t sdu_interval;
    /* ISO Interval */
    uint16_t iso_interval;
    /* Maximum SDU size */
    uint16_t max_sdu;
    /* Max PDU length */
    uint8_t max_pdu;
    /* Burst Number */
    uint8_t bn;
    /* Pre-transmission */
    uint8_t pte;
    /* Framing */
    uint8_t framed : 1;
    /* Framing mode */
    uint8_t framing_mode : 1;
};

/**
 * @struct ble_ll_iso_data_path_cb
 *
 * Interface structure for ISO data path callbacks.
 */
struct ble_ll_iso_data_path_cb {
    /**
     * @brief Callback function for ISO SDU (Service Data Unit) output.
     *
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
    void (*sdu_out)(uint16_t conn_handle, const struct os_mbuf *om,
                    uint32_t timestamp, uint16_t seq_num, bool valid);
};

/* Forward declaration */
struct ble_ll_iso_conn;

/* ISO Rx object */
struct ble_ll_iso_rx {
    struct {
        uint8_t payload_type;
        uint32_t received_sdu_count;
        uint32_t missed_sdu_count;
        uint32_t failed_sdu_count;
    } test;

    /* ISOAL Demultiplexer */
    struct ble_ll_isoal_demux demux;

    /* ISO Connection */
    struct ble_ll_iso_conn *conn;

    /* ISO Parameters */
    const struct ble_ll_iso_params *params;

    /* ISO Data Path */
    const struct ble_ll_iso_data_path_cb *data_path;
};

/* ISO Tx object */
struct ble_ll_iso_tx {
    struct {
        uint8_t payload_type;
        uint32_t rand;
    } test;

    /* ISOAL Multiplexer */
    struct ble_ll_isoal_mux mux;

    /* ISO Connection */
    struct ble_ll_iso_conn *conn;

    /* ISO Parameters */
    const struct ble_ll_iso_params *params;

    /* ISO Data Path */
    const struct ble_ll_iso_data_path_cb *data_path;
};

/* ISO Connection object */
struct ble_ll_iso_conn {
    /* ISO Rx Context */
    struct ble_ll_iso_rx *rx;

    /* ISO Tx Context */
    struct ble_ll_iso_tx *tx;

    /* Connection handle */
    uint16_t handle;

    /* HCI SDU Fragment */
    struct os_mbuf *frag;

    /* Number of Completed Packets */
    uint16_t num_completed_pkt;

    STAILQ_ENTRY(ble_ll_iso_conn) iso_conn_q_next;
};

void ble_ll_iso_conn_init(struct ble_ll_iso_conn *conn, uint16_t conn_handle,
                          struct ble_ll_iso_rx *rx, struct ble_ll_iso_tx *tx);
void ble_ll_iso_conn_reset(struct ble_ll_iso_conn *conn);

void ble_ll_iso_tx_init(struct ble_ll_iso_tx *tx, struct ble_ll_iso_conn *conn,
                        const struct ble_ll_iso_params *params);
void ble_ll_iso_tx_reset(struct ble_ll_iso_tx *tx);
int ble_ll_iso_tx_event_start(struct ble_ll_iso_tx *tx, uint32_t timestamp);
int ble_ll_iso_tx_event_done(struct ble_ll_iso_tx *tx);
int ble_ll_iso_tx_pdu_get(struct ble_ll_iso_tx *tx, uint8_t idx,
                          uint32_t pkt_counter, uint8_t *llid, void *dptr);

void ble_ll_iso_rx_init(struct ble_ll_iso_rx *rx, struct ble_ll_iso_conn *conn,
                        const struct ble_ll_iso_params *params);
void ble_ll_iso_rx_reset(struct ble_ll_iso_rx *rx);
int ble_ll_iso_rx_event_start(struct ble_ll_iso_rx *rx, uint32_t timestamp);
int ble_ll_iso_rx_event_done(struct ble_ll_iso_rx *rx);
int ble_ll_iso_rx_pdu_put(struct ble_ll_iso_rx *rx, uint8_t idx, struct os_mbuf *om);

struct ble_ll_iso_conn *ble_ll_iso_conn_find_by_handle(uint16_t conn_handle);

#ifdef __cplusplus
}
#endif

#endif
