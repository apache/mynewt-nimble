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
#include <syscfg/syscfg.h>
#include <sysinit/sysinit.h>
#include <nimble/ble.h>
#include <icbmsg/icbmsg.h>
#include <nimble/transport.h>
#include <nimble/transport/hci_ipc.h>
#include <ipc/ipc.h>

#define BLE_HCI_IPC_ID (0)

static struct hci_ipc_sm g_hci_ipc_sm;

static void ble_hci_trans_rx(const void *data, size_t len, void *user_data);
static struct ipc_ept_cfg hci_ept_cfg = {
    .name = "nrf_bt_hci",
    .cb = {
        .received = ble_hci_trans_rx,
    },
    .tx_channel = MYNEWT_VAL(BLE_TRANSPORT_IPC_TX_CHANNEL),
    .rx_channel = MYNEWT_VAL(BLE_TRANSPORT_IPC_RX_CHANNEL),
};
static uint8_t hci_ept_local_addr;

static int
icbmsg_ble_hci_send_mbuf(uint8_t type, struct os_mbuf *om)
{
    int rc;
    struct os_mbuf *x;
    struct ipc_icmsg_buf buf;

    rc = ipc_icbmsg_alloc_tx_buf(BLE_HCI_IPC_ID, &buf,
                                 1 + OS_MBUF_PKTHDR(om)->omp_len);
    assert(rc == 0);
    if (rc != 0) {
        return BLE_ERR_MEM_CAPACITY;
    }

    buf.data[0] = type;
    buf.len = 1;

    x = om;
    while (x) {
        memcpy(buf.data + buf.len, x->om_data, x->om_len);
        buf.len += x->om_len;
        x = SLIST_NEXT(x, om_next);
    }

    rc = ipc_icbmsg_send_buf(BLE_HCI_IPC_ID, hci_ept_local_addr, &buf);

    os_mbuf_free_chain(om);

    return (rc < 0) ? BLE_ERR_MEM_CAPACITY : 0;
}

static int
icbmsg_ble_hci_acl_tx(struct os_mbuf *om)
{
    return icbmsg_ble_hci_send_mbuf(HCI_IPC_TYPE_ACL, om);
}

#if !MYNEWT_VAL(BLE_CONTROLLER)
static int
icbmsg_ble_hci_iso_tx(struct os_mbuf *om)
{
    return icbmsg_ble_hci_send_mbuf(HCI_IPC_TYPE_ISO, om);
}
#endif

static void
ble_hci_trans_rx(const void *data, size_t len, void *user_data)
{
    hci_ipc_rx(&g_hci_ipc_sm, data, len);
}

static void
icbmsg_ble_hci_init(void)
{
    os_sr_t sr;

    SYSINIT_ASSERT_ACTIVE();

    OS_ENTER_CRITICAL(sr);
    hci_ept_local_addr = ipc_icmsg_register_ept(BLE_HCI_IPC_ID, &hci_ept_cfg);
    OS_EXIT_CRITICAL(sr);

    while (!ipc_icsmsg_ept_ready(BLE_HCI_IPC_ID, hci_ept_local_addr)) {
        os_cputime_delay_usecs(1000);
        ipc_process_signal(BLE_HCI_IPC_ID);
    }
}

#if MYNEWT_VAL(BLE_CONTROLLER)
int
ble_transport_to_hs_evt_impl(void *ev_buf)
{
    int rc;
    uint8_t *hci_ev = ev_buf;
    struct ipc_icmsg_buf buf;
    uint16_t length;
    uint8_t type = HCI_IPC_TYPE_EVT;

    /* struct ble_hci_ev */
    length = 2 + hci_ev[1];

    rc = ipc_icbmsg_alloc_tx_buf(BLE_HCI_IPC_ID, &buf,
                                 1 + length);
    assert(rc == 0);
    if (rc != 0) {
        return BLE_ERR_MEM_CAPACITY;
    }

    buf.data[0] = type;
    buf.len = 1;

    memcpy(buf.data + buf.len, hci_ev, length);
    buf.len += length;

    rc = ipc_icbmsg_send_buf(BLE_HCI_IPC_ID, hci_ept_local_addr, &buf);

    ble_transport_ipc_free(ev_buf);

    return (rc < 0) ? BLE_ERR_MEM_CAPACITY : 0;
}

int
ble_transport_to_hs_acl_impl(struct os_mbuf *om)
{
    return icbmsg_ble_hci_acl_tx(om);
}

void
ble_transport_hs_init(void)
{
    hci_ipc_init(NULL, &g_hci_ipc_sm);
    icbmsg_ble_hci_init();
}
#endif /* BLE_CONTROLLER */

#if !MYNEWT_VAL(BLE_CONTROLLER)
int
ble_transport_to_ll_cmd_impl(void *ev_buf)
{
    int rc;
    uint8_t *cmd = ev_buf;
    struct ipc_icmsg_buf buf;
    uint16_t length;
    uint8_t type = HCI_IPC_TYPE_CMD;

    /* struct ble_hci_cmd */
    length = 3 + cmd[2];

    rc = ipc_icbmsg_alloc_tx_buf(BLE_HCI_IPC_ID, &buf,
                                 1 + length);
    assert(rc == 0);
    if (rc != 0) {
        return BLE_ERR_MEM_CAPACITY;
    }

    buf.data[0] = type;
    buf.len = 1;

    memcpy(buf.data + buf.len, cmd, length);
    buf.len += length;

    rc = ipc_icbmsg_send_buf(BLE_HCI_IPC_ID, hci_ept_local_addr, &buf);

    ble_transport_ipc_free(ev_buf);

    return (rc < 0) ? BLE_ERR_MEM_CAPACITY :  0;
}

int
ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
    return icbmsg_ble_hci_acl_tx(om);
}

int
ble_transport_to_ll_iso_impl(struct os_mbuf *om)
{
    return icbmsg_ble_hci_iso_tx(om);
}

void
ble_transport_ll_init(void)
{
    hci_ipc_init(NULL, &g_hci_ipc_sm);
    icbmsg_ble_hci_init();
}
#endif /* !BLE_CONTROLLER */
