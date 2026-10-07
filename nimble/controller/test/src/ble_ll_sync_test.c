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

#include <stdint.h>
#include <string.h>
#include <nimble/ble.h>
#include <nimble/hci_common.h>
#include <controller/ble_ll.h>
#include <controller/ble_ll_conn.h>
#include <controller/ble_ll_scan.h>
#include <controller/ble_ll_sync.h>
#include <testutil/testutil.h>

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PERIODIC_ADV)

static uint8_t test_adv_addr[6] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0xc6 };

/* SyncInfo with valid offset and interval and all-zero channel map */
static void
test_sync_syncinfo_zero_chan_map(uint8_t *syncinfo)
{
    memset(syncinfo, 0, BLE_LL_EXT_ADV_SYNC_INFO_SIZE);

    /* Sync Packet Offset */
    syncinfo[0] = 0x10;
    /* Interval */
    put_le16(&syncinfo[2], 0x0050);
    /* Channel Map left all-zero */
    /* Access Address */
    put_le32(&syncinfo[9], 0x8e89bed6);
    /* CRCInit */
    syncinfo[13] = 0x55;
    syncinfo[14] = 0x55;
    syncinfo[15] = 0x55;
}

TEST_CASE_SELF(test_ll_sync_info_zero_chan_map)
{
    struct ble_hci_le_periodic_adv_create_sync_cp cmd;
    uint8_t syncinfo[BLE_LL_EXT_ADV_SYNC_INFO_SIZE];
    struct ble_ll_scan_addr_data addrd;
    struct ble_mbuf_hdr rxhdr;
    int rc;

    memset(&cmd, 0, sizeof(cmd));
    cmd.sid = 1;
    cmd.peer_addr_type = BLE_ADDR_RANDOM;
    memcpy(cmd.peer_addr, test_adv_addr, 6);
    cmd.sync_timeout = htole16(100);

    rc = ble_ll_sync_create((uint8_t *)&cmd, sizeof(cmd));
    TEST_ASSERT_FATAL(rc == 0);

    memset(&addrd, 0, sizeof(addrd));
    addrd.adv_addr = test_adv_addr;
    addrd.adv_addr_type = BLE_ADDR_RANDOM;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    addrd.rpa_index = -1;
#endif

    memset(&rxhdr, 0, sizeof(rxhdr));

    test_sync_syncinfo_zero_chan_map(syncinfo);

    /* SyncInfo with no used channels shall be ignored */
    ble_ll_sync_info_event(&addrd, 1, &rxhdr, syncinfo);

    ble_ll_sync_reset();
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PERIODIC_ADV_SYNC_TRANSFER)
TEST_CASE_SELF(test_ll_sync_periodic_ind_zero_chan_map)
{
    uint8_t sync_ind[34];
    struct ble_ll_conn_sm connsm;

    memset(&connsm, 0, sizeof(connsm));
    memset(sync_ind, 0, sizeof(sync_ind));

    test_sync_syncinfo_zero_chan_map(&sync_ind[2]);

    /* SID 1, random address */
    sync_ind[24] = 0x11;
    /* LE 1M PHY */
    sync_ind[25] = 0x01;
    memcpy(&sync_ind[26], test_adv_addr, 6);

    /* LL_PERIODIC_SYNC_IND with no used channels shall be ignored */
    ble_ll_sync_periodic_ind(&connsm, sync_ind, 0x01, 0, 100);

    ble_ll_sync_reset();
}
#endif

TEST_SUITE(ble_ll_sync_test_suite)
{
    test_ll_sync_info_zero_chan_map();
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PERIODIC_ADV_SYNC_TRANSFER)
    test_ll_sync_periodic_ind_zero_chan_map();
#endif
}

#else

TEST_SUITE(ble_ll_sync_test_suite)
{
}

#endif
