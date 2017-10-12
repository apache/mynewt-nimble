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
#include <string.h>
#include <assert.h>
#include "syscfg/syscfg.h"
#include "bsp/bsp.h"
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
#include "ble_ll_conn_priv.h"
#include "hal/hal_gpio.h"

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
#if MYNEWT_VAL(BLE_LL_NUM_SCAN_DUP_ADVS) > 255
    #error "Cannot have more than 255 duplicate entries!"
#endif
#if MYNEWT_VAL(BLE_LL_NUM_SCAN_RSP_ADVS) > 255
    #error "Cannot have more than 255 scan response entries!"
#endif

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
static const uint8_t ble_ll_valid_scan_phy_mask = (BLE_HCI_LE_PHY_1M_PREF_MASK
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_CODED_PHY)
                                | BLE_HCI_LE_PHY_CODED_PREF_MASK
#endif
                              );
#endif

/* The scanning parameters set by host */
struct ble_ll_scan_params g_ble_ll_scan_params[BLE_LL_SCAN_PHY_NUMBER];

/* The scanning state machine global object */
struct ble_ll_scan_sm g_ble_ll_scan_sm;

#define BLE_LL_EXT_ADV_ADVA_BIT         (0)
#define BLE_LL_EXT_ADV_TARGETA_BIT      (1)
#define BLE_LL_EXT_ADV_RFU_BIT          (2)
#define BLE_LL_EXT_ADV_DATA_INFO_BIT    (3)
#define BLE_LL_EXT_ADV_AUX_PTR_BIT      (4)
#define BLE_LL_EXT_ADV_SYNC_INFO_BIT    (5)
#define BLE_LL_EXT_ADV_TX_POWER_BIT     (6)

#define BLE_LL_EXT_ADV_ADVA_SIZE        (6)
#define BLE_LL_EXT_ADV_TARGETA_SIZE     (6)
#define BLE_LL_EXT_ADV_DATA_INFO_SIZE   (2)
#define BLE_LL_EXT_ADV_AUX_PTR_SIZE     (3)
#define BLE_LL_EXT_ADV_SYNC_INFO_SIZE   (18)
#define BLE_LL_EXT_ADV_TX_POWER_SIZE    (1)

struct ble_ll_ext_adv_hdr
{
    uint8_t mode;
    uint8_t hdr_len;
    uint8_t hdr[0];
};

struct ble_ll_ext_adv {
    /* We support one report per event for now */
    uint8_t event_meta; /* BLE_HCI_EVCODE_LE_META */
    uint8_t event_len;
    uint8_t subevt;
    uint8_t num_reports;

    uint16_t evt_type;
    uint8_t addr_type;
    uint8_t addr[6];
    uint8_t prim_phy;
    uint8_t sec_phy;
    uint8_t sid;
    uint8_t tx_power;
    int8_t rssi;
    uint16_t per_adv_itvl;
    uint8_t dir_addr_type;
    uint8_t dir_addr[6];
    uint8_t adv_data_len;
    uint8_t adv_data[0];
} __attribute__((packed));

/*
 * Structure used to store advertisers. This is used to limit sending scan
 * requests to the same advertiser and also to filter duplicate events sent
 * to the host.
 */
struct ble_ll_scan_advertisers
{
    uint16_t            sc_adv_flags;
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

/* Used to filter duplicate advertising events to host */
static uint8_t g_ble_ll_scan_num_dup_advs;
struct ble_ll_scan_advertisers
g_ble_ll_scan_dup_advs[MYNEWT_VAL(BLE_LL_NUM_SCAN_DUP_ADVS)];

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
static os_membuf_t ext_adv_mem[ OS_MEMPOOL_SIZE(
                    MYNEWT_VAL(BLE_LL_EXT_ADV_AUX_PTR_CNT),
                    sizeof (struct ble_ll_aux_data))
];

static struct os_mempool ext_adv_pool;

static int ble_ll_scan_start(struct ble_ll_scan_sm *scansm,
                             struct ble_ll_sched_item *sch);

static int
ble_ll_aux_scan_cb(struct ble_ll_sched_item *sch)
{
    struct ble_ll_scan_sm *scansm = &g_ble_ll_scan_sm;
    uint8_t lls = ble_ll_state_get();
    uint32_t wfr_usec;

    /* In case scan has been disabled or there is other aux ptr in progress
     * just drop the scheduled item
     */
    if (!scansm->scan_enabled || scansm->cur_aux_data) {
        ble_ll_scan_aux_data_free(sch->cb_arg);
        goto done;
    }

    /* Check if there is no aux connect sent. If so drop the sched item */
    if (lls == BLE_LL_STATE_INITIATING && ble_ll_conn_init_pending_aux_conn_rsp()) {
        ble_ll_scan_aux_data_free(sch->cb_arg);
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
    assert(scansm->cur_aux_data != NULL);
    scansm->cur_aux_data->scanning = 1;

    if (ble_ll_scan_start(scansm, sch)) {
        ble_ll_scan_aux_data_free(scansm->cur_aux_data);
        scansm->cur_aux_data = NULL;
        ble_ll_scan_chk_resume();
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

    e = os_memblock_get(&ext_adv_pool);
    if (!e) {
        return -1;
    }

    memset(e, 0, sizeof(*e));
    e->sch.sched_cb = ble_ll_aux_scan_cb;
    e->sch.cb_arg = e;
    e->sch.sched_type = BLE_LL_SCHED_TYPE_AUX_SCAN;

    *aux_data = e;

    return 0;
}
#endif

/* See Vol 6 Part B Section 4.4.3.2. Active scanning backoff */
static void
ble_ll_scan_req_backoff(struct ble_ll_scan_sm *scansm, int success)
{
    scansm->scan_rsp_pending = 0;
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
    assert(scansm->backoff_count <= 256);
}

/**
 * ble ll scan req pdu make
 *
 * Construct a SCAN_REQ PDU.
 *
 * @param scansm Pointer to scanning state machine
 * @param adv_addr Pointer to device address of advertiser
 * @param addr_type 0 if public; non-zero if random. This is the addr type of
 *                  the advertiser; not our "own address type"
 */
static void
ble_ll_scan_req_pdu_make(struct ble_ll_scan_sm *scansm, uint8_t *adv_addr,
                         uint8_t adv_addr_type)
{
    uint8_t     *dptr;
    uint8_t     pdu_type;
    uint8_t     *scana;
    struct os_mbuf *m;
#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    uint8_t rpa[BLE_DEV_ADDR_LEN];
    struct ble_ll_resolv_entry *rl;
#endif

    /* Construct first PDU header byte */
    pdu_type = BLE_ADV_PDU_TYPE_SCAN_REQ;
    if (adv_addr_type) {
        pdu_type |= BLE_ADV_PDU_HDR_RXADD_RAND;
    }

    /* Get the advertising PDU */
    m = scansm->scan_req_pdu;
    assert(m != NULL);

    /* Get pointer to our device address */
    if ((scansm->own_addr_type & 1) == 0) {
        scana = g_dev_addr;
    } else {
        pdu_type |= BLE_ADV_PDU_HDR_TXADD_RAND;
        scana = g_random_addr;
    }

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    if (scansm->own_addr_type > BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        rl = NULL;
        if (ble_ll_is_rpa(adv_addr, adv_addr_type)) {
            if (scansm->scan_rpa_index >= 0) {
                /* Generate a RPA to use for scana */
                rl = &g_ble_ll_resolv_list[scansm->scan_rpa_index];
            }
        } else {
            if (ble_ll_resolv_enabled()) {
                rl = ble_ll_resolv_list_find(adv_addr, adv_addr_type);
            }
        }

        if (rl) {
            ble_ll_resolv_gen_priv_addr(rl, 1, rpa);
            scana = rpa;
            pdu_type |= BLE_ADV_PDU_HDR_TXADD_RAND;
        }
    }
#endif

    ble_ll_mbuf_init(m, BLE_SCAN_REQ_LEN, pdu_type);

    /* Construct the scan request */
    dptr = m->om_data;
    memcpy(dptr, scana, BLE_DEV_ADDR_LEN);
    memcpy(dptr + BLE_DEV_ADDR_LEN, adv_addr, BLE_DEV_ADDR_LEN);
}

/**
 * Checks to see if an advertiser is on the duplicate address list.
 *
 * @param addr Pointer to address
 * @param txadd TxAdd bit. 0: public; random otherwise
 *
 * @return uint8_t 0: not on list; any other value is
 */
static struct ble_ll_scan_advertisers *
ble_ll_scan_find_dup_adv(uint8_t *addr, uint8_t txadd)
{
    uint8_t num_advs;
    struct ble_ll_scan_advertisers *adv;

    /* Do we have an address match? Must match address type */
    adv = &g_ble_ll_scan_dup_advs[0];
    num_advs = g_ble_ll_scan_num_dup_advs;
    while (num_advs) {
        if (!memcmp(&adv->adv_addr, addr, BLE_DEV_ADDR_LEN)) {
            /* Address type must match */
            if (txadd) {
                if ((adv->sc_adv_flags & BLE_LL_SC_ADV_F_RANDOM_ADDR) == 0) {
                    goto next_dup_adv;
                }
            } else {
                if (adv->sc_adv_flags & BLE_LL_SC_ADV_F_RANDOM_ADDR) {
                    goto next_dup_adv;
                }
            }

            return adv;
        }

next_dup_adv:
        ++adv;
        --num_advs;
    }

    return NULL;
}

/**
 * Do scan machine clean up on PHY disabled
 *
 */
void
ble_ll_scan_clean_cur_aux_data(void)
{
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    struct ble_ll_scan_sm *scansm = &g_ble_ll_scan_sm;

    /* If scanner was reading aux ptr, we need to clean it up */
    if (scansm && scansm->cur_aux_data) {
        ble_ll_scan_aux_data_free(scansm->cur_aux_data);
        scansm->cur_aux_data = NULL;
    }
#endif
}

/**
 * Check if a packet is a duplicate advertising packet.
 *
 * @param pdu_type
 * @param rxbuf
 *
 * @return int 0: not a duplicate. 1:duplicate
 */
int
ble_ll_scan_is_dup_adv(uint8_t pdu_type, uint8_t txadd, uint8_t *addr)
{
    struct ble_ll_scan_advertisers *adv;

    adv = ble_ll_scan_find_dup_adv(addr, txadd);
    if (adv) {
        /* Check appropriate flag (based on type of PDU) */
        if (pdu_type == BLE_ADV_PDU_TYPE_ADV_DIRECT_IND) {
            if (adv->sc_adv_flags & BLE_LL_SC_ADV_F_DIRECT_RPT_SENT) {
                return 1;
            }
        } else if (pdu_type == BLE_ADV_PDU_TYPE_SCAN_RSP) {
            if (adv->sc_adv_flags & BLE_LL_SC_ADV_F_SCAN_RSP_SENT) {
                return 1;
            }
        } else {
            if (adv->sc_adv_flags & BLE_LL_SC_ADV_F_ADV_RPT_SENT) {
                return 1;
            }
        }
    }

    return 0;
}

/**
 * Add an advertiser the list of duplicate advertisers. An address gets added to
 * the list of duplicate addresses when the controller sends an advertising
 * report to the host.
 *
 * @param addr   Pointer to advertisers address or identity address
 * @param Txadd. TxAdd bit (0 public, random otherwise)
 * @param subev  Type of advertising report sent (direct or normal).
 * @param evtype Advertising event type
 */
void
ble_ll_scan_add_dup_adv(uint8_t *addr, uint8_t txadd, uint8_t subev,
                        uint8_t evtype)
{
    uint8_t num_advs;
    struct ble_ll_scan_advertisers *adv;

    /* Check to see if on list. */
    adv = ble_ll_scan_find_dup_adv(addr, txadd);
    if (!adv) {
        /* XXX: for now, if we dont have room, just leave */
        num_advs = g_ble_ll_scan_num_dup_advs;
        if (num_advs == MYNEWT_VAL(BLE_LL_NUM_SCAN_DUP_ADVS)) {
            return;
        }

        /* Add the advertiser to the array */
        adv = &g_ble_ll_scan_dup_advs[num_advs];
        memcpy(&adv->adv_addr, addr, BLE_DEV_ADDR_LEN);
        ++g_ble_ll_scan_num_dup_advs;

        adv->sc_adv_flags = 0;
        if (txadd) {
            adv->sc_adv_flags |= BLE_LL_SC_ADV_F_RANDOM_ADDR;
        }
    }

    if (subev == BLE_HCI_LE_SUBEV_DIRECT_ADV_RPT) {
        adv->sc_adv_flags |= BLE_LL_SC_ADV_F_DIRECT_RPT_SENT;
    } else {
        if (evtype == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP) {
            adv->sc_adv_flags |= BLE_LL_SC_ADV_F_SCAN_RSP_SENT;
        } else {
            adv->sc_adv_flags |= BLE_LL_SC_ADV_F_ADV_RPT_SENT;
        }
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
ble_ll_scan_have_rxd_scan_rsp(uint8_t *addr, uint8_t txadd)
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
                    return 1;
                }
            } else {
                if ((adv->sc_adv_flags & BLE_LL_SC_ADV_F_RANDOM_ADDR) == 0) {
                    return 1;
                }
            }
        }
        ++adv;
        --num_advs;
    }

