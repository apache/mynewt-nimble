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
#include "nimble/hci_common.h"
#include "sys/queue.h"
#include "ble_iso_priv.h"
#include "ble_hs_hci_priv.h"
#include "ble_hs_priv.h"
#include "host/ble_hs_log.h"
#include "host/ble_hs.h"


#if MYNEWT_VAL(BLE_ISO)

#include "os/os_mbuf.h"
#include "host/ble_hs_log.h"
#include "host/ble_hs.h"
#include "host/ble_iso.h"
#include "ble_iso_priv.h"
#include "nimble/hci_common.h"
#include "sys/queue.h"
#include "ble_hs_hci_priv.h"

#define BLE_ISO_INVALID_CIG_ID (0xFF)

#define BLE_ISO_CIS_F_IS_MASTER   (0x01)
#define BLE_ISO_CIS_F_CONNECTED   (0x02)
#define BLE_ISO_IS_BIS            (0x04)

#define BLE_ISO_SET_FLAG(flags, flag) (flags |= (1 << flag))
#define BLE_ISO_CLEAR_FLAG(flags, flag) (flags &= ~(1 << flag))

#define BLE_ISO_CIG_IDS_IDX (MYNEWT_VAL(BLE_MAX_CIG) / 32 + 1)

struct ble_iso_big {
    SLIST_ENTRY(ble_iso_big) next;
    uint8_t id;
    uint16_t max_pdu;
    uint8_t num_bis;
    uint16_t conn_handles[MYNEWT_VAL(BLE_MAX_BIS)];

    ble_iso_event_fn *cb;
    void *cb_arg;
};

struct ble_iso_group {
    uint8_t id;
    uint8_t iso_num;
    uint8_t is_broadcast;
    ble_iso_event_fn *cb;
    void *cb_arg;
    SLIST_HEAD(, ble_iso_conn) iso_head;

    union {
        SLIST_ENTRY(ble_iso_group) active_cig;
        STAILQ_ENTRY(ble_iso_group) free_cig;
    };

    struct ble_iso_active_cig *list;
};

static SLIST_HEAD(, ble_iso_big) ble_iso_bigs;
static struct os_mempool ble_iso_big_pool;
static os_membuf_t ble_iso_big_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_MAX_BIG), sizeof (struct ble_iso_big))];

static uint32_t ble_iso_group_ids[BLE_ISO_CIG_IDS_IDX];

/* For now support only one server */
static ble_iso_event_fn *ble_iso_leaudio_server_cb;
static void *ble_iso_leaudio_server_cb_arg;

static struct ble_iso_group g_ble_iso_groups_ids[MYNEWT_VAL(BLE_MAX_CIG)];

SLIST_HEAD(ble_iso_active_cig, ble_iso_group);
STAILQ_HEAD(ble_iso_free_groups, ble_iso_group);
static struct ble_iso_active_cig g_ble_iso_act_slave_cig_list;
static struct ble_iso_active_cig g_ble_iso_act_master_cig_list;
static struct ble_iso_free_groups g_ble_iso_free_group_list;

static struct ble_iso_active_cig g_ble_iso_big_list;

STAILQ_HEAD(ble_iso_pending_cis_conn, ble_iso_conn);
static struct ble_iso_pending_cis_conn g_ble_iso_pend_cis_conn_list;

static struct os_mempool ble_iso_conn_pool;
static os_membuf_t ble_iso_conn_elem_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_MAX_CIS_CONNECTIONS),
                    sizeof (struct ble_iso_conn))
];

static struct ble_iso_cis_params g_ble_cis_params_dflt = {
    .max_sdu_c_to_p = 100,
    .max_sdu_p_to_c = 100,
    .phy_c_to_p = 0,
    .phy_p_to_c = 0,
    .rnt_c_to_p = 2,
    .rnt_p_to_c = 2,
};

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

