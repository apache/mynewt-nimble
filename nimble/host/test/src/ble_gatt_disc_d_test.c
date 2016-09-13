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

#include <string.h>
#include <errno.h>
#include <limits.h>
#include "testutil/testutil.h"
#include "nimble/ble.h"
#include "host/ble_hs_test.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "ble_hs_test_util.h"

struct ble_gatt_disc_d_test_dsc {
    uint16_t chr_val_handle; /* 0 if last entry. */
    uint16_t dsc_handle;
    uint8_t dsc_uuid128[16];
};

#define BLE_GATT_DISC_D_TEST_MAX_DSCS  256
static struct ble_gatt_disc_d_test_dsc
    ble_gatt_disc_d_test_dscs[BLE_GATT_DISC_D_TEST_MAX_DSCS];
static int ble_gatt_disc_d_test_num_dscs;
static int ble_gatt_disc_d_test_rx_complete;

static void
ble_gatt_disc_d_test_init(void)
{
    ble_hs_test_util_init();

    ble_gatt_disc_d_test_num_dscs = 0;
    ble_gatt_disc_d_test_rx_complete = 0;
}

static int
ble_gatt_disc_d_test_misc_rx_rsp_once(
    uint16_t conn_handle, struct ble_gatt_disc_d_test_dsc *dscs)
{
    struct ble_att_find_info_rsp rsp;
    uint8_t buf[1024];
    uint16_t uuid16_cur;
    uint16_t uuid16_0;
    int off;
    int rc;
    int i;

    /* Send the pending ATT Read By Type Request. */
    ble_hs_test_util_tx_all();

    uuid16_0 = ble_uuid_128_to_16(dscs[0].dsc_uuid128);
    if (uuid16_0 != 0) {
        rsp.bafp_format = BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT;
    } else {
        rsp.bafp_format = BLE_ATT_FIND_INFO_RSP_FORMAT_128BIT;
    }

    ble_att_find_info_rsp_write(buf, BLE_ATT_FIND_INFO_RSP_BASE_SZ, &rsp);

    off = BLE_ATT_FIND_INFO_RSP_BASE_SZ;
    for (i = 0; ; i++) {
        if (dscs[i].chr_val_handle == 0) {
            /* No more descriptors. */
            break;
        }

        /* If the value length is changing, we need a separate response. */
        uuid16_cur = ble_uuid_128_to_16(dscs[i].dsc_uuid128);
        if (((uuid16_0 == 0) ^ (uuid16_cur == 0)) != 0) {
            break;
        }

        htole16(buf + off, dscs[i].dsc_handle);
        off += 2;

        if (uuid16_cur != 0) {
            htole16(buf + off, uuid16_cur);
            off += 2;
        } else {
            memcpy(buf + off, dscs[i].dsc_uuid128, 16);
            off += 16;
        }
    }

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, off);
    TEST_ASSERT(rc == 0);

    return i;
}

static void
ble_gatt_disc_d_test_misc_rx_rsp(uint16_t conn_handle,
                                 uint16_t end_handle,
                                 struct ble_gatt_disc_d_test_dsc *dscs)
{
    int count;
    int idx;

    idx = 0;
    while (dscs[idx].chr_val_handle != 0) {
        count = ble_gatt_disc_d_test_misc_rx_rsp_once(conn_handle, dscs + idx);
        if (count == 0) {
            break;
        }
        idx += count;
    }

    if (dscs[idx - 1].dsc_handle != end_handle) {
        /* Send the pending ATT Request. */
        ble_hs_test_util_tx_all();
        ble_hs_test_util_rx_att_err_rsp(conn_handle, BLE_ATT_OP_FIND_INFO_REQ,
                                        BLE_ATT_ERR_ATTR_NOT_FOUND,
                                        end_handle);
    }
}

static void
ble_gatt_disc_d_test_misc_verify_dscs(struct ble_gatt_disc_d_test_dsc *dscs,
                                      int stop_after)
{
    int i;

    if (stop_after == 0) {
        stop_after = INT_MAX;
    }

    for (i = 0; i < stop_after && dscs[i].chr_val_handle != 0; i++) {
        TEST_ASSERT(dscs[i].chr_val_handle ==
                    ble_gatt_disc_d_test_dscs[i].chr_val_handle);
        TEST_ASSERT(dscs[i].dsc_handle ==
                    ble_gatt_disc_d_test_dscs[i].dsc_handle);
        TEST_ASSERT(memcmp(dscs[i].dsc_uuid128,
                           ble_gatt_disc_d_test_dscs[i].dsc_uuid128,
                           16) == 0);
    }

    TEST_ASSERT(i == ble_gatt_disc_d_test_num_dscs);
    TEST_ASSERT(ble_gatt_disc_d_test_rx_complete);
}

static int
ble_gatt_disc_d_test_misc_cb(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             uint16_t chr_val_handle,
                             const struct ble_gatt_dsc *dsc,
                             void *arg)
{
    struct ble_gatt_disc_d_test_dsc *dst;
    int *stop_after;

    TEST_ASSERT(error != NULL);
    TEST_ASSERT(!ble_gatt_disc_d_test_rx_complete);

    stop_after = arg;

    switch (error->status) {
    case 0:
        TEST_ASSERT_FATAL(ble_gatt_disc_d_test_num_dscs <
                          BLE_GATT_DISC_D_TEST_MAX_DSCS);

        dst = ble_gatt_disc_d_test_dscs + ble_gatt_disc_d_test_num_dscs++;
        dst->chr_val_handle = chr_val_handle;
        dst->dsc_handle = dsc->handle;
        memcpy(dst->dsc_uuid128, dsc->uuid128, 16);
        break;

    case BLE_HS_EDONE:
        ble_gatt_disc_d_test_rx_complete = 1;
        break;

    default:
        TEST_ASSERT(0);
        break;
    }

    if (*stop_after > 0) {
        (*stop_after)--;
        if (*stop_after == 0) {
            ble_gatt_disc_d_test_rx_complete = 1;
            return 1;
        }
    }

    return 0;
}

