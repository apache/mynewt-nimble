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

#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <float.h>
#include "os/mynewt.h"
#include "bsp/bsp.h"

/* BLE */
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "host/ble_cs.h"
#include "host/ble_peer.h"
#include "host/util/util.h"

/* Mandatory services. */
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/ras/ble_svc_ras.h"

/* Application-specified header. */
#include "cs_initiator.h"
#include <math.h>
//#include "dsp/fast_math_functions.h"
#include "console/console.h"

static int cs_initiator_gap_event(struct ble_gap_event *event, void *arg);
static uint16_t last_conn_handle;

#define MAX_RESULTS 24
#define NUM_MODES 4
#define NUM_ROLES 2

struct mode_results {
    void *results[NUM_ROLES];
    int counts[NUM_ROLES];
    int max_count;
    size_t elem_size;
};

static struct ble_cs_mode0_result mode0_i_results[MAX_RESULTS];
static struct ble_cs_mode0_result mode0_r_results[MAX_RESULTS];
static struct ble_cs_mode1_result mode1_i_results[MAX_RESULTS];
static struct ble_cs_mode1_result mode1_r_results[MAX_RESULTS];
static struct ble_cs_mode2_result mode2_i_results[MAX_RESULTS];
static struct ble_cs_mode2_result mode2_r_results[MAX_RESULTS];
static struct ble_cs_mode3_result mode3_i_results[MAX_RESULTS];
static struct ble_cs_mode3_result mode3_r_results[MAX_RESULTS];

static struct mode_results all_results[NUM_MODES] = {
    [0] = { .results = { mode0_i_results, mode0_r_results },
            .counts = {0, 0},
            .max_count = MAX_RESULTS,
            .elem_size = sizeof(struct ble_cs_mode0_result) },
    [1] = { .results = { mode1_i_results, mode1_r_results },
            .counts = {0, 0},
            .max_count = MAX_RESULTS,
            .elem_size = sizeof(struct ble_cs_mode1_result) },
    [2] = { .results = { mode2_i_results, mode2_r_results },
            .counts = {0, 0},
            .max_count = MAX_RESULTS,
            .elem_size = sizeof(struct ble_cs_mode2_result) },
    [3] = { .results = { mode3_i_results, mode3_r_results },
            .counts = {0, 0},
            .max_count = MAX_RESULTS,
            .elem_size = sizeof(struct ble_cs_mode3_result) },
};

//arm_status arm_atan2_q15(q15_t y,q15_t x,q15_t *result);

//#define BLE_CS_MAX_STEPS        40
#define C_M_S                   299792458L  /* Seed of light [m/s] */
#define FREQ_STEP_HZ            1000000L    /* 1 MHz space between channels */
#define BASE_FREQ_HZ            2402000000L /* BLE channel 0 = 2402 MHz */
#define PI                      3.14159265358979323846f

static int32_t
cs_initiator_mode0_freq_offset_get(void)
{
    return 0; // TODO
}

static void
cs_initiator_mode1_distance_get(void)
{
    int32_t tof_ns;
    struct mode_results *res = &all_results[BLE_HS_CS_MODE1];
    struct ble_cs_mode1_result *i_result =
            (struct ble_cs_mode1_result *)res->results[BLE_HS_CS_ROLE_INITIATOR];
    struct ble_cs_mode1_result *r_result =
            (struct ble_cs_mode1_result *)res->results[BLE_HS_CS_ROLE_REFLECTOR];
    uint8_t i_n = res->counts[BLE_HS_CS_ROLE_INITIATOR];
    uint8_t r_n = res->counts[BLE_HS_CS_ROLE_REFLECTOR];
    uint8_t i;

    assert(i_n > 0 && i_n == r_n);

    LOG(INFO, "ToF = ");
    for (i = 0; i < i_n; ++i) {
        if (i_result->toa_tod == BLE_HS_CS_TOA_TOD_NOT_AVAILABLE ||
            r_result->toa_tod == BLE_HS_CS_TOA_TOD_NOT_AVAILABLE) {
            continue;
        }

        tof_ns = (i_result->toa_tod - r_result->toa_tod);
//        LOG(INFO, "ToF = %d[ns] = %d - %d\n", tof_ns, i_result->toa_tod, r_result->toa_tod);
        LOG(INFO, "%d ", tof_ns);
    }
    LOG(INFO, "\n");

    all_results[BLE_HS_CS_MODE1].counts[BLE_HS_CS_ROLE_INITIATOR] = 0;
    all_results[BLE_HS_CS_MODE1].counts[BLE_HS_CS_ROLE_REFLECTOR] = 0;
}

