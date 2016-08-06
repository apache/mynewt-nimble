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

#include <stddef.h>
#include <errno.h>
#include <string.h>
#include "testutil/testutil.h"
#include "nimble/hci_common.h"
#include "host/ble_hs_test.h"
#include "host/ble_uuid.h"
#include "ble_hs_test_util.h"

static uint8_t *ble_att_svr_test_attr_r_1;
static uint16_t ble_att_svr_test_attr_r_1_len;
static uint8_t *ble_att_svr_test_attr_r_2;
static uint16_t ble_att_svr_test_attr_r_2_len;

static uint8_t ble_att_svr_test_attr_w_1[1024];
static uint16_t ble_att_svr_test_attr_w_1_len;
static uint8_t ble_att_svr_test_attr_w_2[1024];
static uint16_t ble_att_svr_test_attr_w_2_len;

static uint16_t ble_att_svr_test_n_conn_handle;
static uint16_t ble_att_svr_test_n_attr_handle;
static uint8_t ble_att_svr_test_attr_n[1024];
static uint16_t ble_att_svr_test_attr_n_len;

static int
ble_att_svr_test_misc_gap_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_NOTIFY_RX:
        ble_att_svr_test_n_conn_handle = event->notify_rx.conn_handle;
        ble_att_svr_test_n_attr_handle = event->notify_rx.attr_handle;
        TEST_ASSERT_FATAL(OS_MBUF_PKTLEN(event->notify_rx.om) <=
                          sizeof ble_att_svr_test_attr_n);
        ble_att_svr_test_attr_n_len = OS_MBUF_PKTLEN(event->notify_rx.om);
        os_mbuf_copydata(event->notify_rx.om, 0, ble_att_svr_test_attr_n_len,
                         ble_att_svr_test_attr_n);
        break;

    default:
        break;
    }

    return 0;
}

/**
 * @return                      The handle of the new test connection.
 */
static uint16_t
ble_att_svr_test_misc_init(uint16_t mtu)
{
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_test_util_init();

    ble_hs_test_util_create_conn(2, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 ble_att_svr_test_misc_gap_cb, NULL);

    ble_hs_lock();

    rc = ble_hs_misc_conn_chan_find(2, BLE_L2CAP_CID_ATT, &conn, &chan);
    TEST_ASSERT_FATAL(rc == 0);

    if (mtu != 0) {
        chan->blc_my_mtu = mtu;
        chan->blc_peer_mtu = mtu;
        chan->blc_flags |= BLE_L2CAP_CHAN_F_TXED_MTU;
    }

    ble_hs_unlock();

    ble_att_svr_test_attr_r_1_len = 0;
    ble_att_svr_test_attr_r_2_len = 0;
    ble_att_svr_test_attr_w_1_len = 0;

    return 2;
}

static int
ble_att_svr_test_misc_attr_fn_r_1(uint16_t conn_handle, uint16_t attr_handle,
                                  uint8_t op, uint16_t offset,
                                  struct os_mbuf **om, void *arg)
{
    switch (op) {
    case BLE_ATT_ACCESS_OP_READ:
        if (offset > ble_att_svr_test_attr_r_1_len) {
            return BLE_ATT_ERR_INVALID_OFFSET;
        }

        os_mbuf_append(*om, ble_att_svr_test_attr_r_1 + offset,
                       ble_att_svr_test_attr_r_1_len - offset);
        return 0;

    default:
        return -1;
    }
}

static int
ble_att_svr_test_misc_attr_fn_r_2(uint16_t conn_handle, uint16_t attr_handle,
                                  uint8_t op, uint16_t offset,
                                  struct os_mbuf **om, void *arg)
{

    switch (op) {
    case BLE_ATT_ACCESS_OP_READ:
        if (offset > ble_att_svr_test_attr_r_2_len) {
            return BLE_ATT_ERR_INVALID_OFFSET;
        }

        os_mbuf_append(*om, ble_att_svr_test_attr_r_2 + offset,
                       ble_att_svr_test_attr_r_2_len - offset);
        return 0;

    default:
        return -1;
    }
}

#define BLE_ATT_SVR_TEST_LAST_SVC  11
#define BLE_ATT_SVR_TEST_LAST_ATTR 24

static int
ble_att_svr_test_misc_attr_fn_r_group(uint16_t conn_handle,
                                      uint16_t attr_handle,
                                      uint8_t op,
                                      uint16_t offset,
                                      struct os_mbuf **om,
                                      void *arg)
{
    uint8_t *src;
    int rc;

    /* Service 0x1122 from 1 to 5 */
    /* Service 0x2233 from 6 to 10 */
    /* Service 010203...0f from 11 to 24 */

    static uint8_t vals[25][16] = {
        [1] =   { 0x22, 0x11 },
        [2] =   { 0x01, 0x11 },
        [3] =   { 0x02, 0x11 },
        [4] =   { 0x03, 0x11 },
        [5] =   { 0x04, 0x11 },
        [6] =   { 0x33, 0x22 },
        [7] =   { 0x01, 0x22 },
        [8] =   { 0x02, 0x22 },
        [9] =   { 0x03, 0x22 },
        [10] =  { 0x04, 0x22 },
        [11] =  { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 },
        [12] =  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
        [13] =  { 0xdd, 0xdd },
        [14] =  { 0x55, 0x55 },
        [15] =  { 0xdd, 0xdd },
        [16] =  { 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2 },
        [17] =  { 0xdd, 0xdd },
        [18] =  { 0x66, 0x66 },
        [19] =  { 0xdd, 0xdd },
        [20] =  { 0x77, 0x77 },
        [21] =  { 0xdd, 0xdd },
        [22] =  { 0x88, 0x88 },
        [23] =  { 0xdd, 0xdd },
        [24] =  { 0x99, 0x99 },
    };

    static uint8_t zeros[14];

    if (op != BLE_ATT_ACCESS_OP_READ) {
        return -1;
    }

    TEST_ASSERT_FATAL(attr_handle >= 1 &&
                      attr_handle <= BLE_ATT_SVR_TEST_LAST_ATTR);

    src = &vals[attr_handle][0];
    if (memcmp(src + 2, zeros, 14) == 0) {
        rc = os_mbuf_append(*om, src, 2);
    } else {
        rc = os_mbuf_append(*om, src, 16);
    }
    if (rc != 0) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return 0;
}

static void
ble_att_svr_test_misc_register_uuid128(uint8_t *uuid128, uint8_t flags,
                                       uint16_t expected_handle,
                                       ble_att_svr_access_fn *fn)
{
    uint16_t handle;
    int rc;

    rc = ble_att_svr_register(uuid128, flags, &handle, fn, NULL);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT_FATAL(handle == expected_handle);
}

static void
ble_att_svr_test_misc_register_uuid16(uint16_t uuid16, uint8_t flags,
                                      uint16_t expected_handle,
                                      ble_att_svr_access_fn *fn)
{
    uint8_t uuid128[16];
    int rc;

    rc = ble_uuid_16_to_128(uuid16, uuid128);
    TEST_ASSERT_FATAL(rc == 0);

    ble_att_svr_test_misc_register_uuid128(uuid128, flags, expected_handle,
                                           fn);
}

static void
ble_att_svr_test_misc_register_group_attrs(void)
{
    /* Service 0x1122 from 1 to 5 */
    /* Service 0x2233 from 6 to 10 */
    /* Service 010203...0f from 11 to 24 */

    int i;

    /* Service 0x1122 from 1 to 5 */
    ble_att_svr_test_misc_register_uuid16(
        BLE_ATT_UUID_PRIMARY_SERVICE, HA_FLAG_PERM_RW, 1,
        ble_att_svr_test_misc_attr_fn_r_group);
    for (i = 2; i <= 5; i++) {
        if ((i - 2) % 2 == 0) {
            ble_att_svr_test_misc_register_uuid16(
                BLE_ATT_UUID_CHARACTERISTIC, HA_FLAG_PERM_RW, i,
                ble_att_svr_test_misc_attr_fn_r_group);
        } else {
            ble_att_svr_test_misc_register_uuid16(
                i, HA_FLAG_PERM_RW, i,
                ble_att_svr_test_misc_attr_fn_r_group);
        }
    }

    /* Service 0x2233 from 6 to 10 */
    ble_att_svr_test_misc_register_uuid16(
        BLE_ATT_UUID_PRIMARY_SERVICE, HA_FLAG_PERM_RW, 6,
        ble_att_svr_test_misc_attr_fn_r_group);
    for (i = 7; i <= 10; i++) {
        ble_att_svr_test_misc_register_uuid16(
            BLE_ATT_UUID_INCLUDE, HA_FLAG_PERM_RW, i,
            ble_att_svr_test_misc_attr_fn_r_group);
    }

    /* Service 010203...0f from 11 to 24 */
    ble_att_svr_test_misc_register_uuid16(
        BLE_ATT_UUID_PRIMARY_SERVICE, HA_FLAG_PERM_RW, 11,
        ble_att_svr_test_misc_attr_fn_r_group);
    for (i = 12; i <= 24; i++) {
        if ((i - 12) % 2 == 0) {
            ble_att_svr_test_misc_register_uuid16(
                BLE_ATT_UUID_CHARACTERISTIC, HA_FLAG_PERM_RW, i,
                ble_att_svr_test_misc_attr_fn_r_group);
        } else {
            ble_att_svr_test_misc_register_uuid16(
                i, HA_FLAG_PERM_RW, i,
                ble_att_svr_test_misc_attr_fn_r_group);
        }
    }
}