    return 0;
}

static void
ble_ll_scan_add_scan_rsp_adv(uint8_t *addr, uint8_t txadd)
{
    uint8_t num_advs;
    struct ble_ll_scan_advertisers *adv;

    /* XXX: for now, if we dont have room, just leave */
    num_advs = g_ble_ll_scan_num_rsp_advs;
    if (num_advs == MYNEWT_VAL(BLE_LL_NUM_SCAN_RSP_ADVS)) {
        return;
    }

    /* Check if address is already on the list */
    if (ble_ll_scan_have_rxd_scan_rsp(addr, txadd)) {
        return;
    }

    /* Add the advertiser to the array */
    adv = &g_ble_ll_scan_rsp_advs[num_advs];
    memcpy(&adv->adv_addr, addr, BLE_DEV_ADDR_LEN);
    adv->sc_adv_flags = BLE_LL_SC_ADV_F_SCAN_RSP_RXD;
    if (txadd) {
        adv->sc_adv_flags |= BLE_LL_SC_ADV_F_RANDOM_ADDR;
    }
    ++g_ble_ll_scan_num_rsp_advs;

    return;
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)

static struct ble_ll_ext_adv *
ble_ll_scan_init_ext_adv(void)
{
    struct ble_ll_ext_adv *evt;

    evt = (struct ble_ll_ext_adv *) ble_hci_trans_buf_alloc(
                                                    BLE_HCI_TRANS_BUF_EVT_LO);
    if (!evt) {
        return NULL;
    }

    memset(evt, 0, sizeof(*evt));

    evt->event_meta = BLE_HCI_EVCODE_LE_META;
    evt->subevt = BLE_HCI_LE_SUBEV_EXT_ADV_RPT;
    /* We support only one report per event now */
    evt->num_reports = 1;
    /* Init TX Power with "Not available" which is 127 */
    evt->tx_power = 127;
    /* Init RSSI with "Not available" which is 127 */
    evt->rssi = 127;
    /* Init SID with "Not available" which is 0xFF */
    evt->sid = 0xFF;
    /* Init address type with "anonymous" which is 0xFF */
    evt->addr_type = 0xFF;

    return evt;
}

static int
ble_ll_hci_send_legacy_ext_adv_report(uint8_t evtype,
                                      uint8_t addr_type, uint8_t *addr,
                                      uint8_t rssi,
                                      uint8_t adv_data_len, struct os_mbuf *adv_data,
                                      uint8_t *inita)
{
    struct ble_ll_ext_adv *evt;

    if (!ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_EXT_ADV_RPT)) {
        return -1;
    }

    /* Drop packet if it does not fit into event buffer */
    if ((sizeof(*evt) + adv_data_len) + 1 > MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE)) {
        STATS_INC(ble_ll_stats, adv_evt_dropped);
        return -1;
    }

    evt = ble_ll_scan_init_ext_adv();
    if (!evt) {
        return 0;
    }

    switch (evtype) {
    case BLE_HCI_ADV_RPT_EVTYPE_ADV_IND:
        evt->evt_type = BLE_HCI_LEGACY_ADV_EVTYPE_ADV_IND;
        break;
    case BLE_HCI_ADV_RPT_EVTYPE_DIR_IND:
        evt->evt_type = BLE_HCI_LEGACY_ADV_EVTYPE_ADV_DIRECT_IND;
        break;
    case BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND:
        evt->evt_type = BLE_HCI_LEGACY_ADV_EVTYPE_ADV_NONCON_IND;
        break;
    case BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP:
        evt->evt_type = BLE_HCI_LEGACY_ADV_EVTYPE_SCAN_RSP_ADV_IND;
         break;
    case BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND:
        evt->evt_type = BLE_HCI_LEGACY_ADV_EVTYPE_ADV_SCAN_IND;
        break;
    default:
        assert(0);
        break;
    }

    evt->addr_type = addr_type;
    memcpy(evt->addr, addr, BLE_DEV_ADDR_LEN);

    evt->event_len = sizeof(*evt);

    if (inita) {
        /* TODO Really ?? */
        evt->dir_addr_type = BLE_HCI_ADV_OWN_ADDR_RANDOM;
        memcpy(evt->dir_addr, inita, BLE_DEV_ADDR_LEN);
        evt->event_len += BLE_DEV_ADDR_LEN  + 1;
    } else if (adv_data_len <= (MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE) - sizeof(*evt))) {
        evt->adv_data_len = adv_data_len;
        os_mbuf_copydata(adv_data, 0, adv_data_len, evt->adv_data);
        evt->event_len += adv_data_len;
    }

    evt->rssi = rssi;
    evt->prim_phy = BLE_PHY_1M;

    return ble_ll_hci_event_send((uint8_t *) evt);
}
#endif

