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
#include <controller/ble_ll_conn.h>
#include <controller/ble_ll_utils.h>
#include <testutil/testutil.h>

TEST_CASE_SELF(ble_ll_csa2_test_1)
{
    struct ble_ll_conn_sm conn;
    uint8_t rc;

    /*
     * Note: This test only verified mapped channel. Sample data also specifies
     * prn_e and unmapped channel values but those would require extra access
     * to internal state of algorithm which is not exposed.
     */

    memset(&conn, 0, sizeof(conn));
    conn.flags.csa2 = 1;

    /*
     * based on sample data from CoreSpec 5.0 Vol 6 Part C 3.1
     * (all channels used)
     */
    conn.channel_id = ((0x8e89bed6 & 0xffff0000) >> 16) ^
                       (0x8e89bed6 & 0x0000ffff);

    conn.chan_map_used = 37;
    conn.chan_map[0] = 0xff;
    conn.chan_map[1] = 0xff;
    conn.chan_map[2] = 0xff;
    conn.chan_map[3] = 0xff;
    conn.chan_map[4] = 0x1f;

    conn.event_cntr = 1;
    rc = ble_ll_conn_calc_dci(&conn, 0);
    TEST_ASSERT(rc == 20);

    conn.event_cntr = 2;
    rc = ble_ll_conn_calc_dci(&conn, 0);
    TEST_ASSERT(rc == 6);

    conn.event_cntr = 3;
    rc = ble_ll_conn_calc_dci(&conn, 0);
    TEST_ASSERT(rc == 21);
}

TEST_CASE_SELF(ble_ll_csa2_test_2)
{
    struct ble_ll_conn_sm conn;
    uint8_t rc;

    /*
     * Note: This test only verified mapped channel. Sample data also specifies
     * prn_e and unmapped channel values but those would require extra access
     * to internal state of algorithm which is not exposed.
     */

    memset(&conn, 0, sizeof(conn));
    conn.flags.csa2 = 1;

    /*
     * based on sample data from CoreSpec 5.0 Vol 6 Part C 3.2
     * (9 channels used)
     */
    conn.channel_id = ((0x8e89bed6 & 0xffff0000) >> 16) ^
                       (0x8e89bed6 & 0x0000ffff);

    conn.chan_map_used = 9;
    conn.chan_map[0] = 0x00;
    conn.chan_map[1] = 0x06;
    conn.chan_map[2] = 0xe0;
    conn.chan_map[3] = 0x00;
    conn.chan_map[4] = 0x1e;

    conn.event_cntr = 6;
    rc = ble_ll_conn_calc_dci(&conn, 0);
    TEST_ASSERT(rc == 23);

    conn.event_cntr = 7;
    rc = ble_ll_conn_calc_dci(&conn, 0);
    TEST_ASSERT(rc == 9);

    conn.event_cntr = 8;
    rc = ble_ll_conn_calc_dci(&conn, 0);
    TEST_ASSERT(rc == 34);
}

