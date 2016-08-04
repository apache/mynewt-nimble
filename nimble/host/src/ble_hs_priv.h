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

#include <assert.h>
#include <inttypes.h>
#include "ble_att_cmd_priv.h"
#include "ble_att_priv.h"
#include "ble_gap_priv.h"
#include "ble_gatt_priv.h"
#include "ble_hs_dbg_priv.h"
#include "ble_hs_hci_priv.h"
#include "ble_hs_atomic_priv.h"
#include "ble_hs_conn_priv.h"
#include "ble_hs_atomic_priv.h"
#include "ble_hs_endian_priv.h"
#include "ble_hs_mbuf_priv.h"
#include "ble_hs_startup_priv.h"
#include "ble_l2cap_priv.h"
#include "ble_l2cap_sig_priv.h"
#include "ble_sm_priv.h"
#include "ble_hs_adv_priv.h"
#include "ble_hs_pvcy_priv.h"
#include "ble_hs_id_priv.h"
#include "ble_uuid_priv.h"
#include "host/ble_hs.h"
#include "nimble/nimble_opt.h"
#include "stats/stats.h"
struct ble_hs_conn;
struct ble_l2cap_chan;
struct os_mbuf;
struct os_mempool;
struct os_event;

#define BLE_HOST_HCI_EVENT_CTLR_EVENT   (OS_EVENT_T_PERUSER + 0)
#define BLE_HS_EVENT_TX_NOTIFICATIONS   (OS_EVENT_T_PERUSER + 1)
#define BLE_HS_EVENT_RESET              (OS_EVENT_T_PERUSER + 2)

#define BLE_HS_SYNC_STATE_BAD           0
#define BLE_HS_SYNC_STATE_BRINGUP       1
#define BLE_HS_SYNC_STATE_GOOD          2

STATS_SECT_START(ble_hs_stats)
    STATS_SECT_ENTRY(conn_create)
    STATS_SECT_ENTRY(conn_delete)
    STATS_SECT_ENTRY(hci_cmd)
    STATS_SECT_ENTRY(hci_event)
    STATS_SECT_ENTRY(hci_invalid_ack)
    STATS_SECT_ENTRY(hci_unknown_event)
    STATS_SECT_ENTRY(hci_timeout)
    STATS_SECT_ENTRY(reset)
    STATS_SECT_ENTRY(sync)
STATS_SECT_END
extern STATS_SECT_DECL(ble_hs_stats) ble_hs_stats;

extern struct ble_hs_cfg ble_hs_cfg;
extern struct os_mbuf_pool ble_hs_mbuf_pool;
extern uint8_t ble_hs_sync_state;

extern const uint8_t ble_hs_misc_null_addr[6];

void ble_hs_process_tx_data_queue(void);
void ble_hs_process_rx_data_queue(void);
int ble_hs_tx_data(struct os_mbuf *om);
void ble_hs_enqueue_hci_event(uint8_t *hci_evt);
void ble_hs_event_enqueue(struct os_event *ev);

int ble_hs_hci_rx_evt(uint8_t *hci_ev, void *arg);
int ble_hs_hci_evt_acl_process(struct os_mbuf *om);

int ble_hs_misc_malloc_mempool(void **mem, struct os_mempool *pool,
                               int num_entries, int entry_size, char *name);
int ble_hs_misc_conn_chan_find(uint16_t conn_handle, uint16_t cid,
                               struct ble_hs_conn **out_conn,
                               struct ble_l2cap_chan **out_chan);
int ble_hs_misc_conn_chan_find_reqd(uint16_t conn_handle, uint16_t cid,
                                    struct ble_hs_conn **out_conn,
                                    struct ble_l2cap_chan **out_chan);
uint8_t ble_hs_misc_addr_type_to_id(uint8_t addr_type);

void ble_hs_cfg_init(struct ble_hs_cfg *cfg);

int ble_hs_locked_by_cur_task(void);
int ble_hs_is_parent_task(void);
void ble_hs_lock(void);
void ble_hs_unlock(void);
void ble_hs_sched_reset(int reason);
void ble_hs_hw_error(uint8_t hw_code);
void ble_hs_heartbeat_sched(int32_t ticks);
void ble_hs_notifications_sched(void);

#if LOG_LEVEL <= LOG_LEVEL_DEBUG

#define BLE_HS_LOG_CMD(is_tx, cmd_type, cmd_name, conn_handle,                \
                       log_cb, cmd) do                                        \
{                                                                             \
    BLE_HS_LOG(DEBUG, "%sed %s command: %s; conn=%d ",                        \
               (is_tx) ? "tx" : "rx", (cmd_type), (cmd_name), (conn_handle)); \
    (log_cb)(cmd);                                                            \
    BLE_HS_LOG(DEBUG, "\n");                                                  \
} while (0)

#define BLE_HS_LOG_EMPTY_CMD(is_tx, cmd_type, cmd_name, conn_handle) do       \
{                                                                             \
    BLE_HS_LOG(DEBUG, "%sed %s command: %s; conn=%d ",                        \
               (is_tx) ? "tx" : "rx", (cmd_type), (cmd_name), (conn_handle)); \
    BLE_HS_LOG(DEBUG, "\n");                                                  \
} while (0)

#else

#define BLE_HS_LOG_CMD(is_tx, cmd_type, cmd_name, conn_handle, log_cb, cmd)
#define BLE_HS_LOG_EMPTY_CMD(is_tx, cmd_type, cmd_name, conn_handle)

#endif

#if BLE_HS_DEBUG
    #define BLE_HS_DBG_ASSERT(x) assert(x)
    #define BLE_HS_DBG_ASSERT_EVAL(x) assert(x)
#else
    #define BLE_HS_DBG_ASSERT(x)
    #define BLE_HS_DBG_ASSERT_EVAL(x) ((void)(x))
#endif

#endif