static void
ble_gatt_disc_d_test_misc_all(uint16_t chr_val_handle, uint16_t end_handle,
                              int stop_after,
                              struct ble_gatt_disc_d_test_dsc *dscs)
{
    int num_left;
    int rc;

    ble_gatt_disc_d_test_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);

    num_left = stop_after;
    rc = ble_gattc_disc_all_dscs(2, chr_val_handle, end_handle,
                                 ble_gatt_disc_d_test_misc_cb, &num_left);
    TEST_ASSERT(rc == 0);

    ble_gatt_disc_d_test_misc_rx_rsp(2, end_handle, dscs);
    ble_gatt_disc_d_test_misc_verify_dscs(dscs, stop_after);
}

TEST_CASE(ble_gatt_disc_d_test_1)
{
    /*** One 16-bit descriptor. */
    ble_gatt_disc_d_test_misc_all(5, 10, 0,
        ((struct ble_gatt_disc_d_test_dsc[]) { {
            .chr_val_handle = 5,
            .dsc_handle = 6,
            .dsc_uuid128 = BLE_UUID16_ARR(0x1234),
        }, {
            0
        } })
    );

    /*** Two 16-bit descriptors. */
    ble_gatt_disc_d_test_misc_all(50, 100, 0,
        ((struct ble_gatt_disc_d_test_dsc[]) { {
            .chr_val_handle = 50,
            .dsc_handle = 51,
            .dsc_uuid128 = BLE_UUID16_ARR(0x1111),
        }, {
            .chr_val_handle = 50,
            .dsc_handle = 52,
            .dsc_uuid128 = BLE_UUID16_ARR(0x2222),
        }, {
            0
        } })
    );

    /*** Five 16-bit descriptors. */
    ble_gatt_disc_d_test_misc_all(50, 100, 0,
        ((struct ble_gatt_disc_d_test_dsc[]) { {
            .chr_val_handle = 50,
            .dsc_handle = 51,
            .dsc_uuid128 = BLE_UUID16_ARR(0x1111),
        }, {
            .chr_val_handle = 50,
            .dsc_handle = 52,
            .dsc_uuid128 = BLE_UUID16_ARR(0x2222),
        }, {
            .chr_val_handle = 50,
            .dsc_handle = 53,
            .dsc_uuid128 = BLE_UUID16_ARR(0x3333),
        }, {
            .chr_val_handle = 50,
            .dsc_handle = 54,
            .dsc_uuid128 = BLE_UUID16_ARR(0x4444),
        }, {
            .chr_val_handle = 50,
            .dsc_handle = 55,
            .dsc_uuid128 = BLE_UUID16_ARR(0x5555),
        }, {
            0
        } })
    );

    /*** Interleaved 16-bit and 128-bit descriptors. */
    ble_gatt_disc_d_test_misc_all(50, 100, 0,
        ((struct ble_gatt_disc_d_test_dsc[]) { {
            .chr_val_handle = 50,
            .dsc_handle = 51,
            .dsc_uuid128 = BLE_UUID16_ARR(0x1111),
        }, {
            .chr_val_handle = 50,
            .dsc_handle = 52,
            .dsc_uuid128 = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 },
        }, {
            .chr_val_handle = 50,
            .dsc_handle = 53,
            .dsc_uuid128 = BLE_UUID16_ARR(0x3333),
        }, {
            .chr_val_handle = 50,
            .dsc_handle = 54,
            .dsc_uuid128 = { 1,0,4,0,6,9,17,7,8,43,7,4,12,43,19,35 },
        }, {
            .chr_val_handle = 50,
            .dsc_handle = 55,
            .dsc_uuid128 = BLE_UUID16_ARR(0x5555),
        }, {
            0
        } })
    );

    /*** Ends with final handle ID. */
    ble_gatt_disc_d_test_misc_all(50, 52, 0,
        ((struct ble_gatt_disc_d_test_dsc[]) { {
            .chr_val_handle = 50,
            .dsc_handle = 51,
            .dsc_uuid128 = BLE_UUID16_ARR(0x1111),
        }, {
            .chr_val_handle = 50,
            .dsc_handle = 52,
            .dsc_uuid128 = BLE_UUID16_ARR(0x2222),
        }, {
            0
        } })
    );

    /*** Stop after two descriptors. */
    ble_gatt_disc_d_test_misc_all(50, 100, 2,
        ((struct ble_gatt_disc_d_test_dsc[]) { {
            .chr_val_handle = 50,
            .dsc_handle = 51,
            .dsc_uuid128 = BLE_UUID16_ARR(0x1111),
        }, {
            .chr_val_handle = 50,
            .dsc_handle = 52,
            .dsc_uuid128 = BLE_UUID16_ARR(0x2222),
        }, {
            .chr_val_handle = 50,
            .dsc_handle = 53,
            .dsc_uuid128 = BLE_UUID16_ARR(0x3333),
        }, {
            .chr_val_handle = 50,
            .dsc_handle = 54,
            .dsc_uuid128 = BLE_UUID16_ARR(0x4444),
        }, {
            .chr_val_handle = 50,
            .dsc_handle = 55,
            .dsc_uuid128 = BLE_UUID16_ARR(0x5555),
        }, {
            0
        } })
    );
}

TEST_SUITE(ble_gatt_disc_d_test_suite)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_gatt_disc_d_test_1();
}

int
ble_gatt_disc_d_test_all(void)
{
    ble_gatt_disc_d_test_suite();

    return tu_any_failed;
}
