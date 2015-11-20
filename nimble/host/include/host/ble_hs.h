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

#ifndef _BLE_HOST_H 
#define _BLE_HOST_H 

#include <inttypes.h>
struct os_mbuf;

#define BLE_HOST_HCI_EVENT_CTLR_EVENT   (OS_EVENT_T_PERUSER + 0)
#define BLE_HS_KICK_EVENT               (OS_EVENT_T_PERUSER + 1)
#define BLE_HS_RX_DATA_EVENT            (OS_EVENT_T_PERUSER + 2)
#define BLE_HS_TX_DATA_EVENT            (OS_EVENT_T_PERUSER + 3)

extern struct os_mbuf_pool ble_hs_mbuf_pool;
extern struct os_eventq ble_hs_evq;

void ble_hs_process_tx_data_queue(void);
void ble_hs_task_handler(void *arg);
int ble_hs_rx_data(struct os_mbuf *om);
int ble_hs_tx_data(struct os_mbuf *om);
void ble_hs_kick(void);
int ble_hs_init(uint8_t prio);

#endif /* _BLE_HOST_H */
