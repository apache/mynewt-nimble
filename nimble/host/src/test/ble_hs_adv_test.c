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

#include <stddef.h>
#include <errno.h>
#include <string.h>
#include "nimble/hci_common.h"
#include "ble_hs_priv.h"
#include "host/ble_hs_test.h"
#include "host/host_hci.h"
#include "ble_l2cap.h"
#include "ble_att_priv.h"
#include "ble_hs_conn.h"
#include "ble_hs_adv_priv.h"
#include "ble_hci_ack.h"
#include "ble_hci_sched.h"
#include "ble_gap_priv.h"
#include "ble_hs_test_util.h"
#include "testutil/testutil.h"

#define BLE_ADV_TEST_DATA_OFF   4

static void
ble_hs_adv_test_misc_verify_tx_adv_data_hdr(int data_len)
{
    uint16_t opcode;
    uint8_t *sptr;

    TEST_ASSERT(ble_hs_test_util_prev_hci_tx != NULL);

    sptr = ble_hs_test_util_prev_hci_tx;

    opcode = le16toh(sptr + 0);
    TEST_ASSERT(BLE_HCI_OGF(opcode) == BLE_HCI_OGF_LE);
    TEST_ASSERT(BLE_HCI_OCF(opcode) == BLE_HCI_OCF_LE_SET_ADV_DATA);

    TEST_ASSERT(sptr[2] == data_len + 1);
    TEST_ASSERT(sptr[3] == data_len);
}

static void
ble_hs_adv_test_misc_verify_tx_field(int off, uint8_t type, uint8_t val_len,
                                     void *val)
{
    uint8_t *sptr;

    TEST_ASSERT_FATAL(ble_hs_test_util_prev_hci_tx != NULL);

    sptr = ble_hs_test_util_prev_hci_tx + off;

    TEST_ASSERT(sptr[0] == val_len + 1);
    TEST_ASSERT(sptr[1] == type);
    TEST_ASSERT(memcmp(sptr + 2, val, val_len) == 0);
}

struct ble_hs_adv_test_field {
    uint8_t type;       /* 0 indicates end of array. */
    uint8_t *val;
    uint8_t val_len;
};

static int
ble_hs_adv_test_misc_calc_data_len(struct ble_hs_adv_test_field *fields)
{
    struct ble_hs_adv_test_field *field;
    int len;

    len = 0;
    for (field = fields; field->type != 0; field++) {
        len += 2 + field->val_len;
    }

    return len;
}

static void
ble_hs_adv_test_misc_verify_tx_fields(int off,
                                      struct ble_hs_adv_test_field *fields)
{
    struct ble_hs_adv_test_field *field;

    for (field = fields; field->type != 0; field++) {
        ble_hs_adv_test_misc_verify_tx_field(off, field->type, field->val_len,
                                          field->val);
        off += 2 + field->val_len;
    }
}

static void
ble_hs_adv_test_misc_verify_tx_data(struct ble_hs_adv_test_field *fields)
{
    int data_len;

    data_len = ble_hs_adv_test_misc_calc_data_len(fields);
    ble_hs_adv_test_misc_verify_tx_adv_data_hdr(data_len);
    ble_hs_adv_test_misc_verify_tx_fields(BLE_ADV_TEST_DATA_OFF, fields);
}

static void
ble_hs_adv_test_misc_tx_and_verify_data(
    uint8_t disc_mode, struct ble_hs_adv_fields *adv_fields,
    struct ble_hs_adv_test_field *test_fields)
{
    int rc;

    ble_hs_test_util_init();

    rc = ble_gap_conn_set_adv_fields(adv_fields);
    TEST_ASSERT_FATAL(rc == 0);

    rc = ble_gap_conn_adv_start(disc_mode, BLE_GAP_CONN_MODE_UND, NULL, 0,
                                NULL, NULL);
    TEST_ASSERT_FATAL(rc == 0);

    ble_hs_test_util_rx_und_adv_acks_count(3);
    ble_hs_adv_test_misc_verify_tx_data(test_fields);
}

TEST_CASE(ble_hs_adv_test_case_flags)
{
    struct ble_hs_adv_fields fields;

    memset(&fields, 0, sizeof fields);

    /* Default flags. */
    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /* Flags |= limited discoverable. */
    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_LTD, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]) {
                    BLE_HS_ADV_F_DISC_LTD | BLE_HS_ADV_F_BREDR_UNSUP
                 },
                .val_len = 1,
            }, {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /* Flags = general discoverable. */
    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_GEN, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]) {
                    BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP
                 },
                .val_len = 1,
            }, {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });
}

