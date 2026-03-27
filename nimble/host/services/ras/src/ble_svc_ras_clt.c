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
// #include "host/ble_hs_log.h"
#include "host/ble_hs.h"
#include "host/ble_cs.h"
#include "host/ble_uuid.h"
#include "host/ble_peer.h"
#include "nimble/hci_common.h"
#include "sys/queue.h"
// #include "ble_hs_priv.h"
// #include "ble_hs_hci_priv.h"
// #include "ble_cs_priv.h"
#include "services/ras/ble_svc_ras.h"
// #include <assert.h>
// #include <stdio.h>
// #include <string.h>
// #include "bsp/bsp.h"
// #include "host/ble_hs.h"
// #include "host/ble_uuid.h"
// #include "cs_reflector.h"

// struct ble_hs_cfg;
// struct ble_gatt_register_ctxt;

#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

#define MBUF_POOL_BUF_SIZE  MYNEWT_VAL(BLE_SVC_RAS_TXRX_BUF_SIZE)
#define MBUF_POOL_BUF_COUNT MYNEWT_VAL(BLE_SVC_RAS_TXRX_BUF_COUNT)

static struct os_mempool ras_clt_mempool;
static struct os_mbuf_pool ras_clt_mbuf_pool;
static uint8_t ras_clt_mbuf_area[MBUF_POOL_BUF_COUNT * MBUF_POOL_BUF_SIZE];
#define SUBEVENT_BUF_LEN 40
static uint8_t subevent_buf[SUBEVENT_BUF_LEN];
static struct ble_gap_event_listener ble_svc_ras_clt_listener;

struct ble_svc_ras_clt_sm {
    int status;
    struct os_mbuf *ranging_data_body_mbuf;
    ble_svc_ras_clt_subscribe_cb *subscribe_cb;
    ble_svc_ras_clt_step_data_received_cb *step_data_cb;
    uint16_t real_time_ranging_data_val_handle;
    uint16_t on_demand_ranging_data_val_handle;
    uint16_t control_point_val_handle;
    uint16_t ranging_data_ready_val_handle;
    uint16_t ranging_data_overwritten_val_handle;
    uint16_t filter_bitmask[4];
    uint16_t conn_handle;
    uint16_t mtu;
    uint16_t ranging_counter;
    uint8_t rtt_pct_included;
    uint8_t local_role;
    uint8_t n_ap;
};

static struct ble_svc_ras_clt_sm g_ble_ras_clt_sm[MYNEWT_VAL(BLE_MAX_CONNECTIONS)];

int ble_att_conn_chan_find(uint16_t conn_handle, uint16_t cid,
                           struct ble_hs_conn **out_conn,
                           struct ble_l2cap_chan **out_chan);
uint16_t ble_att_chan_mtu(const struct ble_l2cap_chan *chan);

static struct ble_svc_ras_clt_sm *
ble_svc_ras_clt_sm_get(uint16_t conn_handle)
{
    struct ble_svc_ras_clt_sm *rassm = NULL;
    uint8_t i;

    for (i = 0; i < ARRAY_SIZE(g_ble_ras_clt_sm); i++) {
        if (g_ble_ras_clt_sm[i].conn_handle == conn_handle) {
            rassm = &g_ble_ras_clt_sm[i];
            break;
        }
    }

    return rassm;
}

