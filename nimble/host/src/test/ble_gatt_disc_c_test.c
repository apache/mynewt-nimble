/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <errno.h>
#include "testutil/testutil.h"
#include "nimble/ble.h"
#include "host/ble_hs_test.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_uuid.h"
#include "ble_hs_priv.h"
#include "ble_att_cmd.h"
#include "ble_gatt_priv.h"
#include "ble_hs_conn.h"
#include "ble_hs_test_util.h"

struct ble_gatt_disc_c_test_char {
    uint16_t decl_handle;
    uint16_t value_handle;
    uint16_t uuid16; /* 0 if not present. */
    uint8_t properties;
    uint8_t uuid128[16];
};

#define BLE_GATT_DISC_C_TEST_MAX_CHARS  256
static struct ble_gatt_chr
    ble_gatt_disc_c_test_chars[BLE_GATT_DISC_C_TEST_MAX_CHARS];
static int ble_gatt_disc_c_test_num_chars;
static int ble_gatt_disc_c_test_rx_complete;

static void
ble_gatt_disc_c_test_init(void)
{
    ble_hs_test_util_init();

    ble_gatt_disc_c_test_num_chars = 0;
    ble_gatt_disc_c_test_rx_complete = 0;
}

static int
ble_gatt_disc_c_test_misc_rx_all_rsp_once(
    struct ble_hs_conn *conn, struct ble_gatt_disc_c_test_char *chars)
{
    struct ble_att_read_type_rsp rsp;
    struct ble_l2cap_chan *chan;
    uint8_t buf[1024];
    int off;
    int rc;
    int i;

    /* Send the pending ATT Read By Type Request. */
    ble_gatt_wakeup();
    ble_hs_process_tx_data_queue();

    if (chars[0].uuid16 != 0) {
       rsp.batp_length = BLE_ATT_READ_TYPE_ADATA_BASE_SZ +
                         BLE_GATT_CHR_DECL_SZ_16;
    } else {
       rsp.batp_length = BLE_ATT_READ_TYPE_ADATA_BASE_SZ +
                         BLE_GATT_CHR_DECL_SZ_128;
    }

    rc = ble_att_read_type_rsp_write(buf, BLE_ATT_READ_TYPE_RSP_BASE_SZ, &rsp);
    TEST_ASSERT_FATAL(rc == 0);

    off = BLE_ATT_READ_TYPE_RSP_BASE_SZ;
    for (i = 0; ; i++) {
        if (chars[i].decl_handle == 0) {
            /* No more services. */
            break;
        }

        /* If the value length is changing, we need a separate response. */
        if (((chars[i].uuid16 == 0) ^ (chars[0].uuid16 == 0)) != 0) {
            break;
        }

        htole16(buf + off, chars[i].decl_handle);
        off += 2;

        buf[off] = chars[i].properties;
        off++;

        htole16(buf + off, chars[i].value_handle);
        off += 2;

        if (chars[i].uuid16 != 0) {
            htole16(buf + off, chars[i].uuid16);
            off += 2;
        } else {
            memcpy(buf + off, chars[i].uuid128, 16);
            off += 16;
        }
    }

    chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    TEST_ASSERT_FATAL(chan != NULL);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn, chan, buf, off);
    TEST_ASSERT(rc == 0);

    return i;
}

static void
ble_gatt_disc_c_test_misc_rx_all_rsp(struct ble_hs_conn *conn,
                                     uint16_t end_handle,
                                     struct ble_gatt_disc_c_test_char *chars)
{
    int count;
    int idx;

    idx = 0;
    while (chars[idx].decl_handle != 0) {
        count = ble_gatt_disc_c_test_misc_rx_all_rsp_once(conn, chars + idx);
        if (count == 0) {
            break;
        }
        idx += count;
    }

    if (chars[idx - 1].decl_handle != end_handle) {
        /* Send the pending ATT Request. */
        ble_gatt_wakeup();
        ble_hs_test_util_rx_att_err_rsp(conn, BLE_ATT_OP_READ_TYPE_REQ,
                                        BLE_ATT_ERR_ATTR_NOT_FOUND);
    }
}

static void
ble_gatt_disc_c_test_misc_verify_chars(struct ble_gatt_disc_c_test_char *chars)
{
    uint16_t uuid16;
    int i;

    for (i = 0; chars[i].decl_handle != 0; i++) {
        TEST_ASSERT(chars[i].decl_handle ==
                    ble_gatt_disc_c_test_chars[i].decl_handle);
        TEST_ASSERT(chars[i].value_handle ==
                    ble_gatt_disc_c_test_chars[i].value_handle);
        if (chars[i].uuid16 != 0) {
            uuid16 = ble_hs_uuid_16bit(ble_gatt_disc_c_test_chars[i].uuid128);
            TEST_ASSERT(chars[i].uuid16 == uuid16);
        } else {
            TEST_ASSERT(memcmp(chars[i].uuid128,
                               ble_gatt_disc_c_test_chars[i].uuid128,
                               16) == 0);
        }
    }

    TEST_ASSERT(i == ble_gatt_disc_c_test_num_chars);
    TEST_ASSERT(ble_gatt_disc_c_test_rx_complete);
}

