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
#include <errno.h>
#include <stdint.h>
#include <sysinit/sysinit.h>
#include <syscfg/syscfg.h>
#include <os/os_mbuf.h>
#include <os/os_mempool.h>
#include <nimble/transport.h>
#include <nimble/transport/hci_h4.h>
#include <class/cdc/cdc_device.h>
#include <device/usbd_pvt.h>
#include <cdc/cdc.h>
#include <nimble/hci_common.h>

#define CDC_HCI_LOG_LEVEL 2

/* State machine for assembling packets from HOST (commands and ALCs) */
static struct hci_h4_sm cdc_hci_h4sm;

static const struct cdc_callbacks cdc_hci_callback;

struct usb_in_packet;
struct usb_in_queue {
    STAILQ_HEAD(, usb_in_packet) queue;
};

static struct os_mempool usb_in_packet_pool;

#define USB_IN_PACKET_COUNT (MYNEWT_VAL(BLE_TRANSPORT_ACL_FROM_LL_COUNT) + \
                             MYNEWT_VAL(BLE_TRANSPORT_EVT_COUNT) + \
                             MYNEWT_VAL(BLE_TRANSPORT_EVT_DISCARDABLE_COUNT) + 1)

typedef struct cdc_hci_itf {
    /* CDC Interface */
    cdc_itf_t cdc_itf;
    /* ACL or Evnet packet that is currently transferred over USB */
    struct usb_in_packet *current_in_packet;
    int current_in_packet_offset;
    /* ACL and Event packets that are waiting to be transmitted */
    struct usb_in_queue usb_in_queue;
    uint8_t rx_buffer[USBD_CDC_DATA_EP_SIZE];
} cdc_hci_itf_t;

static cdc_hci_itf_t cdc_hci_itf = {
    .cdc_itf.callbacks = &cdc_hci_callback,
    .usb_in_queue = {STAILQ_HEAD_INITIALIZER(cdc_hci_itf.usb_in_queue.queue)},
};

struct usb_packet_ops {
    void (*packet_free)(struct usb_in_packet *packet);
    int (*packet_write)(struct usb_in_packet *packet, size_t offset);
    int (*packet_size)(struct usb_in_packet *packet);
};

struct usb_in_packet {
    STAILQ_ENTRY(usb_in_packet) next;
    const struct usb_packet_ops *ops;
    void *data;
};

static uint8_t usb_in_packet_pool_mem[OS_MEMPOOL_BYTES(USB_IN_PACKET_COUNT, sizeof(struct usb_in_packet))];

static void
cdc_hci_rx_cb(cdc_itf_t *itf)
{
    int ret;
    uint8_t cdc_num = itf->cdc_num;
    uint32_t available = tud_cdc_n_available(cdc_num);
    uint32_t received = 0;
    int consumed = 0;

    while ((available > 0) || (received > consumed)) {
        if (consumed == received) {
            consumed = 0;
            received = tud_cdc_n_read(cdc_num, cdc_hci_itf.rx_buffer, min(available, sizeof(cdc_hci_itf.rx_buffer)));
            available = tud_cdc_n_available(cdc_num);
        }
        ret = hci_h4_sm_rx(&cdc_hci_h4sm, cdc_hci_itf.rx_buffer + consumed, received - consumed);
        if (ret < 0) {
            tud_cdc_n_read_flush(cdc_num);
            break;
        }
        consumed += ret;
    }
}

static void
usb_in_packet_free(struct usb_in_packet *pkt)
{
    if (pkt) {
        pkt->ops->packet_free(pkt);
        os_memblock_put(&usb_in_packet_pool, pkt);
    }
}

static int
usb_in_packet_write(struct usb_in_packet *pkt, int offset)
{
    return pkt->ops->packet_write(pkt, offset);
}

static int
usb_in_packet_size(struct usb_in_packet *pkt)
{
    if (pkt) {
        return pkt->ops->packet_size(pkt);
    } else {
        return 0;
    }
}

