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

#include "ble_att_caching.h"
#include "ble_hs_priv.h"

struct ble_att_caching_dsc {
    SLIST_ENTRY(ble_att_caching_dsc) next;
    struct ble_gatt_dsc dsc;
};
SLIST_HEAD(ble_att_caching_dsc_list, ble_att_caching_dsc);

struct ble_att_caching_chr {
    SLIST_ENTRY(ble_att_caching_chr) next;
    struct ble_gatt_chr chr;
    struct ble_att_caching_dsc_list dscs;
    uint8_t all_dsc_cached;
};
SLIST_HEAD(ble_att_caching_chr_list, ble_att_caching_chr);

struct ble_att_caching_svc {
    SLIST_ENTRY(ble_att_caching_svc) next;
    struct ble_gatt_svc svc;
    struct ble_att_caching_chr_list chrs;
    uint8_t all_chars_cached;
};

SLIST_HEAD(ble_att_caching_svc_list, ble_att_caching_svc);

struct ble_att_caching_conn {
    struct ble_att_caching_svc_list svcs;
    /** Peer identity address */
    ble_addr_t peer_id_addr;
    uint16_t handle;
    uint8_t all_services_cached;
    uint16_t service_changed_att_handle;
};

static struct ble_att_caching_conn ble_att_caching_conns[MYNEWT_VAL(
        BLE_MAX_CONNECTIONS)];
static int ble_att_caching_num_conns = 0;

static os_membuf_t ble_att_caching_svc_mem[OS_MEMPOOL_SIZE(
        BLE_ATT_CACHING_MAX_SVCS, sizeof(struct ble_att_caching_svc))];
static struct os_mempool ble_att_caching_svc_pool;

static os_membuf_t ble_att_caching_chr_mem[OS_MEMPOOL_SIZE(
        BLE_ATT_CACHING_MAX_CHRS, sizeof(struct ble_att_caching_chr))];
static struct os_mempool ble_att_caching_chr_pool;

static os_membuf_t ble_att_caching_dsc_mem[OS_MEMPOOL_SIZE(
        BLE_ATT_CACHING_MAX_DSCS, sizeof(struct ble_att_caching_dsc))];
static struct os_mempool ble_att_caching_dsc_pool;
static struct ble_gap_event_listener ble_att_caching_gap_listener;

static void
ble_att_caching_chr_delete(struct ble_att_caching_chr *chr)
{
    struct ble_att_caching_dsc *dsc;

    while ((dsc = SLIST_FIRST(&chr->dscs)) != NULL) {
        SLIST_REMOVE_HEAD(&chr->dscs, next);
        os_memblock_put(&ble_att_caching_dsc_pool, dsc);
    }

    os_memblock_put(&ble_att_caching_chr_pool, chr);
}

static void
ble_att_caching_svc_delete(struct ble_att_caching_svc *svc)
{
    struct ble_att_caching_chr *chr;

    while ((chr = SLIST_FIRST(&svc->chrs)) != NULL) {
        SLIST_REMOVE_HEAD(&svc->chrs, next);
        ble_att_caching_chr_delete(chr);
    }

    os_memblock_put(&ble_att_caching_svc_pool, svc);
}

static int
ble_att_caching_conn_find_idx(uint16_t handle)
{
    int i;

    for (i = 0; i < ble_att_caching_num_conns; i++) {
        if (ble_att_caching_conns[i].handle == handle) {
            return i;
        }
    }

    return -1;
}

static struct ble_att_caching_conn *
ble_att_caching_conn_find(uint16_t handle)
{
    int idx;

    idx = ble_att_caching_conn_find_idx(handle);
    if (idx == -1) {
        return NULL;
    } else {
        return ble_att_caching_conns + idx;
    }
}

static struct ble_att_caching_conn *
ble_att_caching_conn_find_by_add(ble_addr_t *peer_addr)
{
    int i;

    for (i = 0; i < ble_att_caching_num_conns; i++) {
        if (!ble_addr_cmp((const ble_addr_t *)
                &ble_att_caching_conns[i].peer_id_addr,
                (const ble_addr_t *) peer_addr)) {

            return ble_att_caching_conns + i;
        }
    }

    return NULL;
}