static int
ble_svc_ras_clt_mode0_parse(struct ble_svc_ras_clt_sm *rassm,
                            struct os_mbuf *om, int *off)
{
    int rc;
    uint8_t *buf = subevent_buf;
    struct ble_cs_mode0_result result;
    uint8_t len = 0;
    uint8_t i = 0;
    uint16_t filter = rassm->filter_bitmask[BLE_HS_CS_MODE0];
    uint8_t remote_initiator = rassm->local_role == BLE_HS_CS_ROLE_REFLECTOR;

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE0_PACKET_QUALITY))
        len += 1;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE0_PACKET_RSSI))
        len += 1;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE0_PACKET_ANTENNA))
        len += 1;
    if ((filter & BIT(BLE_SVC_RAS_FILTER_MODE0_MEASURED_FREQ_OFFSET)) && remote_initiator)
        len += 2;

    rc = os_mbuf_copydata(om, *off, len, buf);
    if (rc) {
        return rc;
    }

    *off += len;

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE0_PACKET_QUALITY)) {
        result.packet_quality = buf[i++];
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE0_PACKET_RSSI)) {
        result.packet_rssi = buf[i++];
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE0_PACKET_ANTENNA)) {
        result.packet_antenna = buf[i++];
    }

    if ((filter & BIT(BLE_SVC_RAS_FILTER_MODE0_MEASURED_FREQ_OFFSET)) &&
        remote_initiator) {
        result.measured_freq_offset = get_le16(buf + i);
    }

    rassm->step_data_cb(&result, rassm->conn_handle, BLE_HS_CS_MODE0);

    return rc;
}

static int
ble_svc_ras_clt_mode1_parse(struct ble_svc_ras_clt_sm *rassm,
                            struct os_mbuf *om, int *off)
{
    int rc;
    uint8_t *buf = subevent_buf;
    struct ble_cs_mode1_result result;
    uint16_t filter = rassm->filter_bitmask[BLE_HS_CS_MODE1];
    uint8_t len = 0;
    uint8_t i = 0;

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_QUALITY))
        len += 1;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_NADM))
        len += 1;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_RSSI))
        len += 1;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_TOD_TOA))
        len += 2;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_ANTENNA))
        len += 1;
    if (rassm->rtt_pct_included) {
        if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_PCT1))
            len += 4;
        if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_PCT2))
            len += 4;
    }

    rc = os_mbuf_copydata(om, *off, len, buf);
    if (rc) {
        return rc;
    }

    *off += len;

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_QUALITY)) {
        result.packet_quality = buf[i++];
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_NADM)) {
        result.packet_nadm = buf[i++];
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_RSSI)) {
        result.packet_rssi = buf[i++];
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_TOD_TOA)) {
        result.toa_tod = get_le16(buf + i);
        i += 2;
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_ANTENNA)) {
        result.packet_antenna = buf[i++];
    }

    if (rassm->rtt_pct_included) {
        if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_PCT1)) {
            result.packet_pct1 = get_le32(buf + i);
            i += 4;
        }

        if (filter & BIT(BLE_SVC_RAS_FILTER_MODE1_PACKET_PCT2)) {
            result.packet_pct2 = get_le32(buf + i);
            i += 4;
        }
    }

    rassm->step_data_cb(&result, rassm->conn_handle, BLE_HS_CS_MODE1);

    return 0;
}

static int
ble_svc_ras_clt_mode2_parse(struct ble_svc_ras_clt_sm *rassm,
                            struct os_mbuf *om, int *off)
{
    int rc;
    uint8_t *buf = subevent_buf;
    struct ble_cs_mode2_result result;
    uint16_t filter = rassm->filter_bitmask[BLE_HS_CS_MODE2];
    uint8_t n_ap = rassm->n_ap;
    uint8_t i = 0;
    uint8_t k = 0;
    uint8_t len = 0;

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PERMUTATION_ID))
        len += 1;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_TONE_PCT))
        len += 3 * (n_ap + 1);
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_TONE_QUALITY))
        len += n_ap + 1;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_1))
        len += 1;
    if (n_ap > 1 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_2)))
        len += 1;
    if (n_ap > 2 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_3)))
        len += 1;
    if (n_ap > 3 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_4)))
        len += 1;

    rc = os_mbuf_copydata(om, *off, len, buf);
    if (rc) {
        return rc;
    }

    *off += len;

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PERMUTATION_ID)) {
        result.antenna_path_permutation_id = buf[i++];
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_TONE_PCT)) {
        for (k = 0; k < n_ap + 1; ++k) {
            result.tone_pct[k] = get_le24(buf + i);
            i += 3;
        }
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_TONE_QUALITY)) {
        for (k = 0; k < n_ap + 1; ++k) {
            result.tone_quality_ind[k] = buf[i++];
        }
    }

    k = 0;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_1)) {
        result.antenna_paths[k++] = buf[i++];
    }

    if (n_ap > 1 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_2))) {
        result.antenna_paths[k++] = buf[i++];
    }

    if (n_ap > 2 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_3))) {
        result.antenna_paths[k++] = buf[i++];
    }

    if (n_ap > 3 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE2_ANTENNA_PATH_4))) {
        result.antenna_paths[k++] = buf[i++];
    }

    rassm->step_data_cb(&result, rassm->conn_handle, BLE_HS_CS_MODE2);

    return 0;
}