#define MAX_CHANNELS 80
static float phase[MAX_CHANNELS];
static uint8_t channel_ids[MAX_CHANNELS];

static int32_t
cs_initiator_mode2_i_only_distance_get(void)
{
    struct ble_cs_mode2_result *i_result;
    float d, slope_sum = 0, slope_mean;
    float I, Q;
    float delta_f;
    float offset_ph;
    float ph, diff;
    int16_t I12, Q12;
    uint8_t ch, prev_ch = 0, phase_ch_count = 0;
    uint8_t i, sample_count = 0;
    uint8_t i_n = all_results[BLE_HS_CS_MODE2].counts[BLE_HS_CS_ROLE_INITIATOR];

    for (i = 0; i < MAX_CHANNELS; ++i) phase[i] = FLT_MAX;
    memset(channel_ids, 0, sizeof(channel_ids));

    assert(i_n > 0);
    i_result = all_results[BLE_HS_CS_MODE2].results[BLE_HS_CS_ROLE_INITIATOR];

    /* Calculate phases and sort ascending by frequency */
    for (i = 0; i < i_n; ++i) {
        if (i_result[i].tone_quality_ind[0] > 2)
            continue;

        ch = i_result[i].step_channel;
        if (ch >= MAX_CHANNELS || phase[ch] != FLT_MAX)
            continue;

        I12 = (int16_t)(i_result[i].tone_pct[0] & 0xFFF);
        Q12 = (int16_t)((i_result[i].tone_pct[0] >> 12) & 0xFFF);
        if (I12 & 0x800) I12 |= 0xF000;
        if (Q12 & 0x800) Q12 |= 0xF000;

        I = (float)I12;
        Q = (float)Q12;

        /* atan2 returns values from [-π, π] */
        ph = atan2f(Q, I);

        /* unwrap phase to [0, 2π) */
        while (ph >= 2 * PI) ph -= 2 * PI;
        while (ph < 0) ph += 2 * PI;
        phase[ch] = ph;
        channel_ids[phase_ch_count] = ch;
        ++phase_ch_count;
    }

    if (phase_ch_count <= 1) {
        return 0;
    }

    /* Straighten the phases to get a linear graph instead of a sawtooth */
    offset_ph = 0;
    for (i = 1; i < phase_ch_count; ++i) {
        ch = channel_ids[i];
        ph = phase[ch];
        diff = ph - prev_ph;
        if (diff > PI) offset_ph -= 2 * PI;
        else if (diff < -PI) offset_ph += 2 * PI;
        ph += offset_ph;
        prev_ph = phase[ch];
    }

    /* Estimate the slope coefficient */
    sum_f = 0;
    sum_ph = 0;
    sum_f_ph 0;
    sum_f2 = 0;
    for (i = 0; i < phase_ch_count; ++i) {
        ch = channel_ids[i];
        ph = phase[ch];
        f = BASE_FREQ_HZ + ch * FREQ_STEP_HZ;
        sum_f += f;
        sum_ph += ph;
        sum_f_ph += f * ph;
        sum_f2 += f * f;
    }

    numerator = phase_ch_count * sum_f_ph - sum_f * sum_ph
    denominator = phase_ch_count * sum_f2 - sum_f * sum_f

    if (denominator == 0.0) {
        return 0.0
    }

    /* The slope is [rad/Hz] */
    slope = numerator / denominator;

    // float distance = ?;

    return distance;

    // [rad/Hz]
//    if (sample_count == 0)
//        return 0;

//    slope_mean = slope_sum / sample_count;

//
//    int32_t d_int = (int32_t)d;
//    int32_t d_frac = abs((int32_t)((d - d_int) * 1000));
//
//    LOG(INFO, "******\n");
//    LOG(INFO, "Distance: %d.%03d m (%u valid channels)\n", d_int, d_frac, phase_ch_count);
//    LOG(INFO, "******\n");

    return d;
}

