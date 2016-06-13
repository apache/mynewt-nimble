/**
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

#include "os/os.h"
#include "nimble/hci_common.h"
#include "host/ble_hs_test.h"
#include "testutil/testutil.h"
#include "ble_hs_test_util.h"

/* Our global device address. */
uint8_t g_dev_addr[BLE_DEV_ADDR_LEN];

void
ble_hs_test_pkt_txed(struct os_mbuf *om)
{
    /* XXX: For now, just strip the HCI ACL data and L2CAP headers. */
    os_mbuf_adj(om, BLE_HCI_DATA_HDR_SZ + BLE_L2CAP_HDR_SZ);
    ble_hs_test_util_prev_tx_enqueue(om);
}

void
ble_hs_test_hci_txed(uint8_t *cmdbuf)
{
    ble_hs_test_util_enqueue_hci_tx(cmdbuf);
    os_memblock_put(&g_hci_cmd_pool, cmdbuf);
}

#ifdef MYNEWT_SELFTEST

int
main(int argc, char **argv)
{
    tu_config.tc_print_results = 1;
    tu_parse_args(argc, argv);

    tu_init();

    ble_att_clt_test_all();
    ble_att_svr_test_all();
    ble_gap_test_all();
    ble_gatt_conn_test_all();
    ble_gatt_disc_c_test_all();
    ble_gatt_disc_d_test_all();
    ble_gatt_disc_s_test_all();
    ble_gatt_find_s_test_all();
    ble_gatt_read_test_all();
    ble_gatt_write_test_all();
    ble_gatts_notify_test_all();
    ble_gatts_reg_test_all();
    ble_host_hci_test_all();
    ble_hs_adv_test_all();
    ble_hs_conn_test_all();
    ble_sm_test_all();
    ble_sm_sc_test_all();
    ble_l2cap_test_all();
    ble_os_test_all();
    ble_uuid_test_all();

    return tu_any_failed;
}

#endif