int
ble_iso_init(void)
{
    struct ble_iso_group *cig;
    int rc;
    int i;

    SLIST_INIT(&ble_iso_bigs);

    rc = os_mempool_init(&ble_iso_big_pool,
                         MYNEWT_VAL(BLE_MAX_BIG),
                         sizeof (struct ble_iso_big),
                         ble_iso_big_mem, "ble_iso_big_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = os_mempool_init(&ble_iso_conn_pool, MYNEWT_VAL(BLE_MAX_CIS_CONNECTIONS),
                         sizeof (struct ble_iso_conn),
                         ble_iso_conn_elem_mem, "ble_iso_conn_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    SLIST_INIT(&g_ble_iso_act_slave_cig_list);
    SLIST_INIT(&g_ble_iso_act_master_cig_list);
    SLIST_INIT(&g_ble_iso_big_list);
    STAILQ_INIT(&g_ble_iso_pend_cis_conn_list);
    STAILQ_INIT(&g_ble_iso_free_group_list);

    for (i = 0; i < MYNEWT_VAL(BLE_MAX_CIG); i++) {
        cig = &g_ble_iso_groups_ids[i];
        cig->id = BLE_ISO_INVALID_CIG_ID;
        SLIST_INIT(&cig->iso_head);
        STAILQ_INSERT_TAIL(&g_ble_iso_free_group_list, cig, free_cig);
    }

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

    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());

    while (data_left && ble_iso_hci_avail_pkts > 0) {
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

        ble_iso_hci_avail_pkts--;
    }

    return 0;
}

static struct ble_iso_group *
ble_iso_find_group(uint8_t group_id, struct ble_iso_active_cig *active_cig_list)
{
    struct ble_iso_group *group;

    ble_hs_lock();
    SLIST_FOREACH(group, active_cig_list, active_cig) {
        if (group_id == group->id) {
            ble_hs_unlock();
            return group;
        }
    }
    ble_hs_unlock();
    return NULL;
}

struct ble_iso_conn *
ble_iso_find_by_iso_handle(uint16_t iso_handle)
{
    struct ble_iso_group *cig;
    struct ble_iso_conn *cis_conn;

    ble_hs_lock();
    SLIST_FOREACH(cig, &g_ble_iso_act_master_cig_list, active_cig) {
        SLIST_FOREACH(cis_conn, &cig->iso_head, next) {
            if (cis_conn->iso_handle == iso_handle) {
                ble_hs_unlock();
                return cis_conn;
            }
        }
    }

    SLIST_FOREACH(cig, &g_ble_iso_act_slave_cig_list, active_cig) {
        SLIST_FOREACH(cis_conn, &cig->iso_head, next) {
            if (cis_conn->iso_handle == iso_handle) {
                ble_hs_unlock();
                return cis_conn;
            }
        }
    }
    ble_hs_unlock();
    return NULL;
}

static int
ble_iso_pick_and_set_group_id(struct ble_iso_group *cig)
{
    int i;
    int bit;

    for (i = 0; i < BLE_ISO_CIG_IDS_IDX; i++) {
        bit = __builtin_ffs(~(unsigned int)(ble_iso_group_ids[i]));
        if (bit < 32) {
            break;
        }
    }

    if (i == BLE_ISO_CIG_IDS_IDX) {
        return -1;
    }

    BLE_ISO_SET_FLAG(ble_iso_group_ids[i], (bit - 1));
    cig->id = (i * 32 + bit -1);

    return cig->id;
}

static struct ble_iso_group *
ble_iso_get_new_cig_and_put_to_active(struct ble_iso_active_cig *active_list)
{
    struct ble_iso_group *cig;

    cig = STAILQ_FIRST(&g_ble_iso_free_group_list);
    if (cig) {
        STAILQ_REMOVE_HEAD(&g_ble_iso_free_group_list, free_cig);
        SLIST_INSERT_HEAD(active_list, cig, active_cig);
        cig->list = active_list;
    }

    return cig;
}

static void
ble_iso_release_group(struct ble_iso_group * group)
{
    struct ble_iso_conn *iso_conn;
    int idx;

    if (group->list == &g_ble_iso_act_master_cig_list) {
        idx = group->id / 32;
        BLE_ISO_CLEAR_FLAG(ble_iso_group_ids[idx], (group->id - idx * 32));
    }

    group->id = BLE_ISO_INVALID_CIG_ID;

    if (group->is_broadcast) {
        while ((iso_conn = SLIST_FIRST(&group->iso_head))) {
            SLIST_REMOVE_HEAD(&group->iso_head, next);
            os_memblock_put(&ble_iso_conn_pool, iso_conn);
        }
    }

    assert(SLIST_FIRST(&group->iso_head) == NULL);

    SLIST_REMOVE(group->list, group, ble_iso_group, active_cig);
    STAILQ_INSERT_TAIL(&g_ble_iso_free_group_list, group, free_cig);
}

