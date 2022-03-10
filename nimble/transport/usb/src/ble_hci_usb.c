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
#include <stddef.h>
#include "syscfg/syscfg.h"
#include "sysinit/sysinit.h"
#include "os/os.h"
#include "mem/mem.h"

#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "nimble/transport.h"

#include <class/bth/bth_device.h>

static struct os_mbuf *incoming_acl_data;

#define TX_Q_SIZE   (MYNEWT_VAL(BLE_TRANSPORT_ACL_FROM_LL_COUNT) + \
                     MYNEWT_VAL(BLE_TRANSPORT_EVT_COUNT) + \
                     MYNEWT_VAL(BLE_TRANSPORT_EVT_DISCARDABLE_COUNT))


#define BLE_HCI_USB_EVT_COUNT  \
    (MYNEWT_VAL(BLE_TRANSPORT_EVT_COUNT))

/**
 * A packet to be sent over the USB.  This can be a command, an event, or ACL
 * data.
 */
struct ble_hci_pkt {
    STAILQ_ENTRY(ble_hci_pkt) next;
    void *data;
};

static struct os_mempool pool_tx_q;
static os_membuf_t pool_tx_q_buf[ OS_MEMPOOL_SIZE(TX_Q_SIZE,
                                                  sizeof(struct ble_hci_pkt)) ];

struct tx_queue {
    STAILQ_HEAD(, ble_hci_pkt) queue;
};

static struct tx_queue ble_hci_tx_acl_queue = {STAILQ_HEAD_INITIALIZER(ble_hci_tx_acl_queue.queue)};
static struct tx_queue ble_hci_tx_evt_queue = { STAILQ_HEAD_INITIALIZER(ble_hci_tx_evt_queue.queue) };

/*
 * TinyUSB callbacks.
 */
void
tud_bt_acl_data_sent_cb(uint16_t sent_bytes)
{
    struct os_mbuf *om;
    struct ble_hci_pkt *curr_acl;
    struct ble_hci_pkt *next_acl;
    os_sr_t sr;

    OS_ENTER_CRITICAL(sr);
    curr_acl = STAILQ_FIRST(&ble_hci_tx_acl_queue.queue);
    OS_EXIT_CRITICAL(sr);

    assert(curr_acl != NULL);
    om = curr_acl->data;
    assert(om != NULL && om->om_len >= sent_bytes);
    os_mbuf_adj(om, sent_bytes);

    while (om != NULL && om->om_len == 0) {
        curr_acl->data = SLIST_NEXT(om, om_next);
        os_mbuf_free(om);
        om = curr_acl->data;
    }

    if (om == NULL) {
        OS_ENTER_CRITICAL(sr);
        STAILQ_REMOVE_HEAD(&ble_hci_tx_acl_queue.queue, next);
        next_acl = STAILQ_FIRST(&ble_hci_tx_acl_queue.queue);
        OS_EXIT_CRITICAL(sr);
        os_memblock_put(&pool_tx_q, curr_acl);
        if (next_acl != NULL) {
            om = next_acl->data;
        }
    }

    if (om != NULL) {
        tud_bt_acl_data_send(om->om_data, om->om_len);
    }
}

void
tud_bt_event_sent_cb(uint16_t sent_bytes)
{
    struct ble_hci_pkt *curr_evt;
    struct ble_hci_pkt *next_evt;
    uint8_t *hci_ev;
    os_sr_t sr;

    OS_ENTER_CRITICAL(sr);
    curr_evt = STAILQ_FIRST(&ble_hci_tx_evt_queue.queue);
    OS_EXIT_CRITICAL(sr);
    assert(curr_evt != NULL);
    hci_ev = curr_evt->data;

    assert(hci_ev != NULL && hci_ev[1] + sizeof(struct ble_hci_ev) == sent_bytes);

    ble_transport_free(hci_ev);

    OS_ENTER_CRITICAL(sr);
    STAILQ_REMOVE_HEAD(&ble_hci_tx_evt_queue.queue, next);
    next_evt = STAILQ_FIRST(&ble_hci_tx_evt_queue.queue);
    OS_EXIT_CRITICAL(sr);
    os_memblock_put(&pool_tx_q, curr_evt);

    if (next_evt != NULL) {
        hci_ev = next_evt->data;
        tud_bt_event_send(hci_ev, hci_ev[1] + sizeof(struct ble_hci_ev));
    }
}

