/*
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

#ifndef H_L2CAP_COC_PRIV_
#define H_L2CAP_COC_PRIV_

#include <inttypes.h>
#include "syscfg/syscfg.h"
#include "os/queue.h"
#include "host/ble_l2cap.h"
#include "ble_l2cap_sig_priv.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_L2CAP_COC_MTU                       100
#define BLE_L2CAP_COC_CID_START                 0x0040
#define BLE_L2CAP_COC_CID_END                   0x007F

struct ble_l2cap_chan;

struct ble_l2cap_coc_endpoint {
    uint16_t mtu;
    uint16_t credits;
    struct os_mbuf *sdu;
};

struct ble_l2cap_coc_srv {
    STAILQ_ENTRY(ble_l2cap_coc_srv) next;
    uint16_t psm;
    uint16_t mtu;
    ble_l2cap_event_fn *cb;
    void *cb_arg;
};

#if MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM) != 0
int ble_l2cap_coc_init(void);
uint16_t ble_l2cap_coc_get_cid(void);
int ble_l2cap_coc_create_server(uint16_t psm, uint16_t mtu,
                                ble_l2cap_event_fn *cb, void *cb_arg);
struct ble_l2cap_coc_srv * ble_l2cap_coc_srv_find(uint16_t psm);
#else
#define ble_l2cap_coc_init()                                    0
#define ble_l2cap_coc_create_server(psm, mtu, cb, cb_arg)       BLE_HS_ENOTSUP
#endif

#ifdef __cplusplus
}
#endif

#endif /* H_L2CAP_COC_PRIV_ */