static int
ble_iso_num_of_available_conns(void)
{
    return ble_iso_conn_pool.mp_num_free;
}

struct ble_iso_conn *
ble_iso_conn_alloc(uint8_t iso_handle)
{
    struct ble_iso_conn *iso_conn;

    iso_conn = os_memblock_get(&ble_iso_conn_pool);
    assert(iso_conn);
    iso_conn->id = iso_handle;
    iso_conn->seq_num = 0;
    iso_conn->last_timestamp = 0;

    return iso_conn;
}

int
ble_iso_client_create_cig(struct ble_iso_cig_params *cig_params,
                          uint8_t cis_cnt, struct ble_iso_cis_params *cis_params,
                          uint8_t *cig_id, uint8_t *cis_handles)
{
    struct ble_iso_group *cig;
    struct ble_iso_conn *cis_conn;
    struct ble_hci_le_set_cig_params_cp *cmd;
    uint8_t cmd_buf[(sizeof(*cmd)) + BLE_HCI_LE_SET_CIG_CIS_MAX_NUM * sizeof(*cis_params)];
    struct ble_hci_le_set_cig_params_rp *ev;
    uint8_t ev_buf[sizeof(*ev) + BLE_HCI_LE_SET_CIG_CIS_MAX_NUM * sizeof(uint16_t)];
    uint32_t tmp;
    int i;
    int rc;

    if (ble_iso_num_of_available_conns() < cis_cnt) {
        return BLE_HS_ENOMEM;
    }

    cig = ble_iso_get_new_cig_and_put_to_active(&g_ble_iso_act_master_cig_list);
    if (!cig) {
        return BLE_HS_ENOMEM;
    }

    cmd = (struct ble_hci_le_set_cig_params_cp *)&cmd_buf[0];

    cmd->cig_id = ble_iso_pick_and_set_group_id(cig);
    assert(cmd->cig_id >= 0);
    *cig_id = cmd->cig_id;

    tmp = htole32(cig_params->sdu_c_to_p_itvl);
    memcpy(cmd->sdu_interval_c_to_p, &tmp, 3);

    tmp = htole32(cig_params->sdu_c_to_p_itvl);
    memcpy(cmd->sdu_interval_p_to_c, &tmp, 3);

    cmd->worst_sca = cig_params->sca;
    cmd->packing = cig_params->packing;
    cmd->framing = cig_params->framing;
    put_le16((uint8_t *)&cmd->max_latency_c_to_p, cig_params->max_c_to_p_latency);
    put_le16((uint8_t *)&cmd->max_latency_p_to_c, cig_params->max_p_to_c_latency);

    cmd->cis_count = cis_cnt;
    for (i = 0; i < cis_cnt; i++) {
        if (!cis_params) {
            cis_params = &g_ble_cis_params_dflt;
        }

        cmd->cis[i].cis_id = i;
        cmd->cis[i].max_sdu_c_to_p = cis_params->max_sdu_c_to_p;
        cmd->cis[i].max_sdu_p_to_c = cis_params->max_sdu_p_to_c;
        cmd->cis[i].phy_c_to_p = cis_params->phy_c_to_p;
        cmd->cis[i].phy_p_to_c = cis_params->phy_p_to_c;
        cmd->cis[i].rnt_c_to_p = cis_params->rnt_c_to_p;
        cmd->cis[i].rnt_p_to_c = cis_params->rnt_p_to_c;
    }

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_SET_CIG_PARAMS),
                           cmd, sizeof(*cmd) + cis_cnt * sizeof(*cis_params),
                           ev_buf, sizeof(*ev) + cis_cnt * sizeof(uint16_t));
    if (rc) {
        rc = BLE_HS_HCI_ERR(rc);
        goto error;
    }

    ev = (struct ble_hci_le_set_cig_params_rp *)ev_buf;
    assert(cis_cnt == ev->cis_count);

    for (i = 0; i < cis_cnt; i++) {
        cis_conn = ble_iso_conn_alloc(i);
        assert(cis_conn);
        SLIST_INSERT_HEAD(&cig->iso_head, cis_conn, next);
        cis_conn->iso_handle = ev->conn_handle[i];
        cis_conn->cig_id = cig->id;
    }

    cig->iso_num = cis_cnt;

    return 0;

