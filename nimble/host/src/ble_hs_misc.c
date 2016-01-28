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
        console_printf("0x%02x ", u8);
    }
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