static int
ble_att_svr_test_misc_attr_fn_w_1(uint16_t conn_handle, uint16_t attr_handle,
                                  uint8_t op, uint16_t offset,
                                  struct os_mbuf **om, void *arg)
{
    switch (op) {
    case BLE_ATT_ACCESS_OP_WRITE:
        os_mbuf_copydata(*om, 0, OS_MBUF_PKTLEN(*om),
                         ble_att_svr_test_attr_w_1);
        ble_att_svr_test_attr_w_1_len = OS_MBUF_PKTLEN(*om);
        return 0;

    default:
        return -1;
    }
}

static int
ble_att_svr_test_misc_attr_fn_w_2(uint16_t conn_handle, uint16_t attr_handle,
                                  uint8_t op, uint16_t offset,
                                  struct os_mbuf **om, void *arg)
{
    switch (op) {
    case BLE_ATT_ACCESS_OP_WRITE:
        os_mbuf_copydata(*om, 0, OS_MBUF_PKTLEN(*om),
                         ble_att_svr_test_attr_w_2);
        ble_att_svr_test_attr_w_2_len = OS_MBUF_PKTLEN(*om);
        return 0;

    default:
        return -1;
    }
}

static void
ble_att_svr_test_misc_verify_w_1(void *data, int data_len)
{
    TEST_ASSERT(ble_att_svr_test_attr_w_1_len == data_len);
    TEST_ASSERT(memcmp(ble_att_svr_test_attr_w_1, data, data_len) == 0);
}

static void
ble_att_svr_test_misc_verify_w_2(void *data, int data_len)
{
    TEST_ASSERT(ble_att_svr_test_attr_w_2_len == data_len);
    TEST_ASSERT(memcmp(ble_att_svr_test_attr_w_2, data, data_len) == 0);
}

static void
ble_att_svr_test_misc_verify_tx_err_rsp(uint8_t req_op, uint16_t handle,
                                        uint8_t error_code)
{
    struct ble_att_error_rsp rsp;
    struct os_mbuf *om;
    uint8_t buf[BLE_ATT_ERROR_RSP_SZ];
    int rc;

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue();

    rc = os_mbuf_copydata(om, 0, sizeof buf, buf);
    TEST_ASSERT(rc == 0);

    ble_att_error_rsp_parse(buf, sizeof buf, &rsp);

    TEST_ASSERT(rsp.baep_req_op == req_op);
    TEST_ASSERT(rsp.baep_handle == handle);
    TEST_ASSERT(rsp.baep_error_code == error_code);
}

static void
ble_att_svr_test_misc_verify_tx_read_blob_rsp(uint8_t *attr_data, int attr_len)
{
    struct os_mbuf *om;
    uint8_t u8;
    int rc;
    int i;

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue();

    rc = os_mbuf_copydata(om, 0, 1, &u8);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(u8 == BLE_ATT_OP_READ_BLOB_RSP);

    for (i = 0; i < attr_len; i++) {
        rc = os_mbuf_copydata(om, i + 1, 1, &u8);
        TEST_ASSERT(rc == 0);
        TEST_ASSERT(u8 == attr_data[i]);
    }

    rc = os_mbuf_copydata(om, i + 1, 1, &u8);
    TEST_ASSERT(rc != 0);
}

static void
ble_att_svr_test_misc_rx_read_mult_req(uint16_t conn_handle,
                                       uint16_t *handles, int num_handles,
                                       int success)
{
    uint8_t buf[256];
    int off;
    int rc;
    int i;

    ble_att_read_mult_req_write(buf, sizeof buf);

    off = BLE_ATT_READ_MULT_REQ_BASE_SZ;
    for (i = 0; i < num_handles; i++) {
        htole16(buf + off, handles[i]);
        off += 2;
    }

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, off);
    if (success) {
        TEST_ASSERT(rc == 0);
    } else {
        TEST_ASSERT(rc != 0);
    }
}

static void
ble_att_svr_test_misc_verify_tx_read_mult_rsp(
    uint16_t conn_handle, struct ble_hs_test_util_flat_attr *attrs,
    int num_attrs)
{
    struct ble_l2cap_chan *chan;
    struct os_mbuf *om;
    uint16_t attr_len;
    uint16_t mtu;
    uint8_t u8;
    int rc;
    int off;
    int i;

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue();

    rc = os_mbuf_copydata(om, 0, 1, &u8);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(u8 == BLE_ATT_OP_READ_MULT_RSP);

    ble_hs_lock();

    rc = ble_hs_misc_conn_chan_find(conn_handle, BLE_L2CAP_CID_ATT,
                                    NULL, &chan);
    TEST_ASSERT_FATAL(rc == 0);
    mtu = ble_l2cap_chan_mtu(chan);

    ble_hs_unlock();

    off = 1;
    for (i = 0; i < num_attrs; i++) {
        attr_len = min(attrs[i].value_len, mtu - off);

        rc = os_mbuf_cmpf(om, off, attrs[i].value, attr_len);
        TEST_ASSERT(rc == 0);

        off += attr_len;
    }

    TEST_ASSERT(OS_MBUF_PKTLEN(om) == off);
}

static void
ble_att_svr_test_misc_verify_all_read_mult(
    uint16_t conn_handle, struct ble_hs_test_util_flat_attr *attrs,
    int num_attrs)
{
    uint16_t handles[256];
    int i;

    TEST_ASSERT_FATAL(num_attrs <= sizeof handles / sizeof handles[0]);

    for (i = 0; i < num_attrs; i++) {
        handles[i] = attrs[i].handle;
    }

    ble_att_svr_test_misc_rx_read_mult_req(conn_handle, handles, num_attrs, 1);
    ble_att_svr_test_misc_verify_tx_read_mult_rsp(conn_handle,
                                                  attrs, num_attrs);
}


static void
ble_att_svr_test_misc_verify_tx_write_rsp(void)
{
    struct os_mbuf *om;
    uint8_t u8;
    int rc;

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue();

    rc = os_mbuf_copydata(om, 0, 1, &u8);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(u8 == BLE_ATT_OP_WRITE_RSP);
}

static void
ble_att_svr_test_misc_verify_tx_mtu_rsp(uint16_t conn_handle)
{
    struct ble_att_mtu_cmd rsp;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    struct os_mbuf *om;
    uint8_t buf[BLE_ATT_MTU_CMD_SZ];
    int rc;

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue();

    rc = os_mbuf_copydata(om, 0, sizeof buf, buf);
    TEST_ASSERT(rc == 0);

    ble_att_mtu_cmd_parse(buf, sizeof buf, &rsp);

    ble_hs_lock();
    rc = ble_hs_misc_conn_chan_find(conn_handle, BLE_L2CAP_CID_ATT,
                                    &conn, &chan);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(rsp.bamc_mtu == chan->blc_my_mtu);
    ble_hs_unlock();
}

struct ble_att_svr_test_info_entry {
    uint16_t handle;        /* 0 on last entry */
    uint16_t uuid16;        /* 0 if not present. */
    uint8_t uuid128[16];
};

static void
ble_att_svr_test_misc_verify_tx_find_info_rsp(
    struct ble_att_svr_test_info_entry *entries)
{
    struct ble_att_svr_test_info_entry *entry;
    struct ble_att_find_info_rsp rsp;
    struct os_mbuf *om;
    uint16_t handle;
    uint16_t uuid16;
    uint8_t buf[BLE_ATT_FIND_INFO_RSP_BASE_SZ];
    uint8_t uuid128[16];
    int off;
    int rc;

    ble_hs_test_util_tx_all();

    off = 0;

    om = ble_hs_test_util_prev_tx_dequeue_pullup();

    rc = os_mbuf_copydata(om, off, sizeof buf, buf);
    TEST_ASSERT(rc == 0);
    off += sizeof buf;

    ble_att_find_info_rsp_parse(buf, sizeof buf, &rsp);