//static int32_t
//cs_initiator_mode2_distance_get(void)
//{
//    struct ble_cs_mode2_result *i_result;
//    struct ble_cs_mode2_result *r_result;
//    uint8_t nsteps;
//    uint8_t i;
//    float d_sum = 0;
//    float d = 0;
//
//    float f1 = 0;
//    float f2;
//    float phase1_i = 0, phase2_i;
//    float phase1_r = 0, phase2_r;
//    float delta_phase_i, delta_phase_r;
//    float delta_phase_eff, delta_f;
//
//    uint8_t i_n = all_results[BLE_HS_CS_MODE2].counts[BLE_HS_CS_ROLE_INITIATOR];
//    uint8_t r_n = all_results[BLE_HS_CS_MODE2].counts[BLE_HS_CS_ROLE_REFLECTOR];
//    nsteps = min(i_n, r_n);
//
//    assert(nsteps > 1);
//
//    i_result = all_results[BLE_HS_CS_MODE2].results[BLE_HS_CS_ROLE_INITIATOR];
//    r_result = all_results[BLE_HS_CS_MODE2].results[BLE_HS_CS_ROLE_REFLECTOR];
//
//    for (i = 0; i < nsteps; ++i) {
//        f2 = 2 * PI * (BASE_FREQ_HZ + FREQ_STEP_HZ * i_result[i].step_channel);
//
//        phase2_i = phase_from_tonepct(i_result[i].tone_pct[0]);
//        phase2_r = phase_from_tonepct(r_result[i].tone_pct[0]);
//
//        if (f1 == 0) {
//            f1 = f2;
//            phase1_i = phase2_i;
//            phase1_r = phase2_r;
//            continue;
//        }
//
//        delta_phase_i = phase1_i - phase2_i;
//        delta_phase_r = phase1_r - phase2_r;
//        delta_phase_eff = (delta_phase_i - delta_phase_r) / 2.0f;
//
//        delta_f = f1 - f2;
//        d = fabsf((C_M_S * delta_phase_eff) / (2.0f * delta_f));
//        d_sum += d;
//
//        f1 = f2;
//        phase1_i = phase2_i;
//        phase1_r = phase2_r;
//    }
//
//    d = d_sum / (nsteps - 1);
//    return (int32_t)d;
//}

static int32_t
cs_initiator_mode3_distance_get(void)
{
    return 0; // TODO
}

static int
cs_initiator_procedure_summary(void)
{
    int i_mode0_count = all_results[BLE_HS_CS_MODE0].counts[BLE_HS_CS_ROLE_INITIATOR];
//    int r_mode0_count = all_results[BLE_HS_CS_MODE0].counts[BLE_HS_CS_ROLE_REFLECTOR];
    int i_mode1_count = all_results[BLE_HS_CS_MODE1].counts[BLE_HS_CS_ROLE_INITIATOR];
    int r_mode1_count = all_results[BLE_HS_CS_MODE1].counts[BLE_HS_CS_ROLE_REFLECTOR];
    int i_mode2_count = all_results[BLE_HS_CS_MODE2].counts[BLE_HS_CS_ROLE_INITIATOR];
    int r_mode2_count = all_results[BLE_HS_CS_MODE2].counts[BLE_HS_CS_ROLE_REFLECTOR];
    int i_mode3_count = all_results[BLE_HS_CS_MODE3].counts[BLE_HS_CS_ROLE_INITIATOR];
//    int r_mode3_count = all_results[BLE_HS_CS_MODE3].counts[BLE_HS_CS_ROLE_REFLECTOR];

//    if (i_mode0_count > 0) {
//        cs_initiator_mode0_freq_offset_get();
//    }
//
//    if (i_mode1_count > 0 && i_mode1_count == r_mode1_count) {
//        cs_initiator_mode1_distance_get();
//    }

    if (i_mode2_count > 0) {
//        if (i_mode2_count == r_mode2_count) {
//            cs_initiator_mode2_distance_get();
//        } else {
            cs_initiator_mode2_i_only_distance_get();
//        }
    }

//    if (i_mode3_count > 0) {
//        cs_initiator_mode3_distance_get();
//    }

    return 0;
}

