/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef H_BLE_LL_CONN_
#define H_BLE_LL_CONN_

/* Definitions for source clock accuracy */
#define BLE_MASTER_SCA_251_500_PPM      (0)
#define BLE_MASTER_SCA_151_250_PPM      (1)
#define BLE_MASTER_SCA_101_150_PPM      (2)
#define BLE_MASTER_SCA_76_100_PPM       (3)
#define BLE_MASTER_SCA_51_75_PPM        (4)
#define BLE_MASTER_SCA_31_50_PPM        (5)
#define BLE_MASTER_SCA_21_30_PPM        (6)
#define BLE_MASTER_SCA_0_20_PPM         (7)

int ble_ll_conn_create(uint8_t *cmdbuf);
int ble_ll_conn_create_cancel(void);
int ble_ll_conn_is_peer_adv(uint8_t addr_type, uint8_t *adva);
int ble_ll_conn_request_send(uint8_t addr_type, uint8_t *adva);
void ble_ll_init_rx_pdu_proc(uint8_t *rxbuf, struct ble_mbuf_hdr *ble_hdr);
int ble_ll_init_rx_pdu_end(struct os_mbuf *rxpdu);
void ble_ll_conn_slave_start(uint8_t *rxbuf);
void ble_ll_conn_spvn_timeout(void *arg);
void ble_ll_conn_event_end(void *arg);
void ble_ll_conn_init(void);
void ble_ll_conn_rsp_rxd(void);

#endif /* H_BLE_LL_CONN_ */
