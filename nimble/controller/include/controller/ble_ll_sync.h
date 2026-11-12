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

#ifndef H_BLE_LL_SYNC_
#define H_BLE_LL_SYNC_

#include <stdint.h>

#include "nimble/ble.h"
#include "controller/ble_ll_hci.h"
#include "controller/ble_ll_conn.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ble_ll_scan_addr_data;
struct ble_ll_sync_sm;

int ble_ll_sync_create(const uint8_t *cmdbuf, uint8_t len);
int ble_ll_sync_cancel(void);
int ble_ll_sync_terminate(const uint8_t *cmdbuf, uint8_t len);
int ble_ll_sync_list_add(const uint8_t *cmdbuf, uint8_t len);
int ble_ll_sync_list_remove(const uint8_t *cmdbuf, uint8_t len);
int ble_ll_sync_list_clear(void);
int ble_ll_sync_list_size(uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_sync_receive_enable(const uint8_t *cmdbuf, uint8_t len);
int ble_ll_sync_transfer(const uint8_t *cmdbuf, uint8_t len,
                         uint8_t *rspbuf, uint8_t *rsplen);

void ble_ll_sync_periodic_ind(struct ble_ll_conn_sm *connsm,
                              const uint8_t *sync_ind, uint8_t mode,
                              uint16_t max_skip, uint32_t sync_timeout);
void ble_ll_sync_transfer_disconnected(struct ble_ll_conn_sm *connsm);

void ble_ll_sync_info_event(struct ble_ll_scan_addr_data *addrd, uint8_t sid,
                            struct ble_mbuf_hdr *rxhdr,
                            const uint8_t *syncinfo);

int ble_ll_sync_rx_isr_start(uint8_t pdu_type, struct ble_mbuf_hdr *rxhdr);
int ble_ll_sync_rx_isr_end(uint8_t *rxbuf, struct ble_mbuf_hdr *rxhdr);
void ble_ll_sync_rx_pkt_in(struct os_mbuf *rxpdu, struct ble_mbuf_hdr *hdr);
void ble_ll_sync_wfr_timer_exp(void);
void ble_ll_sync_halt(void);
void ble_ll_sync_rmvd_from_sched(struct ble_ll_sync_sm *sm);

uint32_t ble_ll_sync_get_event_end_time(void);

/* Anchor point of periodic advertising event was received */
#define BLE_LL_SYNC_NOTIFY_ANCHOR       0
/* Sync was lost (timeout), no more anchors */
#define BLE_LL_SYNC_NOTIFY_LOST         1
/* Sync was terminated by host, no more anchors */
#define BLE_LL_SYNC_NOTIFY_TERMINATED   2

/**
 * Callback with notifications about established syncs. Called from LL task.
 *
 * BLE_LL_SYNC_NOTIFY_ANCHOR is sent on first PDU of each periodic advertising
 * event received with valid CRC (not for chained PDUs).
 * BLE_LL_SYNC_NOTIFY_LOST and BLE_LL_SYNC_NOTIFY_TERMINATED are sent when sync
 * ends, sync handle is no longer valid then. No notification is sent on LL
 * reset.
 *
 * @param sync_handle     Sync handle
 * @param event           BLE_LL_SYNC_NOTIFY_*
 * @param event_cntr      paEventCounter of the event (ANCHOR only)
 * @param anchor_ticks    Anchor point, i.e. start of received PDU (ANCHOR only)
 * @param anchor_rem_us   Anchor point usecs remainder (ANCHOR only)
 * @param arg             Argument passed to ble_ll_sync_notify_cb_set()
 */
typedef void (*ble_ll_sync_notify_cb_t)(uint16_t sync_handle, uint8_t event,
                                        uint16_t event_cntr, uint32_t anchor_ticks,
                                        uint8_t anchor_rem_us, void *arg);

/**
 * Sets notification callback for all syncs. It is not cleared when sync ends.
 *
 * @param cb   Callback, NULL to disable
 * @param arg  Argument passed to callback
 */
void ble_ll_sync_notify_cb_set(ble_ll_sync_notify_cb_t cb, void *arg);

bool ble_ll_sync_enabled(void);

void ble_ll_sync_reset(void);
void ble_ll_sync_init(void);

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_LL_SYNC_ */
