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
#include <errno.h>
#include "testutil/testutil.h"
#include "nimble/ble.h"
#include "host/ble_uuid.h"
#include "ble_hs_test.h"
#include "ble_hs_test_util.h"
#include "../src/ble_gatt_priv.h"

#define BLE_GATTS_TEST_CL_SUP_FEAT_CHR_UUID 0x1111
#define BLE_GATT_TEST_CL_SUP_FEAT_CHR_SZ    1

/* Client supported features bit positions*/
#define BLE_GATT_TEST_CLI_SUP_FEAT_ROBUST_CACHING_BIT 0x00
#define BLE_GATT_TEST_CLI_SUP_FEAT_EATT_BIT           0x01
#define BLE_GATT_TEST_CLI_SUP_FEAT_MULT_NTF_BIT       0x02

/* --- Client Supported Features masks --- */
#define BLE_GATT_TEST_CLI_SUP_FEAT_NONE_MASK            0x00
#define BLE_GATT_TEST_CLI_SUP_FEAT_ROBUST_CACHING_MASK  0x01
#define BLE_GATT_TEST_CLI_SUP_FEAT_EATT_MASK            0x02
#define BLE_GATT_TEST_CLI_SUP_FEAT_MULT_NTF_MASK        0x04
#define BLE_GATT_TEST_CLI_SUP_FEAT_ROBUST_EATT_MASK     0x03
#define BLE_GATT_TEST_CLI_SUP_FEAT_ROBUST_MULT_NTF_MASK 0x05
#define BLE_GATT_TEST_CLI_SUP_FEAT_EATT_MULT_NTF_MASK   0x06
#define BLE_GATT_TEST_CLI_SUP_FEAT_ALL_MASK             0x07

static uint8_t ble_gatts_cl_sup_feat_test_peer_addr[6] = { 1, 2, 3, 4, 5, 6 };

static int
ble_gatts_cl_sup_feat_test_misc_access(uint16_t conn_handle, uint16_t attr_handle,
                                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t supported_feat;
    int rc;

    TEST_ASSERT(conn_handle != 0xffff);

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        rc = ble_gatts_peer_cl_sup_feat_get(conn_handle, &supported_feat, 1);
        if (rc == 0) {
            rc = os_mbuf_append(ctxt->om, &supported_feat, 1);
        }
        return rc;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        rc = ble_gatts_peer_cl_sup_feat_update(conn_handle, ctxt->om);
        return rc;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def ble_gatts_cl_sup_feat_test_svcs[] = {
    {
     .type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID16_DECLARE(0x1234),
     .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = BLE_UUID16_DECLARE(BLE_GATTS_TEST_CL_SUP_FEAT_CHR_UUID),
                    .access_cb = ble_gatts_cl_sup_feat_test_misc_access,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                },
                { 0 } },
     },
    { 0 }
};

static uint16_t ble_gatts_cl_sup_feat_test_chr_def_handle;
static uint16_t ble_gatts_cl_sup_feat_test_chr_val_handle;
static uint8_t ble_gatts_cl_sup_feat_test_chr_val[BLE_GATT_TEST_CL_SUP_FEAT_CHR_SZ] = { 0 };

static void
ble_gatts_notify_test_misc_reg_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    uint16_t uuid16;

    if (ctxt->op == BLE_GATT_REGISTER_OP_CHR) {
        uuid16 = ble_uuid_u16(ctxt->chr.chr_def->uuid);
        switch (uuid16) {
        case BLE_GATTS_TEST_CL_SUP_FEAT_CHR_UUID:
            ble_gatts_cl_sup_feat_test_chr_def_handle = ctxt->chr.def_handle;
            ble_gatts_cl_sup_feat_test_chr_val_handle = ctxt->chr.val_handle;
            break;

        default:
            TEST_ASSERT_FATAL(0);
            break;
        }
    }
}

static void
ble_gatts_cl_sup_feat_test_misc_init(uint16_t *out_conn_handle, int bonding)
{
    ble_hs_test_util_init();

    ble_hs_test_util_reg_svcs(ble_gatts_cl_sup_feat_test_svcs,
                              ble_gatts_notify_test_misc_reg_cb, NULL);
    TEST_ASSERT_FATAL(ble_gatts_cl_sup_feat_test_chr_def_handle != 0);

    ble_hs_test_util_create_conn(1, ble_gatts_cl_sup_feat_test_peer_addr, NULL, NULL);
    *out_conn_handle = 1;
}

static void
ble_gatts_cl_sup_feat_test_perform_bonding(uint16_t conn_handle)
{
    struct ble_hs_conn *conn;

    ble_hs_lock();
    conn = ble_hs_conn_find(conn_handle);
    TEST_ASSERT_FATAL(conn != NULL);
    conn->bhc_sec_state.encrypted = 1;
    conn->bhc_sec_state.authenticated = 1;
    conn->bhc_sec_state.bonded = 1;
    ble_hs_unlock();
}

