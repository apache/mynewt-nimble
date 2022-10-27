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

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "host/ble_hs.h"
#include "host/ble_gattc_peer.h"

static void *ble_gattc_p_svc_mem;
static struct os_mempool ble_gattc_p_svc_pool;

static void *ble_gattc_p_chr_mem;
static struct os_mempool ble_gattc_p_chr_pool;

static void *ble_gattc_p_dsc_mem;
static struct os_mempool ble_gattc_p_dsc_pool;

static void *ble_gattc_p_mem;
static struct os_mempool ble_gattc_p_pool;
static SLIST_HEAD(, ble_gattc_p) ble_gattc_ps;

static bool disc_after_ind_rcvd;

static struct ble_gattc_p_svc *
ble_gattc_p_svc_find_range(struct ble_gattc_p *ble_gattc_p, uint16_t attr_handle);

static struct ble_gattc_p_svc *
ble_gattc_p_svc_find(struct ble_gattc_p *ble_gattc_p, uint16_t svc_start_handle,
                     struct ble_gattc_p_svc **out_prev);

int
ble_gattc_p_svc_is_empty(const struct ble_gattc_p_svc *svc);

uint16_t
ble_gattc_p_chr_end_handle(const struct ble_gattc_p_svc *svc, const struct ble_gattc_p_chr *chr);

int
ble_gattc_p_chr_is_empty(const struct ble_gattc_p_svc *svc, const struct ble_gattc_p_chr *chr);

static struct ble_gattc_p_chr *
ble_gattc_p_chr_find(const struct ble_gattc_p_svc *svc, uint16_t chr_def_handle,
                     struct ble_gattc_p_chr **out_prev);

static struct ble_gattc_p_chr *
ble_gattc_p_chr_find_handle(const struct ble_gattc_p_svc *svc, uint16_t dsc_handle);

static void
ble_gattc_p_disc_chrs(struct ble_gattc_p *ble_gattc_p);

static void
ble_gattc_p_disc_dscs(struct ble_gattc_p *peer);

struct ble_gattc_p *
ble_gattc_p_find(uint16_t conn_handle)
{
    struct ble_gattc_p *ble_gattc_p;

    SLIST_FOREACH(ble_gattc_p, &ble_gattc_ps, next) {
        if (ble_gattc_p->conn_handle == conn_handle) {
            return ble_gattc_p;
        }
    }

    return NULL;
}

struct ble_gattc_p *
ble_gattc_p_find_by_addr(ble_addr_t peer_addr)
{
    struct ble_gattc_p *ble_gattc_p;
    SLIST_FOREACH(ble_gattc_p, &ble_gattc_ps, next) {
        if (memcmp(&ble_gattc_p->ble_gattc_p_addr, &peer_addr, sizeof(peer_addr)) == 0) {
            return ble_gattc_p;
        }
    }
    return NULL;
}

static struct ble_gattc_p_dsc *
ble_gattc_p_dsc_find_prev(const struct ble_gattc_p_chr *chr, uint16_t dsc_handle)
{
    struct ble_gattc_p_dsc *prev;
    struct ble_gattc_p_dsc *dsc;

    prev = NULL;
    SLIST_FOREACH(dsc, &chr->dscs, next) {
        if (dsc->dsc.handle >= dsc_handle) {
            break;
        }

        prev = dsc;
    }

    return prev;
}

static struct ble_gattc_p_dsc *
ble_gattc_p_dsc_find(const struct ble_gattc_p_chr *chr, uint16_t dsc_handle,
                     struct ble_gattc_p_dsc **out_prev)
{
    struct ble_gattc_p_dsc *prev;
    struct ble_gattc_p_dsc *dsc;

    prev = ble_gattc_p_dsc_find_prev(chr, dsc_handle);
    if (prev == NULL) {
        dsc = SLIST_FIRST(&chr->dscs);
    } else {
        dsc = SLIST_NEXT(prev, next);
    }

    if (dsc != NULL && dsc->dsc.handle != dsc_handle) {
        dsc = NULL;
    }

    if (out_prev != NULL) {
        *out_prev = prev;
    }
    return dsc;
}

