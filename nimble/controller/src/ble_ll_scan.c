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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "syscfg/syscfg.h"
#include "os/os.h"
#include "os/os_cputime.h"
#include "nimble/ble.h"
#include "nimble/nimble_opt.h"
#include "nimble/hci_common.h"
#include "nimble/ble_hci_trans.h"
#include "controller/ble_phy.h"
#include "controller/ble_hw.h"
#include "controller/ble_ll.h"
#include "controller/ble_ll_sched.h"
#include "controller/ble_ll_adv.h"
#include "controller/ble_ll_scan.h"
#include "controller/ble_ll_hci.h"
#include "controller/ble_ll_whitelist.h"
#include "controller/ble_ll_resolv.h"
#include "controller/ble_ll_xcvr.h"
#include "controller/ble_ll_trace.h"
#include "controller/ble_ll_sync.h"
#include "ble_ll_conn_priv.h"

/*
 * XXX:
 * 1) I think I can guarantee that we dont process things out of order if
 * I send an event when a scan request is sent. The scan_rsp_pending flag
 * code might be made simpler.
 *
 * 2) Interleave sending scan requests to different advertisers? I guess I need
 * a list of advertisers to which I sent a scan request and have yet to
 * receive a scan response from? Implement this.
 */

/* Dont allow more than 255 of these entries */
#if MYNEWT_VAL(BLE_LL_NUM_SCAN_RSP_ADVS) > 255
    #error "Cannot have more than 255 scan response entries!"
#endif

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_CODED_PHY)
#define SCAN_VALID_PHY_MASK     (BLE_HCI_LE_PHY_1M_PREF_MASK | BLE_HCI_LE_PHY_CODED_PREF_MASK)
#else
#define SCAN_VALID_PHY_MASK     (BLE_HCI_LE_PHY_1M_PREF_MASK)
#endif

/* The scanning parameters set by host */
static struct ble_ll_scan_params g_ble_ll_scan_params[BLE_LL_SCAN_PHY_NUMBER];

/* The scanning state machine global object */
static struct ble_ll_scan_sm g_ble_ll_scan_sm;

struct ble_ll_ext_adv_hdr
{
    uint8_t mode;
    uint8_t hdr_len;
    uint8_t hdr[0];
};

/*
 * Structure used to store advertisers. This is used to limit sending scan
 * requests to the same advertiser and also to filter duplicate events sent
 * to the host.
 */
struct ble_ll_scan_advertisers
{
    uint16_t            sc_adv_flags;
    uint16_t            adi;
    struct ble_dev_addr adv_addr;
};

#define BLE_LL_SC_ADV_F_RANDOM_ADDR     (0x01)
#define BLE_LL_SC_ADV_F_SCAN_RSP_RXD    (0x02)
#define BLE_LL_SC_ADV_F_DIRECT_RPT_SENT (0x04)
#define BLE_LL_SC_ADV_F_ADV_RPT_SENT    (0x08)
#define BLE_LL_SC_ADV_F_SCAN_RSP_SENT   (0x10)

/* Contains list of advertisers that we have heard scan responses from */
static uint8_t g_ble_ll_scan_num_rsp_advs;
struct ble_ll_scan_advertisers
g_ble_ll_scan_rsp_advs[MYNEWT_VAL(BLE_LL_NUM_SCAN_RSP_ADVS)];

/* Duplicates filtering data */
#define BLE_LL_SCAN_ENTRY_TYPE_LEGACY(addr_type) \
    ((addr_type) & 1)
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
#define BLE_LL_SCAN_ENTRY_TYPE_EXT(addr_type, has_aux, is_anon, adi) \
    (((adi >> 8) & 0xF0) | (1 << 3) | (is_anon << 2) | (has_aux << 1) | ((addr_type) & 1))
#endif

#define BLE_LL_SCAN_DUP_F_ADV_REPORT_SENT       (0x01)
#define BLE_LL_SCAN_DUP_F_DIR_ADV_REPORT_SENT   (0x02)
#define BLE_LL_SCAN_DUP_F_SCAN_RSP_SENT         (0x04)

struct ble_ll_scan_dup_entry {
    uint8_t type;       /* entry type, see BLE_LL_SCAN_ENTRY_TYPE_* */
    uint8_t addr[6];
    uint8_t flags;      /* use BLE_LL_SCAN_DUP_F_xxx */
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    uint16_t adi;
#endif
    TAILQ_ENTRY(ble_ll_scan_dup_entry) link;
};

static os_membuf_t g_scan_dup_mem[ OS_MEMPOOL_SIZE(
                                   MYNEWT_VAL(BLE_LL_NUM_SCAN_DUP_ADVS),
                                   sizeof(struct ble_ll_scan_dup_entry)) ];
static struct os_mempool g_scan_dup_pool;
static TAILQ_HEAD(ble_ll_scan_dup_list, ble_ll_scan_dup_entry) g_scan_dup_list;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
#if MYNEWT_VAL(BLE_LL_EXT_ADV_AUX_PTR_CNT) != 0
static os_membuf_t ext_scan_aux_mem[ OS_MEMPOOL_SIZE(
                    MYNEWT_VAL(BLE_LL_EXT_ADV_AUX_PTR_CNT),
                    sizeof (struct ble_ll_aux_data))
];
#else
#define ext_scan_aux_mem NULL
#endif

static struct os_mempool ext_scan_aux_pool;

static int ble_ll_scan_start(struct ble_ll_scan_sm *scansm,
                             struct ble_ll_sched_item *sch);

static void
ble_ll_aux_scan_drop_event_cb(struct ble_npl_event *ev)
{
    struct ble_ll_aux_data *aux_data = ble_npl_event_get_arg(ev);

    ble_ll_scan_end_adv_evt(aux_data);
    ble_ll_scan_aux_data_unref(aux_data);
}

static void
ble_ll_aux_scan_drop(struct ble_ll_aux_data *aux_data)
{
    BLE_LL_ASSERT(aux_data);

    STATS_INC(ble_ll_stats, aux_scan_drop);

    ble_npl_event_init(&aux_data->ev, ble_ll_aux_scan_drop_event_cb, aux_data);
    ble_ll_event_send(&aux_data->ev);
}

static int
ble_ll_aux_scan_cb(struct ble_ll_sched_item *sch)
{
    struct ble_ll_scan_sm *scansm = &g_ble_ll_scan_sm;
    uint8_t lls = ble_ll_state_get();
    uint32_t wfr_usec;

    STATS_INC(ble_ll_stats, aux_sched_cb);

    /* Drop the scheduled item if scan was disable or there is aux or scan
     * response pending
     */
    if (!scansm->scan_enabled || scansm->cur_aux_data ||
            scansm->scan_rsp_pending) {
        ble_ll_aux_scan_drop(sch->cb_arg);
        sch->cb_arg = NULL;
        goto done;
    }

    /* Check if there is no aux connect sent. If so drop the sched item */
    if (lls == BLE_LL_STATE_INITIATING && ble_ll_conn_init_pending_aux_conn_rsp()) {
        ble_ll_aux_scan_drop(sch->cb_arg);
        sch->cb_arg = NULL;
        goto done;
    }

    /* This function is called only when scanner is running. This can happen
     *  in 3 states:
     * BLE_LL_STATE_SCANNING
     * BLE_LL_STATE_INITIATING
     * BLE_LL_STATE_STANDBY
     */
    if (lls != BLE_LL_STATE_STANDBY) {
        ble_phy_disable();
        ble_ll_wfr_disable();
        ble_ll_state_set(BLE_LL_STATE_STANDBY);
    }

    /* When doing RX for AUX pkt, cur_aux_data keeps valid aux data */
    scansm->cur_aux_data = sch->cb_arg;
    sch->cb_arg = NULL;
    BLE_LL_ASSERT(scansm->cur_aux_data != NULL);
    scansm->cur_aux_data->scanning = 1;

    if (ble_ll_scan_start(scansm, sch)) {
        ble_ll_scan_interrupted(scansm);
        goto done;
    }

    STATS_INC(ble_ll_stats, aux_fired_for_read);

    wfr_usec = scansm->cur_aux_data->offset_units ? 300 : 30;
    ble_phy_wfr_enable(BLE_PHY_WFR_ENABLE_RX, 0, wfr_usec);

done:

    return BLE_LL_SCHED_STATE_DONE;
}

static int
ble_ll_scan_ext_adv_init(struct ble_ll_aux_data **aux_data)
{
    struct ble_ll_aux_data *e;

    e = os_memblock_get(&ext_scan_aux_pool);
    if (!e) {
        return -1;
    }

    memset(e, 0, sizeof(*e));
    e->sch.sched_cb = ble_ll_aux_scan_cb;
    e->sch.sched_type = BLE_LL_SCHED_TYPE_AUX_SCAN;
    e->ref_cnt = 1;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    e->rpa_index = -1;
#endif
    ble_ll_trace_u32x2(BLE_LL_TRACE_ID_AUX_REF, (uint32_t)e, e->ref_cnt);

    *aux_data = e;
    STATS_INC(ble_ll_stats, aux_allocated);

    return 0;
}
#endif

/* See Vol 6 Part B Section 4.4.3.2. Active scanning backoff */
static void
ble_ll_scan_req_backoff(struct ble_ll_scan_sm *scansm, int success)
{
    BLE_LL_ASSERT(scansm->backoff_count == 0);
    BLE_LL_ASSERT(scansm->scan_rsp_pending == 0);

    if (success) {
        scansm->scan_rsp_cons_fails = 0;
        ++scansm->scan_rsp_cons_ok;
        if (scansm->scan_rsp_cons_ok == 2) {
            scansm->scan_rsp_cons_ok = 0;
            if (scansm->upper_limit > 1) {
                scansm->upper_limit >>= 1;
            }
        }
        STATS_INC(ble_ll_stats, scan_req_txg);
    } else {
        scansm->scan_rsp_cons_ok = 0;
        ++scansm->scan_rsp_cons_fails;
        if (scansm->scan_rsp_cons_fails == 2) {
            scansm->scan_rsp_cons_fails = 0;
            if (scansm->upper_limit < 256) {
                scansm->upper_limit <<= 1;
            }
        }
        STATS_INC(ble_ll_stats, scan_req_txf);
    }

    scansm->backoff_count = rand() & (scansm->upper_limit - 1);
    ++scansm->backoff_count;
    BLE_LL_ASSERT(scansm->backoff_count <= 256);
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
static void
ble_ll_scan_refresh_nrpa(struct ble_ll_scan_sm *scansm)
{
    ble_npl_time_t now;

    now = ble_npl_time_get();
    if ((ble_npl_stime_t)(now - scansm->scan_nrpa_timer) >= 0) {
        /* Generate new NRPA */
        ble_ll_rand_data_get(scansm->scan_nrpa, BLE_DEV_ADDR_LEN);
        scansm->scan_nrpa[5] &= ~0xc0;

        /* We'll use the same timeout as for RPA rotation */
        scansm->scan_nrpa_timer = now + ble_ll_resolv_get_rpa_tmo();
    }
}
#endif

/**
 * ble ll scan req pdu make
 *
 * Construct a SCAN_REQ PDU.
 *
 * @param scansm Pointer to scanning state machine
 * @param adv_addr Pointer to device address of advertiser
 * @param addr_type 0 if public; non-zero if random. This is the addr type of
 *                  the advertiser; not our "own address type"
 * @rpa_index       Index on resolve list to use
 */
static void
ble_ll_scan_req_pdu_prepare(struct ble_ll_scan_sm *scansm, uint8_t *adv_addr,
                            uint8_t adv_addr_type, int rpa_index)
{
    uint8_t     hdr_byte;
    uint8_t     *scana;
    struct ble_ll_scan_pdu_data *pdu_data;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    uint8_t rpa[BLE_DEV_ADDR_LEN];
    struct ble_ll_resolv_entry *rl;
#endif

    pdu_data = &scansm->pdu_data;

    /* Construct first PDU header byte */
    hdr_byte = BLE_ADV_PDU_TYPE_SCAN_REQ;
    if (adv_addr_type) {
        hdr_byte |= BLE_ADV_PDU_HDR_RXADD_RAND;
    }

    /* Get pointer to our device address */
    if ((scansm->own_addr_type & 1) == 0) {
        scana = g_dev_addr;
    } else {
        hdr_byte |= BLE_ADV_PDU_HDR_TXADD_RAND;
        scana = g_random_addr;
    }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    if (scansm->own_addr_type > BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        rl = NULL;
        if (ble_ll_is_rpa(adv_addr, adv_addr_type)) {
            if (rpa_index >= 0) {
                /* Generate a RPA to use for scana */
                rl = &g_ble_ll_resolv_list[rpa_index];
            }
        } else {
            if (ble_ll_resolv_enabled()) {
                rl = ble_ll_resolv_list_find(adv_addr, adv_addr_type);
            }
        }

        /*
         * If advertising device is on our resolving list, we use RPA generated
         * using Local IRK from resolving list entry as ScanA. In other case,
         * we use NRPA as ScanA as allowed by spec to prevent our device from
         * being tracked when doing an active scan (see Core 5.0, Vol 6, Part B,
         * section 6.3).
         */
        if (rl) {
            ble_ll_resolv_get_priv_addr(rl, 1, rpa);
            scana = rpa;
        } else {
            ble_ll_scan_refresh_nrpa(scansm);
            scana = scansm->scan_nrpa;
        }

        hdr_byte |= BLE_ADV_PDU_HDR_TXADD_RAND;
    }
#endif

    /* Save scan request data */
    pdu_data->hdr_byte = hdr_byte;
    memcpy(pdu_data->scana, scana, BLE_DEV_ADDR_LEN);
    memcpy(pdu_data->adva, adv_addr, BLE_DEV_ADDR_LEN);
}

static uint8_t
ble_ll_scan_req_tx_pdu_cb(uint8_t *dptr, void *pducb_arg, uint8_t *hdr_byte)
{
    struct ble_ll_scan_sm *scansm = pducb_arg;
    struct ble_ll_scan_pdu_data *pdu_data = &scansm->pdu_data;

    memcpy(dptr, pdu_data->scana, BLE_DEV_ADDR_LEN);
    memcpy(dptr + BLE_DEV_ADDR_LEN, pdu_data->adva, BLE_DEV_ADDR_LEN);

    *hdr_byte = pdu_data->hdr_byte;

    return BLE_DEV_ADDR_LEN * 2;
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
/* if copy_from is provided new report is initialized with that instead of
 * defaults
 */
static struct ble_hci_ev *
ble_ll_scan_get_ext_adv_report(struct ext_adv_report *copy_from)
{
    struct ble_hci_ev_le_subev_ext_adv_rpt *ev;
    struct ext_adv_report *report;
    struct ble_hci_ev *hci_ev;

    hci_ev = ( void *) ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_LO);
    if (!hci_ev) {
        return NULL;
    }

    hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
    hci_ev->length = sizeof(*ev) + sizeof(*report);
    ev = (void *) hci_ev->data;

    memset(ev, 0, sizeof(*ev));
    ev->subev_code = BLE_HCI_LE_SUBEV_EXT_ADV_RPT;
    /* We support only one report per event now */
    ev->num_reports = 1;

    report = ev->reports;

    if (copy_from) {
        memcpy(report, copy_from, sizeof(*report));
        report->data_len = 0;
    } else {
        memset(report, 0, sizeof(*report));

        report->pri_phy = BLE_PHY_1M;
        /* Init SID with "Not available" which is 0xFF */
        report->sid = 0xFF;
        /* Init TX Power with "Not available" which is 127 */
        report->tx_power = 127;
        /* Init RSSI with "Not available" which is 127 */
        report->rssi = 127;
        /* Init address type with "anonymous" which is 0xFF */
        report->addr_type = 0xFF;
    }

    return hci_ev;
}

static void
ble_ll_scan_send_truncated(struct ble_ll_aux_data *aux_data)
{
    struct ble_hci_ev_le_subev_ext_adv_rpt *ev;
    struct ext_adv_report *report;
    struct ble_hci_ev *hci_ev;

    if (!ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_EXT_ADV_RPT)) {
        return;
    }

    BLE_LL_ASSERT(aux_data);

    /* No need to send if we did not send any report or sent truncated already */
    if (!(aux_data->flags_ll & BLE_LL_AUX_FLAG_HCI_SENT_ANY) ||
        (aux_data->flags_ll & BLE_LL_AUX_FLAG_HCI_SENT_TRUNCATED)) {
        return;
    }

    BLE_LL_ASSERT(aux_data->evt);
    hci_ev = aux_data->evt;
    aux_data->evt = NULL;

    hci_ev->length = sizeof(*ev) + sizeof(*report);

    ev = (void *) hci_ev->data;
    report = ev->reports;

    report->data_len = 0;

    report->evt_type = aux_data->evt_type;
    report->evt_type |= BLE_HCI_ADV_DATA_STATUS_TRUNCATED;

    if (aux_data->flags & BLE_LL_AUX_HAS_ADDRA) {
        memcpy(report->addr, aux_data->addr, 6);
        report->addr_type = aux_data->addr_type;
    }

    if (aux_data->flags & BLE_LL_AUX_HAS_DIR_ADDRA) {
        memcpy(report->dir_addr, aux_data->dir_addr, 6);
        report->dir_addr_type = aux_data->dir_addr_type;
    }

    report->sid = aux_data->adi >> 12;
    ble_ll_hci_event_send(hci_ev);

    aux_data->flags_ll |= BLE_LL_AUX_FLAG_SCAN_ERROR;
    aux_data->flags_ll |= BLE_LL_AUX_FLAG_HCI_SENT_ANY;
    aux_data->flags_ll |= BLE_LL_AUX_FLAG_HCI_SENT_TRUNCATED;
}