TEST_CASE_SELF(ble_gatts_cl_sup_feat_test_bond_then_write)
{
    uint16_t conn_handle = 1;
    uint16_t conn_handle_2 = 2;
    uint8_t out_feat = 0;
    int rc;

    ble_gatts_cl_sup_feat_test_misc_init(&conn_handle, 0);

    ble_gatts_cl_sup_feat_test_perform_bonding(conn_handle);
    ble_gatts_bonding_established(conn_handle);

    /* Peer with trusted relationship enables EATT in Client Supported
     * Features Characteristic */
    ble_gatts_cl_sup_feat_test_chr_val[0] = BLE_GATT_TEST_CLI_SUP_FEAT_EATT_MASK;
    ble_hs_test_util_rx_att_write_req(
        conn_handle, ble_gatts_cl_sup_feat_test_chr_val_handle,
        ble_gatts_cl_sup_feat_test_chr_val, BLE_GATT_TEST_CL_SUP_FEAT_CHR_SZ);

    ble_hs_test_util_conn_disconnect(conn_handle);
    ble_hs_test_util_create_conn(conn_handle_2,
                                 ble_gatts_cl_sup_feat_test_peer_addr, NULL, NULL);

    ble_gatts_cl_sup_feat_test_perform_bonding(conn_handle_2);
    ble_gatts_bonding_restored(conn_handle_2);

    /* Verify Client Supported Features value is persisted for this peer after
     * reconnection.*/
    rc = ble_gatts_peer_cl_sup_feat_get(conn_handle_2, &out_feat, 1);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(out_feat == BLE_GATT_TEST_CLI_SUP_FEAT_EATT_MASK);
}

TEST_CASE_SELF(ble_gatts_cl_sup_feat_test_write_then_bond)
{
    uint16_t conn_handle = 1;
    uint16_t new_conn_handle = 2;
    uint8_t out_feat = 0;
    int rc;

    ble_gatts_cl_sup_feat_test_misc_init(&conn_handle, 0);

    /* Peer enables Robust Caching in Client Supported Features Characteristic
     * prior to bonding*/
    ble_gatts_cl_sup_feat_test_chr_val[0] =
        BLE_GATT_TEST_CLI_SUP_FEAT_ROBUST_CACHING_MASK;
    ble_hs_test_util_rx_att_write_req(
        conn_handle, ble_gatts_cl_sup_feat_test_chr_val_handle,
        ble_gatts_cl_sup_feat_test_chr_val, BLE_GATT_TEST_CL_SUP_FEAT_CHR_SZ);

    /* Bonding should result in maintaining last known Client Supported
     * Features value for this peer */
    ble_gatts_cl_sup_feat_test_perform_bonding(conn_handle);
    ble_gatts_bonding_established(conn_handle);

    ble_hs_test_util_conn_disconnect(conn_handle);
    ble_hs_test_util_create_conn(new_conn_handle,
                                 ble_gatts_cl_sup_feat_test_peer_addr, NULL, NULL);

    ble_gatts_cl_sup_feat_test_perform_bonding(new_conn_handle);
    ble_gatts_bonding_restored(new_conn_handle);

    /* Verify Client Supported Features value is persisted for this peer after
     * reconnection.*/
    rc = ble_gatts_peer_cl_sup_feat_get(new_conn_handle, &out_feat, 1);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(out_feat == BLE_GATT_TEST_CLI_SUP_FEAT_ROBUST_CACHING_MASK);
}

TEST_CASE_SELF(ble_gatts_cl_sup_feat_test_unbonded_no_persist)
{
    uint16_t conn_handle = 1;
    uint16_t new_conn_handle = 2;
    uint8_t out_feat = 0xFF; /* Initialize to dummy value */
    int rc;

    ble_gatts_cl_sup_feat_test_misc_init(&conn_handle, 0);

    /* Peer enables EATT in Client Supported Features Characteristic */
    ble_gatts_cl_sup_feat_test_chr_val[0] = BLE_GATT_TEST_CLI_SUP_FEAT_EATT_MASK;
    ble_hs_test_util_rx_att_write_req(
        conn_handle, ble_gatts_cl_sup_feat_test_chr_val_handle,
        ble_gatts_cl_sup_feat_test_chr_val, BLE_GATT_TEST_CL_SUP_FEAT_CHR_SZ);

    /* Disconnect (there was no bonding) */
    ble_hs_test_util_conn_disconnect(conn_handle);

    ble_hs_test_util_create_conn(new_conn_handle,
                                 ble_gatts_cl_sup_feat_test_peer_addr, NULL, NULL);

    /* Verify Client Supported Features value wasn't persisted */
    rc = ble_gatts_peer_cl_sup_feat_get(new_conn_handle, &out_feat, 1);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(out_feat == 0x00);
}

