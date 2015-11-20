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

#ifndef H_L2CAP_
#define H_L2CAP_

#include <inttypes.h>
#include "os/queue.h"
#include "os/os_mbuf.h"
struct ble_hs_conn;
struct hci_data_hdr;

#define BLE_L2CAP_CID_ATT   4
#define BLE_L2CAP_CID_SIG   5

#define BLE_L2CAP_HDR_SZ    4

struct ble_l2cap_hdr
{
    uint16_t blh_len;
    uint16_t blh_cid;
};

struct ble_l2cap_chan;

typedef int ble_l2cap_rx_fn(struct ble_hs_conn *conn,
                            struct ble_l2cap_chan *chan,
                            struct os_mbuf *om);

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

    ble_l2cap_rx_fn *blc_rx_fn;
};


SLIST_HEAD(ble_l2cap_chan_list, ble_l2cap_chan);

struct ble_l2cap_chan *ble_l2cap_chan_alloc(void);
void ble_l2cap_chan_free(struct ble_l2cap_chan *chan);

uint16_t ble_l2cap_chan_mtu(struct ble_l2cap_chan *chan);

int ble_l2cap_parse_hdr(void *pkt, uint16_t len,
                        struct ble_l2cap_hdr *l2cap_hdr);
int ble_l2cap_write_hdr(void *dst, uint16_t len,
                        const struct ble_l2cap_hdr *l2cap_hdr);

int ble_l2cap_rx_payload(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                         void *payload, int len);
int ble_l2cap_rx(struct ble_hs_conn *connection,
                 struct hci_data_hdr *hci_hdr,
                 void *pkt);
int ble_l2cap_tx(struct ble_l2cap_chan *chan, struct os_mbuf *om);
int ble_l2cap_tx_flat(struct ble_l2cap_chan *chan, void *payload, int len);

int ble_l2cap_init(void);

extern struct os_mbuf_pool ble_l2cap_mbuf_pool;

#endif