int
ble_gattc_p_dsc_add(ble_addr_t peer_addr, uint16_t chr_val_handle,
                    const struct ble_gatt_dsc *gatt_dsc)
{
    struct ble_gattc_p_dsc *prev;
    struct ble_gattc_p_dsc *dsc;
    struct ble_gattc_p_svc *svc;
    struct ble_gattc_p_chr *chr;
    struct ble_gattc_p *peer;

    peer = ble_gattc_p_find_by_addr(peer_addr);
    if (peer == NULL) {
        BLE_HS_LOG(ERROR, "Peer not found");
        return BLE_HS_EUNKNOWN;
    }

    svc = ble_gattc_p_svc_find_range(peer, gatt_dsc->handle);
    if (svc == NULL) {
        /* Can't find service for discovered descriptor; this shouldn't
         * happen.
         */
        assert(0);
        return BLE_HS_EUNKNOWN;
    }

    if (chr_val_handle == 0) {
        chr = ble_gattc_p_chr_find_handle(svc, gatt_dsc->handle);
    } else {
        chr = ble_gattc_p_chr_find(svc, chr_val_handle, NULL);
    }

    if (chr == NULL) {
        /* Can't find characteristic for discovered descriptor; this shouldn't
         * happen.
         */
        BLE_HS_LOG(ERROR, "Couldn't find characteristc for dsc handle = %d", gatt_dsc->handle);
        assert(0);
        return BLE_HS_EUNKNOWN;
    }

    dsc = ble_gattc_p_dsc_find(chr, gatt_dsc->handle, &prev);
    if (dsc != NULL) {
        BLE_HS_LOG(DEBUG, "Descriptor already discovered.");
        /* Descriptor already discovered. */
        return 0;
    }

    dsc = os_memblock_get(&ble_gattc_p_dsc_pool);
    if (dsc == NULL) {
        /* Out of memory. */
        return BLE_HS_ENOMEM;
    }
    memset(dsc, 0, sizeof *dsc);

    dsc->dsc = *gatt_dsc;

    if (prev == NULL) {
        SLIST_INSERT_HEAD(&chr->dscs, dsc, next);
    } else {
        SLIST_NEXT(prev, next) = dsc;
    }

    BLE_HS_LOG(DEBUG, "Descriptor added with handle = %d", dsc->dsc.handle);

    return 0;
}

static struct ble_gattc_p_chr *
ble_gattc_p_chr_find_handle(const struct ble_gattc_p_svc *svc, uint16_t dsc_handle)
{
    struct ble_gattc_p_chr *chr;

    SLIST_FOREACH(chr, &svc->chrs, next) {
        if (chr->chr.val_handle == (dsc_handle - 1)) {
            return chr;
        }
    }
    return NULL;
}

uint16_t
ble_gattc_p_chr_end_handle(const struct ble_gattc_p_svc *svc, const struct ble_gattc_p_chr *chr)
{
    const struct ble_gattc_p_chr *next_chr;

    next_chr = SLIST_NEXT(chr, next);
    if (next_chr != NULL) {
        return next_chr->chr.def_handle - 1;
    } else {
        return svc->svc.end_handle;
    }
}

int
ble_gattc_p_chr_is_empty(const struct ble_gattc_p_svc *svc, const struct ble_gattc_p_chr *chr)
{
    return ble_gattc_p_chr_end_handle(svc, chr) <= chr->chr.val_handle;
}

static struct ble_gattc_p_chr *
ble_gattc_p_chr_find_prev(const struct ble_gattc_p_svc *svc, uint16_t chr_val_handle)
{
    struct ble_gattc_p_chr *prev;
    struct ble_gattc_p_chr *chr;

    prev = NULL;
    SLIST_FOREACH(chr, &svc->chrs, next) {
        if (chr->chr.val_handle >= chr_val_handle) {
            break;
        }

        prev = chr;
    }

    return prev;
}

static struct ble_gattc_p_chr *
ble_gattc_p_chr_find(const struct ble_gattc_p_svc *svc, uint16_t chr_val_handle,
                     struct ble_gattc_p_chr **out_prev)
{
    struct ble_gattc_p_chr *prev;
    struct ble_gattc_p_chr *chr;