static int
ble_ll_scan_get_adi(struct ble_ll_aux_data *aux_data, uint16_t *adi)
{
    if (!aux_data || !(aux_data->flags & BLE_LL_AUX_HAS_ADI)) {
        return -1;
    }

    *adi = aux_data->adi;

    return 0;
}

void
ble_ll_scan_end_adv_evt(struct ble_ll_aux_data *aux_data)
{
    /* Make sure we send report with 'truncated' data state if needed */
    ble_ll_scan_send_truncated(aux_data);
}
#endif

static void
ble_ll_scan_clean_cur_aux_data(void)
{
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    struct ble_ll_scan_sm *scansm = &g_ble_ll_scan_sm;

    /* If scanner was reading aux ptr, we need to clean it up */
    if (scansm->cur_aux_data) {
        ble_ll_scan_end_adv_evt(scansm->cur_aux_data);
        ble_ll_scan_aux_data_unref(scansm->cur_aux_data);
        scansm->cur_aux_data = NULL;
    }
#endif
}

void
ble_ll_scan_halt(void)
{
    struct ble_ll_scan_sm *scansm = &g_ble_ll_scan_sm;

    ble_ll_scan_clean_cur_aux_data();

    /* Update backoff if we failed to receive scan response */
    if (scansm->scan_rsp_pending) {
        scansm->scan_rsp_pending = 0;
        ble_ll_scan_req_backoff(scansm, 0);
    }
}

/**
 * Checks to see if we have received a scan response from this advertiser.
 *
 * @param adv_addr Address of advertiser
 * @param txadd TxAdd bit (0: public; random otherwise)
 *
 * @return int 0: have not received a scan response; 1 otherwise.
 */
static int
ble_ll_scan_have_rxd_scan_rsp(uint8_t *addr, uint8_t txadd,
                              uint8_t ext_adv, uint16_t adi)
{
    uint8_t num_advs;
    struct ble_ll_scan_advertisers *adv;

    /* Do we have an address match? Must match address type */
    adv = &g_ble_ll_scan_rsp_advs[0];
    num_advs = g_ble_ll_scan_num_rsp_advs;
    while (num_advs) {
        if (!memcmp(&adv->adv_addr, addr, BLE_DEV_ADDR_LEN)) {
            /* Address type must match */
            if (txadd) {
                if (adv->sc_adv_flags & BLE_LL_SC_ADV_F_RANDOM_ADDR) {
                    if (ext_adv) {
                        if (adi == adv->adi) {
                            return 1;
                        }
                        goto next;
                    }
                    return 1;
                }
            } else {
                if ((adv->sc_adv_flags & BLE_LL_SC_ADV_F_RANDOM_ADDR) == 0) {
                    if (ext_adv) {
                        if (adi == adv->adi) {
                            return 1;
                        }
                        goto next;
                    }
                    return 1;
                }
            }
        }
next:
        ++adv;
        --num_advs;
    }

    return 0;
}

static void
ble_ll_scan_add_scan_rsp_adv(uint8_t *addr, uint8_t txadd,
                             uint8_t ext_adv, uint16_t adi)
{
    uint8_t num_advs;
    struct ble_ll_scan_advertisers *adv;

    /* XXX: for now, if we dont have room, just leave */
    num_advs = g_ble_ll_scan_num_rsp_advs;
    if (num_advs == MYNEWT_VAL(BLE_LL_NUM_SCAN_RSP_ADVS)) {
        return;
    }

    /* Check if address is already on the list */
    if (ble_ll_scan_have_rxd_scan_rsp(addr, txadd, ext_adv, adi)) {
        return;
    }

    /* Add the advertiser to the array */
    adv = &g_ble_ll_scan_rsp_advs[num_advs];
    memcpy(&adv->adv_addr, addr, BLE_DEV_ADDR_LEN);
    adv->sc_adv_flags = BLE_LL_SC_ADV_F_SCAN_RSP_RXD;
    if (txadd) {
        adv->sc_adv_flags |= BLE_LL_SC_ADV_F_RANDOM_ADDR;
    }
    adv->adi = adi;
    ++g_ble_ll_scan_num_rsp_advs;

    return;
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
static int
ble_ll_hci_send_legacy_ext_adv_report(uint8_t evtype,
                                      const uint8_t *addr, uint8_t addr_type,
                                      uint8_t rssi,
                                      uint8_t adv_data_len,
                                      struct os_mbuf *adv_data,
                                      const uint8_t *inita, uint8_t inita_type)
{
    struct ble_hci_ev_le_subev_ext_adv_rpt *ev;
    struct ext_adv_report *report;
    struct ble_hci_ev *hci_ev;

    if (!ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_EXT_ADV_RPT)) {
        return -1;
    }

    /* Drop packet if adv data doesn't fit  */
    if ((sizeof(*ev) + sizeof(ev->reports[0]) + adv_data_len) > BLE_HCI_MAX_DATA_LEN) {
        STATS_INC(ble_ll_stats, adv_evt_dropped);
        return -1;
    }

    hci_ev = ble_ll_scan_get_ext_adv_report(NULL);
    if (!hci_ev) {
        return -1;
    }

    ev = (void *) hci_ev->data;
    report = ev->reports;

    switch (evtype) {
    case BLE_HCI_ADV_RPT_EVTYPE_ADV_IND:
        report->evt_type = BLE_HCI_LEGACY_ADV_EVTYPE_ADV_IND;
        break;
    case BLE_HCI_ADV_RPT_EVTYPE_DIR_IND:
        report->evt_type = BLE_HCI_LEGACY_ADV_EVTYPE_ADV_DIRECT_IND;
        break;
    case BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND:
        report->evt_type = BLE_HCI_LEGACY_ADV_EVTYPE_ADV_NONCON_IND;
        break;
    case BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP:
        report->evt_type = BLE_HCI_LEGACY_ADV_EVTYPE_SCAN_RSP_ADV_IND;
         break;
    case BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND:
        report->evt_type = BLE_HCI_LEGACY_ADV_EVTYPE_ADV_SCAN_IND;
        break;
    default:
        BLE_LL_ASSERT(0);
        ble_hci_trans_buf_free((uint8_t *) hci_ev);
        return -1;
    }

    report->addr_type = addr_type;
    memcpy(report->addr, addr, BLE_DEV_ADDR_LEN);
    report->pri_phy = BLE_PHY_1M;
    report->sid = 0xFF;
    report->tx_power = 127;
    report->rssi = rssi;

    if (inita) {
        report->dir_addr_type = inita_type;
        memcpy(report->dir_addr, inita, BLE_DEV_ADDR_LEN);
    }

    if (adv_data_len) {
        hci_ev->length += adv_data_len;
        report->data_len = adv_data_len;
        os_mbuf_copydata(adv_data, 0, adv_data_len, report->data);
    }

    return ble_ll_hci_event_send(hci_ev);
}
#endif

static int
ble_ll_hci_send_adv_report(uint8_t evtype,
                           const uint8_t *addr, uint8_t addr_type, int8_t rssi,
                           uint8_t adv_data_len, struct os_mbuf *adv_data)
{
    struct ble_hci_ev_le_subev_adv_rpt *ev;
    struct ble_hci_ev *hci_ev;
    int8_t *ev_rssi;

    if (!ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_ADV_RPT)) {
        return -1;
    }

    /* Drop packet if adv data doesn't fit, note extra 1 is for RSSI   */
    if ((sizeof(*ev) + sizeof(ev->reports[0]) + adv_data_len + 1) > BLE_HCI_MAX_DATA_LEN) {
        STATS_INC(ble_ll_stats, adv_evt_dropped);
        return -1;
    }

    hci_ev = (void *) ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_LO);
    if (!hci_ev) {
        return -1;
    }

    hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
    hci_ev->length = sizeof(*ev) + sizeof(ev->reports[0]) + adv_data_len + 1;
    ev = (void *) hci_ev->data;

    ev->subev_code = BLE_HCI_LE_SUBEV_ADV_RPT;
    ev->num_reports = 1;

    ev->reports[0].type = evtype;
    ev->reports[0].addr_type = addr_type;
    memcpy(ev->reports[0].addr, addr, BLE_DEV_ADDR_LEN);
    ev->reports[0].data_len = adv_data_len;
    os_mbuf_copydata(adv_data, 0, adv_data_len, ev->reports[0].data);

    /* RSSI is after adv data... */
    ev_rssi = (int8_t *) (hci_ev->data + sizeof(*ev) + sizeof(ev->reports[0]) + adv_data_len);
    *ev_rssi = rssi;

    return ble_ll_hci_event_send(hci_ev);
}

static int
ble_ll_hci_send_dir_adv_report(const uint8_t *addr, uint8_t addr_type,
                               const uint8_t *inita, uint8_t inita_type,
                               int8_t rssi)
{
    struct ble_hci_ev_le_subev_direct_adv_rpt *ev;
    struct ble_hci_ev *hci_ev;

    if (!ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_DIRECT_ADV_RPT)) {
        return -1;
    }

    hci_ev = (void *) ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_LO);
    if (!hci_ev) {
        return -1;
    }

    hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
    hci_ev->length = sizeof(*ev) + sizeof(*(ev->reports));
    ev = (void *) hci_ev->data;

    ev->subev_code = BLE_HCI_LE_SUBEV_DIRECT_ADV_RPT;
    ev->num_reports = 1;

    ev->reports[0].type = BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
    ev->reports[0].addr_type = addr_type;
    memcpy(ev->reports[0].addr, addr, BLE_DEV_ADDR_LEN);
    ev->reports[0].dir_addr_type = inita_type;
    memcpy(ev->reports[0].dir_addr, inita, BLE_DEV_ADDR_LEN);
    ev->reports[0].rssi = rssi;

    return ble_ll_hci_event_send(hci_ev);
}

static int
ble_ll_scan_dup_update_legacy(uint8_t addr_type, const uint8_t *addr,
                              uint8_t subev, uint8_t evtype)
{
    struct ble_ll_scan_dup_entry *e;
    uint8_t type;

    type = BLE_LL_SCAN_ENTRY_TYPE_LEGACY(addr_type);

    /*
     * We assume ble_ll_scan_dup_check() was called before which either matched
     * some entry or allocated new one and placed in on the top of queue.
     */

    e = TAILQ_FIRST(&g_scan_dup_list);
    BLE_LL_ASSERT(e && e->type == type && !memcmp(e->addr, addr, 6));

    if (subev == BLE_HCI_LE_SUBEV_DIRECT_ADV_RPT) {
        e->flags |= BLE_LL_SCAN_DUP_F_DIR_ADV_REPORT_SENT;
    } else {
        if (evtype == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP) {
            e->flags |= BLE_LL_SCAN_DUP_F_SCAN_RSP_SENT;
        } else {
            e->flags |= BLE_LL_SCAN_DUP_F_ADV_REPORT_SENT;
        }
    }

    return 0;
}

/**
 * Send an advertising report to the host.
 *
 * NOTE: while we are allowed to send multiple devices in one report, we
 * will just send for one for now.
 *
 * @param pdu_type
 * @param txadd
 * @param rxbuf
 * @param hdr
 * @param scansm
 */
static void
ble_ll_scan_send_adv_report(uint8_t pdu_type,
                            const uint8_t *adva, uint8_t adva_type,
                            const uint8_t *inita, uint8_t inita_type,
                            struct os_mbuf *om,
                            struct ble_mbuf_hdr *hdr,
                            struct ble_ll_scan_sm *scansm)
{
    uint8_t subev = BLE_HCI_LE_SUBEV_ADV_RPT;
    uint8_t adv_data_len;
    uint8_t evtype;
    int rc;

    if (pdu_type == BLE_ADV_PDU_TYPE_ADV_DIRECT_IND) {
        if (ble_ll_is_rpa(inita, inita_type)) {
            /* For resolvable we send separate subevent */
            subev = BLE_HCI_LE_SUBEV_DIRECT_ADV_RPT;
        }

        evtype = BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
        adv_data_len = 0;
    } else {
        if (pdu_type == BLE_ADV_PDU_TYPE_ADV_IND) {
            evtype = BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;
        } else if (pdu_type == BLE_ADV_PDU_TYPE_ADV_SCAN_IND) {
            evtype = BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND;
        } else if (pdu_type == BLE_ADV_PDU_TYPE_ADV_NONCONN_IND) {
            evtype = BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND;
        } else {
            evtype = BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP;
        }
        adv_data_len = om->om_data[1] - BLE_DEV_ADDR_LEN;
        os_mbuf_adj(om, BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN);
    }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    /* If RPA has been used, make sure we use correct address types
     * in the advertising report.
     */
    if (BLE_MBUF_HDR_RESOLVED(hdr)) {
        adva_type += 2;
    }
    if (BLE_MBUF_HDR_INITA_RESOLVED(hdr)) {
        inita_type += 2;
    }
#endif

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (scansm->ext_scanning) {
        rc = ble_ll_hci_send_legacy_ext_adv_report(evtype,
                                                   adva, adva_type,
                                                   hdr->rxinfo.rssi,
                                                   adv_data_len, om,
                                                   inita, inita_type);
        goto done;
    }
#endif

    if (subev == BLE_HCI_LE_SUBEV_DIRECT_ADV_RPT) {
        rc = ble_ll_hci_send_dir_adv_report(adva, adva_type, inita, inita_type,
                                            hdr->rxinfo.rssi);
        goto done;
    }

    rc = ble_ll_hci_send_adv_report(evtype, adva, adva_type, hdr->rxinfo.rssi,
                                    adv_data_len, om);
done:
    if (!rc && scansm->scan_filt_dups) {
        ble_ll_scan_dup_update_legacy(adva_type, adva, subev, evtype);
    }
}

/**
 * Checks the scanner filter policy to determine if we should allow or discard
 * the received PDU.
 *
 * NOTE: connect requests and scan requests are not passed here
 *
 * @param pdu_type
 * @param adv_addr
 * @param adv_addr_type
 * @param init_addr
 * @param init_addr_type
 * @param flags
 *
 * @return int 0: pdu allowed by filter policy. 1: pdu not allowed
 */
static int
ble_ll_scan_chk_filter_policy(uint8_t pdu_type, uint8_t *adv_addr,
                              uint8_t adv_addr_type, uint8_t *init_addr,
                              uint8_t init_addr_type, uint8_t devmatch)
{
    int use_whitelist;
    int chk_inita;
    struct ble_ll_scan_params *params =
                        &g_ble_ll_scan_sm.phy_data[g_ble_ll_scan_sm.cur_phy];

    use_whitelist = 0;
    chk_inita = 0;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (pdu_type == BLE_ADV_PDU_TYPE_ADV_EXT_IND && adv_addr == NULL) {
        /* Note: adv_addr can be NULL (but don't have to) for ext adv. If NULL
         * that means it is beacon and skip filter policy for now */
        return 0;
    }
#endif

    switch (params->scan_filt_policy) {
    case BLE_HCI_SCAN_FILT_NO_WL:
        break;
    case BLE_HCI_SCAN_FILT_USE_WL:
        use_whitelist = 1;
        break;
    case BLE_HCI_SCAN_FILT_NO_WL_INITA:
        chk_inita = 1;
        break;
    case BLE_HCI_SCAN_FILT_USE_WL_INITA:
        chk_inita = 1;
        use_whitelist = 1;
        break;
    default:
        BLE_LL_ASSERT(0);
        break;
    }

    /* If we are using the whitelist, check that first */
    if (use_whitelist && (pdu_type != BLE_ADV_PDU_TYPE_SCAN_RSP)) {
        /* If device does not match let us skip this PDU.
         * If device matches, lets check for InitA further in the code
         */
        if (!devmatch) {
            return 1;
        }
    }

    /* If this is a directed advertisement, init_addr is not NULL.
     * Check that it is for us */
    if (init_addr) {
        /* Is this for us? If not, is it resolvable */
        if (!ble_ll_is_our_devaddr(init_addr, init_addr_type)) {
            if (!chk_inita || !ble_ll_is_rpa(adv_addr, adv_addr_type)) {
                return 1;
            }
        }
    }

    return 0;
}

