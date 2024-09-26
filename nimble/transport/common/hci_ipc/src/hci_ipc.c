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
#include <stdint.h>
#include <string.h>
#include <syscfg/syscfg.h>
#include <os/os.h>
#include <os/os_mbuf.h>
#include <nimble/transport.h>
#include <nimble/transport/hci_ipc.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

volatile struct hci_ipc_shm *g_ipc_shm;

static void
hci_ipc_alloc(struct hci_ipc_sm *sm)
{
    assert(sm->hdr.type);
    assert(sm->buf == NULL);

    switch (sm->hdr.type) {
#if MYNEWT_VAL(BLE_CONTROLLER)
    case HCI_IPC_TYPE_CMD:
        sm->buf = ble_transport_alloc_cmd();
        break;
#endif
    case HCI_IPC_TYPE_ACL:
#if MYNEWT_VAL(BLE_CONTROLLER)
        sm->om = ble_transport_alloc_acl_from_hs();
#else
        sm->om = ble_transport_alloc_acl_from_ll();
#endif
        break;
#if !MYNEWT_VAL(BLE_CONTROLLER)
    case HCI_IPC_TYPE_EVT:
        sm->buf = ble_transport_alloc_evt(0);
        break;
    case HCI_IPC_TYPE_EVT_DISCARDABLE:
        sm->buf = ble_transport_alloc_evt(1);
        break;
    case HCI_IPC_TYPE_EVT_IN_CMD:
        sm->buf = ble_transport_alloc_cmd();
        break;
#endif
    case HCI_IPC_TYPE_ISO:
#if MYNEWT_VAL(BLE_CONTROLLER)
        sm->om = ble_transport_alloc_iso_from_hs();
#else
        sm->om = ble_transport_alloc_iso_from_ll();
#endif
        break;
    default:
        assert(0);
        break;
    }

    assert(sm->buf);

    sm->rem_len = sm->hdr.length;
    sm->buf_len = 0;
}

static bool
hci_ipc_has_hdr(struct hci_ipc_sm *sm)
{
#if MYNEWT_VAL(IPC_ICBMSG)
    return sm->hdr_len == sizeof(sm->hdr.type);
#else
    return sm->hdr_len == sizeof(sm->hdr);
#endif
}

static void
hci_ipc_frame(struct hci_ipc_sm *sm)
{
    assert(sm->hdr.type);
    assert(sm->buf);
    assert(sm->rem_len == 0);

    switch (sm->hdr.type) {
#if MYNEWT_VAL(BLE_CONTROLLER)
    case HCI_IPC_TYPE_CMD:
        ble_transport_to_ll_cmd(sm->buf);
        break;
#endif
    case HCI_IPC_TYPE_ACL:
#if MYNEWT_VAL(BLE_CONTROLLER)
        ble_transport_to_ll_acl(sm->om);
#else
        ble_transport_to_hs_acl(sm->om);
#endif
        break;
#if !MYNEWT_VAL(BLE_CONTROLLER)
    case HCI_IPC_TYPE_EVT:
    case HCI_IPC_TYPE_EVT_DISCARDABLE:
    case HCI_IPC_TYPE_EVT_IN_CMD:
        ble_transport_to_hs_evt(sm->buf);
        break;
#endif
    case HCI_IPC_TYPE_ISO:
#if MYNEWT_VAL(BLE_CONTROLLER)
        ble_transport_to_ll_iso(sm->om);
#else
        ble_transport_to_hs_iso(sm->om);
#endif
        break;
    default:
        assert(0);
        break;
    }

    sm->hdr.type = 0;
    sm->hdr.length = 0;
    sm->hdr_len = 0;
    sm->buf_len = 0;
    sm->rem_len = 0;
    sm->buf = NULL;
}

