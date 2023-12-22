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

#include <inttypes.h>
#include "syscfg/syscfg.h"
#include "ble_hs_mbuf_priv.h"

#if MYNEWT_VAL(BLE_ISO)

#include "os/os_mbuf.h"
#include "host/ble_hs_log.h"
#include "host/ble_hs.h"
#include "host/ble_iso.h"
#include "nimble/hci_common.h"
#include "sys/queue.h"
#include "ble_hs_hci_priv.h"
#include "ble_hs_priv.h"

#define BLE_ISO_SET_FLAG(flags, flag) (flags |= (1 << flag))
#define BLE_ISO_GROUP_IDS_IDX (MYNEWT_VAL(BLE_MAX_ISO_GROUPS) / 32 + 1)
#define BLE_ISO_INVALID_CIG_ID (0xFF)

struct ble_iso_big {
    SLIST_ENTRY(ble_iso_big) next;
    uint8_t id;
    uint16_t max_pdu;
    uint8_t num_bis;
    uint16_t conn_handles[MYNEWT_VAL(BLE_MAX_BIS)];

    ble_iso_event_fn *cb;
    void *cb_arg;
};

struct ble_iso_conn {
    union {
        SLIST_ENTRY(ble_iso_conn) next;
        STAILQ_ENTRY(ble_iso_conn) free_conn;
        STAILQ_ENTRY(ble_iso_conn) pend_conn;
    };

    /* Common for bis_handles and cis */
    uint8_t id;
    uint16_t iso_handle;
    uint8_t flags;
    ble_iso_event_fn *cb;
    void *cb_arg;

    /* CIS related only */
    uint8_t cig_id;
    uint16_t acl_handle;

    /*params*/
    uint16_t max_pdu_output;
    uint16_t max_pdu_input;
    uint16_t seq_num;
    uint32_t last_timestamp;
};

struct ble_iso_group {
    uint8_t id;
    uint8_t iso_num;
    uint8_t is_broadcast;
    ble_iso_event_fn *cb;
    void *cb_arg;
    SLIST_HEAD(, ble_iso_conn) iso_head;

    union {
        SLIST_ENTRY(ble_iso_group) active_group;
        STAILQ_ENTRY(ble_iso_group) free_group;
    };

    struct ble_iso_active_group *list;
};

static SLIST_HEAD(, ble_iso_big) ble_iso_bigs;
static struct os_mempool ble_iso_big_pool;
static os_membuf_t ble_iso_big_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_MAX_BIG), sizeof (struct ble_iso_big))];

static struct os_mempool ble_iso_conn_pool;
static os_membuf_t ble_iso_conn_elem_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_MAX_ISO_CONNECTIONS),
                    sizeof (struct ble_iso_conn))
];

SLIST_HEAD(ble_iso_active_group, ble_iso_group);
STAILQ_HEAD(ble_iso_free_groups, ble_iso_group);
static struct ble_iso_free_groups g_ble_iso_free_group_list;
static struct ble_iso_active_group g_ble_iso_group_list;

static uint32_t ble_iso_group_ids[BLE_ISO_GROUP_IDS_IDX];

static struct ble_iso_big *
ble_iso_big_alloc(uint8_t adv_handle)
{
    struct ble_iso_big *new_big;
    struct ble_iso_big *big;

    if (adv_handle >= BLE_ADV_INSTANCES) {
        BLE_HS_LOG_ERROR("Invalid advertising instance");
        return NULL;
    }

    if (!ble_gap_ext_adv_active(adv_handle)) {
        BLE_HS_LOG_ERROR("Instance not active");
        return NULL;
    }

    new_big = os_memblock_get(&ble_iso_big_pool);
    if (new_big == NULL) {
        BLE_HS_LOG_ERROR("No more memory in pool");
        /* Out of memory. */
        return NULL;
    }

    memset(new_big, 0, sizeof *new_big);

    SLIST_FOREACH(big, &ble_iso_bigs, next) {
        if (big->id == adv_handle) {
            BLE_HS_LOG_ERROR("Advertising instance (%d) already in use",
                             adv_handle);
            return NULL;
        }
    }

    new_big->id = adv_handle;

    if (SLIST_EMPTY(&ble_iso_bigs)) {
        SLIST_INSERT_HEAD(&ble_iso_bigs, new_big, next);
    } else {
        SLIST_INSERT_AFTER(big, new_big, next);
    }

    return new_big;
}