static void
ble_ll_get_chan_to_scan(struct ble_ll_scan_sm *scansm, uint8_t *chan,
                        int *phy)
{
    struct ble_ll_scan_params *scanphy = &scansm->phy_data[scansm->cur_phy];
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    struct ble_ll_aux_data *aux_data = scansm->cur_aux_data;

    if (!scansm->ext_scanning || !aux_data || !aux_data->scanning) {
        *chan = scanphy->scan_chan;
        *phy = scanphy->phy;
        return;
    }

    *chan = aux_data->chan;
    *phy = aux_data->aux_phy;
#else
    *chan = scanphy->scan_chan;
    *phy = scanphy->phy;
#endif
}
/**
 * Called to enable the receiver for scanning.
 *
 * Context: Link Layer task
 *
 * @param sch
 *
 * @return int
 */
static int
ble_ll_scan_start(struct ble_ll_scan_sm *scansm, struct ble_ll_sched_item *sch)
{
    int rc;
    struct ble_ll_scan_params *scanphy = &scansm->phy_data[scansm->cur_phy];
    uint8_t scan_chan;
#if (BLE_LL_BT5_PHY_SUPPORTED == 1)
    uint8_t phy_mode;
#endif
    int phy;

    BLE_LL_ASSERT(scansm->scan_rsp_pending == 0);

    ble_ll_get_chan_to_scan(scansm, &scan_chan, &phy);

    /* XXX: right now scheduled item is only present if we schedule for aux
     *      scan just make sanity check that we have proper combination of
     *      sch and resulting scan_chan
     */
    BLE_LL_ASSERT(!sch || scan_chan < BLE_PHY_ADV_CHAN_START);
    BLE_LL_ASSERT(sch || scan_chan >= BLE_PHY_ADV_CHAN_START);

    /* Set channel */
    rc = ble_phy_setchan(scan_chan, BLE_ACCESS_ADDR_ADV, BLE_LL_CRCINIT_ADV);
    BLE_LL_ASSERT(rc == 0);

    /*
     * Set transmit end callback to NULL in case we transmit a scan request.
     * There is a callback for the connect request.
     */
    ble_phy_set_txend_cb(NULL, NULL);

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_ENCRYPTION)
    ble_phy_encrypt_disable();
#endif

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    if (ble_ll_resolv_enabled()) {
        ble_phy_resolv_list_enable();
    } else {
        ble_phy_resolv_list_disable();
    }
#endif

#if (BLE_LL_BT5_PHY_SUPPORTED == 1)
    phy_mode = ble_ll_phy_to_phy_mode(phy, BLE_HCI_LE_PHY_CODED_ANY);
    ble_phy_mode_set(phy_mode, phy_mode);
#endif

    /* XXX: probably need to make sure hfxo is running too */
    /* XXX: can make this better; want to just start asap. */
    if (sch) {
        rc = ble_phy_rx_set_start_time(sch->start_time +
                                       g_ble_ll_sched_offset_ticks,
                                       sch->remainder);
    } else {
        rc = ble_phy_rx_set_start_time(os_cputime_get32() +
                                       g_ble_ll_sched_offset_ticks, 0);
    }
    if (!rc || rc == BLE_PHY_ERR_RX_LATE) {
        /* If we are late here, it is still OK because we keep scanning.
         * Clear error
         */
        rc = 0;

        /* Enable/disable whitelisting */
        if (scanphy->scan_filt_policy & 1) {
            ble_ll_whitelist_enable();
        } else {
            ble_ll_whitelist_disable();
        }

        /* Set link layer state to scanning */
        if (scanphy->scan_type == BLE_SCAN_TYPE_INITIATE) {
            ble_ll_state_set(BLE_LL_STATE_INITIATING);
        } else {
            ble_ll_state_set(BLE_LL_STATE_SCANNING);
        }
    }

    return rc;
}

#ifdef BLE_XCVR_RFCLK
static void
ble_ll_scan_rfclk_chk_stop(void)
{
    int stop;
    int32_t time_till_next;
    os_sr_t sr;
    uint32_t next_time;

    stop = 0;
    OS_ENTER_CRITICAL(sr);
    if (ble_ll_sched_next_time(&next_time)) {
        /*
         * If the time until the next event is too close, dont bother to turn
         * off the clock
         */
        time_till_next = (int32_t)(next_time - os_cputime_get32());
        if (time_till_next > g_ble_ll_data.ll_xtal_ticks) {
            stop = 1;
        }
    } else {
        stop = 1;
    }
    if (stop) {
        ble_ll_xcvr_rfclk_disable();
    }
    OS_EXIT_CRITICAL(sr);
}
#endif

static uint8_t
ble_ll_scan_get_next_adv_prim_chan(uint8_t chan)
{
    ++chan;
    if (chan == BLE_PHY_NUM_CHANS) {
        chan = BLE_PHY_ADV_CHAN_START;
    }

    return chan;
}

static uint32_t
ble_ll_scan_get_scan_win(struct ble_ll_scan_params *scanphy, uint32_t cputime)
{
    uint32_t itvl;

    /* if pass current window, move to next interval */
    itvl = os_cputime_usecs_to_ticks(scanphy->scan_itvl * BLE_HCI_SCAN_ITVL);
    while ((int32_t)(cputime - scanphy->scan_win_start_time) >= (int32_t)itvl) {
        scanphy->scan_win_start_time += itvl;

        /* update channel if moving to next interval */
        scanphy->scan_chan =
                ble_ll_scan_get_next_adv_prim_chan(scanphy->scan_chan);
    }

    return scanphy->scan_win_start_time;
}
/**
 * Called to determine if we are inside or outside the scan window. If we
 * are inside the scan window it means that the device should be receiving
 * on the scan channel.
 *
 * Context: Link Layer
 *
 * @param scansm
 *
 * @return int 0: inside scan window 1: outside scan window
 */
static int
ble_ll_scan_window_chk(struct ble_ll_scan_sm *scansm, uint32_t cputime)
{
    uint32_t win;
    uint32_t dt;
    uint32_t win_start;
    struct ble_ll_scan_params *scanphy = &scansm->phy_data[scansm->cur_phy];

    win_start = ble_ll_scan_get_scan_win(scanphy, cputime);

    if (scanphy->scan_window != scanphy->scan_itvl) {
        win = os_cputime_usecs_to_ticks(scanphy->scan_window * BLE_HCI_SCAN_ITVL);
        dt = cputime - win_start;
        if (dt >= win) {
#ifdef BLE_XCVR_RFCLK
            if (dt < (scanphy->scan_itvl - g_ble_ll_data.ll_xtal_ticks)) {
                ble_ll_scan_rfclk_chk_stop();
            }
#endif
            return 1;
        }
    }

    return 0;
}
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
static void
ble_ll_scan_aux_data_free(struct ble_ll_aux_data *aux_data)
{
    if (aux_data) {
        if (aux_data->evt) {
            ble_hci_trans_buf_free((uint8_t *)aux_data->evt);
            aux_data->evt = NULL;
        }
        os_memblock_put(&ext_scan_aux_pool, aux_data);
        STATS_INC(ble_ll_stats, aux_freed);
    }
}

struct ble_ll_aux_data *
ble_ll_scan_aux_data_ref(struct ble_ll_aux_data *aux_data)
{
    os_sr_t sr;

    BLE_LL_ASSERT(aux_data);

    OS_ENTER_CRITICAL(sr);
    aux_data->ref_cnt++;
    ble_ll_trace_u32x2(BLE_LL_TRACE_ID_AUX_REF, (uint32_t) aux_data, aux_data->ref_cnt);

    OS_EXIT_CRITICAL(sr);

    return aux_data;
}

void
ble_ll_scan_aux_data_unref(struct ble_ll_aux_data *aux_data)
{
    os_sr_t sr;

    BLE_LL_ASSERT(aux_data);

    OS_ENTER_CRITICAL(sr);
    aux_data->ref_cnt--;
    ble_ll_trace_u32x2(BLE_LL_TRACE_ID_AUX_UNREF, (uint32_t) aux_data, aux_data->ref_cnt);

    if (aux_data->ref_cnt == 0) {
        /*
         * Some validation to make sure that we completed scan properly:
         * - we either did not send any report or sent completed/truncated
         * - we only sent one of completed/truncated
         * - in case of error, we wither did not send anything or sent truncated
         */
        BLE_LL_ASSERT(!(aux_data->flags_ll & BLE_LL_AUX_FLAG_HCI_SENT_ANY) ||
                      ((aux_data->flags_ll & BLE_LL_AUX_FLAG_HCI_SENT_ANY) &&
                       (aux_data->flags_ll & (BLE_LL_AUX_FLAG_HCI_SENT_COMPLETED | BLE_LL_AUX_FLAG_HCI_SENT_TRUNCATED))));
        BLE_LL_ASSERT(!(aux_data->flags_ll & BLE_LL_AUX_FLAG_HCI_SENT_COMPLETED) || !(aux_data->flags_ll & BLE_LL_AUX_FLAG_HCI_SENT_TRUNCATED));
        BLE_LL_ASSERT(!(aux_data->flags_ll & BLE_LL_AUX_FLAG_SCAN_ERROR) ||
                      !(aux_data->flags_ll & BLE_LL_AUX_FLAG_HCI_SENT_ANY) ||
                      (aux_data->flags_ll & BLE_LL_AUX_FLAG_HCI_SENT_TRUNCATED));

        ble_ll_scan_aux_data_free(aux_data);
    }

    OS_EXIT_CRITICAL(sr);
}

static void
ble_ll_scan_sched_remove(struct ble_ll_sched_item *sch)
{
    ble_ll_scan_end_adv_evt(sch->cb_arg);
    ble_ll_scan_aux_data_unref(sch->cb_arg);
    sch->cb_arg = NULL;
}
#endif
/**
 * Stop the scanning state machine
 */
void
ble_ll_scan_sm_stop(int chk_disable)
{
    os_sr_t sr;
    uint8_t lls;
    struct ble_ll_scan_sm *scansm;

    /* Stop the scanning timer  */
    scansm = &g_ble_ll_scan_sm;
    os_cputime_timer_stop(&scansm->scan_timer);

    OS_ENTER_CRITICAL(sr);
    /* Disable scanning state machine */
    scansm->scan_enabled = 0;
    scansm->restart_timer_needed = 0;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (scansm->ext_scanning) {
        ble_ll_scan_clean_cur_aux_data();
        ble_ll_sched_rmv_elem_type(BLE_LL_SCHED_TYPE_AUX_SCAN, ble_ll_scan_sched_remove);
        scansm->ext_scanning = 0;
    }
#endif

    /* Update backoff if we failed to receive scan response */
    if (scansm->scan_rsp_pending) {
        scansm->scan_rsp_pending = 0;
        ble_ll_scan_req_backoff(scansm, 0);
    }
    OS_EXIT_CRITICAL(sr);

    /* Count # of times stopped */
    STATS_INC(ble_ll_stats, scan_stops);

    /* Only set state if we are currently in a scan window */
    if (chk_disable) {
        OS_ENTER_CRITICAL(sr);
        lls = ble_ll_state_get();

        if ((lls == BLE_LL_STATE_SCANNING) ||
                        (lls == BLE_LL_STATE_INITIATING && chk_disable == 1)) {
            /* Disable phy */
            ble_phy_disable();

            /* Set LL state to standby */
            ble_ll_state_set(BLE_LL_STATE_STANDBY);

            /* May need to stop the rfclk */
#ifdef BLE_XCVR_RFCLK
            ble_ll_scan_rfclk_chk_stop();
#endif
        }
        OS_EXIT_CRITICAL(sr);
    }
}