error:
    ble_iso_release_group(cig);

    return rc;
}

int
ble_iso_client_remove_group(uint8_t cig_id)
{
    struct ble_hci_le_remove_cig_cp cmd;
    struct ble_iso_group *cig;
    int rc;

    cig = ble_iso_find_group(cig_id, &g_ble_iso_act_master_cig_list);
    if (!cig) {
        return BLE_HS_ENOENT;
    }

    cmd.cig_id = cig_id;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_REMOVE_CIG), &cmd, sizeof(cmd),
                           NULL, 0);

    if (rc == 0) {
        ble_iso_release_group(cig);
    }

    return rc;
}

int
ble_iso_client_create_cis(uint8_t cig_id, uint8_t cis_cnt,
                          struct ble_hci_le_create_cis_params *params,
                          ble_iso_event_fn *cb, void *cb_arg)
{
    struct ble_hci_le_create_cis_cp *cmd;
    uint8_t cmd_buf[sizeof(*cmd) + cis_cnt * sizeof(*params)];
    struct ble_iso_group *cig;
    struct ble_iso_conn *cis_conn;
    int rc;
    int i;

    if (cis_cnt > BLE_HCI_LE_SET_CIG_CIS_MAX_NUM) {
        return BLE_HS_EINVAL;
    }

    cig = ble_iso_find_group(cig_id, &g_ble_iso_act_master_cig_list);
    if (!cig || cig->iso_num < cis_cnt) {
        return BLE_HS_ENOENT;
    }

    cmd = (struct ble_hci_le_create_cis_cp *)cmd_buf;
    cmd->cis_count = cis_cnt;
    memcpy(cmd->cis, params, cis_cnt *
                             sizeof(struct ble_hci_le_create_cis_params));

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_CREATE_CIS),
                           cmd, sizeof(*cmd) + cis_cnt * sizeof(*params),
                           NULL, 0);

    if (rc == 0) {
        for (i = 0; i < cis_cnt; i++) {
            cis_conn = SLIST_FIRST(&cig->iso_head);
            cis_conn->cb = cb;
            cis_conn->cb_arg = cb_arg;

            /* TODO We need to check if ACL is really in Master Role */
            BLE_ISO_SET_FLAG(cis_conn->flags, BLE_ISO_CIS_F_IS_MASTER);

            SLIST_REMOVE_HEAD(&cig->iso_head, next);
            STAILQ_INSERT_HEAD(&g_ble_iso_pend_cis_conn_list, cis_conn, pend_cis);
        }
    }

    return rc;
}

static int
ble_iso_set_hci_data_path(uint16_t handle, bool endpoint_input, bool endpoint_output)
{
    struct ble_hci_le_setup_iso_data_path_cp cmd = {0};
    int rc = 0;

    put_le16(&cmd.conn_handle, handle);

    if (endpoint_input) {
        /* For now we support only HCI path */
        cmd.data_path_dir = 1; /* Controller to Host */
        cmd.data_path_id = 0; /* hci */

        rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                          BLE_HCI_OCF_LE_SETUP_ISO_DATA_PATH),
                               &cmd, sizeof(cmd), NULL, 0);
        assert(rc == 0);
    }

    if (endpoint_output) {
        /* For now we support only HCI path */
        cmd.data_path_dir = 0; /* Host to Controller */
        cmd.data_path_id = 0; /* hci */

        rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                          BLE_HCI_OCF_LE_SETUP_ISO_DATA_PATH),
                               &cmd, sizeof(cmd), NULL, 0);
        assert(rc == 0);
    }
    return rc;
}

