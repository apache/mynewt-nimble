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

#include <inttypes.h>
#include "syscfg/syscfg.h"
#include "os/os_mbuf.h"
#include "host/ble_hs.h"
#include "host/ble_cs.h"
#include "host/ble_uuid.h"
#include "nimble/hci_common.h"
#include "sys/queue.h"
#include "services/ras/ble_svc_ras.h"

#define MBUF_POOL_BUF_SIZE  MYNEWT_VAL(BLE_SVC_RAS_TXRX_BUF_SIZE)
#define MBUF_POOL_BUF_COUNT MYNEWT_VAL(BLE_SVC_RAS_TXRX_BUF_COUNT)

static const ble_uuid16_t ble_svc_ras_ranging_service_uuid =
    BLE_UUID16_INIT(BLE_SVC_RAS_SVC_RANGING_SERVICE_UUID);

static const ble_uuid16_t ble_svc_ras_features_uuid =
    BLE_UUID16_INIT(BLE_SVC_RAS_CHR_RAS_FEATURES_UUID);

static const ble_uuid16_t ble_svc_ras_real_time_ranging_data_uuid =
    BLE_UUID16_INIT(BLE_SVC_RAS_CHR_REAL_TIME_RANGING_DATA_UUID);

static const ble_uuid16_t ble_svc_ras_on_demand_ranging_data_uuid =
    BLE_UUID16_INIT(BLE_SVC_RAS_CHR_ON_DEMAND_RANGING_DATA_UUID);

static const ble_uuid16_t ble_svc_ras_control_point_uuid =
    BLE_UUID16_INIT(BLE_SVC_RAS_CHR_RAS_CONTROL_POINT_UUID);

static const ble_uuid16_t ble_svc_ras_ranging_data_ready_uuid =
    BLE_UUID16_INIT(BLE_SVC_RAS_CHR_RANGING_DATA_READY_UUID);

static const ble_uuid16_t ble_svc_ras_ranging_data_overwritten_uuid =
    BLE_UUID16_INIT(BLE_SVC_RAS_CHR_RANGING_DATA_OVERWRITTEN_UUID);

static uint16_t ble_svc_ras_features_val_handle;
static uint16_t ble_svc_ras_real_time_ranging_data_val_handle;
static uint16_t ble_svc_ras_on_demand_ranging_data_val_handle;
static uint16_t ble_svc_ras_control_point_val_handle;
static uint16_t ble_svc_ras_ranging_data_ready_val_handle;
static uint16_t ble_svc_ras_ranging_data_overwritten_val_handle;

static struct os_mempool ras_mempool;
static struct os_mbuf_pool ras_mbuf_pool;
static uint8_t ras_mbuf_area[MBUF_POOL_BUF_COUNT * MBUF_POOL_BUF_SIZE];
static struct ble_gap_event_listener ble_svc_ras_listener;

#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

#define NOTIFY_REAL_TIME_RANGING_DATA   (0)
#define NOTIFY_ON_DEMAND_RANGING_DATA   (1)
#define NOTIFY_RANGING_DATA_READY       (2)
#define NOTIFY_RANGING_DATA_OVERWRITTEN (3)
#define NOTIFY_INVALID                  (255)

#define INDICATE_REAL_TIME_RANGING_DATA   (0)
#define INDICATE_ON_DEMAND_RANGING_DATA   (1)
#define INDICATE_CONTROL_POINT            (2)
#define INDICATE_RANGING_DATA_READY       (3)
#define INDICATE_RANGING_DATA_OVERWRITTEN (4)
#define INDICATE_INVALID                  (255)

struct ble_svc_ras_sm {
    int status;
    struct os_mbuf *ranging_data_body_mbuf;
    uint8_t *cur_subevent_header_ptr;
    uint8_t notify;
    uint8_t indicate;
    uint32_t supported_features;
    uint16_t data_overwritten_counter;
    uint16_t ranging_counter;
    uint16_t filter_bitmask[4];
    uint16_t sent_data_len;
    uint16_t remaining_data_len;
    uint16_t conn_handle;
    uint16_t mtu;
    uint8_t rolling_segment_counter;
    uint8_t segment_counter;
    uint8_t busy;
};

static struct ble_svc_ras_sm g_ble_ras_sm[MYNEWT_VAL(BLE_MAX_CONNECTIONS)];

int ble_att_conn_chan_find(uint16_t conn_handle, uint16_t cid,
                           struct ble_hs_conn **out_conn,
                           struct ble_l2cap_chan **out_chan);
uint16_t ble_att_chan_mtu(const struct ble_l2cap_chan *chan);

static struct ble_svc_ras_sm *
ble_svc_ras_sm_get(uint16_t conn_handle)
{
    struct ble_svc_ras_sm *rassm = NULL;
    uint8_t i;

    for (i = 0; i < ARRAY_SIZE(g_ble_ras_sm); i++) {
        if (g_ble_ras_sm[i].conn_handle == conn_handle) {
            rassm = &g_ble_ras_sm[i];
            break;
        }
    }

    return rassm;
}

