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
struct ble_hs_conn;
struct hci_data_hdr;

#define BLE_L2CAP_CID_SIG   1
#define BLE_L2CAP_CID_ATT   4

#define BLE_L2CAP_HDR_SZ    4

#define BLE_L2CAP_CHAN_BUF_CAP      256

struct ble_l2cap_hdr
{
    uint16_t blh_len;
    uint16_t blh_cid;
};

struct ble_l2cap_chan;
typedef int ble_l2cap_rx_fn(struct ble_hs_conn *conn,
                            struct ble_l2cap_chan *chan);

struct ble_l2cap_chan
{
    SLIST_ENTRY(ble_l2cap_chan) blc_next;
    uint16_t blc_cid;
    ble_l2cap_rx_fn *blc_rx_fn;

    /* XXX: These will probably be replaced by an mbuf. */
    uint8_t blc_rx_buf[BLE_L2CAP_CHAN_BUF_CAP];
    int blc_rx_buf_sz;

    // tx mbuf
    // tx callback
};


SLIST_HEAD(ble_l2cap_chan_list, ble_l2cap_chan);

struct ble_l2cap_chan *ble_l2cap_chan_alloc(void);
void ble_l2cap_chan_free(struct ble_l2cap_chan *chan);

int ble_l2cap_rx(struct ble_hs_conn *connection,
                 struct hci_data_hdr *hci_hdr,
                 void *pkt);

int ble_l2cap_init(void);

#endif