static int
ble_gatt_disc_c_test_misc_cb(uint16_t conn_handle, uint8_t ble_hs_status,
                             uint8_t att_status, struct ble_gatt_chr *chr,
                             void *arg)
{
    struct ble_gatt_chr *dst;

    TEST_ASSERT(ble_hs_status == 0 && att_status == 0);
    TEST_ASSERT(!ble_gatt_disc_c_test_rx_complete);

    if (chr == NULL) {
        ble_gatt_disc_c_test_rx_complete = 1;
    } else {
        TEST_ASSERT_FATAL(ble_gatt_disc_c_test_num_chars <
                          BLE_GATT_DISC_C_TEST_MAX_CHARS);

        dst = ble_gatt_disc_c_test_chars + ble_gatt_disc_c_test_num_chars++;
        *dst = *chr;
    }

    return 0;
}

static void
ble_gatt_disc_c_test_misc_all(uint16_t start_handle, uint16_t end_handle,
                              struct ble_gatt_disc_c_test_char *chars)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_gatt_disc_c_test_init();

    conn = ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}));

    rc = ble_gatt_disc_all_chars(2, start_handle, end_handle,
                                 ble_gatt_disc_c_test_misc_cb, NULL);
    TEST_ASSERT(rc == 0);

    ble_gatt_disc_c_test_misc_rx_all_rsp(conn, end_handle, chars);
    ble_gatt_disc_c_test_misc_verify_chars(chars);
}

TEST_CASE(ble_gatt_disc_c_test_disc_all)
{
    /*** One 16-bit characteristic. */
    ble_gatt_disc_c_test_misc_all(50, 100,
                                  (struct ble_gatt_disc_c_test_char[]) {
        {
            .decl_handle = 55,
            .value_handle = 56,
            .uuid16 = 0x2010,
        }, { 0 }
    });

    /*** Two 16-bit characteristics. */
    ble_gatt_disc_c_test_misc_all(50, 100,
                                  (struct ble_gatt_disc_c_test_char[]) {
        {
            .decl_handle = 55,
            .value_handle = 56,
            .uuid16 = 0x2010,
        }, {
            .decl_handle = 57,
            .value_handle = 58,
            .uuid16 = 0x64ba,
        }, { 0 }
    });

#if 0
    /*** Five 16-bit characteristics. */
    ble_gatt_disc_c_test_misc_all(50, 100,
                                  (struct ble_gatt_disc_c_test_char[]) {
        { 55, (uint8_t[]) { 0x10, 0x20 }, 2 },
        { 56, (uint8_t[]) { 0x32, 0x55 }, 2 },
        { 58, (uint8_t[]) { 0xfa, 0xc4 }, 2 },
        { 63, (uint8_t[]) { 0x43, 0x2e }, 2 },
        { 77, (uint8_t[]) { 0x83, 0x36 }, 2 },
        { 0 }
    });

    /*** Interleaved 16-bit and 128-bit characteristics. */
    ble_gatt_disc_c_test_misc_all(50, 100,
                                  (struct ble_gatt_disc_c_test_char[]) {
        { 55, (uint8_t[]) { 0x10, 0x20 }, 2 },
        { 56, (uint8_t[]) { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 }, 16 },
        { 58, (uint8_t[]) { 0xfa, 0xc4 }, 2 },
        { 63, (uint8_t[]) { 5,2,7,5,4,8,7,6,2,2,40,64,85,62,50,49 }, 16 },
        { 77, (uint8_t[]) { 0x83, 0x36 }, 2 },
        { 0 }
    });

    /*** Ends with final handle ID. */
    ble_gatt_disc_c_test_misc_all(50, 100,
                                  (struct ble_gatt_disc_c_test_char[]) {
        { 55, (uint8_t[]) { 0x10, 0x20 }, 2 },
        { 100, (uint8_t[]) { 0x32, 0x55 }, 2 },
        { 0 }
    });
#endif
}

TEST_SUITE(gle_gatt_disc_c_test_suite)
{
    ble_gatt_disc_c_test_disc_all();
}

int
ble_gatt_disc_c_test_all(void)
{
    gle_gatt_disc_c_test_suite();

    return tu_any_failed;
}