static int
ble_svc_ras_clt_mode3_parse(struct ble_svc_ras_clt_sm *rassm,
                            struct os_mbuf *om, int *off)
{
    int rc;
    uint8_t *buf = subevent_buf;
    struct ble_cs_mode3_result result;
    uint16_t filter = rassm->filter_bitmask[BLE_HS_CS_MODE3];
    uint8_t n_ap = rassm->n_ap;
    uint8_t len = 0;
    uint8_t i = 0;
    uint8_t k = 0;

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_QUALITY))
        len += 1;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_NADM))
        len += 1;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_RSSI))
        len += 1;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_TOD_TOA))
        len += 2;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_ANTENNA))
        len += 1;
    if (rassm->rtt_pct_included) {
        if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_PCT1))
            len += 4;
        if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_PCT2))
            len += 4;
    }
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PERMUTATION_ID))
        len += 1;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_TONE_PCT))
        len += 3 * (n_ap + 1);
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_TONE_QUALITY))
        len += n_ap + 1;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_1))
        len += 1;
    if (n_ap > 1 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_2)))
        len += 1;
    if (n_ap > 2 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_3)))
        len += 1;
    if (n_ap > 3 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_4)))
        len += 1;

    rc = os_mbuf_copydata(om, *off, len, buf);
    if (rc) {
        return rc;
    }

    *off += len;

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_QUALITY)) {
        result.packet_quality = buf[i++];
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_NADM)) {
        result.packet_nadm = buf[i++];
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_RSSI)) {
        result.packet_rssi = buf[i++];
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_TOD_TOA)) {
        result.toa_tod = get_le16(buf + i);
        i += 2;
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_ANTENNA)) {
        result.packet_antenna = buf[i++];
    }

    if (rassm->rtt_pct_included) {
        if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_PCT1)) {
            result.packet_pct1 = get_le32(buf + i);
            i += 4;
        }

        if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_PACKET_PCT2)) {
            result.packet_pct2 = get_le32(buf + i);
            i += 4;
        }
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PERMUTATION_ID)) {
        result.antenna_path_permutation_id = buf[i++];
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_TONE_PCT)) {
        for (k = 0; k < n_ap + 1; ++k) {
            result.tone_pct[k] = get_le24(buf + i);
            i += 3;
        }
    }

    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_TONE_QUALITY)) {
        for (k = 0; k < n_ap + 1; ++k) {
            result.tone_quality_ind[k] = buf[i++];
        }
    }

    k = 0;
    if (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_1)) {
        result.antenna_paths[k++] = buf[i++];
    }

    if (n_ap > 1 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_2))) {
        result.antenna_paths[k++] = buf[i++];
    }

    if (n_ap > 2 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_3))) {
        result.antenna_paths[k++] = buf[i++];
    }

    if (n_ap > 3 && (filter & BIT(BLE_SVC_RAS_FILTER_MODE3_ANTENNA_PATH_4))) {
        result.antenna_paths[k++] = buf[i++];
    }

    rassm->step_data_cb(&result, rassm->conn_handle, BLE_HS_CS_MODE3);

    return 0;
}