#if MYNEWT_VAL(IPC_ICBMSG)
static uint16_t
hci_ipc_get_pkt_size(const uint8_t *buf, uint8_t type)
{
    switch (type) {
    case HCI_IPC_TYPE_ACL:
        return sizeof(struct hci_data_hdr) + get_le16(&((struct hci_data_hdr *)buf)->hdh_len);
    case HCI_IPC_TYPE_EVT:
        return sizeof(struct ble_hci_ev) + ((struct ble_hci_ev *)buf)->length;
    case HCI_IPC_TYPE_CMD:
        return sizeof(struct ble_hci_cmd) + ((struct ble_hci_cmd *)buf)->length;
    case HCI_IPC_TYPE_ISO:
        return sizeof(struct ble_hci_iso) + get_le16(&((struct ble_hci_iso *)buf)->length);
    }

    return 0;
}
#endif

static uint16_t
hci_ipc_copy_to_hdr(struct hci_ipc_sm *sm, const uint8_t *buf, uint16_t len)
{
#if MYNEWT_VAL(IPC_ICBMSG)
    len = 1;
    sm->hdr.type = buf[0];
    sm->hdr.length = hci_ipc_get_pkt_size(buf + 1, sm->hdr.type);
#else
    uint16_t rem_hdr_len;
    uint8_t *p;

    if (hci_ipc_has_hdr(sm)) {
        return 0;
    }

    rem_hdr_len = sizeof(sm->hdr) - sm->hdr_len;
    len = min(len, rem_hdr_len);

    p = (void *)&sm->hdr;
    memcpy(p + sm->hdr_len, buf, len);
#endif

    sm->hdr_len += len;

    if (hci_ipc_has_hdr(sm)) {
        hci_ipc_alloc(sm);
    }

    return len;
}

static uint16_t
hci_ipc_copy_to_buf(struct hci_ipc_sm *sm, const uint8_t *buf, uint16_t len)
{
    int rc;

    assert(sm->hdr.type);
    assert(sm->buf);

    len = min(len, sm->rem_len);

    switch (sm->hdr.type) {
#if MYNEWT_VAL(BLE_CONTROLLER)
    case HCI_IPC_TYPE_CMD:
#else
    case HCI_IPC_TYPE_EVT:
    case HCI_IPC_TYPE_EVT_DISCARDABLE:
    case HCI_IPC_TYPE_EVT_IN_CMD:
#endif
        memcpy(sm->buf + sm->buf_len, buf, len);
        break;
    case HCI_IPC_TYPE_ACL:
    case HCI_IPC_TYPE_ISO:
        rc = os_mbuf_append(sm->om, buf, len);
        assert(rc == 0);
        break;
    default:
        assert(0);
        break;
    }

    sm->rem_len -= len;
    sm->buf_len += len;

    if (sm->rem_len == 0) {
        hci_ipc_frame(sm);
    }

    return len;
}

int
hci_ipc_rx(struct hci_ipc_sm *sm, const uint8_t *buf, uint16_t len)
{
    uint16_t rem_len = len;
    uint16_t copy_len;

    while (rem_len) {
        if (hci_ipc_has_hdr(sm)) {
            copy_len = hci_ipc_copy_to_buf(sm, buf, rem_len);
        } else {
            copy_len = hci_ipc_copy_to_hdr(sm, buf, rem_len);
        }

        rem_len -= copy_len;
        buf += copy_len;
    }

    return len;
}

void
hci_ipc_init(volatile struct hci_ipc_shm *shm, struct hci_ipc_sm *sm)
{
    memset(sm, 0, sizeof(*sm));
#if !MYNEWT_VAL(IPC_ICBMSG)
    assert(g_ipc_shm == NULL);

    g_ipc_shm = shm;

#if MYNEWT_VAL(BLE_CONTROLLER)
    while (shm->n2a_num_evt_disc == 0) {
        /* Wait until app side initializes credits */
    }
#else
    shm->n2a_num_acl = MYNEWT_VAL(BLE_TRANSPORT_ACL_FROM_LL_COUNT);
    shm->n2a_num_evt = MYNEWT_VAL(BLE_TRANSPORT_EVT_COUNT);
    shm->n2a_num_evt_disc = MYNEWT_VAL(BLE_TRANSPORT_EVT_DISCARDABLE_COUNT);
#endif
#endif
}