    for (entry = entries; entry->handle != 0; entry++) {
        rc = os_mbuf_copydata(om, off, 2, &handle);
        TEST_ASSERT(rc == 0);
        off += 2;

        handle = le16toh((void *)&handle);
        TEST_ASSERT(handle == entry->handle);

        if (entry->uuid16 != 0) {
            TEST_ASSERT(rsp.bafp_format ==
                        BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT);
            rc = os_mbuf_copydata(om, off, 2, &uuid16);
            TEST_ASSERT(rc == 0);
            off += 2;

            uuid16 = le16toh((void *)&uuid16);
            TEST_ASSERT(uuid16 == entry->uuid16);
        } else {
            TEST_ASSERT(rsp.bafp_format ==
                        BLE_ATT_FIND_INFO_RSP_FORMAT_128BIT);
            rc = os_mbuf_copydata(om, off, 16, uuid128);
            TEST_ASSERT(rc == 0);
            off += 16;

            TEST_ASSERT(memcmp(uuid128, entry->uuid128, 16) == 0);
        }
    }

    /* Ensure there is no extra data in the response. */
    TEST_ASSERT(off == OS_MBUF_PKTHDR(om)->omp_len);
}

struct ble_att_svr_test_type_value_entry {
    uint16_t first;        /* 0 on last entry */
    uint16_t last;
};

static void
ble_att_svr_test_misc_verify_tx_find_type_value_rsp(
    struct ble_att_svr_test_type_value_entry *entries)
{
    struct ble_att_svr_test_type_value_entry *entry;
    struct os_mbuf *om;
    uint16_t u16;
    uint8_t op;
    int off;
    int rc;

    ble_hs_test_util_tx_all();

    off = 0;

    om = ble_hs_test_util_prev_tx_dequeue_pullup();

    rc = os_mbuf_copydata(om, off, 1, &op);
    TEST_ASSERT(rc == 0);
    off += 1;

    TEST_ASSERT(op == BLE_ATT_OP_FIND_TYPE_VALUE_RSP);

    for (entry = entries; entry->first != 0; entry++) {
        rc = os_mbuf_copydata(om, off, 2, &u16);
        TEST_ASSERT(rc == 0);
        htole16(&u16, u16);
        TEST_ASSERT(u16 == entry->first);
        off += 2;

        rc = os_mbuf_copydata(om, off, 2, &u16);
        TEST_ASSERT(rc == 0);
        htole16(&u16, u16);
        TEST_ASSERT(u16 == entry->last);
        off += 2;
    }

    /* Ensure there is no extra data in the response. */
    TEST_ASSERT(off == OS_MBUF_PKTHDR(om)->omp_len);
}

struct ble_att_svr_test_group_type_entry {
    uint16_t start_handle;  /* 0 on last entry */
    uint16_t end_handle;    /* 0 on last entry */
    uint16_t uuid16;        /* 0 if not present. */
    uint8_t uuid128[16];
};

/** Returns the number of entries successfully verified. */
static void
ble_att_svr_test_misc_verify_tx_read_group_type_rsp(
    struct ble_att_svr_test_group_type_entry *entries)
{
    struct ble_att_svr_test_group_type_entry *entry;
    struct ble_att_read_group_type_rsp rsp;
    struct os_mbuf *om;
    uint16_t u16;
    uint8_t uuid128[16];
    int off;
    int rc;

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue_pullup();

    ble_att_read_group_type_rsp_parse(om->om_data, om->om_len, &rsp);

    off = BLE_ATT_READ_GROUP_TYPE_RSP_BASE_SZ;
    for (entry = entries; entry->start_handle != 0; entry++) {
        if (entry->uuid16 != 0) {
            TEST_ASSERT(rsp.bagp_length ==
                        BLE_ATT_READ_GROUP_TYPE_ADATA_SZ_16);
        } else {
            TEST_ASSERT(rsp.bagp_length ==
                        BLE_ATT_READ_GROUP_TYPE_ADATA_SZ_128);
        }

        rc = os_mbuf_copydata(om, off, 2, &u16);
        TEST_ASSERT(rc == 0);
        htole16(&u16, u16);
        TEST_ASSERT(u16 == entry->start_handle);
        off += 2;

        rc = os_mbuf_copydata(om, off, 2, &u16);
        TEST_ASSERT(rc == 0);
        htole16(&u16, u16);
        if (entry->start_handle == BLE_ATT_SVR_TEST_LAST_SVC) {
            TEST_ASSERT(u16 == 0xffff);
        } else {
            TEST_ASSERT(u16 == entry->end_handle);
        }
        off += 2;

        if (entry->uuid16 != 0) {
            rc = os_mbuf_copydata(om, off, 2, &u16);
            TEST_ASSERT(rc == 0);
            htole16(&u16, u16);
            TEST_ASSERT(u16 == entry->uuid16);
            off += 2;
        } else {
            rc = os_mbuf_copydata(om, off, 16, uuid128);
            TEST_ASSERT(rc == 0);
            TEST_ASSERT(memcmp(uuid128, entry->uuid128, 16) == 0);
            off += 16;
        }
    }

    /* Ensure there is no extra data in the response. */
    TEST_ASSERT(off == OS_MBUF_PKTLEN(om));
}

struct ble_att_svr_test_type_entry {
    uint16_t handle;  /* 0 on last entry */
    void *value;
    int value_len;
};

/** Returns the number of entries successfully verified. */
static void
ble_att_svr_test_misc_verify_tx_read_type_rsp(
    struct ble_att_svr_test_type_entry *entries)
{
    struct ble_att_svr_test_type_entry *entry;
    struct ble_att_read_type_rsp rsp;
    struct os_mbuf *om;
    uint16_t handle;
    uint8_t buf[512];
    int off;
    int rc;

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue_pullup();

    ble_att_read_type_rsp_parse(om->om_data, om->om_len, &rsp);

    off = BLE_ATT_READ_TYPE_RSP_BASE_SZ;
    for (entry = entries; entry->handle != 0; entry++) {
        TEST_ASSERT_FATAL(rsp.batp_length ==
                          BLE_ATT_READ_TYPE_ADATA_BASE_SZ + entry->value_len);

        rc = os_mbuf_copydata(om, off, 2, &handle);
        TEST_ASSERT(rc == 0);
        handle = le16toh(&handle);
        TEST_ASSERT(handle == entry->handle);
        off += 2;

        rc = os_mbuf_copydata(om, off, entry->value_len, buf);
        TEST_ASSERT(rc == 0);
        TEST_ASSERT(memcmp(entry->value, buf, entry->value_len) == 0);
        off += entry->value_len;
    }

    /* Ensure there is no extra data in the response. */
    TEST_ASSERT(off == OS_MBUF_PKTLEN(om));
}

static void
ble_att_svr_test_misc_verify_tx_prep_write_rsp(uint16_t attr_handle,
                                               uint16_t offset,
                                               void *data, int data_len)
{
    struct ble_att_prep_write_cmd rsp;
    struct os_mbuf *om;
    uint8_t buf[1024];
    int rc;

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue();

    rc = os_mbuf_copydata(om, 0, OS_MBUF_PKTLEN(om), buf);
    TEST_ASSERT_FATAL(rc == 0);

    ble_att_prep_write_rsp_parse(buf, sizeof buf, &rsp);

    TEST_ASSERT(rsp.bapc_handle == attr_handle);
    TEST_ASSERT(rsp.bapc_offset == offset);
    TEST_ASSERT(memcmp(buf + BLE_ATT_PREP_WRITE_CMD_BASE_SZ, data,
                       data_len) == 0);

    TEST_ASSERT(OS_MBUF_PKTLEN(om) ==
                BLE_ATT_PREP_WRITE_CMD_BASE_SZ + data_len);
}

static void
ble_att_svr_test_misc_verify_tx_exec_write_rsp(void)
{
    struct os_mbuf *om;

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue_pullup();
    ble_att_exec_write_rsp_parse(om->om_data, om->om_len);
}

static void
ble_att_svr_test_misc_mtu_exchange(uint16_t my_mtu, uint16_t peer_sent,
                                   uint16_t peer_actual, uint16_t chan_mtu)
{
    struct ble_att_mtu_cmd req;
    struct ble_l2cap_chan *chan;
    struct ble_hs_conn *conn;
    uint16_t conn_handle;
    uint8_t buf[BLE_ATT_MTU_CMD_SZ];
    int rc;

    conn_handle = ble_att_svr_test_misc_init(my_mtu);

    req.bamc_mtu = peer_sent;
    ble_att_mtu_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);

    ble_att_svr_test_misc_verify_tx_mtu_rsp(conn_handle);

    ble_hs_lock();
    rc = ble_hs_misc_conn_chan_find(conn_handle, BLE_L2CAP_CID_ATT,
                                    &conn, &chan);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(chan->blc_peer_mtu == peer_actual);
    TEST_ASSERT(ble_l2cap_chan_mtu(chan) == chan_mtu);
    ble_hs_unlock();

}

