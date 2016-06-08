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
#include "testutil/testutil.h"
#include "nimble/ble.h"
#include "host/ble_uuid.h"
#include "host/ble_hs_test.h"
#include "ble_hs_test_util.h"
#include "ble_hs_test_util_store.h"

#define BLE_GATTS_NOTIFY_TEST_CHR_1_UUID    0x1111
#define BLE_GATTS_NOTIFY_TEST_CHR_2_UUID    0x2222

static uint8_t ble_gatts_peer_addr[6] = {2,3,4,5,6,7};

static int
ble_gatts_notify_test_misc_access(uint16_t conn_handle,
                                  uint16_t attr_handle, uint8_t op,
                                  union ble_gatt_access_ctxt *ctxt,
                                  void *arg);
static void
ble_gatts_notify_test_misc_reg_cb(uint8_t op,
                                  union ble_gatt_register_ctxt *ctxt,
                                  void *arg);

static const struct ble_gatt_svc_def ble_gatts_notify_test_svcs[] = { {
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid128 = BLE_UUID16(0x1234),
    .characteristics = (struct ble_gatt_chr_def[]) { {
        .uuid128 = BLE_UUID16(BLE_GATTS_NOTIFY_TEST_CHR_1_UUID),
        .access_cb = ble_gatts_notify_test_misc_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY |
                 BLE_GATT_CHR_F_INDICATE,
    }, {
        .uuid128 = BLE_UUID16(BLE_GATTS_NOTIFY_TEST_CHR_2_UUID),
        .access_cb = ble_gatts_notify_test_misc_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY |
                 BLE_GATT_CHR_F_INDICATE,
    }, {
        0
    } },
}, {
    0
} };


static uint16_t ble_gatts_notify_test_chr_1_def_handle;
static uint8_t ble_gatts_notify_test_chr_1_val[1024];
static int ble_gatts_notify_test_chr_1_len;
static uint16_t ble_gatts_notify_test_chr_2_def_handle;
static uint8_t ble_gatts_notify_test_chr_2_val[1024];
static int ble_gatts_notify_test_chr_2_len;

typedef int ble_store_write_fn(int obj_type, union ble_store_value *val);

typedef int ble_store_delete_fn(int obj_type, union ble_store_key *key);

static uint16_t
ble_gatts_notify_test_misc_read_notify(uint16_t conn_handle,
                                       uint16_t chr_def_handle)
{
    struct ble_att_read_req req;
    struct os_mbuf *om;
    uint8_t buf[BLE_ATT_READ_REQ_SZ];
    uint16_t flags;
    int rc;

    req.barq_handle = chr_def_handle + 2;
    ble_att_read_req_write(buf, sizeof buf, &req);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue_pullup();
    TEST_ASSERT_FATAL(om != NULL);
    TEST_ASSERT_FATAL(om->om_len == 3);
    TEST_ASSERT_FATAL(om->om_data[0] == BLE_ATT_OP_READ_RSP);

    flags = le16toh(om->om_data + 1);
    return flags;
}

static void
ble_gatts_notify_test_misc_enable_notify(uint16_t conn_handle,
                                         uint16_t chr_def_handle,
                                         uint16_t flags)
{
    struct ble_att_write_req req;
    uint8_t buf[BLE_ATT_WRITE_REQ_BASE_SZ + 2];
    int rc;

    req.bawq_handle = chr_def_handle + 2;
    ble_att_write_req_write(buf, sizeof buf, &req);

    htole16(buf + BLE_ATT_WRITE_REQ_BASE_SZ, flags);
    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
}

