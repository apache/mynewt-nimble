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

/*
 * Host-based resolution of peers' resolvable private addresses (RPAs), for
 * controllers without a resolving list (LL privacy).
 *
 * The host keeps the list it would otherwise program into the controller:
 * ble_hs_pvcy_add_entry() and ble_hs_pvcy_remove_entry() update it, so bonds
 * restored from the store at startup, new bonds and unpairing keep it in
 * sync. RPAs in advertising reports and connection complete events are
 * resolved against it and reported the way a controller reports resolved
 * addresses: the identity address with type BLE_ADDR_PUBLIC_ID or
 * BLE_ADDR_RANDOM_ID, and the RPA as the peer RPA of the connection.
 *
 * A connection to a peer's identity address is made to the RPA the peer was
 * last seen advertising with, since the controller cannot resolve it.
 *
 * Our own address is not affected: generating our RPAs still needs
 * controller privacy.
 */

#include <string.h>
#include "syscfg/syscfg.h"
#include "ble_hs_priv.h"

#if MYNEWT_VAL(BLE_HOST_BASED_PRIVACY) && MYNEWT_VAL(BLE_CONTROLLER)
#error "BLE_HOST_BASED_PRIVACY is for host-only builds"
#endif

#if MYNEWT_VAL(BLE_HOST_BASED_PRIVACY) && !NIMBLE_BLE_SM
#error "BLE_HOST_BASED_PRIVACY requires the security manager"
#endif

#if BLE_HS_RESOLV_ENABLED

#define BLE_HS_RESOLV_MAX MYNEWT_VAL(BLE_HOST_BASED_PRIVACY_LIST_SIZE)

struct ble_hs_resolv_entry {
    /* Identity address, type BLE_ADDR_PUBLIC or BLE_ADDR_RANDOM */
    ble_addr_t id_addr;
    uint8_t irk[16];
    /* RPA the peer was last seen with; type BLE_ADDR_RANDOM if valid */
    ble_addr_t last_rpa;
};

static struct ble_hs_resolv_entry ble_hs_resolv_list[BLE_HS_RESOLV_MAX];
static int ble_hs_resolv_cnt;

static const uint8_t ble_hs_resolv_zero_irk[16];

static struct ble_hs_resolv_entry *
ble_hs_resolv_find_id(const ble_addr_t *id_addr)
{
    uint8_t type = id_addr->type & 1;
    int i;

    for (i = 0; i < ble_hs_resolv_cnt; i++) {
        if (ble_hs_resolv_list[i].id_addr.type == type &&
            memcmp(ble_hs_resolv_list[i].id_addr.val, id_addr->val, 6) == 0) {
            return &ble_hs_resolv_list[i];
        }
    }

    return NULL;
}

static struct ble_hs_resolv_entry *
ble_hs_resolv_find_rpa(const uint8_t *rpa)
{
    uint8_t hash[3];
    int i;

    /* RPA is hash (3 LSB octets) || prand (3 MSB octets) */
    for (i = 0; i < ble_hs_resolv_cnt; i++) {
        if (ble_sm_alg_ah(ble_hs_resolv_list[i].irk, rpa + 3, hash) == 0 &&
            memcmp(hash, rpa, 3) == 0) {
            return &ble_hs_resolv_list[i];
        }
    }

    return NULL;
}

int
ble_hs_resolv_add(const ble_addr_t *id_addr, const uint8_t *irk)
{
    struct ble_hs_resolv_entry *entry;
    int rc = 0;

    /* Peers without an IRK use their identity address over the air. */
    if (memcmp(irk, ble_hs_resolv_zero_irk, 16) == 0) {
        return 0;
    }

    ble_hs_lock();

    entry = ble_hs_resolv_find_id(id_addr);
    if (entry == NULL) {
        if (ble_hs_resolv_cnt >= BLE_HS_RESOLV_MAX) {
            rc = BLE_HS_ENOMEM;
            goto done;
        }
        entry = &ble_hs_resolv_list[ble_hs_resolv_cnt++];
    }

    memset(entry, 0, sizeof(*entry));
    entry->id_addr.type = id_addr->type & 1;
    memcpy(entry->id_addr.val, id_addr->val, 6);
    memcpy(entry->irk, irk, 16);

done:
    ble_hs_unlock();
    return rc;
}

int
ble_hs_resolv_remove(const ble_addr_t *id_addr)
{
    struct ble_hs_resolv_entry *entry;
    int rc = BLE_HS_ENOENT;

    ble_hs_lock();

    entry = ble_hs_resolv_find_id(id_addr);
    if (entry != NULL) {
        *entry = ble_hs_resolv_list[--ble_hs_resolv_cnt];
        rc = 0;
    }

    ble_hs_unlock();
    return rc;
}

void
ble_hs_resolv_clear(void)
{
    ble_hs_lock();
    memset(ble_hs_resolv_list, 0, sizeof(ble_hs_resolv_list));
    ble_hs_resolv_cnt = 0;
    ble_hs_unlock();
}

int
ble_hs_resolv_addr(ble_addr_t *addr)
{
    struct ble_hs_resolv_entry *entry;
    int resolved = 0;

    ble_hs_lock();

    if (ble_hs_resolv_cnt == 0) {
        goto done;
    }

    if (BLE_ADDR_IS_RPA(addr)) {
        entry = ble_hs_resolv_find_rpa(addr->val);
        if (entry != NULL) {
            entry->last_rpa = *addr;
            addr->type = entry->id_addr.type + 2;
            memcpy(addr->val, entry->id_addr.val, 6);
            resolved = 1;
        }
    } else if (addr->type == BLE_ADDR_PUBLIC || addr->type == BLE_ADDR_RANDOM) {
        /* Peer seen with its identity address: connect to that, not to an
         * older RPA.
         */
        entry = ble_hs_resolv_find_id(addr);
        if (entry != NULL) {
            memset(&entry->last_rpa, 0, sizeof(entry->last_rpa));
        }
    }

done:
    ble_hs_unlock();
    return resolved;
}

void
ble_hs_resolv_conn_complete(struct ble_gap_conn_complete *evt)
{
    ble_addr_t addr;

    if (evt->peer_addr_type != BLE_ADDR_RANDOM) {
        return;
    }

    addr.type = evt->peer_addr_type;
    memcpy(addr.val, evt->peer_addr, 6);
    if (!ble_hs_resolv_addr(&addr)) {
        return;
    }

    memcpy(evt->peer_rpa, evt->peer_addr, 6);
    memcpy(evt->peer_addr, addr.val, 6);
    evt->peer_addr_type = addr.type;
}

int
ble_hs_resolv_peer_rpa(const ble_addr_t *peer_addr, ble_addr_t *out_rpa)
{
    struct ble_hs_resolv_entry *entry;
    int rc = BLE_HS_ENOENT;

    ble_hs_lock();

    entry = ble_hs_resolv_find_id(peer_addr);
    if (entry != NULL && entry->last_rpa.type == BLE_ADDR_RANDOM) {
        *out_rpa = entry->last_rpa;
        rc = 0;
    }

    ble_hs_unlock();
    return rc;
}

#endif