static void
ble_att_svr_test_misc_prep_write(uint16_t conn_handle, uint16_t attr_handle,
                                 uint16_t offset, void *data,
                                 int data_len, uint8_t error_code)
{
    struct ble_att_prep_write_cmd prep_req;
    uint8_t buf[1024];
    int rc;

    prep_req.bapc_handle = attr_handle;
    prep_req.bapc_offset = offset;
    ble_att_prep_write_req_write(buf, sizeof buf, &prep_req);
    memcpy(buf + BLE_ATT_PREP_WRITE_CMD_BASE_SZ, data, data_len);
    rc = ble_hs_test_util_l2cap_rx_payload_flat(
        conn_handle, BLE_L2CAP_CID_ATT, buf,
        BLE_ATT_PREP_WRITE_CMD_BASE_SZ + data_len);

    if (error_code == 0) {
        TEST_ASSERT(rc == 0);
        ble_att_svr_test_misc_verify_tx_prep_write_rsp(attr_handle, offset,
                                                       data, data_len);
    } else {
        TEST_ASSERT(rc != 0);
        ble_att_svr_test_misc_verify_tx_err_rsp(BLE_ATT_OP_PREP_WRITE_REQ,
                                                attr_handle, error_code);
    }
}

static void
ble_att_svr_test_misc_exec_write(uint16_t conn_handle, uint8_t flags,
                                 uint8_t error_code, uint16_t error_handle)
{
    struct ble_att_exec_write_req exec_req;
    uint8_t buf[1024];
    int rc;

    exec_req.baeq_flags = flags;
    ble_att_exec_write_req_write(buf, sizeof buf, &exec_req);
    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf,
                                                BLE_ATT_EXEC_WRITE_REQ_SZ);

    if (error_code == 0) {
        TEST_ASSERT(rc == 0);
        ble_att_svr_test_misc_verify_tx_exec_write_rsp();
    } else {
        TEST_ASSERT(rc != 0);
        ble_att_svr_test_misc_verify_tx_err_rsp(BLE_ATT_OP_EXEC_WRITE_REQ,
                                                error_handle, error_code);
    }
}

static void
ble_att_svr_test_misc_rx_notify(uint16_t conn_handle, uint16_t attr_handle,
                                void *attr_val, int attr_len, int good)
{
    struct ble_att_notify_req req;
    uint8_t buf[1024];
    int off;
    int rc;

    req.banq_handle = attr_handle;
    ble_att_notify_req_write(buf, sizeof buf, &req);
    off = BLE_ATT_NOTIFY_REQ_BASE_SZ;

    memcpy(buf + off, attr_val, attr_len);
    off += attr_len;

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, off);
    if (good) {
        TEST_ASSERT(rc == 0);
    } else {
        TEST_ASSERT(rc == BLE_HS_EBADDATA);
    }
}

static void
ble_att_svr_test_misc_verify_notify(uint16_t conn_handle, uint16_t attr_handle,
                                    void *attr_val, int attr_len, int good)
{
    ble_att_svr_test_n_conn_handle = 0xffff;
    ble_att_svr_test_n_attr_handle = 0;
    ble_att_svr_test_attr_n_len = 0;

    ble_att_svr_test_misc_rx_notify(conn_handle, attr_handle, attr_val,
                                    attr_len, good);

    if (good) {
        TEST_ASSERT(ble_att_svr_test_n_conn_handle == conn_handle);
        TEST_ASSERT(ble_att_svr_test_n_attr_handle == attr_handle);
        TEST_ASSERT(ble_att_svr_test_attr_n_len == attr_len);
        TEST_ASSERT(memcmp(ble_att_svr_test_attr_n, attr_val, attr_len) == 0);
    } else {
        TEST_ASSERT(ble_att_svr_test_n_conn_handle == 0xffff);
        TEST_ASSERT(ble_att_svr_test_n_attr_handle == 0);
        TEST_ASSERT(ble_att_svr_test_attr_n_len == 0);
    }
}

static void
ble_att_svr_test_misc_verify_tx_indicate_rsp(void)
{
    struct os_mbuf *om;

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue_pullup();

    ble_att_indicate_rsp_parse(om->om_data, om->om_len);
}

static void
ble_att_svr_test_misc_rx_indicate(uint16_t conn_handle, uint16_t attr_handle,
                                  void *attr_val, int attr_len, int good)
{
    struct ble_att_indicate_req req;
    uint8_t buf[1024];
    int off;
    int rc;

    req.baiq_handle = attr_handle;
    ble_att_indicate_req_write(buf, sizeof buf, &req);
    off = BLE_ATT_INDICATE_REQ_BASE_SZ;

    memcpy(buf + off, attr_val, attr_len);
    off += attr_len;

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, off);
    if (good) {
        TEST_ASSERT(rc == 0);
    } else {
        TEST_ASSERT(rc == BLE_HS_EBADDATA);
    }
}

static void
ble_att_svr_test_misc_verify_indicate(uint16_t conn_handle,
                                      uint16_t attr_handle,
                                      void *attr_val, int attr_len, int good)
{
    ble_att_svr_test_n_conn_handle = 0xffff;
    ble_att_svr_test_n_attr_handle = 0;
    ble_att_svr_test_attr_n_len = 0;

    ble_att_svr_test_misc_rx_indicate(conn_handle, attr_handle, attr_val,
                                      attr_len, good);

    if (good) {
        TEST_ASSERT(ble_att_svr_test_n_conn_handle == conn_handle);
        TEST_ASSERT(ble_att_svr_test_n_attr_handle == attr_handle);
        TEST_ASSERT(ble_att_svr_test_attr_n_len == attr_len);
        TEST_ASSERT(memcmp(ble_att_svr_test_attr_n, attr_val, attr_len) == 0);
        ble_att_svr_test_misc_verify_tx_indicate_rsp();
    } else {
        TEST_ASSERT(ble_att_svr_test_n_conn_handle == 0xffff);
        TEST_ASSERT(ble_att_svr_test_n_attr_handle == 0);
        TEST_ASSERT(ble_att_svr_test_attr_n_len == 0);
        ble_hs_test_util_tx_all();
        TEST_ASSERT(ble_hs_test_util_prev_tx_queue_sz() == 0);
    }
}

TEST_CASE(ble_att_svr_test_mtu)
{
    /*** MTU too low; should pretend peer sent default value instead. */
    ble_att_svr_test_misc_mtu_exchange(BLE_ATT_MTU_DFLT, 5,
                                       BLE_ATT_MTU_DFLT, BLE_ATT_MTU_DFLT);

    /*** MTUs equal. */
    ble_att_svr_test_misc_mtu_exchange(50, 50, 50, 50);

    /*** Peer's higher than mine. */
    ble_att_svr_test_misc_mtu_exchange(50, 100, 100, 50);

    /*** Mine higher than peer's. */
    ble_att_svr_test_misc_mtu_exchange(100, 50, 50, 50);
}

TEST_CASE(ble_att_svr_test_read)
{
    struct ble_att_read_req req;
    struct ble_hs_conn *conn;
    struct os_mbuf *om;
    uint16_t conn_handle;
    uint8_t buf[BLE_ATT_READ_REQ_SZ];
    uint8_t uuid_sec[16] = {1};
    uint8_t uuid[16] = {0};
    int rc;

    conn_handle = ble_att_svr_test_misc_init(0);

    /*** Nonexistent attribute. */
    req.barq_handle = 0;
    ble_att_read_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(BLE_ATT_OP_READ_REQ, 0,
                                            BLE_ATT_ERR_INVALID_HANDLE);

    /*** Successful read. */
    ble_att_svr_test_attr_r_1 = (uint8_t[]){0,1,2,3,4,5,6,7};
    ble_att_svr_test_attr_r_1_len = 8;
    rc = ble_att_svr_register(uuid, HA_FLAG_PERM_RW, &req.barq_handle,
                              ble_att_svr_test_misc_attr_fn_r_1, NULL);
    TEST_ASSERT(rc == 0);

    ble_att_read_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_verify_tx_read_rsp(
        ble_att_svr_test_attr_r_1, ble_att_svr_test_attr_r_1_len);

    /*** Partial read. */
    ble_att_svr_test_attr_r_1 =
        (uint8_t[]){0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,
                    22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39};
    ble_att_svr_test_attr_r_1_len = 40;

    ble_att_read_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_verify_tx_read_rsp(ble_att_svr_test_attr_r_1,
                                        BLE_ATT_MTU_DFLT - 1);

    /*** Read requires encryption. */
    /* Insufficient authentication. */
    rc = ble_att_svr_register(uuid_sec, BLE_ATT_F_READ | BLE_ATT_F_READ_ENC,
                              &req.barq_handle,
                              ble_att_svr_test_misc_attr_fn_r_1, NULL);
    TEST_ASSERT(rc == 0);

    ble_att_read_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN));
    ble_att_svr_test_misc_verify_tx_err_rsp(BLE_ATT_OP_READ_REQ,
                                            req.barq_handle,
                                            BLE_ATT_ERR_INSUFFICIENT_AUTHEN);

    /* Security check bypassed for local reads. */
    rc = ble_att_svr_read_local(req.barq_handle, &om);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(OS_MBUF_PKTLEN(om) == ble_att_svr_test_attr_r_1_len);
    TEST_ASSERT(os_mbuf_cmpf(om, 0, ble_att_svr_test_attr_r_1,
                               ble_att_svr_test_attr_r_1_len) == 0);
    os_mbuf_free_chain(om);

    /* Ensure no response got sent. */
    ble_hs_test_util_tx_all();
    TEST_ASSERT(ble_hs_test_util_prev_tx_dequeue() == NULL);

    /* Encrypt link; success. */
    ble_hs_lock();
    conn = ble_hs_conn_find(conn_handle);
    conn->bhc_sec_state.encrypted = 1;
    ble_hs_unlock();

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_hs_test_util_verify_tx_read_rsp(ble_att_svr_test_attr_r_1,
                                        BLE_ATT_MTU_DFLT - 1);

}

