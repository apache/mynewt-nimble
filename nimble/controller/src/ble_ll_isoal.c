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

#include <stdint.h>
#include <syscfg/syscfg.h>
#include <controller/ble_ll.h>
#include <controller/ble_ll_isoal.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#if MYNEWT_VAL(BLE_LL_ISO)

void
ble_ll_isoal_mux_init(struct ble_ll_isoal_mux *mux,
                      const struct ble_ll_isoal_config *config)
{
    memset(mux, 0, sizeof(*mux));
    mux->config = *config;

    /* Core 5.3, Vol 6, Part G, 2.1 */
    mux->sdu_per_interval = config->iso_interval_us / config->sdu_interval_us;

    if (config->framed) {
        /* TODO */
    } else {
        mux->pdu_per_sdu = config->bn / mux->sdu_per_interval;
    }

    mux->sdu_per_event = (1 + config->pte) * mux->sdu_per_interval;

    STAILQ_INIT(&mux->sdu_q);
    mux->sdu_q_len = 0;
}

void
ble_ll_isoal_mux_free(struct ble_ll_isoal_mux *mux)
{
    struct os_mbuf_pkthdr *pkthdr;

    pkthdr = STAILQ_FIRST(&mux->sdu_q);
    while (pkthdr) {
        /* remove from list before freeing om */
        STAILQ_REMOVE_HEAD(&mux->sdu_q, omp_next);

        os_mbuf_free_chain(OS_MBUF_PKTHDR_TO_MBUF(pkthdr));

        pkthdr = STAILQ_FIRST(&mux->sdu_q);
    }

    STAILQ_INIT(&mux->sdu_q);
    mux->sdu_q_len = 0;
}

void
ble_ll_isoal_mux_sdu_put(struct ble_ll_isoal_mux *mux, struct os_mbuf *om)
{
    struct os_mbuf_pkthdr *pkthdr;
    os_sr_t sr;

    BLE_LL_ASSERT(mux);

    OS_ENTER_CRITICAL(sr);
    pkthdr = OS_MBUF_PKTHDR(om);
    STAILQ_INSERT_TAIL(&mux->sdu_q, pkthdr, omp_next);
    mux->sdu_q_len++;
#if MYNEWT_VAL(BLE_LL_ISOAL_MUX_PREFILL)
    if (mux->sdu_q_len >= mux->sdu_per_event) {
        mux->active = 1;
    }
#endif
    OS_EXIT_CRITICAL(sr);
}

uint32_t
ble_ll_isoal_mux_sdu_timestamp(const struct ble_ll_isoal_mux *mux,
                               uint32_t rx_time, uint32_t time_stamp)
{
    int32_t range_us;
    int32_t diff;

    /* Time_Stamp far from the time of reception is not based on the
     * Controller clock (Vol 6, Part G, 3.3), so it is ignored.
     */
    range_us = mux->config.iso_interval_us + mux->config.sdu_interval_us;
    diff = time_stamp - rx_time;
    if ((diff < -range_us) || (diff > range_us)) {
        return rx_time;
    }

    return time_stamp;
}

#if MYNEWT_VAL(BLE_LL_ISOAL_MUX_DROP_LATE)
static void
ble_ll_isoal_mux_drop_late(struct ble_ll_isoal_mux *mux)
{
    struct os_mbuf_pkthdr *pkthdr;
    struct os_mbuf *om;
    struct os_mbuf *om_next;
    os_sr_t sr;

    if (mux->config.framed) {
        return;
    }

    while (mux->sdu_q_len > mux->sdu_per_event) {
        OS_ENTER_CRITICAL(sr);
        pkthdr = STAILQ_FIRST(&mux->sdu_q);
        STAILQ_REMOVE_HEAD(&mux->sdu_q, omp_next);
        mux->sdu_q_len--;
        OS_EXIT_CRITICAL(sr);

        om = OS_MBUF_PKTHDR_TO_MBUF(pkthdr);
        while (om) {
            om_next = SLIST_NEXT(om, om_next);
            os_mbuf_free(om);
            mux->dropped_pkt++;
            om = om_next;
        }
    }
}
#endif

static uint8_t
ble_ll_isoal_mux_framed_num_sdu_ready(struct ble_ll_isoal_mux *mux, uint32_t timestamp)
{
    struct os_mbuf_pkthdr *pkthdr;
    struct ble_mbuf_hdr *blehdr;
    uint8_t num_sdu = 0;

    STAILQ_FOREACH(pkthdr, &mux->sdu_q, omp_next) {
        blehdr = BLE_MBUF_HDR_PTR(OS_MBUF_PKTHDR_TO_MBUF(pkthdr));
        if ((int32_t)(timestamp - blehdr->txiso.sdu_timestamp) <= 0) {
            break;
        }
        num_sdu++;
    }

    return num_sdu;
}