    prev = ble_gattc_p_chr_find_prev(svc, chr_val_handle);
    if (prev == NULL) {
        chr = SLIST_FIRST(&svc->chrs);
    } else {
        chr = SLIST_NEXT(prev, next);
    }

    if (chr != NULL && chr->chr.val_handle != chr_val_handle) {
        chr = NULL;
    }

    if (out_prev != NULL) {
        *out_prev = prev;
    }
    return chr;
}

static void
ble_gattc_p_chr_delete(struct ble_gattc_p_chr *chr)
{
    struct ble_gattc_p_dsc *dsc;

    while ((dsc = SLIST_FIRST(&chr->dscs)) != NULL) {
        SLIST_REMOVE_HEAD(&chr->dscs, next);
        os_memblock_put(&ble_gattc_p_dsc_pool, dsc);
    }

    os_memblock_put(&ble_gattc_p_chr_pool, chr);
}

int
ble_gattc_p_chr_add(ble_addr_t peer_addr, uint16_t svc_start_handle,
                    const struct ble_gatt_chr *gatt_chr)
{
    struct ble_gattc_p_chr *prev;
    struct ble_gattc_p_chr *chr;
    struct ble_gattc_p_svc *svc;
    struct ble_gattc_p *peer;

    peer = ble_gattc_p_find_by_addr(peer_addr);
    if (peer == NULL) {
        BLE_HS_LOG(ERROR, "Peer not found");
        return BLE_HS_EUNKNOWN;
    }

    if (svc_start_handle == 0) {
        svc = ble_gattc_p_svc_find_range(peer, gatt_chr->val_handle);
    } else {
        svc = ble_gattc_p_svc_find(peer, svc_start_handle, NULL);
    }

    if (svc == NULL) {
        /* Can't find service for discovered characteristic; this shouldn't
         * happen.
         */
        assert(0);
        return BLE_HS_EUNKNOWN;
    }

    chr = ble_gattc_p_chr_find(svc, gatt_chr->def_handle, &prev);
    if (chr != NULL) {
        /* Characteristic already discovered. */
        return 0;
    }

    chr = os_memblock_get(&ble_gattc_p_chr_pool);
    if (chr == NULL) {
        /* Out of memory. */
        return BLE_HS_ENOMEM;
    }
    memset(chr, 0, sizeof *chr);

    chr->chr = *gatt_chr;

    if (prev == NULL) {
        SLIST_INSERT_HEAD(&svc->chrs, chr, next);
    } else {
        SLIST_NEXT(prev, next) = chr;
    }

#if MYNEWT_VAL(BLE_GATT_CACHING)
    ble_gattc_db_hash_chr_present(chr->chr.uuid.u16);
#endif

    BLE_HS_LOG(DEBUG, "Characteristc added with def_handle = %d and val_handle = %d",
                   chr->chr.def_handle, chr->chr.val_handle);

    return 0;
}

int
ble_gattc_p_svc_is_empty(const struct ble_gattc_p_svc *svc)
{
    return svc->svc.end_handle <= svc->svc.start_handle;
}

static struct ble_gattc_p_svc *
ble_gattc_p_svc_find_prev(struct ble_gattc_p *peer, uint16_t svc_start_handle)
{
    struct ble_gattc_p_svc *prev;
    struct ble_gattc_p_svc *svc;

    prev = NULL;
    SLIST_FOREACH(svc, &peer->svcs, next) {
        if (svc->svc.start_handle >= svc_start_handle) {
            break;
        }

        prev = svc;
    }

    return prev;
}

static struct ble_gattc_p_svc *
ble_gattc_p_svc_find(struct ble_gattc_p *ble_gattc_p, uint16_t svc_start_handle,
                     struct ble_gattc_p_svc **out_prev)
{
    struct ble_gattc_p_svc *prev;
    struct ble_gattc_p_svc *svc;

    prev = ble_gattc_p_svc_find_prev(ble_gattc_p, svc_start_handle);
    if (prev == NULL) {
        svc = SLIST_FIRST(&ble_gattc_p->svcs);
    } else {
        svc = SLIST_NEXT(prev, next);
    }

    if (svc != NULL && svc->svc.start_handle != svc_start_handle) {
        svc = NULL;
    }