static void
ble_svc_ras_flush(struct ble_svc_ras_sm *rassm)
{
    if (rassm->ranging_data_body_mbuf != NULL) {
        os_mbuf_free_chain(rassm->ranging_data_body_mbuf);
        rassm->ranging_data_body_mbuf = NULL;
        rassm->cur_subevent_header_ptr = NULL;
    }
}

int
ble_svc_ras_ranging_data_body_init(uint16_t conn_handle,
                                   uint16_t procedure_counter, uint8_t config_id,
                                   uint8_t tx_power, uint8_t antenna_paths_mask)
{
    struct ble_svc_ras_sm *rassm;
    struct os_mbuf *om;
    uint8_t *buf;

    rassm = ble_svc_ras_sm_get(conn_handle);
    assert(rassm != NULL);

    ble_svc_ras_flush(rassm);

    om = os_mbuf_get_pkthdr(&ras_mbuf_pool, 0);
    assert(om != NULL);
    rassm->ranging_counter = procedure_counter & 0x0FFF;
    rassm->ranging_data_body_mbuf = om;

    buf = os_mbuf_extend(om, 4);
    assert(buf != NULL);

    put_le16(buf, ((uint16_t)config_id << 12) | (rassm->ranging_counter & 0x0FFF));
    buf[2] = tx_power;
    buf[3] = antenna_paths_mask;

    return 0;
}

int
ble_svc_ras_ranging_subevent_init(uint16_t conn_handle, uint16_t start_acl_conn_event,
                                  uint16_t frequency_compensation,
                                  uint8_t ranging_done_status,
                                  uint8_t subevent_done_status,
                                  uint8_t ranging_abort_reason,
                                  uint8_t subevent_abort_reason,
                                  uint8_t reference_power_level,
                                  uint8_t number_of_steps_reported)
{
    struct ble_svc_ras_sm *rassm;
    struct os_mbuf *om;
    uint8_t *buf;

    rassm = ble_svc_ras_sm_get(conn_handle);
    assert(rassm != NULL);

    om = rassm->ranging_data_body_mbuf;
    buf = os_mbuf_extend(om, 8);
    assert(buf != NULL);

    put_le16(buf, start_acl_conn_event);
    put_le16(buf + 2, frequency_compensation);
    buf[4] = ranging_done_status | subevent_done_status << 4;
    buf[5] = ranging_abort_reason | subevent_abort_reason << 4;
    buf[6] = reference_power_level;
    buf[7] = number_of_steps_reported;

    rassm->cur_subevent_header_ptr = buf;

    return 0;
}

int
ble_svc_ras_ranging_subevent_update_status(uint16_t conn_handle,
                                           uint8_t number_of_steps_reported,
                                           uint8_t ranging_done_status,
                                           uint8_t subevent_done_status,
                                           uint8_t ranging_abort_reason,
                                           uint8_t subevent_abort_reason)
{
    struct ble_svc_ras_sm *rassm;
    uint8_t *buf;

    rassm = ble_svc_ras_sm_get(conn_handle);
    assert(rassm != NULL);

    buf = rassm->cur_subevent_header_ptr;
    assert(buf != NULL);

    buf[4] = ranging_done_status | subevent_done_status << 4;
    buf[5] = ranging_abort_reason | subevent_abort_reason << 4;
    buf[7] += number_of_steps_reported;

    return 0;
}

int
ble_svc_ras_add_step_mode(uint16_t conn_handle, uint8_t mode, uint8_t status)
{
    int rc;
    struct ble_svc_ras_sm *rassm;
    struct os_mbuf *om;
    uint8_t step_mode = mode | (status << 7);

    rassm = ble_svc_ras_sm_get(conn_handle);
    assert(rassm != NULL);

    om = rassm->ranging_data_body_mbuf;

    rc = os_mbuf_append(om, &step_mode, sizeof(step_mode));
    assert(rc == 0);

    return rc;
}

int
ble_svc_ras_add_mode0_result(struct ble_cs_mode0_result *result,
                             uint16_t conn_handle, uint8_t local_role)
{
    int rc;
    struct ble_svc_ras_sm *rassm;
    struct os_mbuf *om;
    uint8_t buf[5];
    uint8_t i = 0;
    uint16_t filter;

    rassm = ble_svc_ras_sm_get(conn_handle);
    assert(rassm != NULL);

    om = rassm->ranging_data_body_mbuf;
    filter = rassm->filter_bitmask[0];

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE0_PACKET_QUALITY)) {
        buf[i++] = result->packet_quality;
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE0_PACKET_RSSI)) {
        buf[i++] = result->packet_rssi;
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE0_PACKET_ANTENNA)) {
        buf[i++] = result->packet_antenna;
    }

    if ((filter & BIT(BLE_SVC_RAS_FILTER_MODE0_MEASURED_FREQ_OFFSET)) &&
        local_role == BLE_HS_CS_ROLE_INITIATOR) {
        put_le16(buf + i, result->measured_freq_offset);
        i += 2;
    }

    rc = os_mbuf_append(om, buf, i);
    assert(rc == 0);

    return rc;
}