int
ble_ll_isoal_mux_event_start(struct ble_ll_isoal_mux *mux, uint32_t timestamp)
{
#if MYNEWT_VAL(BLE_LL_ISOAL_MUX_DROP_LATE)
    ble_ll_isoal_mux_drop_late(mux);
#endif

#if MYNEWT_VAL(BLE_LL_ISOAL_MUX_PREFILL)
    /* If prefill is enabled, we always expect to have required number of SDUs
     * in queue, otherwise we disable mux until enough SDUs are queued again.
     */
    if (mux->sdu_per_event > mux->sdu_q_len) {
        mux->active = 0;
    }
    if (mux->active && mux->config.framed) {
        mux->sdu_in_event = ble_ll_isoal_mux_framed_num_sdu_ready(mux, timestamp);
    } else if (mux->active) {
        mux->sdu_in_event = mux->sdu_per_event;
    } else {
        mux->sdu_in_event = 0;
    }
#else
    if (mux->config.framed) {
        mux->sdu_in_event = ble_ll_isoal_mux_framed_num_sdu_ready(mux, timestamp);
    } else {
        mux->sdu_in_event = min(mux->sdu_q_len, mux->sdu_per_event);
    }
#endif
    mux->event_tx_timestamp = timestamp;

    return mux->sdu_in_event;
}

static int
ble_ll_isoal_mux_unframed_event_done(struct ble_ll_isoal_mux *mux)
{
    struct os_mbuf_pkthdr *pkthdr;
    struct os_mbuf *om;
    struct os_mbuf *om_next;
    uint8_t num_sdu;
    int pkt_freed = 0;
    os_sr_t sr;

    num_sdu = min(mux->sdu_in_event, mux->sdu_per_interval);

#if MYNEWT_VAL(BLE_LL_ISO_HCI_DISCARD_THRESHOLD)
    /* Drop queued SDUs if number of queued SDUs exceeds defined threshold.
     * Threshold is defined as number of ISO events. If number of queued SDUs
     * exceeds number of SDUs required for single event (i.e. including pt)
     * and number of subsequent ISO events defined by threshold value, we'll
     * drop any excessive SDUs and notify host as if they were sent.
     */
    uint32_t thr = MYNEWT_VAL(BLE_LL_ISO_HCI_DISCARD_THRESHOLD);
    if (mux->sdu_q_len > mux->sdu_per_event + thr * mux->sdu_per_interval) {
        num_sdu = mux->sdu_q_len - mux->sdu_per_event -
                  thr * mux->sdu_per_interval;
    }
#endif

    pkthdr = STAILQ_FIRST(&mux->sdu_q);
    while (pkthdr && num_sdu--) {
        OS_ENTER_CRITICAL(sr);
        STAILQ_REMOVE_HEAD(&mux->sdu_q, omp_next);
        BLE_LL_ASSERT(mux->sdu_q_len > 0);
        mux->sdu_q_len--;
        OS_EXIT_CRITICAL(sr);

        om = OS_MBUF_PKTHDR_TO_MBUF(pkthdr);
        while (om) {
            om_next = SLIST_NEXT(om, om_next);
            os_mbuf_free(om);
            pkt_freed++;
            om = om_next;
        }

        pkthdr = STAILQ_FIRST(&mux->sdu_q);
    }

    mux->sdu_in_event = 0;

    return pkt_freed;
}

static struct os_mbuf *
ble_ll_isoal_sdu_trim(struct os_mbuf *om, int *pkt_freed)
{
    /* Count the number of packets that will be freed. */
    for (struct os_mbuf *mbuf = om; mbuf; mbuf = SLIST_NEXT(mbuf, om_next)) {
        if (mbuf->om_len == 0) {
            (*pkt_freed)++;
        }
    }

    return os_mbuf_trim_front(om);
}