static void
ble_gatts_notify_test_misc_init(uint16_t *out_conn_handle, int bonding,
                                uint16_t chr1_flags, uint16_t chr2_flags)
{
    struct ble_hs_conn *conn;
    uint16_t flags;
    int exp_num_cccds;
    int rc;

    ble_hs_test_util_init();

    ble_hs_test_util_store_init(10, 10, 10);
    ble_hs_cfg.store_read_cb = ble_hs_test_util_store_read;
    ble_hs_cfg.store_write_cb = ble_hs_test_util_store_write;

    rc = ble_gatts_register_svcs(ble_gatts_notify_test_svcs,
                                 ble_gatts_notify_test_misc_reg_cb, NULL);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT_FATAL(ble_gatts_notify_test_chr_1_def_handle != 0);
    TEST_ASSERT_FATAL(ble_gatts_notify_test_chr_2_def_handle != 0);

    ble_gatts_start();

    ble_hs_test_util_create_conn(2, ble_gatts_peer_addr, NULL, NULL);
    *out_conn_handle = 2;

    if (bonding) {
        ble_hs_lock();
        conn = ble_hs_conn_find(2);
        TEST_ASSERT_FATAL(conn != NULL);
        conn->bhc_sec_state.encrypted = 1;
        conn->bhc_sec_state.authenticated = 1;
        conn->bhc_sec_state.bonded = 1;
        ble_hs_unlock();
    }

    /* Ensure notifications disabled on new connection. */
    flags = ble_gatts_notify_test_misc_read_notify(
        2, ble_gatts_notify_test_chr_1_def_handle);
    TEST_ASSERT(flags == 0);
    flags = ble_gatts_notify_test_misc_read_notify(
        2, ble_gatts_notify_test_chr_2_def_handle);
    TEST_ASSERT(flags == 0);

    /* Set initial notification / indication state. */
    if (chr1_flags != 0) {
        ble_gatts_notify_test_misc_enable_notify(
            2, ble_gatts_notify_test_chr_1_def_handle, chr1_flags);
    }
    if (chr2_flags != 0) {
        ble_gatts_notify_test_misc_enable_notify(
            2, ble_gatts_notify_test_chr_2_def_handle, chr2_flags);
    }

    /* Toss both write responses. */
    ble_hs_test_util_prev_tx_queue_clear();

    /* Ensure notification / indication state reads back correctly. */
    flags = ble_gatts_notify_test_misc_read_notify(
        2, ble_gatts_notify_test_chr_1_def_handle);
    TEST_ASSERT(flags == chr1_flags);
    flags = ble_gatts_notify_test_misc_read_notify(
        2, ble_gatts_notify_test_chr_2_def_handle);
    TEST_ASSERT(flags == chr2_flags);

    /* Ensure both CCCDs still persisted. */
    exp_num_cccds = (chr1_flags != 0) + (chr2_flags != 0);
    TEST_ASSERT(ble_hs_test_util_store_num_cccds == exp_num_cccds);
}

static void
ble_gatts_restore_bonding(uint16_t conn_handle)
{
    struct ble_hs_conn *conn;

    ble_hs_lock();
    conn = ble_hs_conn_find(conn_handle);
    TEST_ASSERT_FATAL(conn != NULL);
    conn->bhc_sec_state.encrypted = 1;
    conn->bhc_sec_state.authenticated = 1;
    conn->bhc_sec_state.bonded = 1;
    ble_hs_unlock();

    ble_gatts_bonding_restored(conn_handle);
}

static void
ble_gatts_notify_test_misc_reg_cb(uint8_t op,
                                  union ble_gatt_register_ctxt *ctxt,
                                  void *arg)
{
    uint16_t uuid16;

    if (op == BLE_GATT_REGISTER_OP_CHR) {
        uuid16 = ble_uuid_128_to_16(ctxt->chr_reg.chr->uuid128);
        switch (uuid16) {
        case BLE_GATTS_NOTIFY_TEST_CHR_1_UUID:
            ble_gatts_notify_test_chr_1_def_handle = ctxt->chr_reg.def_handle;
            break;

        case BLE_GATTS_NOTIFY_TEST_CHR_2_UUID:
            ble_gatts_notify_test_chr_2_def_handle = ctxt->chr_reg.def_handle;
            break;

        default:
            TEST_ASSERT_FATAL(0);
            break;
        }
    }
}

