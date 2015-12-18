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

#ifndef H_BLE_HS_PRIV_
#define H_BLE_HS_PRIV_

#include <inttypes.h>
#include "host/ble_hs.h"
struct os_mbuf;
struct os_mempool;

#define BLE_HOST_HCI_EVENT_CTLR_EVENT   (OS_EVENT_T_PERUSER + 0)
#define BLE_HS_KICK_HCI_EVENT           (OS_EVENT_T_PERUSER + 1)
#define BLE_HS_KICK_GATT_EVENT          (OS_EVENT_T_PERUSER + 2)
#define BLE_HS_RX_DATA_EVENT            (OS_EVENT_T_PERUSER + 3)
#define BLE_HS_TX_DATA_EVENT            (OS_EVENT_T_PERUSER + 4)

extern struct os_mbuf_pool ble_hs_mbuf_pool;
extern struct os_eventq ble_hs_evq;

void ble_hs_process_tx_data_queue(void);
int ble_hs_rx_data(struct os_mbuf *om);
int ble_hs_tx_data(struct os_mbuf *om);
void ble_hs_kick_hci(void);
void ble_hs_kick_gatt(void);

int ble_hs_misc_malloc_mempool(void **mem, struct os_mempool *pool,
                               int num_entries, int entry_size, char *name);

void ble_hs_cfg_init(void);

#endif