    if (out_prev != NULL) {
        *out_prev = prev;
    }
    return svc;
}

static struct ble_gattc_p_svc *
ble_gattc_p_svc_find_range(struct ble_gattc_p *ble_gattc_p, uint16_t attr_handle)
{
    struct ble_gattc_p_svc *svc;

    SLIST_FOREACH(svc, &ble_gattc_p->svcs, next) {
        if (svc->svc.start_handle <= attr_handle &&
            svc->svc.end_handle >= attr_handle) {

            return svc;
        }
    }

    return NULL;
}

const struct ble_gattc_p_svc *
ble_gattc_p_svc_find_uuid(const struct ble_gattc_p *ble_gattc_p, const ble_uuid_t *uuid)
{
    const struct ble_gattc_p_svc *svc;

    SLIST_FOREACH(svc, &ble_gattc_p->svcs, next) {
        if (ble_uuid_cmp(&svc->svc.uuid.u, uuid) == 0) {
            return svc;
        }
    }

    return NULL;
}

const struct ble_gattc_p_chr *
ble_gattc_p_chr_find_uuid(const struct ble_gattc_p *ble_gattc_p, const ble_uuid_t *svc_uuid,
                          const ble_uuid_t *chr_uuid)
{
    const struct ble_gattc_p_svc *svc;
    const struct ble_gattc_p_chr *chr;

    svc = ble_gattc_p_svc_find_uuid(ble_gattc_p, svc_uuid);
    if (svc == NULL) {
        return NULL;
    }

    SLIST_FOREACH(chr, &svc->chrs, next) {
        if (ble_uuid_cmp(&chr->chr.uuid.u, chr_uuid) == 0) {
            return chr;
        }
    }

    return NULL;
}

const struct ble_gattc_p_dsc *
ble_gattc_p_dsc_find_uuid(const struct ble_gattc_p *ble_gattc_p, const ble_uuid_t *svc_uuid,
                          const ble_uuid_t *chr_uuid, const ble_uuid_t *dsc_uuid)
{
    const struct ble_gattc_p_chr *chr;
    const struct ble_gattc_p_dsc *dsc;

    chr = ble_gattc_p_chr_find_uuid(ble_gattc_p, svc_uuid, chr_uuid);
    if (chr == NULL) {
        return NULL;
    }

    SLIST_FOREACH(dsc, &chr->dscs, next) {
        if (ble_uuid_cmp(&dsc->dsc.uuid.u, dsc_uuid) == 0) {
            return dsc;
        }
    }

    return NULL;
}

int
ble_gattc_p_svc_add(ble_addr_t peer_addr, const struct ble_gatt_svc *gatt_svc)
{
    struct ble_gattc_p_svc *prev;
    struct ble_gattc_p_svc *svc;
    struct ble_gattc_p *peer;

    peer = ble_gattc_p_find_by_addr(peer_addr);
    if (peer == NULL) {
        BLE_HS_LOG(ERROR, "Peer not found");
        return BLE_HS_EUNKNOWN;
    }

    svc = ble_gattc_p_svc_find(peer, gatt_svc->start_handle, &prev);
    if (svc != NULL) {
        /* Service already discovered. */
        return 0;
    }

    svc = os_memblock_get(&ble_gattc_p_svc_pool);
    if (svc == NULL) {
        /* Out of memory. */
        return BLE_HS_ENOMEM;
    }
    memset(svc, 0, sizeof *svc);

    svc->svc = *gatt_svc;
    SLIST_INIT(&svc->chrs);

    if (prev == NULL) {
        SLIST_INSERT_HEAD(&peer->svcs, svc, next);
    } else {
        SLIST_INSERT_AFTER(prev, svc, next);
    }

    BLE_HS_LOG(DEBUG, "Service added with start_handle = %d and end_handle = %d",
                   gatt_svc->start_handle, gatt_svc->end_handle);

    return 0;
}

static void
ble_gattc_p_svc_delete(struct ble_gattc_p_svc *svc)
{
    struct ble_gattc_p_chr *chr;

    while ((chr = SLIST_FIRST(&svc->chrs)) != NULL) {
        SLIST_REMOVE_HEAD(&svc->chrs, next);
        ble_gattc_p_chr_delete(chr);
    }

    os_memblock_put(&ble_gattc_p_svc_pool, svc);
}