static int
ble_gatts_notify_test_misc_access(uint16_t conn_handle,
                                  uint16_t attr_handle, uint8_t op,
                                  union ble_gatt_access_ctxt *ctxt,
                                  void *arg)
{
    TEST_ASSERT_FATAL(op == BLE_GATT_ACCESS_OP_READ_CHR);
    TEST_ASSERT(conn_handle == 0xffff);

    if (attr_handle == ble_gatts_notify_test_chr_1_def_handle + 1) {
        TEST_ASSERT(ctxt->chr_access.chr ==
                    &ble_gatts_notify_test_svcs[0].characteristics[0]);
        ctxt->chr_access.data = ble_gatts_notify_test_chr_1_val;
        ctxt->chr_access.len = ble_gatts_notify_test_chr_1_len;
    } else if (attr_handle == ble_gatts_notify_test_chr_2_def_handle + 1) {
        TEST_ASSERT(ctxt->chr_access.chr ==
                    &ble_gatts_notify_test_svcs[0].characteristics[1]);
        ctxt->chr_access.data = ble_gatts_notify_test_chr_2_val;
        ctxt->chr_access.len = ble_gatts_notify_test_chr_2_len;
    } else {
        TEST_ASSERT(0);
    }

    return 0;
}

static void
ble_gatts_notify_test_misc_rx_indicate_rsp(uint16_t conn_handle)
{
    uint8_t buf[BLE_ATT_INDICATE_RSP_SZ];
    int rc;

    ble_att_indicate_rsp_write(buf, sizeof buf);

    rc = ble_hs_test_util_l2cap_rx_payload_flat(conn_handle, BLE_L2CAP_CID_ATT,
                                                buf, sizeof buf);
    TEST_ASSERT(rc == 0);
}


static void
ble_gatts_notify_test_misc_verify_tx_n(uint8_t *attr_data, int attr_len)
{
    struct ble_att_notify_req req;
    struct os_mbuf *om;
    int i;

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue_pullup();
    TEST_ASSERT_FATAL(om != NULL);

    ble_att_notify_req_parse(om->om_data, om->om_len, &req);

    for (i = 0; i < attr_len; i++) {
        TEST_ASSERT(om->om_data[BLE_ATT_NOTIFY_REQ_BASE_SZ + i] ==
                    attr_data[i]);
    }
}

static void
ble_gatts_notify_test_misc_verify_tx_i(uint8_t *attr_data, int attr_len)
{
    struct ble_att_indicate_req req;
    struct os_mbuf *om;
    int i;

    ble_hs_test_util_tx_all();

    om = ble_hs_test_util_prev_tx_dequeue_pullup();
    TEST_ASSERT_FATAL(om != NULL);

    ble_att_indicate_req_parse(om->om_data, om->om_len, &req);

    for (i = 0; i < attr_len; i++) {
        TEST_ASSERT(om->om_data[BLE_ATT_INDICATE_REQ_BASE_SZ + i] ==
                    attr_data[i]);
    }
}