static int
ble_ll_hci_send_adv_report(uint8_t subev, uint8_t evtype,uint8_t event_len,
                           uint8_t addr_type, uint8_t *addr, uint8_t rssi,
                           uint8_t adv_data_len, struct os_mbuf *adv_data,
                           uint8_t *inita)
{
    uint8_t *evbuf;
    uint8_t *tmp;

    if (!ble_ll_hci_is_le_event_enabled(subev)) {
        return -1;
    }

    /* Drop packet if it does not fit into event buffer */
    if (event_len + 1 > MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE)) {
        STATS_INC(ble_ll_stats, adv_evt_dropped);
        return -1;
    }

    evbuf = ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_LO);
    if (!evbuf) {
        return -1;
    }

    evbuf[0] = BLE_HCI_EVCODE_LE_META;
    evbuf[1] = event_len;
    evbuf[2] = subev;
    evbuf[3] = 1;       /* number of reports */
    evbuf[4] = evtype;
    evbuf[5] = addr_type;
    memcpy(&evbuf[6], addr, BLE_DEV_ADDR_LEN);

    tmp = &evbuf[12];
    if (subev == BLE_HCI_LE_SUBEV_DIRECT_ADV_RPT) {
        assert(inita);
        tmp[0] = BLE_HCI_ADV_OWN_ADDR_RANDOM;
        memcpy(tmp + 1, inita, BLE_DEV_ADDR_LEN);
        tmp += BLE_DEV_ADDR_LEN + 1;
    } else if (subev == BLE_HCI_LE_SUBEV_ADV_RPT) {
        tmp[0] = adv_data_len;
        os_mbuf_copydata(adv_data, 0, adv_data_len, tmp + 1);
        tmp += adv_data_len + 1;
    } else {
        assert(0);
        return -1;
    }

    tmp[0] = rssi;

    return ble_ll_hci_event_send(evbuf);
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
ble_ll_scan_send_adv_report(uint8_t pdu_type, uint8_t txadd, struct os_mbuf *om,
                           struct ble_mbuf_hdr *hdr,
                           struct ble_ll_scan_sm *scansm)
{
    int rc;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    int index;
#endif
    uint8_t *rxbuf = om->om_data;
    uint8_t evtype;
    uint8_t subev;
    uint8_t *adv_addr;
    uint8_t *inita;
    uint8_t addr_type;
    uint8_t adv_data_len;
    uint8_t event_len;

    inita = NULL;

    if (pdu_type == BLE_ADV_PDU_TYPE_ADV_DIRECT_IND) {
        inita = rxbuf + BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN;
        if (!(inita[5] & 0x40)) {
            /* Let's ignore it if address is not resolvable */
            return;
        }

        subev = BLE_HCI_LE_SUBEV_DIRECT_ADV_RPT;
        evtype = BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
        event_len = BLE_HCI_LE_ADV_DIRECT_RPT_LEN;
        adv_data_len = 0;
    } else {
        subev = BLE_HCI_LE_SUBEV_ADV_RPT;
        if (pdu_type == BLE_ADV_PDU_TYPE_ADV_IND) {
            evtype = BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;
        } else if (pdu_type == BLE_ADV_PDU_TYPE_ADV_SCAN_IND) {
            evtype = BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND;
        } else if (pdu_type == BLE_ADV_PDU_TYPE_ADV_NONCONN_IND) {
            evtype = BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND;
        } else {
            evtype = BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP;
        }
        adv_data_len = rxbuf[1] & BLE_ADV_PDU_HDR_LEN_MASK;
        adv_data_len -= BLE_DEV_ADDR_LEN;
        event_len = BLE_HCI_LE_ADV_RPT_MIN_LEN + adv_data_len;
        os_mbuf_adj(om, BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN);
    }

    if (txadd) {
        addr_type = BLE_HCI_ADV_OWN_ADDR_RANDOM;
    } else {
        addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
    }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    if (BLE_MBUF_HDR_RESOLVED(hdr)) {
        index = scansm->scan_rpa_index;
        adv_addr = g_ble_ll_resolv_list[index].rl_identity_addr;
        /*
         * NOTE: this looks a bit odd, but the resolved address types
         * are 2 greater than the unresolved ones in the spec, so
         * we just add 2 here.
         */
        addr_type = g_ble_ll_resolv_list[index].rl_addr_type + 2;
    } else {
        adv_addr = rxbuf + BLE_LL_PDU_HDR_LEN;
    }
#else
    adv_addr = rxbuf + BLE_LL_PDU_HDR_LEN;
#endif

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (scansm->ext_scanning) {
        rc = ble_ll_hci_send_legacy_ext_adv_report(evtype,
                                                   addr_type, adv_addr,
                                                   hdr->rxinfo.rssi,
                                                   adv_data_len, om,
                                                   inita);
    } else {
        rc = ble_ll_hci_send_adv_report(subev, evtype, event_len,
                                        addr_type, adv_addr,
                                        hdr->rxinfo.rssi,
                                        adv_data_len, om,
                                        inita);
    }
#else
    rc = ble_ll_hci_send_adv_report(subev, evtype, event_len,
                                    addr_type, adv_addr,
                                    hdr->rxinfo.rssi,
                                    adv_data_len, om,
                                    inita);
#endif
    if (!rc) {
        /* If filtering, add it to list of duplicate addresses */
        if (scansm->scan_filt_dups) {
            ble_ll_scan_add_dup_adv(adv_addr, txadd, subev, evtype);
        }
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
        assert(0);
        break;
    }