static void
cdc_hci_send_next_in_packet(void)
{
    int sr;
    struct usb_in_packet *last_packet;

    if (cdc_hci_itf.current_in_packet == NULL ||
        cdc_hci_itf.current_in_packet_offset == usb_in_packet_size(cdc_hci_itf.current_in_packet)) {
        OS_ENTER_CRITICAL(sr);
        last_packet = cdc_hci_itf.current_in_packet;
        cdc_hci_itf.current_in_packet_offset = 0;
        cdc_hci_itf.current_in_packet = STAILQ_FIRST(&cdc_hci_itf.usb_in_queue.queue);
        if (cdc_hci_itf.current_in_packet) {
            STAILQ_REMOVE_HEAD(&cdc_hci_itf.usb_in_queue.queue, next);
        }
        OS_EXIT_CRITICAL(sr);
        usb_in_packet_free(last_packet);
    }
    if (cdc_hci_itf.current_in_packet != NULL &&
        cdc_hci_itf.current_in_packet_offset < usb_in_packet_size(cdc_hci_itf.current_in_packet)) {
        cdc_hci_itf.current_in_packet_offset += usb_in_packet_write(cdc_hci_itf.current_in_packet,
                                                                    cdc_hci_itf.current_in_packet_offset);
    }
}

static void
cdc_hci_tx_complete_cb(cdc_itf_t *itf)
{
    (void)itf;

    cdc_hci_send_next_in_packet();
}

static void
cdc_hci_line_state_cb(cdc_itf_t *itf, bool dtr, bool rts)
{
    (void)itf;
    (void)rts;

    if (dtr) {
        cdc_hci_send_next_in_packet();
    }
}

static struct usb_in_packet *
cdc_hci_get_usb_in_packet(void)
{
    struct usb_in_packet *packet = (struct usb_in_packet *)os_memblock_get(&usb_in_packet_pool);
    if (packet) {
        packet->data = NULL;
    }
    return packet;
}

static void
cdc_hci_send_next_in_packet_from_usbd_task(void *dummy)
{
    (void)dummy;
    cdc_hci_send_next_in_packet();
}

static void
cdc_hci_usb_in_enqueue_packet(struct usb_in_packet *pkt)
{
    int sr;

    /* Add to IN queue */
    OS_ENTER_CRITICAL(sr);
    STAILQ_INSERT_TAIL(&cdc_hci_itf.usb_in_queue.queue, pkt, next);
    OS_EXIT_CRITICAL(sr);

    usbd_defer_func(cdc_hci_send_next_in_packet_from_usbd_task, NULL, true);
}


/*
 * Returns packet size as seen over USB
 */
static int
cdc_hci_event_packet_size(struct usb_in_packet *packet)
{
    struct ble_hci_ev *event = packet->data;
    return event->length + 2 /* opcode + length */ + 1 /* H4 header */;
}

/*
 * Free Event packet that BLE stack provided to USB stack.
 * Function is called once all data was sent over USB IN endpoint.
 */
static void
cdc_hci_event_packet_free(struct usb_in_packet *packet)
{
    ble_transport_free(packet->data);
}

/*
 * Write Event packed from BLE stack to USB IN endpoint.
 * Flush will be called separately.
 */
static int
cdc_hci_event_packet_write(struct usb_in_packet *packet, size_t offset)
{
    uint8_t cdc_num = cdc_hci_itf.cdc_itf.cdc_num;
    const uint8_t *buf = ((const uint8_t *)packet->data) - 1;
    const struct ble_hci_ev *hci_ev = packet->data;
    size_t h4_event_size = hci_ev->length + sizeof(struct ble_hci_ev) + 1;
    size_t new_offset = offset;

    /* Write H4 Event type */
    if (new_offset == 0) {
        new_offset = tud_cdc_n_write_char(cdc_num, HCI_H4_EVT);
    }
    /* If first byte was not written rest of the event has to wait as well */
    if (new_offset > 0) {
        new_offset += tud_cdc_n_write(cdc_num, buf + new_offset, h4_event_size - new_offset);
        tud_cdc_n_write_flush(cdc_num);
    }

    return (int)(new_offset - offset);
}

/*
 * USB IN endpoint packet handling functions for Events
 */
static const struct usb_packet_ops event_packet_ops = {
    .packet_free = cdc_hci_event_packet_free,
    .packet_write = cdc_hci_event_packet_write,
    .packet_size = cdc_hci_event_packet_size,
};

/*
 * Returns packet size as seen over USB
 */
static int
cdc_hci_acl_packet_size(struct usb_in_packet *packet)
{
    struct os_mbuf *om = (struct os_mbuf *)packet->data;
    /* Data size of mbuf plus one for H4 header (2) */
    return os_mbuf_len(om) + 1;
}

/*
 * Free ACL data packet that BLE stack provided to USB stack.
 * Function is called once all data was sent over USB IN endpoint.
 */
static void
cdc_hci_acl_packet_free(struct usb_in_packet *packet)
{
    struct os_mbuf *om = (struct os_mbuf *)packet->data;

    os_mbuf_free_chain(om);
}