static int
ble_svc_ras_clt_subevent_data_parse(struct ble_svc_ras_clt_sm *rassm,
                                    struct os_mbuf *om, int *off,
                                    uint8_t number_of_steps)
{
    int rc = 1;
    uint8_t step_mode;
    uint8_t i;

    for (i = 0; i < number_of_steps; ++i) {
        rc = os_mbuf_copydata(om, *off, 1, &step_mode);
        if (rc || step_mode > BLE_HS_CS_MODE3) {
            return rc;
        }

        *off += 1;

        switch (step_mode) {
        case BLE_HS_CS_MODE0:
            rc = ble_svc_ras_clt_mode0_parse(rassm, om, off);
            break;
        case BLE_HS_CS_MODE1:
            rc = ble_svc_ras_clt_mode1_parse(rassm, om, off);
            break;
        case BLE_HS_CS_MODE2:
            rc = ble_svc_ras_clt_mode2_parse(rassm, om, off);
            break;
        case BLE_HS_CS_MODE3:
            rc = ble_svc_ras_clt_mode3_parse(rassm, om, off);
            break;
        default:
            rc = 1;
        }

        if (rc) {
            return 0;
        }
    }

    return rc;
}

static int
ble_svc_ras_clt_subevent_parse(struct ble_svc_ras_clt_subevent_header *header,
                               struct os_mbuf *om, int *off)
{
    int rc;
    uint8_t *buf = subevent_buf;

    rc = os_mbuf_copydata(om, *off, 8, buf);
    if (rc) {
        return rc;
    }

    *off += 8;
    header->start_acl_conn_event = get_le16(buf);
    header->frequency_compensation = get_le16(&buf[2]);
    header->done_status = buf[4];
    header->abort_reason = buf[5];
    header->reference_power_level = buf[6];
    header->number_of_steps_reported = buf[7];

    return rc;
}

static int
ble_svc_ras_clt_ranging_header_parse(struct ble_svc_ras_clt_ranging_header *header,
                                     struct os_mbuf *om, int *off)
{
    int rc;
    uint8_t *buf = subevent_buf;

    rc = os_mbuf_copydata(om, *off, 4, buf);
    if (rc) {
        return rc;
    }

    *off += 4;
    header->ranging_counter = get_le16(buf) & 0x0FFF;
    header->config_id = buf[1] >> 4;
    header->tx_power = buf[2];
    header->antenna_paths_mask = buf[3];

    return rc;
}

static int
ble_svc_ras_clt_ranging_data_parse(struct ble_svc_ras_clt_sm *rassm,
                                   struct os_mbuf *om,
                                   struct ble_svc_ras_clt_ranging_header *header)
{
    int rc;
    int off = 0;
    int buf_len = OS_MBUF_PKTLEN(om);
    uint8_t *buf = subevent_buf;
    struct ble_svc_ras_clt_subevent_header sub_header;

    rc = ble_svc_ras_clt_ranging_header_parse(header, om, &off);
    if (rc) {
        return rc;
    }

    while (off < buf_len) {
        ble_svc_ras_clt_subevent_parse(&sub_header, om, &off);

        rc = ble_svc_ras_clt_subevent_data_parse(
            rassm, om, &off, sub_header.number_of_steps_reported);
        if (rc) {
            return rc;
        }
    }

    return rc;
}