static int
ble_ll_isoal_mux_framed_event_done(struct ble_ll_isoal_mux *mux)
{
    struct os_mbuf_pkthdr *pkthdr;
    struct os_mbuf_pkthdr *pkthdr_temp;
    struct os_mbuf *om;
    uint8_t num_sdu;
    uint8_t num_pdu;
    uint8_t pdu_len = 0;
    uint8_t frag_len = 0;
    uint8_t hdr_len = 0;
    int pkt_freed = 0;
    os_sr_t sr;

    num_sdu = mux->sdu_in_event;
    if (num_sdu == 0) {
        return 0;
    }

    num_pdu = mux->config.bn;

#if MYNEWT_VAL(BLE_LL_ISO_HCI_DISCARD_THRESHOLD)
    /* Drop queued SDUs if number of queued SDUs exceeds defined threshold.
     * Threshold is defined as number of ISO events. If number of queued SDUs
     * exceeds number of SDUs required for single event (i.e. including pt)
     * and number of subsequent ISO events defined by threshold value, we'll
     * drop any excessive SDUs and notify host as if they were sent.
     */
    uint32_t thr = MYNEWT_VAL(BLE_LL_ISO_HCI_DISCARD_THRESHOLD);
    if (mux->sdu_q_len > mux->sdu_per_event + thr * mux->sdu_per_interval) {
        num_sdu = mux->sdu_q_len - mux->sdu_per_event -
                  thr * mux->sdu_per_interval;
    }
#endif

    /* Iterate over all queued SDUs. */
    STAILQ_FOREACH_SAFE(pkthdr, &mux->sdu_q, omp_next, pkthdr_temp) {
        if (num_sdu == 0) {
            break;
        }

        BLE_LL_ASSERT(mux->sdu_q_len > 0);

        /* Remove the SDU from the queue, as we are about to free some buffers from a chain.
         * Otherwise, it will result in invalid head pointed by sdu_q entry.
         */
        OS_ENTER_CRITICAL(sr);
        STAILQ_REMOVE_HEAD(&mux->sdu_q, omp_next);
        mux->sdu_q_len--;
        OS_EXIT_CRITICAL(sr);

        om = OS_MBUF_PKTHDR_TO_MBUF(pkthdr);

        /* Iterate over all buffers in the SDU chain. */
        while (num_pdu > 0 && OS_MBUF_PKTLEN(om) > 0) {
            hdr_len = mux->sc ? 2 /* Segmentation Header */
                              : 5 /* Segmentation Header + TimeOffset */;

            /* If the next SDU fragment header size exceeds remaining space in the PDU, advance to next PDU. */
            if (mux->config.max_pdu <= hdr_len + pdu_len) {
                pdu_len = 0;
                num_pdu--;
                continue;
            }

            /* "Put" the header in the PDU. */
            pdu_len += hdr_len;

            /* SDU fragment length that can be fit in the PDU. */
            frag_len = min(OS_MBUF_PKTLEN(om), mux->config.max_pdu - pdu_len);

            /* Increase PDU length by the length of the SDU fragment. */
            pdu_len += frag_len;

            os_mbuf_adj(om, frag_len);

            /* If the SDU fragment does not fit in the PDU, set the SC flag. */
            if (OS_MBUF_PKTLEN(om) > frag_len) {
                mux->sc = 1;
            } else {
                mux->sc = 0;
            }
        }

        om = ble_ll_isoal_sdu_trim(om, &pkt_freed);
        if (OS_MBUF_PKTLEN(om) > 0) {
            /* If there is still data in the SDU chain, place the SDU back in the queue. */
            OS_ENTER_CRITICAL(sr);
            STAILQ_INSERT_HEAD(&mux->sdu_q, pkthdr, omp_next);
            mux->sdu_q_len++;
            OS_EXIT_CRITICAL(sr);
        } else {
            /* If there are no more data in the SDU chain, free the SDU chain. */
            os_mbuf_free_chain(om);
            num_sdu--;
        }

        if (num_pdu == 0) {
            break;
        }
    }

    mux->sdu_in_event = 0;

    return pkt_freed;
}

int
ble_ll_isoal_mux_event_done(struct ble_ll_isoal_mux *mux)
{
    struct os_mbuf_pkthdr *pkthdr;
    struct ble_mbuf_hdr *blehdr;
    struct os_mbuf *om;
#if MYNEWT_VAL(BLE_LL_ISOAL_MUX_DROP_LATE)
    int pkt_freed;
#endif

    pkthdr = STAILQ_FIRST(&mux->sdu_q);
    if (pkthdr) {
        om = OS_MBUF_PKTHDR_TO_MBUF(pkthdr);
        blehdr = BLE_MBUF_HDR_PTR(om);
        mux->last_tx_timestamp = mux->event_tx_timestamp;
        mux->last_tx_packet_seq_num = blehdr->txiso.packet_seq_num;
    }

    if (mux->config.framed) {
        return ble_ll_isoal_mux_framed_event_done(mux);
    }

#if MYNEWT_VAL(BLE_LL_ISOAL_MUX_DROP_LATE)
    pkt_freed = mux->dropped_pkt;
    mux->dropped_pkt = 0;

    return pkt_freed + ble_ll_isoal_mux_unframed_event_done(mux);
#else
    return ble_ll_isoal_mux_unframed_event_done(mux);
#endif
}