int
ble_iso_rx_hci_evt_le_cis_established(const struct ble_hci_ev_le_subev_cis_established *ev)
{
    struct ble_iso_group *cig;
    struct ble_iso_conn *cis_conn_prev;
    struct ble_iso_conn *cis_conn;
    struct ble_iso_event event = {0};

    cis_conn = STAILQ_FIRST(&g_ble_iso_pend_cis_conn_list);
    assert(cis_conn);

    if (cis_conn->iso_handle != le16toh(ev->conn_handle)) {
        while (STAILQ_NEXT(cis_conn, pend_cis)) {
            cis_conn_prev = cis_conn;
            cis_conn = STAILQ_NEXT(cis_conn, pend_cis);
            if (cis_conn->iso_handle == le16toh(ev->conn_handle)) {
                STAILQ_REMOVE_AFTER(&g_ble_iso_pend_cis_conn_list, cis_conn_prev, pend_cis);
                break;
            }
        }
    }

    assert(cis_conn);

    if (cis_conn->flags & BLE_ISO_CIS_F_IS_MASTER) {
        cig = ble_iso_find_group(cis_conn->cig_id, &g_ble_iso_act_master_cig_list);
    } else {
        cig = ble_iso_find_group(cis_conn->cig_id, &g_ble_iso_act_slave_cig_list);
    }
    assert(cig);

    event.type = BLE_ISO_EVENT_CIS_ESTABLISHED;
    event.cis_established.cis_handle = cis_conn->iso_handle;
    event.cis_established.status = ev->status;

    cis_conn->cb(&event, cis_conn->cb_arg);

    if (event.cis_established.status != BLE_ERR_SUCCESS) {
        os_memblock_put(&ble_iso_conn_pool, cis_conn);
        return 0;
    }

    if (cis_conn->flags & BLE_ISO_CIS_F_IS_MASTER) {
        cis_conn->max_pdu_output = ev->max_pdu_c_to_p;
        cis_conn->max_pdu_input = ev->max_pdu_p_to_c;
    } else {
        cis_conn->max_pdu_input = ev->max_pdu_c_to_p;
        cis_conn->max_pdu_output = ev->max_pdu_p_to_c;
    }

    ble_iso_set_hci_data_path(ev->conn_handle,
                              !!cis_conn->max_pdu_input,
                              !!cis_conn->max_pdu_output);
    SLIST_INSERT_HEAD(&cig->iso_head, cis_conn, next);

    return 0;
}

static void
ble_iso_cis_reject_rsp(uint16_t cis_handle, uint8_t reason)
{
    struct ble_hci_le_reject_cis_request_cp cmd;

    put_le16(&cmd.conn_handle, cis_handle);
    cmd.reason = reason;

    if (ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                     BLE_HCI_OCF_LE_REJECT_CIS_REQ), &cmd, sizeof(cmd),
                          NULL, 0)) {
        assert(0);
    }
}

static void
ble_iso_cis_accept_rsp(uint16_t cis_handle)
{
    struct ble_hci_le_accept_cis_request_cp cmd;

    put_le16(&cmd.conn_handle, cis_handle);

    if (ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                     BLE_HCI_OCF_LE_ACCEPT_CIS_REQ), &cmd, sizeof(cmd),
                          NULL, 0)) {
        assert(0);
    }
}

int
ble_iso_rx_hci_evt_le_cis_request(const struct ble_hci_ev_le_subev_cis_request *ev)
{
    struct ble_iso_group *cig;
    struct ble_iso_conn *cis_conn;
    struct ble_iso_event event = {0};
    int rc;

    if (!ble_iso_leaudio_server_cb) {
        ble_iso_cis_reject_rsp(ev->cis_conn_handle, BLE_ERR_UNSUPPORTED);
        return 0;
    }

    cig = ble_iso_find_group(ev->cig_id, &g_ble_iso_act_slave_cig_list);
    if (!cig) {
        /* This is new CIG - lets us create it */
        cig = ble_iso_get_new_cig_and_put_to_active(&g_ble_iso_act_slave_cig_list);
        if (!cig) {
            ble_iso_cis_reject_rsp(ev->cis_conn_handle, BLE_ERR_CONN_LIMIT);
            return 0;
        }
        cig->id = ev->cig_id;
    }

    cis_conn = ble_iso_conn_alloc(ev->cis_id);
    if (!cis_conn) {
        ble_iso_cis_reject_rsp(ev->cis_conn_handle, BLE_ERR_CONN_LIMIT);
        ble_iso_release_group(cig);
        return 0;
    }

    cis_conn->iso_handle = le16toh(ev->cis_conn_handle);
    cis_conn->acl_handle = le16toh(ev->acl_conn_handle);
    cis_conn->cig_id = cig->id;

    event.type = BLE_ISO_EVENT_CONNECT_REQUEST;
    event.cis_connect_req.cis_handle = ev->cis_conn_handle;
    event.cis_connect_req.conn_handle = ev->acl_conn_handle;

    cis_conn->cb = ble_iso_leaudio_server_cb;
    cis_conn->cb_arg = ble_iso_leaudio_server_cb_arg;

    rc = cis_conn->cb(&event, cis_conn->cb_arg);
    if (rc) {
        ble_iso_cis_reject_rsp(ev->cis_conn_handle, rc);
        ble_iso_release_group(cig);
    } else {
        ble_iso_cis_accept_rsp(ev->cis_conn_handle);
        STAILQ_INSERT_HEAD(&g_ble_iso_pend_cis_conn_list, cis_conn, pend_cis);
    }

    return 0;
}