static struct ble_att_caching_svc *
ble_att_caching_svc_find_prev(struct ble_att_caching_conn *conn,
        uint16_t svc_start_handle)
{
    struct ble_att_caching_svc *prev;
    struct ble_att_caching_svc *svc;

    prev = NULL;
    SLIST_FOREACH(svc, &conn->svcs, next) {
        if (svc->svc.start_handle >= svc_start_handle) {
            break;
        }

        prev = svc;
    }

    return prev;
}

static struct ble_att_caching_svc *
ble_att_caching_svc_find(struct ble_att_caching_conn *conn,
        uint16_t svc_start_handle, struct ble_att_caching_svc **out_prev)
{
    struct ble_att_caching_svc *prev;
    struct ble_att_caching_svc *svc;

    prev = ble_att_caching_svc_find_prev(conn, svc_start_handle);
    if (prev == NULL) {
        svc = SLIST_FIRST(&conn->svcs);
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

static struct ble_att_caching_svc *
ble_att_caching_svc_find_range(struct ble_att_caching_conn *conn,
        uint16_t attr_handle)
{
    struct ble_att_caching_svc *svc;

    SLIST_FOREACH(svc, &conn->svcs, next) {
        if (svc->svc.start_handle <= attr_handle &&
                svc->svc.end_handle >= attr_handle) {

            return svc;
        }
    }

    return NULL;
}

static struct ble_att_caching_dsc *
ble_att_caching_dsc_find_prev(const struct ble_att_caching_chr *chr,
        uint16_t dsc_handle)
{
    struct ble_att_caching_dsc *prev;
    struct ble_att_caching_dsc *dsc;

    prev = NULL;
    SLIST_FOREACH(dsc, &chr->dscs, next) {
        if (dsc->dsc.handle >= dsc_handle) {
            break;
        }

        prev = dsc;
    }

    return prev;
}

static struct ble_att_caching_dsc *
ble_att_caching_dsc_find(const struct ble_att_caching_chr *chr,
        uint16_t dsc_handle, struct ble_att_caching_dsc **out_prev)
{
    struct ble_att_caching_dsc *prev;
    struct ble_att_caching_dsc *dsc;

    prev = ble_att_caching_dsc_find_prev(chr, dsc_handle);
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

static struct ble_att_caching_chr *
ble_att_caching_chr_find_prev(const struct ble_att_caching_svc *svc,
        uint16_t chr_val_handle)
{
    struct ble_att_caching_chr *prev;
    struct ble_att_caching_chr *chr;

    prev = NULL;
    SLIST_FOREACH(chr, &svc->chrs, next) {
        if (chr->chr.val_handle >= chr_val_handle) {
            break;
        }

        prev = chr;
    }

    return prev;
}

static struct ble_att_caching_chr *
ble_att_caching_chr_find(const struct ble_att_caching_svc *svc,
        uint16_t chr_val_handle, struct ble_att_caching_chr **out_prev)
{
    struct ble_att_caching_chr *prev;
    struct ble_att_caching_chr *chr;

    prev = ble_att_caching_chr_find_prev(svc, chr_val_handle);
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

struct ble_att_caching_conn *
ble_att_caching_conn_add(struct ble_gap_conn_desc *desc)
{
    struct ble_att_caching_conn *conn;
    assert(ble_att_caching_num_conns < MYNEWT_VAL(BLE_MAX_CONNECTIONS) );
    conn = ble_att_caching_conn_find_by_add(&desc->peer_id_addr);
    if (conn != NULL) {
        return conn;
    }
    conn = ble_att_caching_conns + ble_att_caching_num_conns;
    ble_att_caching_num_conns++;

    conn->handle = desc->conn_handle;
    conn->peer_id_addr = desc->peer_id_addr;
    SLIST_INIT(&conn->svcs);
    return conn;
}

int
ble_att_caching_svc_add(uint16_t conn_handle,
        const struct ble_gatt_svc *gatt_svc)
{
    struct ble_att_caching_conn *conn;
    struct ble_att_caching_svc *prev;
    struct ble_att_caching_svc *svc;
    BLE_HS_LOG(DEBUG, "RECEIVED SERVICE FOR UNKNOWN CONNECTION; "
            "HANDLE=%d\n",
            conn_handle);
    conn = ble_att_caching_conn_find(conn_handle);
    if (conn == NULL) {
        BLE_HS_LOG(DEBUG, "RECEIVED SERVICE FOR UNKNOWN CONNECTION; "
                "HANDLE=%d\n",
                conn_handle);
        return BLE_HS_ENOTCONN;
    }

    svc = ble_att_caching_svc_find(conn, gatt_svc->start_handle, &prev);
    if (svc != NULL) {
        /* Service already discovered. */
        return 0;
    }

    svc = os_memblock_get(&ble_att_caching_svc_pool);
    if (svc == NULL) {
        BLE_HS_LOG(DEBUG, "OOM WHILE DISCOVERING SERVICE\n");
        return BLE_HS_ENOMEM;
    }
    memset(svc, 0, sizeof *svc);

    svc->svc = *gatt_svc;
    SLIST_INIT(&svc->chrs);

    if (prev == NULL) {
        SLIST_INSERT_HEAD(&conn->svcs, svc, next);
    } else {
        SLIST_INSERT_AFTER(prev, svc, next);
    }

    return 0;
}

int
ble_att_caching_set_all_services_cached(uint16_t conn_handle)
{
    struct ble_att_caching_conn *conn;

    conn = ble_att_caching_conn_find(conn_handle);
    if (conn == NULL) {
        BLE_HS_LOG(DEBUG, "RECEIVED SERVICE FOR UNKNOWN CONNECTION; "
                "HANDLE=%d\n",
                conn_handle);
        return BLE_HS_ENOTCONN;
    }
    conn->all_services_cached = 1;
    return 0;
}

int
ble_att_caching_set_all_chrs_cached(uint16_t conn_handle, uint16_t start_handle)
{
    struct ble_att_caching_conn *conn;
    struct ble_att_caching_svc *svc;
    conn = ble_att_caching_conn_find(conn_handle);
    if (conn == NULL) {
        BLE_HS_LOG(DEBUG, "RECEIVED SERVICE FOR UNKNOWN CONNECTION; "
                "HANDLE=%d\n",
                conn_handle);
        return BLE_HS_ENOTCONN;
    }

    svc = ble_att_caching_svc_find(conn, start_handle, NULL);
    if (svc == NULL) {
        BLE_HS_LOG(DEBUG, "CAN'T FIND SERVICE FOR DISCOVERED DSC; HANDLE=%d\n",
                conn_handle);
        return BLE_HS_EINVAL;
    }
    svc->all_chars_cached = 1;
    return 0;
}

int
ble_att_caching_set_all_dscs_cached(uint16_t conn_handle, uint16_t chr_val_handle)
{
    struct ble_att_caching_conn *conn;
    struct ble_att_caching_svc *svc;
    struct ble_att_caching_chr *chr;

    conn = ble_att_caching_conn_find(conn_handle);
    if (conn == NULL) {
        BLE_HS_LOG(DEBUG, "RECEIVED SERVICE FOR UNKNOWN CONNECTION; "
                "HANDLE=%d\n",
                conn_handle);
        return BLE_HS_ENOTCONN;
    }
    svc = ble_att_caching_svc_find_range(conn, chr_val_handle);
    if (svc == NULL) {
        BLE_HS_LOG(DEBUG, "CAN'T FIND SERVICE FOR DISCOVERED DSC; HANDLE=%d\n",
                conn_handle);
        return BLE_HS_EINVAL;
    }

    chr = ble_att_caching_chr_find(svc, chr_val_handle, NULL);
    if (chr == NULL) {
        BLE_HS_LOG(DEBUG, "CAN'T FIND CHARACTERISTIC FOR DISCOVERED DSC; "
                "HANDLE=%d\n",
                conn_handle);
        return BLE_HS_EINVAL;
    }
    chr->all_dsc_cached = 1;
    return 0;
}

int
ble_att_caching_check_if_services_cached(uint16_t conn_handle,
        ble_gatt_disc_svc_fn *cb, void *cb_arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_att_caching_conn *conn;
    struct ble_att_caching_svc *svc;
    int status = 0;
    conn = ble_att_caching_conn_find(conn_handle);
    if (conn == NULL) {
        BLE_HS_LOG(DEBUG, "RECEIVED SERVICE FOR UNKNOWN CONNECTION; "
                "HANDLE=%d\n",
                conn_handle);
        return BLE_HS_ENOTCONN;
    }
   ble_gap_conn_find(conn_handle, &desc);

    /* Check bonded , all services cached and they have
     *  the same address at the same time. */
    if (desc.sec_state.bonded && conn->all_services_cached &&
            !ble_addr_cmp((const ble_addr_t *) &conn->peer_id_addr,
                    (const ble_addr_t *) &desc.peer_id_addr)) {
        /* Then don`t discover. */
        SLIST_FOREACH(svc, &conn->svcs, next) {
            cb(conn_handle, ble_gattc_error(status, 0), &svc->svc, cb_arg);
        }
        status = BLE_HS_EDONE;
        cb(conn_handle, ble_gattc_error(status, 0), &svc->svc, cb_arg);

        return 0;
    } else {
        while ((svc = SLIST_FIRST(&conn->svcs)) != NULL) {
            conn->all_services_cached = 0;
            SLIST_REMOVE_HEAD(&conn->svcs, next);
            ble_att_caching_svc_delete(svc);
        }
    }

    return BLE_HS_EINVAL;
}

int
ble_att_caching_check_if_chars_cached(uint16_t conn_handle,
        uint16_t start_handle, ble_gatt_chr_fn *cb, void *cb_arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_att_caching_conn *conn;
    struct ble_att_caching_svc *svc;
    struct ble_att_caching_chr *chr;
    int status = 0;

    conn = ble_att_caching_conn_find(conn_handle);
    if (conn == NULL) {
        BLE_HS_LOG(DEBUG, "RECEIVED SERVICE FOR UNKNOWN CONNECTION; "
                "HANDLE=%d\n",
                conn_handle);
        return BLE_HS_ENOTCONN;
    }
    ble_gap_conn_find(conn_handle, &desc);
    svc = ble_att_caching_svc_find(conn, start_handle, NULL);
    if (svc == NULL) {
        BLE_HS_LOG(DEBUG, "CAN'T FIND SERVICE FOR DISCOVERED CHR; HANDLE=%d\n",
                conn_handle);
        return BLE_HS_EINVAL;
    }
    /* Check bonded , all services cached and they have
     *  the same address at the same time. */
    if (desc.sec_state.bonded && svc->all_chars_cached &&
            !ble_addr_cmp((const ble_addr_t *) &conn->peer_id_addr,
                    (const ble_addr_t *) &desc.peer_id_addr)) {
        SLIST_FOREACH(chr, &svc->chrs, next) {
           cb(conn_handle, ble_gattc_error(status, 0), &chr->chr, cb_arg);
        }
        status = BLE_HS_EDONE;
        cb(conn_handle, ble_gattc_error(status, 0), &chr->chr, cb_arg);
        /* Then don`t discover. */
    }

    return BLE_HS_EINVAL;
}

int
ble_att_caching_check_if_dsc_cached(uint16_t conn_handle,
        uint16_t chr_val_handle, ble_gatt_dsc_fn *cb, void *cb_arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_att_caching_conn *conn;
    struct ble_att_caching_svc *svc;
    struct ble_att_caching_chr *chr;
    struct ble_att_caching_dsc *dsc;
    int status = 0;

    conn = ble_att_caching_conn_find(conn_handle);
    if (conn == NULL) {
        BLE_HS_LOG(DEBUG, "RECEIVED SERVICE FOR UNKNOWN CONNECTION; "
                "HANDLE=%d\n",
                conn_handle);
        return BLE_HS_ENOTCONN;
    }
    ble_gap_conn_find(conn_handle, &desc);
    svc = ble_att_caching_svc_find_range(conn, chr_val_handle);
    if (svc == NULL) {
        BLE_HS_LOG(DEBUG, "CAN'T FIND SERVICE FOR DISCOVERED DSC; HANDLE=%d\n",
                conn_handle);
        return BLE_HS_EINVAL;
    }

    chr = ble_att_caching_chr_find(svc, chr_val_handle, NULL);
    if (chr == NULL) {
        BLE_HS_LOG(DEBUG, "CAN'T FIND CHARACTERISTIC FOR DISCOVERED DSC; "
                "HANDLE=%d\n",
                conn_handle);
        return BLE_HS_EINVAL;
    }
    /* Check bonded , all services cached and they have
     *  the same address at the same time. */
    if (desc.sec_state.bonded && chr->all_dsc_cached &&
            !ble_addr_cmp((const ble_addr_t *) &conn->peer_id_addr,
                    (const ble_addr_t *) &desc.peer_id_addr)) {
        SLIST_FOREACH(dsc, &chr->dscs, next) {
            cb(conn_handle, ble_gattc_error(status, 0), chr_val_handle,
                    &dsc->dsc, cb_arg);
        }
        status = BLE_HS_EDONE;
        cb(conn_handle, ble_gattc_error(status, 0), chr_val_handle, &dsc->dsc,
                cb_arg);
        /* Then don`t discover. */
        return 0;
    }

    return BLE_HS_EINVAL;
}

int ble_att_caching_chr_add(uint16_t conn_handle, uint16_t svc_start_handle,
        const struct ble_gatt_chr *gatt_chr)
{
    struct ble_att_caching_conn *conn;
    struct ble_att_caching_chr *prev;
    struct ble_att_caching_chr *chr;
    struct ble_att_caching_svc *svc;

    conn = ble_att_caching_conn_find(conn_handle);
    if (conn == NULL) {
        BLE_HS_LOG(DEBUG, "RECEIVED SERVICE FOR UNKNOWN CONNECTION; "
                "HANDLE=%d\n",
                conn_handle);
        return BLE_HS_ENOTCONN;
    }

    svc = ble_att_caching_svc_find(conn, svc_start_handle, NULL);
    if (svc == NULL) {
        BLE_HS_LOG(DEBUG, "CAN'T FIND SERVICE FOR DISCOVERED CHR; HANDLE=%d\n",
                conn_handle);
        return BLE_HS_EINVAL;
    }

    chr = ble_att_caching_chr_find(svc, gatt_chr->val_handle, &prev);
    if (chr != NULL) {
        /* Characteristic already discovered. */
        return 0;
    }

    chr = os_memblock_get(&ble_att_caching_chr_pool);
    if (chr == NULL) {
        BLE_HS_LOG(DEBUG, "OOM WHILE DISCOVERING CHARACTERISTIC\n");
        return BLE_HS_ENOMEM;
    }
    memset(chr, 0, sizeof *chr);

    chr->chr = *gatt_chr;

    if (prev == NULL) {
        SLIST_INSERT_HEAD(&svc->chrs, chr, next);
    } else {
        SLIST_NEXT(prev, next) = chr;
    }

    return 0;
}

int
ble_att_caching_dsc_add(uint16_t conn_handle, uint16_t chr_val_handle,
        const struct ble_gatt_dsc *gatt_dsc)
{
    struct ble_att_caching_conn *conn;
    struct ble_att_caching_dsc *prev;
    struct ble_att_caching_dsc *dsc;
    struct ble_att_caching_svc *svc;
    struct ble_att_caching_chr *chr;

    conn = ble_att_caching_conn_find(conn_handle);
    if (conn == NULL) {
        BLE_HS_LOG(DEBUG, "RECEIVED SERVICE FOR UNKNOWN CONNECTION; "
                "HANDLE=%d\n",
                conn_handle);
        return BLE_HS_ENOTCONN;
    }

    svc = ble_att_caching_svc_find_range(conn, chr_val_handle);
    if (svc == NULL) {
        BLE_HS_LOG(DEBUG, "CAN'T FIND SERVICE FOR DISCOVERED DSC; HANDLE=%d\n",
                conn_handle);
        return BLE_HS_EINVAL;
    }

    chr = ble_att_caching_chr_find(svc, chr_val_handle, NULL);
    if (chr == NULL) {
        BLE_HS_LOG(DEBUG, "CAN'T FIND CHARACTERISTIC FOR DISCOVERED DSC; "
                "HANDLE=%d\n",
                conn_handle);
        return BLE_HS_EINVAL;
    }
    if (!ble_uuid_cmp(
            BLE_UUID16_DECLARE(BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16),
            (const ble_uuid_t *) &chr->chr.uuid)) {
        conn->service_changed_att_handle = chr->chr.val_handle;
    }
    dsc = ble_att_caching_dsc_find(chr, gatt_dsc->handle, &prev);
    if (dsc != NULL) {
        /* Descriptor already discovered. */
        return 0;
    }

    dsc = os_memblock_get(&ble_att_caching_dsc_pool);
    if (dsc == NULL) {
        BLE_HS_LOG(DEBUG,"OOM WHILE DISCOVERING DESCRIPTOR\n");
        return BLE_HS_ENOMEM;
    }
    memset(dsc, 0, sizeof *dsc);

    dsc->dsc = *gatt_dsc;

    if (prev == NULL) {
        SLIST_INSERT_HEAD(&chr->dscs, dsc, next);
    } else {
        SLIST_NEXT(prev, next) = dsc;
    }

    return 0;
}

uint16_t
ble_att_caching_get_service_changed_att_handle(uint16_t conn_handle)
{
    struct ble_att_caching_conn *conn;

    conn = ble_att_caching_conn_find(conn_handle);
    if (conn == NULL) {
        BLE_HS_LOG(DEBUG, "RECEIVED SERVICE FOR UNKNOWN CONNECTION; "
                "HANDLE=%d\n",
                conn_handle);
        return BLE_HS_ENOTCONN;
    }
    return conn->service_changed_att_handle;
}

static int
ble_att_caching_service_changed_rx(struct ble_gap_event *event, void *arg)
{
    struct ble_att_caching_conn *conn;
    struct ble_att_caching_svc *svc;
    struct ble_att_caching_svc *prev;
    uint16_t svc_end_handle;
    if(event->type != BLE_GAP_EVENT_SERVICE_CHANGED_RX) {
        return 0;
    }

    conn = ble_att_caching_conn_find(
            event->indicate_svc_changed_rx.conn_handle);
    if (conn == NULL) {
        BLE_HS_LOG(DEBUG, "RECEIVED SERVICE FOR UNKNOWN CONNECTION; "
                "HANDLE=%d\n",
                event->indicate_svc_changed_rx.conn_handle);
        return BLE_HS_ENOTCONN;
    }
    conn->all_services_cached = 0;
    svc = ble_att_caching_svc_find(conn,
            event->indicate_svc_changed_rx.start_handle, &prev);
    if (svc == NULL) {
        /* Service not found. */
        return BLE_HS_EINVAL;
    }
    for (; svc; svc = SLIST_NEXT((prev), next)) {
        svc_end_handle = svc->svc.end_handle;
        SLIST_NEXT((prev), next) = SLIST_NEXT((svc), next);
        ble_att_caching_svc_delete(svc);
        if (svc_end_handle == event->indicate_svc_changed_rx.end_handle) {
            break;
        }
    }
    return 0;
}

int
ble_att_caching_init(void)
{
    int rc;
    rc = os_mempool_init(&ble_att_caching_svc_pool, BLE_ATT_CACHING_MAX_SVCS,
            sizeof(struct ble_att_caching_svc), ble_att_caching_svc_mem,
            "ble_att_caching_svc_pool");
    BLE_HS_DBG_ASSERT(rc == 0);

    rc = os_mempool_init(&ble_att_caching_chr_pool, BLE_ATT_CACHING_MAX_CHRS,
            sizeof(struct ble_att_caching_chr), ble_att_caching_chr_mem,
            "ble_att_caching_chr_pool");
    BLE_HS_DBG_ASSERT(rc == 0);

    rc = os_mempool_init(&ble_att_caching_dsc_pool, BLE_ATT_CACHING_MAX_DSCS,
            sizeof(struct ble_att_caching_dsc), ble_att_caching_dsc_mem,
            "ble_att_caching_dsc_pool");
    BLE_HS_DBG_ASSERT(rc == 0);
    rc = ble_gap_event_listener_register(&ble_att_caching_gap_listener,
            ble_att_caching_service_changed_rx, NULL);
    BLE_HS_DBG_ASSERT(rc == 0);

    return rc;
}
