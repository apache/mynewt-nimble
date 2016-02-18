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

#ifndef H_BLE_HS_PRIV_
#define H_BLE_HS_PRIV_

#include <inttypes.h>
#include "stats/stats.h"
#include "log/log.h"
#include "nimble/nimble_opt.h"
#include "host/ble_hs.h"
struct os_mbuf;
struct os_mempool;
struct ble_hs_conn;
struct ble_l2cap_chan;

#define BLE_HOST_HCI_EVENT_CTLR_EVENT   (OS_EVENT_T_PERUSER + 0)
#define BLE_HS_KICK_HCI_EVENT           (OS_EVENT_T_PERUSER + 1)
#define BLE_HS_KICK_GATT_EVENT          (OS_EVENT_T_PERUSER + 2)
#define BLE_HS_KICK_L2CAP_SIG_EVENT     (OS_EVENT_T_PERUSER + 3)

STATS_SECT_START(ble_hs_stats)
    STATS_SECT_ENTRY(conn_create)
    STATS_SECT_ENTRY(conn_delete)
    STATS_SECT_ENTRY(hci_cmd)
    STATS_SECT_ENTRY(hci_event)
    STATS_SECT_ENTRY(hci_invalid_ack)
    STATS_SECT_ENTRY(hci_unknown_event)
STATS_SECT_END
extern STATS_SECT_DECL(ble_hs_stats) ble_hs_stats;

extern struct ble_hs_cfg ble_hs_cfg;

extern struct os_mbuf_pool ble_hs_mbuf_pool;
extern struct os_eventq ble_hs_evq;

extern struct log ble_hs_log;

void ble_hs_process_tx_data_queue(void);
int ble_hs_rx_data(struct os_mbuf *om);
int ble_hs_tx_data(struct os_mbuf *om);
void ble_hs_kick_hci(void);
void ble_hs_kick_gatt(void);
void ble_hs_kick_l2cap_sig(void);

int ble_hs_misc_malloc_mempool(void **mem, struct os_mempool *pool,
                               int num_entries, int entry_size, char *name);
void ble_hs_misc_log_mbuf(struct os_mbuf *om);
void ble_hs_misc_log_flat_buf(void *data, int len);
int ble_hs_misc_conn_chan_find(uint16_t conn_handle, uint16_t cid,
                               struct ble_hs_conn **out_conn,
                               struct ble_l2cap_chan **out_chan);
int ble_hs_misc_conn_chan_find_reqd(uint16_t conn_handle, uint16_t cid,
                                    struct ble_hs_conn **out_conn,
                                    struct ble_l2cap_chan **out_chan);

void ble_hs_cfg_init(struct ble_hs_cfg *cfg);

void ble_hs_misc_assert_no_locks(void);

struct os_mbuf *ble_hs_misc_pkthdr(void);

int ble_hs_misc_pullup_base(struct os_mbuf **om, int base_len);

#define BLE_HS_LOG(lvl, ...) \
    LOG_ ## lvl(&ble_hs_log, LOG_MODULE_NIMBLE_HOST, __VA_ARGS__)

#endif