static int
ble_ll_scan_sm_start(struct ble_ll_scan_sm *scansm)
{
    if (!ble_ll_is_valid_own_addr_type(scansm->own_addr_type, g_random_addr)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Count # of times started */
    STATS_INC(ble_ll_stats, scan_starts);

    /* Set flag telling us that scanning is enabled */
    scansm->scan_enabled = 1;

    /* Set first advertising channel */
    BLE_LL_ASSERT(scansm->cur_phy != PHY_NOT_CONFIGURED);
    scansm->phy_data[scansm->cur_phy].scan_chan = BLE_PHY_ADV_CHAN_START;

    if (scansm->next_phy != PHY_NOT_CONFIGURED &&
            scansm->next_phy != scansm->cur_phy) {
        scansm->phy_data[scansm->next_phy].scan_chan = BLE_PHY_ADV_CHAN_START;
    }

    /* Reset scan request backoff parameters to default */
    scansm->upper_limit = 1;
    scansm->backoff_count = 1;
    scansm->scan_rsp_pending = 0;

    /* Forget filtered advertisers from previous scan. */
    g_ble_ll_scan_num_rsp_advs = 0;

    os_mempool_clear(&g_scan_dup_pool);
    TAILQ_INIT(&g_scan_dup_list);

    /* XXX: align to current or next slot???. */
    /* Schedule start time now */
    scansm->phy_data[scansm->cur_phy].scan_win_start_time = os_cputime_get32();

    if (scansm->next_phy != PHY_NOT_CONFIGURED) {
        /* Schedule start time right after first phy */
        scansm->phy_data[scansm->next_phy].scan_win_start_time =
                scansm->phy_data[scansm->cur_phy].scan_win_start_time +
                os_cputime_usecs_to_ticks(scansm->phy_data[scansm->cur_phy].scan_window * BLE_HCI_SCAN_ITVL);
    }

    /* Post scanning event to start off the scanning process */
    ble_ll_event_send(&scansm->scan_sched_ev);

    return BLE_ERR_SUCCESS;
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
static void
ble_ll_scan_switch_current_phy(struct ble_ll_scan_sm *scansm)
{
    uint8_t tmp;

    BLE_LL_ASSERT(scansm->next_phy != PHY_NOT_CONFIGURED);

    tmp = scansm->next_phy;
    scansm->next_phy = scansm->cur_phy;
    scansm->cur_phy = tmp;

    /* PHY is changing in ble_ll_scan_start() */
}

static void
ble_ll_aux_scan_rsp_failed(struct ble_ll_scan_sm *scansm)
{
    if (!scansm->cur_aux_data) {
        return;
    }

    STATS_INC(ble_ll_stats, aux_scan_rsp_err);
    ble_ll_scan_interrupted(scansm);
}
#endif

static void
ble_ll_scan_interrupted_event_cb(struct ble_npl_event *ev)
{
    struct ble_ll_scan_sm *scansm = &g_ble_ll_scan_sm;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    struct ble_ll_aux_data *aux_data;
#endif

    if (!scansm->scan_enabled) {
        return;
    }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    aux_data = ble_npl_event_get_arg(ev);

    if (aux_data) {
        if (scansm->scan_rsp_pending) {
            STATS_INC(ble_ll_stats, aux_scan_rsp_err);
        }
        ble_ll_scan_end_adv_evt(aux_data);
        ble_ll_scan_aux_data_unref(aux_data);
        ble_npl_event_set_arg(ev, NULL);
        STATS_INC(ble_ll_stats, aux_missed_adv);
    }
#endif

    /*
    * If we timed out waiting for a response, the scan response pending
    * flag should be set. Deal with scan backoff. Put device back into rx.
    */

    if (scansm->scan_rsp_pending) {
        scansm->scan_rsp_pending = 0;
        ble_ll_scan_req_backoff(scansm, 0);
    }

    ble_ll_scan_chk_resume();
}

static int
check_phy_window(struct ble_ll_scan_params *scanphy, uint32_t now,
                 uint32_t *next_event_time, uint32_t *dt, uint32_t *win,
                 uint32_t *scan_itvl)
{
    uint32_t win_start;

    win_start = ble_ll_scan_get_scan_win(scanphy, now);

    /* Determine on/off state based on scan window */
    *scan_itvl = os_cputime_usecs_to_ticks(scanphy->scan_itvl *
                                                    BLE_HCI_SCAN_ITVL);

    /* for continues scan assume we are always inside window */
    if (scanphy->scan_window == scanphy->scan_itvl) {
        *next_event_time = win_start + *scan_itvl;
        return 1;
    }

    *win = os_cputime_usecs_to_ticks(scanphy->scan_window * BLE_HCI_SCAN_ITVL);

    /* Check if we are in scan window */
    *dt = now - win_start;
    if (*dt < *win) {
        *next_event_time = win_start + *win;
        return 1;
    }

    *next_event_time = win_start + *scan_itvl;
    return 0;
}

/**
 * Called to process the scanning OS event which was posted to the LL task
 *
 * Context: Link Layer task.
 *
 * @param arg
 */
static void
ble_ll_scan_event_proc(struct ble_npl_event *ev)
{
    os_sr_t sr;
    int inside_window;
    int start_scan;
    uint32_t now;
    uint32_t dt = 0;
    uint32_t win = 0;
    uint32_t scan_itvl = 0;
    uint32_t next_event_time;
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_scan_params *scanphy;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    uint32_t dt_next;
    uint32_t win_next;
    uint32_t scan_itvl_next;
    uint32_t next_event_time_next;
    struct ble_ll_scan_params *scanphy_next;
#endif

#ifdef BLE_XCVR_RFCLK
    uint32_t xtal_ticks;
    int xtal_state;
#endif

    /*
     * Get the scanning state machine. If not enabled (this is possible), just
     * leave and do nothing (just make sure timer is stopped).
     */
    scansm = (struct ble_ll_scan_sm *)ble_npl_event_get_arg(ev);
    scanphy = &scansm->phy_data[scansm->cur_phy];

    OS_ENTER_CRITICAL(sr);
    if (!scansm->scan_enabled) {
        os_cputime_timer_stop(&scansm->scan_timer);
        OS_EXIT_CRITICAL(sr);
        return;
    }

    if (scansm->cur_aux_data || scansm->scan_rsp_pending) {
        /* Aux scan in progress. Wait */
        STATS_INC(ble_ll_stats, scan_timer_stopped);
        scansm->restart_timer_needed = 1;
        OS_EXIT_CRITICAL(sr);
        return;
    }

    now = os_cputime_get32();

    /* check current phy */
    inside_window = check_phy_window(scanphy, now, &next_event_time, &dt, &win,
                                     &scan_itvl);

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (!inside_window) {
        if (scansm->next_phy != PHY_NOT_CONFIGURED) {
            scanphy_next = &scansm->phy_data[scansm->next_phy];

            /* check next phy */
            inside_window = check_phy_window(scanphy_next, now,
                                             &next_event_time_next, &dt_next,
                                             &win_next, &scan_itvl_next);

            /* Update current PHY if either next phy is in window or
             * closest window is for next-PHY
             */
            if (inside_window ||
                    CPUTIME_LEQ(next_event_time_next, next_event_time)) {
                ble_ll_scan_switch_current_phy(scansm);

                dt = dt_next;
                win = win_next;
                scan_itvl = scan_itvl_next;
                next_event_time = next_event_time_next;
            }
        }
    }
#endif

    /*
     * If we are not in the standby state it means that the scheduled
     * scanning event was overlapped in the schedule. In this case all we do
     * is post the scan schedule end event.
     */
    start_scan = 1;
    switch (ble_ll_state_get()) {
    case BLE_LL_STATE_ADV:
    case BLE_LL_STATE_CONNECTION:
    case BLE_LL_STATE_SYNC:
         start_scan = 0;
         break;
    case BLE_LL_STATE_INITIATING:
        /* Must disable PHY since we will move to a new channel */
        ble_phy_disable();
        if (!inside_window) {
            ble_ll_state_set(BLE_LL_STATE_STANDBY);
        }
        /* PHY is disabled - make sure we do not wait for AUX_CONNECT_RSP */
        ble_ll_conn_reset_pending_aux_conn_rsp();
        break;
    case BLE_LL_STATE_SCANNING:
        /* Must disable PHY since we will move to a new channel */
        ble_phy_disable();
        if (!inside_window) {
            ble_ll_state_set(BLE_LL_STATE_STANDBY);
        }
        break;
    case BLE_LL_STATE_STANDBY:
        break;
    default:
        BLE_LL_ASSERT(0);
        break;
    }

    if (start_scan && inside_window) {
#ifdef BLE_XCVR_RFCLK
            xtal_state = ble_ll_xcvr_rfclk_state();
        if (xtal_state != BLE_RFCLK_STATE_SETTLED) {
            if (xtal_state == BLE_RFCLK_STATE_OFF) {
                xtal_ticks = g_ble_ll_data.ll_xtal_ticks;
            } else {
                xtal_ticks = ble_ll_xcvr_rfclk_time_till_settled();
            }

            /*
             * Only bother if we have enough time to receive anything
             * here. The next event time will turn off the clock.
             */
            if (win != 0) {
                if ((win - dt) <= xtal_ticks)  {
                    goto done;
                }
            }

            /*
             * If clock off, start clock. Set next event time to now plus
             * the clock setting time.
             */
            if (xtal_state == BLE_RFCLK_STATE_OFF) {
                ble_ll_xcvr_rfclk_start_now();
            }
            next_event_time = now + xtal_ticks;
            goto done;
        }
#endif
        ble_ll_scan_start(scansm, NULL);
        goto done;
    }

#ifdef BLE_XCVR_RFCLK
    if (inside_window == 0) {
        /*
         * We need to wake up before we need to start scanning in order
         * to make sure the rfclock is on. If we are close to being on,
         * enable the rfclock. If not, set wakeup time.
         */
        if (dt >= (scan_itvl - g_ble_ll_data.ll_xtal_ticks)) {
            /* Start the clock if necessary */
            if (start_scan) {
                if (ble_ll_xcvr_rfclk_state() == BLE_RFCLK_STATE_OFF) {
                    ble_ll_xcvr_rfclk_start_now();
                    next_event_time = now + g_ble_ll_data.ll_xtal_ticks;
                }
            }
        } else {
            next_event_time -= g_ble_ll_data.ll_xtal_ticks;
            if (start_scan) {
                ble_ll_scan_rfclk_chk_stop();
            }
        }
    }
#endif

done:
    OS_EXIT_CRITICAL(sr);
    os_cputime_timer_start(&scansm->scan_timer, next_event_time);
}

/**
 * ble ll scan rx pdu start
 *
 * Called when a PDU reception has started and the Link Layer is in the
 * scanning state.
 *
 * Context: Interrupt
 *
 * @param pdu_type
 * @param rxflags
 *
 * @return int
 *  0: we will not attempt to reply to this frame
 *  1: we may send a response to this frame.
 */
int
ble_ll_scan_rx_isr_start(uint8_t pdu_type, uint16_t *rxflags)
{
    int rc;
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_scan_params *scanphy;

    rc = 0;
    scansm = &g_ble_ll_scan_sm;
    scanphy = &scansm->phy_data[scansm->cur_phy];

    switch (scanphy->scan_type) {
    case BLE_SCAN_TYPE_ACTIVE:
        /* If adv ind or scan ind, we may send scan request */
        if ((pdu_type == BLE_ADV_PDU_TYPE_ADV_IND) ||
            (pdu_type == BLE_ADV_PDU_TYPE_ADV_SCAN_IND)) {
            rc = 1;
        }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        if ((pdu_type == BLE_ADV_PDU_TYPE_ADV_EXT_IND && scansm->ext_scanning)) {
            *rxflags |= BLE_MBUF_HDR_F_EXT_ADV;
            rc = 1;
        }
#endif

        if (scansm->cur_aux_data && !scansm->scan_rsp_pending ) {
            STATS_INC(ble_ll_stats, aux_received);
        }

        /*
         * If this is the first PDU after we sent the scan response (as
         * denoted by the scan rsp pending flag), we set a bit in the ble
         * header so the link layer can check to see if the scan request
         * was successful. We do it this way to let the Link Layer do the
         * work for successful scan requests. If failed, we do the work here.
         */
        if (scansm->scan_rsp_pending) {
            scansm->scan_rsp_pending = 0;

            if (pdu_type == BLE_ADV_PDU_TYPE_SCAN_RSP) {
                *rxflags |= BLE_MBUF_HDR_F_SCAN_RSP_RXD;
            } else if (pdu_type == BLE_ADV_PDU_TYPE_AUX_SCAN_RSP) {
                *rxflags |= BLE_MBUF_HDR_F_SCAN_RSP_RXD;
            } else {
                ble_ll_scan_req_backoff(scansm, 0);
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
                ble_ll_aux_scan_rsp_failed(scansm);
#endif
            }
        }

        /* Disable wfr if running */
        ble_ll_wfr_disable();
        break;
    case BLE_SCAN_TYPE_PASSIVE:
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        if ((pdu_type == BLE_ADV_PDU_TYPE_ADV_EXT_IND && scansm->ext_scanning)) {
            *rxflags |= BLE_MBUF_HDR_F_EXT_ADV;
        }
        break;
#endif
    default:
        break;
    }

    return rc;
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
static uint8_t
ble_ll_ext_adv_phy_mode_to_local_phy(uint8_t adv_phy_mode)
{
    switch (adv_phy_mode) {
    case 0x00:
        return BLE_PHY_1M;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_2M_PHY)
     case 0x01:
        return BLE_PHY_2M;
#endif
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_CODED_PHY)
    case 0x02:
        return BLE_PHY_CODED;
#endif
    }

    return 0;
}

static int
ble_ll_ext_scan_parse_aux_ptr(struct ble_ll_aux_data *aux_data, uint8_t *buf)
{
    uint32_t aux_ptr_field = get_le32(buf) & 0x00FFFFFF;

    aux_data->chan = (aux_ptr_field) & 0x3F;
    if (aux_data->chan >= BLE_PHY_NUM_DATA_CHANS) {
        return -1;
    }

    /* TODO use CA aux_ptr_field >> 6 */

    aux_data->offset = 30 * ((aux_ptr_field >> 8) & 0x1FFF);

    if ((aux_ptr_field >> 7) & 0x01) {
            aux_data->offset *= 10;
            aux_data->offset_units = 1;
    }

    if (aux_data->offset < BLE_LL_MAFS) {
        return -1;
    }

    aux_data->aux_phy =
            ble_ll_ext_adv_phy_mode_to_local_phy((aux_ptr_field >> 21) & 0x07);
    if (aux_data->aux_phy == 0) {
        return -1;
    }

    return 0;
}

static void
ble_ll_ext_scan_parse_adv_info(struct ext_adv_report *report, const uint8_t *buf)
{
    uint16_t adv_info = get_le16(buf);

    /* TODO Use DID */

    report->sid = (adv_info >> 12);
}

/**
 * ble_ll_scan_update_aux_data
 *
 * Update aux_data stored in ble_hdr.rxinfo.user_data. If no aux_data is present
 * (i.e. processing ADV_EXT_IND) this will try to allocate new aux_data.
 *
 * Context: Interrupt
 *
 * @param ble_hdr
 * @param rxbuf
 *
 * @return int
 *  1: do not scan for next AUX (no AuxPtr or malformed data)
 *  0: scan for next AUX (valid AuxPtr found)
 * -1: error
 */
int
ble_ll_scan_update_aux_data(struct ble_mbuf_hdr *ble_hdr, uint8_t *rxbuf,
                            bool *adva_present)
{
    uint8_t pdu_hdr;
    uint8_t pdu_len;
    uint8_t adv_mode;
    uint8_t eh_len;
    uint8_t eh_flags;
    uint8_t *eh;
    struct ble_ll_aux_data *aux_data;
    bool is_aux;

    aux_data = ble_hdr->rxinfo.user_data;
    /* aux_data is initially not set only for ADV_EXT_IND */
    is_aux = aux_data;

    pdu_hdr = rxbuf[0];
    pdu_len = rxbuf[1];

    /* PDU without at least Extended Header Length is invalid */
    if (pdu_len == 0) {
        return -1;
    }

    adv_mode = rxbuf[2] >> 6;
    eh_len = rxbuf[2] & 0x3f;
    eh_flags = rxbuf[3];
    eh = &rxbuf[4];

    /*
     * PDU without Extended Header is valid in case of last AUX_CHAIN_IND in
     * chain so aux_data has to be set and advertising mode has to be 00b,
     * otherwise it's an invalid PDU.
     */
    if (eh_len == 0) {
        if (!aux_data || adv_mode) {
            return -1;
        }
        aux_data->flags_isr |= BLE_LL_AUX_FLAG_SCAN_COMPLETE;
        return 1;
    }

    /*
     * If aux_data is not set, this is ADV_EXT_IND which starts new extended
     * advertising event.
     */
    if (!aux_data) {
        if (ble_ll_scan_ext_adv_init(&aux_data)) {
            return -1;
        }

        aux_data->aux_primary_phy = ble_hdr->rxinfo.phy;
    } else {
        aux_data->flags_isr |= BLE_LL_AUX_FLAG_AUX_RECEIVED;
    }

    /* Now parse extended header... */

    if (eh_flags & (1 << BLE_LL_EXT_ADV_ADVA_BIT)) {
        aux_data->flags |= BLE_LL_AUX_HAS_ADDRA;
        memcpy(aux_data->addr, eh, 6);
        aux_data->addr_type = !!(pdu_hdr & BLE_ADV_PDU_HDR_TXADD_MASK);
        eh += BLE_LL_EXT_ADV_ADVA_SIZE;

        if (adva_present) {
            *adva_present = true;
        }
    }

    if (eh_flags & (1 << BLE_LL_EXT_ADV_TARGETA_BIT)) {
        aux_data->flags |= BLE_LL_AUX_HAS_DIR_ADDRA;
        memcpy(aux_data->dir_addr, eh, 6);
        aux_data->dir_addr_type = !!(pdu_hdr & BLE_ADV_PDU_HDR_RXADD_MASK);
        eh += BLE_LL_EXT_ADV_TARGETA_SIZE;
    }


    if (eh_flags & (1 << BLE_LL_EXT_ADV_CTE_INFO_BIT)) {
        eh += 1;
    }

    if (eh_flags & (1 << BLE_LL_EXT_ADV_DATA_INFO_BIT)) {
        aux_data->flags |= BLE_LL_AUX_HAS_ADI;
        if (is_aux) {
            if (get_le16(eh) != aux_data->adi) {
                aux_data->flags_isr |= BLE_LL_AUX_FLAG_SCAN_ERROR;
                STATS_INC(ble_ll_stats, aux_chain_err);
            }
        } else {
            aux_data->adi = get_le16(eh);
        }
        eh += BLE_LL_EXT_ADV_DATA_INFO_SIZE;
    }

    if (eh_flags & (1 << BLE_LL_EXT_ADV_AUX_PTR_BIT)) {
        if (ble_ll_ext_scan_parse_aux_ptr(aux_data, eh)) {
            aux_data->flags_isr |= BLE_LL_AUX_FLAG_SCAN_ERROR;
        }
    } else if (!(adv_mode & BLE_LL_EXT_ADV_MODE_SCAN)) {
        /* No AuxPtr for scannable PDU is ignored since we can still scan it */
        aux_data->flags_isr |= BLE_LL_AUX_FLAG_SCAN_COMPLETE;
    }

    ble_hdr->rxinfo.user_data = aux_data;

    /* Do not scan for next AUX if either no AuxPtr or malformed data found */
    return !(eh_flags & (1 << BLE_LL_EXT_ADV_AUX_PTR_BIT)) ||
           (aux_data->flags_isr & BLE_LL_AUX_FLAG_SCAN_ERROR);
}

/**
 * Called when a receive ADV_EXT PDU has ended.
 *
 * Context: Interrupt
 *
 * @return int
 *       < 0  Error
 *      >= 0: Success (number of bytes left in PDU)
 *
 */
static int
ble_ll_scan_parse_ext_hdr(struct os_mbuf *om,
                          const uint8_t *adva, uint8_t adva_type,
                          const uint8_t *inita, uint8_t inita_type,
                          struct ble_mbuf_hdr *ble_hdr,
                          struct ext_adv_report *report)
{
    uint8_t pdu_len;
    uint8_t ext_hdr_len;
    uint8_t ext_hdr_flags;
    uint8_t *ext_hdr;
    uint8_t *rxbuf = om->om_data;
    int i = 1;
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_aux_data *aux_data = ble_hdr->rxinfo.user_data;

    BLE_LL_ASSERT(report);

    scansm = &g_ble_ll_scan_sm;

    if (!scansm->ext_scanning) {
       /* Ignore ext adv if host does not want it*/
       return -1;
    }

    pdu_len = rxbuf[1];
    if (pdu_len == 0) {
        return -1;
    }

    report->evt_type = rxbuf[2] >> 6;
    if ( report->evt_type > BLE_LL_EXT_ADV_MODE_SCAN) {
        return -1;
    }

    if (BLE_MBUF_HDR_SCAN_RSP_RXD(ble_hdr)) {
        report->evt_type |= BLE_HCI_ADV_SCAN_RSP_MASK;
    }

    ext_hdr_len = rxbuf[2] & 0x3F;
    os_mbuf_adj(om, 3);

    ext_hdr_flags = rxbuf[3];
    ext_hdr = &rxbuf[4];

    if (ext_hdr_len) {
        i = 0;
        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_ADVA_BIT)) {
            i += BLE_LL_EXT_ADV_ADVA_SIZE;
        }

        if (adva) {
            memcpy(report->addr, adva, 6);
            report->addr_type = adva_type;
        }

        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_TARGETA_BIT)) {
            i += BLE_LL_EXT_ADV_TARGETA_SIZE;
        }

        if (inita) {
           memcpy(report->dir_addr, inita, 6);
           report->dir_addr_type = inita_type;
           report->evt_type |= BLE_HCI_ADV_DIRECT_MASK;
        }

        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_CTE_INFO_BIT)) {
            /* Just skip it for now*/
            i += 1;
        }

        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_DATA_INFO_BIT)) {
            ble_ll_ext_scan_parse_adv_info(report, (ext_hdr + i));
            i += BLE_LL_EXT_ADV_DATA_INFO_SIZE;
        } else if (report->evt_type & BLE_HCI_ADV_SCAN_RSP_MASK) {
            report->sid = (aux_data->adi >> 12);
        }

        /* In this point of time we don't want to care about aux ptr */
        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_AUX_PTR_BIT)) {
            i += BLE_LL_EXT_ADV_AUX_PTR_SIZE;
        }

        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_SYNC_INFO_BIT)) {
            report->periodic_itvl = get_le16(ext_hdr + i + 2);
            i += BLE_LL_EXT_ADV_SYNC_INFO_SIZE;
        }

        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_TX_POWER_BIT)) {
            report->tx_power = *(ext_hdr + i);
            i += BLE_LL_EXT_ADV_TX_POWER_SIZE;
        }

        /* TODO Handle ACAD if needed */
    }

    /* In the event we need information on primary and secondary PHY used during
     * advertising.
     */
    if (!aux_data) {
        report->pri_phy = ble_hdr->rxinfo.phy;
        goto done;
    }

    report->sec_phy = aux_data->aux_phy;
    report->pri_phy = aux_data->aux_primary_phy;

    if (ext_hdr_len) {
        /* Adjust mbuf to contain advertising data only */
        os_mbuf_adj(om, ext_hdr_len);
    }

    /* Let us first keep update event type in aux data.
     * Note that in aux chain and aux scan response packets
     * we do miss original event type, which we need for advertising report.
     */
    aux_data->evt_type |= report->evt_type;
    report->evt_type = aux_data->evt_type;