TEST_CASE(ble_hs_adv_test_case_user)
{
    struct ble_hs_adv_fields fields;

    /*** Complete 16-bit service class UUIDs. */
    memset(&fields, 0, sizeof fields);
    fields.uuids16 = (uint16_t[]) { 0x0001, 0x1234, 0x54ab };
    fields.num_uuids16 = 3;
    fields.uuids16_is_complete = 1;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_COMP_UUIDS16,
                .val = (uint8_t[]) { 0x01, 0x00, 0x34, 0x12, 0xab, 0x54 },
                .val_len = 6,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** Incomplete 16-bit service class UUIDs. */
    memset(&fields, 0, sizeof fields);
    fields.uuids16 = (uint16_t[]) { 0x0001, 0x1234, 0x54ab };
    fields.num_uuids16 = 3;
    fields.uuids16_is_complete = 0;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_INCOMP_UUIDS16,
                .val = (uint8_t[]) { 0x01, 0x00, 0x34, 0x12, 0xab, 0x54 },
                .val_len = 6,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** Complete 32-bit service class UUIDs. */
    memset(&fields, 0, sizeof fields);
    fields.uuids32 = (uint32_t[]) { 0x12345678, 0xabacadae };
    fields.num_uuids32 = 2;
    fields.uuids32_is_complete = 1;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_COMP_UUIDS32,
                .val = (uint8_t[]) { 0x78,0x56,0x34,0x12,0xae,0xad,0xac,0xab },
                .val_len = 8,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** Incomplete 32-bit service class UUIDs. */
    memset(&fields, 0, sizeof fields);
    fields.uuids32 = (uint32_t[]) { 0x12345678, 0xabacadae };
    fields.num_uuids32 = 2;
    fields.uuids32_is_complete = 0;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_INCOMP_UUIDS32,
                .val = (uint8_t[]) { 0x78,0x56,0x34,0x12,0xae,0xad,0xac,0xab },
                .val_len = 8,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** Complete 128-bit service class UUIDs. */
    memset(&fields, 0, sizeof fields);
    fields.uuids128 = (uint8_t[]) {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    };
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_COMP_UUIDS128,
                .val = (uint8_t[]) {
                    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
                },
                .val_len = 16,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** Incomplete 128-bit service class UUIDs. */
    memset(&fields, 0, sizeof fields);
    fields.uuids128 = (uint8_t[]) {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    };
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 0;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_INCOMP_UUIDS128,
                .val = (uint8_t[]) {
                    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
                },
                .val_len = 16,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** Complete name. */
    memset(&fields, 0, sizeof fields);
    fields.name = (uint8_t *)"myname";
    fields.name_len = 6;
    fields.name_is_complete = 1;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_COMP_NAME,
                .val = (uint8_t*)"myname",
                .val_len = 6,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** Incomplete name. */
    memset(&fields, 0, sizeof fields);
    fields.name = (uint8_t *)"myname";
    fields.name_len = 6;
    fields.name_is_complete = 0;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_INCOMP_NAME,
                .val = (uint8_t*)"myname",
                .val_len = 6,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** Class of device. */
    memset(&fields, 0, sizeof fields);
    fields.device_class = (uint8_t[]){ 1,2,3 };

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_DEVICE_CLASS,
                .val = (uint8_t[]) { 1,2,3 },
                .val_len = BLE_HS_ADV_DEVICE_CLASS_LEN,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** Slave interval range. */
    memset(&fields, 0, sizeof fields);
    fields.slave_itvl_range = (uint8_t[]){ 1,2,3,4 };

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_SLAVE_ITVL_RANGE,
                .val = (uint8_t[]) { 1,2,3,4 },
                .val_len = BLE_HS_ADV_SLAVE_ITVL_RANGE_LEN,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** 0x16 - Service data - 16-bit UUID. */
    memset(&fields, 0, sizeof fields);
    fields.svc_data_uuid16 = (uint8_t[]){ 1,2,3,4 };
    fields.svc_data_uuid16_len = 4;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_SVC_DATA_UUID16,
                .val = (uint8_t[]) { 1,2,3,4 },
                .val_len = 4,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** 0x17 - Public target address. */
    memset(&fields, 0, sizeof fields);
    fields.public_tgt_addr = (uint8_t[]){ 1,2,3,4,5,6, 6,5,4,3,2,1 };
    fields.num_public_tgt_addrs = 2;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_PUBLIC_TGT_ADDR,
                .val = (uint8_t[]){ 1,2,3,4,5,6, 6,5,4,3,2,1 },
                .val_len = 2 * BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** 0x19 - Appearance. */
    memset(&fields, 0, sizeof fields);
    fields.appearance = 0x1234;
    fields.appearance_is_present = 1;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_APPEARANCE,
                .val = (uint8_t[]){ 0x34, 0x12 },
                .val_len = BLE_HS_ADV_APPEARANCE_LEN,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** 0x1a - Advertising interval. */
    memset(&fields, 0, sizeof fields);
    fields.adv_itvl = 0x1234;
    fields.adv_itvl_is_present = 1;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_ADV_ITVL,
                .val = (uint8_t[]){ 0x34, 0x12 },
                .val_len = BLE_HS_ADV_ADV_ITVL_LEN,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** 0x1b - LE bluetooth device address. */
    memset(&fields, 0, sizeof fields);
    fields.le_addr = (uint8_t[]){ 1,2,3,4,5,6,7 };

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_LE_ADDR,
                .val = (uint8_t[]) { 1,2,3,4,5,6,7 },
                .val_len = BLE_HS_ADV_LE_ADDR_LEN,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** 0x1c - LE role. */
    memset(&fields, 0, sizeof fields);
    fields.le_role = BLE_HS_ADV_LE_ROLE_BOTH_PERIPH_PREF;
    fields.le_role_is_present = 1;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_LE_ROLE,
                .val = (uint8_t[]) { BLE_HS_ADV_LE_ROLE_BOTH_PERIPH_PREF },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** 0x20 - Service data - 32-bit UUID. */
    memset(&fields, 0, sizeof fields);
    fields.svc_data_uuid32 = (uint8_t[]){ 1,2,3,4,5 };
    fields.svc_data_uuid32_len = 5;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_SVC_DATA_UUID32,
                .val = (uint8_t[]) { 1,2,3,4,5 },
                .val_len = 5,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** 0x21 - Service data - 128-bit UUID. */
    memset(&fields, 0, sizeof fields);
    fields.svc_data_uuid128 =
        (uint8_t[]){ 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18 };
    fields.svc_data_uuid128_len = 18;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_SVC_DATA_UUID128,
                .val = (uint8_t[]){ 1,2,3,4,5,6,7,8,9,10,
                                    11,12,13,14,15,16,17,18 },
                .val_len = 18,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** 0x24 - URI. */
    memset(&fields, 0, sizeof fields);
    fields.uri = (uint8_t[]){ 1,2,3,4 };
    fields.uri_len = 4;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_URI,
                .val = (uint8_t[]) { 1,2,3,4 },
                .val_len = 4,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });

    /*** 0xff - Manufacturer specific data. */
    memset(&fields, 0, sizeof fields);
    fields.mfg_data = (uint8_t[]){ 1,2,3,4 };
    fields.mfg_data_len = 4;

    ble_hs_adv_test_misc_tx_and_verify_data(BLE_GAP_DISC_MODE_NON, &fields,
        (struct ble_hs_adv_test_field[]) {
            {
                .type = BLE_HS_ADV_TYPE_MFG_DATA,
                .val = (uint8_t[]) { 1,2,3,4 },
                .val_len = 4,
            },
            {
                .type = BLE_HS_ADV_TYPE_FLAGS,
                .val = (uint8_t[]){ BLE_HS_ADV_F_BREDR_UNSUP },
                .val_len = 1,
            },
            {
                .type = BLE_HS_ADV_TYPE_TX_PWR_LVL,
                .val = (uint8_t[]){ 0x00 },
                .val_len = 1,
            },
            { 0 },
        });
}

TEST_SUITE(ble_hs_adv_test_suite)
{
    ble_hs_adv_test_case_flags();
    ble_hs_adv_test_case_user();
}

int
ble_hs_adv_test_all(void)
{
    ble_hs_adv_test_suite();

    return tu_any_failed;
}

