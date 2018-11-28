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
#include "syscfg/syscfg.h"
#include "os/os.h"
#include "host/ble_hs_id.h"
#include "ble_hs_priv.h"

static SLIST_HEAD(, ble_hs_periodic_sync) ble_hs_periodic_sync_handles;
static struct os_mempool ble_hs_periodic_sync_pool;

static os_membuf_t ble_hs_psync_elem_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_MAX_PERIODIC_SCANNING_SM),
                    sizeof (struct ble_hs_periodic_sync))
];

int
ble_hs_periodic_sync_can_alloc(void)
{
#if !MYNEWT_VAL(BLE_PERIODIC_ADV)
    return 0;
#endif

    return (ble_hs_periodic_sync_pool.mp_num_free >= 1);
}

struct ble_hs_periodic_sync *
ble_hs_periodic_sync_alloc(void)
{
#if !MYNEWT_VAL(BLE_PERIODIC_ADV)
    return NULL;
#endif

    struct ble_hs_periodic_sync *psync;

    psync = os_memblock_get(&ble_hs_periodic_sync_pool);
    if (psync == NULL) {
        goto err;
    }
    memset(psync, 0, sizeof *psync);

    return psync;

err:
    ble_hs_periodic_sync_free(psync);
    return NULL;
}

void
ble_hs_periodic_sync_free(struct ble_hs_periodic_sync *psync)
{
#if !MYNEWT_VAL(BLE_PERIODIC_ADV)
    return;
#endif

    int rc;
    if (psync == NULL) {
        return;
    }

#if MYNEWT_VAL(BLE_HS_DEBUG)
    memset(psync, 0xff, sizeof *psync);
#endif
    rc = os_memblock_put(&ble_hs_periodic_sync_pool, psync);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);
}

void
ble_hs_periodic_sync_insert(struct ble_hs_periodic_sync *psync)
{
#if !MYNEWT_VAL(BLE_PERIODIC_ADV)
    return;
#endif

    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());

    BLE_HS_DBG_ASSERT_EVAL(
                       ble_hs_periodic_sync_find(psync->sync_handle) == NULL);

    SLIST_INSERT_HEAD(&ble_hs_periodic_sync_handles, psync, bhc_next);
}

void
ble_hs_periodic_sync_remove(struct ble_hs_periodic_sync *psync)
{
#if !MYNEWT_VAL(BLE_PERIODIC_ADV)
    return;
#endif

    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());

    SLIST_REMOVE(&ble_hs_periodic_sync_handles,
                 psync,
                 ble_hs_periodic_sync,
                 bhc_next);
}

struct ble_hs_periodic_sync *
ble_hs_periodic_sync_find(uint16_t sync_handle)
{
#if !MYNEWT_VAL(BLE_PERIODIC_ADV)
    return NULL;
#endif

    struct ble_hs_periodic_sync *psync;

    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());

    SLIST_FOREACH(psync, &ble_hs_periodic_sync_handles, bhc_next) {
        if (psync->sync_handle == sync_handle) {
            return psync;
        }
    }

    return NULL;
}

struct ble_hs_periodic_sync *
ble_hs_periodic_sync_find_assert(uint16_t sync_handle)
{
    struct ble_hs_periodic_sync *psync;

    psync = ble_hs_periodic_sync_find(sync_handle);
    BLE_HS_DBG_ASSERT(psync != NULL);

    return psync;
}

struct ble_hs_periodic_sync *
ble_hs_periodic_sync_find_by_adv_addr(const ble_addr_t *addr)
{
#if !MYNEWT_VAL(BLE_PERIODIC_ADV)
    return NULL;
#endif

    struct ble_hs_periodic_sync *psync;

    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());

    if (!addr) {
        return NULL;
    }

    SLIST_FOREACH(psync, &ble_hs_periodic_sync_handles, bhc_next) {
        if (ble_addr_cmp(&psync->advertiser_addr, addr) == 0) {
            return psync;
        }
    }

    return NULL;
}

struct ble_hs_periodic_sync *
ble_hs_periodic_sync_find_by_adv_sid(uint16_t sid)
{
#if !MYNEWT_VAL(BLE_PERIODIC_ADV)
    return 0;
#endif

    struct ble_hs_periodic_sync *psync;

    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());

    SLIST_FOREACH(psync, &ble_hs_periodic_sync_handles, bhc_next) {
        if (psync->adv_sid == sid) {
            return psync;
        }
    }

    return NULL;
}

int
ble_hs_periodic_sync_exists(uint16_t sync_handle)
{
#if !MYNEWT_VAL(BLE_PERIODIC_ADV)
    return 0;
#endif
    return ble_hs_periodic_sync_find(sync_handle) != NULL;
}

/**
 * Retrieves the first periodic discovery handle in the list.
 */
struct ble_hs_periodic_sync *
ble_hs_periodic_sync_first(void)
{
#if !MYNEWT_VAL(BLE_PERIODIC_ADV)
    return NULL;
#endif

    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());
    return SLIST_FIRST(&ble_hs_periodic_sync_handles);
}

int
ble_hs_periodic_sync_init(void)
{
    int rc;

    rc = os_mempool_init(&ble_hs_periodic_sync_pool,
                         MYNEWT_VAL(BLE_MAX_PERIODIC_SCANNING_SM),
                         sizeof (struct ble_hs_periodic_sync),
                         ble_hs_psync_elem_mem, "ble_hs_periodic_disc_pool");
    if (rc != 0) {
        return BLE_HS_EOS;
    }

    SLIST_INIT(&ble_hs_periodic_sync_handles);

    return 0;
}