done:
    return pdu_len - ext_hdr_len - 1;
}

static int
ble_ll_scan_get_addr_from_ext_adv(uint8_t *rxbuf, struct ble_mbuf_hdr *ble_hdr,
                                  uint8_t **addr, uint8_t *addr_type,
                                  uint8_t **inita, uint8_t *inita_type,
                                  int *ext_mode)
{
    uint8_t pdu_len;
    uint8_t ext_hdr_len;
    uint8_t ext_hdr_flags;
    uint8_t *ext_hdr;
    bool has_adva = false;
    bool has_inita = false;
    int i;
    struct ble_ll_aux_data *aux_data = ble_hdr->rxinfo.user_data;

    *addr = NULL;
    *inita = NULL;

    pdu_len = rxbuf[1];
    if (pdu_len == 0) {
        return -1;
    }

    *ext_mode = rxbuf[2] >> 6;
    if (*ext_mode > BLE_LL_EXT_ADV_MODE_SCAN) {
        return -1;
    }

    ext_hdr_len = rxbuf[2] & 0x3F;
    if (ext_hdr_len == 0) {
        goto done;
    }

    ext_hdr_flags = rxbuf[3];
    ext_hdr = &rxbuf[4];

    i = 0;
    if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_ADVA_BIT)) {
        if (ext_hdr_len < BLE_LL_EXT_ADV_ADVA_SIZE) {
            return -1;
        }

        *addr = ext_hdr + i;
        *addr_type =
                ble_ll_get_addr_type(rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK);
        i += BLE_LL_EXT_ADV_ADVA_SIZE;

        has_adva = true;
    }

    if (!inita) {
        goto done;
    }

    if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_TARGETA_BIT)) {
        *inita = ext_hdr + i;
        *inita_type =
                ble_ll_get_addr_type(rxbuf[0] & BLE_ADV_PDU_HDR_RXADD_MASK);
        i += BLE_LL_EXT_ADV_TARGETA_SIZE;

        has_inita = true;
    }

done:
    /* Check if we had address already. If yes, replace it with new one */

    if (aux_data) {
        /* If address has been provided, we do have it already in aux_data.*/
        if (aux_data->flags & BLE_LL_AUX_HAS_ADDRA) {
            if (!has_adva) {
                *addr = aux_data->addr;
                *addr_type = aux_data->addr_type;
            } else {
                memcpy(aux_data->addr, *addr, 6);
                aux_data->addr_type = *addr_type;
            }
        }

        if (!inita) {
            return 0;
        }

        if (aux_data->flags & BLE_LL_AUX_HAS_DIR_ADDRA) {
            if (!has_inita) {
                *inita = aux_data->dir_addr;
                *inita_type = aux_data->dir_addr_type;
            } else {
                memcpy(aux_data->dir_addr, *inita, 6);
                aux_data->dir_addr_type = *inita_type;
            }
        }
    }

    return 0;
}
#endif

int
ble_ll_scan_adv_decode_addr(uint8_t pdu_type, uint8_t *rxbuf,
                            struct ble_mbuf_hdr *ble_hdr,
                            uint8_t **addr, uint8_t *addr_type,
                            uint8_t **inita, uint8_t *inita_type,
                            int *ext_mode)
{
    /*
     * XXX this should be only used for legacy advertising, but need to refactor
     *     code in ble_ll_init first so it does not call this for ext
     */

    if (pdu_type != BLE_ADV_PDU_TYPE_ADV_EXT_IND &&
        pdu_type != BLE_ADV_PDU_TYPE_AUX_CONNECT_RSP) {
        /* Legacy advertising */
        *addr_type = ble_ll_get_addr_type(rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK);
        *addr = rxbuf + BLE_LL_PDU_HDR_LEN;

        if (!inita) {
            return 0;
        }

        if (pdu_type != BLE_ADV_PDU_TYPE_ADV_DIRECT_IND) {
            *inita = NULL;
            *inita_type = 0;
            return 0;
        }

        *inita = rxbuf + BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN;
        *inita_type = ble_ll_get_addr_type(rxbuf[0] & BLE_ADV_PDU_HDR_RXADD_MASK);

        return 0;
    }

    /* Extended advertising */
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    return ble_ll_scan_get_addr_from_ext_adv(rxbuf, ble_hdr, addr, addr_type,
                                          inita, inita_type, ext_mode);
#else
    return -1;
#endif

    return 0;
}

/**
 * Called when a receive PDU has ended.
 *
 * Context: Interrupt
 *
 * @param rxpdu
 *
 * @return int
 *       < 0: Disable the phy after reception.
 *      == 0: Success. Do not disable the PHY.
 *       > 0: Do not disable PHY as that has already been done.
 */
int
ble_ll_scan_rx_isr_end(struct os_mbuf *rxpdu, uint8_t crcok)
{
    int rc;
    int chk_send_req;
    int chk_wl;
    int index;
    int resolved;
    uint8_t pdu_type;
    uint8_t addr_type;
    uint8_t peer_addr_type = 0;
    uint8_t *adv_addr = NULL;
    uint8_t *peer = NULL;
    uint8_t *inita = NULL;
    uint8_t inita_type = 0;
    uint8_t *rxbuf;
    struct ble_mbuf_hdr *ble_hdr;
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_scan_params *scanphy;
    int ext_adv_mode = -1;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    uint8_t phy_mode;
    uint16_t adi;
    struct ble_ll_aux_data *aux_data = NULL;
#endif
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    struct ble_ll_resolv_entry *rl = NULL;
    bool resolve_peer = false;
#endif

    /* Get scanning state machine */
    scansm = &g_ble_ll_scan_sm;
    scanphy = &scansm->phy_data[scansm->cur_phy];

    /*
     * The reason we do something different here (as opposed to failed CRC) is
     * that the received PDU will not be handed up in this case. So we have
     * to restart scanning and handle a failed scan request. Note that we
     * return 0 in this case because we dont want the phy disabled.
     */
    if (rxpdu == NULL) {
        ble_ll_scan_interrupted(scansm);
        return 0;
    }

    rc = -1;

    ble_hdr = BLE_MBUF_HDR_PTR(rxpdu);

    /* Get pdu type, pointer to address and address "type"  */
    rxbuf = rxpdu->om_data;
    pdu_type = rxbuf[0] & BLE_ADV_PDU_HDR_TYPE_MASK;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (scansm->cur_aux_data) {
        ble_hdr->rxinfo.user_data = scansm->cur_aux_data;
        scansm->cur_aux_data = NULL;
        /* If we were expecting aux/chain and it not arrived,
         * lets just exit here.
         */
        if (pdu_type != BLE_ADV_PDU_TYPE_ADV_EXT_IND) {
            goto scan_rx_isr_exit;
        }
    }
#endif

    /* Just leave if the CRC is not OK. */
    if (!crcok) {
        goto scan_rx_isr_exit;
    }

    switch (pdu_type) {
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    /*
     * For extended advertising events all data are decoded to aux_data and can
     * be retrieved from there. For legacy advertising we need to get addresses
     * from PDU.
     */
    case BLE_ADV_PDU_TYPE_ADV_EXT_IND:
        if (!scansm->ext_scanning) {
            goto scan_rx_isr_exit;
        }
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
        rc = ble_ll_scan_update_aux_data(ble_hdr, rxbuf, &resolve_peer);
#else
        rc = ble_ll_scan_update_aux_data(ble_hdr, rxbuf, NULL);
#endif
        if (rc < 0) {
            ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_AUX_INVALID;
            goto scan_rx_isr_exit;
        } else if (rc == 0) {
            ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_AUX_PTR_WAIT;
        }

        ext_adv_mode = rxbuf[2] >> 6;

        aux_data = ble_hdr->rxinfo.user_data;
        if (aux_data->flags & BLE_LL_AUX_HAS_ADDRA) {
            peer = aux_data->addr;
            peer_addr_type = aux_data->addr_type;
        }
        if (aux_data->flags & BLE_LL_AUX_HAS_DIR_ADDRA) {
            inita = aux_data->dir_addr;
            inita_type = aux_data->dir_addr_type;
        }

        rc = -1;
    break;
#endif
    default:
        /* Try to decode addresses from PDU */
        if (ble_ll_scan_adv_decode_addr(pdu_type, rxbuf, ble_hdr, &peer,
                                        &peer_addr_type, &inita, &inita_type,
                                        &ext_adv_mode)) {
            goto scan_rx_isr_exit;
        }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
        resolve_peer = true;
#endif
        break;
    }

    /* Determine if request may be sent and if whitelist needs to be checked */
    chk_send_req = 0;
    switch (pdu_type) {
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    case BLE_ADV_PDU_TYPE_ADV_EXT_IND:
        if (!peer) {
            /*Wait for AUX ptr */
            goto scan_rx_isr_exit;
        }

        if ((ext_adv_mode == BLE_LL_EXT_ADV_MODE_SCAN) &&
                (scanphy->scan_type == BLE_SCAN_TYPE_ACTIVE)) {
            chk_send_req = 1;
        }
        chk_wl = 1;
    break;
#endif
    case BLE_ADV_PDU_TYPE_ADV_IND:
    case BLE_ADV_PDU_TYPE_ADV_SCAN_IND:
        if (scanphy->scan_type == BLE_SCAN_TYPE_ACTIVE) {
            chk_send_req = 1;
        }
        chk_wl = 1;
        break;
    case BLE_ADV_PDU_TYPE_ADV_NONCONN_IND:
    case BLE_ADV_PDU_TYPE_ADV_DIRECT_IND:
        chk_wl = 1;
        break;
    default:
        chk_wl = 0;
        break;
    }

    /* Since peer might point to different address e.g. resolved one
     * lets do a copy of pointers for scan request
     */
    adv_addr = peer;
    addr_type = peer_addr_type;

    if ((scanphy->scan_filt_policy & 1) == 0) {
        chk_wl = 0;
    }
    resolved = 0;

    index = -1;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    /* Only resolve if packet actually contained AdvA. With Extended Advertising
     * is it possible that AdvA is present either in ADV_EXT_IND or AUX_ADV_IND
     */
    if (ble_ll_resolv_enabled()) {
        if (ble_ll_is_rpa(peer, peer_addr_type)) {
            if (resolve_peer) {
                index = ble_hw_resolv_list_match();
            } else {
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
                /* for legacy PDUs we always attempt to resolve */
                BLE_LL_ASSERT(aux_data);
                index = aux_data->rpa_index;
#else
                BLE_LL_ASSERT(false);
#endif
            }

            if (index >= 0) {
                ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_RESOLVED;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
                if (aux_data) {
                    aux_data->rpa_index = index;
                }
#endif
                peer = g_ble_ll_resolv_list[index].rl_identity_addr;
                peer_addr_type = g_ble_ll_resolv_list[index].rl_addr_type;
                resolved = 1;
                if (ble_ll_is_rpa(inita, inita_type)) {
                    if (!ble_ll_resolv_rpa(inita, g_ble_ll_resolv_list[index].rl_local_irk)) {
                        goto scan_rx_isr_exit;
                    }
                    ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_INITA_RESOLVED;
                }
            } else {
                if (chk_wl) {
                    goto scan_rx_isr_exit;
                }
                /* We don't know peer and InitA is RPA so nothing to do more here */
                if (chk_send_req && inita && ble_ll_is_rpa(inita, inita_type)) {
                    goto scan_rx_isr_exit;
                }
            }
        } else if (chk_send_req && inita && ble_ll_is_rpa(inita, inita_type)) {
            /* If remote is identity address but InitA is RPA, make sure we can resolve it.
             * If not, nothing more to do here
             */
            rl = ble_ll_resolv_list_find(peer, peer_addr_type);
            if (!rl) {
                goto scan_rx_isr_exit;
            }

            if (!ble_ll_resolv_rpa(inita, rl->rl_local_irk)) {
                goto scan_rx_isr_exit;
            }

            ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_INITA_RESOLVED;
        }
    }

    ble_hdr->rxinfo.rpa_index = index;
#else
    if (chk_send_req && inita && ble_ll_is_rpa(inita, inita_type)) {
        goto scan_rx_isr_exit;
    }
#endif

    /* If whitelist enabled, check to see if device is in the white list */
    if (chk_wl && !ble_ll_whitelist_match(peer, peer_addr_type, resolved)) {
        goto scan_rx_isr_exit;
    }
    ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_DEVMATCH;

    /* Should we send a scan request? */
    if (chk_send_req) {
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        if (pdu_type == BLE_ADV_PDU_TYPE_ADV_EXT_IND) {
            /* Let us check if InitA is our device.
             * Note that InitA RPA is handled above where privacy is handled
             */
            if (inita && !ble_ll_is_rpa(inita, inita_type) &&
                         !ble_ll_is_our_devaddr(inita, inita_type)) {
                goto scan_rx_isr_exit;
            }

            if (ble_ll_scan_get_adi(ble_hdr->rxinfo.user_data, &adi) < 0) {
                /* There is not ADI in scannable packet? This must be some trash,
                 * ignore it
                 */
                goto scan_rx_isr_exit;
            }
            if (ble_ll_scan_have_rxd_scan_rsp(peer, peer_addr_type, 1, adi)) {
                goto scan_rx_isr_exit;
            }
        } else {
            /* Dont send scan request if we have sent one to this advertiser */
            if (ble_ll_scan_have_rxd_scan_rsp(peer, peer_addr_type, 0, 0)) {
                goto scan_rx_isr_exit;
            }
        }
#else
        /* Dont send scan request if we have sent one to this advertiser */
        if (ble_ll_scan_have_rxd_scan_rsp(peer, peer_addr_type, 0, 0)) {
            goto scan_rx_isr_exit;
        }
#endif
        /* Better not be a scan response pending */
        BLE_LL_ASSERT(scansm->scan_rsp_pending == 0);

        /* We want to send a request. See if backoff allows us */
        if (scansm->backoff_count > 0) {
            scansm->backoff_count--;
        }

        if (scansm->backoff_count == 0) {
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
            phy_mode = ble_ll_phy_to_phy_mode(ble_hdr->rxinfo.phy,
                                              BLE_HCI_LE_PHY_CODED_ANY);
            if (ble_ll_sched_scan_req_over_aux_ptr(ble_hdr->rxinfo.channel,
                                                   phy_mode)) {
                goto scan_rx_isr_exit;
            }
#endif
            /* XXX: TODO assume we are on correct phy */
            ble_ll_scan_req_pdu_prepare(scansm, adv_addr, addr_type, index);
            rc = ble_phy_tx(ble_ll_scan_req_tx_pdu_cb, scansm,
                            BLE_PHY_TRANSITION_TX_RX);

            if (rc == 0) {
                /* Set "waiting for scan response" flag */

                scansm->scan_rsp_pending = 1;
                ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_SCAN_REQ_TXD;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
                if (ble_hdr->rxinfo.channel <  BLE_PHY_NUM_DATA_CHANS) {
                    /* Let's keep the aux ptr as a reference to scan rsp */
                    scansm->cur_aux_data =
                            ble_ll_scan_aux_data_ref(ble_hdr->rxinfo.user_data);
                    STATS_INC(ble_ll_stats, aux_scan_req_tx);
                }
#endif
            }
        }
    }

scan_rx_isr_exit:
    if (rc) {
        ble_ll_state_set(BLE_LL_STATE_STANDBY);
    }
    return rc;
}