TEST_CASE(ble_att_svr_test_read_blob)
{
    struct ble_att_read_blob_req req;
    uint16_t conn_handle;
    uint8_t buf[BLE_ATT_READ_BLOB_REQ_SZ];
    uint8_t uuid[16] = {0};
    int rc;

    conn_handle = ble_att_svr_test_misc_init(0);

    /*** Nonexistent attribute. */
    req.babq_handle = 0;
    req.babq_offset = 0;
    ble_att_read_blob_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(BLE_ATT_OP_READ_BLOB_REQ, 0,
                                            BLE_ATT_ERR_INVALID_HANDLE);


    /*** Successful partial read. */
    ble_att_svr_test_attr_r_1 =
        (uint8_t[]){0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,
                    22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39};
    ble_att_svr_test_attr_r_1_len = 40;
    rc = ble_att_svr_register(uuid, HA_FLAG_PERM_RW, &req.babq_handle,
                              ble_att_svr_test_misc_attr_fn_r_1, NULL);
    TEST_ASSERT(rc == 0);

    ble_att_read_blob_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_read_blob_rsp(ble_att_svr_test_attr_r_1,
                                                  BLE_ATT_MTU_DFLT - 1);

    /*** Read remainder of attribute. */
    req.babq_offset = BLE_ATT_MTU_DFLT - 1;
    ble_att_read_blob_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_read_blob_rsp(
        ble_att_svr_test_attr_r_1 + BLE_ATT_MTU_DFLT - 1,
        40 - (BLE_ATT_MTU_DFLT - 1));

    /*** Zero-length read. */
    req.babq_offset = ble_att_svr_test_attr_r_1_len;
    ble_att_read_blob_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_read_blob_rsp(ble_att_svr_test_attr_r_1,
                                                  0);

}

TEST_CASE(ble_att_svr_test_read_mult)
{
    uint16_t conn_handle;
    int rc;

    conn_handle = ble_att_svr_test_misc_init(0);

    struct ble_hs_test_util_flat_attr attrs[2] = {
        {
            .handle = 0,
            .offset = 0,
            .value = { 1, 2, 3, 4 },
            .value_len = 4,
        },
        {
            .handle = 0,
            .offset = 0,
            .value = { 2, 3, 4, 5, 6 },
            .value_len = 5,
        },
    };

    ble_att_svr_test_attr_r_1 = attrs[0].value;
    ble_att_svr_test_attr_r_1_len = attrs[0].value_len;
    ble_att_svr_test_attr_r_2 = attrs[1].value;
    ble_att_svr_test_attr_r_2_len = attrs[1].value_len;

    rc = ble_att_svr_register(BLE_UUID16(0x1111), HA_FLAG_PERM_RW,
                              &attrs[0].handle,
                              ble_att_svr_test_misc_attr_fn_r_1, NULL);
    TEST_ASSERT(rc == 0);

    rc = ble_att_svr_register(BLE_UUID16(0x2222), HA_FLAG_PERM_RW,
                              &attrs[1].handle,
                              ble_att_svr_test_misc_attr_fn_r_2, NULL);
    TEST_ASSERT(rc == 0);

    /*** Single nonexistent attribute. */
    ble_att_svr_test_misc_rx_read_mult_req(
        conn_handle, ((uint16_t[]){ 100 }), 1, 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(BLE_ATT_OP_READ_MULT_REQ,
                                            100, BLE_ATT_ERR_INVALID_HANDLE);

    /*** Single attribute. */
    ble_att_svr_test_misc_verify_all_read_mult(conn_handle, &attrs[0], 1);

    /*** Two attributes. */
    ble_att_svr_test_misc_verify_all_read_mult(conn_handle, attrs, 2);

    /*** Reverse order. */
    ble_att_svr_test_misc_verify_all_read_mult(conn_handle, attrs, 2);

    /*** Second attribute nonexistent; verify only error txed. */
    ble_att_svr_test_misc_rx_read_mult_req(
        conn_handle, ((uint16_t[]){ attrs[0].handle, 100 }), 2, 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(BLE_ATT_OP_READ_MULT_REQ,
                                            100, BLE_ATT_ERR_INVALID_HANDLE);

    /*** Response too long; verify only MTU bytes sent. */
    attrs[0].value_len = 20;
    memcpy(attrs[0].value,
           ((uint8_t[]){0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19}),
           attrs[0].value_len);
    ble_att_svr_test_attr_r_1_len = attrs[0].value_len;

    attrs[1].value_len = 20;
    memcpy(attrs[1].value,
           ((uint8_t[]){
                22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39
           }),
           attrs[1].value_len);
    ble_att_svr_test_attr_r_2_len = attrs[1].value_len;

    ble_att_svr_test_misc_verify_all_read_mult(conn_handle, attrs, 2);

}

TEST_CASE(ble_att_svr_test_write)
{
    struct ble_att_write_req req;
    struct ble_hs_conn *conn;
    uint16_t conn_handle;
    uint8_t buf[BLE_ATT_WRITE_REQ_BASE_SZ + 8];
    uint8_t uuid_sec[16] = {2};
    uint8_t uuid_rw[16] = {0};
    uint8_t uuid_r[16] = {1};
    int rc;

    conn_handle = ble_att_svr_test_misc_init(0);

    /*** Nonexistent attribute. */
    req.bawq_handle = 0;
    ble_att_write_req_write(buf, sizeof buf, &req);
    memcpy(buf + BLE_ATT_READ_REQ_SZ, ((uint8_t[]){0,1,2,3,4,5,6,7}), 8);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_WRITE_REQ, 0, BLE_ATT_ERR_INVALID_HANDLE);

    /*** Write not permitted if non-local. */
    /* Non-local write (fail). */
    rc = ble_att_svr_register(uuid_r, BLE_ATT_F_READ, &req.bawq_handle,
                              ble_att_svr_test_misc_attr_fn_w_1, NULL);
    TEST_ASSERT(rc == 0);

    ble_att_write_req_write(buf, sizeof buf, &req);
    memcpy(buf + BLE_ATT_WRITE_REQ_BASE_SZ,
           ((uint8_t[]){0,1,2,3,4,5,6,7}), 8);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == BLE_HS_ENOTSUP);
    ble_att_svr_test_misc_verify_tx_err_rsp(BLE_ATT_OP_WRITE_REQ,
                                            req.bawq_handle,
                                            BLE_ATT_ERR_WRITE_NOT_PERMITTED);

    /* Local write (success). */
    rc = ble_hs_test_util_write_local_flat(req.bawq_handle, buf, sizeof buf);
    TEST_ASSERT(rc == 0);

    /* Ensure no response got sent. */
    ble_hs_test_util_tx_all();
    TEST_ASSERT(ble_hs_test_util_prev_tx_dequeue() == NULL);

    /*** Successful write. */
    rc = ble_att_svr_register(uuid_rw, HA_FLAG_PERM_RW, &req.bawq_handle,
                              ble_att_svr_test_misc_attr_fn_w_1, NULL);
    TEST_ASSERT(rc == 0);

    ble_att_write_req_write(buf, sizeof buf, &req);
    memcpy(buf + BLE_ATT_WRITE_REQ_BASE_SZ,
           ((uint8_t[]){0,1,2,3,4,5,6,7}), 8);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_write_rsp();

    /*** Write requires encryption. */
    /* Insufficient authentication. */
    rc = ble_att_svr_register(uuid_sec, BLE_ATT_F_WRITE | BLE_ATT_F_WRITE_ENC,
                              &req.bawq_handle,
                              ble_att_svr_test_misc_attr_fn_w_1, NULL);
    TEST_ASSERT(rc == 0);

    ble_att_write_req_write(buf, sizeof buf, &req);
    memcpy(buf + BLE_ATT_WRITE_REQ_BASE_SZ,
           ((uint8_t[]){0,1,2,3,4,5,6,7}), 8);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN));
    ble_att_svr_test_misc_verify_tx_err_rsp(BLE_ATT_OP_WRITE_REQ,
                                            req.bawq_handle,
                                            BLE_ATT_ERR_INSUFFICIENT_AUTHEN);

    /* Security check bypassed for local writes. */
    rc = ble_hs_test_util_write_local_flat(req.bawq_handle, buf, sizeof buf);
    TEST_ASSERT(rc == 0);

    /* Ensure no response got sent. */
    ble_hs_test_util_tx_all();
    TEST_ASSERT(ble_hs_test_util_prev_tx_dequeue() == NULL);

    /* Encrypt link; success. */
    ble_hs_lock();
    conn = ble_hs_conn_find(conn_handle);
    conn->bhc_sec_state.encrypted = 1;
    ble_hs_unlock();

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_write_rsp();
}