static struct ble_iso_big *
ble_iso_big_find_by_id(uint8_t big_id)
{
    struct ble_iso_big *big;

    SLIST_FOREACH(big, &ble_iso_bigs, next) {
        if (big->id == big_id) {
            return big;
        }
    }

    return NULL;
}

static int
ble_iso_big_free(struct ble_iso_big *big)
{
    SLIST_REMOVE(&ble_iso_bigs, big, ble_iso_big, next);
    os_memblock_put(&ble_iso_big_pool, big);
    return 0;
}

int
ble_iso_create_big(const struct ble_iso_create_big_params *create_params,
                   const struct ble_iso_big_params *big_params)
{
    struct ble_hci_le_create_big_cp cp = { 0 };
    struct ble_iso_big *big;

    big = ble_iso_big_alloc(create_params->adv_handle);
    if (big == NULL) {
        return BLE_HS_ENOENT;
    }

    big->cb = create_params->cb;
    big->cb_arg = create_params->cb_arg;

    cp.big_handle = create_params->adv_handle;

    cp.adv_handle = create_params->adv_handle;
    if (create_params->bis_cnt > MYNEWT_VAL(BLE_MAX_BIS)) {
        return BLE_HS_EINVAL;
    }

    cp.num_bis = create_params->bis_cnt;
    put_le24(cp.sdu_interval, big_params->sdu_interval);
    cp.max_sdu = big_params->max_sdu;
    cp.max_transport_latency = big_params->max_transport_latency;
    cp.rtn = big_params->rtn;
    cp.phy = big_params->phy;
    cp.packing = big_params->packing;
    cp.framing = big_params->framing;
    cp.encryption = big_params->encryption;
    if (big_params->encryption) {
        memcpy(cp.broadcast_code, big_params->broadcast_code, 16);
    }

    return ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                        BLE_HCI_OCF_LE_CREATE_BIG),
                             &cp, sizeof(cp),NULL, 0);
}

int
ble_iso_terminate_big(uint8_t big_id)
{
    struct ble_hci_le_terminate_big_cp cp;
    struct ble_iso_big *big;
    int rc;

    big = ble_iso_big_find_by_id(big_id);
    if (big == NULL) {
        BLE_HS_LOG_ERROR("No BIG with id=%d\n", big_id);
        return BLE_HS_ENOENT;
    }

    cp.big_handle = big->id;
    cp.reason = BLE_ERR_CONN_TERM_LOCAL;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_TERMINATE_BIG),
                           &cp, sizeof(cp),NULL, 0);

    return rc;
}

static int
ble_iso_num_of_available_conns(void)
{
    return ble_iso_conn_pool.mp_num_free;
}

static struct ble_iso_group *
ble_iso_get_new_group_and_put_to_active(struct ble_iso_active_group
                                        *active_list)
{
    struct ble_iso_group *group;

    group = STAILQ_FIRST(&g_ble_iso_free_group_list);
    if (group) {
        STAILQ_REMOVE_HEAD(&g_ble_iso_free_group_list, free_group);
        SLIST_INSERT_HEAD(active_list, group, active_group);
        group->list = active_list;
    }

    return group;
}

static int
ble_iso_pick_and_set_group_id(struct ble_iso_group *cig)
{
    int i;
    int bit;

    for (i = 0; i < BLE_ISO_GROUP_IDS_IDX; i++) {
        bit = __builtin_ffs(~(unsigned int)(ble_iso_group_ids[i]));
        if (bit < 32) {
            break;
        }
    }

    if (i == BLE_ISO_GROUP_IDS_IDX) {
        return -1;
    }

    BLE_ISO_SET_FLAG(ble_iso_group_ids[i], (bit - 1));
    cig->id = (i * 32 + bit -1);

    return cig->id;
}

static void
ble_iso_release_group(struct ble_iso_group * group)
{
    struct ble_iso_conn *iso_conn;
    int idx;

    group->id = BLE_ISO_INVALID_CIG_ID;

    if (group->is_broadcast) {
        while ((iso_conn = SLIST_FIRST(&group->iso_head))) {
            SLIST_REMOVE_HEAD(&group->iso_head, next);
            os_memblock_put(&ble_iso_conn_pool, iso_conn);
        }
    }

    assert(SLIST_FIRST(&group->iso_head) == NULL);

    SLIST_REMOVE(group->list, group, ble_iso_group, active_group);
    STAILQ_INSERT_TAIL(&g_ble_iso_free_group_list, group, free_group);
}