/**
 * Called to resume scanning. This is called after an advertising event or
 * connection event has ended. It is also called if we receive a packet while
 * in the initiating or scanning state.
 *
 * If periodic advertising is enabled this is also called on sync event end
 * or sync packet received if chaining
 *
 * Context: Link Layer task
 */
void
ble_ll_scan_chk_resume(void)
{
    os_sr_t sr;
    struct ble_ll_scan_sm *scansm;

    scansm = &g_ble_ll_scan_sm;
    if (scansm->scan_enabled) {
        OS_ENTER_CRITICAL(sr);
        if (scansm->restart_timer_needed) {
            scansm->restart_timer_needed = 0;
            ble_ll_event_send(&scansm->scan_sched_ev);
            STATS_INC(ble_ll_stats, scan_timer_restarted);
            OS_EXIT_CRITICAL(sr);
            return;
        }

        if (ble_ll_state_get() == BLE_LL_STATE_STANDBY &&
                    ble_ll_scan_window_chk(scansm, os_cputime_get32()) == 0) {
            /* Turn on the receiver and set state */
            ble_ll_scan_start(scansm, NULL);
        }
        OS_EXIT_CRITICAL(sr);
    }
}

/**
 * Scan timer callback; means that the scan window timeout has been reached
 * and we should perform the appropriate actions.
 *
 * Context: Interrupt (cputimer)
 *
 * @param arg Pointer to scan state machine.
 */
void
ble_ll_scan_timer_cb(void *arg)
{
    struct ble_ll_scan_sm *scansm;

    scansm = (struct ble_ll_scan_sm *)arg;
    ble_ll_event_send(&scansm->scan_sched_ev);
}

void
ble_ll_scan_interrupted(struct ble_ll_scan_sm *scansm)
{
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    ble_npl_event_set_arg(&scansm->scan_interrupted_ev, scansm->cur_aux_data);
    scansm->cur_aux_data = NULL;
#endif

    ble_ll_event_send(&scansm->scan_interrupted_ev);
}

/**
 * Called when the wait for response timer expires while in the scanning
 * state.
 *
 * Context: Interrupt.
 */
void
ble_ll_scan_wfr_timer_exp(void)
{
    struct ble_ll_scan_sm *scansm;
    uint8_t chan;
    int phy;
    int rc;
#if (BLE_LL_BT5_PHY_SUPPORTED == 1)
    uint8_t phy_mode;
#endif

    scansm = &g_ble_ll_scan_sm;

    /* Update backoff if we failed to receive scan response */
    if (scansm->scan_rsp_pending) {
        scansm->scan_rsp_pending = 0;
        ble_ll_scan_req_backoff(scansm, 0);
    }

    if (scansm->cur_aux_data) {
        /* We actually care about interrupted scan only for EXT ADV because only
         * then we might consider to send truncated event to the host.
         */
        ble_ll_scan_interrupted(scansm);

        /* Need to disable phy since we are going to move to BLE_LL_STATE_STANDBY
         * or we will need to change channel to primary one
         */
        ble_phy_disable();

        if (ble_ll_scan_window_chk(scansm, os_cputime_get32()) == 1) {
            /* Outside the window scan */
            ble_ll_state_set(BLE_LL_STATE_STANDBY);
            return;
        }

        ble_ll_get_chan_to_scan(scansm, &chan, &phy);
#if (BLE_LL_BT5_PHY_SUPPORTED == 1)
        phy_mode = ble_ll_phy_to_phy_mode(phy, BLE_HCI_LE_PHY_CODED_ANY);
        ble_phy_mode_set(phy_mode, phy_mode);
#endif
        rc = ble_phy_setchan(chan, BLE_ACCESS_ADDR_ADV, BLE_LL_CRCINIT_ADV);
        BLE_LL_ASSERT(rc == 0);
    }


    ble_phy_restart_rx();
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
/*
 * Send extended advertising report
 *
 * @return -1 on error (data truncated or other error)
 *          0 on success (data status is "completed")
 *          1 on success (data status is not "completed")
 */
static int
ble_ll_hci_send_ext_adv_report(uint8_t ptype, uint8_t *adva, uint8_t adva_type,
                               uint8_t *inita, uint8_t inita_type,
                               struct os_mbuf *om,
                               struct ble_mbuf_hdr *hdr)
{
    struct ble_ll_aux_data *aux_data = hdr->rxinfo.user_data;
    struct ble_hci_ev_le_subev_ext_adv_rpt *ev;
    struct ext_adv_report *report;
    struct ble_hci_ev *hci_ev;
    struct ble_hci_ev *hci_ev_next;
    int offset;
    int datalen;
    int rc;
    bool need_event;
    bool is_scannable_aux;
    uint8_t max_data_len;

    if (!ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_EXT_ADV_RPT)) {
        rc = -1;
        goto done;
    }

    /*
     * We keep one allocated event in aux_data to be able to truncate chain
     * properly in case of error. If there is no event in aux_data it means this
     * is the first event for this chain.
     */
    if (aux_data && aux_data->evt) {
        hci_ev = aux_data->evt;
        aux_data->evt = NULL;

        hci_ev->length = sizeof(*ev) + sizeof(*report);
    } else {
        hci_ev = ble_ll_scan_get_ext_adv_report(NULL);
        if (!hci_ev) {
            rc = -1;
            goto done;
        }
    }

    ev = (void *) hci_ev->data;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    /* If RPA has been used, make sure we use correct address types
     * in the advertising report.
     */
    if (BLE_MBUF_HDR_RESOLVED(hdr)) {
        adva_type += 2;
    }
    if (BLE_MBUF_HDR_INITA_RESOLVED(hdr)) {
        inita_type += 2;
    }
#endif

    datalen = ble_ll_scan_parse_ext_hdr(om, adva, adva_type, inita, inita_type,
                                        hdr, ev->reports);
    if (datalen < 0) {
        rc = -1;

        /* Need to send truncated event if we already sent some reports */
        if (aux_data && (aux_data->flags_ll & BLE_LL_AUX_FLAG_HCI_SENT_ANY)) {
            BLE_LL_ASSERT(!(aux_data->flags_ll & BLE_LL_AUX_FLAG_HCI_SENT_COMPLETED));
            BLE_LL_ASSERT(!(aux_data->flags_ll & BLE_LL_AUX_FLAG_HCI_SENT_TRUNCATED));

            aux_data->flags_ll |= BLE_LL_AUX_FLAG_SCAN_ERROR;
            aux_data->flags_ll |= BLE_LL_AUX_FLAG_HCI_SENT_TRUNCATED;

            report = ev->reports;
            report->data_len = 0;
            report->evt_type |= BLE_HCI_ADV_DATA_STATUS_TRUNCATED;

            ble_ll_hci_event_send(hci_ev);
            goto done;
        }

        ble_hci_trans_buf_free((uint8_t *)hci_ev);
        goto done;
    }

    is_scannable_aux = aux_data &&
                       (aux_data->evt_type & BLE_HCI_ADV_SCAN_MASK) &&
                       !(aux_data->evt_type & BLE_HCI_ADV_SCAN_RSP_MASK);

    max_data_len = BLE_LL_MAX_EVT_LEN - sizeof(*hci_ev) - sizeof(*ev) - sizeof(*report);
    offset = 0;

    do {
        hci_ev_next = NULL;

        ev = (void *) hci_ev->data;
        report = ev->reports;

        report->data_len = min(max_data_len, datalen - offset);

        /* adjust event length */
        hci_ev->length += report->data_len;
        report->rssi = hdr->rxinfo.rssi;

        os_mbuf_copydata(om, offset, report->data_len, report->data);
        offset += report->data_len;

        /*
         * We need another event if either there are still some data left to
         * send in current PDU or scan is not completed. The only exception is
         * when this is a scannable event which is not a scan response.
         */
        need_event = ((offset < datalen) || (aux_data && !(aux_data->flags_ll & BLE_LL_AUX_FLAG_SCAN_COMPLETE))) && !is_scannable_aux;

        if (need_event) {
            /*
             * We will need another event so let's try to allocate one now. If
             * we cannot do this, need to mark event as truncated.
             */
            hci_ev_next = ble_ll_scan_get_ext_adv_report(report);

            if (hci_ev_next) {
                report->evt_type |= BLE_HCI_ADV_DATA_STATUS_INCOMPLETE;
                rc = 1;
            } else {
                report->evt_type |= BLE_HCI_ADV_DATA_STATUS_TRUNCATED;
                rc = -1;
            }
        } else if (aux_data && (aux_data->flags_ll & BLE_LL_AUX_FLAG_SCAN_ERROR)) {
            report->evt_type |= BLE_HCI_ADV_DATA_STATUS_TRUNCATED;
            rc = -1;
        } else {
            rc = 0;
        }

        if ((rc == -1) && aux_data) {
            aux_data->flags_ll |= BLE_LL_AUX_FLAG_SCAN_ERROR;

            if (!(aux_data->flags_ll & BLE_LL_AUX_FLAG_HCI_SENT_ANY)) {
                ble_hci_trans_buf_free((uint8_t *)hci_ev);
                goto  done;
            }

            aux_data->flags_ll |= BLE_LL_AUX_FLAG_HCI_SENT_TRUNCATED;
        } else if (!is_scannable_aux) {
            /*
             * We do not set 'sent' flags for scannable AUX since we only care
             * about scan response that will come next.
             */
            aux_data->flags_ll |= BLE_LL_AUX_FLAG_HCI_SENT_ANY;
            if (rc == 0) {
                aux_data->flags_ll |= BLE_LL_AUX_FLAG_HCI_SENT_COMPLETED;
            }
        }

        ble_ll_hci_event_send(hci_ev);

        hci_ev = hci_ev_next;
    } while ((offset < datalen) && hci_ev);

    BLE_LL_ASSERT(offset <= datalen);

    if (aux_data) {
        /* Store any event left for later use */
        aux_data->evt = hci_ev;
    } else {
        /* If it is empty beacon, evt shall be NULL */
        BLE_LL_ASSERT(!hci_ev);
    }

done:
    if (!aux_data) {
        return rc;
    }

    if (rc == 0) {
        if (aux_data->evt_type & BLE_HCI_ADV_SCAN_RSP_MASK) {
            /* Complete scan response can be added to duplicates list */
            ble_ll_scan_add_scan_rsp_adv(aux_data->addr, aux_data->addr_type,
                                         1, aux_data->adi);
        } else if (is_scannable_aux) {
            /*
             * Scannable AUX is marked as incomplete because we do not want to
             * add this to duplicates list now, this should happen only after
             * we receive complete scan response. The drawback here is that we
             * will keep receiving reports for scannable PDUs until complete
             * scan response is received.
             *
             * XXX ^^ extend duplicates list to fix
             */
            rc = 1;
        }
    } else if (rc < 0) {
        aux_data->flags_ll |= BLE_LL_AUX_FLAG_SCAN_ERROR;
    }

    return rc;
}
#endif

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PERIODIC_ADV)
static void
check_periodic_sync(const struct os_mbuf *om, struct ble_mbuf_hdr *rxhdr,
                          uint8_t *adva, uint8_t adva_type, int rpa_index)
{
    uint8_t pdu_len;
    uint8_t ext_hdr_len;
    uint8_t ext_hdr_flags;
    uint8_t *ext_hdr;
    uint8_t *rxbuf = om->om_data;
    uint8_t sid;
    int i;

    pdu_len = rxbuf[1];
    if (pdu_len == 0) {
        return;
    }

    ext_hdr_len = rxbuf[2] & 0x3F;

    if (ext_hdr_len) {
        ext_hdr_flags = rxbuf[3];
        ext_hdr = &rxbuf[4];
        i = 0;

        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_ADVA_BIT)) {
            i += BLE_LL_EXT_ADV_ADVA_SIZE;
        }

        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_TARGETA_BIT)) {
            i += BLE_LL_EXT_ADV_TARGETA_SIZE;
        }

        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_CTE_INFO_BIT)) {
            i += 1;
        }

        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_DATA_INFO_BIT)) {
            sid = (get_le16(ext_hdr + i) >> 12);
            i += BLE_LL_EXT_ADV_DATA_INFO_SIZE;
        } else {
            /* ADI is mandatory */
            return;
        }

        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_AUX_PTR_BIT)) {
            i += BLE_LL_EXT_ADV_AUX_PTR_SIZE;
        }

        if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_SYNC_INFO_BIT)) {
            ble_ll_sync_info_event(adva, adva_type, rpa_index, sid, rxhdr,
                                   ext_hdr + i);
        }
    }
}
#endif

static inline void
ble_ll_scan_dup_move_to_head(struct ble_ll_scan_dup_entry *e)
{
    if (e != TAILQ_FIRST(&g_scan_dup_list)) {
        TAILQ_REMOVE(&g_scan_dup_list, e, link);
        TAILQ_INSERT_HEAD(&g_scan_dup_list, e, link);
    }
}

static inline struct ble_ll_scan_dup_entry *
ble_ll_scan_dup_new(void)
{
    struct ble_ll_scan_dup_entry *e;

    e = os_memblock_get(&g_scan_dup_pool);
    if (!e) {
        e = TAILQ_LAST(&g_scan_dup_list, ble_ll_scan_dup_list);
        TAILQ_REMOVE(&g_scan_dup_list, e, link);
    }

    memset(e, 0, sizeof(*e));

    return e;
}

