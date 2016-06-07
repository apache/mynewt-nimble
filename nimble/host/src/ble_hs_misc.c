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

void
ble_hs_misc_log_mbuf(struct os_mbuf *om)
{
    uint8_t u8;
    int i;

    for (i = 0; i < OS_MBUF_PKTLEN(om); i++) {
        os_mbuf_copydata(om, i, 1, &u8);
        BLE_HS_LOG(DEBUG, "0x%02x ", u8);
    }
}

void
ble_hs_misc_log_flat_buf(void *data, int len)
{
    uint8_t *u8ptr;
    int i;

    u8ptr = data;
    for (i = 0; i < len; i++) {
        BLE_HS_LOG(DEBUG, "0x%02x ", u8ptr[i]);
    }
}

/**
 * Allocates an mbuf for use by the nimble host.
 */
struct os_mbuf *
ble_hs_misc_pkthdr(void)
{
    struct os_mbuf *om;
    int rc;

    om = os_msys_get_pkthdr(0, 0);
    if (om == NULL) {
        return NULL;
    }

    /* Make room in the buffer for various headers.  XXX Check this number. */
    if (om->om_omp->omp_databuf_len < 8) {
        rc = os_mbuf_free_chain(om);
        BLE_HS_DBG_ASSERT_EVAL(rc == 0);
        return NULL;
    }

    om->om_data += 8;

    return om;
}

int
ble_hs_misc_pullup_base(struct os_mbuf **om, int base_len)
{
    if (OS_MBUF_PKTLEN(*om) < base_len) {
        return BLE_HS_EBADDATA;
    }

    *om = os_mbuf_pullup(*om, base_len);
    if (*om == NULL) {
        return BLE_HS_ENOMEM;
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

int
ble_hs_misc_conn_chan_find_reqd(uint16_t conn_handle, uint16_t cid,
                                struct ble_hs_conn **out_conn,
                                struct ble_l2cap_chan **out_chan)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    rc = ble_hs_misc_conn_chan_find(conn_handle, cid, &conn, &chan);
    BLE_HS_DBG_ASSERT(conn == NULL || chan != NULL);

    if (out_conn != NULL) {
        *out_conn = conn;
    }
    if (out_chan != NULL) {
        *out_chan = chan;
    }

    return rc;
}

uint8_t
ble_hs_misc_addr_type_to_ident(uint8_t addr_type)
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