int
ble_svc_ras_add_mode1_result(struct ble_cs_mode1_result *result,
                             uint16_t conn_handle, uint8_t rtt_pct_included)
{
    struct ble_svc_ras_sm *rassm;
    struct os_mbuf *om;
    struct os_mbuf *om2;
    uint8_t *buf;
    uint16_t filter;
    uint8_t i = 0;

    rassm = ble_svc_ras_sm_get(conn_handle);
    assert(rassm != NULL);

    om = rassm->ranging_data_body_mbuf;
    filter = rassm->filter_bitmask[1];

    om2 = os_mbuf_get(&ras_mbuf_pool, 0);
    assert(om2 != NULL);

    assert(om2->om_omp->omp_databuf_len >= 14);
    buf = om2->om_data;

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_QUALITY)) {
        buf[i++] = result->packet_quality;
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_NADM)) {
        buf[i++] = result->packet_nadm;
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_RSSI)) {
        buf[i++] = result->packet_rssi;
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_TOD_TOA)) {
        put_le16(buf + i, result->toa_tod);
        i += 2;
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_ANTENNA)) {
        buf[i++] = result->packet_antenna;
    }

    if (rtt_pct_included) {
        if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_PCT1)) {
            put_le32(buf + i, result->packet_pct1);
            i += 4;
        }

        if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_PCT2)) {
            put_le32(buf + i, result->packet_pct2);
            i += 4;
        }
    }

    om2->om_len = i;

    os_mbuf_pack_chains(om, om2);

    return 0;
}

int
ble_svc_ras_add_mode2_result(struct ble_cs_mode2_result *result,
                             uint16_t conn_handle, uint8_t n_ap)
{
    struct ble_svc_ras_sm *rassm;
    struct os_mbuf *om;
    struct os_mbuf *om2;
    uint8_t *buf;
    uint16_t filter;
    uint8_t i = 0;
    uint8_t k = 0;

    rassm = ble_svc_ras_sm_get(conn_handle);
    assert(rassm != NULL);

    om = rassm->ranging_data_body_mbuf;
    filter = rassm->filter_bitmask[2];

    om2 = os_mbuf_get(&ras_mbuf_pool, 0);
    assert(om2 != NULL);

    assert(om2->om_omp->omp_databuf_len >= 25);
    buf = om2->om_data;

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PERMUTATION_ID)) {
        buf[i++] = result->antenna_path_permutation_id;
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_TONE_PCT)) {
        for (k = 0; k < n_ap + 1; ++k) {
            put_le24(buf + i, result->tone_pct[k]);
            i += 3;
        }
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_TONE_QUALITY)) {
        for (k = 0; k < n_ap + 1; ++k) {
            buf[i++] = result->tone_quality_ind[k];
        }
    }

    k = 0;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_1)) {
        buf[i++] = result->antenna_paths[k++];
    }

    if (n_ap > 1 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_2))) {
        buf[i++] = result->antenna_paths[k++];
    }

    if (n_ap > 2 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_3))) {
        buf[i++] = result->antenna_paths[k++];
    }

    if (n_ap > 3 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_4))) {
        buf[i++] = result->antenna_paths[k++];
    }

    om2->om_len = i;

    os_mbuf_pack_chains(om, om2);

    return 0;
}

int
ble_svc_ras_add_mode3_result(struct ble_cs_mode3_result *result, uint16_t conn_handle,
                             uint8_t n_ap, uint8_t rtt_pct_included)
{
    struct ble_svc_ras_sm *rassm;
    struct os_mbuf *om;
    struct os_mbuf *om2;
    uint8_t *buf;
    uint16_t filter;
    uint8_t i = 0;
    uint8_t k = 0;

    rassm = ble_svc_ras_sm_get(conn_handle);
    assert(rassm != NULL);

    om = rassm->ranging_data_body_mbuf;
    filter = rassm->filter_bitmask[3];

    om2 = os_mbuf_get(&ras_mbuf_pool, 0);
    assert(om2 != NULL);

    assert(om2->om_omp->omp_databuf_len >= 39);
    buf = om2->om_data;

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_QUALITY)) {
        buf[i++] = result->packet_quality;
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_NADM)) {
        buf[i++] = result->packet_nadm;
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_RSSI)) {
        buf[i++] = result->packet_rssi;
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_TOD_TOA)) {
        put_le16(buf + i, result->toa_tod);
        i += 2;
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_ANTENNA)) {
        buf[i++] = result->packet_antenna;
    }

    if (rtt_pct_included) {
        if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_PCT1)) {
            put_le32(buf + i, result->packet_pct1);
            i += 4;
        }

        if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_PCT2)) {
            put_le32(buf + i, result->packet_pct2);
            i += 4;
        }
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PERMUTATION_ID)) {
        buf[i++] = result->antenna_path_permutation_id;
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_TONE_PCT)) {
        for (k = 0; k < n_ap + 1; ++k) {
            put_le24(buf + i, result->tone_pct[k]);
            i += 3;
        }
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_TONE_QUALITY)) {
        for (k = 0; k < n_ap + 1; ++k) {
            buf[i++] = result->tone_quality_ind[k];
        }
    }

    k = 0;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_1)) {
        buf[i++] = result->antenna_paths[k++];
    }

    if (n_ap > 1 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_2))) {
        buf[i++] = result->antenna_paths[k++];
    }

    if (n_ap > 2 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_3))) {
        buf[i++] = result->antenna_paths[k++];
    }

    if (n_ap > 3 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_4))) {
        buf[i++] = result->antenna_paths[k++];
    }

    om2->om_len = i;

    os_mbuf_pack_chains(om, om2);

    return 0;
}