    /* If we are using the whitelist, check that first */
    if (use_whitelist && (pdu_type != BLE_ADV_PDU_TYPE_SCAN_RSP)) {
        /* If there was a devmatch, we will allow the PDU */
        if (devmatch) {
            return 0;
        } else {
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

    ble_ll_get_chan_to_scan(scansm, &scan_chan, &phy);

    /* XXX: right now scheduled item is only present if we schedule for aux
     *      scan just make sanity check that we have proper combination of
     *      sch and resulting scan_chan
     */
    assert(!sch || scan_chan < BLE_PHY_ADV_CHAN_START);
    assert(sch || scan_chan >= BLE_PHY_ADV_CHAN_START);
    
    /* Set channel */
    rc = ble_phy_setchan(scan_chan, BLE_ACCESS_ADDR_ADV, BLE_LL_CRCINIT_ADV);
    assert(rc == 0);

    /*
     * Set transmit end callback to NULL in case we transmit a scan request.
     * There is a callback for the connect request.
     */
    ble_phy_set_txend_cb(NULL, NULL);

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_ENCRYPTION) == 1)
    ble_phy_encrypt_disable();
#endif

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
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
    if (!rc) {
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

    /* If there is a still a scan response pending, we have failed! */
    if (scansm->scan_rsp_pending) {
        ble_ll_scan_req_backoff(scansm, 0);
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
        ble_ll_log(BLE_LL_LOG_ID_RFCLK_SCAN_DIS, g_ble_ll_data.ll_rfclk_state,0,0);
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
ble_ll_scan_get_current_scan_win(struct ble_ll_scan_sm *scansm, uint32_t cputime)
{
    uint32_t itvl;
    struct ble_ll_scan_params *scanphy = &scansm->phy_data[scansm->cur_phy];

    /* Well, in case we missed to schedule scan, lets move to next stan interval.
     */
    itvl = os_cputime_usecs_to_ticks(scanphy->scan_itvl * BLE_HCI_SCAN_ITVL);
    while ((int32_t)(cputime - scanphy->scan_win_start_time) >= itvl) {
        scanphy->scan_win_start_time += itvl;

        /* If we missed scan window, make sure we update scan channel */
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

    win_start = ble_ll_scan_get_current_scan_win(scansm, cputime);

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

    /* Disable scanning state machine */
    scansm->scan_enabled = 0;
    scansm->restart_timer_needed = 0;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    OS_ENTER_CRITICAL(sr);
    ble_ll_scan_clean_cur_aux_data();
    OS_EXIT_CRITICAL(sr);
#endif

    /* Count # of times stopped */
    STATS_INC(ble_ll_stats, scan_stops);

    /* Only set state if we are currently in a scan window */
    if (chk_disable) {
        OS_ENTER_CRITICAL(sr);
        lls = ble_ll_state_get();
        if ((lls == BLE_LL_STATE_SCANNING) || (lls == BLE_LL_STATE_INITIATING)) {
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
    /*
     * This is not in the specification. I will reject the command with a
     * command disallowed error if no random address has been sent by the
     * host. All the parameter errors refer to the command parameter
     * (which in this case is just enable or disable) so that is why I chose
     * command disallowed.
     */
    if (scansm->own_addr_type == BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        if (!ble_ll_is_valid_random_addr(g_random_addr)) {
            return BLE_ERR_CMD_DISALLOWED;
        }
    }

    /* Count # of times started */
    STATS_INC(ble_ll_stats, scan_starts);

    /* Set flag telling us that scanning is enabled */
    scansm->scan_enabled = 1;

    /* Set first advertising channel */
    assert(scansm->cur_phy != PHY_NOT_CONFIGURED);
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
    g_ble_ll_scan_num_dup_advs = 0;

    /* XXX: align to current or next slot???. */
    /* Schedule start time now */
    scansm->phy_data[scansm->cur_phy].scan_win_start_time = os_cputime_get32();

    /* Post scanning event to start off the scanning process */
    ble_ll_event_send(&scansm->scan_sched_ev);

    return BLE_ERR_SUCCESS;
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
static void
ble_ll_scan_switch_phy(struct ble_ll_scan_sm *scansm)
{
    uint8_t tmp;

    if (scansm->next_phy == PHY_NOT_CONFIGURED) {
        return;
    }

    tmp = scansm->next_phy;
    scansm->next_phy = scansm->cur_phy;
    scansm->cur_phy = tmp;

    /* PHY is changing in ble_ll_scan_start() */
}

/**
 * Called to change PHY if needed and set new event time
 *
 * Context: Link Layer task.
 *
 * @param arg
 */
static uint32_t
ble_ll_scan_start_next_phy(struct ble_ll_scan_sm *scansm,
                                 uint32_t next_event_time)
{
    struct ble_ll_scan_params *cur_phy;
    struct ble_ll_scan_params *next_phy;
    uint32_t now;
    uint32_t win;

    /* Lets check if we want to switch to other PHY */
    if (scansm->cur_phy == scansm->next_phy ||
            scansm->next_phy == PHY_NOT_CONFIGURED) {
        return next_event_time;
    }

    cur_phy = &scansm->phy_data[scansm->cur_phy];
    next_phy = &scansm->phy_data[scansm->next_phy];

    cur_phy->next_event_start = next_event_time;

    if ((int32_t)(next_phy->next_event_start - next_event_time)<  0) {
        /* Other PHY already wanted to scan. Allow it */
        ble_ll_scan_switch_phy(scansm);
        now = os_cputime_get32();
        if ((int32_t)(next_phy->next_event_start - now) <= 0) {
            /* Start with new channel only if PHY was scanning already */
            if (next_phy->next_event_start != 0) {
                next_phy->scan_chan =
                        ble_ll_scan_get_next_adv_prim_chan(next_phy->scan_chan);
            }
            next_phy->scan_win_start_time = now;
            win = os_cputime_usecs_to_ticks(next_phy->scan_window *
                                            BLE_HCI_SCAN_ITVL);

            next_phy->next_event_start = now + win;
            ble_ll_scan_start(scansm, NULL);
        }
        next_event_time = next_phy->next_event_start;
    }

    return next_event_time;
}

static void
ble_ll_aux_scan_rsp_failed(void)
{
    STATS_INC(ble_ll_stats, aux_scan_rsp_err);
    ble_ll_scan_clean_cur_aux_data();
}
#endif

/**
 * Called to process the scanning OS event which was posted to the LL task
 *
 * Context: Link Layer task.
 *
 * @param arg
 */
static void
ble_ll_scan_event_proc(struct os_event *ev)
{
    os_sr_t sr;
    int inside_window;
    int start_scan;
    uint32_t now;
    uint32_t dt;
    uint32_t win;
    uint32_t win_start;
    uint32_t scan_itvl;
    uint32_t next_event_time;
#ifdef BLE_XCVR_RFCLK
    uint32_t xtal_ticks;
    int xtal_state;
#endif
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_scan_params *scanphy;

    /*
     * Get the scanning state machine. If not enabled (this is possible), just
     * leave and do nothing (just make sure timer is stopped).
     */
    scansm = (struct ble_ll_scan_sm *)ev->ev_arg;
    scanphy = &scansm->phy_data[scansm->cur_phy];

    OS_ENTER_CRITICAL(sr);
    if (!scansm->scan_enabled) {
        os_cputime_timer_stop(&scansm->scan_timer);
        OS_EXIT_CRITICAL(sr);
        return;
    }

    if (scansm->cur_aux_data) {
        /* Aux scan in progress. Wait */
        STATS_INC(ble_ll_stats, scan_timer_stopped);
        scansm->restart_timer_needed = 1;
        OS_EXIT_CRITICAL(sr);
        return;
    }

    /* Make sure the scan window start time and channel are up to date. */
    now = os_cputime_get32();
    win_start = ble_ll_scan_get_current_scan_win(scansm, now);

    /* Check if we are in scan window */
    dt = now - win_start;

    if (scanphy->scan_window != scanphy->scan_itvl) {
        win = os_cputime_usecs_to_ticks(scanphy->scan_window * BLE_HCI_SCAN_ITVL);
        inside_window = dt < win ? 1 : 0;
    } else {
        win = 0;
        /* In case continous scan lets assume we area always in the window*/
        inside_window = 1;
    }

    /* Determine on/off state based on scan window */
    scan_itvl = os_cputime_usecs_to_ticks(scanphy->scan_itvl *
                                                    BLE_HCI_SCAN_ITVL);

    if (win != 0 && inside_window) {
        next_event_time = win_start + win;
    } else {
        next_event_time = win_start + scan_itvl;
    }

    /*
     * If we are not in the standby state it means that the scheduled
     * scanning event was overlapped in the schedule. In this case all we do
     * is post the scan schedule end event.
     */
    start_scan = 1;
    switch (ble_ll_state_get()) {
    case BLE_LL_STATE_ADV:
    case BLE_LL_STATE_CONNECTION:
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
        assert(0);
        break;
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
                    ble_ll_xcvr_rfclk_start_now(now);
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
                    goto rfclk_not_settled;
                }
            }

            /*
             * If clock off, start clock. Set next event time to now plus
             * the clock setting time.
             */
            if (xtal_state == BLE_RFCLK_STATE_OFF) {
                ble_ll_xcvr_rfclk_start_now(now);
            }
            next_event_time = now + xtal_ticks;
            goto rfclk_not_settled;
        }
#endif
        ble_ll_scan_start(scansm, NULL);
    } else {
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
      next_event_time = ble_ll_scan_start_next_phy(scansm, next_event_time);
#endif
    }

#ifdef BLE_XCVR_RFCLK
rfclk_not_settled:
#endif
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

        if ((pdu_type == BLE_ADV_PDU_TYPE_ADV_EXT_IND && scansm->ext_scanning)) {
            *rxflags |= BLE_MBUF_HDR_F_EXT_ADV;
            rc = 1;
        }

        /*
         * If this is the first PDU after we sent the scan response (as
         * denoted by the scan rsp pending flag), we set a bit in the ble
         * header so the link layer can check to see if the scan request
         * was successful. We do it this way to let the Link Layer do the
         * work for successful scan requests. If failed, we do the work here.
         */
        if (scansm->scan_rsp_pending) {
            if (pdu_type == BLE_ADV_PDU_TYPE_SCAN_RSP) {
                *rxflags |= BLE_MBUF_HDR_F_SCAN_RSP_CHK;
            } else if (pdu_type == BLE_ADV_PDU_TYPE_AUX_SCAN_RSP) {
                *rxflags |= BLE_MBUF_HDR_F_SCAN_RSP_CHK;
            } else {
                ble_ll_scan_req_backoff(scansm, 0);
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
                ble_ll_aux_scan_rsp_failed();
#endif
            }
        }

        if (scansm->cur_aux_data && !scansm->scan_rsp_pending ) {
            STATS_INC(ble_ll_stats, aux_received);
        }

        /* Disable wfr if running */
        ble_ll_wfr_disable();
        break;
    case BLE_SCAN_TYPE_PASSIVE:
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
    case 0x01:
        return BLE_PHY_2M;
    case 0x02:
        return BLE_PHY_CODED;
    }

    return 0;
}

static int
ble_ll_ext_scan_parse_aux_ptr(struct ble_ll_scan_sm *scansm,
                              struct ble_ll_aux_data *aux_scan, uint8_t *buf)
{
    uint32_t aux_ptr_field = get_le32(buf) & 0x00FFFFFF;

    aux_scan->chan = (aux_ptr_field) & 0x3F;
    if (aux_scan->chan >= BLE_PHY_NUM_DATA_CHANS) {
        return -1;
    }

    /* TODO use CA aux_ptr_field >> 6 */

    aux_scan->offset = 30 * ((aux_ptr_field >> 8) & 0x1FFF);

    if ((aux_ptr_field >> 7) & 0x01) {
            aux_scan->offset *= 10;
            aux_scan->offset_units = 1;
    }

    if (aux_scan->offset < BLE_LL_MAFS) {
        return -1;
    }

    aux_scan->aux_phy =
            ble_ll_ext_adv_phy_mode_to_local_phy((aux_ptr_field >> 21) & 0x07);
    if (aux_scan->aux_phy == 0) {
        return -1;
    }

    return 0;
}

static void
ble_ll_ext_scan_parse_adv_info(struct ble_ll_scan_sm *scansm,
                               struct ble_ll_ext_adv *evt, uint8_t *buf)
{
    uint16_t adv_info = get_le16(buf);

    /* TODO Use DID */

    evt->sid = (adv_info >> 12);
}

/**
 * ble_ll_scan_get_aux_data
 *
 * Get aux data pointer. It is new allocated data for beacon or currently
 * processing aux data pointer
 *
 * Context: Interrupt
 *
 * @param scansm
 * @param ble_hdr
 * @param rxbuf
 * @param aux_data
 *
 * @return int
 *  0: new allocated aux data
 *  1: current processing aux data
 * -1: error
 */
int
ble_ll_scan_get_aux_data(struct ble_ll_scan_sm *scansm,
                         struct ble_mbuf_hdr *ble_hdr, uint8_t *rxbuf,
                         struct ble_ll_aux_data **aux_data)
{
    uint8_t ext_hdr_len;
    uint8_t pdu_len;
    uint8_t ext_hdr_flags;
    uint8_t *ext_hdr;
    uint8_t has_addr = 0;
    int i;
    struct ble_ll_aux_data tmp_aux_data = { 0 };

    pdu_len = rxbuf[1];
    if (pdu_len == 0) {
        return -1;
    }

    tmp_aux_data.mode = rxbuf[2] >> 6;

    ext_hdr_len = rxbuf[2] & 0x3F;
    if (ext_hdr_len < BLE_LL_EXT_ADV_AUX_PTR_SIZE) {
        return -1;
    }

    ext_hdr_flags = rxbuf[3];
    ext_hdr = &rxbuf[4];

    i = 0;
    /* Just all until AUX PTR it for now*/
    if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_ADVA_BIT)) {
        memcpy(tmp_aux_data.addr, ext_hdr + i, 6);
        tmp_aux_data.addr_type =
                ble_ll_get_addr_type(rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK);
        i += BLE_LL_EXT_ADV_ADVA_SIZE;
        has_addr = 1;
    }

    if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_TARGETA_BIT)) {
        i += BLE_LL_EXT_ADV_TARGETA_SIZE;
    }

    if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_RFU_BIT)) {
        i += 1;
    }

    if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_DATA_INFO_BIT)) {
        tmp_aux_data.did = get_le16(ext_hdr + i);
        i += BLE_LL_EXT_ADV_DATA_INFO_SIZE;
    }

    if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_AUX_PTR_BIT)) {

        if (ble_ll_ext_scan_parse_aux_ptr(scansm, &tmp_aux_data, ext_hdr + i) < 0) {
            return -1;
        }

        if (scansm->cur_aux_data) {
            /* If we are here that means there is chain advertising. */

            /* Lets reuse old aux_data */
            *aux_data = scansm->cur_aux_data;

            /* TODO Collision; Do smth smart when did does not match */
            if (tmp_aux_data.did != (*aux_data)->did) {
                STATS_INC(ble_ll_stats, aux_chain_err);
                (*aux_data)->flags |= BLE_LL_AUX_INCOMPLETE_ERR_BIT;
            }

            (*aux_data)->flags |= BLE_LL_AUX_CHAIN_BIT;
            (*aux_data)->flags |= BLE_LL_AUX_INCOMPLETE_BIT;
        } else if (ble_ll_scan_ext_adv_init(aux_data) < 0) {
            return -1;
        }

        (*aux_data)->aux_phy = tmp_aux_data.aux_phy;

        if (!scansm->cur_aux_data) {
            /* Only for first ext adv we want to keep primary phy.*/
            (*aux_data)->aux_primary_phy = ble_hdr->rxinfo.phy;
        } else {
            /* We are ok to clear cur_aux_data now. */
            scansm->cur_aux_data = NULL;
        }

        (*aux_data)->did = tmp_aux_data.did;
        (*aux_data)->chan = tmp_aux_data.chan;
        (*aux_data)->offset = tmp_aux_data.offset;
        (*aux_data)->mode = tmp_aux_data.mode;
        if (has_addr) {
            memcpy((*aux_data)->addr, tmp_aux_data.addr, 6);
            (*aux_data)->addr_type = tmp_aux_data.addr_type;
            (*aux_data)->flags |= BLE_LL_AUX_HAS_ADDRA;
        }
        return 0;
    }

    /*If there is no new aux ptr, just get current one */
    (*aux_data) = scansm->cur_aux_data;
    scansm->cur_aux_data = NULL;

    /* Clear incomplete flag */
    (*aux_data)->flags &= ~BLE_LL_AUX_INCOMPLETE_BIT;

    return 1;
}
/**
 * Called when a receive ADV_EXT PDU has ended.
 *
 * Context: Interrupt
 *
 * @return int
 *       < 0  Error
 *      == 0: Success.
 *
 */
