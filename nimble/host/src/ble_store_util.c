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

#include "host/ble_store.h"
#include "ble_hs_priv.h"

struct ble_store_util_peer_set {
    ble_addr_t *peer_id_addrs;
    int num_peers;
    int max_peers;
    int status;
};

static int
ble_store_util_iter_unique_peer(int obj_type,
                                union ble_store_value *val,
                                void *arg)
{
    struct ble_store_util_peer_set *set;
    int i;

    BLE_HS_DBG_ASSERT(obj_type == BLE_STORE_OBJ_TYPE_OUR_SEC ||
                      obj_type == BLE_STORE_OBJ_TYPE_PEER_SEC);

    set = arg;

    /* Do nothing if this peer is a duplicate. */
    for (i = 0; i < set->num_peers; i++) {
        if (ble_addr_cmp(set->peer_id_addrs + i, &val->sec.peer_addr) == 0) {
            return 0;
        }
    }

    if (set->num_peers >= set->max_peers) {
        /* Overflow; abort the iterate procedure. */
        set->status = BLE_HS_ENOMEM;
        return 1;
    }

    set->peer_id_addrs[set->num_peers] = val->sec.peer_addr;
    set->num_peers++;

    return 0;
}

int
ble_store_util_bonded_peers(ble_addr_t *out_peer_id_addrs, int *out_num_peers,
                            int max_peers)
{
    struct ble_store_util_peer_set set = {
        .peer_id_addrs = out_peer_id_addrs,
        .num_peers = 0,
        .max_peers = max_peers,
        .status = 0,
    };
    int rc;

    rc = ble_store_iterate(BLE_STORE_OBJ_TYPE_OUR_SEC,
                           ble_store_util_iter_unique_peer,
                           &set);
    if (rc != 0) {
        return rc;
    }

    *out_num_peers = set.num_peers;
    return 0;
}

int
ble_store_util_delete_peer(const ble_addr_t *peer_id_addr)
{
    union ble_store_key key;
    int rc;

    memset(&key, 0, sizeof key);
    key.sec.peer_addr = *peer_id_addr;

    rc = ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_OUR_SEC, &key);
    if (rc != 0) {
        return rc;
    }

    rc = ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_PEER_SEC, &key);
    if (rc != 0) {
        return rc;
    }

    memset(&key, 0, sizeof key);
    key.cccd.peer_addr = *peer_id_addr;

    rc = ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_CCCD, &key);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_store_util_delete_all(int type, const union ble_store_key *key)
{
    int rc;

    do {
        rc = ble_store_delete(type, key);
    } while (rc == 0);

    if (rc != BLE_HS_ENOENT) {
        return rc;
    }

    return 0;
}

static int
ble_store_util_iter_count(int obj_type,
                          union ble_store_value *val,
                          void *arg)
{
    int *count;

    count = arg;
    (*count)++;

    return 0;
}

int
ble_store_util_count(int type, int *out_count)
{
    int rc;

    *out_count = 0;
    rc = ble_store_iterate(type,
                           ble_store_util_iter_count,
                           out_count);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_store_util_delete_oldest_peer(void)
{
#if MYNEWT_VAL(BLE_STORE_MAX_BONDS)
    ble_addr_t peer_id_addrs[MYNEWT_VAL(BLE_STORE_MAX_BONDS)];
    int num_peers;
    int rc;

    rc = ble_store_util_bonded_peers(
            peer_id_addrs, &num_peers,
            sizeof peer_id_addrs / sizeof peer_id_addrs[0]);
    if (rc != 0) {
        return rc;
    }

    if (num_peers == 0) {
        return 0;
    }

    rc = ble_store_util_delete_peer(&peer_id_addrs[0]);
    if (rc != 0) {
        return rc;
    }
#endif
    return 0;
}

int
ble_store_util_status_rr(struct ble_store_status_event *event, void *arg)
{
    switch (event->event_code) {
    case BLE_STORE_EVENT_OVERFLOW:
        switch (event->overflow.obj_type) {
        case BLE_STORE_OBJ_TYPE_OUR_SEC:
        case BLE_STORE_OBJ_TYPE_PEER_SEC:
            return ble_gap_unpair_oldest_peer();
        case BLE_STORE_OBJ_TYPE_CCCD:
            /* Try unpairing oldest peer except current peer */
            return ble_gap_unpair_oldest_except(&event->overflow.value->cccd.peer_addr);

        default:
            return BLE_HS_EUNKNOWN;
        }

    case BLE_STORE_EVENT_FULL:
        /* Just proceed with the operation.  If it results in an overflow,
         * we'll delete a record when the overflow occurs.
         */
        return 0;

    default:
        return BLE_HS_EUNKNOWN;
    }
}

struct ble_store_util_lru_peer {
    ble_addr_t peer_addr;
    uint16_t bond_count;
    int found;
    const ble_addr_t *except;
};

static int
ble_store_util_iter_lru_peer(int obj_type, union ble_store_value *val, void *arg)
{
    struct ble_store_util_lru_peer *lru;

    BLE_HS_DBG_ASSERT(obj_type == BLE_STORE_OBJ_TYPE_OUR_SEC);

    lru = arg;

    if (lru->except != NULL && ble_addr_cmp(lru->except, &val->sec.peer_addr) == 0) {
        return 0;
    }

    if (!lru->found || val->sec.bond_count < lru->bond_count) {
        lru->peer_addr = val->sec.peer_addr;
        lru->bond_count = val->sec.bond_count;
        lru->found = 1;
    }

    return 0;
}

static int
ble_store_util_find_lru_peer(ble_addr_t *out_peer_addr, const ble_addr_t *except)
{
    struct ble_store_util_lru_peer lru = {
        .found = 0,
        .except = except,
    };
    int rc;

    rc = ble_store_iterate(BLE_STORE_OBJ_TYPE_OUR_SEC,
                           ble_store_util_iter_lru_peer, &lru);
    if (rc != 0) {
        return rc;
    }

    if (!lru.found) {
        return BLE_HS_ENOENT;
    }

    *out_peer_addr = lru.peer_addr;
    return 0;
}

static int
ble_store_util_unpair_lru_peer(void)
{
    ble_addr_t peer_addr;
    int rc;

    rc = ble_store_util_find_lru_peer(&peer_addr, NULL);
    if (rc != 0) {
        return rc;
    }

    return ble_gap_unpair(&peer_addr);
}

static int
ble_store_util_unpair_lru_except(const ble_addr_t *except)
{
    ble_addr_t peer_addr;
    int rc;

    rc = ble_store_util_find_lru_peer(&peer_addr, except);
    if (rc != 0) {
        return rc;
    }

    return ble_gap_unpair(&peer_addr);
}

int
ble_store_util_status_lru(struct ble_store_status_event *event, void *arg)
{
    switch (event->event_code) {
    case BLE_STORE_EVENT_OVERFLOW:
        switch (event->overflow.obj_type) {
        case BLE_STORE_OBJ_TYPE_OUR_SEC:
        case BLE_STORE_OBJ_TYPE_PEER_SEC:
            return ble_store_util_unpair_lru_peer();
        case BLE_STORE_OBJ_TYPE_CCCD:
            /* Try unpairing LRU peer except current peer */
            return ble_store_util_unpair_lru_except(
                &event->overflow.value->cccd.peer_addr);

        default:
            return BLE_HS_EUNKNOWN;
        }

    case BLE_STORE_EVENT_FULL:
        /* Just proceed with the operation.  If it results in an overflow,
         * we'll delete a record when the overflow occurs.
         */
        return 0;

    default:
        return BLE_HS_EUNKNOWN;
    }
}