int
ble_gattc_p_delete(uint16_t conn_handle)
{
    struct ble_gattc_p_svc *svc;
    struct ble_gattc_p *ble_gattc_p;
    int rc;

    ble_gattc_p = ble_gattc_p_find(conn_handle);
    if (ble_gattc_p == NULL) {
        return BLE_HS_ENOTCONN;
    }

    SLIST_REMOVE(&ble_gattc_ps, ble_gattc_p, ble_gattc_p, next);

    while ((svc = SLIST_FIRST(&ble_gattc_p->svcs)) != NULL) {
        SLIST_REMOVE_HEAD(&ble_gattc_p->svcs, next);
        ble_gattc_p_svc_delete(svc);

    }

    rc = os_memblock_put(&ble_gattc_p_pool, ble_gattc_p);
    if (rc != 0) {
        return BLE_HS_EOS;
    }

    return 0;
}

int
ble_gattc_p_add(uint16_t conn_handle, ble_addr_t ble_gattc_p_addr)
{
    struct ble_gattc_p *ble_gattc_p;

    /* Make sure the connection handle is unique. */
    ble_gattc_p = ble_gattc_p_find(conn_handle);
    if (ble_gattc_p != NULL) {
        return BLE_HS_EALREADY;
    }

    ble_gattc_p = os_memblock_get(&ble_gattc_p_pool);
    if (ble_gattc_p == NULL) {
        /* Out of memory. */
        return BLE_HS_ENOMEM;
    }

    memset(ble_gattc_p, 0, sizeof *ble_gattc_p);
    ble_gattc_p->conn_handle = conn_handle;
    memcpy(&ble_gattc_p->ble_gattc_p_addr, &ble_gattc_p_addr, sizeof(ble_addr_t));
    SLIST_INSERT_HEAD(&ble_gattc_ps, ble_gattc_p, next);

    return 0;
}

size_t
ble_gattc_p_get_db_size(struct ble_gattc_p *peer)
{
    if (peer == NULL) {
        return 0;
    }

    size_t db_size = 0;
    struct ble_gattc_p_svc *svc;
    struct ble_gattc_p_chr *chr;
    struct ble_gattc_p_dsc *dsc;

    SLIST_FOREACH(peer, &ble_gattc_ps, next) {
        SLIST_FOREACH(svc, &peer->svcs, next) {
            db_size++;
            SLIST_FOREACH(chr, &svc->chrs, next) {
                db_size++;
                SLIST_FOREACH(dsc, &chr->dscs, next) {
                    db_size++;
                }
            }
        }
    }

    return db_size;
}

#if MYNEWT_VAL(BLE_GATT_CACHING)
static void
ble_gattc_p_cache_peer(struct ble_gattc_p *peer)
{
    /* Cache the discovered peer */
    size_t db_size = ble_gattc_p_get_db_size(peer);
    ble_gattc_cache_save(peer, db_size);
}

void
ble_gattc_p_load_hash(ble_addr_t peer_addr, ble_hash_key hash_key)
{
    struct ble_gattc_p *peer;
    peer = ble_gattc_p_find_by_addr(peer_addr);

    if (peer == NULL) {
        BLE_HS_LOG(ERROR, "%s peer NULL", __func__);
    } else {
        BLE_HS_LOG(DEBUG, "Saved hash for peer");
        memcpy(&peer->database_hash, hash_key, sizeof(ble_hash_key));
    }
}
#endif

static void
ble_gattc_p_disc_complete(struct ble_gattc_p *peer, int rc)
{
    peer->disc_prev_chr_val = 0;

    /* Notify caller that discovery has completed. */
    if (peer->disc_cb != NULL) {
        peer->disc_cb(peer, rc, peer->disc_cb_arg);
    }
}

