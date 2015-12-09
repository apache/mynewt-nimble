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

#ifndef H_BLE_ATT_PRIV_
#define H_BLE_ATT_PRIV_

#include <inttypes.h>
#include "os/queue.h"
#include "host/ble_att.h"
struct os_mbuf;
struct ble_hs_conn;
struct ble_l2cap_chan;
struct ble_att_find_info_req;
struct ble_att_error_rsp;
struct ble_att_mtu_cmd;
struct ble_att_read_req;
struct ble_att_read_type_req;
struct ble_att_read_group_type_req;
struct ble_att_read_group_type_rsp;
struct ble_att_find_type_value_req;

#define BLE_ATT_MTU_DFLT         23  /* Also the minimum. */
#define BLE_ATT_MTU_MAX          256 /* XXX: I'm making this up! */

/**
 * Called from ble_att_svr_walk().  Called on each entry in the 
 * ble_att_svr_list.
 *
 * @param Contains the current ble_att being iterated through
 * @param The user supplied argument to ble_att_svr_walk()
 *
 * @return 0 on continue, 1 on stop
 */
typedef int (*ble_att_svr_walk_func_t)(struct ble_att_svr_entry *entry,
                                       void *arg);


#define HA_OPCODE_METHOD_START (0)
#define HA_OPCODE_METHOD_END (5)
#define HA_OPCODE_COMMAND_FLAG (1 << 6) 
#define HA_OPCODE_AUTH_SIG_FLAG (1 << 7) 

struct ble_att_clt_adata {
    uint16_t att_handle;
    uint16_t end_group_handle;
    int value_len;
    void *value;
};

SLIST_HEAD(ble_att_clt_entry_list, ble_att_clt_entry);

/*** @gen */
struct ble_l2cap_chan *ble_att_create_chan(void);
void ble_att_set_peer_mtu(struct ble_l2cap_chan *chan, uint16_t peer_mtu);
struct os_mbuf *ble_att_get_pkthdr(void);

/*** @svr */
int ble_att_svr_rx_mtu(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                       struct os_mbuf **rxom);
int ble_att_svr_rx_find_info(struct ble_hs_conn *conn,
                             struct ble_l2cap_chan *chan,
                             struct os_mbuf **rxom);
int ble_att_svr_rx_find_type_value(struct ble_hs_conn *conn,
                                   struct ble_l2cap_chan *chan,
                                   struct os_mbuf **rxom);
int ble_att_svr_rx_read_type(struct ble_hs_conn *conn,
                             struct ble_l2cap_chan *chan,
                             struct os_mbuf **rxom);
int ble_att_svr_rx_read_group_type(struct ble_hs_conn *conn,
                                   struct ble_l2cap_chan *chan,
                                   struct os_mbuf **rxom);
int ble_att_svr_rx_read(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                        struct os_mbuf **rxom);
int ble_att_svr_rx_write(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                         struct os_mbuf **rxom);
int ble_att_svr_init(void);

/*** @clt */
int ble_att_clt_rx_error(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                         struct os_mbuf **rxom);
int ble_att_clt_tx_mtu(struct ble_hs_conn *conn,
                       struct ble_att_mtu_cmd *req);
int ble_att_clt_rx_mtu(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                       struct os_mbuf **rxom);
int ble_att_clt_tx_read(struct ble_hs_conn *conn,
                        struct ble_att_read_req *req);
int ble_att_clt_rx_read(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                        struct os_mbuf **rxom);
int ble_att_clt_tx_read_type(struct ble_hs_conn *conn,
                             struct ble_att_read_type_req *req,
                             void *uuid128);
int ble_att_clt_rx_read_type(struct ble_hs_conn *conn,
                             struct ble_l2cap_chan *chan,
                             struct os_mbuf **rxom);
int ble_att_clt_tx_read_group_type(struct ble_hs_conn *conn,
                                   struct ble_att_read_group_type_req *req,
                                   void *uuid128);
int ble_att_clt_rx_read_group_type(struct ble_hs_conn *conn,
                                   struct ble_l2cap_chan *chan,
                                   struct os_mbuf **rxom);
int ble_att_clt_tx_find_info(struct ble_hs_conn *conn,
                             struct ble_att_find_info_req *req);
int ble_att_clt_rx_find_info(struct ble_hs_conn *conn,
                             struct ble_l2cap_chan *chan,
                             struct os_mbuf **rxom);
int ble_att_clt_tx_find_type_value(struct ble_hs_conn *conn,
                                   struct ble_att_find_type_value_req *req,
                                   void *attribute_value, int value_len);
int ble_att_clt_rx_find_type_value(struct ble_hs_conn *conn,
                                   struct ble_l2cap_chan *chan,
                                   struct os_mbuf **rxom);

#endif
