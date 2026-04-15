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

#ifndef H_BLE_LL_ISO_BIG_SYNC_
#define H_BLE_LL_ISO_BIG_SYNC_

#ifdef __cplusplus
extern "C" {
#endif

#if MYNEWT_VAL(BLE_LL_ISO_BROADCAST_SYNC)

int ble_ll_iso_big_sync_rx_isr_start(uint8_t pdu_type, struct ble_mbuf_hdr *rxhdr);
int ble_ll_iso_big_sync_rx_isr_end(uint8_t *rxbuf, struct ble_mbuf_hdr *rxhdr);
int ble_ll_iso_big_sync_rx_isr_early_end(const uint8_t *rxbuf,
                                         const struct ble_mbuf_hdr *rxhdr);
void ble_ll_iso_big_sync_rx_pdu_in(struct os_mbuf **rxpdu, struct ble_mbuf_hdr *hdr);

void ble_ll_iso_big_sync_wfr_timer_exp(void);
void ble_ll_iso_big_sync_halt(void);

int ble_ll_iso_big_sync_hci_create(const uint8_t *cmdbuf, uint8_t len);
int ble_ll_iso_big_sync_hci_terminate(const uint8_t *cmdbuf, uint8_t len,
                                      uint8_t *rspbuf, uint8_t *rsplen);

void ble_ll_iso_big_sync_init(void);
void ble_ll_iso_big_sync_reset(void);

#endif /* BLE_LL_ISO_BROADCAST_SYNC */

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_LL_ISO_BIG_SYNC_ */