static int
ble_svc_ras_clt_notification_parse(struct os_mbuf *src_om, uint16_t conn_handle)
{
    int rc;
    struct os_mbuf *cache_om;
    struct ble_svc_ras_clt_sm *rassm;
    struct ble_svc_ras_clt_ranging_header ranging_header;
    uint8_t segment_header;
    uint8_t is_first_segment;
    uint8_t is_last_segment;

    rassm = ble_svc_ras_clt_sm_get(conn_handle);
    assert(rassm != NULL);

    rc = os_mbuf_copydata(src_om, 0, sizeof(segment_header), &segment_header);
    if (rc != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    is_first_segment = segment_header & 0b01;
    is_last_segment = segment_header & 0b10;

    if (is_first_segment) {
        cache_om = os_mbuf_get_pkthdr(&ras_clt_mbuf_pool, 0);
        assert(cache_om != NULL);
        rassm->ranging_data_body_mbuf = cache_om;
    } else {
        cache_om = rassm->ranging_data_body_mbuf;
    }

    rc = os_mbuf_appendfrom(cache_om, src_om, 1, OS_MBUF_PKTLEN(src_om) - 1);

    if (is_last_segment) {
        ble_svc_ras_clt_ranging_data_parse(rassm, cache_om, &ranging_header);
    }

    return 0;
}

static void
ble_svc_ras_clt_on_disconnect(uint16_t conn_handle)
{
    struct ble_svc_ras_clt_sm *rassm;

    rassm = ble_svc_ras_clt_sm_get(conn_handle);
    if (rassm == NULL) {
        return;
    }

    rassm->conn_handle = BLE_CONN_HANDLE_INVALID;

    if (rassm->ranging_data_body_mbuf != NULL) {
        os_mbuf_free_chain(rassm->ranging_data_body_mbuf);
        rassm->ranging_data_body_mbuf = NULL;
    }
}

static int
ble_svc_ras_clt_cp_write_flat(uint16_t conn_handle, const void *data,
                              uint16_t data_len, ble_gatt_attr_fn *cb, void *cb_arg)
{
    int rc;
    const struct ble_peer *peer;
    const struct ble_peer_chr *chr;

    peer = ble_peer_find(conn_handle);
    if (peer == NULL) {
        return 1;
    }

    chr = ble_peer_chr_find_uuid(
        peer, BLE_UUID16_DECLARE(BLE_SVC_RAS_SVC_RANGING_SERVICE_UUID),
        BLE_UUID16_DECLARE(BLE_SVC_RAS_CHR_RAS_CONTROL_POINT_UUID));

    rc = ble_gattc_write_flat(conn_handle, chr->chr.val_handle, data, data_len,
                              cb, cb_arg);

    return rc;
}

static int
ble_svc_ras_clt_cp_get_ranging_data(struct ble_svc_ras_clt_sm *rassm,
                                    uint16_t ranging_counter)
{
    uint8_t value[3];

    value[0] = BLE_SVC_RAS_CP_CMD_GET_RANGING_DATA;
    put_le16(value + 1, ranging_counter);

    return ble_svc_ras_clt_cp_write_flat(rassm->conn_handle, value,
                                         sizeof(value), NULL, NULL);
}

static int
ble_svc_ras_clt_cp_ack_ranging_data(struct ble_svc_ras_clt_sm *rassm,
                                    uint16_t ranging_counter)
{
    uint8_t value[3];

    value[0] = BLE_SVC_RAS_CP_CMD_ACK_RANGING_DATA;
    put_le16(value + 1, ranging_counter);

    return ble_svc_ras_clt_cp_write_flat(rassm->conn_handle, value,
                                         sizeof(value), NULL, NULL);
}

static int
ble_svc_ras_clt_cp_ack_retrieve_lost_segment(struct ble_svc_ras_clt_sm *rassm,
                                             uint16_t ranging_counter,
                                             uint8_t first_segment_id,
                                             uint8_t last_segment_id)
{
    uint8_t value[5];

    value[0] = BLE_SVC_RAS_CP_CMD_RETRIEVE_LOST_SEGMENT;
    put_le16(value + 1, ranging_counter);
    value[3] = first_segment_id;
    value[4] = last_segment_id;

    return ble_svc_ras_clt_cp_write_flat(rassm->conn_handle, value,
                                         sizeof(value), NULL, NULL);
}

static int
ble_svc_ras_clt_cp_abort_operation(struct ble_svc_ras_clt_sm *rassm,
                                   uint16_t ranging_counter)
{
    uint8_t value = BLE_SVC_RAS_CP_CMD_ABORT_OPERATION;

    return ble_svc_ras_clt_cp_write_flat(rassm->conn_handle, &value,
                                         sizeof(value), NULL, NULL);
}

static int
ble_svc_ras_clt_cp_set_filter(struct ble_svc_ras_clt_sm *rassm, uint8_t mode,
                              uint8_t filter)
{
    uint8_t value[2];

    assert(mode <= BLE_HS_CS_MODE3);
    rassm->filter_bitmask[mode] = filter;
    value[0] = BLE_SVC_RAS_CP_CMD_SET_FILTER;
    value[1] = filter << 2 | (mode & 0b11);

    return ble_svc_ras_clt_cp_write_flat(rassm->conn_handle, value,
                                         sizeof(value), NULL, NULL);
}

static void
ble_svc_ras_clt_on_connect(struct ble_gap_event *event)
{
    int rc;
    struct ble_l2cap_chan *chan;
    struct ble_svc_ras_clt_sm *rassm = NULL;
    uint8_t i;

    if (event->connect.status) {
        return;
    }

    rassm = ble_svc_ras_clt_sm_get(BLE_CONN_HANDLE_INVALID);
    assert(rassm != NULL);
    assert(rassm->ranging_data_body_mbuf == NULL);
    memset(rassm, 0, sizeof(*rassm));
    rassm->conn_handle = event->connect.conn_handle;
    //    rassm->data_overwritten_counter = 0;
    //    rassm->completed_procedures_counter = 0;
    //    rassm->rolling_segment_counter = 0;

    //    ble_hs_lock();
    rc = ble_att_conn_chan_find(rassm->conn_handle, BLE_L2CAP_CID_ATT, NULL, &chan);
    if (rc == 0) {
        rassm->mtu = ble_att_chan_mtu(chan);
    }
    //    ble_hs_unlock();

    for (i = 0; i < 4; i++) {
        rassm->filter_bitmask[i] = 0xffff;
    }
}

static void
ble_svc_ras_clt_on_mtu(struct ble_gap_event *event)
{
    struct ble_svc_ras_clt_sm *rassm;

    rassm = ble_svc_ras_clt_sm_get(event->mtu.conn_handle);
    if (rassm == NULL || event->mtu.channel_id != BLE_L2CAP_CID_ATT) {
        return;
    }

    rassm->mtu = event->mtu.value;
}

static int
ble_svc_ras_clt_cp_on_ranging_data_ready(struct ble_svc_ras_clt_sm *rassm,
                                         struct os_mbuf *om)
{
    int rc;
    uint16_t ranging_counter;

    rc = os_mbuf_copydata(om, 0, sizeof(ranging_counter), &ranging_counter);
    if (rc != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    rassm->ranging_counter = get_le16(&ranging_counter);

    return ble_svc_ras_clt_cp_get_ranging_data(rassm, rassm->ranging_counter);
}

static void
ble_svc_ras_clt_on_notify_rx(struct ble_gap_event *event)
{
    struct ble_svc_ras_clt_sm *rassm;
    uint16_t attr_handle = event->notify_rx.attr_handle;

    rassm = ble_svc_ras_clt_sm_get(event->notify_rx.conn_handle);
    if (rassm == NULL) {
        return;
    }

    if (attr_handle == rassm->ranging_data_ready_val_handle) {
        ble_svc_ras_clt_cp_on_ranging_data_ready(rassm, event->notify_rx.om);
    } else if (attr_handle == rassm->real_time_ranging_data_val_handle ||
               attr_handle == rassm->on_demand_ranging_data_val_handle) {
        ble_svc_ras_clt_notification_parse(event->notify_rx.om,
                                           event->notify_rx.conn_handle);
    } else if (attr_handle == rassm->ranging_data_overwritten_val_handle) {
    } else if (attr_handle == rassm->control_point_val_handle) {
    }
}

static int
ble_svc_ras_clt_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ble_svc_ras_clt_on_connect(event);
        break;
    case BLE_GAP_EVENT_MTU:
        ble_svc_ras_clt_on_mtu(event);
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        ble_svc_ras_clt_on_notify_rx(event);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ble_svc_ras_clt_on_disconnect(event->disconnect.conn.conn_handle);
        break;
    default:
        break;
    }

    return 0;
}