/*
 * Write ACL packed from BLE stack to USB IN endpoint.
 * Code traverses mbuf to send all data.
 * Flush will be called separately.
 */
static int
cdc_hci_acl_packet_write(struct usb_in_packet *packet, size_t offset)
{
    uint8_t cdc_num = cdc_hci_itf.cdc_itf.cdc_num;
    struct os_mbuf *om = (struct os_mbuf *)packet->data;
    struct os_mbuf *mb;
    size_t new_offset = offset;
    uint16_t mbuf_offset;
    size_t write_size;
    size_t written;

    if (om) {
        if (new_offset == 0) {
            /* Write H4 ACL type */
            new_offset += tud_cdc_n_write_char(cdc_num, HCI_H4_ACL);
        }

        for (;;) {
            mb = os_mbuf_off(om, (int)new_offset - 1, &mbuf_offset);
            assert(mb);
            /* mbuf_offset is == om_len when new_offset reached end of mbuf data */
            if (mb->om_len == mbuf_offset) {
                break;
            }
            /* Chunk in current mbuf */
            write_size = mb->om_len - mbuf_offset;
            written = tud_cdc_n_write(cdc_num, mb->om_data + mbuf_offset, write_size);
            new_offset += written;
            /* USB FIFO did not have enough space for whole mbuf, stop write for now */
            if (written < write_size) {
                break;
            }
        }
        tud_cdc_n_write_flush(cdc_num);
    }

    return (int)(new_offset - offset);
}

/*
 * USB IN endpoint packet handling functions for ACL data
 */
static const struct usb_packet_ops cdc_hci_acl_packet_ops = {
    .packet_free = cdc_hci_acl_packet_free,
    .packet_write = cdc_hci_acl_packet_write,
    .packet_size = cdc_hci_acl_packet_size,
};

static int
cdc_hci_frame_cb(uint8_t pkt_type, void *data)
{
    switch (pkt_type) {
    case HCI_H4_CMD:
        return ble_transport_to_ll_cmd(data);
    case HCI_H4_ACL:
        return ble_transport_to_ll_acl(data);
    default:
        assert(0);
        break;
    }

    return -1;
}

/*
 * BLE stack callback with Event to be dispatched to USB IN endpoint.
 */
int
ble_transport_to_hs_evt_impl(void *buf)
{
    struct usb_in_packet *pkt;

    assert(buf != NULL);

    pkt = cdc_hci_get_usb_in_packet();
    if (pkt == NULL) {
        ble_transport_free(buf);
        return BLE_ERR_MEM_CAPACITY;
    }

    pkt->data = buf;
    pkt->ops = &event_packet_ops;
    cdc_hci_usb_in_enqueue_packet(pkt);

    return 0;
}

/*
 * BLE stack callback with ACL data to be dispatched to USB IN endpoint.
 */
int
ble_transport_to_hs_acl_impl(struct os_mbuf *om)
{
    struct usb_in_packet *pkt;

    /* If this packet is zero length, just free it */
    if (OS_MBUF_PKTLEN(om) == 0) {
        os_mbuf_free_chain(om);
        return 0;
    }

    pkt = cdc_hci_get_usb_in_packet();
    if (pkt == NULL) {
        assert(0);
        return -ENOMEM;
    }

    pkt->data = om;
    pkt->ops = &cdc_hci_acl_packet_ops;
    cdc_hci_usb_in_enqueue_packet(pkt);

    return 0;
}

void
ble_transport_hs_init(void)
{
    int rc;

    SYSINIT_ASSERT_ACTIVE();

    rc = os_mempool_init(&usb_in_packet_pool,
                         USB_IN_PACKET_COUNT,
                         sizeof(struct usb_in_packet),
                         usb_in_packet_pool_mem,
                         "usb_in_packet_pool");

    SYSINIT_PANIC_ASSERT(rc == 0);

    cdc_itf_add(&cdc_hci_itf.cdc_itf);

    hci_h4_sm_init(&cdc_hci_h4sm, &hci_h4_allocs_from_hs, cdc_hci_frame_cb);
}

static const struct cdc_callbacks cdc_hci_callback = {
    .cdc_rx_cb = cdc_hci_rx_cb,
    .cdc_line_coding_cb = NULL,
    .cdc_line_state_cb = cdc_hci_line_state_cb,
    .cdc_rx_wanted_cb = NULL,
    .cdc_send_break_cb = NULL,
    .cdc_tx_complete_cb = cdc_hci_tx_complete_cb,
};

