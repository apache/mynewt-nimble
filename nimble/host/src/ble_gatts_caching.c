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

/**
 * Server-side GATT robust caching (Vol 3, Part G, 2.5.2.1).
 *
 * Tracks a change-aware / change-unaware state for each client.  A
 * change-unaware client that enabled Robust Caching in its Client Supported
 * Features receives a Database Out Of Sync error (0x12) on its first ATT
 * request after the database changed, and becomes change-aware again by
 * reading the Database Hash characteristic, confirming a Service Changed
 * indication, or sending another request after the error.  For bonded peers
 * the Client Supported Features value and the change-aware state persist via
 * the BLE_STORE_OBJ_TYPE_CSFC store record.
 */

#include "syscfg/syscfg.h"

#if MYNEWT_VAL(BLE_GATT_CACHING)

#include <stdbool.h>
#include <string.h>
#include "os/endian.h"
#include "host/ble_store.h"
#include "ble_hs_priv.h"
#if MYNEWT_VAL(BLE_EATT_CHAN_NUM) > 0
#include "ble_eatt_priv.h"
#endif

/* The stored CSFC record must be able to hold the full RAM value. */
_Static_assert(BLE_STORE_CSFC_SZ >= BLE_GATT_CHR_CLI_SUP_FEAT_SZ,
               "CSFC store record too small");

static bool
ble_gatts_caching_fixed_chan_only(uint16_t conn_handle)
{
#if MYNEWT_VAL(BLE_EATT_CHAN_NUM) > 0
    return !ble_eatt_has_chan(conn_handle);
#else
    (void)conn_handle;
    return true;
#endif
}

static bool
ble_gatts_caching_robust_unaware(const struct ble_gatts_conn *gatt_srv)
{
    return (gatt_srv->peer_cl_sup_feat[0] &
            BLE_GATT_CHR_CLI_SUP_FEAT_ROBUST_CACHING) &&
           !(gatt_srv->chg_aware_flags & BLE_GATTS_CONN_F_CHANGE_AWARE);
}

/**
 * Persists the peer's Client Supported Features and change-aware state.
 * Only bonded peers are persisted.
 */
static void
ble_gatts_caching_persist(uint16_t conn_handle)
{
    struct ble_store_value_csfc value;
    struct ble_hs_conn *conn;

    memset(&value, 0, sizeof value);

    ble_hs_lock();
    conn = ble_hs_conn_find(conn_handle);
    if (conn == NULL || !conn->bhc_sec_state.bonded) {
        ble_hs_unlock();
        return;
    }

    value.peer_addr = conn->bhc_peer_addr;
    value.peer_addr.type =
        ble_hs_misc_peer_addr_type_to_id(conn->bhc_peer_addr.type);
    memcpy(value.csfc, conn->bhc_gatt_svr.peer_cl_sup_feat,
           BLE_GATT_CHR_CLI_SUP_FEAT_SZ);
    value.change_aware = !!(conn->bhc_gatt_svr.chg_aware_flags &
                            BLE_GATTS_CONN_F_CHANGE_AWARE);
    ble_hs_unlock();

    ble_store_write_csfc(&value);
}

/**
 * Checks the Read By Type Request exceptions: a change-unaware client is
 * still served when reading over the full handle range (e.g. the Database
 * Hash characteristic) or when reading Include / Characteristic declarations.
 *
 * @param om                    The request payload, with the opcode stripped.
 */
static bool
ble_gatts_caching_read_type_exception(struct os_mbuf *om)
{
    uint8_t buf[4];
    uint16_t pktlen;
    uint16_t uuid16;

    pktlen = OS_MBUF_PKTLEN(om);
    if (pktlen != BLE_ATT_READ_TYPE_REQ_SZ_16 - 1 &&
        pktlen != BLE_ATT_READ_TYPE_REQ_SZ_128 - 1) {
        /* Malformed; let the request handler reject it. */
        return true;
    }

    if (os_mbuf_copydata(om, 0, 4, buf) != 0) {
        return true;
    }

    if (get_le16(buf) == 0x0001 && get_le16(buf + 2) == 0xffff) {
        return true;
    }

    if (pktlen == BLE_ATT_READ_TYPE_REQ_SZ_16 - 1) {
        if (os_mbuf_copydata(om, 4, 2, buf) != 0) {
            return true;
        }

        uuid16 = get_le16(buf);
        if (uuid16 == BLE_ATT_UUID_INCLUDE ||
            uuid16 == BLE_ATT_UUID_CHARACTERISTIC) {
            return true;
        }
    }

    return false;
}

/**
 * Gates an incoming ATT PDU on the peer's change-aware state.  Called before
 * the PDU is dispatched to its handler; the opcode has been stripped from om.
 */