static int
ble_svc_ras_ranging_data_notify(struct ble_svc_ras_sm *rassm, uint8_t notify,
                                uint8_t indicate)
{
    int rc;
    int len;
    struct os_mbuf *om;
    struct os_mbuf *om2;
    uint8_t *buf;
    uint16_t val_handle;
    uint8_t segment_header;

    if (((rassm->notify & BIT(notify)) == 0) &&
        ((rassm->indicate & BIT(indicate)) == 0)) {
        return 1;
    }

    om = rassm->ranging_data_body_mbuf;
    assert(om != NULL);

    segment_header = rassm->rolling_segment_counter << 2;

    if (rassm->remaining_data_len == 0) {
        rassm->remaining_data_len = OS_MBUF_PKTLEN(om);
        rassm->sent_data_len = 0;
        rassm->segment_counter = 0;
        /* Set the First Segment bit */
        segment_header |= 0b01;
    }

    len = min(rassm->mtu - 4, rassm->remaining_data_len + 1) - 1;
    rassm->remaining_data_len -= len;

    om2 = os_mbuf_get(&ras_mbuf_pool, 0);
    assert(om2 != NULL);

    buf = os_mbuf_extend(om2, len + 1);
    assert(buf != NULL);

    if (rassm->remaining_data_len == 0) {
        /* Set the Last Segment bit */
        segment_header |= 0b10;
    }
    buf[0] = segment_header;

    rc = os_mbuf_copydata(om, rassm->sent_data_len, len, &buf[1]);
    assert(rc == 0);

    ++rassm->segment_counter;
    rassm->sent_data_len += len;
    rassm->rolling_segment_counter = (rassm->rolling_segment_counter + 1) % 64;

    if (notify == NOTIFY_REAL_TIME_RANGING_DATA) {
        val_handle = ble_svc_ras_real_time_ranging_data_val_handle;
    } else {
        val_handle = ble_svc_ras_on_demand_ranging_data_val_handle;
    }

    if (rassm->notify & BIT(notify)) {
        rc = ble_gatts_notify_custom(rassm->conn_handle, val_handle, om2);
    } else {
        rc = ble_gatts_indicate_custom(rassm->conn_handle, val_handle, om2);
    }

    return rc;
}

static int
ble_svc_ras_features_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    struct ble_svc_ras_sm *rassm;
    uint8_t *buf;

    rassm = ble_svc_ras_sm_get(conn_handle);
    assert(rassm != NULL);

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        buf = os_mbuf_extend(ctxt->om, 4);
        if (buf == NULL) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        put_le32(buf, rassm->supported_features);
        break;
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

static int
ble_svc_ras_cp_rsp_complete_ranging_data_notify(struct ble_svc_ras_sm *rassm)
{
    int rc;
    struct os_mbuf *om;
    uint8_t *buf;

    if ((rassm->indicate & BIT(INDICATE_CONTROL_POINT)) == 0) {
        return 1;
    }

    om = os_msys_get_pkthdr(3, 0);
    assert(om != NULL);

    buf = os_mbuf_extend(om, 3);
    assert(buf != NULL);

    buf[0] = BLE_SVC_RAS_CP_RSP_COMPLETE_RANGING_DATA;
    put_le16(buf + 1, rassm->ranging_counter);

    rc = ble_gatts_indicate_custom(rassm->conn_handle,
                                   ble_svc_ras_control_point_val_handle, om);

    return rc;
}

static int
ble_svc_ras_cp_rsp_complete_lost_segment_notify(struct ble_svc_ras_sm *rassm,
                                                uint16_t ranging_counter,
                                                uint8_t first_segment_id,
                                                uint8_t last_segment_id)
{
    int rc;
    struct os_mbuf *om;
    uint8_t *buf;

    if ((rassm->indicate & BIT(INDICATE_CONTROL_POINT)) == 0) {
        return 1;
    }

    om = os_msys_get_pkthdr(5, 0);
    assert(om != NULL);

    buf = os_mbuf_extend(om, 5);
    assert(buf != NULL);

    buf[0] = BLE_SVC_RAS_CP_RSP_COMPLETE_LOST_SEGMENT;
    put_le16(buf + 1, ranging_counter);
    buf[3] = first_segment_id;
    buf[4] = last_segment_id;

    rc = ble_gatts_indicate_custom(rassm->conn_handle,
                                   ble_svc_ras_control_point_val_handle, om);

    return rc;
}

