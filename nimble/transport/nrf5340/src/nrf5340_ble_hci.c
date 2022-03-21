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
#include <sysinit/sysinit.h>
#include <nimble/ble.h>
#include <ipc_nrf5340/ipc_nrf5340.h>
#include <nimble/transport.h>
#include <nimble/transport/hci_h4.h>

#if MYNEWT_VAL(BLE_CONTROLLER)
#define IPC_TX_CHANNEL 0
#define IPC_RX_CHANNEL 1
#else
#define IPC_TX_CHANNEL 1
#define IPC_RX_CHANNEL 0
#endif

static struct hci_h4_sm hci_nrf5340_h4sm;

static int
nrf5340_ble_hci_acl_tx(struct os_mbuf *om)
{
    uint8_t ind = HCI_H4_ACL;
    struct os_mbuf *x;
    int rc;

    rc = ipc_nrf5340_send(IPC_TX_CHANNEL, &ind, 1);
    if (rc == 0) {
        x = om;
        while (x) {
            rc = ipc_nrf5340_send(IPC_TX_CHANNEL, x->om_data, x->om_len);
            if (rc < 0) {
                break;
            }
            x = SLIST_NEXT(x, om_next);
        }
    }

    os_mbuf_free_chain(om);

    return (rc < 0) ? BLE_ERR_MEM_CAPACITY : 0;
}

static int
nrf5340_ble_hci_frame_cb(uint8_t pkt_type, void *data)
{
    int rc;

    switch (pkt_type) {
#if MYNEWT_VAL(BLE_CONTROLLER)
    case HCI_H4_CMD:
        rc = ble_transport_to_ll_cmd(data);
        break;
#endif
    case HCI_H4_ACL:
#if MYNEWT_VAL(BLE_CONTROLLER)
        rc = ble_transport_to_ll_acl(data);
#else
        rc = ble_transport_to_hs_acl(data);
#endif
        break;
#if !MYNEWT_VAL(BLE_CONTROLLER)
    case HCI_H4_EVT:
        rc = ble_transport_to_hs_evt(data);
        break;
#endif
    default:
        assert(0);
        break;
    }

    return rc;
}

static void
nrf5340_ble_hci_trans_rx(int channel, void *user_data)
{
    uint8_t *buf;
    int len;

    len = ipc_nrf5340_available_buf(channel, (void **)&buf);
    while (len > 0) {
        len = hci_h4_sm_rx(&hci_nrf5340_h4sm, buf, len);
        ipc_nrf5340_consume(channel, len);
        len = ipc_nrf5340_available_buf(channel, (void **)&buf);
    }
}

static void
nrf5340_ble_hci_init(void)
{
    SYSINIT_ASSERT_ACTIVE();

    ipc_nrf5340_recv(IPC_RX_CHANNEL, nrf5340_ble_hci_trans_rx, NULL);
}

#if MYNEWT_VAL(BLE_CONTROLLER)
int
ble_transport_to_hs_evt_impl(void *buf)
{
    uint8_t ind = HCI_H4_EVT;
    uint8_t* hci_ev = buf;
    int len = 2 + hci_ev[1];
    int rc;

    rc = ipc_nrf5340_send(IPC_TX_CHANNEL, &ind, 1);
    if (rc == 0) {
        rc = ipc_nrf5340_send(IPC_TX_CHANNEL, hci_ev, len);
    }

    ble_transport_free(buf);

    return (rc < 0) ? BLE_ERR_MEM_CAPACITY : 0;
}

int
ble_transport_to_hs_acl_impl(struct os_mbuf *om)
{
    return nrf5340_ble_hci_acl_tx(om);
}

void
ble_transport_hs_init(void)
{
    hci_h4_sm_init(&hci_nrf5340_h4sm, &hci_h4_allocs_from_hs,
                   nrf5340_ble_hci_frame_cb);
    nrf5340_ble_hci_init();
}
#endif /* BLE_CONTROLLER */

#if !MYNEWT_VAL(BLE_CONTROLLER)
int
ble_transport_to_ll_cmd_impl(void *buf)
{
    uint8_t ind = HCI_H4_CMD;
    uint8_t *cmd = buf;
    int len = 3 + cmd[2];
    int rc;

    rc = ipc_nrf5340_send(IPC_TX_CHANNEL, &ind, 1);
    if (rc == 0) {
        rc = ipc_nrf5340_send(IPC_TX_CHANNEL, cmd, len);
    }

    ble_transport_free(buf);

    return (rc < 0) ? BLE_ERR_MEM_CAPACITY :  0;
}

int
ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
    return nrf5340_ble_hci_acl_tx(om);
}

void
ble_transport_ll_init(void)
{
    hci_h4_sm_init(&hci_nrf5340_h4sm, &hci_h4_allocs_from_ll,
                   nrf5340_ble_hci_frame_cb);
    nrf5340_ble_hci_init();
}
#endif /* !BLE_CONTROLLER */