int
ble_gatts_caching_rx_gate(uint16_t conn_handle, uint16_t cid, uint8_t op,
                          struct os_mbuf *om)
{
    struct ble_gatts_conn *gatt_srv;
    struct ble_hs_conn *conn;
    bool persist;
    bool is_req;
    int res;

    switch (op) {
    case BLE_ATT_OP_READ_TYPE_REQ:
    case BLE_ATT_OP_READ_REQ:
    case BLE_ATT_OP_READ_BLOB_REQ:
    case BLE_ATT_OP_READ_MULT_REQ:
    case BLE_ATT_OP_READ_MULT_VAR_REQ:
    case BLE_ATT_OP_WRITE_REQ:
    case BLE_ATT_OP_PREP_WRITE_REQ:
        is_req = true;
        break;

    case BLE_ATT_OP_WRITE_CMD:
        is_req = false;
        break;

    default:
        return BLE_GATTS_CACHING_GATE_PASS;
    }

    res = BLE_GATTS_CACHING_GATE_PASS;
    persist = false;

    ble_hs_lock();

    conn = ble_hs_conn_find(conn_handle);
    if (conn == NULL) {
        goto done;
    }
    gatt_srv = &conn->bhc_gatt_svr;

    if (!ble_gatts_caching_robust_unaware(gatt_srv)) {
        goto done;
    }

    /* A change-unaware client's commands are ignored. */
    if (!is_req) {
        res = BLE_GATTS_CACHING_GATE_DROP;
        goto done;
    }

    /* The client becomes change-aware on the request following a Database
     * Hash read or, when using only the fixed ATT bearer, following a
     * Database Out Of Sync error.
     */
    if ((gatt_srv->chg_aware_flags & BLE_GATTS_CONN_F_DB_HASH_READ) ||
        ((gatt_srv->chg_aware_flags & BLE_GATTS_CONN_F_OUT_OF_SYNC_SENT) &&
         ble_gatts_caching_fixed_chan_only(conn_handle))) {
        gatt_srv->chg_aware_flags = BLE_GATTS_CONN_F_CHANGE_AWARE;
        persist = conn->bhc_sec_state.bonded;
        goto done;
    }

    if (op == BLE_ATT_OP_READ_TYPE_REQ &&
        ble_gatts_caching_read_type_exception(om)) {
        goto done;
    }

    if (cid == BLE_L2CAP_CID_ATT) {
        if (!(gatt_srv->chg_aware_flags & BLE_GATTS_CONN_F_OUT_OF_SYNC_SENT)) {
            gatt_srv->chg_aware_flags |= BLE_GATTS_CONN_F_OUT_OF_SYNC_SENT;
            res = BLE_GATTS_CACHING_GATE_ERROR;
        } else {
            res = BLE_GATTS_CACHING_GATE_DROP;
        }
    } else {
        /* Enhanced bearer; the error-then-request transition does not apply
         * (Vol 3, Part G, 2.5.2.1), so respond with the error every time.
         */
        res = BLE_GATTS_CACHING_GATE_ERROR;
    }

done:
    ble_hs_unlock();

    if (persist) {
        ble_gatts_caching_persist(conn_handle);
    }

    return res;
}

/**
 * Called when a peer reads the Database Hash characteristic.  The
 * change-aware transition completes on the peer's next ATT request.
 */
void
ble_gatts_caching_hash_read(uint16_t conn_handle)
{
    struct ble_hs_conn *conn;

    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    ble_hs_lock();
    conn = ble_hs_conn_find(conn_handle);
    if (conn != NULL &&
        ble_gatts_caching_robust_unaware(&conn->bhc_gatt_svr)) {
        conn->bhc_gatt_svr.chg_aware_flags |= BLE_GATTS_CONN_F_DB_HASH_READ;
    }
    ble_hs_unlock();
}

/**
 * Called after a peer successfully wrote its Client Supported Features.  A
 * client writing its supported features is considered change-aware.
 */
void
ble_gatts_caching_cl_sup_feat_updated(uint16_t conn_handle)
{
    struct ble_hs_conn *conn;
    bool persist;

    persist = false;

    ble_hs_lock();
    conn = ble_hs_conn_find(conn_handle);
    if (conn != NULL) {
        conn->bhc_gatt_svr.chg_aware_flags = BLE_GATTS_CONN_F_CHANGE_AWARE;
        persist = conn->bhc_sec_state.bonded;
    }
    ble_hs_unlock();

    if (persist) {
        ble_gatts_caching_persist(conn_handle);
    }
}

/**
 * Called when a peer confirms an indication.  Confirming a Service Changed
 * indication makes a client using only the fixed ATT bearer change-aware.
 */
