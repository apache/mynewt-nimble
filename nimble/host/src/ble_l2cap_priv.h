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

#ifndef H_L2CAP_PRIV_
#define H_L2CAP_PRIV_

#include "host/ble_l2cap.h"
#include <inttypes.h>
#include "stats/stats.h"
#include "os/queue.h"
#include "os/os_mbuf.h"
struct ble_hs_conn;
struct hci_data_hdr;

STATS_SECT_START(ble_l2cap_stats)
    STATS_SECT_ENTRY(chan_create)
    STATS_SECT_ENTRY(chan_delete)
    STATS_SECT_ENTRY(update_init)
    STATS_SECT_ENTRY(update_rx)
    STATS_SECT_ENTRY(update_fail)
    STATS_SECT_ENTRY(proc_timeout)
    STATS_SECT_ENTRY(sig_tx)
    STATS_SECT_ENTRY(sig_rx)
    STATS_SECT_ENTRY(sm_tx)
    STATS_SECT_ENTRY(sm_rx)
STATS_SECT_END
extern STATS_SECT_DECL(ble_l2cap_stats) ble_l2cap_stats;

#define BLE_L2CAP_SIG_HDR_SZ                4
struct ble_l2cap_sig_hdr {
    uint8_t op;
    uint8_t identifier;
    uint16_t length;
};

#define BLE_L2CAP_SIG_REJECT_MIN_SZ         2
struct ble_l2cap_sig_reject {
    uint16_t reason;
};

#define BLE_L2CAP_SIG_UPDATE_REQ_SZ         8
struct ble_l2cap_sig_update_req {
    uint16_t itvl_min;
    uint16_t itvl_max;
    uint16_t slave_latency;
    uint16_t timeout_multiplier;
};

#define BLE_L2CAP_SIG_UPDATE_RSP_SZ         2
struct ble_l2cap_sig_update_rsp {
    uint16_t result;
};

#define BLE_L2CAP_SIG_UPDATE_RSP_RESULT_ACCEPT  0x0000
#define BLE_L2CAP_SIG_UPDATE_RSP_RESULT_REJECT  0x0001

#define BLE_L2CAP_CID_ATT   4
#define BLE_L2CAP_CID_SIG   5
#define BLE_L2CAP_CID_SM    6

#define BLE_L2CAP_HDR_SZ    4

struct ble_l2cap_hdr
{
    uint16_t blh_len;
    uint16_t blh_cid;
};

struct ble_l2cap_chan;

typedef int ble_l2cap_rx_fn(uint16_t conn_handle,
                            struct os_mbuf **om);

typedef int ble_l2cap_tx_fn(struct ble_hs_conn *conn,
                            struct ble_l2cap_chan *chan);

typedef uint8_t ble_l2cap_chan_flags;
#define BLE_L2CAP_CHAN_F_TXED_MTU       0x01    /* We have sent our MTU. */

struct ble_l2cap_chan
{
    SLIST_ENTRY(ble_l2cap_chan) blc_next;
    uint16_t blc_cid;
    uint16_t blc_my_mtu;
    uint16_t blc_peer_mtu;      /* 0 if not exchanged. */
    uint16_t blc_default_mtu;
    ble_l2cap_chan_flags blc_flags;

    struct os_mbuf *blc_rx_buf;
    uint16_t blc_rx_len;        /* Length of current reassembled rx packet. */

    ble_l2cap_rx_fn *blc_rx_fn;
};


SLIST_HEAD(ble_l2cap_chan_list, ble_l2cap_chan);

int ble_l2cap_sig_locked_by_cur_task(void);
int ble_l2cap_parse_hdr(struct os_mbuf *om, int off,
                        struct ble_l2cap_hdr *l2cap_hdr);
struct os_mbuf *ble_l2cap_prepend_hdr(struct os_mbuf *om, uint16_t cid,
                                      uint16_t len);

int ble_l2cap_sig_hdr_parse(void *payload, uint16_t len,
                            struct ble_l2cap_sig_hdr *hdr);
int ble_l2cap_sig_hdr_write(void *payload, uint16_t len,
                            struct ble_l2cap_sig_hdr *hdr);
int ble_l2cap_sig_reject_write(void *payload, uint16_t len,
                               struct ble_l2cap_sig_hdr *hdr,
                               struct ble_l2cap_sig_reject *cmd);
int ble_l2cap_sig_reject_tx(uint16_t conn_handle, uint8_t id, uint16_t reason);
int ble_l2cap_sig_update_req_parse(void *payload, int len,
                                   struct ble_l2cap_sig_update_req *req);
int ble_l2cap_sig_update_req_write(void *payload, int len,
                                   struct ble_l2cap_sig_hdr *hdr,
                                   struct ble_l2cap_sig_update_req *req);
int ble_l2cap_sig_update_req_tx(uint16_t conn_handle, uint8_t id,
                                struct ble_l2cap_sig_update_req *req);
int ble_l2cap_sig_update_rsp_parse(void *payload, int len,
                                   struct ble_l2cap_sig_update_rsp *cmd);
int ble_l2cap_sig_update_rsp_write(void *payload, int len,
                                   struct ble_l2cap_sig_hdr *hdr,
                                   struct ble_l2cap_sig_update_rsp *cmd);
int ble_l2cap_sig_update_rsp_tx(uint16_t conn_handle, uint8_t id,
                                uint16_t result);


struct ble_l2cap_chan *ble_l2cap_chan_alloc(void);
void ble_l2cap_chan_free(struct ble_l2cap_chan *chan);

uint16_t ble_l2cap_chan_mtu(struct ble_l2cap_chan *chan);


int ble_l2cap_rx(struct ble_hs_conn *conn,
                 struct hci_data_hdr *hci_hdr,
                 struct os_mbuf *om,
                 ble_l2cap_rx_fn **out_rx_cb,
                 struct os_mbuf **out_rx_buf);
int ble_l2cap_tx(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                 struct os_mbuf *om);

void ble_l2cap_sig_wakeup(void);
int ble_l2cap_sig_init(void);
int ble_l2cap_init(void);

#endif