int
ble_iso_server_register_le_audio(ble_iso_event_fn *cb, void *cb_arg)
{
    if (ble_iso_leaudio_server_cb) {
        return BLE_HS_EBUSY;
    }

    ble_iso_leaudio_server_cb = cb;
    ble_iso_leaudio_server_cb_arg = cb_arg;

    return 0;
}

static void
ble_iso_send_disconnected_event(struct ble_iso_conn *cis_conn, uint8_t reason)
{
    struct ble_iso_event event;

    event.type = BLE_ISO_EVENT_CIS_DISCONNECTED;
    event.cis_disconnected.status = reason;
    event.cis_disconnected.cis_handle = cis_conn->iso_handle;
    cis_conn->cb(&event, cis_conn->cb_arg);

    BLE_ISO_CLEAR_FLAG(cis_conn->flags, BLE_ISO_CIS_F_CONNECTED);
}

void
ble_iso_disconnect_cis(uint16_t handle)
{
    struct ble_hci_lc_disconnect_cp cmd;
    int rc;

    put_le16(&cmd.conn_handle, handle);
    cmd.reason = BLE_ERR_REM_USER_CONN_TERM;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LINK_CTRL,
                                      BLE_HCI_OCF_DISCONNECT_CMD),
                           &cmd, sizeof(cmd), NULL, 0);
    if (rc != 0) {
        BLE_HS_LOG_INFO("Cis already disconnected? handle =0x%04x", handle);
    }
}

static void
ble_iso_acl_disconnected(uint16_t acl_handle, uint8_t reason)
{
    struct ble_iso_conn *cis_conn;
    struct ble_iso_group *cig;
    bool done = false;

    ble_hs_lock();
    SLIST_FOREACH(cig, &g_ble_iso_act_master_cig_list, active_cig) {
        SLIST_FOREACH(cis_conn, &cig->iso_head, next) {
            if (cis_conn->acl_handle == acl_handle) {
                ble_iso_disconnect_cis(cis_conn->iso_handle);

                done = true;
            }
        }
    }

    if (done) {
        ble_hs_unlock();
        return;
    }

    SLIST_FOREACH(cig, &g_ble_iso_act_slave_cig_list, active_cig) {
        SLIST_FOREACH(cis_conn, &cig->iso_head, next) {
            if (cis_conn->acl_handle == acl_handle) {
                ble_iso_disconnect_cis(cis_conn->iso_handle);
                os_memblock_put(&ble_iso_conn_pool, cis_conn);
            }
        }
    }
    ble_hs_unlock();
}

static void
ble_iso_cis_disconnected(uint16_t cis_handle, uint8_t reason)
{
    struct ble_iso_conn *cis_conn;
    struct ble_iso_group *cig;

    ble_hs_lock();
    SLIST_FOREACH(cig, &g_ble_iso_act_master_cig_list, active_cig) {
        SLIST_FOREACH(cis_conn, &cig->iso_head, next) {
            if (cis_conn->iso_handle == cis_handle) {
                ble_iso_send_disconnected_event(cis_conn, reason);
                SLIST_REMOVE(&cig->iso_head, cis_conn, ble_iso_conn, next);
                os_memblock_put(&ble_iso_conn_pool, cis_conn);
                ble_hs_unlock();
                return;
            }
        }
    }

    SLIST_FOREACH(cig, &g_ble_iso_act_slave_cig_list, active_cig) {
        SLIST_FOREACH(cis_conn, &cig->iso_head, next) {
            if (cis_conn->iso_handle == cis_handle) {
                ble_iso_send_disconnected_event(cis_conn, reason);
                SLIST_REMOVE(&cig->iso_head, cis_conn, ble_iso_conn, next);
                os_memblock_put(&ble_iso_conn_pool, cis_conn);
                if (SLIST_EMPTY((&cig->iso_head))) {
                    ble_iso_release_group(cig);
                }
                ble_hs_unlock();
                return;
            }
        }
    }
    ble_hs_unlock();
}