static int
ble_ll_scan_dup_check_legacy(uint8_t addr_type, uint8_t *addr, uint8_t pdu_type)
{
    struct ble_ll_scan_dup_entry *e;
    uint8_t type;
    int rc;

    type = BLE_LL_SCAN_ENTRY_TYPE_LEGACY(addr_type);

    TAILQ_FOREACH(e, &g_scan_dup_list, link) {
        if ((e->type == type) && !memcmp(e->addr, addr, 6)) {
            break;
        }
    }

    if (e) {
        if (pdu_type == BLE_ADV_PDU_TYPE_ADV_DIRECT_IND) {
            rc = e->flags & BLE_LL_SCAN_DUP_F_DIR_ADV_REPORT_SENT;
        } else if (pdu_type == BLE_ADV_PDU_TYPE_SCAN_RSP) {
            rc = e->flags & BLE_LL_SCAN_DUP_F_SCAN_RSP_SENT;
        } else {
            rc = e->flags & BLE_LL_SCAN_DUP_F_ADV_REPORT_SENT;
        }

        ble_ll_scan_dup_move_to_head(e);
    } else {
        rc = 0;

        e = ble_ll_scan_dup_new();
        e->flags = 0;
        e->type = type;
        memcpy(e->addr, addr, 6);

        TAILQ_INSERT_HEAD(&g_scan_dup_list, e, link);
    }

    return rc;
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
static int
ble_ll_scan_dup_check_ext(uint8_t addr_type, uint8_t *addr,
                          struct ble_ll_aux_data *aux_data)
{
    struct ble_ll_scan_dup_entry *e;
    bool has_aux;
    bool is_anon;
    uint16_t adi;
    uint8_t type;
    int rc;

    has_aux = aux_data != NULL;
    is_anon = addr == NULL;
    adi = has_aux ? aux_data->adi : 0;

    type = BLE_LL_SCAN_ENTRY_TYPE_EXT(addr_type, has_aux, is_anon, adi);

    TAILQ_FOREACH(e, &g_scan_dup_list, link) {
        if ((e->type == type) &&
            (is_anon || !memcmp(e->addr, addr, BLE_DEV_ADDR_LEN))) {
            break;
        }
    }

    if (e) {
        if (e->adi != adi) {
            rc = 0;

            e->flags = 0;
            e->adi = adi;
        } else {
            rc = e->flags & BLE_LL_SCAN_DUP_F_ADV_REPORT_SENT;
        }

        ble_ll_scan_dup_move_to_head(e);
    } else {
        rc = 0;

        e = ble_ll_scan_dup_new();
        e->flags = 0;
        e->type = type;
        e->adi = adi;
        if (!is_anon) {
            memcpy(e->addr, addr, 6);
        }

        TAILQ_INSERT_HEAD(&g_scan_dup_list, e, link);
    }

    return rc;
}

static int
ble_ll_scan_dup_update_ext(uint8_t addr_type, uint8_t *addr,
                           struct ble_ll_aux_data *aux_data)
{
    struct ble_ll_scan_dup_entry *e;
    bool has_aux;
    bool is_anon;
    uint16_t adi;
    uint8_t type;

    has_aux = aux_data != NULL;
    is_anon = addr == NULL;
    adi = has_aux ? aux_data->adi : 0;

    type = BLE_LL_SCAN_ENTRY_TYPE_EXT(addr_type, has_aux, is_anon, adi);

    /*
     * We assume ble_ll_scan_dup_check() was called before which either matched
     * some entry or allocated new one and placed in on the top of queue.
     */

    e = TAILQ_FIRST(&g_scan_dup_list);
    BLE_LL_ASSERT(e && e->type == type && (is_anon || !memcmp(e->addr, addr, 6)));

    e->flags |= BLE_LL_SCAN_DUP_F_ADV_REPORT_SENT;

    return 0;
}
#endif

/**
 * Process a received PDU while in the scanning state.
 *
 * Context: Link Layer task.
 *
 * @param pdu_type
 * @param rxbuf
 */
void
ble_ll_scan_rx_pkt_in(uint8_t ptype, struct os_mbuf *om, struct ble_mbuf_hdr *hdr)
{
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    int index;
    struct ble_ll_resolv_entry *rl;
#endif
    uint8_t *rxbuf = om->om_data;
    uint8_t *adv_addr = NULL;
    uint8_t *adva;
    uint8_t *ident_addr;
    uint8_t ident_addr_type;
    uint8_t *init_addr = NULL;
    uint8_t init_addr_type = 0;
    uint8_t txadd = 0;
    uint8_t rxadd;
    uint8_t scan_rsp_rxd;
    int backoff_success = 0;
    struct ble_ll_scan_sm *scansm = &g_ble_ll_scan_sm;
    int ext_adv_mode = -1;
    bool is_addr_decoded = false;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    struct ble_ll_aux_data *aux_data = hdr->rxinfo.user_data;
    bool aux_error = false;
    int rc;
#endif

    /* if packet is scan response */
    scan_rsp_rxd = BLE_MBUF_HDR_SCAN_RSP_RXD(hdr);

    if (!BLE_MBUF_HDR_CRC_OK(hdr)) {
        goto scan_continue;
    }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (aux_data) {
        aux_data->flags_ll |= aux_data->flags_isr;
    }

    /* Drop on scan error or if we received not what we expected to receive */
    if (BLE_MBUF_HDR_AUX_INVALID(hdr) ||
        (aux_data && (aux_data->flags_ll & BLE_LL_AUX_FLAG_SCAN_ERROR)) ||
        (aux_data && (ptype != BLE_ADV_PDU_TYPE_ADV_EXT_IND))) {
        aux_error = true;
        goto scan_continue;
    }
#endif

    /* We dont care about scan requests or connect requests */
    if ((ptype == BLE_ADV_PDU_TYPE_SCAN_REQ) || (ptype == BLE_ADV_PDU_TYPE_CONNECT_IND)) {
        goto scan_continue;
    }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    /*
     * For extended advertising events all data are decoded to aux_data and can
     * be retrieved from there. For legacy advertising we need to get addresses
     * from PDU.
     */
    if (aux_data) {
        ext_adv_mode = rxbuf[2] >> 6;

        if (aux_data->flags & BLE_LL_AUX_HAS_ADDRA) {
            adv_addr = aux_data->addr;
            txadd = aux_data->addr_type;
        }
        if (aux_data->flags & BLE_LL_AUX_HAS_DIR_ADDRA) {
            init_addr = aux_data->dir_addr;
            init_addr_type = aux_data->dir_addr_type;
        }

        is_addr_decoded = true;
    }
#endif

    if (!is_addr_decoded) {
        if (ble_ll_scan_adv_decode_addr(ptype, rxbuf, hdr, &adv_addr, &txadd,
                                        &init_addr, &init_addr_type,
                                        &ext_adv_mode)) {
            goto scan_continue;
        }
        is_addr_decoded = true;
    }

    ident_addr = adv_addr;
    ident_addr_type = txadd;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    index = hdr->rxinfo.rpa_index;
    if (index >= 0) {
        ident_addr = g_ble_ll_resolv_list[index].rl_identity_addr;
        ident_addr_type = g_ble_ll_resolv_list[index].rl_addr_type;

        if (ble_ll_is_rpa(init_addr, init_addr_type)) {
           /* Let's try resolve InitA. */
           if (ble_ll_resolv_rpa(init_addr, g_ble_ll_resolv_list[index].rl_local_irk)) {
               init_addr = ble_ll_get_our_devaddr(scansm->own_addr_type & 1);
               init_addr_type = scansm->own_addr_type & 1;
           }
       }
    } else if (init_addr && ble_ll_resolv_enabled() && ble_ll_is_rpa(init_addr, init_addr_type)) {
        /* If we are here it means AdvA is identity. Check if initA is RPA */
        rl = ble_ll_resolv_list_find(ident_addr, ident_addr_type);
        if (rl && ble_ll_resolv_rpa(init_addr, rl->rl_local_irk)) {
            init_addr = ble_ll_get_our_devaddr(scansm->own_addr_type & 1);
            init_addr_type = scansm->own_addr_type & 1;
        }
    }
#endif

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PERIODIC_ADV)
    /* periodic scan is using own filter list so we need to bypass scan filter
     * policy if PDU and adv type match and advertiser address is present
     */
    if ((ptype == BLE_ADV_PDU_TYPE_ADV_EXT_IND) &&
            (ext_adv_mode == BLE_LL_EXT_ADV_MODE_NON_CONN) && adv_addr) {
        check_periodic_sync(om, hdr, adv_addr, txadd, index);
    }
#endif

    /* Check the scanner filter policy */
    if (ble_ll_scan_chk_filter_policy(ptype, adv_addr, txadd, init_addr,
                                      init_addr_type,
                                      BLE_MBUF_HDR_DEVMATCH(hdr))) {
        goto scan_continue;
    }

    if (ptype == BLE_ADV_PDU_TYPE_SCAN_RSP) {
        rxadd = scansm->pdu_data.hdr_byte & BLE_ADV_PDU_HDR_RXADD_MASK;
        adva = scansm->pdu_data.adva;
        if (scan_rsp_rxd && ((txadd && rxadd) || ((txadd + rxadd) == 0)) &&
            !memcmp(adv_addr, adva, BLE_DEV_ADDR_LEN)) {
            /* We have received a scan response. Add to list */
            ble_ll_scan_add_scan_rsp_adv(ident_addr, ident_addr_type, 0, 0);
            backoff_success = 1;
        } else {
            /* This is not addressed for us, ignore */
            goto scan_continue;
        }
    }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (ptype == BLE_ADV_PDU_TYPE_ADV_EXT_IND) {
        if (!scansm->ext_scanning) {
            goto scan_continue;
        }

        /* Let's see if that packet contains aux ptr*/
        if (BLE_MBUF_HDR_WAIT_AUX(hdr)) {
            BLE_LL_ASSERT(aux_data);

            if (ble_ll_sched_aux_scan(hdr, scansm, aux_data)) {
                hdr->rxinfo.flags &= ~BLE_MBUF_HDR_F_AUX_PTR_WAIT;

                aux_data->flags_ll |= BLE_LL_AUX_FLAG_SCAN_ERROR;

                /* Just ignore if no HCI event was sent to host */
                if (!(aux_data->flags_ll & BLE_LL_AUX_FLAG_HCI_SENT_ANY)) {
                    goto scan_continue;
                }
            }

            /* Ignore if this was just ADV_EXT_IND with AuxPtr, will process AUX */
            if (!(aux_data->flags_ll & BLE_LL_AUX_FLAG_AUX_RECEIVED)) {
                goto scan_continue;
            }

            STATS_INC(ble_ll_stats, aux_chain_cnt);
        }

        /* Filter duplicates */
        if (!scansm->scan_filt_dups ||
            !ble_ll_scan_dup_check_ext(ident_addr_type, ident_addr, aux_data)) {
            rc = ble_ll_hci_send_ext_adv_report(ptype, ident_addr, ident_addr_type,
                                                init_addr, init_addr_type, om, hdr);
            if ((rc < 0) && BLE_MBUF_HDR_WAIT_AUX(hdr)) {
                /* Data were truncated, stop scanning for subsequent chains */
                hdr->rxinfo.flags &= ~BLE_MBUF_HDR_F_AUX_PTR_WAIT;

                if (ble_ll_sched_rmv_elem(&aux_data->sch) == 0) {
                    ble_ll_scan_aux_data_unref(aux_data->sch.cb_arg);
                    aux_data->sch.cb_arg = NULL;
                }

                aux_data->flags_ll |= BLE_LL_AUX_FLAG_SCAN_ERROR;
                aux_error = true;
            } else if ((rc == 0) && scansm->scan_filt_dups) {
                /* Complete data were send, now we can update scan_dup list */
                ble_ll_scan_dup_update_ext(ident_addr_type, ident_addr, aux_data);
            }
        }

        if (BLE_MBUF_HDR_SCAN_REQ_TXD(hdr)) {
            /* We are here because we sent SCAN_REQ and wait for SCAN_RSP. */
            ble_ll_scan_aux_data_unref(hdr->rxinfo.user_data);
            hdr->rxinfo.user_data = NULL;
            return;
        }

        if (scan_rsp_rxd) {
            /* XXX: For now let us consider scan response as succeed in the
             * backoff context, after first scan response packet is received.
             * I guess we should marked it succeed after complete scan response
             * is received, and failed when truncated, but then we need to
             * analyze reason of truncation as it also might be issue of the
             * resources on our side
             */
            backoff_success = 1;
        }

        goto scan_continue;
    }
#endif

    /* Filter duplicates */
    if (!scansm->scan_filt_dups ||
        !ble_ll_scan_dup_check_legacy(ident_addr_type, ident_addr, ptype)) {
        /* Send the advertising report, also updates scan duplicates list */
        ble_ll_scan_send_adv_report(ptype, ident_addr, ident_addr_type,
                                    init_addr, init_addr_type, om, hdr, scansm);
    }

scan_continue:

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (aux_data) {
        if (aux_error || !BLE_MBUF_HDR_CRC_OK(hdr)) {
            ble_ll_scan_end_adv_evt(aux_data);
        }

        ble_ll_scan_aux_data_unref(hdr->rxinfo.user_data);
        hdr->rxinfo.user_data = NULL;
    }
#endif
    /* if we got scan response update backoff */
    if (scan_rsp_rxd) {
        ble_ll_scan_req_backoff(scansm, backoff_success);
    }

    ble_ll_scan_chk_resume();
}

