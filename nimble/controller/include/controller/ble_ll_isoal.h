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

#ifndef H_BLE_LL_ISOAL_
#define H_BLE_LL_ISOAL_

#include <os/os_mbuf.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_LL_ISOAL_SEGHDR(sc, cmplt, len)                                   \
    ((uint16_t)((sc) & 0x01) | (((cmplt) & 0x01) << 1) | ((len) & 0xff) << 8)

#define BLE_LL_ISOAL_SEGHDR_SC(word)    ((word) & 0x01)
#define BLE_LL_ISOAL_SEGHDR_CMPLT(word) ((word >> 1) & 0x01)
#define BLE_LL_ISOAL_SEGHDR_LEN(word)   ((word >> 8) & 0xff)

#define BLE_LL_ISOAL_TIME_OFFSET_LEN 3

struct ble_ll_isoal_config {
    /* ISO Interval */
    uint32_t iso_interval_us;
    /* SDU Interval */
    uint32_t sdu_interval_us;
    /* Maximum SDU size */
    uint16_t max_sdu;
    /* Max PDU length */
    uint8_t max_pdu;
    /* Burst Number */
    uint8_t bn;
    /**/
    uint8_t pte;
    /* Framed */
    uint8_t framed : 1;
    /* Framing mode = segmentable/unsegmented */
    uint8_t unsegmented : 1;
};

struct ble_ll_isoal_mux {
    struct ble_ll_isoal_config config;
#if MYNEWT_VAL(BLE_LL_ISOAL_MUX_PREFILL)
    uint8_t active;
#endif
    /* Number of expected SDUs per ISO interval */
    uint8_t sdu_per_interval;
    /* Number of expected PDUs per SDU */
    uint8_t pdu_per_sdu;
    /* Number of SDUs required to fill complete BIG/CIG event (i.e. with pt) */
    uint8_t sdu_per_event;
    /* Number of SDUs available for the current event */
    uint8_t sdu_in_event;

    STAILQ_HEAD(, os_mbuf_pkthdr) sdu_q;
    uint16_t sdu_q_len;

    uint32_t sdu_counter;

    uint32_t event_tx_timestamp;
    uint32_t last_tx_timestamp;
    uint16_t last_tx_packet_seq_num;

    /* The head SDU Segment is the Continuation of an SDU */
    uint8_t sc : 1;
};

void ble_ll_isoal_mux_init(struct ble_ll_isoal_mux *mux,
                           struct ble_ll_isoal_config *config);
void ble_ll_isoal_mux_reset(struct ble_ll_isoal_mux *mux);
int ble_ll_isoal_mux_event_start(struct ble_ll_isoal_mux *mux, uint32_t timestamp);
int ble_ll_isoal_mux_event_done(struct ble_ll_isoal_mux *mux);
int ble_ll_isoal_mux_pdu_get(struct ble_ll_isoal_mux *mux, uint8_t idx,
                             uint8_t *llid, void *dptr);
void ble_ll_isoal_mux_sdu_put(struct ble_ll_isoal_mux *mux, struct os_mbuf *om);

/* Forward declaration */
struct ble_ll_isoal_demux;

/* ISOAL Demultiplexer callback structure */
struct ble_ll_isoal_demux_cb {
    void (*sdu_cb)(struct ble_ll_isoal_demux *demux, const struct os_mbuf *om,
                   uint32_t timestamp, uint16_t seq_num, bool valid);
};

struct ble_ll_isoal_demux {
    struct ble_ll_isoal_config config;

    uint8_t active;

    /* Number of expected SDUs per ISO interval */
    uint8_t sdu_per_interval;
    /* Number of expected PDUs per SDU */
    uint8_t pdu_per_sdu;
    /* Number of SDUs expected for the current event */
    uint8_t sdu_in_event;

    STAILQ_HEAD(, os_mbuf_pkthdr) pdu_q;

    uint32_t sdu_counter;

    uint32_t ref_time;
    uint32_t last_rx_timestamp;

    struct os_mbuf *frag;

    const struct ble_ll_isoal_demux_cb *cb;
};

void ble_ll_isoal_demux_init(struct ble_ll_isoal_demux *demux,
                             struct ble_ll_isoal_config *config);
void ble_ll_isoal_demux_reset(struct ble_ll_isoal_demux *demux);
int ble_ll_isoal_demux_event_start(struct ble_ll_isoal_demux *demux, uint32_t timestamp);
int ble_ll_isoal_demux_event_done(struct ble_ll_isoal_demux *demux);
void ble_ll_isoal_demux_pdu_put(struct ble_ll_isoal_demux *demux, uint8_t idx,
                                struct os_mbuf *om);

static inline void
ble_ll_isoal_demux_cb_set(struct ble_ll_isoal_demux *demux,
                          const struct ble_ll_isoal_demux_cb *cb)
{
    demux->cb = cb;
}

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_LL_ISOAL_ */
