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
#include <nimble/hci_common.h>
#include <controller/ble_ll.h>
#include <controller/ble_ll_isoal.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#if MYNEWT_VAL(BLE_LL_ISO)

void
ble_ll_isoal_mux_init(struct ble_ll_isoal_mux *mux, uint8_t max_pdu,
                      uint32_t iso_interval_us, uint32_t sdu_interval_us,
                      uint8_t bn, uint8_t pte)
{
    memset(mux, 0, sizeof(*mux));

    BLE_LL_ASSERT(iso_interval_us >= sdu_interval_us);

    mux->max_pdu = max_pdu;
    /* Core 5.3, Vol 6, Part G, 2.1 */
    mux->sdu_per_interval = iso_interval_us / sdu_interval_us;
    mux->pdu_per_sdu = bn / mux->sdu_per_interval;

    mux->sdu_per_event = (1 + pte) * mux->sdu_per_interval;

    STAILQ_INIT(&mux->sdu_q);
    mux->sdu_q_len = 0;
}

void
ble_ll_isoal_mux_free(struct ble_ll_isoal_mux *mux)
{
    struct os_mbuf_pkthdr *pkthdr;
    struct os_mbuf *om;
    struct os_mbuf *om_next;

    pkthdr = STAILQ_FIRST(&mux->sdu_q);
    while (pkthdr) {
        om = OS_MBUF_PKTHDR_TO_MBUF(pkthdr);

        while (om) {
            om_next = SLIST_NEXT(om, om_next);
            os_mbuf_free(om);
            om = om_next;
        }

        STAILQ_REMOVE_HEAD(&mux->sdu_q, omp_next);
        pkthdr = STAILQ_FIRST(&mux->sdu_q);
    }

    STAILQ_INIT(&mux->sdu_q);
}

void
ble_ll_isoal_mux_sdu_enqueue(struct ble_ll_isoal_mux *mux, struct os_mbuf *om, uint32_t timestamp)
{
    struct os_mbuf_pkthdr *pkthdr;
    struct ble_mbuf_hdr *blehdr;
    os_sr_t sr;

    BLE_LL_ASSERT(mux);

    blehdr = BLE_MBUF_HDR_PTR(om);
    blehdr->txiso.packet_seq_num = ++mux->sdu_counter;

    OS_ENTER_CRITICAL(sr);
    pkthdr = OS_MBUF_PKTHDR(om);
    STAILQ_INSERT_TAIL(&mux->sdu_q, pkthdr, omp_next);
    mux->sdu_q_len++;
#if MYNEWT_VAL(BLE_LL_ISOAL_MUX_PREFILL)
    if (mux->sdu_q_len == mux->sdu_per_event) {
        mux->active = 1;
    }
#endif
    OS_EXIT_CRITICAL(sr);
}

int
ble_ll_isoal_mux_event_start(struct ble_ll_isoal_mux *mux, uint32_t timestamp)
{
#if MYNEWT_VAL(BLE_LL_ISOAL_MUX_PREFILL)
    /* If prefill is enabled, we always expect to have required number of SDUs
     * in queue, otherwise we disable mux until enough SDUs are queued again.
     */
    mux->sdu_in_event = mux->sdu_per_event;
    if (mux->sdu_in_event > mux->sdu_q_len) {
        mux->active = 0;
    }
    if (!mux->active) {
        mux->sdu_in_event = 0;
    }
#else
    mux->sdu_in_event = min(mux->sdu_q_len, mux->sdu_per_event);
#endif
    mux->event_tx_timestamp = timestamp;

    return mux->sdu_in_event;
}

int
ble_ll_isoal_mux_event_done(struct ble_ll_isoal_mux *mux)
{
    struct os_mbuf_pkthdr *pkthdr;
    struct ble_mbuf_hdr *blehdr;
    struct os_mbuf *om;
    struct os_mbuf *om_next;
    uint8_t num_sdu;
    int pkt_freed = 0;
    os_sr_t sr;

    num_sdu = min(mux->sdu_in_event, mux->sdu_per_interval);

    pkthdr = STAILQ_FIRST(&mux->sdu_q);
    if (pkthdr) {
        om = OS_MBUF_PKTHDR_TO_MBUF(pkthdr);
        blehdr = BLE_MBUF_HDR_PTR(om);
        mux->last_tx_timestamp = mux->event_tx_timestamp;
        mux->last_tx_packet_seq_num = blehdr->txiso.packet_seq_num;
    }

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

int
ble_ll_isoal_mux_pdu_get(struct ble_ll_isoal_mux *mux, uint8_t idx,
                         uint8_t *llid, void *dptr)
{
    struct os_mbuf_pkthdr *pkthdr;
    struct os_mbuf *om;
    uint8_t sdu_idx;
    uint8_t pdu_idx;
    uint16_t sdu_offset;
    uint16_t rem_len;
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
    sdu_offset = pdu_idx * mux->max_pdu;
    rem_len = OS_MBUF_PKTLEN(om) - sdu_offset;

    if ((int32_t)rem_len <= 0) {
        *llid = 1;
        pdu_len = 0;
    } else {
        *llid = (pdu_idx < mux->pdu_per_sdu - 1);
        pdu_len = min(mux->max_pdu, rem_len);
    }

    os_mbuf_copydata(om, sdu_offset, pdu_len, dptr);

    return pdu_len;
}

void
ble_ll_isoal_init(void)
{
}

void
ble_ll_isoal_reset(void)
{
}

#endif /* BLE_LL_ISO */