static int
ble_gattc_p_dsc_disced(uint16_t conn_handle, const struct ble_gatt_error *error,
                       uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                       void *arg)
{
    struct ble_gattc_p *peer;
    int rc;

    peer = arg;
    assert(peer->conn_handle == conn_handle);

    switch (error->status) {
    case 0:
        rc = ble_gattc_p_dsc_add(peer->ble_gattc_p_addr, chr_val_handle, dsc);
        break;

    case BLE_HS_EDONE:
        /* All descriptors in this characteristic discovered; start discovering
         * descriptors in the next characteristic.
         */
        if (peer->disc_prev_chr_val > 0) {
            ble_gattc_p_disc_dscs(peer);
        }
        rc = 0;
        break;

    default:
        /* Error; abort discovery. */
        rc = error->status;
        break;
    }

    if (rc != 0) {
        /* Error; abort discovery. */
        ble_gattc_p_disc_complete(peer, rc);
    }

    return rc;
}

static void
ble_gattc_p_disc_dscs(struct ble_gattc_p *peer)
{
    struct ble_gattc_p_chr *chr;
    struct ble_gattc_p_svc *svc;
    int rc;

    /* Search through the list of discovered characteristics for the first
     * characteristic that contains undiscovered descriptors.  Then, discover
     * all descriptors belonging to that characteristic.
     */
    SLIST_FOREACH(svc, &peer->svcs, next) {
        SLIST_FOREACH(chr, &svc->chrs, next) {
            if (!ble_gattc_p_chr_is_empty(svc, chr) &&
                SLIST_EMPTY(&chr->dscs) &&
                (peer->disc_prev_chr_val <= chr->chr.def_handle)) {

                rc = ble_gattc_disc_all_dscs(peer->conn_handle,
                                             chr->chr.val_handle,
                                             ble_gattc_p_chr_end_handle(svc, chr),
                                             ble_gattc_p_dsc_disced, peer);
                if (rc != 0) {
                    ble_gattc_p_disc_complete(peer, rc);
                }

                peer->disc_prev_chr_val = chr->chr.val_handle;
                return;
            }
        }
    }

#if MYNEWT_VAL(BLE_GATT_CACHING)
    ble_gattc_p_cache_peer(peer);
#endif
    /* All descriptors discovered. */
    ble_gattc_p_disc_complete(peer, 0);
}


static int
ble_gattc_p_chr_disced(uint16_t conn_handle, const struct ble_gatt_error *error,
                       const struct ble_gatt_chr *chr, void *arg)
{
    struct ble_gattc_p *peer;
    int rc;

    peer = arg;
    assert(peer->conn_handle == conn_handle);

    switch (error->status) {
    case 0:
        rc = ble_gattc_p_chr_add(peer->ble_gattc_p_addr, peer->cur_svc->svc.start_handle, chr);
        break;

    case BLE_HS_EDONE:
        /* All characteristics in this service discovered; start discovering
         * characteristics in the next service.
         */
        if (peer->disc_prev_chr_val > 0) {
            ble_gattc_p_disc_chrs(peer);
        }
        rc = 0;
        break;

    default:
        rc = error->status;
        break;
    }

    if (rc != 0) {
        /* Error; abort discovery. */
        ble_gattc_p_disc_complete(peer, rc);
    }

    return rc;
}

static void
ble_gattc_p_disc_chrs(struct ble_gattc_p *peer)
{
    struct ble_gattc_p_svc *svc;
    int rc;
    /* Search through the list of discovered service for the first service that
     * contains undiscovered characteristics.  Then, discover all
     * characteristics belonging to that service.
     */
    SLIST_FOREACH(svc, &peer->svcs, next) {
        if (!ble_gattc_p_svc_is_empty(svc) && SLIST_EMPTY(&svc->chrs)) {
            peer->cur_svc = svc;
            rc = ble_gattc_disc_all_chrs(peer->conn_handle,
                                         svc->svc.start_handle,
                                         svc->svc.end_handle,
                                         ble_gattc_p_chr_disced, peer);
            if (rc != 0) {
                ble_gattc_p_disc_complete(peer, rc);
            }
            return;
        }
    }

    /* All characteristics discovered. */
    ble_gattc_p_disc_dscs(peer);
}