static int
ble_ll_isoal_mux_unframed_get(struct ble_ll_isoal_mux *mux, uint8_t idx,
                              uint8_t *llid, void *dptr)
{
    struct os_mbuf_pkthdr *pkthdr;
    struct os_mbuf *om;
    int32_t rem_len;
    uint8_t sdu_idx;
    uint8_t pdu_idx;
    uint16_t sdu_offset;
    uint8_t pdu_len;

    sdu_idx = idx / mux->pdu_per_sdu;
    pdu_idx = idx - sdu_idx * mux->pdu_per_sdu;

    if (sdu_idx >= mux->sdu_in_event) {
        *llid = 0;
        return 0;
    }

    pkthdr = STAILQ_FIRST(&mux->sdu_q);
    while (pkthdr && sdu_idx--) {
        pkthdr = STAILQ_NEXT(pkthdr, omp_next);
    }

    if (!pkthdr) {
        *llid = 0;
        return 0;
    }

    om = OS_MBUF_PKTHDR_TO_MBUF(pkthdr);
    sdu_offset = pdu_idx * mux->config.max_pdu;
    rem_len = OS_MBUF_PKTLEN(om) - sdu_offset;

    if (OS_MBUF_PKTLEN(om) == 0) {
        /* LLID = 0b00: Zero-Length SDU (complete SDU) */
        *llid = 0;
        pdu_len = 0;
    } else if (rem_len <= 0) {
        /* LLID = 0b01: ISO Data PDU used as padding */
        *llid = 1;
        pdu_len = 0;
    } else {
        /* LLID = 0b00: Data remaining fits the ISO Data PDU size,
         *              it's end fragment of an SDU or complete SDU.
         * LLID = 0b01: Data remaining exceeds the ISO Data PDU size,
         *              it's start or continuation fragment of an SDU.
         */
        *llid = rem_len > mux->config.max_pdu;
        pdu_len = min(mux->config.max_pdu, rem_len);
    }

    os_mbuf_copydata(om, sdu_offset, pdu_len, dptr);

    return pdu_len;
}

static int
ble_ll_isoal_mux_framed_get(struct ble_ll_isoal_mux *mux, uint8_t idx,
                            uint8_t *llid, uint8_t *dptr)
{
    struct ble_mbuf_hdr *blehdr;
    struct os_mbuf_pkthdr *pkthdr;
    struct os_mbuf *om;
    uint32_t time_offset;
    uint16_t seghdr;
    uint16_t rem_len = 0;
    uint16_t sdu_offset = 0;
    uint8_t num_sdu;
    uint8_t num_pdu;
    uint8_t frag_len;
    uint8_t pdu_offset = 0;
    bool sc = mux->sc;
    uint8_t hdr_len = 0;

    *llid = 0b10;

    num_sdu = mux->sdu_in_event;
    if (num_sdu == 0) {
        return 0;
    }

    num_pdu = idx;

    /* Skip the idx PDUs */
    pkthdr = STAILQ_FIRST(&mux->sdu_q);
    while (pkthdr && num_sdu > 0 && num_pdu > 0) {
        om = OS_MBUF_PKTHDR_TO_MBUF(pkthdr);

        rem_len = OS_MBUF_PKTLEN(om) - sdu_offset;
        hdr_len = sc ? 2 /* Segmentation Header */
                     : 5 /* Segmentation Header + TimeOffset */;

        if (mux->config.max_pdu <= hdr_len + pdu_offset) {
            /* Advance to next PDU */
            pdu_offset = 0;
            num_pdu--;
            continue;
        }

        frag_len = min(rem_len, mux->config.max_pdu - hdr_len - pdu_offset);

        pdu_offset += hdr_len + frag_len;

        if (frag_len == rem_len) {
            /* Process next SDU */
            sdu_offset = 0;
            num_sdu--;
            pkthdr = STAILQ_NEXT(pkthdr, omp_next);

            sc = 0;
        } else {
            sdu_offset += frag_len;

            sc = 1;
        }
    }

    if (num_pdu > 0) {
        return 0;
    }

    BLE_LL_ASSERT(pdu_offset == 0);

    while (pkthdr && num_sdu > 0) {
        om = OS_MBUF_PKTHDR_TO_MBUF(pkthdr);

        rem_len = OS_MBUF_PKTLEN(om) - sdu_offset;
        hdr_len = sc ? 2 /* Segmentation Header */
                     : 5 /* Segmentation Header + TimeOffset */;

        if (mux->config.max_pdu <= hdr_len + pdu_offset) {
            break;
        }

        frag_len = min(rem_len, mux->config.max_pdu - hdr_len - pdu_offset);

        /* Segmentation Header */
        seghdr = BLE_LL_ISOAL_SEGHDR(sc, frag_len == rem_len, frag_len + hdr_len - 2);
        put_le16(dptr + pdu_offset, seghdr);
        pdu_offset += 2;

        /* Time Offset */
        if (hdr_len > 2) {
            blehdr = BLE_MBUF_HDR_PTR(om);

            time_offset = mux->event_tx_timestamp - blehdr->txiso.sdu_timestamp;
            put_le24(dptr + pdu_offset, time_offset);
            pdu_offset += 3;
        }

        /* ISO Data Fragment */
        os_mbuf_copydata(om, sdu_offset, frag_len, dptr + pdu_offset);
        pdu_offset += frag_len;

        if (frag_len == rem_len) {
            /* Process next SDU */
            sdu_offset = 0;
            num_sdu--;
            pkthdr = STAILQ_NEXT(pkthdr, omp_next);

            sc = 0;
        } else {
            sdu_offset += frag_len;

            sc = 1;
        }
    }

    return pdu_offset;
}