TEST_CASE_SELF(ble_ll_csa2_test_3)
{
    uint8_t chan_map[5];
    uint8_t chan_map_used;
    uint16_t chan_id;
    uint16_t prn_sub_lu;
    uint16_t chan_idx;
    uint16_t remap_idx;

    /* Sample data: Core 5.3, Vol 6, Part C, 3.1 */
    chan_map[0] = 0xff;
    chan_map[1] = 0xff;
    chan_map[2] = 0xff;
    chan_map[3] = 0xff;
    chan_map[4] = 0x1f;
    chan_map_used = ble_ll_utils_chan_map_used_get(chan_map);
    TEST_ASSERT(chan_map_used == 37);
    chan_id = 0x305f;

    chan_idx = ble_ll_utils_dci_iso_event(0, chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 56857);
    TEST_ASSERT(chan_idx == 25);
    TEST_ASSERT(remap_idx == 25);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 11710);
    TEST_ASSERT(chan_idx == 1);
    TEST_ASSERT(remap_idx == 1);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 16649);
    TEST_ASSERT(chan_idx == 16);
    TEST_ASSERT(remap_idx == 16);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 38198);
    TEST_ASSERT(chan_idx == 36);
    TEST_ASSERT(remap_idx == 36);

    chan_idx = ble_ll_utils_dci_iso_event(1, chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 1685);
    TEST_ASSERT(chan_idx == 20);
    TEST_ASSERT(remap_idx == 20);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 20925);
    TEST_ASSERT(chan_idx == 36);
    TEST_ASSERT(remap_idx == 36);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 11081);
    TEST_ASSERT(chan_idx == 12);
    TEST_ASSERT(remap_idx == 12);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 48920);
    TEST_ASSERT(chan_idx == 34);
    TEST_ASSERT(remap_idx == 34);

    chan_idx = ble_ll_utils_dci_iso_event(2, chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 38301);
    TEST_ASSERT(chan_idx == 6);
    TEST_ASSERT(remap_idx == 6);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 6541);
    TEST_ASSERT(chan_idx == 18);
    TEST_ASSERT(remap_idx == 18);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 14597);
    TEST_ASSERT(chan_idx == 32);
    TEST_ASSERT(remap_idx == 32);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 62982);
    TEST_ASSERT(chan_idx == 21);
    TEST_ASSERT(remap_idx == 21);

    chan_idx = ble_ll_utils_dci_iso_event(3, chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 27475);
    TEST_ASSERT(chan_idx == 21);
    TEST_ASSERT(remap_idx == 21);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 40400);
    TEST_ASSERT(chan_idx == 4);
    TEST_ASSERT(remap_idx == 4);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 30015);
    TEST_ASSERT(chan_idx == 22);
    TEST_ASSERT(remap_idx == 22);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 49818);
    TEST_ASSERT(chan_idx == 8);
    TEST_ASSERT(remap_idx == 8);

    /* Sample data: Core 5.3, Vol 6, Part C, 3.2 */
    chan_map[0] = 0x00;
    chan_map[1] = 0x06;
    chan_map[2] = 0xe0;
    chan_map[3] = 0x00;
    chan_map[4] = 0x1e;
    chan_map_used = ble_ll_utils_chan_map_used_get(chan_map);
    TEST_ASSERT(chan_map_used == 9);
    chan_id = 0x305f;

    chan_idx = ble_ll_utils_dci_iso_event(6, chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 10975);
    TEST_ASSERT(chan_idx == 23);
    TEST_ASSERT(remap_idx == 4);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 14383);
    TEST_ASSERT(chan_idx == 35);
    TEST_ASSERT(remap_idx == 7);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 28946);
    TEST_ASSERT(chan_idx == 21);
    TEST_ASSERT(remap_idx == 2);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 61038);
    TEST_ASSERT(chan_idx == 36);
    TEST_ASSERT(remap_idx == 8);

    chan_idx = ble_ll_utils_dci_iso_event(7, chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 5490);
    TEST_ASSERT(chan_idx == 9);
    TEST_ASSERT(remap_idx == 0);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 4108);
    TEST_ASSERT(chan_idx == 22);
    TEST_ASSERT(remap_idx == 3);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 45462);
    TEST_ASSERT(chan_idx == 36);
    TEST_ASSERT(remap_idx == 8);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 64381);
    TEST_ASSERT(chan_idx == 33);
    TEST_ASSERT(remap_idx == 5);

    chan_idx = ble_ll_utils_dci_iso_event(8, chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 46970);
    TEST_ASSERT(chan_idx == 34);
    TEST_ASSERT(remap_idx == 6);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 7196);
    TEST_ASSERT(chan_idx == 9);
    TEST_ASSERT(remap_idx == 0);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 33054);
    TEST_ASSERT(chan_idx == 33);
    TEST_ASSERT(remap_idx == 5);

    chan_idx = ble_ll_utils_dci_iso_subevent(chan_id, &prn_sub_lu, chan_map_used, chan_map, &remap_idx);
    TEST_ASSERT((prn_sub_lu ^ chan_id) == 42590);
    TEST_ASSERT(chan_idx == 10);
    TEST_ASSERT(remap_idx == 1);
}

TEST_SUITE(ble_ll_csa2_test_suite)
{
    ble_ll_csa2_test_1();
    ble_ll_csa2_test_2();
    ble_ll_csa2_test_3();
}