int
ble_ll_scan_set_scan_params(const uint8_t *cmdbuf, uint8_t len)
{
    const struct ble_hci_le_set_scan_params_cp *cmd = (const void *)cmdbuf;
    uint16_t scan_itvl;
    uint16_t scan_window;
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_scan_params *scanp;

    if (len != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* If already enabled, we return an error */
    scansm = &g_ble_ll_scan_sm;
    if (scansm->scan_enabled) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* Get the scan interval and window */
    scan_itvl = le16toh(cmd->scan_itvl);
    scan_window = le16toh(cmd->scan_window);

    /* Check scan type */
    if ((cmd->scan_type != BLE_HCI_SCAN_TYPE_PASSIVE) &&
        (cmd->scan_type != BLE_HCI_SCAN_TYPE_ACTIVE)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check interval and window */
    if ((scan_itvl < BLE_HCI_SCAN_ITVL_MIN) ||
        (scan_itvl > BLE_HCI_SCAN_ITVL_MAX) ||
        (scan_window < BLE_HCI_SCAN_WINDOW_MIN) ||
        (scan_window > BLE_HCI_SCAN_WINDOW_MAX) ||
        (scan_itvl < scan_window)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check own addr type */
    if (cmd->own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check scanner filter policy */
    if (cmd->filter_policy > BLE_HCI_SCAN_FILT_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Store scan parameters */
    scanp = &g_ble_ll_scan_params[PHY_UNCODED];
    scanp->configured = 1;
    scanp->scan_type = cmd->scan_type;
    scanp->scan_itvl = scan_itvl;
    scanp->scan_window = scan_window;
    scanp->scan_filt_policy = cmd->filter_policy;
    scanp->own_addr_type = cmd->own_addr_type;

#if (BLE_LL_SCAN_PHY_NUMBER == 2)
    g_ble_ll_scan_params[PHY_CODED].configured = 0;
#endif

    return 0;
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
static int
ble_ll_check_scan_params(uint8_t type, uint16_t itvl, uint16_t window)
{
    /* Check scan type */
    if ((type != BLE_HCI_SCAN_TYPE_PASSIVE) &&
        (type != BLE_HCI_SCAN_TYPE_ACTIVE)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check interval and window */
    if ((itvl < BLE_HCI_SCAN_ITVL_MIN) ||
        (itvl > BLE_HCI_SCAN_ITVL_MAX) ||
        (window < BLE_HCI_SCAN_WINDOW_MIN) ||
        (window > BLE_HCI_SCAN_WINDOW_MAX) ||
        (itvl < window)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    return 0;
}

int
ble_ll_set_ext_scan_params(const uint8_t *cmdbuf, uint8_t len)
{
    const struct ble_hci_le_set_ext_scan_params_cp *cmd = (const void *) cmdbuf;
    const struct scan_params *params = cmd->scans;

    struct ble_ll_scan_params new_params[BLE_LL_SCAN_PHY_NUMBER] = { };
    struct ble_ll_scan_params *uncoded = &new_params[PHY_UNCODED];
    struct ble_ll_scan_params *coded = &new_params[PHY_CODED];
    int rc;

    if (len <= sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    len -= sizeof(*cmd);

    /* If already enabled, we return an error */
    if (g_ble_ll_scan_sm.scan_enabled) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* Check own addr type */
    if (cmd->own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    coded->own_addr_type = cmd->own_addr_type;
    uncoded->own_addr_type = cmd->own_addr_type;

    /* Check scanner filter policy */
    if (cmd->filter_policy > BLE_HCI_SCAN_FILT_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    coded->scan_filt_policy = cmd->filter_policy;
    uncoded->scan_filt_policy = cmd->filter_policy;

    /* Check if no reserved bits in PHYS are set and that at least one valid PHY
     * is set.
     */
    if (!(cmd->phys & SCAN_VALID_PHY_MASK) ||
        (cmd->phys & ~SCAN_VALID_PHY_MASK)) {
         return BLE_ERR_INV_HCI_CMD_PARMS;
     }

    if (cmd->phys & BLE_HCI_LE_PHY_1M_PREF_MASK) {
        if (len < sizeof(*params)) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        uncoded->scan_type = params->type;
        uncoded->scan_itvl = le16toh(params->itvl);
        uncoded->scan_window = le16toh(params->window);

        rc = ble_ll_check_scan_params(uncoded->scan_type,
                                      uncoded->scan_itvl,
                                      uncoded->scan_window);
        if (rc) {
            return rc;
        }

        /* That means user wants to use this PHY for scanning */
        uncoded->configured = 1;
        params++;
        len -= sizeof(*params);
    }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_CODED_PHY)
    if (cmd->phys & BLE_HCI_LE_PHY_CODED_PREF_MASK) {
        if (len < sizeof(*params)) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        coded->scan_type = params->type;
        coded->scan_itvl = le16toh(params->itvl);
        coded->scan_window = le16toh(params->window);

        rc = ble_ll_check_scan_params(coded->scan_type,
                                      coded->scan_itvl,
                                      coded->scan_window);
        if (rc) {
            return rc;
        }

        /* That means user wants to use this PHY for scanning */
        coded->configured = 1;
    }
#endif

    /* if any of PHYs is configured for continuous scan we alter interval to
     * fit other PHY
     */
    if (coded->configured && uncoded->configured) {
        if (coded->scan_itvl == coded->scan_window) {
            coded->scan_itvl += uncoded->scan_window;
        }

        if (uncoded->scan_itvl == uncoded->scan_window) {
            uncoded->scan_itvl += coded->scan_window;
        }
    }

    memcpy(g_ble_ll_scan_params, new_params, sizeof(new_params));

    return 0;
}

#endif

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
static void
ble_ll_scan_duration_period_timers_restart(struct ble_ll_scan_sm *scansm)
{
    uint32_t now;

    now = os_cputime_get32();

    os_cputime_timer_stop(&scansm->duration_timer);
    os_cputime_timer_stop(&scansm->period_timer);

    if (scansm->duration_ticks) {
        os_cputime_timer_start(&scansm->duration_timer,
                                                now + scansm->duration_ticks);

        if (scansm->period_ticks) {
            os_cputime_timer_start(&scansm->period_timer,
                                                    now + scansm->period_ticks);
        }
    }
}

static void
ble_ll_scan_duration_timer_cb(void *arg)
{
    struct ble_ll_scan_sm *scansm;

    scansm = (struct ble_ll_scan_sm *)arg;

    ble_ll_scan_sm_stop(2);

    /* if period is set both timers get started from period cb */
    if (!scansm->period_ticks) {
        ble_ll_hci_ev_send_scan_timeout();
    }
}

static void
ble_ll_scan_period_timer_cb(void *arg)
{
    struct ble_ll_scan_sm *scansm = arg;

    ble_ll_scan_sm_start(scansm);

    /* always start timer regardless of ble_ll_scan_sm_start result
     * if it failed will restart in next period
     */
    ble_ll_scan_duration_period_timers_restart(scansm);
}
#endif

/**
 * ble ll scan set enable
 *
 * HCI scan set enable command processing function
 *
 *  Context: Link Layer task (HCI Command parser).
 *
 * @return int BLE error code.
 */
static int
ble_ll_scan_set_enable(uint8_t enable, uint8_t filter_dups, uint16_t period,
                       uint16_t dur, bool ext)
{
    int rc;
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_scan_params *scanp;
    struct ble_ll_scan_params *scanphy;
    int i;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    uint32_t period_ticks = 0;
    uint32_t dur_ticks = 0;
#endif

    /* Check for valid parameters */
    if ((filter_dups > 1) || (enable > 1)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    scansm = &g_ble_ll_scan_sm;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    /* we can do that here since value will never change until reset */
    scansm->ext_scanning = ext;

    if (ext) {
        /* Period parameter is ignored when the Duration parameter is zero */
        if (!dur) {
            period = 0;
        }

        /* period is in 1.28 sec units
         * TODO support full range, would require os_cputime milliseconds API
         */
        if (period > 3355) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }
        period_ticks = os_cputime_usecs_to_ticks(period * 1280000);

        /* duration is in 10ms units */
        dur_ticks = os_cputime_usecs_to_ticks(dur * 10000);

        if (dur_ticks && period_ticks && (dur_ticks >= period_ticks)) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }
    }
#endif

    /* disable*/
    if (!enable) {
        if (scansm->scan_enabled) {
            ble_ll_scan_sm_stop(1);
        }
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        os_cputime_timer_stop(&scansm->duration_timer);
        os_cputime_timer_stop(&scansm->period_timer);
#endif

        return BLE_ERR_SUCCESS;
    }

    /* if already enable we just need to update parameters */
    if (scansm->scan_enabled) {
        /* Controller does not allow initiating and scanning.*/
        for (i = 0; i < BLE_LL_SCAN_PHY_NUMBER; i++) {
            scanphy = &scansm->phy_data[i];
            if (scanphy->configured &&
                                scanphy->scan_type == BLE_SCAN_TYPE_INITIATE) {
                return BLE_ERR_CMD_DISALLOWED;
            }
        }

#if MYNEWT_VAL(BLE_LL_NUM_SCAN_DUP_ADVS)
        /* update filter policy */
        scansm->scan_filt_dups = filter_dups;
#endif

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        /* restart timers according to new settings */
        scansm->duration_ticks = dur_ticks;
        scansm->period_ticks = period_ticks;
        ble_ll_scan_duration_period_timers_restart(scansm);
#endif

        return BLE_ERR_SUCCESS;
    }

    /* we can store those upfront regardless of start scan result since scan is
     * disabled now
     */

#if MYNEWT_VAL(BLE_LL_NUM_SCAN_DUP_ADVS)
    scansm->scan_filt_dups = filter_dups;
#endif
    scansm->cur_phy = PHY_NOT_CONFIGURED;
    scansm->next_phy = PHY_NOT_CONFIGURED;

    for (i = 0; i < BLE_LL_SCAN_PHY_NUMBER; i++) {
        scanphy = &scansm->phy_data[i];
        scanp = &g_ble_ll_scan_params[i];

        if (!scanp->configured) {
            continue;
        }

        scanphy->configured = scanp->configured;
        scanphy->scan_type = scanp->scan_type;
        scanphy->scan_itvl = scanp->scan_itvl;
        scanphy->scan_window = scanp->scan_window;
        scanphy->scan_filt_policy = scanp->scan_filt_policy;
        scanphy->own_addr_type = scanp->own_addr_type;

        if (scansm->cur_phy == PHY_NOT_CONFIGURED) {
            scansm->cur_phy = i;
            /* Take own_addr_type from the first configured PHY.
             * Note: All configured PHYs shall have the same own_addr_type
             */
            scansm->own_addr_type = scanphy->own_addr_type;
        } else {
            scansm->next_phy = i;
        }
    }

    /* spec is not really clear if we should use defaults in this case
     * or just disallow starting scan without explicit configuration
     * For now be nice to host and just use values based on LE Set Scan
     * Parameters defaults.
     */
    if (scansm->cur_phy == PHY_NOT_CONFIGURED) {
        scansm->cur_phy = PHY_UNCODED;
        scansm->own_addr_type = BLE_ADDR_PUBLIC;

        scanphy = &scansm->phy_data[PHY_UNCODED];

        scanphy->configured = 1;
        scanphy->scan_type = BLE_SCAN_TYPE_PASSIVE;
        scanphy->scan_itvl = 0x0010;
        scanphy->scan_window = 0x0010;
        scanphy->scan_filt_policy = BLE_HCI_SCAN_FILT_NO_WL;
        scanphy->own_addr_type = BLE_ADDR_PUBLIC;
    }

    rc = ble_ll_scan_sm_start(scansm);

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (rc == BLE_ERR_SUCCESS) {
        scansm->duration_ticks = dur_ticks;
        scansm->period_ticks = period_ticks;
        ble_ll_scan_duration_period_timers_restart(scansm);
    }
#endif

    return rc;
}

int ble_ll_hci_scan_set_enable(const uint8_t *cmdbuf, uint8_t len)
{
    const struct ble_hci_le_set_scan_enable_cp *cmd = (const void *) cmdbuf;

    if (len != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    return ble_ll_scan_set_enable(cmd->enable, cmd->filter_duplicates, 0, 0,
                                  false);
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
int ble_ll_hci_ext_scan_set_enable(const uint8_t *cmdbuf, uint8_t len)
{
    const struct ble_hci_le_set_ext_scan_enable_cp *cmd = (const void *) cmdbuf;

    if (len != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    return ble_ll_scan_set_enable(cmd->enable, cmd->filter_dup,
                                  le16toh(cmd->period), le16toh(cmd->duration),
                                  true);
}
#endif

/**
 * Checks if controller can change the whitelist. If scanning is enabled and
 * using the whitelist the controller is not allowed to change the whitelist.
 *
 * @return int 0: not allowed to change whitelist; 1: change allowed.
 */
int
ble_ll_scan_can_chg_whitelist(void)
{
    int rc;
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_scan_params *params;

    scansm = &g_ble_ll_scan_sm;
    params = &scansm->phy_data[scansm->cur_phy];
    if (scansm->scan_enabled && (params->scan_filt_policy & 1)) {
        rc = 0;
    } else {
        rc = 1;
    }

    return rc;
}

int
ble_ll_scan_initiator_start(struct hci_create_conn *hcc,
                            struct ble_ll_scan_sm **sm)
{
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_scan_params *scanphy;
    int rc;

    scansm = &g_ble_ll_scan_sm;
    scansm->own_addr_type = hcc->own_addr_type;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    scansm->ext_scanning = 0;
#endif
    scansm->cur_phy = PHY_UNCODED;
    scansm->next_phy = PHY_NOT_CONFIGURED;

    scanphy = &scansm->phy_data[scansm->cur_phy];
    scanphy->scan_filt_policy = hcc->filter_policy;
    scanphy->scan_itvl = hcc->scan_itvl;
    scanphy->scan_window = hcc->scan_window;
    scanphy->scan_type = BLE_SCAN_TYPE_INITIATE;

    rc = ble_ll_scan_sm_start(scansm);
    if (sm == NULL) {
        return rc;
    }

    if (rc == BLE_ERR_SUCCESS) {
        *sm = scansm;
    } else {
        *sm = NULL;
    }

    return rc;
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
int
ble_ll_scan_ext_initiator_start(struct hci_ext_create_conn *hcc,
                                struct ble_ll_scan_sm **sm)
{
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_scan_params *uncoded;
    struct ble_ll_scan_params *coded;
    struct hci_ext_conn_params *params;
    int rc;

    scansm = &g_ble_ll_scan_sm;
    scansm->own_addr_type = hcc->own_addr_type;
    scansm->cur_phy = PHY_NOT_CONFIGURED;
    scansm->next_phy = PHY_NOT_CONFIGURED;
    scansm->ext_scanning = 1;

    if (hcc->init_phy_mask & BLE_PHY_MASK_1M) {
        params = &hcc->params[0];
        uncoded = &scansm->phy_data[PHY_UNCODED];

        uncoded->scan_itvl = params->scan_itvl;
        uncoded->scan_window = params->scan_window;
        uncoded->scan_type = BLE_SCAN_TYPE_INITIATE;
        uncoded->scan_filt_policy = hcc->filter_policy;
        scansm->cur_phy = PHY_UNCODED;
    }

    if (hcc->init_phy_mask & BLE_PHY_MASK_CODED) {
        params = &hcc->params[2];
        coded = &scansm->phy_data[PHY_CODED];

        coded->scan_itvl = params->scan_itvl;
        coded->scan_window = params->scan_window;
        coded->scan_type = BLE_SCAN_TYPE_INITIATE;
        coded->scan_filt_policy = hcc->filter_policy;
        if (scansm->cur_phy == PHY_NOT_CONFIGURED) {
            scansm->cur_phy = PHY_CODED;
        } else {
            scansm->next_phy = PHY_CODED;
        }
    }

    /* if any of PHYs is configured for continuous scan we alter interval to
     * fit other PHY
     */
    if (coded->configured && uncoded->configured) {
        if (coded->scan_itvl == coded->scan_window) {
            coded->scan_itvl += uncoded->scan_window;
        }

        if (uncoded->scan_itvl == uncoded->scan_window) {
            uncoded->scan_itvl += coded->scan_window;
        }
    }

    rc = ble_ll_scan_sm_start(scansm);
    if (sm == NULL) {
        return rc;
    }

    if (rc == BLE_ERR_SUCCESS) {
        *sm = scansm;
    } else {
        *sm = NULL;
    }

    return rc;
}
#endif

/**
 * Checks to see if the scanner is enabled.
 *
 * @return int 0: not enabled; enabled otherwise
 */
int
ble_ll_scan_enabled(void)
{
    return (int)g_ble_ll_scan_sm.scan_enabled;
}

/**
 * Returns the peer resolvable private address of last device connecting to us
 *
 * @return uint8_t*
 */
uint8_t *
ble_ll_scan_get_peer_rpa(void)
{
    struct ble_ll_scan_sm *scansm;

    /* XXX: should this go into IRK list or connection? */
    scansm = &g_ble_ll_scan_sm;
    return scansm->scan_peer_rpa;
}

/**
 * Returns the local resolvable private address currently being using by
 * the scanner/initiator
 *
 * @return uint8_t*
 */
uint8_t *
ble_ll_scan_get_local_rpa(void)
{
    return g_ble_ll_scan_sm.pdu_data.scana;
}

/**
 * Set the Resolvable Private Address in the scanning (or initiating) state
 * machine.
 *
 * XXX: should this go into IRK list or connection?
 *
 * @param rpa
 */
void
ble_ll_scan_set_peer_rpa(uint8_t *rpa)
{
    struct ble_ll_scan_sm *scansm;

    scansm = &g_ble_ll_scan_sm;
    memcpy(scansm->scan_peer_rpa, rpa, BLE_DEV_ADDR_LEN);
}

struct ble_ll_scan_pdu_data *
ble_ll_scan_get_pdu_data(void)
{
    return &g_ble_ll_scan_sm.pdu_data;
}

/* Returns true if whitelist is enabled for scanning */
int
ble_ll_scan_whitelist_enabled(void)
{
    struct ble_ll_scan_params *params;

    params = &g_ble_ll_scan_sm.phy_data[g_ble_ll_scan_sm.cur_phy];
    return params->scan_filt_policy & 1;
}

static void
ble_ll_scan_common_init(void)
{
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_scan_params *scanp;
    int i;

    /* Clear state machine in case re-initialized */
    scansm = &g_ble_ll_scan_sm;
    memset(scansm, 0, sizeof(struct ble_ll_scan_sm));

    /* Clear scan parameters in case re-initialized */
    memset(g_ble_ll_scan_params, 0, sizeof(g_ble_ll_scan_params));

    /* Initialize scanning window end event */
    ble_npl_event_init(&scansm->scan_sched_ev, ble_ll_scan_event_proc, scansm);

    for (i = 0; i < BLE_LL_SCAN_PHY_NUMBER; i++) {
        /* Set all non-zero default parameters */
        scanp = &g_ble_ll_scan_params[i];
        scanp->scan_itvl = BLE_HCI_SCAN_ITVL_DEF;
        scanp->scan_window = BLE_HCI_SCAN_WINDOW_DEF;
    }

    scansm->phy_data[PHY_UNCODED].phy = BLE_PHY_1M;
#if (BLE_LL_SCAN_PHY_NUMBER == 2)
    scansm->phy_data[PHY_CODED].phy = BLE_PHY_CODED;
#endif

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    /* Make sure we'll generate new NRPA if necessary */
    scansm->scan_nrpa_timer = ble_npl_time_get();
#endif

    /* Initialize scanning timer */
    os_cputime_timer_init(&scansm->scan_timer, ble_ll_scan_timer_cb, scansm);

    /* Initialize extended scan timers */
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    os_cputime_timer_init(&scansm->duration_timer,
                                        ble_ll_scan_duration_timer_cb, scansm);
    os_cputime_timer_init(&scansm->period_timer, ble_ll_scan_period_timer_cb,
                                                                        scansm);
#endif

    ble_npl_event_init(&scansm->scan_interrupted_ev, ble_ll_scan_interrupted_event_cb, NULL);
}

/**
 * Called when the controller receives the reset command. Resets the
 * scanning state machine to its initial state.
 *
 * @return int
 */
void
ble_ll_scan_reset(void)
{
    struct ble_ll_scan_sm *scansm;

    scansm = &g_ble_ll_scan_sm;

    /* If enabled, stop it. */
    if (scansm->scan_enabled) {
        ble_ll_scan_sm_stop(0);
    }

    /* stop extended scan timers */
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    os_cputime_timer_stop(&scansm->duration_timer);
    os_cputime_timer_stop(&scansm->period_timer);
#endif

    /* Reset duplicate advertisers and those from which we rxd a response */
    g_ble_ll_scan_num_rsp_advs = 0;
    memset(&g_ble_ll_scan_rsp_advs[0], 0, sizeof(g_ble_ll_scan_rsp_advs));

    os_mempool_clear(&g_scan_dup_pool);
    TAILQ_INIT(&g_scan_dup_list);

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    /* clear memory pool for AUX scan results */
    os_mempool_clear(&ext_scan_aux_pool);
#endif

    /* Call the common init function again */
    ble_ll_scan_common_init();
}

/**
 * ble ll scan init
 *
 * Initialize a scanner. Must be called before scanning can be started.
 * Expected to be called with a un-initialized scanning state machine.
 */
void
ble_ll_scan_init(void)
{
    os_error_t err;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    err = os_mempool_init(&ext_scan_aux_pool,
                          MYNEWT_VAL(BLE_LL_EXT_ADV_AUX_PTR_CNT),
                          sizeof (struct ble_ll_aux_data),
                          ext_scan_aux_mem,
                          "ble_ll_aux_scan_pool");
    BLE_LL_ASSERT(err == 0);
#endif

    err = os_mempool_init(&g_scan_dup_pool,
                          MYNEWT_VAL(BLE_LL_NUM_SCAN_DUP_ADVS),
                          sizeof(struct ble_ll_scan_dup_entry),
                          g_scan_dup_mem,
                          "ble_ll_scan_dup_pool");
    BLE_LL_ASSERT(err == 0);

    TAILQ_INIT(&g_scan_dup_list);

    ble_ll_scan_common_init();
}