TEST_CASE(ble_gatts_notify_test_n)
{
    uint16_t conn_handle;
    uint16_t flags;

    ble_gatts_notify_test_misc_init(&conn_handle, 0, 0, 0);

    /* Enable notifications on both characteristics. */
    ble_gatts_notify_test_misc_enable_notify(
        conn_handle, ble_gatts_notify_test_chr_1_def_handle,
        BLE_GATTS_CLT_CFG_F_NOTIFY);
    ble_gatts_notify_test_misc_enable_notify(
        conn_handle, ble_gatts_notify_test_chr_2_def_handle,
        BLE_GATTS_CLT_CFG_F_NOTIFY);

    /* Toss both write responses. */
    ble_hs_test_util_prev_tx_queue_clear();

    /* Ensure nothing got persisted since peer is not bonded. */
    TEST_ASSERT(ble_hs_test_util_store_num_cccds == 0);

    /* Ensure notifications read back as enabled. */
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_1_def_handle);
    TEST_ASSERT(flags == BLE_GATTS_CLT_CFG_F_NOTIFY);
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_2_def_handle);
    TEST_ASSERT(flags == BLE_GATTS_CLT_CFG_F_NOTIFY);

    /* Update characteristic 1's value. */
    ble_gatts_notify_test_chr_1_len = 1;
    ble_gatts_notify_test_chr_1_val[0] = 0xab;
    ble_gatts_chr_updated(ble_gatts_notify_test_chr_1_def_handle + 1);

    /* Verify notification sent properly. */
    ble_gatts_notify_test_misc_verify_tx_n(ble_gatts_notify_test_chr_1_val,
                                           ble_gatts_notify_test_chr_1_len);

    /* Update characteristic 2's value. */
    ble_gatts_notify_test_chr_2_len = 16;
    memcpy(ble_gatts_notify_test_chr_2_val,
           ((uint8_t[]){0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}), 16);
    ble_gatts_chr_updated(ble_gatts_notify_test_chr_2_def_handle + 1);

    /* Verify notification sent properly. */
    ble_gatts_notify_test_misc_verify_tx_n(ble_gatts_notify_test_chr_2_val,
                                           ble_gatts_notify_test_chr_2_len);

    /***
     * Disconnect, modify characteristic values, and reconnect.  Ensure
     * notifications are not sent and are no longer enabled.
     */

    ble_hs_test_util_conn_disconnect(conn_handle);

    /* Update characteristic 1's value. */
    ble_gatts_notify_test_chr_1_len = 1;
    ble_gatts_notify_test_chr_1_val[0] = 0xdd;
    ble_gatts_chr_updated(ble_gatts_notify_test_chr_1_def_handle + 1);

    /* Update characteristic 2's value. */
    ble_gatts_notify_test_chr_2_len = 16;
    memcpy(ble_gatts_notify_test_chr_2_val,
           ((uint8_t[]){1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}), 16);
    ble_gatts_chr_updated(ble_gatts_notify_test_chr_2_def_handle + 1);

    ble_hs_test_util_create_conn(conn_handle, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);

    /* Ensure no notifications sent. */
    TEST_ASSERT(ble_hs_test_util_prev_tx_dequeue() == NULL);

    /* Ensure notifications disabled. */
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_1_def_handle);
    TEST_ASSERT(flags == 0);
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_2_def_handle);
    TEST_ASSERT(flags == 0);
}

