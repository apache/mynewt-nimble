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

#include <assert.h>
#include <stdlib.h>
#include "os/os.h"
#include "console/console.h"
#include "ble_hs_priv.h"

const uint8_t ble_hs_misc_null_addr[6];

int
ble_hs_misc_malloc_mempool(void **mem, struct os_mempool *pool,
                           int num_entries, int entry_size, char *name)
{
    int rc;

    *mem = malloc(OS_MEMPOOL_BYTES(num_entries, entry_size));
    if (*mem == NULL) {
        return BLE_HS_ENOMEM;
    }

    rc = os_mempool_init(pool, num_entries, entry_size, *mem, name);
    if (rc != 0) {
        free(*mem);
        return BLE_HS_EOS;
    }

    return 0;
}

int
ble_hs_misc_conn_chan_find(uint16_t conn_handle, uint16_t cid,
                           struct ble_hs_conn **out_conn,
                           struct ble_l2cap_chan **out_chan)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    conn = ble_hs_conn_find(conn_handle);
    if (conn == NULL) {
        chan = NULL;
        rc = BLE_HS_ENOTCONN;
    } else {
        chan = ble_hs_conn_chan_find(conn, cid);
        if (chan == NULL) {
            rc = BLE_HS_ENOTCONN;
        } else {
            rc = 0;
        }
    }

    if (out_conn != NULL) {
        *out_conn = conn;
    }
    if (out_chan != NULL) {
        *out_chan = chan;
    }

    return rc;
}

void
ble_hs_misc_conn_chan_find_reqd(uint16_t conn_handle, uint16_t cid,
                                struct ble_hs_conn **out_conn,
                                struct ble_l2cap_chan **out_chan)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    rc = ble_hs_misc_conn_chan_find(conn_handle, cid, &conn, &chan);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

    if (out_conn != NULL) {
        *out_conn = conn;
    }
    if (out_chan != NULL) {
        *out_chan = chan;
    }
}

uint8_t
ble_hs_misc_addr_type_to_id(uint8_t addr_type)
{
    switch (addr_type) {
    case BLE_ADDR_TYPE_PUBLIC:
    case BLE_ADDR_TYPE_RPA_PUB_DEFAULT:
         return BLE_ADDR_TYPE_PUBLIC;

    case BLE_ADDR_TYPE_RANDOM:
    case BLE_ADDR_TYPE_RPA_RND_DEFAULT:
         return BLE_ADDR_TYPE_RANDOM;

    default:
        BLE_HS_DBG_ASSERT(0);
        return BLE_ADDR_TYPE_PUBLIC;
    }
}