void
ble_iso_disconnected_event(uint16_t conn_handle, uint8_t reason, bool is_acl)
{
    if (is_acl) {
        ble_iso_acl_disconnected(conn_handle, reason);
    } else {
        ble_iso_cis_disconnected(conn_handle, reason);
    }
}

static void
ble_iso_send_iso_data_event(struct ble_iso_conn *conn, struct os_mbuf *om)
{
    struct ble_iso_event event;

    if (!conn->cb) {
        return;
    }

    event.type = BLE_ISO_EVENT_DATA;
    event.iso_data.handle = conn->iso_handle;
    event.iso_data.om = om;
    conn->cb(&event, conn->cb_arg);
}

int
ble_iso_hci_util_data_hdr_strip(struct os_mbuf *om,
                                struct hci_iso_hdr *out_hdr)
{
    int rc;
    /* TODO THis should be part of ble_iso_hci */
    rc = os_mbuf_copydata(om, 0, sizeof(*out_hdr), out_hdr);
    if (rc != 0) {
        return BLE_HS_ECONTROLLER;
    }

    out_hdr->handle_pb_ts = get_le16(&out_hdr->handle_pb_ts);
    out_hdr->len = BLE_HCI_ISO_DATA_LEN(get_le16(&out_hdr->len));

    if (BLE_HCI_ISO_DATA_TS(out_hdr->handle_pb_ts)) {
        out_hdr->with_ts.ts = get_le32(&out_hdr->with_ts.ts);
        out_hdr->with_ts.seq_num = get_le16(&out_hdr->with_ts.seq_num);
        out_hdr->with_ts.sdu_len_psf = get_le16(&out_hdr->with_ts.sdu_len_psf);

        /* Strip HCI ACL data header from the front of the packet. */
        os_mbuf_adj(om, BLE_HCI_ISO_HDR_SIZE_WITH_TS);

    } else {
        out_hdr->no_ts.seq_num = get_le16(&out_hdr->no_ts.seq_num);
        out_hdr->no_ts.sdu_len_psf = BLE_HCI_ISO_DATA_SDU_LEN(get_le16(&out_hdr->no_ts.sdu_len_psf));
        /* Strip HCI ACL data header from the front of the packet. */
        os_mbuf_adj(om, BLE_HCI_ISO_HDR_SIZE_NO_TS);
    }

    return 0;
}

static uint16_t
ble_iso_hci_util_handle_pb_ts_join(uint16_t handle, uint8_t pb, uint8_t ts)
{
    BLE_HS_DBG_ASSERT(handle <= 0x0fff);
    BLE_HS_DBG_ASSERT(pb <= 0x03);
    BLE_HS_DBG_ASSERT(ts <= 0x01);

    return (handle << 0) |
           (pb << 12) |
           (ts << 14);
}

static int
ble_iso_tx_iso(struct ble_iso_conn *iso_conn, struct os_mbuf *om)
{
    struct os_mbuf *om2;
    struct hci_iso_hdr *hci_iso_hdr;
    uint16_t sdu_len = OS_MBUF_PKTLEN(om);
    uint32_t ts;

    om2 = os_mbuf_prepend(om, BLE_HCI_ISO_HDR_SIZE_NO_TS);
    assert(om2);

    /* Make sure we have linear memory for header */
    om2 = os_mbuf_pullup(om2, BLE_HCI_ISO_HDR_SIZE_NO_TS);
    assert(om2);

    hci_iso_hdr = (struct hci_iso_hdr *)&om2->om_data[0];

    /* TODO Add fragmentation but this is going to be in ble_iso_hci */
    hci_iso_hdr->handle_pb_ts =
        ble_iso_hci_util_handle_pb_ts_join(iso_conn->iso_handle,
                                           BLE_HCI_ISO_PB_COMPLETE, 0);
    put_le16(&hci_iso_hdr->len, sdu_len + 4);

    ts = os_cputime_get32();
    iso_conn->last_timestamp = ts;

    /* TODO seq_num should probably be increased every SDU interval */
    put_le16(&hci_iso_hdr->no_ts.seq_num, iso_conn->seq_num++);
    put_le16(&hci_iso_hdr->no_ts.sdu_len_psf, sdu_len);

    /* TODO: Verify credits */
    return ble_hs_tx_iso_data(om2);
}