int
ble_ll_isoal_mux_pdu_get(struct ble_ll_isoal_mux *mux, uint8_t idx,
                         uint8_t *llid, void *dptr)
{
    if (mux->config.framed) {
        return ble_ll_isoal_mux_framed_get(mux, idx, llid, dptr);
    }

    return ble_ll_isoal_mux_unframed_get(mux, idx, llid, dptr);
}

void
ble_ll_isoal_demux_init(struct ble_ll_isoal_demux *demux,
                        const struct ble_ll_isoal_config *config)
{
    memset(demux, 0, sizeof(*demux));
    demux->config = *config;

    if (!config->framed) {
        /* Core 5.3, Vol 6, Part G, 2.1 */
        demux->sdu_per_interval = config->iso_interval_us / config->sdu_interval_us;
        demux->pdu_per_sdu = config->bn / demux->sdu_per_interval;
    }

    STAILQ_INIT(&demux->pdu_q);
}

void
ble_ll_isoal_demux_reset(struct ble_ll_isoal_demux *demux)
{
    struct os_mbuf_pkthdr *pkthdr;
    struct os_mbuf *om;
    struct os_mbuf *om_next;

    pkthdr = STAILQ_FIRST(&demux->pdu_q);
    while (pkthdr) {
        om = OS_MBUF_PKTHDR_TO_MBUF(pkthdr);

        while (om) {
            om_next = SLIST_NEXT(om, om_next);
            os_mbuf_free(om);
            om = om_next;
        }

        STAILQ_REMOVE_HEAD(&demux->pdu_q, omp_next);
        pkthdr = STAILQ_FIRST(&demux->pdu_q);
    }

    STAILQ_INIT(&demux->pdu_q);

    if (demux->frag) {
        os_mbuf_free_chain(demux->frag);
        demux->frag = NULL;
    }
}

int
ble_ll_isoal_demux_event_start(struct ble_ll_isoal_demux *demux, uint32_t timestamp)
{
    const struct ble_ll_isoal_config *config = &demux->config;

    if (config->framed) {
        demux->sdu_in_event = 0;
    } else {
        demux->sdu_in_event = demux->sdu_per_interval;
    }

    demux->ref_time = timestamp;

    return demux->sdu_in_event;
}

static void
ble_ll_isoal_demux_sdu_emit(struct ble_ll_isoal_demux *demux,
                            struct os_mbuf *om, uint32_t timestamp, bool valid)
{
    const struct ble_ll_isoal_config *config = &demux->config;
    struct os_mbuf *sdu;
    uint16_t sdu_len;

    sdu = om;
    sdu_len = sdu != NULL ? os_mbuf_len(sdu) : 0;

    /* Core 6.0 | Vol 6, Part G, 4
     * SDUs with a length exceeding Max_SDU. In this case, the length of the
     * SDU reported to the upper layer shall not exceed the Max_SDU length. The
     * SDU shall be truncated to Max_SDU octets.
     */
    if (sdu_len > config->max_sdu) {
        BLE_LL_ASSERT(sdu != NULL);
        os_mbuf_adj(sdu, -(sdu_len - config->max_sdu));
        valid = false;
    } else if (sdu_len == 0 && !valid) {
        /* If the SDU is empty and the SDU was reported as having errors,
         * the SDU shall be discarded and reported as lost data.
         */
        sdu = NULL;
    }

    if (demux->cb != NULL && demux->cb->sdu_cb != NULL) {
        demux->cb->sdu_cb(demux, sdu, timestamp, ++demux->sdu_counter, valid);
    }

    if (om != NULL) {
        os_mbuf_free_chain(om);
    }

    if (!demux->active) {
        demux->last_rx_timestamp = timestamp;
        demux->active = 1;
    } else if ((int32_t)(timestamp - demux->last_rx_timestamp) <
               (int32_t)config->sdu_interval_us) {
        demux->last_rx_timestamp += config->sdu_interval_us;
    } else {
        demux->last_rx_timestamp = timestamp;
    }
    demux->sdu_in_event--;
}

static uint8_t
pdu_idx_get(struct os_mbuf *om)
{
    struct ble_mbuf_hdr *hdr;

    hdr = BLE_MBUF_HDR_PTR(om);

    return POINTER_TO_UINT(hdr->rxinfo.user_data);
}