static int
ble_cs_subscribe_indication(struct ble_svc_ras_clt_sm *rassm, ble_gatt_attr_fn *cb,
                            uint16_t svc_uuid, uint16_t chr_uuid)
{
    int rc;
    const struct ble_peer *peer;
    const struct ble_peer_dsc *dsc;
    uint8_t value[2];

    peer = ble_peer_find(rassm->conn_handle);
    if (peer == NULL) {
        return 1;
    }

    dsc = ble_peer_dsc_find_uuid(peer, BLE_UUID16_DECLARE(svc_uuid),
                                 BLE_UUID16_DECLARE(chr_uuid),
                                 BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16));

    value[0] = 0b10;
    value[1] = 0;
    rc = ble_gattc_write_flat(rassm->conn_handle, dsc->dsc.handle, value,
                              sizeof(value), cb, rassm);

    return rc;
}

static int
ble_cs_subscribe_control_point_cb(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  struct ble_gatt_attr *attr, void *arg)
{
    struct ble_svc_ras_clt_sm *rassm = arg;

    if (rassm->subscribe_cb) {
        rassm->subscribe_cb(conn_handle);
    }

    return 0;
}

static int
ble_cs_subscribe_ranging_data_overwritten_cb(uint16_t conn_handle,
                                             const struct ble_gatt_error *error,
                                             struct ble_gatt_attr *attr, void *arg)
{
    return ble_cs_subscribe_indication(arg, ble_cs_subscribe_control_point_cb,
                                       BLE_SVC_RAS_SVC_RANGING_SERVICE_UUID,
                                       BLE_SVC_RAS_CHR_RAS_CONTROL_POINT_UUID);
}

