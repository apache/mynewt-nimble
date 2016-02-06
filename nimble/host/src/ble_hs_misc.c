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
#include "ble_hs_conn.h"
#include "ble_gap_priv.h"
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
        console_printf("0x%02x ", u8);
    }
    console_printf("\n");
}

void
ble_hs_misc_log_flat_buf(void *data, int len)
{
    uint8_t *u8ptr;
    int i;

    u8ptr = data;
    for (i = 0; i < len; i++) {
        console_printf("0x%02x ", u8ptr[i]);
    }
}

void
ble_hs_misc_assert_no_locks(void)
{
    assert(!ble_hs_conn_locked_by_cur_task());
    assert(!ble_gattc_locked_by_cur_task());
    assert(!ble_gap_locked_by_cur_task());
}

/**
 * Allocates an mbuf for use by the nimble host.
 *
 * Lock restrictions: None.
 */
struct os_mbuf *
ble_hs_misc_pkthdr(void)
{
    struct os_mbuf *om;

    om = os_msys_get_pkthdr(0, 0);
    if (om == NULL) {
        return NULL;
    }

    /* Make room in the buffer for various headers.  XXX Check this number. */
    om->om_data += 8;

    return om;
}