static void
pdu_idx_set(struct os_mbuf *om, uint8_t pdu_idx)
{
    struct ble_mbuf_hdr *hdr;

    hdr = BLE_MBUF_HDR_PTR(om);

    hdr->rxinfo.user_data = UINT_TO_POINTER(pdu_idx);
}

static void
sdu_ts_set(struct os_mbuf *om, uint32_t timestamp)
{
    struct ble_mbuf_hdr *hdr;

    hdr = BLE_MBUF_HDR_PTR(om);

    hdr->beg_cputime = timestamp;
}

static uint32_t
sdu_ts_get(struct os_mbuf *om)
{
    struct ble_mbuf_hdr *hdr;

    hdr = BLE_MBUF_HDR_PTR(om);

    return hdr->beg_cputime;
}

static uint8_t
ble_ll_isoal_demux_get_num_lost_sdu(struct ble_ll_isoal_demux *demux, uint32_t timestamp)
{
    const struct ble_ll_isoal_config *config = &demux->config;
    int32_t time_diff;

    time_diff = timestamp - demux->last_rx_timestamp - config->sdu_interval_us / 2;
    if (time_diff < 0) {
        return 0;
    }

    return time_diff / config->sdu_interval_us;
}

static void
ble_ll_isoal_demux_frag_flush(struct ble_ll_isoal_demux *demux)
{
    if (demux->frag != NULL) {
        ble_ll_isoal_demux_sdu_emit(demux, demux->frag, sdu_ts_get(demux->frag), false);
        demux->frag = NULL;
    }
}

static void
ble_ll_isoal_demux_reassemble(struct ble_ll_isoal_demux *demux)
{
    const struct ble_ll_isoal_config *config = &demux->config;
    struct os_mbuf_pkthdr *pdu_pkthdr;
    struct os_mbuf *pdu;
    struct os_mbuf *seg;
    uint32_t time_offset;
    uint32_t timestamp;
    uint16_t seghdr;
    uint8_t buf[BLE_LL_ISOAL_TIME_OFFSET_LEN];
    uint8_t hdr_byte;
    uint8_t llid;
    uint8_t len;
    uint8_t num_lost;
    bool sdu_continuation;
    bool pdu_missing;
    bool sdu_started;
    bool cmplt;
    bool valid;

    pdu_missing = false;
    sdu_started = false;
    pdu = NULL;
    for (uint8_t pdu_idx = 0; pdu_idx < config->bn;) {
        valid = true;

        if (pdu == NULL) {
            pdu_pkthdr = STAILQ_FIRST(&demux->pdu_q);
            if (pdu_pkthdr == NULL) {
                pdu_missing = true;
                ble_ll_isoal_demux_frag_flush(demux);
                break;
            }

            pdu = OS_MBUF_PKTHDR_TO_MBUF(pdu_pkthdr);

            if (pdu_idx_get(pdu) != pdu_idx) {
                pdu_missing = true;
                ble_ll_isoal_demux_frag_flush(demux);

                /* Clear 'pdu' to pull it again in the next iteration */
                pdu_idx += pdu_idx_get(pdu) - pdu_idx;
                pdu = NULL;
                continue;
            }

            hdr_byte = pdu->om_data[0];
            BLE_LL_ASSERT(BLE_LL_BIS_LLID_IS_DATA(hdr_byte));

            llid = hdr_byte & BLE_LL_BIS_PDU_HDR_LLID_MASK;
            if (llid != BLE_LL_BIS_LLID_DATA_PDU_FRAMED) {
                ble_ll_isoal_demux_frag_flush(demux);
                goto pdu_done;
            }

            /* Strip the header from the buffer to process only the payload data */
            os_mbuf_adj(pdu, BLE_LL_PDU_HDR_LEN);

            if (os_mbuf_len(pdu) == 0) {
                /* Padding */
                goto pdu_done;
            }
        }

        if (os_mbuf_copydata(pdu, 0, sizeof(seghdr), buf) != 0) {
            ble_ll_isoal_demux_frag_flush(demux);
            goto pdu_done;
        }

        seghdr = get_le16(buf);
        os_mbuf_adj(pdu, sizeof(seghdr));

        len = BLE_LL_ISOAL_SEGHDR_LEN(seghdr);
        if (os_mbuf_len(pdu) < len) {
            /* Valid if the segment length does not exceed the length of the buffer */
            len = os_mbuf_len(pdu);
            valid = false;
        }

        /* Duplicate and adjust buffer */
        seg = os_mbuf_dup(pdu);
        if (seg == NULL) {
            ble_ll_isoal_demux_frag_flush(demux);
            goto pdu_done;
        }

        sdu_continuation = BLE_LL_ISOAL_SEGHDR_SC(seghdr);
        if (!sdu_continuation) {
            /* SDU Start */
            if (os_mbuf_copydata(seg, 0, BLE_LL_ISOAL_TIME_OFFSET_LEN, buf) != 0) {
                os_mbuf_free_chain(seg);
                ble_ll_isoal_demux_frag_flush(demux);
                goto pdu_done;
            }

            time_offset = get_le24(buf);
            os_mbuf_adj(seg, BLE_LL_ISOAL_TIME_OFFSET_LEN);

            timestamp = sdu_ts_get(seg) - time_offset;

            sdu_ts_set(seg, timestamp);
        }

        /* Trim the 'pdu' head so that 'pdu->om_data' will point to the data behind SDU Segment */
        os_mbuf_adj(pdu, len);
        len = os_mbuf_len(pdu);

        /* Trim the 'seg' tail so that the 'seg' will contain SDU Segment only */
        os_mbuf_adj(seg, -len);

        if (sdu_continuation) {
            if (demux->frag == NULL) {
                /* Drop the segment if we do not have a start segment */
                os_mbuf_free_chain(seg);
            } else {
                demux->frag = os_mbuf_pack_chains(demux->frag, seg);
            }
        } else {
            ble_ll_isoal_demux_frag_flush(demux);

            /* Emit lost SDUs */
            num_lost =
                demux->active
                    ? ble_ll_isoal_demux_get_num_lost_sdu(demux, sdu_ts_get(seg))
                    : 0;
            for (uint8_t i = 0; i < num_lost; i++) {
                timestamp = demux->last_rx_timestamp + config->sdu_interval_us;
                ble_ll_isoal_demux_sdu_emit(demux, NULL, timestamp, false);
            }

            demux->frag = seg;
            sdu_started = true;
        }

        cmplt = BLE_LL_ISOAL_SEGHDR_CMPLT(seghdr);

        if (demux->frag != NULL && (cmplt || !valid)) {
            ble_ll_isoal_demux_sdu_emit(demux, demux->frag,
                                        sdu_ts_get(demux->frag), valid);
            demux->frag = NULL;
        }

        if (os_mbuf_len(pdu) > 0) {
            continue;
        }
    pdu_done:
        /* Free */
        STAILQ_REMOVE_HEAD(&demux->pdu_q, omp_next);
        os_mbuf_free_chain(pdu);
        pdu = NULL;

        pdu_idx++;
    }

    if (sdu_started && !pdu_missing) {
        timestamp = demux->ref_time - config->sdu_interval_us / 2;
    } else {
        timestamp = demux->ref_time + config->sdu_interval_us / 2;
    }
    num_lost =
        demux->active ? ble_ll_isoal_demux_get_num_lost_sdu(demux, timestamp) : 0;

    /* Emit lost SDUs */
    for (uint8_t i = 0; i < num_lost; i++) {
        timestamp = demux->last_rx_timestamp + config->sdu_interval_us;
        ble_ll_isoal_demux_sdu_emit(demux, demux->frag, timestamp, false);
        demux->frag = NULL;
    }
}

