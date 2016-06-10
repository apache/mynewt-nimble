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
#include "ble_hci_util_priv.h"
#include "ble_hs_adv_priv.h"
#include "ble_hs_atomic_priv.h"
#include "ble_hs_conn_priv.h"
#include "ble_hs_endian_priv.h"
#include "ble_hs_startup_priv.h"
#include "ble_l2cap_priv.h"
#include "ble_l2cap_sig_priv.h"
#include "ble_sm_priv.h"
#include "host/ble_hs.h"
#include "log/log.h"
#include "nimble/nimble_opt.h"
#include "stats/stats.h"
struct ble_hs_conn;
struct ble_l2cap_chan;
struct os_mbuf;
struct os_mempool;

#define BLE_HOST_HCI_EVENT_CTLR_EVENT   (OS_EVENT_T_PERUSER + 0)

STATS_SECT_START(ble_hs_stats)
    STATS_SECT_ENTRY(conn_create)
    STATS_SECT_ENTRY(conn_delete)
    STATS_SECT_ENTRY(hci_cmd)
    STATS_SECT_ENTRY(hci_event)
    STATS_SECT_ENTRY(hci_invalid_ack)
    STATS_SECT_ENTRY(hci_unknown_event)
STATS_SECT_END
extern STATS_SECT_DECL(ble_hs_stats) ble_hs_stats;

struct ble_hci_ack {
    int bha_status;         /* A BLE_HS_E<...> error; NOT a naked HCI code. */
    uint8_t *bha_params;
    int bha_params_len;
    uint16_t bha_opcode;
    uint8_t bha_hci_handle;
};

extern struct ble_hs_dev ble_hs_our_dev;
extern struct ble_hs_cfg ble_hs_cfg;

extern struct os_mbuf_pool ble_hs_mbuf_pool;

extern struct log ble_hs_log;

void ble_hs_process_tx_data_queue(void);
int ble_hs_rx_data(struct os_mbuf *om);
int ble_hs_tx_data(struct os_mbuf *om);

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
uint8_t ble_hs_misc_addr_type_to_ident(uint8_t addr_type);

void ble_hs_cfg_init(struct ble_hs_cfg *cfg);

int ble_hs_locked_by_cur_task(void);
int ble_hs_thread_safe(void);
int ble_hs_is_parent_task(void);
void ble_hs_lock(void);
void ble_hs_unlock(void);

struct os_mbuf *ble_hs_misc_pkthdr(void);

int ble_hs_misc_pullup_base(struct os_mbuf **om, int base_len);

int ble_hci_cmd_tx(void *cmd, void *evt_buf, uint8_t evt_buf_len,
                   uint8_t *out_evt_buf_len);
int ble_hci_cmd_tx_empty_ack(void *cmd);
void ble_hci_cmd_rx_ack(uint8_t *ack_ev);
void ble_hci_cmd_init(void);
int ble_hs_priv_set_nrpa(void);
void ble_hs_priv_get_nrpa(uint8_t *addr);
void ble_hs_priv_update_identity(uint8_t *addr);
void ble_hs_priv_update_irk(uint8_t *irk);
uint8_t *bls_hs_priv_get_local_identity_addr(uint8_t *type);
void bls_hs_priv_copy_local_identity_addr(uint8_t *pdst, uint8_t* addr_type);
uint8_t *ble_hs_priv_get_local_irk(void);
int ble_keycache_remove_irk_entry(uint8_t addr_type, uint8_t *addr);
int ble_keycache_write_irk_entry(uint8_t *addr, uint8_t addrtype, uint8_t *irk);
#if PHONY_HCI_ACKS
typedef int ble_hci_cmd_phony_ack_fn(uint8_t *ack, int ack_buf_len);

void ble_hci_set_phony_ack_cb(ble_hci_cmd_phony_ack_fn *cb);
#endif

#define BLE_HS_LOG(lvl, ...) \
    LOG_ ## lvl(&ble_hs_log, LOG_MODULE_NIMBLE_HOST, __VA_ARGS__)

#define BLE_HS_LOG_ADDR(lvl, addr)                      \
    BLE_HS_LOG(lvl, "%02x:%02x:%02x:%02x:%02x:%02x",    \
               (addr)[5], (addr)[4], (addr)[3],         \
               (addr)[2], (addr)[1], (addr)[0])

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