void
ble_gatts_caching_indicate_ack(uint16_t conn_handle, uint16_t chr_val_handle)
{
    struct ble_att_svr_entry *entry;
    struct ble_hs_conn *conn;
    bool persist;

    entry = ble_att_svr_find_by_handle(chr_val_handle);
    if (entry == NULL || entry->ha_uuid->type != BLE_UUID_TYPE_16 ||
        BLE_UUID16(entry->ha_uuid)->value !=
        BLE_GATT_CHR_SVC_CHANGED_UUID16) {
        return;
    }

    if (!ble_gatts_caching_fixed_chan_only(conn_handle)) {
        return;
    }

    persist = false;

    ble_hs_lock();
    conn = ble_hs_conn_find(conn_handle);
    if (conn != NULL &&
        ble_gatts_caching_robust_unaware(&conn->bhc_gatt_svr)) {
        conn->bhc_gatt_svr.chg_aware_flags = BLE_GATTS_CONN_F_CHANGE_AWARE;
        persist = conn->bhc_sec_state.bonded;
    }
    ble_hs_unlock();

    if (persist) {
        ble_gatts_caching_persist(conn_handle);
    }
}

void
ble_gatts_caching_bonding_established(uint16_t conn_handle)
{
    struct ble_hs_conn *conn;
    bool persist;

    ble_hs_lock();
    conn = ble_hs_conn_find(conn_handle);
    persist = conn != NULL && conn->bhc_gatt_svr.peer_cl_sup_feat[0] != 0;
    ble_hs_unlock();

    if (persist) {
        ble_gatts_caching_persist(conn_handle);
    }
}

void
ble_gatts_caching_bonding_restored(uint16_t conn_handle)
{
    struct ble_store_value_csfc value;
    struct ble_store_key_csfc key;
    struct ble_hs_conn *conn;
    int rc;

    memset(&key, 0, sizeof key);

    ble_hs_lock();
    conn = ble_hs_conn_find(conn_handle);
    if (conn == NULL) {
        ble_hs_unlock();
        return;
    }
    key.peer_addr = conn->bhc_peer_addr;
    key.peer_addr.type =
        ble_hs_misc_peer_addr_type_to_id(conn->bhc_peer_addr.type);
    ble_hs_unlock();

    rc = ble_store_read_csfc(&key, &value);
    if (rc != 0) {
        return;
    }

    ble_hs_lock();
    conn = ble_hs_conn_find(conn_handle);
    if (conn != NULL) {
        if (conn->bhc_gatt_svr.peer_cl_sup_feat[0] == 0) {
            /* The peer has not written its features on this connection;
             * adopt the persisted change-aware state.
             */
            if (value.change_aware) {
                conn->bhc_gatt_svr.chg_aware_flags =
                    BLE_GATTS_CONN_F_CHANGE_AWARE;
            } else {
                conn->bhc_gatt_svr.chg_aware_flags = 0;
            }
        }
        conn->bhc_gatt_svr.peer_cl_sup_feat[0] |= value.csfc[0];
    }
    ble_hs_unlock();
}

/**
 * Marks every persisted peer as change-unaware.
 */
static void
ble_gatts_caching_set_unaware_persisted(void)
{
    struct ble_store_value_csfc value;
    struct ble_store_key_csfc key;
    int rc;

    memset(&key, 0, sizeof key);
    key.peer_addr = *BLE_ADDR_ANY;

    for (key.idx = 0; ; key.idx++) {
        rc = ble_store_read_csfc(&key, &value);
        if (rc != 0) {
            break;
        }

        if (value.change_aware) {
            value.change_aware = 0;
            ble_store_write_csfc(&value);
        }
    }
}

/**
 * Marks every peer, connected or persisted, as change-unaware.  Called when
 * the database changes at runtime (e.g. service visibility change).
 */
void
ble_gatts_caching_db_changed(void)
{
    struct ble_hs_conn *conn;
    int i;

    for (i = 0; ; i++) {
        ble_hs_lock();
        conn = ble_hs_conn_find_by_idx(i);
        if (conn != NULL) {
            conn->bhc_gatt_svr.chg_aware_flags = 0;
        }
        ble_hs_unlock();

        if (conn == NULL) {
            break;
        }
    }

    ble_gatts_caching_set_unaware_persisted();
}

/**
 * Compares the database hash against the persisted one.  If the database
 * changed (e.g. across a firmware update), every persisted peer becomes
 * change-unaware and the new hash is persisted.  Called when the GATT server
 * starts.
 */
void
ble_gatts_caching_start(void)
{
    struct ble_store_value_db_hash stored;
    struct ble_store_value_db_hash cur;
    int rc;

    rc = ble_gatts_calculate_hash(cur.hash);
    if (rc != 0) {
        return;
    }

    rc = ble_store_read_db_hash(&stored);
    if (rc == 0 && memcmp(stored.hash, cur.hash, sizeof cur.hash) == 0) {
        return;
    }

    if (rc == 0) {
        /* The database changed since the hash was last stored. */
        ble_gatts_caching_set_unaware_persisted();
    }

    ble_store_write_db_hash(&cur);
}

#endif /* MYNEWT_VAL(BLE_GATT_CACHING) */