static void
cs_initiator_cleanup(void)
{
    struct mode_results *res;
    uint8_t i, j;

    for (i = 0; i < ARRAY_SIZE(all_results); ++i) {
        res = &all_results[i];
        for (j = 0; j < ARRAY_SIZE(res->counts); ++j) {
            memset(res->results[j], 0, res->max_count * res->elem_size);
            res->counts[j] = 0;
        }
    }
}

static void
cs_initiator_start_cs(uint16_t conn_handle)
{
    struct ble_cs_procedure_start_params cmd;
    int rc;

    LOG(INFO, "Starting new CS procedure, conn_handle %d\n", conn_handle);

    last_conn_handle = conn_handle;

    cmd.conn_handle = conn_handle;
    rc = ble_cs_procedure_start(&cmd);
    if (rc) {
        LOG(INFO, "Failed to start new CS procedure, err %d\n", rc);
    }
}

static int
cs_initiator_procedure_complete(void)
{
    cs_initiator_procedure_summary();

    LOG(INFO, "CS procedure completed\n");

    os_time_delay(os_time_ms_to_ticks32(2000));

    cs_initiator_cleanup();
    cs_initiator_start_cs(last_conn_handle);

    return 0;
}

static int
cs_initiator_step_data_received(void *data, uint8_t step_mode, uint8_t role)
{
    struct mode_results *mode;

    if (step_mode >= NUM_MODES) {
        return -1;
    }

    mode = &all_results[step_mode];
    if (mode->counts[role] < mode->max_count) {
        memcpy((char*)mode->results[role] + mode->counts[role] * mode->elem_size,
               data, mode->elem_size);
        mode->counts[role]++;
    }

    return 0;
}

static int
cs_initiator_cs_event(struct ble_cs_event *event, void *arg)
{
    switch (event->type) {
    case BLE_CS_EVENT_CS_PROCEDURE_COMPLETE:
        cs_initiator_procedure_complete();
        break;
    case BLE_CS_EVENT_CS_STEP_DATA:
        if (event->status == 0) {
            cs_initiator_step_data_received(event->step_data.data, event->step_data.mode,
                                            event->step_data.role);
        }
        break;
    }

    return 0;
}

/**
 * Initiates the GAP general discovery procedure.
 */