static int
ble_cs_subscribe_ranging_data_ready_cb(uint16_t conn_handle,
                                       const struct ble_gatt_error *error,
                                       struct ble_gatt_attr *attr, void *arg)
{
    return ble_cs_subscribe_indication(arg, ble_cs_subscribe_ranging_data_overwritten_cb,
                                       BLE_SVC_RAS_SVC_RANGING_SERVICE_UUID,
                                       BLE_SVC_RAS_CHR_RANGING_DATA_OVERWRITTEN_UUID);
}

static int
ble_cs_subscribe_on_demand_ranging_data_cb(uint16_t conn_handle,
                                           const struct ble_gatt_error *error,
                                           struct ble_gatt_attr *attr, void *arg)
{
    return ble_cs_subscribe_indication(arg, ble_cs_subscribe_ranging_data_ready_cb,
                                       BLE_SVC_RAS_SVC_RANGING_SERVICE_UUID,
                                       BLE_SVC_RAS_CHR_RANGING_DATA_READY_UUID);
}

static int
ble_cs_subscribe_real_time_ranging_data_cb(uint16_t conn_handle,
                                           const struct ble_gatt_error *error,
                                           struct ble_gatt_attr *attr, void *arg)
{
    return ble_cs_subscribe_indication(arg, ble_cs_subscribe_control_point_cb,
                                       BLE_SVC_RAS_SVC_RANGING_SERVICE_UUID,
                                       BLE_SVC_RAS_CHR_RAS_CONTROL_POINT_UUID);
}

int
ble_svc_ras_clt_config_set(uint16_t conn_handle, uint8_t rtt_pct_included,
                           uint8_t n_ap, uint8_t local_role)
{
    struct ble_svc_ras_clt_sm *rassm = NULL;

    rassm = ble_svc_ras_clt_sm_get(conn_handle);
    assert(rassm != NULL);

    rassm->rtt_pct_included = rtt_pct_included;
    rassm->n_ap = n_ap;
    rassm->local_role = local_role;

    return 0;
}