int
ble_ll_scan_parse_ext_adv(struct os_mbuf *om, struct ble_mbuf_hdr *ble_hdr,
                          struct ble_ll_ext_adv *out_evt)
{
    uint8_t pdu_len;
    uint8_t ext_hdr_len;
    uint8_t ext_hdr_flags;
    uint8_t *ext_hdr;
    uint8_t *rxbuf = om->om_data;
    int i = 1;
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_aux_data *aux_data = ble_hdr->rxinfo.user_data;

    if (!out_evt) {
        return -1;
    }

    scansm = &g_ble_ll_scan_sm;

    if (!scansm->ext_scanning) {
       /* Ignore ext adv if host does not want it*/
       return -1;
    }

    pdu_len = rxbuf[1];
    if (pdu_len == 0) {
        return -1;
    }

    out_evt->evt_type = rxbuf[2] >> 6;
    if (out_evt->evt_type > BLE_LL_EXT_ADV_MODE_SCAN) {
        return -1;
    }

    ext_hdr_len = rxbuf[2] & 0x3F;
    os_mbuf_adj(om, 3);

    ext_hdr_flags = rxbuf[3];
    ext_hdr = &rxbuf[4];

    i = 0;
    if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_ADVA_BIT)) {
        memcpy(out_evt->addr, ext_hdr + i, BLE_LL_EXT_ADV_ADVA_SIZE);
        out_evt->addr_type =
                ble_ll_get_addr_type(rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK);
        i += BLE_LL_EXT_ADV_ADVA_SIZE;
    } else {
        if (aux_data->flags & BLE_LL_AUX_HAS_ADDRA) {
            /* Have address in aux_data */
            memcpy(out_evt->addr, aux_data->addr, 6);
            out_evt->addr_type = aux_data->addr_type;
        }
    }

    if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_TARGETA_BIT)) {
        memcpy(out_evt->dir_addr, ext_hdr + i, BLE_LL_EXT_ADV_ADVA_SIZE);
        out_evt->dir_addr_type =
                ble_ll_get_addr_type(rxbuf[0] & BLE_ADV_PDU_HDR_RXADD_MASK);
        i += BLE_LL_EXT_ADV_TARGETA_SIZE;
    }

    if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_RFU_BIT)) {
        /* Just skip it for now*/
        i += 1;
    }

    if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_DATA_INFO_BIT)) {
        ble_ll_ext_scan_parse_adv_info(scansm, out_evt, (ext_hdr + i));
        i += BLE_LL_EXT_ADV_DATA_INFO_SIZE;
    }

    /* In this point of time we don't want to care about aux ptr */
    if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_AUX_PTR_BIT)) {
        i += BLE_LL_EXT_ADV_AUX_PTR_SIZE;
    }

    if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_SYNC_INFO_BIT)) {
        /* TODO Handle periodic adv */
        i += BLE_LL_EXT_ADV_SYNC_INFO_SIZE;
    }

    if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_TX_POWER_BIT)) {
        out_evt->tx_power = *(ext_hdr + i);
        i += BLE_LL_EXT_ADV_TX_POWER_SIZE;
    }

    /* Skip ADAC if it is there */
    i = ext_hdr_len;

    /* Adjust for advertising data */
    os_mbuf_adj(om, i);

    if ((pdu_len - i - 1 > 0)) {
        out_evt->adv_data_len = pdu_len - i - 1;

        /* XXX Drop packet if it does not fit into event buffer for now.*/
        if ((sizeof(*out_evt) + out_evt->adv_data_len) + 1 >
                                        MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE)) {
            STATS_INC(ble_ll_stats, adv_evt_dropped);
            return -1;
        }

        os_mbuf_copydata(om, 0, out_evt->adv_data_len, out_evt->adv_data);
    }

    /* In the event we need information on primary and secondary PHY used during
     * advertising.
     */
    if (!aux_data) {
        out_evt->prim_phy = ble_hdr->rxinfo.phy;
        goto done;
    }

    out_evt->sec_phy = aux_data->aux_phy;
    out_evt->prim_phy = aux_data->aux_primary_phy;

    /* Set event type */
    if (BLE_LL_CHECK_AUX_FLAG(aux_data, BLE_LL_AUX_INCOMPLETE_BIT)) {
        out_evt->evt_type |= (BLE_HCI_ADV_INCOMPLETE << 8);
    } else if (BLE_LL_CHECK_AUX_FLAG(aux_data, BLE_LL_AUX_INCOMPLETE_ERR_BIT)) {
        out_evt->evt_type |= (BLE_HCI_ADV_CORRUPTED << 8);
    }

    if (BLE_MBUF_HDR_SCAN_RSP_RCV(ble_hdr)) {
        out_evt->evt_type |= BLE_HCI_ADV_SCAN_RSP_MASK;
    }