int
ble_iso_create_big_sync(uint8_t *out_big_handle, uint16_t sync_handle,
                        bool encrypted, uint8_t *broadcast_code,
                        uint8_t mse, uint32_t sync_timeout_ms,
                        uint8_t bis_cnt, uint8_t *bis,
                        ble_iso_event_fn *cb, void *cb_arg)
{
    struct ble_hci_le_big_create_sync_cp *cmd;
    uint8_t cmd_len = sizeof(*cmd) + bis_cnt * sizeof(uint8_t);
    uint8_t cmd_buf[cmd_len] ;
    struct ble_iso_group *big;
    int rc;

    if (bis_cnt < 1) {
        BLE_HS_LOG_ERROR("No bis_handles? \n");
        return BLE_HS_EINVAL;
    }

    if (encrypted && !broadcast_code) {
        BLE_HS_LOG_ERROR("Requested encryption but no broadcast code\n");
        return BLE_HS_EINVAL;
    }

    if (ble_iso_num_of_available_conns() < bis_cnt) {
        BLE_HS_LOG_ERROR("Requested number of iso conn not available (%d != %d)\n",
                         bis_cnt, ble_iso_num_of_available_conns());
        return BLE_HS_ENOMEM;
    }
    big = ble_iso_get_new_group_and_put_to_active(&g_ble_iso_group_list);
    if (!big) {
        return BLE_HS_ENOMEM;
    }
    ble_iso_pick_and_set_group_id(big);
    big->is_broadcast = true;
    big->cb_arg = cb_arg;
    big->cb = cb;

    cmd = (struct ble_hci_le_big_create_sync_cp *)cmd_buf;
    memset(cmd, 0, cmd_len);
    cmd->big_handle = big->id;
    cmd->sync_handle = htole16(sync_handle/10);
    cmd->encryption = encrypted;

    if (encrypted) {
        memcpy(cmd->broadcast_code, broadcast_code, 16);
    }

    cmd->sync_timeout = sync_timeout_ms;
    cmd->num_bis = bis_cnt;
    memcpy(cmd->bis, bis, bis_cnt);

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_BIG_CREATE_SYNC),
                           cmd, cmd_len,NULL, 0);
    if (rc) {
        ble_iso_release_group(big);
        return rc;
    }
    *out_big_handle = big->id;
    return 0;
}

static struct ble_iso_group *
ble_iso_find_group(uint8_t group_id, struct ble_iso_active_group
    *active_group_list)
{
    struct ble_iso_group *group;

    ble_hs_lock();
    SLIST_FOREACH(group, active_group_list, active_group) {
        if (group_id == group->id) {
            ble_hs_unlock();
            return group;
        }
    }
    ble_hs_unlock();
    return NULL;
}

int
ble_iso_terminate_big_sync(uint8_t big_handle)
{
    struct ble_hci_le_big_terminate_sync_cp cp;
    struct ble_iso_group *big;
    int rc;

    big = ble_iso_find_group(big_handle, &g_ble_iso_group_list);
    if (!big) {
        BLE_HS_LOG_ERROR("Could not find big 0x%02x\n", big_handle);
        return 0;
    }

    cp.big_handle = big_handle;
    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_BIG_TERMINATE_SYNC),
                           &cp, sizeof(cp),NULL, 0);
    if (rc == 0) {
        ble_iso_release_group(big);
    }

    return rc;
}