static int
ble_gattc_p_svc_disced(uint16_t conn_handle, const struct ble_gatt_error *error,
                       const struct ble_gatt_svc *service, void *arg)
{
    struct ble_gattc_p *peer;
    int rc;

    peer = arg;
    assert(peer->conn_handle == conn_handle);

    switch (error->status) {
    case 0:
        rc = ble_gattc_p_svc_add(peer->ble_gattc_p_addr, service);
        break;

    case BLE_HS_EDONE:
        /* All services discovered; start discovering characteristics. */
        if (peer->disc_prev_chr_val > 0) {
            ble_gattc_p_disc_chrs(peer);
        }
        rc = 0;
        break;

    default:
        rc = error->status;
        break;
    }

    if (rc != 0) {
        /* Error; abort discovery. */
        ble_gattc_p_disc_complete(peer, rc);
    }

    return rc;
}

void
ble_gattc_p_undisc_all(struct ble_gattc_p *peer)
{
#if MYNEWT_VAL(BLE_GATT_CACHING)
    ble_gattc_cacheReset(&peer->ble_gattc_p_addr);
#endif

    struct ble_gattc_p_svc *svc;

    while ((svc = SLIST_FIRST(&peer->svcs)) != NULL) {
        SLIST_REMOVE_HEAD(&peer->svcs, next);
        ble_gattc_p_svc_delete(svc);
    }
}

int
ble_gattc_p_disc(uint16_t conn_handle, struct ble_gattc_p *peer)
{
    ble_gattc_p_undisc_all(peer);

    peer->disc_prev_chr_val = 1;

    return ble_gattc_disc_all_svcs(conn_handle, ble_gattc_p_svc_disced, peer);
}

#if MYNEWT_VAL(BLE_GATT_CACHING)
static int
ble_gattc_p_on_read(uint16_t conn_handle,
                    const struct ble_gatt_error *error,
                    struct ble_gatt_attr *attr,
                    void *arg)
{
    uint16_t res;

    BLE_HS_LOG(INFO, "%s Read complete; status=%d conn_handle=%d",__func__, error->status,
                conn_handle);
    if (error->status != 0) {
        res = error->status;
        goto disc;
    }
    BLE_HS_LOG(INFO, "attr_handle=%d value=", attr->handle);
    print_mbuf(attr->om);

    res = ble_gattc_cache_check_hash((struct ble_gattc_p *)arg, attr->om);
    if (res == 0) {
        BLE_HS_LOG(DEBUG, "Hash value is unchanged, no need for discovery");
        ble_gattc_p_disc_complete((struct ble_gattc_p *)arg, res);
        return 0;
    }

disc:
    ble_gattc_p_disc(conn_handle, (struct ble_gattc_p *)arg);

    return res;
}
#endif

int
ble_gattc_p_disc_all(uint16_t conn_handle, ble_addr_t peer_addr, ble_gattc_p_disc_fn *disc_cb,
                     void *disc_cb_arg)
{
    struct ble_gattc_p *peer;
    int rc;

    peer = ble_gattc_p_find(conn_handle);
    if (peer == NULL) {
        return BLE_HS_ENOTCONN;
    }

    peer->disc_cb = disc_cb;
    peer->disc_cb_arg = disc_cb_arg;

    /* Checking if client hash is updated */
#if MYNEWT_VAL(BLE_GATT_CACHING) && !disc_after_ind_rcvd
    rc = ble_gattc_cache_load(peer_addr);
    if (rc == 0) {

        struct ble_gattc_p_chr *chr;
        chr = ble_gattc_p_chr_find_uuid(peer,
                                        BLE_UUID16_DECLARE(BLE_GATT_SVC_UUID16),
                                        BLE_UUID16_DECLARE(BLE_SVC_GATT_CHR_DATABASE_HASH_UUID16));
        if (chr == NULL) {
            BLE_HS_LOG(ERROR, "Error: Peer doesn't support the Service "
                          "Change characteristic\n");
            goto disc;
        }

        rc = ble_gattc_read(peer->conn_handle, chr->chr.val_handle,
                            ble_gattc_p_on_read, peer);
        if (rc != 0) {
            BLE_HS_LOG(ERROR, "Error: Failed to read characteristic; rc = %d\n", rc);
            goto disc;
        }
        return rc;
    }

disc:
#endif
    rc = ble_gattc_p_disc(conn_handle, peer);

    return rc;
}