static void
ble_ll_isoal_demux_recombine(struct ble_ll_isoal_demux *demux)
{
    const struct ble_ll_isoal_config *config = &demux->config;
    struct os_mbuf_pkthdr *entry;
    struct os_mbuf *om;
    struct os_mbuf *sdu;
    uint32_t timestamp;
    uint8_t hdr_byte;
    uint8_t llid;
    bool cmplt;
    bool valid;

    entry = STAILQ_FIRST(&demux->pdu_q);
    for (uint8_t bn = 0; bn < config->bn; bn += demux->pdu_per_sdu) {
        sdu = NULL;
        valid = true;
        cmplt = false;

        for (uint8_t pdu_idx = bn; pdu_idx < bn + demux->pdu_per_sdu; pdu_idx++) {
            if (entry == NULL) {
                /* SDU may be complete already. If so, it means we lost padding. */
                valid &= cmplt;
                break;
            }

            om = OS_MBUF_PKTHDR_TO_MBUF(entry);

            if (pdu_idx_get(om) != pdu_idx) {
                /* SDU may be complete already. If so, it means we lost padding. */
                valid &= cmplt;
                continue;
            }

            /* Remove the PDU from queue */
            STAILQ_REMOVE_HEAD(&demux->pdu_q, omp_next);

            if (!valid) {
                os_mbuf_free_chain(om);
                entry = STAILQ_FIRST(&demux->pdu_q);
                continue;
            }

            hdr_byte = om->om_data[0];
            BLE_LL_ASSERT(BLE_LL_BIS_LLID_IS_DATA(hdr_byte));

            /* Strip the header from the buffer to process only the payload data */
            os_mbuf_adj(om, BLE_LL_PDU_HDR_LEN);

            llid = hdr_byte & BLE_LL_BIS_PDU_HDR_LLID_MASK;

            if (llid == BLE_LL_BIS_LLID_DATA_PDU_UNFRAMED_CMPLT) {
                /* Unframed BIS Data PDU; end fragment of an SDU or a complete SDU. */
                if (cmplt) {
                    /* Core 6.0 | Vol 6, Part G | 4
                     * Unframed SDUs without exactly one fragment with LLID=0b00 shall be
                     * discarded or reported as data with errors.
                     */
                    valid = false;
                }

                if (sdu != NULL) {
                    os_mbuf_concat(sdu, om);
                } else {
                    sdu = om;
                }
                cmplt = true;
            } else if (llid == BLE_LL_BIS_LLID_DATA_PDU_UNFRAMED_SC) {
                /* Unframed BIS Data PDU; start or continuation fragment of an SDU. */
                if (cmplt && os_mbuf_len(om) > 0) {
                    /* Core 6.0 | Vol 6, Part G | 4
                     * Unframed SDUs where the fragment with LLID=0b00 is followed by a fragment
                     * with LLID=0b01 and containing at least one octet of data shall be discarded
                     * or reported as data with errors.
                     */
                    valid = false;
                }

                if (sdu != NULL) {
                    os_mbuf_concat(sdu, om);
                } else {
                    sdu = om;
                }
            } else {
                os_mbuf_free_chain(om);
                valid = false;
            }

            entry = STAILQ_FIRST(&demux->pdu_q);
        }

        /* Core 5.4 | Vol 6, Part G; 3.2
         * All PDUs belonging to a burst as defined by the configuration of BN
         * have the same reference anchor point. When multiple SDUs have the
         * same reference anchor point, the first SDU uses the reference anchor
         * point timing. Each subsequent SDU increases the SDU synchronization
         * reference timing with one SDU interval.
         */
        timestamp = demux->ref_time + (bn / demux->pdu_per_sdu) * config->sdu_interval_us;

        ble_ll_isoal_demux_sdu_emit(demux, sdu, timestamp, valid && cmplt);
    }
}

