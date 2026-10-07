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

#include "testutil/testutil.h"

#include "os/os_mbuf.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "audio/ble_audio.h"
#include "audio/ble_audio_scan_delegator.h"
#include "services/bass/ble_audio_svc_bass.h"

#define BASS_OP_REMOTE_SCAN_STOPPED 0x00
#define BASS_OP_REMOTE_SCAN_STARTED 0x01
#define BASS_OP_ADD_SOURCE          0x02
#define BASS_OP_MODIFY_SOURCE       0x03
#define BASS_OP_REMOVE_SOURCE       0x05

static uint16_t bass_cp_handle;

static int
bass_test_action_fn(struct ble_audio_scan_delegator_action *action, void *arg)
{
    return 0;
}

/* Needs to be called at the beginning of each test case as sysinit is called
 * before each test case.
 */
static void
bass_test_init(void)
{
    int rc;

    rc = ble_gatts_start();
    TEST_ASSERT_FATAL(rc == 0);

    rc = ble_gatts_find_chr(BLE_UUID16_DECLARE(BLE_SVC_AUDIO_BASS_UUID16),
                            BLE_UUID16_DECLARE(BLE_SVC_AUDIO_BASS_CHR_UUID16_BASS_CP),
                            NULL, &bass_cp_handle);
    TEST_ASSERT_FATAL(rc == 0);

    /* Callback is not cleared by sysinit */
    rc = ble_audio_scan_delegator_action_fn_set(bass_test_action_fn, NULL);
    TEST_ASSERT_FATAL(rc == 0 || rc == BLE_HS_EALREADY);
}

static int
bass_test_cp_write_om(struct os_mbuf *om)
{
    return ble_att_svr_write_local(bass_cp_handle, om);
}

static int
bass_test_cp_write(const uint8_t *data, uint16_t len)
{
    struct os_mbuf *om;

    om = ble_hs_mbuf_from_flat(data, len);
    TEST_ASSERT_FATAL(om != NULL);

    return bass_test_cp_write_om(om);
}

static int
bass_test_add_source(uint8_t num_subgroups, uint8_t metadata_len)
{
    uint8_t buf[256];
    uint16_t len = 0;
    int i;

    buf[len++] = BASS_OP_ADD_SOURCE;
    buf[len++] = BLE_ADDR_PUBLIC;
    memset(&buf[len], 0x11, 6);
    len += 6;
    buf[len++] = 0x00; /* Advertising_SID */
    put_le24(&buf[len], 0x123456);
    len += 3;
    buf[len++] = 0x00; /* PA_Sync: do not synchronize */
    put_le16(&buf[len], 0xffff);
    len += 2;
    buf[len++] = num_subgroups;

    for (i = 0; i < num_subgroups; i++) {
        TEST_ASSERT_FATAL(len + 5 + metadata_len <= sizeof(buf));
        put_le32(&buf[len], 0);
        len += 4;
        buf[len++] = metadata_len;
        memset(&buf[len], 0xaa, metadata_len);
        len += metadata_len;
    }

    return bass_test_cp_write(buf, len);
}

static int
bass_test_source_id_first(uint8_t *source_id)
{
    int i;

    for (i = 0; i < MYNEWT_VAL(BLE_SVC_AUDIO_BASS_RECEIVE_STATE_MAX); i++) {
        if (ble_svc_audio_bass_source_id_get(i, source_id) == 0) {
            return 0;
        }
    }

    return BLE_HS_ENOENT;
}

static void
bass_test_remove_all(void)
{
    uint8_t buf[2];
    uint8_t source_id;
    int rc;
    int i;

    for (i = 0; i < MYNEWT_VAL(BLE_SVC_AUDIO_BASS_RECEIVE_STATE_MAX); i++) {
        if (ble_svc_audio_bass_source_id_get(i, &source_id) != 0) {
            continue;
        }

        buf[0] = BASS_OP_REMOVE_SOURCE;
        buf[1] = source_id;
        rc = bass_test_cp_write(buf, sizeof(buf));
        TEST_ASSERT(rc == 0);
    }
}

TEST_CASE_SELF(ble_audio_bass_test_cp_empty_write)
{
    struct os_mbuf *om;
    int rc;

    bass_test_init();

    /* Leave valid opcode in buffer but write zero length value */
    om = ble_hs_mbuf_from_flat((uint8_t[]){ BASS_OP_REMOTE_SCAN_STOPPED }, 1);
    TEST_ASSERT_FATAL(om != NULL);
    os_mbuf_adj(om, -1);
    TEST_ASSERT_FATAL(OS_MBUF_PKTLEN(om) == 0);

    rc = bass_test_cp_write_om(om);
    TEST_ASSERT(rc != 0);
}

TEST_CASE_SELF(ble_audio_bass_test_cp_unknown_opcode)
{
    uint8_t buf[32] = { 0 };
    int rc;
    int op;

    bass_test_init();

    /* Sanity check that writes reach Control Point */
    buf[0] = BASS_OP_REMOTE_SCAN_STARTED;
    rc = bass_test_cp_write(buf, 1);
    TEST_ASSERT(rc == 0);

    for (op = BASS_OP_REMOVE_SOURCE + 1; op <= 0xff; op++) {
        buf[0] = op;
        rc = bass_test_cp_write(buf, sizeof(buf));
        TEST_ASSERT(rc != 0);
    }
}

TEST_CASE_SELF(ble_audio_bass_test_add_source_metadata_len)
{
    struct ble_svc_audio_bass_receiver_state *state;
    uint16_t rcv_state_handle;
    struct os_mbuf *om;
    uint8_t source_id;
    int rc;

    bass_test_init();

    rc = ble_gatts_find_chr(
        BLE_UUID16_DECLARE(BLE_SVC_AUDIO_BASS_UUID16),
        BLE_UUID16_DECLARE(BLE_SVC_AUDIO_BASS_CHR_UUID16_BROADCAST_RECEIVE_STATE),
        NULL, &rcv_state_handle);
    TEST_ASSERT_FATAL(rc == 0);

    /* Metadata longer than what can be stored */
    rc = bass_test_add_source(1, MYNEWT_VAL(BLE_SVC_AUDIO_BASS_METADATA_MAX_SZ) + 8);
    TEST_ASSERT_FATAL(rc == 0);

    rc = bass_test_source_id_first(&source_id);
    TEST_ASSERT_FATAL(rc == 0);

    rc = ble_svc_audio_bass_receiver_state_get(source_id, &state);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(state->num_subgroups == 1);
    TEST_ASSERT(state->subgroups[0].metadata_length <=
                MYNEWT_VAL(BLE_SVC_AUDIO_BASS_METADATA_MAX_SZ));

    /* Receive State value: 15 bytes of fixed fields and subgroup with
     * BIS_Sync, Metadata_Length and Metadata
     */
    rc = ble_att_svr_read_local(rcv_state_handle, &om);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(OS_MBUF_PKTLEN(om) <=
                15 + 5 + MYNEWT_VAL(BLE_SVC_AUDIO_BASS_METADATA_MAX_SZ));
    os_mbuf_free_chain(om);

    bass_test_remove_all();
}

TEST_SUITE(ble_audio_bass_test_suite)
{
    ble_audio_bass_test_cp_empty_write();
    ble_audio_bass_test_cp_unknown_opcode();
    ble_audio_bass_test_add_source_metadata_len();
}