static void
cs_initiator_scan(void)
{
    uint8_t own_addr_type;
    struct ble_gap_ext_disc_params params = {0};
    int rc;

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        LOG(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    params.itvl = BLE_GAP_SCAN_FAST_INTERVAL_MAX;
    params.passive = 1;
    params.window = BLE_GAP_SCAN_FAST_WINDOW;

    rc = ble_gap_ext_disc(own_addr_type, 30000, 0, 0, 0, 0, &params, NULL,
                          cs_initiator_gap_event, NULL);
    if (rc != 0) {
        LOG(ERROR, "Error initiating GAP discovery procedure; rc=%d\n", rc);
    }
}

/**
 * Indicates whether we should tre to connect to the sender of the specified
 * advertisement.  The function returns a positive result if the device
 * advertises connectability and support for the Alert Notification service.
 */
static int
cs_initiator_should_connect(const ble_addr_t *addr, const struct ble_hs_adv_fields *fields)
{
    int i;

    if (addr == NULL) {
        return 0;
    }

    if (fields->name != NULL && !memcmp((const char *)fields->name, "Pixel 9", 7)) {
        return 1;
    }

    for (i = 0; i < fields->num_uuids16; i++) {
        if (ble_uuid_u16(&fields->uuids16[i].u) == BLE_SVC_RAS_SVC_RANGING_SERVICE_UUID) {
            return 1;
        }
    }

    return 0;
}

/**
 * Connects to the sender of the specified advertisement of it looks
 * interesting.  A device is "interesting" if it advertises connectability and
 * support for the Alert Notification service.
 */
static void
cs_initiator_connect_if_interesting(const struct ble_gap_disc_desc *disc,
                                    const struct ble_gap_ext_disc_desc *ext_disc)
{
    int rc;
    const ble_addr_t *addr;
    struct ble_hs_adv_fields fields;
    uint8_t own_addr_type;
    uint8_t length_data;
    const uint8_t *data;
    struct ble_gap_conn_params conn_params = {
        .scan_itvl = 0x0010,
        .scan_window = 0x0010,
        .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN,
        .itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX,
        .latency = BLE_GAP_INITIAL_CONN_LATENCY,
        .supervision_timeout = BLE_GAP_INITIAL_SUPERVISION_TIMEOUT,
        .min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN,
        .max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN,
    };

    if (disc) {
        if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
            disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
            return;
        }
        data = disc->data;
        length_data = disc->length_data;
        addr = &disc->addr;
    } else if (ext_disc) {
        if (ext_disc->props & BLE_HCI_ADV_LEGACY_MASK &&
            (ext_disc->legacy_event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
             ext_disc->legacy_event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)) {
            return;
        } else if (!(ext_disc->props & BLE_HCI_ADV_CONN_MASK)) {
            return;
        }
        data = ext_disc->data;
        length_data = ext_disc->length_data;
        addr = &ext_disc->addr;
    } else {
        return;
    }

    rc = ble_hs_adv_parse_fields(&fields, data, length_data);
    if (rc != 0) {
        return;
    }

    /* An advertisment report was received during GAP discovery. */
//    print_adv_fields(&fields);

    /* Don't do anything if we don't care about this advertiser. */
    if (!cs_initiator_should_connect(addr, &fields)) {
        return;
    }

    /* Scanning must be stopped before a connection can be initiated. */
    rc = ble_gap_disc_cancel();
    if (rc != 0) {
        LOG(DEBUG, "Failed to cancel scan; rc=%d\n", rc);
        return;
    }

    /* Figure out address to use for connect (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        LOG(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Try to connect the the advertiser.  Allow 30 seconds (30000 ms) for
     * timeout.
     */
    if (disc) {
        rc = ble_gap_connect(own_addr_type, addr, 0, &conn_params,
                             cs_initiator_gap_event, NULL);
    } else {
        rc = ble_gap_ext_connect(own_addr_type, addr, 0, BLE_GAP_LE_PHY_1M_MASK,
                                 &conn_params, NULL, NULL, cs_initiator_gap_event, NULL);
    }

    if (rc != 0) {
        LOG(ERROR, "Error: Failed to connect to device; addr_type=%d "
            "addr=%s; rc=%d\n",
                    disc->addr.type, addr_str(disc->addr.val), rc);
        return;
    }
}