int
ble_svc_ras_clt_subscribe(ble_svc_ras_clt_subscribe_cb *subscribe_cb,
                          ble_svc_ras_clt_step_data_received_cb *step_data_cb,
                          uint16_t conn_handle, uint8_t mode)
{
    struct ble_svc_ras_clt_sm *rassm = NULL;
    const struct ble_peer *peer;
    const struct ble_peer_chr *chr;
    ble_gatt_attr_fn *cb;
    uint16_t chr_uuid;

    rassm = ble_svc_ras_clt_sm_get(conn_handle);
    assert(rassm != NULL);
    rassm->subscribe_cb = subscribe_cb;
    rassm->step_data_cb = step_data_cb;

    peer = ble_peer_find(conn_handle);
    if (peer == NULL) {
        return 1;
    }

    chr = ble_peer_chr_find_uuid(
        peer, BLE_UUID16_DECLARE(BLE_SVC_RAS_SVC_RANGING_SERVICE_UUID),
        BLE_UUID16_DECLARE(BLE_SVC_RAS_CHR_REAL_TIME_RANGING_DATA_UUID));
    rassm->real_time_ranging_data_val_handle = chr->chr.val_handle;

    chr = ble_peer_chr_find_uuid(
        peer, BLE_UUID16_DECLARE(BLE_SVC_RAS_SVC_RANGING_SERVICE_UUID),
        BLE_UUID16_DECLARE(BLE_SVC_RAS_CHR_ON_DEMAND_RANGING_DATA_UUID));
    rassm->on_demand_ranging_data_val_handle = chr->chr.val_handle;

    chr = ble_peer_chr_find_uuid(
        peer, BLE_UUID16_DECLARE(BLE_SVC_RAS_SVC_RANGING_SERVICE_UUID),
        BLE_UUID16_DECLARE(BLE_SVC_RAS_CHR_RAS_CONTROL_POINT_UUID));
    rassm->control_point_val_handle = chr->chr.val_handle;

    chr = ble_peer_chr_find_uuid(
        peer, BLE_UUID16_DECLARE(BLE_SVC_RAS_SVC_RANGING_SERVICE_UUID),
        BLE_UUID16_DECLARE(BLE_SVC_RAS_CHR_RANGING_DATA_READY_UUID));
    rassm->ranging_data_ready_val_handle = chr->chr.val_handle;

    chr = ble_peer_chr_find_uuid(
        peer, BLE_UUID16_DECLARE(BLE_SVC_RAS_SVC_RANGING_SERVICE_UUID),
        BLE_UUID16_DECLARE(BLE_SVC_RAS_CHR_RANGING_DATA_OVERWRITTEN_UUID));
    rassm->ranging_data_overwritten_val_handle = chr->chr.val_handle;

    if (mode == BLE_SVC_RAS_MODE_ON_DEMAND) {
        cb = ble_cs_subscribe_on_demand_ranging_data_cb;
        chr_uuid = BLE_SVC_RAS_CHR_ON_DEMAND_RANGING_DATA_UUID;
    } else {
        cb = ble_cs_subscribe_real_time_ranging_data_cb;
        chr_uuid = BLE_SVC_RAS_CHR_REAL_TIME_RANGING_DATA_UUID;
    }

    return ble_cs_subscribe_indication(
        rassm, cb, BLE_SVC_RAS_SVC_RANGING_SERVICE_UUID, chr_uuid);
}

void
ble_svc_ras_clt_init(void)
{
    int rc;
    uint8_t i;

    for (i = 0; i < ARRAY_SIZE(g_ble_ras_clt_sm); i++) {
        g_ble_ras_clt_sm[i].conn_handle = BLE_CONN_HANDLE_INVALID;
    }

    rc = os_mempool_init(&ras_clt_mempool, MBUF_POOL_BUF_COUNT, MBUF_POOL_BUF_SIZE,
                         &ras_clt_mbuf_area[0], "ras_clt_mbuf_pool");
    assert(rc == 0);

    rc = os_mbuf_pool_init(&ras_clt_mbuf_pool, &ras_clt_mempool,
                           MBUF_POOL_BUF_SIZE, MBUF_POOL_BUF_COUNT);
    assert(rc == 0);

    rc = ble_gap_event_listener_register(&ble_svc_ras_clt_listener,
                                         ble_svc_ras_clt_gap_event, NULL);
    assert(rc == 0);
}
