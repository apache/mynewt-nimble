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

#include <string.h>
#include <errno.h>
#include "console/console.h"
#include "nimble/ble.h"
#include "ble_hs_priv.h"
#include "ble_l2cap_priv.h"
#include "ble_l2cap_coc_priv.h"
#include "ble_l2cap_sig_priv.h"

#if MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM) != 0

STAILQ_HEAD(ble_l2cap_coc_srv_list, ble_l2cap_coc_srv);

static struct ble_l2cap_coc_srv_list ble_l2cap_coc_srvs;

static os_membuf_t ble_l2cap_coc_srv_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM),
                    sizeof (struct ble_l2cap_coc_srv))
];

static struct os_mempool ble_l2cap_coc_srv_pool;

static void
ble_l2cap_coc_dbg_assert_srv_not_inserted(struct ble_l2cap_coc_srv *srv)
{
#if MYNEWT_VAL(BLE_HS_DEBUG)
    struct ble_l2cap_coc_srv *cur;

    STAILQ_FOREACH(cur, &ble_l2cap_coc_srvs, next) {
        BLE_HS_DBG_ASSERT(cur != srv);
    }
#endif
}

static struct ble_l2cap_coc_srv *
ble_l2cap_coc_srv_alloc(void)
{
    struct ble_l2cap_coc_srv *srv;

    srv = os_memblock_get(&ble_l2cap_coc_srv_pool);
    if (srv != NULL) {
        memset(srv, 0, sizeof(*srv));
    }

    return srv;
}

int
ble_l2cap_coc_create_server(uint16_t psm, uint16_t mtu,
                                        ble_l2cap_event_fn *cb, void *cb_arg)
{
    struct ble_l2cap_coc_srv * srv;

    srv = ble_l2cap_coc_srv_alloc();
    if (!srv) {
            return BLE_HS_ENOMEM;
    }

    srv->psm = psm;
    srv->mtu = mtu;
    srv->cb = cb;
    srv->cb_arg = cb_arg;

    ble_l2cap_coc_dbg_assert_srv_not_inserted(srv);

    STAILQ_INSERT_HEAD(&ble_l2cap_coc_srvs, srv, next);

    return 0;
}

uint16_t
ble_l2cap_coc_get_cid(void)
{
    static uint16_t next_cid = BLE_L2CAP_COC_CID_START;

    if (next_cid > BLE_L2CAP_COC_CID_END) {
            next_cid = BLE_L2CAP_COC_CID_START;
    }

    /*TODO: Make it smarter*/
    return next_cid++;
}

struct ble_l2cap_coc_srv *
ble_l2cap_coc_srv_find(uint16_t psm)
{
    struct ble_l2cap_coc_srv *cur, *srv;

    srv = NULL;
    STAILQ_FOREACH(cur, &ble_l2cap_coc_srvs, next) {
        if (cur->psm == psm) {
                srv = cur;
                break;
        }
    }

    return srv;
}

int
ble_l2cap_coc_init(void)
{
    STAILQ_INIT(&ble_l2cap_coc_srvs);

    return os_mempool_init(&ble_l2cap_coc_srv_pool,
                         MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM),
                         sizeof (struct ble_l2cap_coc_srv),
                         ble_l2cap_coc_srv_mem,
                         "ble_l2cap_coc_srv_pool");
}

#endif