TEST_CASE(ble_gatts_notify_test_i)
{
    uint16_t conn_handle;
    uint16_t flags;

    ble_gatts_notify_test_misc_init(&conn_handle, 0, 0, 0);

    /* Enable indications on both characteristics. */
    ble_gatts_notify_test_misc_enable_notify(
        conn_handle, ble_gatts_notify_test_chr_1_def_handle,
        BLE_GATTS_CLT_CFG_F_INDICATE);
    ble_gatts_notify_test_misc_enable_notify(
        conn_handle, ble_gatts_notify_test_chr_2_def_handle,
        BLE_GATTS_CLT_CFG_F_INDICATE);

    /* Toss both write responses. */
    ble_hs_test_util_prev_tx_queue_clear();

    /* Ensure nothing got persisted since peer is not bonded. */
    TEST_ASSERT(ble_hs_test_util_store_num_cccds == 0);

    /* Ensure indications read back as enabled. */
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_1_def_handle);
    TEST_ASSERT(flags == BLE_GATTS_CLT_CFG_F_INDICATE);
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_2_def_handle);
    TEST_ASSERT(flags == BLE_GATTS_CLT_CFG_F_INDICATE);

    /* Update characteristic 1's value. */
    ble_gatts_notify_test_chr_1_len = 1;
    ble_gatts_notify_test_chr_1_val[0] = 0xab;
    ble_gatts_chr_updated(ble_gatts_notify_test_chr_1_def_handle + 1);

    /* Verify indication sent properly. */
    ble_gatts_notify_test_misc_verify_tx_i(ble_gatts_notify_test_chr_1_val,
                                           ble_gatts_notify_test_chr_1_len);

    /* Update characteristic 2's value. */
    ble_gatts_notify_test_chr_2_len = 16;
    memcpy(ble_gatts_notify_test_chr_2_val,
           ((uint8_t[]){0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}), 16);
    ble_gatts_chr_updated(ble_gatts_notify_test_chr_2_def_handle + 1);

    /* Verify the second indication doesn't get sent until the first is
     * confirmed.
     */
    ble_hs_test_util_tx_all();
    TEST_ASSERT(ble_hs_test_util_prev_tx_queue_sz() == 0);

    /* Receive the confirmation for the first indication. */
    ble_gatts_notify_test_misc_rx_indicate_rsp(conn_handle);

    /* Verify indication sent properly. */
    ble_hs_test_util_tx_all();
    ble_gatts_notify_test_misc_verify_tx_i(ble_gatts_notify_test_chr_2_val,
                                           ble_gatts_notify_test_chr_2_len);

    /* Receive the confirmation for the second indication. */
    ble_gatts_notify_test_misc_rx_indicate_rsp(conn_handle);

    /* Verify no pending GATT jobs. */
    TEST_ASSERT(!ble_gattc_any_jobs());

    /***
     * Disconnect, modify characteristic values, and reconnect.  Ensure
     * indications are not sent and are no longer enabled.
     */

    ble_hs_test_util_conn_disconnect(conn_handle);

    /* Update characteristic 1's value. */
    ble_gatts_notify_test_chr_1_len = 1;
    ble_gatts_notify_test_chr_1_val[0] = 0xdd;
    ble_gatts_chr_updated(ble_gatts_notify_test_chr_1_def_handle + 1);

    /* Update characteristic 2's value. */
    ble_gatts_notify_test_chr_2_len = 16;
    memcpy(ble_gatts_notify_test_chr_2_val,
           ((uint8_t[]){1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}), 16);
    ble_gatts_chr_updated(ble_gatts_notify_test_chr_2_def_handle + 1);

    ble_hs_test_util_create_conn(conn_handle, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);

    /* Ensure no indications sent. */
    TEST_ASSERT(ble_hs_test_util_prev_tx_dequeue() == NULL);

    /* Ensure indications disabled. */
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_1_def_handle);
    TEST_ASSERT(flags == 0);
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_2_def_handle);
    TEST_ASSERT(flags == 0);
}

TEST_CASE(ble_gatts_notify_test_bonded_n)
{
    uint16_t conn_handle;
    uint16_t flags;

    ble_gatts_notify_test_misc_init(&conn_handle, 1,
                                    BLE_GATTS_CLT_CFG_F_NOTIFY,
                                    BLE_GATTS_CLT_CFG_F_NOTIFY);

    /* Disconnect. */
    ble_hs_test_util_conn_disconnect(conn_handle);

    /* Ensure both CCCDs still persisted. */
    TEST_ASSERT(ble_hs_test_util_store_num_cccds == 2);

    /* Update characteristic 1's value. */
    ble_gatts_notify_test_chr_1_len = 1;
    ble_gatts_notify_test_chr_1_val[0] = 0xdd;
    ble_gatts_chr_updated(ble_gatts_notify_test_chr_1_def_handle + 1);

    /* Update characteristic 2's value. */
    ble_gatts_notify_test_chr_2_len = 16;
    memcpy(ble_gatts_notify_test_chr_2_val,
           ((uint8_t[]){1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}), 16);
    ble_gatts_chr_updated(ble_gatts_notify_test_chr_2_def_handle + 1);

    /* Reconnect; ensure notifications don't get sent while unbonded and that
     * notifications appear disabled.
     */

    ble_hs_test_util_create_conn(conn_handle, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);

    /* Ensure no notifications sent. */
    TEST_ASSERT(ble_hs_test_util_prev_tx_dequeue() == NULL);

    /* Ensure notifications disabled. */
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_1_def_handle);
    TEST_ASSERT(flags == 0);
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_2_def_handle);
    TEST_ASSERT(flags == 0);

    /* Simulate a successful encryption procedure (bonding restoration). */
    ble_gatts_restore_bonding(conn_handle);

    /* Verify notifications sent properly. */
    ble_gatts_notify_test_misc_verify_tx_n(ble_gatts_notify_test_chr_1_val,
                                           ble_gatts_notify_test_chr_1_len);
    ble_gatts_notify_test_misc_verify_tx_n(ble_gatts_notify_test_chr_2_val,
                                           ble_gatts_notify_test_chr_2_len);

    /* Ensure notifications enabled. */
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_1_def_handle);
    TEST_ASSERT(flags == BLE_GATTS_CLT_CFG_F_NOTIFY);
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_2_def_handle);
    TEST_ASSERT(flags == BLE_GATTS_CLT_CFG_F_NOTIFY);

    /* Ensure both CCCDs still persisted. */
    TEST_ASSERT(ble_hs_test_util_store_num_cccds == 2);
}