TEST_CASE_SELF(ble_gatts_cl_sup_feat_test_enable_new_feature_across_sessions)
{
    uint16_t conn_handle = 1;
    uint16_t conn_handle_2 = 2;
    uint16_t conn_handle_3 = 3;
    uint8_t out_feat = 0;
    int rc;

    ble_gatts_cl_sup_feat_test_misc_init(&conn_handle, 0);

    ble_gatts_cl_sup_feat_test_perform_bonding(conn_handle);
    ble_gatts_bonding_established(conn_handle);

    /* Peer with trusted relationship enables EATT and MULT NTF in Client
     * Supported Features Characteristic */
    ble_gatts_cl_sup_feat_test_chr_val[0] =
        BLE_GATT_TEST_CLI_SUP_FEAT_EATT_MULT_NTF_MASK;
    ble_hs_test_util_rx_att_write_req(
        conn_handle, ble_gatts_cl_sup_feat_test_chr_val_handle,
        ble_gatts_cl_sup_feat_test_chr_val, BLE_GATT_TEST_CL_SUP_FEAT_CHR_SZ);

    ble_hs_test_util_conn_disconnect(conn_handle);
    ble_hs_test_util_create_conn(conn_handle_2,
                                 ble_gatts_cl_sup_feat_test_peer_addr, NULL, NULL);

    ble_gatts_cl_sup_feat_test_perform_bonding(conn_handle_2);
    ble_gatts_bonding_restored(conn_handle_2);

    /* After rebonding, peer enables all features in Client Supported Features
     * Characteristic */
    ble_gatts_cl_sup_feat_test_chr_val[0] = BLE_GATT_TEST_CLI_SUP_FEAT_ALL_MASK;
    ble_hs_test_util_rx_att_write_req(
        conn_handle_2, ble_gatts_cl_sup_feat_test_chr_val_handle,
        ble_gatts_cl_sup_feat_test_chr_val, BLE_GATT_TEST_CL_SUP_FEAT_CHR_SZ);

    ble_hs_test_util_conn_disconnect(conn_handle_2);
    ble_hs_test_util_create_conn(conn_handle_3,
                                 ble_gatts_cl_sup_feat_test_peer_addr, NULL, NULL);

    ble_gatts_cl_sup_feat_test_perform_bonding(conn_handle_3);
    ble_gatts_bonding_restored(conn_handle_3);

    /* Verify Client Supported Features value is persisted for this peer after
     * reconnection i.e. all features should be enabled. */
    rc = ble_gatts_peer_cl_sup_feat_get(conn_handle_3, &out_feat, 1);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(out_feat == BLE_GATT_TEST_CLI_SUP_FEAT_ALL_MASK);
}

TEST_CASE_SELF(ble_gatts_cl_sup_feat_test_reject_disabling_already_enabled)
{
    uint16_t conn_handle = 1;
    uint16_t new_conn_handle = 2;
    uint8_t out_feat = 0;
    int rc;

    ble_gatts_cl_sup_feat_test_misc_init(&conn_handle, 0);

    ble_gatts_cl_sup_feat_test_perform_bonding(conn_handle);
    ble_gatts_bonding_established(conn_handle);

    /* Peer with trusted relationship enables EATT and MULT NTF in Client
     * Supported Features Characteristic */
    ble_gatts_cl_sup_feat_test_chr_val[0] =
        BLE_GATT_TEST_CLI_SUP_FEAT_EATT_MULT_NTF_MASK;
    ble_hs_test_util_rx_att_write_req(
        conn_handle, ble_gatts_cl_sup_feat_test_chr_val_handle,
        ble_gatts_cl_sup_feat_test_chr_val, BLE_GATT_TEST_CL_SUP_FEAT_CHR_SZ);

    ble_hs_test_util_conn_disconnect(conn_handle);
    ble_hs_test_util_create_conn(new_conn_handle,
                                 ble_gatts_cl_sup_feat_test_peer_addr, NULL, NULL);

    ble_gatts_cl_sup_feat_test_perform_bonding(new_conn_handle);
    ble_gatts_bonding_restored(new_conn_handle);

    /* Attempt to disable EATT feature */
    ble_gatts_cl_sup_feat_test_chr_val[0] = BLE_GATT_TEST_CLI_SUP_FEAT_MULT_NTF_BIT;
    ble_hs_test_util_rx_att_write_req(
        new_conn_handle, ble_gatts_cl_sup_feat_test_chr_val_handle,
        ble_gatts_cl_sup_feat_test_chr_val, BLE_GATT_TEST_CL_SUP_FEAT_CHR_SZ);

    /* Verify original features from storage weren't overwritten */
    rc = ble_gatts_peer_cl_sup_feat_get(new_conn_handle, &out_feat, 1);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(out_feat == BLE_GATT_TEST_CLI_SUP_FEAT_EATT_MULT_NTF_MASK);
}

TEST_SUITE(ble_gatts_cl_sup_feat_suite)
{
    ble_gatts_cl_sup_feat_test_bond_then_write();
    ble_gatts_cl_sup_feat_test_write_then_bond();
    ble_gatts_cl_sup_feat_test_unbonded_no_persist();
    ble_gatts_cl_sup_feat_test_enable_new_feature_across_sessions();
    ble_gatts_cl_sup_feat_test_reject_disabling_already_enabled();
}