int
ble_iso_init(void)
{
    int rc;

    SLIST_INIT(&ble_iso_bigs);
    SLIST_INIT(&g_ble_iso_group_list);
    STAILQ_INIT(&g_ble_iso_free_group_list);

    rc = os_mempool_init(&ble_iso_big_pool,
                         MYNEWT_VAL(BLE_MAX_BIG),
                         sizeof (struct ble_iso_big),
                         ble_iso_big_mem, "ble_iso_big_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    return 0;
}

void
ble_iso_rx_create_big_complete(const struct ble_hci_ev_le_subev_create_big_complete *ev)
{
    struct ble_iso_event event;

    struct ble_iso_big *big;
    int i;

    big = ble_iso_big_find_by_id(ev->big_handle);

    big->num_bis = ev->num_bis;

    for (i = 0; i < ev->num_bis; i++) {
        big->conn_handles[i] = ev->conn_handle[i];
    }

    big->max_pdu = ev->max_pdu;

    event.type = BLE_ISO_EVENT_BIG_CREATE_COMPLETE;
    event.big_created.desc.big_handle = ev->big_handle;
    event.big_created.desc.big_sync_delay = get_le24(ev->big_sync_delay);
    event.big_created.desc.transport_latency_big =
        get_le24(ev->transport_latency_big);
    event.big_created.desc.phy = ev->phy;
    event.big_created.desc.nse = ev->nse;
    event.big_created.desc.bn = ev->bn;
    event.big_created.desc.pto = ev->pto;
    event.big_created.desc.irc = ev->irc;
    event.big_created.desc.max_pdu = ev->max_pdu;
    event.big_created.desc.iso_interval = ev->iso_interval;
    event.big_created.desc.num_bis = ev->num_bis;
    memcpy(event.big_created.desc.conn_handle, ev->conn_handle,
           ev->num_bis * sizeof(uint16_t));

    if (big->cb != NULL) {
        big->cb(&event, big->cb_arg);
    }
}

void
ble_iso_rx_terminate_big_complete(const struct ble_hci_ev_le_subev_terminate_big_complete *ev)
{
    struct ble_iso_event event;
    struct ble_iso_big *big;

    big = ble_iso_big_find_by_id(ev->big_handle);

    event.type = BLE_ISO_EVENT_BIG_TERMINATE_COMPLETE;
    event.big_terminated.big_handle = ev->big_handle;
    event.big_terminated.reason = ev->reason;

    if (big->cb != NULL) {
        big->cb(&event, big->cb_arg);
    }

    ble_iso_big_free(big);
}

static int
ble_iso_tx_complete(uint16_t conn_handle, const uint8_t *data,
                    uint16_t data_len)
{
    struct os_mbuf *om;
    int rc;

    om = ble_hs_mbuf_bare_pkt();
    if (!om) {
        return BLE_HS_ENOMEM;
    }

    os_mbuf_extend(om, 8);
    /* Connection_Handle, PB_Flag, TS_Flag */
    put_le16(&om->om_data[0],
             BLE_HCI_ISO_HANDLE(conn_handle, BLE_HCI_ISO_PB_COMPLETE, 0));
    /* Data_Total_Length = Data length + Packet_Sequence_Number placeholder */
    put_le16(&om->om_data[2], data_len + 4);
    /* Packet_Sequence_Number placeholder */
    put_le16(&om->om_data[4], 0);
    /* ISO_SDU_Length */
    put_le16(&om->om_data[6], data_len);

    rc = os_mbuf_append(om, data, data_len);
    if (rc) {
        return rc;
    }

    return ble_transport_to_ll_iso(om);
}

static int
ble_iso_tx_segmented(uint16_t conn_handle, const uint8_t *data,
                     uint16_t data_len)
{
    struct os_mbuf *om;
    uint16_t data_left = data_len;
    uint16_t packet_len;
    uint16_t offset = 0;
    uint8_t pb;
    int rc;

    while (data_left) {
        packet_len = min(MYNEWT_VAL(BLE_TRANSPORT_ISO_SIZE), data_left);
        if (data_left == data_len) {
            pb = BLE_HCI_ISO_PB_FIRST;
        } else if (packet_len == data_left) {
            pb = BLE_HCI_ISO_PB_LAST;
        } else {
            pb = BLE_HCI_ISO_PB_CONTINUATION;
        }

        om = ble_hs_mbuf_bare_pkt();
        if (!om) {
            return BLE_HS_ENOMEM;
        }

        os_mbuf_extend(om, pb == BLE_HCI_ISO_PB_FIRST ? 8: 4);

        /* Connection_Handle, PB_Flag, TS_Flag */
        put_le16(&om->om_data[0],
                 BLE_HCI_ISO_HANDLE(conn_handle, pb, 0));

        if (pb == BLE_HCI_ISO_PB_FIRST) {
            /* Data_Total_Length = Data length +
             * Packet_Sequence_Number placeholder*/
            put_le16(&om->om_data[2], packet_len + 4);

            /* Packet_Sequence_Number placeholder */
            put_le16(&om->om_data[8], 0);

            /* ISO_SDU_Length */
            put_le16(&om->om_data[10], packet_len);
        } else {
            put_le16(&om->om_data[2], packet_len);
        }

        rc = os_mbuf_append(om, data + offset, packet_len);
        if (rc) {
            return rc;
        }

        ble_transport_to_ll_iso(om);

        offset += packet_len;
        data_left -= packet_len;
    }

    return 0;
}

int
ble_iso_tx(uint16_t conn_handle, void *data, uint16_t data_len)
{
    int rc;

    if (data_len <= MYNEWT_VAL(BLE_TRANSPORT_ISO_SIZE)) {
        rc = ble_iso_tx_complete(conn_handle, data, data_len);
    } else {
        rc = ble_iso_tx_segmented(conn_handle, data, data_len);
    }

    return rc;
}
#endif