TEST_CASE(ble_gatts_notify_test_bonded_i)
{
    uint16_t conn_handle;
    uint16_t flags;

    ble_gatts_notify_test_misc_init(&conn_handle, 1,
                                    BLE_GATTS_CLT_CFG_F_INDICATE,
                                    BLE_GATTS_CLT_CFG_F_INDICATE);

    /* Disconnect. */
    ble_hs_test_util_conn_disconnect(conn_handle);

    /* Ensure both CCCDs still persisted. */
    TEST_ASSERT(ble_hs_test_util_store_num_cccds == 2);

    /* Update characteristic 1's value. */
    ble_gatts_notify_test_chr_1_len = 1;
    ble_gatts_notify_test_chr_1_val[0] = 0xab;
    ble_gatts_chr_updated(ble_gatts_notify_test_chr_1_def_handle + 1);

    /* Update characteristic 2's value. */
    ble_gatts_notify_test_chr_2_len = 16;
    memcpy(ble_gatts_notify_test_chr_2_val,
           ((uint8_t[]){0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}), 16);
    ble_gatts_chr_updated(ble_gatts_notify_test_chr_2_def_handle + 1);

    /* Reconnect; ensure notifications don't get sent while unbonded and that
     * notifications appear disabled.
     */

    ble_hs_test_util_create_conn(conn_handle, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);

    /* Ensure no indications sent. */
    TEST_ASSERT(ble_hs_test_util_prev_tx_dequeue() == NULL);

    /* Ensure notifications disabled. */
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_1_def_handle);
    TEST_ASSERT(flags == 0);
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_2_def_handle);
    TEST_ASSERT(flags == 0);

    /* Simulate a successful encryption procedure (bonding restoration). */
    ble_gatts_restore_bonding(conn_handle);

    /* Verify first indication sent properly. */
    ble_gatts_notify_test_misc_verify_tx_i(ble_gatts_notify_test_chr_1_val,
                                           ble_gatts_notify_test_chr_1_len);

    /* Verify the second indication doesn't get sent until the first is
     * confirmed.
     */
    ble_hs_test_util_tx_all();
    TEST_ASSERT(ble_hs_test_util_prev_tx_queue_sz() == 0);

    /* Receive the confirmation for the first indication. */
    ble_gatts_notify_test_misc_rx_indicate_rsp(conn_handle);

    /* Verify indication sent properly. */
    ble_hs_test_util_tx_all();
    ble_gatts_notify_test_misc_verify_tx_i(ble_gatts_notify_test_chr_2_val,
                                           ble_gatts_notify_test_chr_2_len);

    /* Receive the confirmation for the second indication. */
    ble_gatts_notify_test_misc_rx_indicate_rsp(conn_handle);

    /* Verify no pending GATT jobs. */
    TEST_ASSERT(!ble_gattc_any_jobs());

    /* Ensure notifications enabled. */
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_1_def_handle);
    TEST_ASSERT(flags == BLE_GATTS_CLT_CFG_F_INDICATE);
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_2_def_handle);
    TEST_ASSERT(flags == BLE_GATTS_CLT_CFG_F_INDICATE);

    /* Ensure both CCCDs still persisted. */
    TEST_ASSERT(ble_hs_test_util_store_num_cccds == 2);
}