static struct ble_iso_conn *
ble_iso_iso_conn_output(uint8_t cig_id, bool master)
{
    struct ble_iso_group *cig;
    struct ble_iso_conn *iso_conn;

    if (master) {
        cig = ble_iso_find_group(cig_id, &g_ble_iso_act_master_cig_list);
    } else {
        cig = ble_iso_find_group(cig_id, &g_ble_iso_act_slave_cig_list);
    }

    assert(cig);

    ble_hs_lock();
    SLIST_FOREACH(iso_conn, &cig->iso_head, next) {
        if (iso_conn->max_pdu_output) {
            ble_hs_unlock();
            return iso_conn;
        }
    }
    ble_hs_unlock();

    return NULL;
}

void
ble_iso_rx(struct os_mbuf *om)
{
    uint16_t handle;
    struct hci_iso_hdr hci_iso_hdr;
    struct ble_iso_conn *iso_conn;
    struct ble_iso_conn *iso_conn_loop;
    int rc;
    bool is_ts;
    uint8_t ps_flag;

    rc = ble_iso_hci_util_data_hdr_strip(om, &hci_iso_hdr);
    if (rc != 0) {
        goto done;
    }

    handle = BLE_HCI_ISO_DATA_HANDLE(hci_iso_hdr.handle_pb_ts);
    is_ts = BLE_HCI_ISO_DATA_TS(hci_iso_hdr.handle_pb_ts);

    if ((is_ts && hci_iso_hdr.with_ts.sdu_len_psf == 0) ||
        (!is_ts && hci_iso_hdr.no_ts.sdu_len_psf == 0)) {
        goto done;
    }

    if (is_ts) {
        ps_flag = BLE_HCI_ISO_DATA_PS_FLAG(hci_iso_hdr.with_ts.sdu_len_psf);
    } else {
        ps_flag = BLE_HCI_ISO_DATA_PS_FLAG(hci_iso_hdr.no_ts.sdu_len_psf);
    }

    if (ps_flag) {
        BLE_HS_LEAUDIO_LOG_DEBUG("ps_flag: 0x%02x, buf size %d\n", ps_flag, om->om_len);
        if (om->om_len == 0) {
            goto done;
        }
    }

    iso_conn = ble_iso_find_by_iso_handle(handle);
    if (!iso_conn) {
        BLE_HS_LEAUDIO_LOG_ERROR("Iso not there ? handle=0x%04x\n", handle);
        goto done;
    }

    if (MYNEWT_VAL(BLE_ISO_LOOPBACK) == 1) {
        if (iso_conn->max_pdu_output == 0) {
            /* ISO is not bi directional. Check if there is any output ISO. If yes, use it */
            iso_conn_loop = ble_iso_iso_conn_output(iso_conn->cig_id, (iso_conn->flags & BLE_ISO_CIS_F_IS_MASTER));
            if (iso_conn_loop) {
                rc = ble_iso_tx_iso(iso_conn_loop, om);
            } else {
                BLE_HS_LEAUDIO_LOG_ERROR("No Output ISO\n");
            }
        } else {
            /* bi directional ISO, */
            rc = ble_iso_tx_iso(iso_conn, om);
        }
        /* In case of loop, we just return here */
        return;
    }

    ble_iso_send_iso_data_event(iso_conn, om);

done:
    os_mbuf_free_chain(om);
}

int
ble_iso_tx(uint16_t conn_handle, void *data, uint16_t data_len)
{
    int rc;

    ble_hs_lock();
    if (data_len <= ble_hs_iso_hci_max_iso_payload_sz()) {
        rc = ble_iso_tx_complete(conn_handle, data, data_len);
    } else {
        rc = ble_iso_tx_segmented(conn_handle, data, data_len);
    }
    ble_hs_unlock();

    return rc;
}
#endif