TEST_CASE(ble_att_svr_test_find_info)
{
    struct ble_att_find_info_req req;
    uint16_t conn_handle;
    uint16_t handle1;
    uint16_t handle2;
    uint16_t handle3;
    uint8_t buf[BLE_ATT_FIND_INFO_REQ_SZ];
    uint8_t uuid1[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    uint8_t uuid2[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    uint8_t uuid3[16] = {
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00
    };
    int rc;

    conn_handle = ble_att_svr_test_misc_init(128);

    /*** Start handle of 0. */
    req.bafq_start_handle = 0;
    req.bafq_end_handle = 0;

    ble_att_find_info_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_FIND_INFO_REQ, 0, BLE_ATT_ERR_INVALID_HANDLE);

    /*** Start handle > end handle. */
    req.bafq_start_handle = 101;
    req.bafq_end_handle = 100;

    ble_att_find_info_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_FIND_INFO_REQ, 101, BLE_ATT_ERR_INVALID_HANDLE);

    /*** No attributes. */
    req.bafq_start_handle = 200;
    req.bafq_end_handle = 300;

    ble_att_find_info_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_FIND_INFO_REQ, 200, BLE_ATT_ERR_ATTR_NOT_FOUND);

    /*** Range too late. */
    rc = ble_att_svr_register(uuid1, HA_FLAG_PERM_RW, &handle1,
                              ble_att_svr_test_misc_attr_fn_r_1, NULL);
    TEST_ASSERT(rc == 0);

    req.bafq_start_handle = 200;
    req.bafq_end_handle = 300;

    ble_att_find_info_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_FIND_INFO_REQ, 200, BLE_ATT_ERR_ATTR_NOT_FOUND);

    /*** One 128-bit entry. */
    req.bafq_start_handle = handle1;
    req.bafq_end_handle = handle1;

    ble_att_find_info_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_find_info_rsp(
        ((struct ble_att_svr_test_info_entry[]) { {
            .handle = handle1,
            .uuid128 = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
        }, {
            .handle = 0,
        } }));

    /*** Two 128-bit entries. */
    rc = ble_att_svr_register(uuid2, HA_FLAG_PERM_RW, &handle2,
                              ble_att_svr_test_misc_attr_fn_r_1, NULL);
    TEST_ASSERT(rc == 0);

    req.bafq_start_handle = handle1;
    req.bafq_end_handle = handle2;

    ble_att_find_info_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_find_info_rsp(
        ((struct ble_att_svr_test_info_entry[]) { {
            .handle = handle1,
            .uuid128 = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
        }, {
            .handle = handle2,
            .uuid128 = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16},
        }, {
            .handle = 0,
        } }));

    /*** Two 128-bit entries; 16-bit entry doesn't get sent. */
    rc = ble_att_svr_register(uuid3, HA_FLAG_PERM_RW, &handle3,
                              ble_att_svr_test_misc_attr_fn_r_1, NULL);
    TEST_ASSERT(rc == 0);

    req.bafq_start_handle = handle1;
    req.bafq_end_handle = handle3;

    ble_att_find_info_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_find_info_rsp(
        ((struct ble_att_svr_test_info_entry[]) { {
            .handle = handle1,
            .uuid128 = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
        }, {
            .handle = handle2,
            .uuid128 = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16},
        }, {
            .handle = 0,
        } }));

    /*** Remaining 16-bit entry requested. */
    req.bafq_start_handle = handle3;
    req.bafq_end_handle = handle3;

    ble_att_find_info_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_find_info_rsp(
        ((struct ble_att_svr_test_info_entry[]) { {
            .handle = handle3,
            .uuid16 = 0x000f,
        }, {
            .handle = 0,
        } }));

}

TEST_CASE(ble_att_svr_test_find_type_value)
{
    struct ble_att_find_type_value_req req;
    uint8_t buf[BLE_ATT_FIND_TYPE_VALUE_REQ_BASE_SZ + 2];
    uint16_t conn_handle;
    uint16_t handle1;
    uint16_t handle2;
    uint16_t handle3;
    uint16_t handle4;
    uint16_t handle5;
    uint8_t uuid1[16] = {
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00
    };
    uint8_t uuid2[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    uint8_t uuid3[16] = {
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00
    };
    int rc;

    conn_handle = ble_att_svr_test_misc_init(128);

    /* One-time write of the attribute value at the end of the request. */
    ble_att_svr_test_attr_r_1 = (uint8_t[]){0x99, 0x99};
    ble_att_svr_test_attr_r_1_len = 2;
    memcpy(buf + BLE_ATT_FIND_TYPE_VALUE_REQ_BASE_SZ,
           ble_att_svr_test_attr_r_1,
           ble_att_svr_test_attr_r_1_len);

    /*** Start handle of 0. */
    req.bavq_start_handle = 0;
    req.bavq_end_handle = 0;
    req.bavq_attr_type = 0x0001;

    ble_att_find_type_value_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_FIND_TYPE_VALUE_REQ, 0,
        BLE_ATT_ERR_INVALID_HANDLE);

    /*** Start handle > end handle. */
    req.bavq_start_handle = 101;
    req.bavq_end_handle = 100;

    ble_att_find_type_value_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_FIND_TYPE_VALUE_REQ, 101,
        BLE_ATT_ERR_INVALID_HANDLE);

    /*** No attributes. */
    req.bavq_start_handle = 200;
    req.bavq_end_handle = 300;

    ble_att_find_type_value_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_FIND_TYPE_VALUE_REQ, 200,
        BLE_ATT_ERR_ATTR_NOT_FOUND);

    /*** Range too late. */
    rc = ble_att_svr_register(uuid1, HA_FLAG_PERM_RW, &handle1,
                              ble_att_svr_test_misc_attr_fn_r_1, NULL);
    TEST_ASSERT(rc == 0);

    req.bavq_start_handle = 200;
    req.bavq_end_handle = 300;

    ble_att_find_type_value_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_FIND_TYPE_VALUE_REQ, 200,
        BLE_ATT_ERR_ATTR_NOT_FOUND);

    /*** One entry, one attribute. */
    req.bavq_start_handle = handle1;
    req.bavq_end_handle = handle1;

    ble_att_find_type_value_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_find_type_value_rsp(
        ((struct ble_att_svr_test_type_value_entry[]) { {
            .first = handle1,
            .last = handle1,
        }, {
            .first = 0,
        } }));

    /*** One entry, two attributes. */
    rc = ble_att_svr_register(uuid1, HA_FLAG_PERM_RW, &handle2,
                              ble_att_svr_test_misc_attr_fn_r_1, NULL);
    TEST_ASSERT(rc == 0);

    req.bavq_start_handle = handle1;
    req.bavq_end_handle = handle2;

    ble_att_find_type_value_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_find_type_value_rsp(
        ((struct ble_att_svr_test_type_value_entry[]) { {
            .first = handle1,
            .last = handle2,
        }, {
            .first = 0,
        } }));

    /*** Entry 1: two attributes; entry 2: one attribute. */
    rc = ble_att_svr_register(uuid2, HA_FLAG_PERM_RW, &handle3,
                              ble_att_svr_test_misc_attr_fn_r_2, NULL);
    TEST_ASSERT(rc == 0);

    rc = ble_att_svr_register(uuid1, HA_FLAG_PERM_RW, &handle4,
                              ble_att_svr_test_misc_attr_fn_r_1, NULL);
    TEST_ASSERT(rc == 0);

    req.bavq_start_handle = 0x0001;
    req.bavq_end_handle = 0xffff;

    ble_att_find_type_value_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_find_type_value_rsp(
        ((struct ble_att_svr_test_type_value_entry[]) { {
            .first = handle1,
            .last = handle2,
        }, {
            .first = handle4,
            .last = handle4,
        }, {
            .first = 0,
        } }));

    /*** Ensure attribute with wrong value is not included. */
    ble_att_svr_test_attr_r_2 = (uint8_t[]){0x00, 0x00};
    ble_att_svr_test_attr_r_2_len = 2;

    req.bavq_start_handle = 0x0001;
    req.bavq_end_handle = 0xffff;

    ble_att_find_type_value_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_find_type_value_rsp(
        ((struct ble_att_svr_test_type_value_entry[]) { {
            .first = handle1,
            .last = handle2,
        }, {
            .first = handle4,
            .last = handle4,
        }, {
            .first = 0,
        } }));

    /*** Ensure attribute with wrong type is not included. */
    rc = ble_att_svr_register(uuid3, HA_FLAG_PERM_RW, &handle5,
                              ble_att_svr_test_misc_attr_fn_r_1, NULL);

    req.bavq_start_handle = 0x0001;
    req.bavq_end_handle = 0xffff;

    ble_att_find_type_value_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_find_type_value_rsp(
        ((struct ble_att_svr_test_type_value_entry[]) { {
            .first = handle1,
            .last = handle2,
        }, {
            .first = handle4,
            .last = handle4,
        }, {
            .first = 0,
        } }));

}