TEST_CASE(ble_gatts_notify_test_bonded_i_no_ack)
{
    struct ble_store_value_cccd value_cccd;
    struct ble_store_key_cccd key_cccd;
    uint16_t conn_handle;
    uint16_t flags;
    int rc;

    ble_gatts_notify_test_misc_init(&conn_handle, 1,
                                    BLE_GATTS_CLT_CFG_F_INDICATE, 0);

    /* Update characteristic 1's value. */
    ble_gatts_notify_test_chr_1_len = 1;
    ble_gatts_notify_test_chr_1_val[0] = 0xab;
    ble_gatts_chr_updated(ble_gatts_notify_test_chr_1_def_handle + 1);

    /* Verify indication sent properly. */
    ble_hs_test_util_tx_all();
    ble_gatts_notify_test_misc_verify_tx_i(ble_gatts_notify_test_chr_1_val,
                                           ble_gatts_notify_test_chr_1_len);

    /* Verify 'updated' state is still persisted. */
    key_cccd.peer_addr_type = BLE_STORE_ADDR_TYPE_NONE;
    key_cccd.chr_val_handle = ble_gatts_notify_test_chr_1_def_handle + 1;
    key_cccd.idx = 0;

    rc = ble_store_read_cccd(&key_cccd, &value_cccd);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(value_cccd.value_changed);

    /* Disconnect. */
    ble_hs_test_util_conn_disconnect(conn_handle);

    /* Ensure CCCD still persisted. */
    TEST_ASSERT(ble_hs_test_util_store_num_cccds == 1);

    /* Reconnect. */
    ble_hs_test_util_create_conn(conn_handle, ((uint8_t[]){2,3,4,5,6,7,8,9}),
                                 NULL, NULL);

    /* Simulate a successful encryption procedure (bonding restoration). */
    ble_gatts_restore_bonding(conn_handle);

    /* Verify indication sent properly. */
    ble_gatts_notify_test_misc_verify_tx_i(ble_gatts_notify_test_chr_1_val,
                                           ble_gatts_notify_test_chr_1_len);

    /* Receive the confirmation for the indication. */
    ble_gatts_notify_test_misc_rx_indicate_rsp(conn_handle);

    /* Verify no pending GATT jobs. */
    TEST_ASSERT(!ble_gattc_any_jobs());

    /* Ensure indication enabled. */
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_1_def_handle);
    TEST_ASSERT(flags == BLE_GATTS_CLT_CFG_F_INDICATE);
    flags = ble_gatts_notify_test_misc_read_notify(
        conn_handle, ble_gatts_notify_test_chr_2_def_handle);
    TEST_ASSERT(flags == 0);

    /* Ensure CCCD still persisted. */
    TEST_ASSERT(ble_hs_test_util_store_num_cccds == 1);

    /* Verify 'updated' state is no longer persisted. */
    rc = ble_store_read_cccd(&key_cccd, &value_cccd);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(!value_cccd.value_changed);
}

TEST_SUITE(ble_gatts_notify_suite)
{
    ble_gatts_notify_test_n();
    ble_gatts_notify_test_i();

    ble_gatts_notify_test_bonded_n();
    ble_gatts_notify_test_bonded_i();

    ble_gatts_notify_test_bonded_i_no_ack();

    /* XXX: Test corner cases:
     *     o Bonding after CCCD configuration.
     *     o Disconnect prior to rx of indicate ack.
     */
}

int
ble_gatts_notify_test_all(void)
{
    ble_gatts_notify_suite();

    return tu_any_failed;
}
