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

struct ble_iso_big {
    SLIST_ENTRY(ble_iso_big) next;
    uint8_t id;
    uint16_t max_pdu;
    uint8_t num_bis;
    uint16_t conn_handles[MYNEWT_VAL(BLE_MAX_BIS)];

    ble_iso_event_fn *cb;
    void *cb_arg;
};

static SLIST_HEAD(, ble_iso_big) ble_iso_bigs;
static struct os_mempool ble_iso_big_pool;
static os_membuf_t ble_iso_big_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_MAX_BIG), sizeof (struct ble_iso_big))];

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
    int rc;

    SLIST_INIT(&ble_iso_bigs);

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