static void
ble_att_svr_test_misc_read_type(uint16_t mtu)
{
    struct ble_att_read_type_req req;
    uint16_t conn_handle;
    uint8_t buf[BLE_ATT_READ_TYPE_REQ_SZ_16];
    int rc;

    conn_handle = ble_att_svr_test_misc_init(mtu);

    /*** Start handle of 0. */
    req.batq_start_handle = 0;
    req.batq_end_handle = 0;

    ble_att_read_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_PRIMARY_SERVICE);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_READ_TYPE_REQ, 0,
        BLE_ATT_ERR_INVALID_HANDLE);

    /*** Start handle > end handle. */
    req.batq_start_handle = 101;
    req.batq_end_handle = 100;

    ble_att_read_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_PRIMARY_SERVICE);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_READ_TYPE_REQ, 101,
        BLE_ATT_ERR_INVALID_HANDLE);

    /*** No attributes. */
    req.batq_start_handle = 1;
    req.batq_end_handle = 0xffff;

    ble_att_read_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_PRIMARY_SERVICE);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_READ_TYPE_REQ, 1,
        BLE_ATT_ERR_ATTR_NOT_FOUND);

    /*** Range too late. */
    ble_att_svr_test_misc_register_group_attrs();
    req.batq_start_handle = 200;
    req.batq_end_handle = 300;

    ble_att_read_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_PRIMARY_SERVICE);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_READ_TYPE_REQ, 200,
        BLE_ATT_ERR_ATTR_NOT_FOUND);

    /*** One characteristic from one service. */
    req.batq_start_handle = 1;
    req.batq_end_handle = 2;

    ble_att_read_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_CHARACTERISTIC);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_read_type_rsp(
        ((struct ble_att_svr_test_type_entry[]) { {
            .handle = 2,
            .value = (uint8_t[]){ 0x01, 0x11 },
            .value_len = 2,
        }, {
            .handle = 0,
        } }));

    /*** Both characteristics from one service. */
    req.batq_start_handle = 1;
    req.batq_end_handle = 10;

    ble_att_read_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_CHARACTERISTIC);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_read_type_rsp(
        ((struct ble_att_svr_test_type_entry[]) { {
            .handle = 2,
            .value = (uint8_t[]){ 0x01, 0x11 },
            .value_len = 2,
        }, {
            .handle = 4,
            .value = (uint8_t[]){ 0x03, 0x11 },
            .value_len = 2,
        }, {
            .handle = 0,
        } }));

    /*** Ensure 16-bit and 128-bit values are retrieved separately. */
    req.batq_start_handle = 11;
    req.batq_end_handle = 0xffff;

    ble_att_read_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_CHARACTERISTIC);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_read_type_rsp(
        ((struct ble_att_svr_test_type_entry[]) { {
            .handle = 12,
            .value = (uint8_t[]){ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
            .value_len = 16,
        }, {
            .handle = 0,
        } }));

    req.batq_start_handle = 13;
    req.batq_end_handle = 0xffff;

    ble_att_read_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_CHARACTERISTIC);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_read_type_rsp(
        ((struct ble_att_svr_test_type_entry[]) { {
            .handle = 14,
            .value = (uint8_t[]){ 0x55, 0x55 },
            .value_len = 2,
        }, {
            .handle = 0,
        } }));

    req.batq_start_handle = 15;
    req.batq_end_handle = 0xffff;

    ble_att_read_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_CHARACTERISTIC);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_read_type_rsp(
        ((struct ble_att_svr_test_type_entry[]) { {
            .handle = 16,
            .value = (uint8_t[]){ 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2 },
            .value_len = 16,
        }, {
            .handle = 0,
        } }));

    /*** Read until the end of the attribute list. */
    req.batq_start_handle = 17;
    req.batq_end_handle = 0xffff;

    ble_att_read_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_CHARACTERISTIC);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_read_type_rsp(
        ((struct ble_att_svr_test_type_entry[]) { {
            .handle = 18,
            .value = (uint8_t[]){ 0x66, 0x66 },
            .value_len = 2,
        }, {
            .handle = 20,
            .value = (uint8_t[]){ 0x77, 0x77 },
            .value_len = 2,
        }, {
            .handle = 22,
            .value = (uint8_t[]){ 0x88, 0x88 },
            .value_len = 2,
        }, {
            .handle = 24,
            .value = (uint8_t[]){ 0x99, 0x99 },
            .value_len = 2,
        }, {
            .handle = 0,
        } }));

}

TEST_CASE(ble_att_svr_test_read_type)
{
    ble_att_svr_test_misc_read_type(0);
    ble_att_svr_test_misc_read_type(128);
}

TEST_CASE(ble_att_svr_test_read_group_type)
{
    struct ble_att_read_group_type_req req;
    uint16_t conn_handle;
    uint8_t buf[BLE_ATT_READ_GROUP_TYPE_REQ_SZ_16];
    int rc;

    conn_handle = ble_att_svr_test_misc_init(128);

    /*** Start handle of 0. */
    req.bagq_start_handle = 0;
    req.bagq_end_handle = 0;

    ble_att_read_group_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_PRIMARY_SERVICE);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_READ_GROUP_TYPE_REQ, 0,
        BLE_ATT_ERR_INVALID_HANDLE);

    /*** Start handle > end handle. */
    req.bagq_start_handle = 101;
    req.bagq_end_handle = 100;

    ble_att_read_group_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_PRIMARY_SERVICE);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_READ_GROUP_TYPE_REQ, 101,
        BLE_ATT_ERR_INVALID_HANDLE);

    /*** Invalid group UUID (0x1234). */
    req.bagq_start_handle = 110;
    req.bagq_end_handle = 150;

    ble_att_read_group_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ, 0x1234);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_READ_GROUP_TYPE_REQ, 110,
        BLE_ATT_ERR_UNSUPPORTED_GROUP);

    /*** No attributes. */
    req.bagq_start_handle = 1;
    req.bagq_end_handle = 0xffff;

    ble_att_read_group_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_PRIMARY_SERVICE);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_READ_GROUP_TYPE_REQ, 1,
        BLE_ATT_ERR_ATTR_NOT_FOUND);

    /*** Range too late. */
    ble_att_svr_test_misc_register_group_attrs();
    req.bagq_start_handle = 200;
    req.bagq_end_handle = 300;

    ble_att_read_group_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_PRIMARY_SERVICE);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc != 0);
    ble_att_svr_test_misc_verify_tx_err_rsp(
        BLE_ATT_OP_READ_GROUP_TYPE_REQ, 200,
        BLE_ATT_ERR_ATTR_NOT_FOUND);

    /*** One 16-bit UUID service. */
    req.bagq_start_handle = 1;
    req.bagq_end_handle = 5;

    ble_att_read_group_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_PRIMARY_SERVICE);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_read_group_type_rsp(
        ((struct ble_att_svr_test_group_type_entry[]) { {
            .start_handle = 1,
            .end_handle = 5,
            .uuid16 = 0x1122,
        }, {
            .start_handle = 0,
        } }));

    /*** Two 16-bit UUID services. */
    req.bagq_start_handle = 1;
    req.bagq_end_handle = 10;

    ble_att_read_group_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_PRIMARY_SERVICE);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_read_group_type_rsp(
        ((struct ble_att_svr_test_group_type_entry[]) { {
            .start_handle = 1,
            .end_handle = 5,
            .uuid16 = 0x1122,
        }, {
            .start_handle = 6,
            .end_handle = 10,
            .uuid16 = 0x2233,
        }, {
            .start_handle = 0,
        } }));

    /*** Two 16-bit UUID services; ensure 128-bit service not returned. */
    req.bagq_start_handle = 1;
    req.bagq_end_handle = 100;

    ble_att_read_group_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_PRIMARY_SERVICE);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_read_group_type_rsp(
        ((struct ble_att_svr_test_group_type_entry[]) { {
            .start_handle = 1,
            .end_handle = 5,
            .uuid16 = 0x1122,
        }, {
            .start_handle = 6,
            .end_handle = 10,
            .uuid16 = 0x2233,
        }, {
            .start_handle = 0,
        } }));

    /*** One 128-bit service. */
    req.bagq_start_handle = 11;
    req.bagq_end_handle = 100;

    ble_att_read_group_type_req_write(buf, sizeof buf, &req);
    htole16(buf + BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ,
            BLE_ATT_UUID_PRIMARY_SERVICE);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
    ble_att_svr_test_misc_verify_tx_read_group_type_rsp(
        ((struct ble_att_svr_test_group_type_entry[]) { {
            .start_handle = 11,
            .end_handle = 19,
            .uuid128 = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16},
        }, {
            .start_handle = 0,
        } }));

}

