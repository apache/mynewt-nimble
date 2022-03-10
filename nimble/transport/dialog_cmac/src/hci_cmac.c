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
#if MYNEWT_VAL(BLE_CONTROLLER)
/* to enable dser diag */
#include <mcu/mcu.h>
#endif /* BLE_CONTROLLER */
#include <cmac_driver/cmac_shared.h>
#if !MYNEWT_VAL(BLE_CONTROLLER)
#include <cmac_driver/cmac_host.h>
#endif /* !BLE_CONTROLLER */
#include <os/os_mbuf.h>
#include <os/os_mempool.h>
#include <nimble/transport.h>
#include <nimble/transport/hci_h4.h>

static struct hci_h4_sm hci_cmac_h4sm;

static int
hci_cmac_acl_tx(struct os_mbuf *om)
{
    uint8_t pkt_type = HCI_H4_ACL;
    struct os_mbuf *om_next;

    cmac_mbox_write(&pkt_type, sizeof(pkt_type));

    while (om) {
        om_next = SLIST_NEXT(om, om_next);

        cmac_mbox_write(om->om_data, om->om_len);

        os_mbuf_free(om);
        om = om_next;
    }

    return 0;
}

#if !MYNEWT_VAL(BLE_CONTROLLER)
static int
hci_cmac_hs_frame_cb(uint8_t pkt_type, void *data)
{
    int rc;

    switch (pkt_type) {
    case HCI_H4_ACL:
        rc = ble_transport_to_hs_acl(data);
        break;
    case HCI_H4_EVT:
        rc = ble_transport_to_hs_evt(data);
        break;
    default:
        assert(0);
        break;
    }

    return rc;
}

static int
hci_cmac_hs_mbox_read_cb(const void *data, uint16_t len)
{
    int rlen;

    rlen = hci_h4_sm_rx(&hci_cmac_h4sm, data, len);
    assert(rlen >= 0);

    return rlen;
}

static void
hci_cmac_hs_mbox_write_notif_cb(void)
{
    cmac_host_signal2cmac();
}

void
ble_transport_ll_init(void)
{
    hci_h4_sm_init(&hci_cmac_h4sm, &hci_h4_allocs_from_ll, hci_cmac_hs_frame_cb);

    /* We can now handle data from CMAC, initialize it */
    cmac_mbox_set_read_cb(hci_cmac_hs_mbox_read_cb);
    cmac_mbox_set_write_notif_cb(hci_cmac_hs_mbox_write_notif_cb);
    cmac_host_init();
}
#endif /* !BLE_CONTROLLER */

#if MYNEWT_VAL(BLE_CONTROLLER)
#if !MYNEWT_VAL(MCU_DEBUG_DSER_BLE_HCI_CMAC_LL)
#define MCU_DIAG_SER_DISABLE
#endif

static int
hci_cmac_ll_frame_cb(uint8_t pkt_type, void *data)
{
    int rc;

    switch (pkt_type) {
    case HCI_H4_CMD:
        rc = ble_transport_to_ll_cmd(data);
        break;
    case HCI_H4_ACL:
        rc = ble_transport_to_ll_acl(data);
        break;
    default:
        assert(0);
        break;
    }

    return rc;
}

static int
hci_cmac_ll_mbox_read_cb(const void *data, uint16_t len)
{
    int rlen;

    MCU_DIAG_SER('R');
    rlen = hci_h4_sm_rx(&hci_cmac_h4sm, data, len);
    assert(rlen >= 0);

    return rlen;
}

static void
hci_cmac_ll_mbox_write_notif_cb(void)
{
    MCU_DIAG_SER('W');
    CMAC->CM_EV_SET_REG = CMAC_CM_EV_SET_REG_EV1C_CMAC2SYS_IRQ_SET_Msk;
}

void
ble_transport_hs_init(void)
{
    hci_h4_sm_init(&hci_cmac_h4sm, &hci_h4_allocs_from_hs, hci_cmac_ll_frame_cb);

    /* Setup callbacks for mailboxes */
    cmac_mbox_set_read_cb(hci_cmac_ll_mbox_read_cb);
    cmac_mbox_set_write_notif_cb(hci_cmac_ll_mbox_write_notif_cb);

    /* Synchronize with SYS */
    cmac_shared_sync();
}
#endif

#if !MYNEWT_VAL(BLE_CONTROLLER)
int
ble_transport_to_ll_cmd_impl(void *buf)
{
    uint8_t pkt_type = HCI_H4_CMD;
    uint8_t *cmd = buf;

    cmac_mbox_write(&pkt_type, sizeof(pkt_type));
    cmac_mbox_write(cmd, cmd[2] + 3);

    ble_transport_free(buf);

    return 0;
}

int
ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
    return hci_cmac_acl_tx(om);
}
#endif

#if MYNEWT_VAL(BLE_CONTROLLER)
int
ble_transport_to_hs_acl_impl(struct os_mbuf *om)
{
    return hci_cmac_acl_tx(om);
}

int
ble_transport_to_hs_evt_impl(void *buf)
{
    uint8_t pkt_type = HCI_H4_EVT;
    uint8_t *evt = buf;
    os_sr_t sr;

    OS_ENTER_CRITICAL(sr);

    cmac_mbox_write(&pkt_type, sizeof(pkt_type));
    cmac_mbox_write(evt, evt[1] + 2);

    ble_transport_free(buf);

    OS_EXIT_CRITICAL(sr);

    return 0;
}
#endif /* BLE_CONTROLLER */