void
tud_bt_acl_data_received_cb(void *acl_data, uint16_t data_len)
{
    uint8_t *data;
    uint32_t len;
    struct os_mbuf *om = incoming_acl_data;
    int rc;

    if (om == NULL) {
        om = ble_transport_alloc_acl_from_hs();
        assert(om != NULL);
    }
    assert(om->om_len + data_len <= MYNEWT_VAL(BLE_TRANSPORT_ACL_SIZE) +
                                    BLE_HCI_DATA_HDR_SZ);

    os_mbuf_append(om, acl_data, data_len);
    incoming_acl_data = om;
    if (om->om_len > BLE_HCI_DATA_HDR_SZ) {
        data = incoming_acl_data->om_data;
        len = data[2] + (data[3] << 8) + BLE_HCI_DATA_HDR_SZ;
        if (incoming_acl_data->om_len >= len) {
            incoming_acl_data = NULL;
            rc = ble_transport_to_ll_acl(om);
            (void)rc;
        }
    }
}

void
tud_bt_hci_cmd_cb(void *hci_cmd, size_t cmd_len)
{
    uint8_t *buf;
    int rc = -1;

    buf = ble_transport_alloc_cmd();
    assert(buf != NULL);
    memcpy(buf, hci_cmd, min(cmd_len, 258));

    rc = ble_transport_to_ll_cmd(buf);

    if (rc != 0) {
        ble_transport_free(buf);
    }
}

static int
ble_hci_trans_ll_tx(struct tx_queue *queue, struct os_mbuf *om)
{
    struct ble_hci_pkt *pkt;
    os_sr_t sr;
    bool first;

    /* If this packet is zero length, just free it */
    if (OS_MBUF_PKTLEN(om) == 0) {
        os_mbuf_free_chain(om);
        return 0;
    }

    pkt = os_memblock_get(&pool_tx_q);
    if (pkt == NULL) {
        os_mbuf_free_chain(om);
        return BLE_ERR_MEM_CAPACITY;
    }

    pkt->data = om;
    OS_ENTER_CRITICAL(sr);
    first = STAILQ_EMPTY(&queue->queue);
    STAILQ_INSERT_TAIL(&queue->queue, pkt, next);
    OS_EXIT_CRITICAL(sr);
    if (first) {
        tud_bt_acl_data_send(om->om_data, om->om_len);
    }

    return 0;
}

static int
ble_hci_trans_ll_evt_tx(void *buf)
{
    struct ble_hci_pkt *pkt;
    uint8_t *hci_ev = buf;
    os_sr_t sr;
    bool first;

    assert(hci_ev != NULL);

    if (!tud_ready()) {
        ble_transport_free(hci_ev);
        return 0;
    }

    pkt = os_memblock_get(&pool_tx_q);
    if (pkt == NULL) {
        ble_transport_free(hci_ev);
        return BLE_ERR_MEM_CAPACITY;
    }

    pkt->data = hci_ev;
    OS_ENTER_CRITICAL(sr);
    first = STAILQ_EMPTY(&ble_hci_tx_evt_queue.queue);
    STAILQ_INSERT_TAIL(&ble_hci_tx_evt_queue.queue, pkt, next);
    OS_EXIT_CRITICAL(sr);
    if (first) {
        tud_bt_event_send(hci_ev, hci_ev[1] + sizeof(struct ble_hci_ev));
    }

    return 0;
}

int
ble_transport_to_hs_acl_impl(struct os_mbuf *om)
{
    return ble_hci_trans_ll_tx(&ble_hci_tx_acl_queue, om);
}

int
ble_transport_to_hs_evt_impl(void *buf)
{
    return ble_hci_trans_ll_evt_tx(buf);
}

void
ble_transport_hs_init(void)
{
    int rc;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    rc = os_mempool_init(&pool_tx_q, TX_Q_SIZE, sizeof(struct ble_hci_pkt),
                         pool_tx_q_buf, "ble_hci_usb_tx_q");
    SYSINIT_PANIC_ASSERT(rc == 0);
}