TEST_CASE(ble_att_svr_test_prep_write)
{
    uint16_t conn_handle;
    int i;

    static uint8_t data[1024];

    conn_handle = ble_att_svr_test_misc_init(205);

    /* Initialize some attribute data. */
    for (i = 0; i < sizeof data; i++) {
        data[i] = i;
    }

    /* Register two writable attributes. */
    ble_att_svr_test_misc_register_uuid16(0x1234, HA_FLAG_PERM_RW, 1,
                                          ble_att_svr_test_misc_attr_fn_w_1);
    ble_att_svr_test_misc_register_uuid16(0x8989, HA_FLAG_PERM_RW, 2,
                                          ble_att_svr_test_misc_attr_fn_w_2);

    /* Register a third attribute that is not writable. */
    ble_att_svr_test_misc_register_uuid16(0xabab, BLE_ATT_F_READ, 3,
                                          ble_att_svr_test_misc_attr_fn_r_1);

    /*** Empty write succeeds. */
    ble_att_svr_test_misc_exec_write(conn_handle, BLE_ATT_EXEC_WRITE_F_CONFIRM,
                                     0, 0);

    /*** Empty cancel succeeds. */
    ble_att_svr_test_misc_exec_write(conn_handle, 0, 0, 0);

    /*** Failure for prep write to nonexistent attribute. */
    ble_att_svr_test_misc_prep_write(conn_handle, 53525, 0, data, 10,
                                     BLE_ATT_ERR_INVALID_HANDLE);

    /*** Failure for write starting at nonzero offset. */
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 1, data, 10, 0);
    ble_att_svr_test_misc_exec_write(conn_handle, BLE_ATT_EXEC_WRITE_F_CONFIRM,
                                     BLE_ATT_ERR_INVALID_OFFSET, 1);
    ble_att_svr_test_misc_verify_w_1(NULL, 0);

    /*** Success for clear starting at nonzero offset. */
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 1, data, 10, 0);
    ble_att_svr_test_misc_exec_write(conn_handle, 0, 0, 0);
    ble_att_svr_test_misc_verify_w_1(NULL, 0);

    /*** Failure for write with gap. */
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 0, data, 10, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 11, data, 10, 0);
    ble_att_svr_test_misc_exec_write(conn_handle, BLE_ATT_EXEC_WRITE_F_CONFIRM,
                                     BLE_ATT_ERR_INVALID_OFFSET, 1);
    ble_att_svr_test_misc_verify_w_1(NULL, 0);

    /*** Success for clear with gap. */
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 0, data, 10, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 11, data, 10, 0);
    ble_att_svr_test_misc_exec_write(conn_handle, 0, 0, 0);
    ble_att_svr_test_misc_verify_w_1(NULL, 0);

    /*** Failure for overlong write. */
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 0, data, 200, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 200, data + 200, 200, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 400, data + 400, 200, 0);
    ble_att_svr_test_misc_exec_write(conn_handle, BLE_ATT_EXEC_WRITE_F_CONFIRM,
                                     BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN, 1);
    ble_att_svr_test_misc_verify_w_1(NULL, 0);

    /*** Failure due to attribute callback. */
    ble_att_svr_test_misc_prep_write(conn_handle, 3, 0, data, 35, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 3, 35, data + 35, 43, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 3, 78, data + 78, 1, 0);
    ble_att_svr_test_misc_exec_write(conn_handle, BLE_ATT_EXEC_WRITE_F_CONFIRM,
                                     BLE_ATT_ERR_WRITE_NOT_PERMITTED, 0);
    ble_att_svr_test_misc_verify_w_1(NULL, 0);

    /*** Successful two part write. */
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 0, data, 20, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 20, data + 20, 20, 0);
    ble_att_svr_test_misc_exec_write(conn_handle, BLE_ATT_EXEC_WRITE_F_CONFIRM,
                                     0, 0);
    ble_att_svr_test_misc_verify_w_1(data, 40);

    /*** Successful three part write. */
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 0, data, 35, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 35, data + 35, 43, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 78, data + 78, 1, 0);
    ble_att_svr_test_misc_exec_write(conn_handle, BLE_ATT_EXEC_WRITE_F_CONFIRM,
                                     0, 0);
    ble_att_svr_test_misc_verify_w_1(data, 79);

    /*** Successful two part write to two attributes. */
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 0, data, 7, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 7, data + 7, 10, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 2, 0, data, 20, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 2, 20, data + 20, 10, 0);
    ble_att_svr_test_misc_exec_write(conn_handle, BLE_ATT_EXEC_WRITE_F_CONFIRM,
                                     0, 0);
    ble_att_svr_test_misc_verify_w_1(data, 17);
    ble_att_svr_test_misc_verify_w_2(data, 30);

    /*** Fail write to second attribute; ensure first write doesn't occur. */
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 0, data, 5, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 5, data + 5, 2, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 2, 0, data, 11, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 2, 12, data + 11, 19, 0);
    ble_att_svr_test_misc_exec_write(conn_handle, BLE_ATT_EXEC_WRITE_F_CONFIRM,
                                     BLE_ATT_ERR_INVALID_OFFSET, 2);
    ble_att_svr_test_misc_verify_w_1(data, 17);
    ble_att_svr_test_misc_verify_w_2(data, 30);

    /*** Successful out of order write to two attributes. */
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 0, data, 9, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 2, 0, data, 18, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 1, 9, data + 9, 3, 0);
    ble_att_svr_test_misc_prep_write(conn_handle, 2, 18, data + 18, 43, 0);
    ble_att_svr_test_misc_exec_write(conn_handle, BLE_ATT_EXEC_WRITE_F_CONFIRM,
                                     0, 0);
    ble_att_svr_test_misc_verify_w_1(data, 12);
    ble_att_svr_test_misc_verify_w_2(data, 61);
}

TEST_CASE(ble_att_svr_test_notify)
{
    uint16_t conn_handle;

    conn_handle = ble_att_svr_test_misc_init(0);

    /*** Successful notifies; verify callback is executed. */
    /* 3-length attribute. */
    ble_att_svr_test_misc_verify_notify(conn_handle, 10,
                                        (uint8_t[]) { 1, 2, 3 }, 3, 1);
    /* 1-length attribute. */
    ble_att_svr_test_misc_verify_notify(conn_handle, 1,
                                        (uint8_t[]) { 0xff }, 1, 1);
    /* 0-length attribute. */
    ble_att_svr_test_misc_verify_notify(conn_handle, 43, NULL, 0, 1);

    /*** Bad notifies; verify callback is not executed. */
    /* Attribute handle of 0. */
    ble_att_svr_test_misc_verify_notify(conn_handle, 0,
                                        (uint8_t[]) { 1, 2, 3 }, 3, 0);

}

TEST_CASE(ble_att_svr_test_indicate)
{
    uint16_t conn_handle;

    conn_handle = ble_att_svr_test_misc_init(0);

    /*** Successful indicates; verify callback is executed. */
    /* 3-length attribute. */
    ble_att_svr_test_misc_verify_indicate(conn_handle, 10,
                                          (uint8_t[]) { 1, 2, 3 }, 3, 1);
    /* 1-length attribute. */
    ble_att_svr_test_misc_verify_indicate(conn_handle, 1,
                                          (uint8_t[]) { 0xff }, 1, 1);
    /* 0-length attribute. */
    ble_att_svr_test_misc_verify_indicate(conn_handle, 43, NULL, 0, 1);

    /*** Bad indicates; verify callback is not executed. */
    /* Attribute handle of 0. */
    ble_att_svr_test_misc_verify_indicate(conn_handle, 0,
                                          (uint8_t[]) { 1, 2, 3 }, 3, 0);

}

TEST_SUITE(ble_att_svr_suite)
{
    tu_suite_set_post_test_cb(ble_hs_test_util_post_test, NULL);

    ble_att_svr_test_mtu();
    ble_att_svr_test_read();
    ble_att_svr_test_read_blob();
    ble_att_svr_test_read_mult();
    ble_att_svr_test_write();
    ble_att_svr_test_find_info();
    ble_att_svr_test_find_type_value();
    ble_att_svr_test_read_type();
    ble_att_svr_test_read_group_type();
    ble_att_svr_test_prep_write();
    ble_att_svr_test_notify();
    ble_att_svr_test_indicate();
}

int
ble_att_svr_test_all(void)
{
    ble_att_svr_suite();

    return tu_any_failed;
}