#if MYNEWT_VAL(BLE_GATT_CACHING)
int
ble_gattc_p_disc_after_ind(uint16_t conn_handle, uint16_t attr_handle,
                           struct os_mbuf *om)
{
    struct ble_gattc_p *peer;
    struct ble_gattc_p_chr *chr;
    int rc;

    peer = ble_gattc_p_find(conn_handle);
    if (peer == NULL) {
        return BLE_HS_ENOTCONN;
    }

    /* Check if attr_handle is of service change char */
    chr = ble_gattc_p_chr_find_uuid(peer, BLE_UUID16_DECLARE(BLE_GATT_SVC_UUID16),
                                    BLE_UUID16_DECLARE(BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16));

    if (chr == NULL) {
        BLE_HS_LOG(ERROR, "Cannot find service change characteristic");
        return 0;
    }

    if (chr->chr.val_handle != attr_handle) {
        BLE_HS_LOG(ERROR, "Indication not recevived on service change charactistic,"
                      " don't go for discovery");
        return 0;
    }

    ble_gattc_p_undisc_all(peer);
    peer->disc_prev_chr_val = 1;

    /* read_write_subscribe was being called in a loop, so made it NULL for now */
    peer->disc_cb = NULL;

    rc = ble_gattc_disc_all_svcs(conn_handle, ble_gattc_p_svc_disced, peer);
    if (rc != 0) {
        BLE_HS_LOG(ERROR, "Failed to discover services");
        return rc;
    }

    return 0;
}
#endif

void
ble_gattc_p_free_mem(void)
{
    free(ble_gattc_p_mem);
    ble_gattc_p_mem = NULL;

    free(ble_gattc_p_svc_mem);
    ble_gattc_p_svc_mem = NULL;

    free(ble_gattc_p_chr_mem);
    ble_gattc_p_chr_mem = NULL;

    free(ble_gattc_p_dsc_mem);
    ble_gattc_p_dsc_mem = NULL;
}

int
ble_gattc_p_init(int max_ble_gattc_ps, int max_svcs, int max_chrs, int max_dscs, void *storage_cb)
{
    int rc;

    disc_after_ind_rcvd = false;
    /* Free memory first in case this function gets called more than once. */
    ble_gattc_p_free_mem();

    ble_gattc_p_mem = malloc(
                   OS_MEMPOOL_BYTES(max_ble_gattc_ps, sizeof (struct ble_gattc_p)));
    if (ble_gattc_p_mem == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = os_mempool_init(&ble_gattc_p_pool, max_ble_gattc_ps,
                         sizeof (struct ble_gattc_p), ble_gattc_p_mem,
                         "ble_gattc_p_pool");
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    ble_gattc_p_svc_mem = malloc(
                       OS_MEMPOOL_BYTES(max_svcs, sizeof (struct ble_gattc_p_svc)));
    if (ble_gattc_p_svc_mem == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = os_mempool_init(&ble_gattc_p_svc_pool, max_svcs,
                         sizeof (struct ble_gattc_p_svc), ble_gattc_p_svc_mem,
                         "ble_gattc_p_svc_pool");
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    ble_gattc_p_chr_mem = malloc(
                       OS_MEMPOOL_BYTES(max_chrs, sizeof (struct ble_gattc_p_chr)));
    if (ble_gattc_p_chr_mem == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = os_mempool_init(&ble_gattc_p_chr_pool, max_chrs,
                         sizeof (struct ble_gattc_p_chr), ble_gattc_p_chr_mem,
                         "ble_gattc_p_chr_pool");
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    ble_gattc_p_dsc_mem = malloc(
                       OS_MEMPOOL_BYTES(max_dscs, sizeof (struct ble_gattc_p_dsc)));
    if (ble_gattc_p_dsc_mem == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = os_mempool_init(&ble_gattc_p_dsc_pool, max_dscs,
                         sizeof (struct ble_gattc_p_dsc), ble_gattc_p_dsc_mem,
                         "ble_gattc_p_dsc_pool");
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

#if MYNEWT_VAL(BLE_GATT_CACHING)
    rc = ble_gattc_cache_init(storage_cb);
#endif
    return 0;

err:
    ble_gattc_p_free_mem();
    return rc;
}