int
ble_ll_isoal_demux_event_done(struct ble_ll_isoal_demux *demux)
{
    const struct ble_ll_isoal_config *config = &demux->config;
    struct os_mbuf_pkthdr *entry, *prev;
    struct os_mbuf *om;
    uint8_t idx;

    if (config->framed) {
        ble_ll_isoal_demux_reassemble(demux);
    } else {
        ble_ll_isoal_demux_recombine(demux);
    }

    prev = NULL;
    entry = STAILQ_FIRST(&demux->pdu_q);
    while (entry != NULL) {
        om = OS_MBUF_PKTHDR_TO_MBUF(entry);

        idx = pdu_idx_get(om);
        if (idx >= config->bn) {
            /* Pre-transmission - update payload index only */
            pdu_idx_set(om, idx - config->bn);

            prev = entry;
            entry = STAILQ_NEXT(entry, omp_next);
            continue;
        }

        /* Current event data */
        if (prev == NULL) {
            STAILQ_REMOVE_HEAD(&demux->pdu_q, omp_next);
        } else {
            STAILQ_REMOVE_AFTER(&demux->pdu_q, prev, omp_next);
        }

        if (prev == NULL) {
            entry = STAILQ_FIRST(&demux->pdu_q);
        } else {
            entry = STAILQ_NEXT(prev, omp_next);
        }

        os_mbuf_free_chain(om);
    }

    return 0;
}

void
ble_ll_isoal_demux_pdu_put(struct ble_ll_isoal_demux *demux, uint8_t idx,
                           struct os_mbuf *pdu)
{
    struct os_mbuf_pkthdr *entry, *prev;
    struct os_mbuf *om;

    sdu_ts_set(pdu, demux->ref_time + (idx / demux->config.bn) *
                                          demux->config.iso_interval_us);
    pdu_idx_set(pdu, idx);

    prev = NULL;
    entry = STAILQ_FIRST(&demux->pdu_q);
    while (entry) {
        om = OS_MBUF_PKTHDR_TO_MBUF(entry);

        if (pdu_idx_get(om) == idx) {
            /* Already queued */
            os_mbuf_free_chain(pdu);
            return;
        }

        if (pdu_idx_get(om) > idx) {
            /* Insert before */
            break;
        }

        prev = entry;
        entry = STAILQ_NEXT(entry, omp_next);
    }

    entry = OS_MBUF_PKTHDR(pdu);
    if (prev) {
        STAILQ_INSERT_AFTER(&demux->pdu_q, prev, entry, omp_next);
    } else {
        STAILQ_INSERT_HEAD(&demux->pdu_q, entry, omp_next);
    }
}
#endif /* BLE_LL_ISO */