static void
cs_initiator_on_disc_complete(const struct ble_peer *peer, int status, void *arg)
{

    if (status != 0) {
        /* Service discovery failed.  Terminate the connection. */
        LOG(ERROR, "Error: Service discovery failed; status=%d "
            "conn_handle=%d\n", status, peer->conn_handle);
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }

    /* Service discovery has completed successfully.  Now we have a complete
     * list of services, characteristics, and descriptors that the peer
     * supports.
     */
    LOG(INFO, "Service discovery complete; status=%d "
        "conn_handle=%d\n", status, peer->conn_handle);

    cs_initiator_start_cs(peer->conn_handle);
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that is
 * established.  cs_initiator uses the same callback for all connections.
 *
 * @param event                 The event being signalled.
 * @param arg                   Application-specified argument; unused by
 *                                  cs_initiator.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int
cs_initiator_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_cs_setup_params cmd;
    struct ble_sm_io pk;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_EXT_DISC:
        cs_initiator_connect_if_interesting(NULL, &event->ext_disc);
        return 0;
    case BLE_GAP_EVENT_DISC:
        cs_initiator_connect_if_interesting(&event->disc, NULL);
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        if (event->connect.status == 0) {
            /* Connection successfully established. */
            LOG(INFO, "Connection established ");

            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            print_conn_desc(&desc);
            LOG(INFO, "\n");

            /* Remember peer. */
            rc = ble_peer_add(event->connect.conn_handle);
            if (rc != 0) {
                LOG(ERROR, "Failed to add peer; rc=%d\n", rc);
                return 0;
            }

            rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc) {
                LOG(INFO, "Failed to pair");
            }
        } else {
            /* Connection attempt failed; resume scanning. */
            LOG(ERROR, "Error: Connection failed; status=%d\n",
                event->connect.status);
            cs_initiator_scan();
        }

        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        /* Connection terminated. */
        LOG(INFO, "disconnect; reason=%d ", event->disconnect.reason);
        print_conn_desc(&event->disconnect.conn);
        LOG(INFO, "\n");

        /* Forget about peer. */
        ble_peer_delete(event->disconnect.conn.conn_handle);

        /* Resume scanning. */
        cs_initiator_scan();

        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        LOG(INFO, "discovery complete; reason=%d\n",
            event->disc_complete.reason);
        return 0;

    case BLE_GAP_EVENT_PAIRING_COMPLETE:
        LOG(INFO, "received pairing complete: "
            "conn_handle=%d status=%d\n",
            event->pairing_complete.conn_handle,
            event->pairing_complete.status);

        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        LOG(INFO, "encryption change event; status=%d ",
            event->enc_change.status);

        if (event->enc_change.status != 0) {
            return 0;
        }

        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        print_conn_desc(&desc);

        cmd.conn_handle = event->connect.conn_handle;
        cmd.cb = cs_initiator_cs_event;
        cmd.cb_arg = NULL;
        cmd.local_role = BLE_HS_CS_ROLE_INITIATOR;
        ble_cs_setup(&cmd);

        /* Perform service discovery. */
        rc = ble_peer_disc_all(event->connect.conn_handle,
                               cs_initiator_on_disc_complete, NULL);
        if (rc != 0) {
            LOG(ERROR, "Failed to discover services; rc=%d\n", rc);
            return 0;
        }

        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        /* Peer sent us a notification or indication. */
        LOG(DEBUG, "received %s; conn_handle=%d attr_handle=%d "
            "attr_len=%d\n",
            event->notify_rx.indication ?
            "indication" :
            "notification",
            event->notify_rx.conn_handle,
            event->notify_rx.attr_handle,
            OS_MBUF_PKTLEN(event->notify_rx.om));

        /* Attribute data is contained in event->notify_rx.attr_data. */
        return 0;

    case BLE_GAP_EVENT_MTU:
        LOG(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
            event->mtu.conn_handle,
            event->mtu.channel_id,
            event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */

//        /* Delete the old bond. */
//        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
//        assert(rc == 0);
//        ble_store_util_delete_peer(&desc.peer_id_addr);

        /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
         * continue with the pairing operation.
         */
        return BLE_GAP_REPEAT_PAIRING_RETRY;
#if MYNEWT_VAL(BLE_SM_SC)
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        LOG(INFO, "passkey action event; action=%d", event->passkey.params.action);
        if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            LOG(INFO, " numcmp=%lu", (unsigned long) event->passkey.params.numcmp);

            pk.action = BLE_SM_IOACT_NUMCMP;
            pk.numcmp_accept = 1;
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pk);
            assert(rc == 0);
        }
        return 0;
#endif
    default:
        return 0;
    }
}

static void
cs_initiator_on_reset(int reason)
{
    LOG(ERROR, "Resetting state; reason=%d\n", reason);
}

static void
cs_initiator_on_sync(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Begin scanning for a peripheral to connect to. */
    cs_initiator_scan();
}

/**
 * main
 *
 * All application logic and NimBLE host work is performed in default task.
 *
 * @return int NOTE: this function should never return!
 */
int
mynewt_main(int argc, char **argv)
{
    int rc;

    /* Initialize OS */
    sysinit();

    rc = modlog_register(MODLOG_MODULE_APP, log_console_get(),
                         LOG_LEVEL_DEBUG, NULL);
    assert(rc == 0);

    LOG(DEBUG, "Started CS Initiator\n");

    /* Configure the host. */
    ble_hs_cfg.reset_cb = cs_initiator_on_reset;
    ble_hs_cfg.sync_cb = cs_initiator_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
#if MYNEWT_VAL(BLE_SM_SC)
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_YESNO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
#endif

    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set("nimble-cs_initiator");
    assert(rc == 0);

    /* os start should never return. If it does, this should be an error */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
