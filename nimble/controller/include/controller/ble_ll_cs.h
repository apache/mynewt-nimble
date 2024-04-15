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

/* All Channel Sounding APIs are experimental and subject to change at any time */

#ifndef H_BLE_LL_CS
#define H_BLE_LL_CS

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ble_ll_cs_init(void);
void ble_ll_cs_reset(void);

void ble_ll_cs_capabilities_pdu_make(struct ble_ll_conn_sm *connsm, uint8_t *dptr);
void ble_ll_cs_config_req_make(struct ble_ll_conn_sm *connsm, uint8_t *dptr);

int ble_ll_cs_rx_capabilities_req(struct ble_ll_conn_sm *connsm, uint8_t *dptr, uint8_t *rspbuf);
void ble_ll_cs_rx_capabilities_rsp(struct ble_ll_conn_sm *connsm, uint8_t *dptr);
void ble_ll_cs_rx_capabilities_req_rejected(struct ble_ll_conn_sm *connsm, uint8_t ble_error);
int ble_ll_cs_rx_fae_req(struct ble_ll_conn_sm *connsm, struct os_mbuf *om);
void ble_ll_cs_rx_fae_rsp(struct ble_ll_conn_sm *connsm, uint8_t *dptr);
void ble_ll_cs_rx_fae_req_rejected(struct ble_ll_conn_sm *connsm, uint8_t ble_error);
int ble_ll_cs_rx_config_req(struct ble_ll_conn_sm *connsm, uint8_t *dptr, uint8_t *rspbuf);
void ble_ll_cs_rx_config_rsp(struct ble_ll_conn_sm *connsm, uint8_t *dptr);
void ble_ll_cs_rx_config_req_rejected(struct ble_ll_conn_sm *connsm, uint8_t ble_error);

/* HCI handlers */
int ble_ll_cs_hci_rd_loc_supp_cap(uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_cs_hci_rd_rem_supp_cap(const uint8_t *cmdbuf, uint8_t cmdlen);
int ble_ll_cs_hci_wr_cached_rem_supp_cap(const uint8_t *cmdbuf, uint8_t cmdlen, uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_cs_hci_sec_enable(const uint8_t *cmdbuf, uint8_t cmdlen);
int ble_ll_cs_hci_set_def_settings(const uint8_t *cmdbuf, uint8_t cmdlen, uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_cs_hci_rd_rem_fae(const uint8_t *cmdbuf, uint8_t cmdlen);
int ble_ll_cs_hci_wr_cached_rem_fae(const uint8_t *cmdbuf, uint8_t cmdlen, uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_cs_hci_create_config(const uint8_t *cmdbuf, uint8_t cmdlen);
int ble_ll_cs_hci_remove_config(const uint8_t *cmdbuf, uint8_t cmdlen);
int ble_ll_cs_hci_set_chan_class(const uint8_t *cmdbuf, uint8_t cmdlen);
int ble_ll_cs_hci_set_proc_params(const uint8_t *cmdbuf, uint8_t cmdlen, uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_cs_hci_proc_enable(const uint8_t *cmdbuf, uint8_t cmdlen);
int ble_ll_cs_hci_test(const uint8_t *cmdbuf, uint8_t cmdlen, uint8_t *rspbuf, uint8_t *rsplen);
int ble_ll_cs_hci_test_end(void);

#ifdef __cplusplus
}
#endif

#endif
