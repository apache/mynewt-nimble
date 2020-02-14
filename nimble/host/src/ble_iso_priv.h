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

#ifndef H_BLE_ISO_PRIV_
#define H_BLE_ISO_PRIV_

#include <inttypes.h>
#include "nimble/hci_common.h"
#ifdef __cplusplus
extern "C" {
#endif

#include "host/ble_iso.h"

struct ble_iso_conn {
    union {
        SLIST_ENTRY(ble_iso_conn) next;
        STAILQ_ENTRY(ble_iso_conn) free_cis;
        STAILQ_ENTRY(ble_iso_conn) pend_cis;
    };

    /* Common for bis_handles and cis */
    uint8_t id;
    uint16_t iso_handle;
    uint8_t flags;
    ble_iso_event_fn *cb;
    void *cb_arg;

    /* CIS related only */
    uint8_t cig_id;
    uint16_t acl_handle;

    /*params*/
    uint16_t max_pdu_output;
    uint16_t max_pdu_input;
    uint16_t seq_num;
    uint32_t last_timestamp;

    /**
     * Count of packets sent over this connection that the controller has not
     * transmitted or flushed yet.
     */
    uint16_t outstanding_pkts;
};

void
ble_iso_rx_create_big_complete(const struct ble_hci_ev_le_subev_create_big_complete *ev);

void
ble_iso_rx_terminate_big_complete(const struct ble_hci_ev_le_subev_terminate_big_complete *ev);

int ble_iso_rx_hci_evt_le_cis_established(const struct ble_hci_ev_le_subev_cis_established *ev);
int ble_iso_rx_hci_evt_le_cis_request(const struct ble_hci_ev_le_subev_cis_request *ev);
int ble_iso_rx_hci_evt_le_big_completed(const struct ble_hci_ev_le_subev_create_big_complete *ev);
int ble_iso_rx_hci_le_big_terminate_complete(const struct ble_hci_ev_le_subev_terminate_big_complete *ev);
int ble_iso_hci_le_big_sync_established(const struct ble_hci_ev_le_subev_big_sync_established *ev);
int ble_iso_hci_le_big_sync_lost(const struct ble_hci_ev_le_subev_big_sync_lost *ev);

struct ble_iso_conn *ble_iso_find_by_iso_handle(uint16_t iso_handle);
void ble_iso_disconnected_event(uint16_t conn_handle, uint8_t reason, bool is_acl);
int ble_iso_hci_set_buf_sz(uint16_t iso_pktlen, uint16_t iso_max_pkts);
/* Data handling */
void ble_iso_rx(struct os_mbuf *om);
void ble_iso_disconnect_cis(uint16_t handle);

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_ISO_PRIV_ */
