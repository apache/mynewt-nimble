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

#include <string.h>
#include "syscfg/syscfg.h"
#include "testutil/testutil.h"
#include "nimble/transport/hci_h4.h"
#include "ble_hs_test.h"

#define BLE_HCI_H4_TEST_EVT_SIZE MYNEWT_VAL(BLE_TRANSPORT_EVT_SIZE)

static uint8_t ble_hci_h4_test_evt_buf[BLE_HCI_H4_TEST_EVT_SIZE];
static int ble_hci_h4_test_evt_allocated;
static int ble_hci_h4_test_num_frames;
static uint8_t ble_hci_h4_test_frame_type;

static void *
ble_hci_h4_test_alloc_evt(int discardable)
{
    if (ble_hci_h4_test_evt_allocated) {
        return NULL;
    }

    ble_hci_h4_test_evt_allocated = 1;

    return ble_hci_h4_test_evt_buf;
}

static int
ble_hci_h4_test_frame_cb(uint8_t pkt_type, void *data)
{
    TEST_ASSERT(data == ble_hci_h4_test_evt_buf);

    ble_hci_h4_test_frame_type = pkt_type;
    ble_hci_h4_test_num_frames++;

    return 0;
}

static const struct hci_h4_allocators ble_hci_h4_test_allocs = {
    .evt = ble_hci_h4_test_alloc_evt,
};

TEST_CASE_SELF(ble_hci_h4_test_evt_too_long)
{
    static const uint8_t valid_evt[] = {
        HCI_H4_EVT, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00,
    };
    struct hci_h4_sm h4sm;
    uint8_t data[3 + 255 + sizeof(valid_evt)];
    int rc;

    /* Only relevant if event buffer cannot hold max sized event */
    if (BLE_HCI_H4_TEST_EVT_SIZE >= 2 + 255) {
        return;
    }

    ble_hci_h4_test_evt_allocated = 0;
    ble_hci_h4_test_num_frames = 0;

    hci_h4_sm_init(&h4sm, &ble_hci_h4_test_allocs, ble_hci_h4_test_frame_cb);

    /* Event with 255 bytes of parameters followed by valid event */
    memset(data, 0xaa, sizeof(data));
    data[0] = HCI_H4_EVT;
    data[1] = 0x0e;
    data[2] = 0xff;
    memcpy(&data[3 + 255], valid_evt, sizeof(valid_evt));

    /* Too long event shall be dropped without allocating a buffer */
    rc = hci_h4_sm_rx(&h4sm, data, 3 + 255);
    TEST_ASSERT(rc == 3 + 255);
    TEST_ASSERT(ble_hci_h4_test_evt_allocated == 0);
    TEST_ASSERT(ble_hci_h4_test_num_frames == 0);

    /* Parser shall be in sync for following event */
    rc = hci_h4_sm_rx(&h4sm, &data[3 + 255], sizeof(valid_evt));
    TEST_ASSERT(rc == sizeof(valid_evt));
    TEST_ASSERT(ble_hci_h4_test_evt_allocated == 1);
    TEST_ASSERT(ble_hci_h4_test_num_frames == 1);
    TEST_ASSERT(ble_hci_h4_test_frame_type == HCI_H4_EVT);
    TEST_ASSERT(memcmp(ble_hci_h4_test_evt_buf, &valid_evt[1],
                       sizeof(valid_evt) - 1) == 0);
}

TEST_SUITE(ble_hci_h4_test_suite)
{
    ble_hci_h4_test_evt_too_long();
}