static int
ble_svc_ras_cp_rsp_response_code_notify(struct ble_svc_ras_sm *rassm, uint8_t rsp_code)
{
    int rc;
    struct os_mbuf *om;
    uint8_t *buf;

    if ((rassm->indicate & BIT(INDICATE_CONTROL_POINT)) == 0) {
        return 1;
    }

    om = os_msys_get_pkthdr(2, 0);
    assert(om != NULL);

    buf = os_mbuf_extend(om, 2);
    assert(buf != NULL);

    buf[0] = BLE_SVC_RAS_CP_RSP_RESPONSE_CODE;
    buf[1] = rsp_code;

    rc = ble_gatts_indicate_custom(rassm->conn_handle,
                                   ble_svc_ras_control_point_val_handle, om);

    return rc;
}

static int
ras_cp_cmd_get_ranging_data(struct ble_svc_ras_sm *rassm,
                            const struct os_mbuf *om, int off)
{
    int rc;
    uint16_t ranging_counter;

    rc = os_mbuf_copydata(om, off, sizeof(ranging_counter), &ranging_counter);
    if (rc != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    ranging_counter = get_le16(&ranging_counter);

    if (rassm->ranging_counter != ranging_counter) {
        return ble_svc_ras_cp_rsp_response_code_notify(
            rassm, BLE_SVC_RAS_CP_RSPCODE_NO_RECORDS_FOUND);
    }

    rassm->busy = 1;

    return ble_svc_ras_ranging_data_notify(rassm, NOTIFY_ON_DEMAND_RANGING_DATA,
                                           INDICATE_ON_DEMAND_RANGING_DATA);
}

static int
ras_cp_cmd_ack_ranging_data(struct ble_svc_ras_sm *rassm,
                            const struct os_mbuf *om, int off)
{
    int rc;
    uint16_t ranging_counter;

    rc = os_mbuf_copydata(om, off, sizeof(ranging_counter), &ranging_counter);
    if (rc != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    ranging_counter = get_le16(&ranging_counter);

    return ble_svc_ras_cp_rsp_response_code_notify(rassm, BLE_SVC_RAS_CP_RSPCODE_SUCCESS);
}

static int
ras_cp_cmd_retrieve_lost_segment(struct ble_svc_ras_sm *rassm,
                                 const struct os_mbuf *om, int off)
{
    int rc;
    uint8_t buf[4];
    uint16_t ranging_counter;
    uint8_t first_segment_id;
    uint8_t last_segment_id;

    rc = os_mbuf_copydata(om, off, 4, buf);
    if (rc != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    ranging_counter = get_le16(buf);
    first_segment_id = buf[2];
    last_segment_id = buf[3];
    /* TODO */
    (void)ranging_counter;
    (void)first_segment_id;
    (void)last_segment_id;

    return ble_svc_ras_cp_rsp_response_code_notify(
        rassm, BLE_SVC_RAS_CP_RSPCODE_OP_CODE_NOT_SUPPORTED);
}

static int
ras_cp_cmd_abort_operation(struct ble_svc_ras_sm *rassm)
{
    ble_svc_ras_flush(rassm);

    return ble_svc_ras_cp_rsp_response_code_notify(rassm, BLE_SVC_RAS_CP_RSPCODE_SUCCESS);
}

static int
ras_cp_cmd_set_filter(struct ble_svc_ras_sm *rassm, const struct os_mbuf *om, int off)
{
    int rc;
    uint16_t filter_config;
    uint16_t filter;
    uint8_t mode;

    rc = os_mbuf_copydata(om, off, sizeof(filter_config), &filter_config);
    if (rc != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    filter_config = get_le16(&filter_config);
    mode = filter_config & 0b11;
    filter = filter_config >> 2;
    rassm->filter_bitmask[mode] = filter;

    return ble_svc_ras_cp_rsp_response_code_notify(rassm, BLE_SVC_RAS_CP_RSPCODE_SUCCESS);
}

static int
ble_svc_ras_control_point_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;
    struct ble_svc_ras_sm *rassm;
    uint8_t opcode;

    rassm = ble_svc_ras_sm_get(conn_handle);
    assert(rassm != NULL);

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    rc = os_mbuf_copydata(ctxt->om, 0, sizeof(opcode), &opcode);
    if (rc != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    if (rassm->busy && opcode != BLE_SVC_RAS_CP_CMD_ABORT_OPERATION) {
        return ble_svc_ras_cp_rsp_response_code_notify(
            rassm, BLE_SVC_RAS_CP_RSPCODE_SERVER_BUSY);
    }

    switch (opcode) {
    case BLE_SVC_RAS_CP_CMD_GET_RANGING_DATA:
        rc = ras_cp_cmd_get_ranging_data(rassm, ctxt->om, sizeof(opcode));
        break;
    case BLE_SVC_RAS_CP_CMD_ACK_RANGING_DATA:
        rc = ras_cp_cmd_ack_ranging_data(rassm, ctxt->om, sizeof(opcode));
        break;
    case BLE_SVC_RAS_CP_CMD_RETRIEVE_LOST_SEGMENT:
        rc = ras_cp_cmd_retrieve_lost_segment(rassm, ctxt->om, sizeof(opcode));
        break;
    case BLE_SVC_RAS_CP_CMD_ABORT_OPERATION:
        rc = ras_cp_cmd_abort_operation(rassm);
        break;
    case BLE_SVC_RAS_CP_CMD_SET_FILTER:
        rc = ras_cp_cmd_set_filter(rassm, ctxt->om, sizeof(opcode));
        break;
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (rc == BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN) {
        return ble_svc_ras_cp_rsp_response_code_notify(
            rassm, BLE_SVC_RAS_CP_RSPCODE_INVALID_PARAMETER);
    }

    return 0;
}

static int
ble_svc_ras_ranging_data_ready_notify(struct ble_svc_ras_sm *rassm)
{
    int rc;
    struct os_mbuf *om;
    uint8_t *buf;

    if (((rassm->notify & BIT(NOTIFY_RANGING_DATA_READY)) == 0) &&
        ((rassm->indicate & BIT(INDICATE_RANGING_DATA_READY)) == 0)) {
        return 1;
    }

    om = os_msys_get_pkthdr(2, 0);
    if (!om) {
        return 1;
    }

    buf = os_mbuf_extend(om, 2);
    assert(buf != NULL);

    put_le16(buf, rassm->ranging_counter);

    if (rassm->notify & BIT(NOTIFY_RANGING_DATA_READY)) {
        rc = ble_gatts_notify_custom(
            rassm->conn_handle, ble_svc_ras_ranging_data_ready_val_handle, om);
    } else {
        rc = ble_gatts_indicate_custom(
            rassm->conn_handle, ble_svc_ras_ranging_data_ready_val_handle, om);
    }

    return rc;
}

int
ble_svc_ras_ranging_data_ready(uint16_t conn_handle)
{
    struct ble_svc_ras_sm *rassm;

    rassm = ble_svc_ras_sm_get(conn_handle);
    assert(rassm != NULL);

    if ((rassm->notify & BIT(NOTIFY_RANGING_DATA_READY)) ||
        (rassm->indicate & BIT(INDICATE_RANGING_DATA_READY))) {
        return ble_svc_ras_ranging_data_ready_notify(rassm);
    }

    if ((rassm->notify & BIT(NOTIFY_REAL_TIME_RANGING_DATA)) ||
        (rassm->indicate & BIT(INDICATE_REAL_TIME_RANGING_DATA))) {
        return ble_svc_ras_ranging_data_notify(rassm, NOTIFY_REAL_TIME_RANGING_DATA,
                                               INDICATE_REAL_TIME_RANGING_DATA);
    }

    return 0;
}

static int
ble_svc_ras_ranging_data_ready_access(uint16_t conn_handle, uint16_t attr_handle,
                                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    struct ble_svc_ras_sm *rassm;
    uint8_t *buf;

    rassm = ble_svc_ras_sm_get(conn_handle);

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        buf = os_mbuf_extend(ctxt->om, 2);
        if (buf == NULL) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        put_le16(buf, rassm->ranging_counter);
        break;
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

static int
ble_svc_ras_ranging_data_overwritten_notify(struct ble_svc_ras_sm *rassm,
                                            uint32_t toa_tod_val)
{
    int rc;
    struct os_mbuf *om;
    uint8_t *buf;

    if (((rassm->notify & BIT(NOTIFY_RANGING_DATA_OVERWRITTEN)) == 0) &&
        ((rassm->indicate & BIT(INDICATE_RANGING_DATA_OVERWRITTEN)) == 0)) {
        return 1;
    }

    om = os_msys_get_pkthdr(2, 0);
    if (!om) {
        return 1;
    }

    buf = os_mbuf_extend(om, 2);
    assert(buf != NULL);

    put_le16(buf, rassm->data_overwritten_counter);

    if (rassm->notify & BIT(NOTIFY_RANGING_DATA_OVERWRITTEN)) {
        rc = ble_gatts_notify_custom(
            rassm->conn_handle, ble_svc_ras_ranging_data_overwritten_val_handle, om);
    } else {
        rc = ble_gatts_indicate_custom(
            rassm->conn_handle, ble_svc_ras_ranging_data_overwritten_val_handle, om);
    }

    return rc;
}

static int
ble_svc_ras_ranging_data_overwritten_access(uint16_t conn_handle, uint16_t attr_handle,
                                            struct ble_gatt_access_ctxt *ctxt,
                                            void *arg)
{
    struct ble_svc_ras_sm *rassm;
    uint8_t *buf;

    rassm = ble_svc_ras_sm_get(conn_handle);

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        buf = os_mbuf_extend(ctxt->om, 2);
        if (buf == NULL) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        put_le16(buf, rassm->data_overwritten_counter);
        break;
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

static int
gatt_forbidden_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    { /*** Service: ToA_ToD samples */
      .type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &ble_svc_ras_ranging_service_uuid.u,
     .characteristics =
          (struct ble_gatt_chr_def[]){
              {
                  .uuid = &ble_svc_ras_features_uuid.u,
                  .access_cb = ble_svc_ras_features_access,
                  .val_handle = &ble_svc_ras_features_val_handle,
                  .flags = BLE_GATT_CHR_F_READ,
              },
              {
                  .uuid = &ble_svc_ras_real_time_ranging_data_uuid.u,
                  .access_cb = gatt_forbidden_access_cb,
                  .val_handle = &ble_svc_ras_real_time_ranging_data_val_handle,
                  .flags = BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_NOTIFY,
              },
              {
                  .uuid = &ble_svc_ras_on_demand_ranging_data_uuid.u,
                  .access_cb = gatt_forbidden_access_cb,
                  .val_handle = &ble_svc_ras_on_demand_ranging_data_val_handle,
                  .flags = BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_NOTIFY,
              },
              {
                  .uuid = &ble_svc_ras_control_point_uuid.u,
                  .access_cb = ble_svc_ras_control_point_access,
                  .val_handle = &ble_svc_ras_control_point_val_handle,
                  .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_INDICATE,
              },
              {
                  .uuid = &ble_svc_ras_ranging_data_ready_uuid.u,
                  .access_cb = ble_svc_ras_ranging_data_ready_access,
                  .val_handle = &ble_svc_ras_ranging_data_ready_val_handle,
                  .flags = BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
              },
              {
                  .uuid = &ble_svc_ras_ranging_data_overwritten_uuid.u,
                  .access_cb = ble_svc_ras_ranging_data_overwritten_access,
                  .val_handle = &ble_svc_ras_ranging_data_overwritten_val_handle,
                  .flags = BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
              },
              {
                  0, /* No more characteristics in this service. */
              } } },
    {
     0, /* No more services. */
    },
};

static void
ble_svc_ras_on_disconnect(uint16_t conn_handle)
{
    struct ble_svc_ras_sm *rassm;

    rassm = ble_svc_ras_sm_get(conn_handle);
    if (rassm == NULL) {
        return;
    }

    rassm->conn_handle = conn_handle;

    ble_svc_ras_flush(rassm);
}

static void
ble_svc_ras_on_connect(struct ble_gap_event *event)
{
    int rc;
    struct ble_l2cap_chan *chan;
    struct ble_svc_ras_sm *rassm = NULL;
    uint16_t conn_handle;
    uint8_t i;

    if (event->connect.status) {
        return;
    }

    conn_handle = event->connect.conn_handle;

    rassm = ble_svc_ras_sm_get(BLE_CONN_HANDLE_INVALID);
    assert(rassm != NULL);
    assert(rassm->ranging_data_body_mbuf == NULL);
    memset(rassm, 0, sizeof(*rassm));
    rassm->conn_handle = conn_handle;
    rassm->supported_features = 0b1111;

    rc = ble_att_conn_chan_find(conn_handle, BLE_L2CAP_CID_ATT, NULL, &chan);
    if (rc == 0) {
        rassm->mtu = ble_att_chan_mtu(chan);
    }

    for (i = 0; i < 4; i++) {
        rassm->filter_bitmask[i] = 0xffff;
    }
}

static void
ble_svc_ras_on_mtu(struct ble_gap_event *event)
{
    struct ble_svc_ras_sm *rassm;

    rassm = ble_svc_ras_sm_get(event->mtu.conn_handle);
    if (rassm == NULL || event->mtu.channel_id != BLE_L2CAP_CID_ATT) {
        return;
    }

    rassm->mtu = event->mtu.value;
}

static void
ble_svc_ras_on_notify_tx(struct ble_gap_event *event)
{
    struct ble_svc_ras_sm *rassm;
    uint16_t attr_handle = event->notify_tx.attr_handle;
    uint8_t notify;
    uint8_t indicate;

    if (event->notify_tx.status != BLE_HS_EDONE) {
        return;
    }

    rassm = ble_svc_ras_sm_get(event->notify_tx.conn_handle);
    if (rassm == NULL) {
        return;
    }

    if (attr_handle == ble_svc_ras_real_time_ranging_data_val_handle) {
        notify = NOTIFY_REAL_TIME_RANGING_DATA;
        indicate = INDICATE_REAL_TIME_RANGING_DATA;
    } else if (attr_handle == ble_svc_ras_on_demand_ranging_data_val_handle) {
        notify = NOTIFY_ON_DEMAND_RANGING_DATA;
        indicate = INDICATE_ON_DEMAND_RANGING_DATA;
    } else {
        return;
    }

    if (rassm->remaining_data_len > 0) {
        ble_svc_ras_ranging_data_notify(rassm, notify, indicate);
    } else {
        ble_svc_ras_cp_rsp_complete_ranging_data_notify(rassm);
        rassm->busy = 0;
    }
}

static int
ble_svc_ras_on_subscribe(struct ble_gap_event *event)
{
    struct ble_svc_ras_sm *rassm;
    uint8_t notify = NOTIFY_INVALID;
    uint8_t indicate = INDICATE_INVALID;

    rassm = ble_svc_ras_sm_get(event->subscribe.conn_handle);
    if (rassm == NULL) {
        return 0;
    }

    if (event->subscribe.attr_handle == ble_svc_ras_real_time_ranging_data_val_handle) {
        if (rassm->notify & BIT(NOTIFY_ON_DEMAND_RANGING_DATA) ||
            rassm->indicate & BIT(INDICATE_ON_DEMAND_RANGING_DATA)) {
            /* The RAS Server shall operate in either Real-time or On-demand
             * mode, but not both simultaneously.
             */
            return BLE_ATT_ERR_CCCD_IMPORER_CONF;
        }
        notify = NOTIFY_REAL_TIME_RANGING_DATA;
        indicate = INDICATE_REAL_TIME_RANGING_DATA;
    } else if (event->subscribe.attr_handle ==
               ble_svc_ras_on_demand_ranging_data_val_handle) {
        if (rassm->notify & BIT(NOTIFY_REAL_TIME_RANGING_DATA) ||
            rassm->indicate & BIT(INDICATE_REAL_TIME_RANGING_DATA)) {
            return BLE_ATT_ERR_CCCD_IMPORER_CONF;
        }
        notify = NOTIFY_ON_DEMAND_RANGING_DATA;
        indicate = INDICATE_ON_DEMAND_RANGING_DATA;
    } else if (event->subscribe.attr_handle == ble_svc_ras_control_point_val_handle) {
        indicate = INDICATE_CONTROL_POINT;
    } else if (event->subscribe.attr_handle == ble_svc_ras_ranging_data_ready_val_handle) {
        notify = NOTIFY_RANGING_DATA_READY;
        indicate = INDICATE_RANGING_DATA_READY;
    } else if (event->subscribe.attr_handle ==
               ble_svc_ras_ranging_data_overwritten_val_handle) {
        notify = NOTIFY_RANGING_DATA_OVERWRITTEN;
        indicate = INDICATE_RANGING_DATA_OVERWRITTEN;
    }

    if (notify != NOTIFY_INVALID) {
        if (event->subscribe.cur_notify) {
            rassm->notify |= BIT(notify);
        } else if (event->subscribe.prev_notify) {
            rassm->notify &= ~BIT(notify);
        }
    }

    if (indicate != INDICATE_INVALID) {
        if (event->subscribe.cur_indicate) {
            rassm->indicate |= BIT(indicate);
        } else if (event->subscribe.prev_indicate) {
            rassm->indicate &= ~BIT(indicate);
        }
    }

    return 0;
}

static int
ble_svc_ras_gap_event(struct ble_gap_event *event, void *arg)
{
    int rc = 0;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ble_svc_ras_on_connect(event);
        break;
    case BLE_GAP_EVENT_MTU:
        ble_svc_ras_on_mtu(event);
        break;
    case BLE_GAP_EVENT_NOTIFY_TX:
        ble_svc_ras_on_notify_tx(event);
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        rc = ble_svc_ras_on_subscribe(event);
        /* TODO: We have to be able to reject a write to CCCD with
         * Client Characteristic Configuration Descriptor Improperly
         * Configured, but for now API does not allow that.
         */
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ble_svc_ras_on_disconnect(event->disconnect.conn.conn_handle);
        break;
    default:
        break;
    }

    return rc;
}

void
ble_svc_ras_init(void)
{
    int rc;
    uint8_t i;

    for (i = 0; i < ARRAY_SIZE(g_ble_ras_sm); i++) {
        g_ble_ras_sm[i].conn_handle = BLE_CONN_HANDLE_INVALID;
    }

    rc = os_mempool_init(&ras_mempool, MBUF_POOL_BUF_COUNT, MBUF_POOL_BUF_SIZE,
                         &ras_mbuf_area[0], "ras_mbuf_pool");
    assert(rc == 0);

    rc = os_mbuf_pool_init(&ras_mbuf_pool, &ras_mempool, MBUF_POOL_BUF_SIZE,
                           MBUF_POOL_BUF_COUNT);
    assert(rc == 0);

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    assert(rc == 0);

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    assert(rc == 0);

    rc = ble_gap_event_listener_register(&ble_svc_ras_listener,
                                         ble_svc_ras_gap_event, NULL);
    assert(rc == 0);
}