done:

    return 0;
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
    int i;
    struct ble_ll_aux_data *aux_data = ble_hdr->rxinfo.user_data;

    pdu_len = rxbuf[1];
    if (pdu_len == 0) {
        return -1;
    }

    *ext_mode = rxbuf[2] >> 6;
    if (*ext_mode > BLE_LL_EXT_ADV_MODE_SCAN) {
        return -1;
    }

    ext_hdr_len = rxbuf[2] & 0x3F;

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
        if (aux_data) {
            /* Lets copy addr to aux_data. Need it for e.g. chaining */
            /* XXX add sanity checks */
            memcpy(aux_data->addr, *addr, 6);
            aux_data->addr_type = *addr_type;
            aux_data->flags |= BLE_LL_AUX_HAS_ADDRA;
        }
    } else {
        /* We should have address already in aux_data */
        if (aux_data->flags & BLE_LL_AUX_HAS_ADDRA) {
            *addr = aux_data->addr;
            *addr_type = aux_data->addr_type;
        }
    }

    if (!inita) {
        return 0;
    }

    if (ext_hdr_flags & (1 << BLE_LL_EXT_ADV_TARGETA_BIT)) {
        *inita = ext_hdr + i;
        *inita_type =
                ble_ll_get_addr_type(rxbuf[0] & BLE_ADV_PDU_HDR_RXADD_MASK);
        i += BLE_LL_EXT_ADV_TARGETA_SIZE;
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
    if (ble_ll_scan_get_addr_from_ext_adv(rxbuf, ble_hdr, addr, addr_type,
                                          inita, inita_type, ext_mode)) {
        return -1;
    }
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
    uint8_t *rxbuf;
    struct ble_mbuf_hdr *ble_hdr;
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_scan_params *scanphy;
    int ext_adv_mode = -1;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    struct ble_ll_aux_data *aux_data = NULL;
    uint8_t phy_mode;
#endif

    /* Get scanning state machine */
    scansm = &g_ble_ll_scan_sm;
    scanphy = &scansm->phy_data[scansm->cur_phy];
    ble_hdr = BLE_MBUF_HDR_PTR(rxpdu);

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    ble_hdr->rxinfo.user_data = scansm->cur_aux_data;
#endif
    /*
     * The reason we do something different here (as opposed to failed CRC) is
     * that the received PDU will not be handed up in this case. So we have
     * to restart scanning and handle a failed scan request. Note that we
     * return 0 in this case because we dont want the phy disabled.
     */
    if (rxpdu == NULL) {
        if (scansm->scan_rsp_pending) {
            ble_ll_scan_req_backoff(scansm, 0);
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
            ble_ll_aux_scan_rsp_failed();
#endif
        }
        ble_phy_restart_rx();
        return 0;
    }

    /* Just leave if the CRC is not OK. */
    rc = -1;
    if (!crcok) {
        scansm->cur_aux_data = NULL;
        goto scan_rx_isr_exit;
    }

    /* Get pdu type, pointer to address and address "type"  */
    rxbuf = rxpdu->om_data;
    pdu_type = rxbuf[0] & BLE_ADV_PDU_HDR_TYPE_MASK;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (pdu_type == BLE_ADV_PDU_TYPE_ADV_EXT_IND) {
        if (!scansm->ext_scanning) {
            goto scan_rx_isr_exit;
        }
        /* Create new aux data for beacon or get current processing aux ptr */
        rc = ble_ll_scan_get_aux_data(scansm, ble_hdr, rxbuf, &aux_data);
        if (rc < 0) {
            /* No memory or broken packet */
            ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_AUX_INVALID;
            /* cur_aux_data is already in the ble_hdr->rxinfo.user_data and
             * will be taken care by LL task */
            scansm->cur_aux_data = NULL;

            goto scan_rx_isr_exit;
        }
        if (rc == 0) {
            /* Let's tell LL to schedule aux */
            ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_AUX_PTR_WAIT;
        }

        /* If the ble_ll_scan_get_aux_data() succeed, scansm->cur_aux_data is NULL
         * and aux_data contains correct data
         */
        assert(scansm->cur_aux_data == NULL);
        ble_hdr->rxinfo.user_data = aux_data;
        rc = -1;
    }
#endif

    /* Lets get addresses from advertising report*/
    if (ble_ll_scan_adv_decode_addr(pdu_type, rxbuf, ble_hdr,
                                    &peer, &peer_addr_type,
                                    NULL, NULL, &ext_adv_mode)) {
        goto scan_rx_isr_exit;
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

        if (ext_adv_mode == BLE_LL_EXT_ADV_MODE_SCAN) {
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
#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    if (ble_ll_is_rpa(peer, peer_addr_type) && ble_ll_resolv_enabled()) {
        index = ble_hw_resolv_list_match();
        if (index >= 0) {
            ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_RESOLVED;
            peer = g_ble_ll_resolv_list[index].rl_identity_addr;
            peer_addr_type = g_ble_ll_resolv_list[index].rl_addr_type;
            resolved = 1;
        } else {
            if (chk_wl) {
                goto scan_rx_isr_exit;
            }
        }
    }
#endif
    scansm->scan_rpa_index = index;

    /* If whitelist enabled, check to see if device is in the white list */
    if (chk_wl && !ble_ll_whitelist_match(peer, peer_addr_type, resolved)) {
        goto scan_rx_isr_exit;
    }
    ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_DEVMATCH;

    /* Should we send a scan request? */
    if (chk_send_req) {
        /* Dont send scan request if we have sent one to this advertiser */
        if (ble_ll_scan_have_rxd_scan_rsp(peer, peer_addr_type)) {
            goto scan_rx_isr_exit;
        }

        /* Better not be a scan response pending */
        assert(scansm->scan_rsp_pending == 0);

        /* We want to send a request. See if backoff allows us */
        --scansm->backoff_count;
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
            ble_ll_scan_req_pdu_make(scansm, adv_addr, addr_type);
            rc = ble_phy_tx(scansm->scan_req_pdu, BLE_PHY_TRANSITION_TX_RX);

            if (rc == 0) {
                /* Set "waiting for scan response" flag */
                scansm->scan_rsp_pending = 1;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
                if (ble_hdr->rxinfo.channel <  BLE_PHY_NUM_DATA_CHANS) {
                    /* Let's keep the aux ptr as a reference to scan rsp */
                    scansm->cur_aux_data = ble_hdr->rxinfo.user_data;
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

    /*
     * If we timed out waiting for a response, the scan response pending
     * flag should be set. Deal with scan backoff. Put device back into rx.
     */
    scansm = &g_ble_ll_scan_sm;
    if (scansm->scan_rsp_pending) {
        ble_ll_scan_req_backoff(scansm, 0);
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        ble_ll_aux_scan_rsp_failed();
        ble_ll_scan_chk_resume();
#endif
    }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (scansm->cur_aux_data) {
        /*TODO handle chain incomplete, data truncated */
        ble_ll_scan_aux_data_free(scansm->cur_aux_data);
        scansm->cur_aux_data = NULL;
        STATS_INC(ble_ll_stats, aux_missed_adv);
        ble_ll_scan_chk_resume();
    }
#endif

    ble_phy_restart_rx();
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
void
ble_ll_scan_aux_data_free(struct ble_ll_aux_data *aux_scan)
{
    if (aux_scan) {
        os_memblock_put(&ext_adv_pool, aux_scan);
    }
}

static void
ble_ll_hci_send_ext_adv_report(uint8_t ptype, struct os_mbuf *om,
                               struct ble_mbuf_hdr *hdr)
{
    struct ble_ll_ext_adv *evt;
    int rc;

    if (!ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_EXT_ADV_RPT)) {
        return;
    }

    evt = ble_ll_scan_init_ext_adv();
    if (!evt) {
        return;
    }

    rc = ble_ll_scan_parse_ext_adv(om, hdr, evt);
    if (rc) {

        ble_hci_trans_buf_free((uint8_t *)evt);
        return;
    }

    evt->event_len = sizeof(struct ble_ll_ext_adv) + evt->adv_data_len;
    evt->rssi = hdr->rxinfo.rssi;

    ble_ll_hci_event_send((uint8_t *)evt);
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
    uint8_t scan_rsp_chk;
    struct ble_ll_scan_sm *scansm = &g_ble_ll_scan_sm;
    struct ble_mbuf_hdr *ble_hdr;
    int ext_adv_mode = -1;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    struct ble_ll_aux_data *aux_data;
#endif

    /* Set scan response check flag */
    scan_rsp_chk = BLE_MBUF_HDR_SCAN_RSP_RCV(hdr);

    /* We dont care about scan requests or connect requests */
    if (!BLE_MBUF_HDR_CRC_OK(hdr) || (ptype == BLE_ADV_PDU_TYPE_SCAN_REQ) ||
        (ptype == BLE_ADV_PDU_TYPE_CONNECT_REQ)) {
        goto scan_continue;
    }

    if (ble_ll_scan_adv_decode_addr(ptype, rxbuf, hdr,
                                    &adv_addr, &txadd,
                                    &init_addr, &init_addr_type,
                                    &ext_adv_mode)) {
       goto scan_continue;
    }

    /* Check the scanner filter policy */
    if (ble_ll_scan_chk_filter_policy(ptype, adv_addr, txadd, init_addr,
                                      init_addr_type,
                                      BLE_MBUF_HDR_DEVMATCH(hdr))) {
        goto scan_continue;
    }

    ident_addr = adv_addr;
    ident_addr_type = txadd;

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    index = scansm->scan_rpa_index;
    if (index >= 0) {
        ident_addr = g_ble_ll_resolv_list[index].rl_identity_addr;
        ident_addr_type = g_ble_ll_resolv_list[index].rl_addr_type;
    }
#endif

    /*
     * XXX: The BLE spec is a bit unclear here. What if we get a scan
     * response from an advertiser that we did not send a request to?
     * Do we send an advertising report? Do we add it to list of devices
     * that we have heard a scan response from?
     */
    if (ptype == BLE_ADV_PDU_TYPE_SCAN_RSP) {
        /*
         * If this is a scan response in reply to a request we sent we need
         * to store this advertiser's address so we dont send a request to it.
         */
        if (scansm->scan_rsp_pending && scan_rsp_chk) {
            /*
             * We could also check the timing of the scan reponse; make sure
             * that it is relatively close to the end of the scan request but
             * we wont for now.
             */
            ble_hdr = BLE_MBUF_HDR_PTR(scansm->scan_req_pdu);
            rxadd = ble_hdr->txinfo.hdr_byte & BLE_ADV_PDU_HDR_RXADD_MASK;
            adva = scansm->scan_req_pdu->om_data + BLE_DEV_ADDR_LEN;
            if (((txadd && rxadd) || ((txadd + rxadd) == 0)) &&
                !memcmp(adv_addr, adva, BLE_DEV_ADDR_LEN)) {
                /* We have received a scan response. Add to list */
                ble_ll_scan_add_scan_rsp_adv(ident_addr, ident_addr_type);

                /* Perform scan request backoff procedure */
                ble_ll_scan_req_backoff(scansm, 1);
            }
        } else {
            /* Ignore if this is not ours */
            goto scan_continue;
        }
    }

    /* Filter duplicates */
    if (scansm->scan_filt_dups) {
        if (ble_ll_scan_is_dup_adv(ptype, ident_addr_type, ident_addr)) {
            goto scan_continue;
        }
    }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (ptype == BLE_ADV_PDU_TYPE_ADV_EXT_IND) {
        if (!scansm->ext_scanning) {
            goto scan_continue;
        }

        if (BLE_MBUF_HDR_AUX_INVALID(hdr)) {
            goto scan_continue;
        }

        aux_data = hdr->rxinfo.user_data;

        /* Let's see if that packet contains aux ptr*/
        if (BLE_MBUF_HDR_WAIT_AUX(hdr)) {
            if (ble_ll_sched_aux_scan(hdr, scansm, hdr->rxinfo.user_data)) {
                /* We are here when could not schedule the aux ptr */
                hdr->rxinfo.flags &= ~BLE_MBUF_HDR_F_AUX_PTR_WAIT;
            }

            if (!BLE_LL_CHECK_AUX_FLAG(aux_data, BLE_LL_AUX_CHAIN_BIT)) {
                /* This is just beacon. Let's wait for more data */
                if (BLE_MBUF_HDR_WAIT_AUX(hdr)) {
                    /* If scheduled for aux let's don't remove aux data */
                    hdr->rxinfo.user_data = NULL;
                }
                goto scan_continue;
            }

            STATS_INC(ble_ll_stats, aux_chain_cnt);
        }

        ble_ll_hci_send_ext_adv_report(ptype, om, hdr);
        ble_ll_scan_switch_phy(scansm);

        if (BLE_MBUF_HDR_WAIT_AUX(hdr)) {
            /* If scheduled for aux let's don't remove aux data */
            hdr->rxinfo.user_data = NULL;
        }

        if (scansm->scan_rsp_pending) {
            if (!scan_rsp_chk) {
                return;
            }

            ble_ll_scan_req_backoff(scansm, 1);
        }

        goto scan_continue;
    }
#endif

    /* Send the advertising report */
    ble_ll_scan_send_adv_report(ptype, ident_addr_type, om, hdr, scansm);

scan_continue:

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        ble_ll_scan_aux_data_free(hdr->rxinfo.user_data);
#endif
    /*
     * If the scan response check bit is set and we are pending a response,
     * we have failed the scan request (as we would have reset the scan rsp
     * pending flag if we received a valid response
     */
    if (scansm->scan_rsp_pending && scan_rsp_chk) {
        ble_ll_scan_req_backoff(scansm, 0);
    }

    ble_ll_scan_chk_resume();
}

int
ble_ll_scan_set_scan_params(uint8_t *cmd)
{
    uint8_t scan_type;
    uint8_t own_addr_type;
    uint8_t filter_policy;
    uint16_t scan_itvl;
    uint16_t scan_window;
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_scan_params *scanp;

    /* If already enabled, we return an error */
    scansm = &g_ble_ll_scan_sm;
    if (scansm->scan_enabled) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* Get the scan interval and window */
    scan_type = cmd[0];
    scan_itvl  = get_le16(cmd + 1);
    scan_window = get_le16(cmd + 3);
    own_addr_type = cmd[5];
    filter_policy = cmd[6];

    /* Check scan type */
    if ((scan_type != BLE_HCI_SCAN_TYPE_PASSIVE) &&
        (scan_type != BLE_HCI_SCAN_TYPE_ACTIVE)) {
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
    if (own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check scanner filter policy */
    if (filter_policy > BLE_HCI_SCAN_FILT_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Store scan parameters */
    scanp = &g_ble_ll_scan_params[PHY_UNCODED];
    scanp->configured = 1;
    scanp->scan_type = scan_type;
    scanp->scan_itvl = scan_itvl;
    scanp->scan_window = scan_window;
    scanp->scan_filt_policy = filter_policy;
    scanp->own_addr_type = own_addr_type;
    
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
ble_ll_set_ext_scan_params(uint8_t *cmd)
{
    struct ble_ll_scan_params new_params[BLE_LL_SCAN_PHY_NUMBER] = { };
    struct ble_ll_scan_params *uncoded = &new_params[PHY_UNCODED];
    struct ble_ll_scan_params *coded = &new_params[PHY_CODED];
    uint8_t idx;
    int rc;

    /* If already enabled, we return an error */
    if (g_ble_ll_scan_sm.scan_enabled) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* Check own addr type */
    if (cmd[0] > BLE_HCI_ADV_OWN_ADDR_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    coded->own_addr_type = cmd[0];
    uncoded->own_addr_type = cmd[0];

    /* Check scanner filter policy */
    if (cmd[1] > BLE_HCI_SCAN_FILT_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    coded->scan_filt_policy = cmd[1];
    uncoded->scan_filt_policy = cmd[1];

    if (!(cmd[2] & ble_ll_valid_scan_phy_mask)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    idx = 3;
    if (cmd[2] & BLE_HCI_LE_PHY_1M_PREF_MASK) {
        uncoded->scan_type = cmd[idx];
        idx++;
        uncoded->scan_itvl = get_le16(cmd + idx);
        idx += 2;
        uncoded->scan_window = get_le16(cmd + idx);
        idx += 2;

        rc = ble_ll_check_scan_params(uncoded->scan_type,
                                      uncoded->scan_itvl,
                                      uncoded->scan_window);
        if (rc) {
            return rc;
        }

        /* That means user whats to use this PHY for scanning */
        uncoded->configured = 1;
    }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_CODED_PHY)
    if (cmd[2] & BLE_HCI_LE_PHY_CODED_PREF_MASK) {
        coded->scan_type = cmd[idx];
        idx++;
        coded->scan_itvl = get_le16(cmd + idx);
        idx += 2;
        coded->scan_window = get_le16(cmd + idx);
        idx += 2;

        rc = ble_ll_check_scan_params(coded->scan_type,
                                      coded->scan_itvl,
                                      coded->scan_window);
        if (rc) {
            return rc;
        }

        /* That means user whats to use this PHY for scanning */
        coded->configured = 1;
    }
#endif

    /* For now we don't accept request for continuous scan if 2 PHYs are
     * requested.
     */
    if ((cmd[2] ==
            (BLE_HCI_LE_PHY_1M_PREF_MASK | BLE_HCI_LE_PHY_CODED_PREF_MASK)) &&
                ((uncoded->scan_itvl == uncoded->scan_window) ||
                (coded->scan_itvl == coded-> scan_window))) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    memcpy(g_ble_ll_scan_params, new_params, sizeof(new_params));

    return 0;
}

#endif

/**
 * ble ll scan set enable
 *
 *  HCI scan set enable command processing function
 *
 *  Context: Link Layer task (HCI Command parser).
 *
 * @param cmd Pointer to command buffer
 *
 * @return int BLE error code.
 */
int
ble_ll_scan_set_enable(uint8_t *cmd, uint8_t ext)
{
    int rc;
    uint8_t filter_dups;
    uint8_t enable;
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_scan_params *scanp;
    struct ble_ll_scan_params *scanphy;
    uint16_t dur;
    uint16_t period;
    int i;

    /* Check for valid parameters */
    enable = cmd[0];
    filter_dups = cmd[1];
    if ((filter_dups > 1) || (enable > 1)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    scansm = &g_ble_ll_scan_sm;

    scansm->ext_scanning = ext;

    if (ext) {
        /* TODO: Mark that extended scan is ongoing in order to send
        * correct advertising report
        */

        dur = get_le16(cmd + 2);
        period = get_le16(cmd + 4);

        /* TODO Use period and duration */
        (void) dur;
        (void) period;
    }

    rc = BLE_ERR_SUCCESS;
    if (enable) {
        /* If already enabled, do nothing */
        if (!scansm->scan_enabled) {

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
                scansm->scan_filt_dups = filter_dups;

                if (scansm->cur_phy == PHY_NOT_CONFIGURED) {
                    scansm->cur_phy = i;
                } else {
                    scansm->next_phy = i;
                }
            }

            rc = ble_ll_scan_sm_start(scansm);
        } else {
            /* Controller does not allow initiating and scanning.*/
            for (i = 0; i < BLE_LL_SCAN_PHY_NUMBER; i++) {
                scanphy = &scansm->phy_data[i];
                if (scanphy->configured &&
                        scanphy->scan_type == BLE_SCAN_TYPE_INITIATE) {
                        rc = BLE_ERR_CMD_DISALLOWED;
                        break;
                }
            }
        }
    } else {
        if (scansm->scan_enabled) {
            ble_ll_scan_sm_stop(1);
        }
    }

    return rc;
}

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
    scansm->ext_scanning = 0;
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
    struct ble_ll_scan_params *scanphy;
    struct hci_ext_conn_params *params;
    int rc;

    scansm = &g_ble_ll_scan_sm;
    scansm->own_addr_type = hcc->own_addr_type;
    scansm->cur_phy = PHY_NOT_CONFIGURED;
    scansm->next_phy = PHY_NOT_CONFIGURED;
    scansm->ext_scanning = 1;

    if (hcc->init_phy_mask & BLE_PHY_MASK_1M) {
        params = &hcc->params[0];
        scanphy = &scansm->phy_data[PHY_UNCODED];

        scanphy->scan_itvl = params->scan_itvl;
        scanphy->scan_window = params->scan_window;
        scanphy->scan_type = BLE_SCAN_TYPE_INITIATE;
        scanphy->scan_filt_policy = hcc->filter_policy;
        scansm->cur_phy = PHY_UNCODED;
    }

    if (hcc->init_phy_mask & BLE_PHY_MASK_CODED) {
        params = &hcc->params[2];
        scanphy = &scansm->phy_data[PHY_CODED];

        scanphy->scan_itvl = params->scan_itvl;
        scanphy->scan_window = params->scan_window;
        scanphy->scan_type = BLE_SCAN_TYPE_INITIATE;
        scanphy->scan_filt_policy = hcc->filter_policy;
        if (scansm->cur_phy == PHY_NOT_CONFIGURED) {
            scansm->cur_phy = PHY_CODED;
        } else {
            scansm->next_phy = PHY_CODED;
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
    uint8_t *rpa;
    struct ble_ll_scan_sm *scansm;

    scansm = &g_ble_ll_scan_sm;

    /*
     * The RPA we used is in connect request or scan request and is the
     * first address in the packet
     */
    rpa = scansm->scan_req_pdu->om_data;

    return rpa;
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

/* Returns the PDU allocated by the scanner */
struct os_mbuf *
ble_ll_scan_get_pdu(void)
{
    return g_ble_ll_scan_sm.scan_req_pdu;
}

/* Returns true if whitelist is enabled for scanning */
int
ble_ll_scan_whitelist_enabled(void)
{
    struct ble_ll_scan_params *params;

    params = &g_ble_ll_scan_sm.phy_data[g_ble_ll_scan_sm.cur_phy];
    return params->scan_filt_policy & 1;
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

    /* If enabled, stop it. */
    scansm = &g_ble_ll_scan_sm;
    if (scansm->scan_enabled) {
        ble_ll_scan_sm_stop(0);
    }

    /* Free the scan request pdu */
    os_mbuf_free_chain(scansm->scan_req_pdu);

    /* Reset duplicate advertisers and those from which we rxd a response */
    g_ble_ll_scan_num_rsp_advs = 0;
    memset(&g_ble_ll_scan_rsp_advs[0], 0, sizeof(g_ble_ll_scan_rsp_advs));

    g_ble_ll_scan_num_dup_advs = 0;
    memset(&g_ble_ll_scan_dup_advs[0], 0, sizeof(g_ble_ll_scan_dup_advs));

    /* Call the init function again */
    ble_ll_scan_init();
}

/**
 * ble ll scan init
 *
 * Initialize a scanner. Must be called before scanning can be started.
 * Expected to be called with a un-initialized or reset scanning state machine.
 */
void
ble_ll_scan_init(void)
{
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_scan_params *scanp;
    int i;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    os_error_t err;
#endif

    /* Clear state machine in case re-initialized */
    scansm = &g_ble_ll_scan_sm;
    memset(scansm, 0, sizeof(struct ble_ll_scan_sm));

    /* Clear scan parameters in case re-initialized */
    memset(g_ble_ll_scan_params, 0, sizeof(g_ble_ll_scan_params));

    /* Initialize scanning window end event */
    scansm->scan_sched_ev.ev_cb = ble_ll_scan_event_proc;
    scansm->scan_sched_ev.ev_arg = scansm;

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

    /* Initialize scanning timer */
    os_cputime_timer_init(&scansm->scan_timer, ble_ll_scan_timer_cb, scansm);

    /* Get a scan request mbuf (packet header) and attach to state machine */
    scansm->scan_req_pdu = os_msys_get_pkthdr(BLE_SCAN_LEGACY_MAX_PKT_LEN,
                                              sizeof(struct ble_mbuf_hdr));
    assert(scansm->scan_req_pdu != NULL);

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    err = os_mempool_init(&ext_adv_pool,
                          MYNEWT_VAL(BLE_LL_EXT_ADV_AUX_PTR_CNT),
                          sizeof (struct ble_ll_aux_data),
                          ext_adv_mem,
                          "ble_ll_aux_scan_pool");
    assert(err == 0);
#endif
}
