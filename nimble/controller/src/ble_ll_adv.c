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
#include "os/os.h"
#include "os/os_cputime.h"
#include "bsp/bsp.h"
#include "ble/xcvr.h"
#include "nimble/ble.h"
#include "nimble/nimble_opt.h"
#include "nimble/hci_common.h"
#include "nimble/ble_hci_trans.h"
#include "controller/ble_phy.h"
#include "controller/ble_hw.h"
#include "controller/ble_ll.h"
#include "controller/ble_ll_hci.h"
#include "controller/ble_ll_adv.h"
#include "controller/ble_ll_sched.h"
#include "controller/ble_ll_scan.h"
#include "controller/ble_ll_whitelist.h"
#include "controller/ble_ll_resolv.h"
#include "ble_ll_conn_priv.h"
#include "hal/hal_gpio.h"

/* XXX: TODO
 * 1) Need to look at advertising and scan request PDUs. Do I allocate these
 * once? Do I use a different pool for smaller ones? Do I statically declare
 * them?
 * 3) How do features get supported? What happens if device does not support
 * advertising? (for example)
 * 4) How to determine the advertising interval we will actually use. As of
 * now, we set it to max.
 * 5) How does the advertising channel tx power get set? I dont implement
 * that currently.
 */

/*
 * Advertising state machine
 *
 * The advertising state machine data structure.
 *
 *  adv_pdu_len
 *      The length of the advertising PDU that will be sent. This does not
 *      include the preamble, access address and CRC.
 *
 *  initiator_addr:
 *      This is the address that we send in directed advertisements (the
 *      INITA field). If we are using Privacy this is a RPA that we need to
 *      generate. We reserve space in the advsm to save time when creating
 *      the ADV_DIRECT_IND. If own address type is not 2 or 3, this is simply
 *      the peer address from the set advertising parameters.
 */
struct ble_ll_adv_sm
{
    uint8_t adv_enabled;
    uint8_t adv_instance;
    uint8_t adv_len;
    uint8_t adv_chanmask;
    uint8_t adv_filter_policy;
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint8_t adv_chan;
    uint8_t scan_rsp_len;
    uint8_t adv_pdu_len;
    int8_t adv_rpa_index;
    uint8_t flags;
    int8_t adv_txpwr;
    uint16_t props;
    uint16_t adv_itvl_min;
    uint16_t adv_itvl_max;
    uint32_t adv_itvl_usecs;
    uint32_t adv_event_start_time;
    uint32_t adv_pdu_start_time;
    uint32_t adv_end_time;
    uint32_t adv_rpa_timer;
    uint8_t adva[BLE_DEV_ADDR_LEN];
    uint8_t adv_rpa[BLE_DEV_ADDR_LEN];
    uint8_t peer_addr[BLE_DEV_ADDR_LEN];
    uint8_t initiator_addr[BLE_DEV_ADDR_LEN];
    uint8_t adv_data[BLE_ADV_DATA_MAX_LEN];
    uint8_t scan_rsp_data[BLE_SCAN_RSP_DATA_MAX_LEN];
    uint8_t *conn_comp_ev;
    struct os_event adv_txdone_ev;
    struct ble_ll_sched_item adv_sch;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    uint32_t adv_secondary_start_time;
    struct ble_ll_sched_item adv_secondary_sch;
    struct os_event adv_sec_txdone_ev;
    uint16_t duration;
    uint16_t adi;
    uint8_t adv_secondary_chan;
    uint8_t adv_random_addr[BLE_DEV_ADDR_LEN];
    uint8_t events_max;
    uint8_t events;
    uint8_t pri_phy;
    uint8_t sec_phy;
#endif
};

#define BLE_LL_ADV_SM_FLAG_TX_ADD               0x01
#define BLE_LL_ADV_SM_FLAG_RX_ADD               0x02
#define BLE_LL_ADV_SM_FLAG_SCAN_REQ_NOTIF       0x04
#define BLE_LL_ADV_SM_FLAG_CONN_RSP_TXD         0x08
#define BLE_LL_ADV_SM_FLAG_ACTIVE_CHANSET_MASK  0x30 /* use helpers! */

static inline int
ble_ll_adv_active_chanset_is_pri(struct ble_ll_adv_sm *advsm)
{
    return (advsm->flags & BLE_LL_ADV_SM_FLAG_ACTIVE_CHANSET_MASK) == 0x10;
}

static inline int
ble_ll_adv_active_chanset_is_sec(struct ble_ll_adv_sm *advsm)
{
    return (advsm->flags & BLE_LL_ADV_SM_FLAG_ACTIVE_CHANSET_MASK) == 0x20;
}

static inline void
ble_ll_adv_active_chanset_clear(struct ble_ll_adv_sm *advsm)
{
    advsm->flags &= ~BLE_LL_ADV_SM_FLAG_ACTIVE_CHANSET_MASK;
}

static inline void
ble_ll_adv_active_chanset_set_pri(struct ble_ll_adv_sm *advsm)
{
    assert((advsm->flags & BLE_LL_ADV_SM_FLAG_ACTIVE_CHANSET_MASK) == 0);
    advsm->flags &= ~BLE_LL_ADV_SM_FLAG_ACTIVE_CHANSET_MASK;
    advsm->flags |= 0x10;
}

static inline void
ble_ll_adv_active_chanset_set_sec(struct ble_ll_adv_sm *advsm)
{
    assert((advsm->flags & BLE_LL_ADV_SM_FLAG_ACTIVE_CHANSET_MASK) == 0);
    advsm->flags &= ~BLE_LL_ADV_SM_FLAG_ACTIVE_CHANSET_MASK;
    advsm->flags |= 0x20;
}

/* The advertising state machine global object */
struct ble_ll_adv_sm g_ble_ll_adv_sm[BLE_ADV_INSTANCES];
struct ble_ll_adv_sm *g_ble_ll_cur_adv_sm;

static void ble_ll_adv_make_done(struct ble_ll_adv_sm *advsm, struct ble_mbuf_hdr *hdr);
static void ble_ll_adv_sm_init(struct ble_ll_adv_sm *advsm);

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
/**
 * Called to change advertisers ADVA and INITA (for directed advertisements)
 * as an advertiser needs to adhere to the resolvable private address generation
 * timer.
 *
 * NOTE: the resolvable private address code uses its own timer to regenerate
 * local resolvable private addresses. The advertising code uses its own
 * timer to reset the INITA (for directed advertisements). This code also sets
 * the appropriate txadd and rxadd bits that will go into the advertisement.
 *
 * Another thing to note: it is possible that an IRK is all zeroes in the
 * resolving list. That is why we need to check if the generated address is
 * in fact a RPA as a resolving list entry with all zeroes will use the
 * identity address (which may be a private address or public).
 *
 * @param advsm
 */
void
ble_ll_adv_chk_rpa_timeout(struct ble_ll_adv_sm *advsm)
{
    uint32_t now;

    if (advsm->own_addr_type > BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        now = os_time_get();
        if ((int32_t)(now - advsm->adv_rpa_timer) >= 0) {
            ble_ll_resolv_gen_rpa(advsm->peer_addr, advsm->peer_addr_type,
                                  advsm->adva, 1);

            if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_DIRECTED) {
                ble_ll_resolv_gen_rpa(advsm->peer_addr, advsm->peer_addr_type,
                                      advsm->initiator_addr, 0);
                if (ble_ll_is_rpa(advsm->initiator_addr, 1)) {
                    advsm->flags |= BLE_LL_ADV_SM_FLAG_RX_ADD;
                } else {
                    if (advsm->own_addr_type & 1) {
                        advsm->flags |= BLE_LL_ADV_SM_FLAG_RX_ADD;
                    } else {
                        advsm->flags &= ~BLE_LL_ADV_SM_FLAG_RX_ADD;
                    }
                }
            }
            advsm->adv_rpa_timer = now + ble_ll_resolv_get_rpa_tmo();

            /* May have to reset txadd bit */
            if (ble_ll_is_rpa(advsm->adva, 1)) {
                advsm->flags |= BLE_LL_ADV_SM_FLAG_TX_ADD;
            } else {
                if (advsm->own_addr_type & 1) {
                    advsm->flags |= BLE_LL_ADV_SM_FLAG_TX_ADD;
                } else {
                    advsm->flags &= ~BLE_LL_ADV_SM_FLAG_TX_ADD;
                }
            }
        }
    }
}
#endif

/**
 * Calculate the first channel that we should advertise upon when we start
 * an advertising event.
 *
 * @param advsm
 *
 * @return uint8_t The number of the first channel usable for advertising.
 */
static uint8_t
ble_ll_adv_first_chan(struct ble_ll_adv_sm *advsm)
{
    uint8_t adv_chan;

    /* Set first advertising channel */
    if (advsm->adv_chanmask & 0x01) {
        adv_chan = BLE_PHY_ADV_CHAN_START;
    } else if (advsm->adv_chanmask & 0x02) {
        adv_chan = BLE_PHY_ADV_CHAN_START + 1;
    } else {
        adv_chan = BLE_PHY_ADV_CHAN_START + 2;
    }

    return adv_chan;
}

/**
 * Calculate the final channel that we should advertise upon when we start
 * an advertising event.
 *
 * @param advsm
 *
 * @return uint8_t The number of the final channel usable for advertising.
 */
static uint8_t
ble_ll_adv_final_chan(struct ble_ll_adv_sm *advsm)
{
    uint8_t adv_chan;

    if (advsm->adv_chanmask & 0x04) {
        adv_chan = BLE_PHY_ADV_CHAN_START + 2;
    } else if (advsm->adv_chanmask & 0x02) {
        adv_chan = BLE_PHY_ADV_CHAN_START + 1;
    } else {
        adv_chan = BLE_PHY_ADV_CHAN_START;
    }

    return adv_chan;
}

/**
 * Create the advertising legacy PDU
 *
 * @param advsm Pointer to advertisement state machine
 */
static void
ble_ll_adv_legacy_pdu_make(struct ble_ll_adv_sm *advsm, struct os_mbuf *m)
{
    uint8_t     adv_data_len;
    uint8_t     *dptr;
    uint8_t     pdulen;
    uint8_t     pdu_type;

    /* assume this is not a direct ind */
    adv_data_len = advsm->adv_len;
    pdulen = BLE_DEV_ADDR_LEN + adv_data_len;

    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_DIRECTED) {
        pdu_type = BLE_ADV_PDU_TYPE_ADV_DIRECT_IND;

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_CSA2) == 1)
        pdu_type |= BLE_ADV_PDU_HDR_CHSEL;
#endif

        if (advsm->flags & BLE_LL_ADV_SM_FLAG_RX_ADD) {
            pdu_type |= BLE_ADV_PDU_HDR_RXADD_RAND;
        }

        adv_data_len = 0;
        pdulen = BLE_ADV_DIRECT_IND_LEN;
    } else if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_CONNECTABLE) {
        pdu_type = BLE_ADV_PDU_TYPE_ADV_IND;

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_CSA2) == 1)
        pdu_type |= BLE_ADV_PDU_HDR_CHSEL;
#endif
    } else if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_SCANNABLE) {
        pdu_type = BLE_ADV_PDU_TYPE_ADV_SCAN_IND;
    } else {
        pdu_type = BLE_ADV_PDU_TYPE_ADV_NONCONN_IND;
    }

    /* An invalid advertising data length indicates a memory overwrite */
    assert(adv_data_len <= BLE_ADV_LEGACY_DATA_MAX_LEN);

    /* Set the PDU length in the state machine (includes header) */
    advsm->adv_pdu_len = pdulen + BLE_LL_PDU_HDR_LEN;

    /* Set TxAdd to random if needed. */
    if (advsm->flags & BLE_LL_ADV_SM_FLAG_TX_ADD) {
        pdu_type |= BLE_ADV_PDU_HDR_TXADD_RAND;
    }

    /* Get the advertising PDU and initialize it*/
    ble_ll_mbuf_init(m, pdulen, pdu_type);

    /* Construct advertisement */
    dptr = m->om_data;
    memcpy(dptr, advsm->adva, BLE_DEV_ADDR_LEN);
    dptr += BLE_DEV_ADDR_LEN;

    /* For ADV_DIRECT_IND add inita */
    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_DIRECTED) {
        memcpy(dptr, advsm->initiator_addr, BLE_DEV_ADDR_LEN);
    }

    /* Copy in advertising data, if any */
    if (adv_data_len != 0) {
        memcpy(dptr, advsm->adv_data, adv_data_len);
    }
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
/* TODO this shouldn't be needed
 *
 * PDU could be constructed before scheduling and held in mbuf until
 * transmission
 *
 */
static uint8_t
ble_ll_adv_secondary_pdu_len(struct ble_ll_adv_sm *advsm)
{
    uint8_t pdulen;

    pdulen = 2 + BLE_LL_PDU_HDR_LEN + BLE_LL_EXT_ADV_DATA_INFO_SIZE;

    if (!(advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_SCANNABLE) &&
            advsm->adv_len) {
        pdulen += advsm->adv_len;
    }

    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_INC_TX_PWR) {
        pdulen += BLE_LL_EXT_ADV_TX_POWER_SIZE;
    }

    if ((advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_DIRECTED) ||
            (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_CONNECTABLE)) {
        pdulen += BLE_LL_EXT_ADV_TARGETA_SIZE;
    }

    return pdulen;
}

static uint8_t
ble_ll_adv_aux_scan_rsp_len(struct ble_ll_adv_sm *advsm)
{
    uint8_t pdulen;

    pdulen = 2 + BLE_LL_PDU_HDR_LEN + BLE_LL_EXT_ADV_ADVA_SIZE;

    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_INC_TX_PWR) {
        pdulen += BLE_LL_EXT_ADV_TX_POWER_SIZE;
    }

    pdulen += advsm->scan_rsp_len;

    return pdulen;
}

/**
 * Create the advertising PDU
 */
static void
ble_ll_adv_pdu_make(struct ble_ll_adv_sm *advsm, struct os_mbuf *m)
{
    uint8_t *dptr;
    uint8_t pdulen;
    uint8_t pdu_type;

    bool adva = false;
    bool targeta = false;
    bool adi = false;
    bool aux_ptr = false;
    bool tx_power = false;
    bool adv_data = false;
    uint8_t adv_mode;
    uint8_t ext_hdr_len;
    uint8_t ext_hdr_flags;
    uint32_t offset;

    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
        return ble_ll_adv_legacy_pdu_make(advsm, m);
    }

    pdulen = 1; /* ext hdr len + AdvMode */

    ext_hdr_len = 0;

    /* TODO for now always add flags */
    ext_hdr_len += 1;
    ext_hdr_flags = 0;


    if (ble_ll_adv_active_chanset_is_sec(advsm)) {
        pdu_type = BLE_ADV_PDU_TYPE_AUX_ADV_IND;

        adi = true;
        adva = true;

        if (!(advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_SCANNABLE) &&
                advsm->adv_len) {
            adv_data = true;
        }

        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_INC_TX_PWR) {
            tx_power = true;
        }

        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_DIRECTED) {
            targeta = true;
        }

    } else {
        assert(ble_ll_adv_active_chanset_is_pri(advsm));

        /* only ADV_EXT_IND goes on primary advertising channels */
        pdu_type = BLE_ADV_PDU_TYPE_ADV_EXT_IND;

        /* TODO in some cases we could avoid auxiliary packet */
        aux_ptr = true;
        adi = true;
    }

    adv_mode = 0;
    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_CONNECTABLE) {
        adv_mode |= BLE_LL_EXT_ADV_MODE_CONN;
    }

    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_SCANNABLE) {
            adv_mode |= BLE_LL_EXT_ADV_MODE_SCAN;
    }

    if (adva) {
        ext_hdr_flags |= (1 << BLE_LL_EXT_ADV_ADVA_BIT);
        ext_hdr_len += BLE_LL_EXT_ADV_ADVA_SIZE;
    }

    if (targeta) {
        ext_hdr_flags |= (1 << BLE_LL_EXT_ADV_TARGETA_BIT);
        ext_hdr_len += BLE_LL_EXT_ADV_TARGETA_SIZE;
    }

    if (adi) {
        ext_hdr_flags |= (1 << BLE_LL_EXT_ADV_DATA_INFO_BIT);
        ext_hdr_len += BLE_LL_EXT_ADV_DATA_INFO_SIZE;
    }

    if (aux_ptr) {
        ext_hdr_flags |= (1 << BLE_LL_EXT_ADV_AUX_PTR_BIT);
        ext_hdr_len += BLE_LL_EXT_ADV_AUX_PTR_SIZE;
    }

    if (tx_power) {
        ext_hdr_flags |= (1 << BLE_LL_EXT_ADV_TX_POWER_BIT);
        ext_hdr_len += BLE_LL_EXT_ADV_TX_POWER_SIZE;
    }

    /* TODO ACAD */

    if (adv_data) {
        pdulen += advsm->adv_len;
    }

    /* Set TxAdd to random if needed. */
    if (advsm->flags & BLE_LL_ADV_SM_FLAG_TX_ADD) {
        pdu_type |= BLE_ADV_PDU_HDR_TXADD_RAND;
    }

    pdulen += ext_hdr_len;

    /* Set the PDU length in the state machine (includes header) */
    advsm->adv_pdu_len = pdulen + BLE_LL_PDU_HDR_LEN;

    ble_ll_mbuf_init(m, pdulen, pdu_type);

    /* Construct advertisement */
    dptr = m->om_data;

    /* ext hdr len and adv mode */
    dptr[0] = ext_hdr_len | (adv_mode << 6);
    dptr += 1;

    /* ext hdr flags */
    dptr[0] = ext_hdr_flags;
    dptr += 1;

    if (adva) {
        memcpy(dptr, advsm->adva, BLE_LL_EXT_ADV_ADVA_SIZE);
        dptr += BLE_LL_EXT_ADV_ADVA_SIZE;
    }

    if (targeta) {
        memcpy(dptr, advsm->initiator_addr, BLE_LL_EXT_ADV_TARGETA_SIZE);
        dptr += BLE_LL_EXT_ADV_TARGETA_SIZE;
    }

    if (adi) {
        dptr[0] = advsm->adi & 0x00ff;
        dptr[1] = advsm->adi >> 8;
        dptr += BLE_LL_EXT_ADV_DATA_INFO_SIZE;
    }

    if (aux_ptr) {
        offset = advsm->adv_secondary_start_time - advsm->adv_pdu_start_time;

        /* in usecs */
        offset = os_cputime_ticks_to_usecs(offset);

        dptr[0] = advsm->adv_secondary_chan;

        if (offset > 245700) {
            dptr[0] |= 0x80;
            offset = offset / 300;
        } else {
            offset = offset / 30;
        }

        dptr[1] = (offset & 0x000000ff);
        dptr[2] = ((offset >> 8) & 0x0000001f) | (advsm->sec_phy - 1) << 5; //TODO;

        dptr += BLE_LL_EXT_ADV_AUX_PTR_SIZE;
    }

    if (tx_power) {
        dptr[0] = advsm->adv_txpwr;
        dptr += BLE_LL_EXT_ADV_TX_POWER_SIZE;
    }

    if (adv_data) {
        memcpy(dptr, advsm->adv_data, advsm->adv_len);
        dptr += advsm->adv_len;
    }
}
#endif

static struct os_mbuf *
ble_ll_adv_scan_rsp_legacy_pdu_make(struct ble_ll_adv_sm *advsm)
{
    uint8_t     scan_rsp_len;
    uint8_t     *dptr;
    uint8_t     pdulen;
    uint8_t     hdr;
    struct os_mbuf *m;

    /* Obtain scan response buffer */
    m = os_msys_get_pkthdr(BLE_SCAN_RSP_LEGACY_DATA_MAX_LEN + BLE_DEV_ADDR_LEN,
                           sizeof(struct ble_mbuf_hdr));
    if (!m) {
        return NULL;
    }

    /* Make sure that the length is valid */
    scan_rsp_len = advsm->scan_rsp_len;
    assert(scan_rsp_len <= BLE_SCAN_RSP_LEGACY_DATA_MAX_LEN);

    /* Set BLE transmit header */
    pdulen = BLE_DEV_ADDR_LEN + scan_rsp_len;
    hdr = BLE_ADV_PDU_TYPE_SCAN_RSP;
    if (advsm->flags & BLE_LL_ADV_SM_FLAG_TX_ADD) {
        hdr |= BLE_ADV_PDU_HDR_TXADD_RAND;
    }

    ble_ll_mbuf_init(m, pdulen, hdr);

    /*
     * The adva in this packet will be the same one that was being advertised
     * and is based on the peer identity address in the set advertising
     * parameters. If a different peer sends us a scan request (for some reason)
     * we will reply with an adva that was not generated based on the local irk
     * of the peer sending the scan request.
     */

    /* Construct scan response */
    dptr = m->om_data;
    memcpy(dptr, advsm->adva, BLE_DEV_ADDR_LEN);
    if (scan_rsp_len != 0) {
        memcpy(dptr + BLE_DEV_ADDR_LEN, advsm->scan_rsp_data, scan_rsp_len);
    }

    return m;
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
/**
 * Create a scan response PDU
 *
 * @param advsm
 */
static struct os_mbuf *
ble_ll_adv_scan_rsp_pdu_make(struct ble_ll_adv_sm *advsm)
{
    uint8_t     *dptr;
    uint8_t     pdulen;
    uint8_t     ext_hdr_len;
    uint8_t     ext_hdr_flags;
    uint8_t     hdr;
    struct os_mbuf *m;

    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
        return ble_ll_adv_scan_rsp_legacy_pdu_make(advsm);
    }

    /* ext hdr len + SCAN_RSP */
    pdulen = 1 + advsm->scan_rsp_len;

    /* flags, adva and optional TX power */
    ext_hdr_len = 1 + BLE_LL_EXT_ADV_ADVA_SIZE;
    ext_hdr_flags = (1 << BLE_LL_EXT_ADV_ADVA_BIT);

    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_INC_TX_PWR) {
        ext_hdr_len += BLE_LL_EXT_ADV_TX_POWER_SIZE;
        ext_hdr_len += BLE_LL_EXT_ADV_TX_POWER_SIZE;
    }

    pdulen += ext_hdr_len;

    /* Obtain scan response buffer */
    m = os_msys_get_pkthdr(pdulen, sizeof(struct ble_mbuf_hdr));
    if (!m) {
        return NULL;
    }

    /* Set BLE transmit header */
    hdr = BLE_ADV_PDU_TYPE_AUX_SCAN_RSP;
    if (advsm->flags & BLE_LL_ADV_SM_FLAG_TX_ADD) {
        hdr |= BLE_ADV_PDU_HDR_TXADD_RAND;
    }

    ble_ll_mbuf_init(m, pdulen, hdr);

    /* Construct scan response */
    dptr = m->om_data;

    /* ext hdr len and adv mode (00b) */
    dptr[0] = ext_hdr_len;
    dptr += 1;

    /* ext hdr flags */
    dptr[0] = ext_hdr_flags;
    dptr += 1;

    /*
     * The adva in this packet will be the same one that was being advertised
     * and is based on the peer identity address in the set advertising
     * parameters. If a different peer sends us a scan request (for some reason)
     * we will reply with an adva that was not generated based on the local irk
     * of the peer sending the scan request.
     */
    memcpy(dptr, advsm->adva, BLE_LL_EXT_ADV_ADVA_SIZE);
    dptr += BLE_LL_EXT_ADV_ADVA_SIZE;

    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_INC_TX_PWR) {
        dptr[0] = advsm->adv_txpwr;
        dptr += BLE_LL_EXT_ADV_TX_POWER_SIZE;
    }

    memcpy(dptr, advsm->scan_rsp_data, advsm->scan_rsp_len);
    dptr += advsm->scan_rsp_len;

    return m;
}

/**
 * Create a AUX connect response PDU
 *
 * @param advsm
 */
static struct os_mbuf *
ble_ll_adv_aux_conn_rsp_pdu_make(struct ble_ll_adv_sm *advsm, uint8_t *peer,
                                 uint8_t rxadd)
{
    uint8_t     *dptr;
    uint8_t     pdulen;
    uint8_t     ext_hdr_len;
    uint8_t     ext_hdr_flags;
    uint8_t     hdr;
    struct os_mbuf *m;

    /* ext hdr len */
    pdulen = 1;

    /* flags,AdvA and TargetA */
    ext_hdr_len = 1 + BLE_LL_EXT_ADV_ADVA_SIZE + BLE_LL_EXT_ADV_TARGETA_SIZE;
    ext_hdr_flags = (1 << BLE_LL_EXT_ADV_ADVA_BIT);
    ext_hdr_flags |= (1 << BLE_LL_EXT_ADV_TARGETA_BIT);

    pdulen += ext_hdr_len;

    /* Obtain scan response buffer */
    m = os_msys_get_pkthdr(pdulen, sizeof(struct ble_mbuf_hdr));
    if (!m) {
        return NULL;
    }

    /* Set BLE transmit header */
    hdr = BLE_ADV_PDU_TYPE_AUX_CONNECT_RSP | rxadd;
    if (advsm->flags & BLE_LL_ADV_SM_FLAG_TX_ADD) {
        hdr |= BLE_ADV_PDU_HDR_TXADD_MASK;
    }

    ble_ll_mbuf_init(m, pdulen, hdr);

    /* Construct connect response */
    dptr = m->om_data;

    /* ext hdr len and adv mode (00b) */
    dptr[0] = ext_hdr_len;
    dptr += 1;

    /* ext hdr flags */
    dptr[0] = ext_hdr_flags;
    dptr += 1;

    memcpy(dptr, advsm->adva, BLE_LL_EXT_ADV_ADVA_SIZE);
    dptr += BLE_LL_EXT_ADV_ADVA_SIZE;

    memcpy(dptr, peer, BLE_LL_EXT_ADV_TARGETA_SIZE);
    dptr += BLE_LL_EXT_ADV_ADVA_SIZE;

    return m;
}
#endif

/**
 * Called to indicate the advertising event is over.
 *
 * Context: Interrupt
 *
 * @param advsm
 *
 */
static void
ble_ll_adv_tx_done(void *arg)
{
    struct ble_ll_adv_sm *advsm;

    /* XXX: for now, reset power to max after advertising */
    ble_phy_txpwr_set(MYNEWT_VAL(BLE_LL_TX_PWR_DBM));

    advsm = (struct ble_ll_adv_sm *)arg;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (ble_ll_adv_active_chanset_is_pri(advsm)) {
        os_eventq_put(&g_ble_ll_data.ll_evq, &advsm->adv_txdone_ev);
    } else if (ble_ll_adv_active_chanset_is_sec(advsm)) {
        os_eventq_put(&g_ble_ll_data.ll_evq, &advsm->adv_sec_txdone_ev);
    } else {
        assert(0);
    }
#else
    assert(ble_ll_adv_active_chanset_is_pri(advsm));
    os_eventq_put(&g_ble_ll_data.ll_evq, &advsm->adv_txdone_ev);
#endif

    ble_ll_log(BLE_LL_LOG_ID_ADV_TXDONE, ble_ll_state_get(),
               advsm->adv_instance, 0);

    ble_ll_state_set(BLE_LL_STATE_STANDBY);

    ble_ll_adv_active_chanset_clear(advsm);

    /* We no longer have a current state machine */
    g_ble_ll_cur_adv_sm = NULL;
}

/*
 * Called when an advertising event has been removed from the scheduler
 * without being run.
 */
void
ble_ll_adv_event_rmvd_from_sched(struct ble_ll_adv_sm *advsm)
{
    /*
     * Need to set advertising channel to final chan so new event gets
     * scheduled.
     */
    advsm->adv_chan = ble_ll_adv_final_chan(advsm);
    os_eventq_put(&g_ble_ll_data.ll_evq, &advsm->adv_txdone_ev);
}

/**
 * This is the scheduler callback (called from interrupt context) which
 * transmits an advertisement.
 *
 * Context: Interrupt (scheduler)
 *
 * @param sch
 *
 * @return int
 */
static int
ble_ll_adv_tx_start_cb(struct ble_ll_sched_item *sch)
{
    int rc;
    uint8_t end_trans;
    uint32_t txstart;
    struct ble_ll_adv_sm *advsm;
    struct os_mbuf *adv_pdu;

    /* Get the state machine for the event */
    advsm = (struct ble_ll_adv_sm *)sch->cb_arg;

    /* Set the current advertiser */
    g_ble_ll_cur_adv_sm = advsm;

    ble_ll_adv_active_chanset_set_pri(advsm);

    /* Set the power */
    ble_phy_txpwr_set(advsm->adv_txpwr);

    /* Set channel */
    rc = ble_phy_setchan(advsm->adv_chan, BLE_ACCESS_ADDR_ADV, BLE_LL_CRCINIT_ADV);
    assert(rc == 0);

#if (BLE_LL_BT5_PHY_SUPPORTED == 1)
    /* Set phy mode */
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
        ble_phy_mode_set(BLE_PHY_MODE_1M, BLE_PHY_MODE_1M);
    } else {
        ble_phy_mode_set(advsm->pri_phy, advsm->pri_phy);
    }
#else
    ble_phy_mode_set(BLE_PHY_MODE_1M, BLE_PHY_MODE_1M);
#endif
#endif

    /* Set transmit start time. */
    txstart = sch->start_time + g_ble_ll_sched_offset_ticks;
    rc = ble_phy_tx_set_start_time(txstart, sch->remainder);
    if (rc) {
        STATS_INC(ble_ll_stats, adv_late_starts);
        goto adv_tx_done;
    }

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_ENCRYPTION) == 1)
    /* XXX: automatically do this in the phy based on channel? */
    ble_phy_encrypt_disable();
#endif

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    advsm->adv_rpa_index = -1;
    if (ble_ll_resolv_enabled()) {
        ble_phy_resolv_list_enable();
    } else {
        ble_phy_resolv_list_disable();
    }
#endif

    /* We switch to RX after connectable or scannable legacy packets. */
    if ((advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) &&
            ((advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_CONNECTABLE) ||
             (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_SCANNABLE))) {
        end_trans = BLE_PHY_TRANSITION_TX_RX;
        ble_phy_set_txend_cb(NULL, NULL);
    } else {
        end_trans = BLE_PHY_TRANSITION_NONE;
        ble_phy_set_txend_cb(ble_ll_adv_tx_done, advsm);
    }

    /* Get an advertising mbuf (packet header)  */
    adv_pdu = os_msys_get_pkthdr(BLE_ADV_LEGACY_MAX_PKT_LEN,
                                 sizeof(struct ble_mbuf_hdr));
    if (!adv_pdu) {
        ble_phy_disable();
        goto adv_tx_done;
    }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    ble_ll_adv_pdu_make(advsm, adv_pdu);
#else
    ble_ll_adv_legacy_pdu_make(advsm, adv_pdu);
#endif

    /* Transmit advertisement */
    rc = ble_phy_tx(adv_pdu, end_trans);
    os_mbuf_free_chain(adv_pdu);
    if (rc) {
        goto adv_tx_done;
    }

    /* Enable/disable whitelisting based on filter policy */
    if (advsm->adv_filter_policy != BLE_HCI_ADV_FILT_NONE) {
        ble_ll_whitelist_enable();
    } else {
        ble_ll_whitelist_disable();
    }

    /* Set link layer state to advertising */
    ble_ll_state_set(BLE_LL_STATE_ADV);

    /* Count # of adv. sent */
    STATS_INC(ble_ll_stats, adv_txg);

    return BLE_LL_SCHED_STATE_RUNNING;

adv_tx_done:
    ble_ll_adv_tx_done(advsm);
    return BLE_LL_SCHED_STATE_DONE;
}

static void
ble_ll_adv_set_sched(struct ble_ll_adv_sm *advsm)
{
    uint32_t max_usecs;
    struct ble_ll_sched_item *sch;

    sch = &advsm->adv_sch;
    sch->cb_arg = advsm;
    sch->sched_cb = ble_ll_adv_tx_start_cb;
    sch->sched_type = BLE_LL_SCHED_TYPE_ADV;

    /* Set end time to maximum time this schedule item may take */
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
        max_usecs = ble_ll_pdu_tx_time_get(advsm->adv_pdu_len, BLE_PHY_MODE_1M);

        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_DIRECTED) {
            max_usecs += BLE_LL_SCHED_DIRECT_ADV_MAX_USECS;
        } else if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_CONNECTABLE) {
            max_usecs += BLE_LL_SCHED_ADV_MAX_USECS;
        }
    } else {
        /*
         * In ADV_EXT_IND we always set only ADI and AUX so the payload length
         * is always 7 bytes.
         */
        max_usecs = ble_ll_pdu_tx_time_get(7, advsm->pri_phy);
    }
#else
    max_usecs = ble_ll_pdu_tx_time_get(advsm->adv_pdu_len, BLE_PHY_MODE_1M);

    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_DIRECTED) {
        max_usecs += BLE_LL_SCHED_DIRECT_ADV_MAX_USECS;
    } else if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_CONNECTABLE) {
        max_usecs += BLE_LL_SCHED_ADV_MAX_USECS;
    }
#endif

    sch->start_time = advsm->adv_pdu_start_time - g_ble_ll_sched_offset_ticks;
    sch->remainder = 0;
    sch->end_time = advsm->adv_pdu_start_time +
                    ble_ll_usecs_to_ticks_round_up(max_usecs);
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
static int
ble_ll_adv_secondary_tx_start_cb(struct ble_ll_sched_item *sch)
{
    int rc;
    uint8_t end_trans;
    uint32_t txstart;
    struct ble_ll_adv_sm *advsm;
    struct os_mbuf *adv_pdu;

    /* Get the state machine for the event */
    advsm = (struct ble_ll_adv_sm *)sch->cb_arg;

    /* Set the current advertiser */
    g_ble_ll_cur_adv_sm = advsm;

    ble_ll_adv_active_chanset_set_sec(advsm);

    /* Set the power */
    ble_phy_txpwr_set(advsm->adv_txpwr);

    /* Set channel */
    rc = ble_phy_setchan(advsm->adv_secondary_chan, BLE_ACCESS_ADDR_ADV,
                         BLE_LL_CRCINIT_ADV);
    assert(rc == 0);

#if (BLE_LL_BT5_PHY_SUPPORTED == 1)
    /* Set phy mode */
     ble_phy_mode_set(advsm->sec_phy, advsm->sec_phy);
#endif

    /* Set transmit start time. */
    txstart = sch->start_time + g_ble_ll_sched_offset_ticks;
    rc = ble_phy_tx_set_start_time(txstart, sch->remainder);
    if (rc) {
        STATS_INC(ble_ll_stats, adv_late_starts);
        goto adv_tx_done;
    }

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_ENCRYPTION) == 1)
    ble_phy_encrypt_disable();
#endif

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    advsm->adv_rpa_index = -1;
    if (ble_ll_resolv_enabled()) {
        ble_phy_resolv_list_enable();
    } else {
        ble_phy_resolv_list_disable();
    }
#endif

    /* Set phy mode based on type of advertisement */
    if ((advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_CONNECTABLE) ||
        (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_SCANNABLE)) {
        end_trans = BLE_PHY_TRANSITION_TX_RX;
        ble_phy_set_txend_cb(NULL, NULL);
    } else {
        end_trans = BLE_PHY_TRANSITION_NONE;
        ble_phy_set_txend_cb(ble_ll_adv_tx_done, advsm);
    }

    /* Get an advertising mbuf (packet header)  */
    adv_pdu = os_msys_get_pkthdr(BLE_ADV_LEGACY_MAX_PKT_LEN,
                                 sizeof(struct ble_mbuf_hdr));
    if (!adv_pdu) {
        ble_phy_disable();
        goto adv_tx_done;
    }

    ble_ll_adv_pdu_make(advsm, adv_pdu);

    /* Transmit advertisement */
    rc = ble_phy_tx(adv_pdu, end_trans);
    os_mbuf_free_chain(adv_pdu);
    if (rc) {
        goto adv_tx_done;
    }

    /* Enable/disable whitelisting based on filter policy */
    if (advsm->adv_filter_policy != BLE_HCI_ADV_FILT_NONE) {
        ble_ll_whitelist_enable();
    } else {
        ble_ll_whitelist_disable();
    }

    /* Set link layer state to advertising */
    ble_ll_state_set(BLE_LL_STATE_ADV);

    /* Count # of adv. sent */
    STATS_INC(ble_ll_stats, adv_txg);

    return BLE_LL_SCHED_STATE_RUNNING;

adv_tx_done:
    ble_ll_adv_tx_done(advsm);
    return BLE_LL_SCHED_STATE_DONE;
}

static void
ble_ll_adv_secondary_set_sched(struct ble_ll_adv_sm *advsm)
{
    uint32_t max_usecs;
    struct ble_ll_sched_item *sch;

    sch = &advsm->adv_secondary_sch;
    sch->cb_arg = advsm;
    sch->sched_cb = ble_ll_adv_secondary_tx_start_cb;
    sch->sched_type = BLE_LL_SCHED_TYPE_ADV;

    /* TODO we could use CSA2 for this
     * (will be needed for periodic advertising anyway)
     */
    advsm->adv_secondary_chan = rand() % BLE_PHY_NUM_DATA_CHANS;

    /* Set end time to maximum time this schedule item may take */
    max_usecs = ble_ll_pdu_tx_time_get(ble_ll_adv_secondary_pdu_len(advsm),
                                       advsm->sec_phy);

    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_CONNECTABLE) {
        max_usecs += BLE_LL_IFS +
                     /* AUX_CONN_REQ */
                     ble_ll_pdu_tx_time_get(34 + 14, advsm->sec_phy)  +
                     BLE_LL_IFS +
                     /* AUX_CONN_RSP */
                     ble_ll_pdu_tx_time_get(14, advsm->sec_phy);
    } else if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_SCANNABLE) {
        max_usecs += BLE_LL_IFS +
                     /* AUX_SCAN_REQ */
                     ble_ll_pdu_tx_time_get(12, advsm->sec_phy)  +
                     BLE_LL_IFS +
                     /* AUX_SCAN_RSP */
                     ble_ll_pdu_tx_time_get(ble_ll_adv_aux_scan_rsp_len(advsm),
                                            advsm->sec_phy);
    }

    /*
     * XXX: For now, just schedule some additional time so we insure we have
     * enough time to do everything we want.
     */
    max_usecs += XCVR_PROC_DELAY_USECS;

    sch->start_time = advsm->adv_secondary_start_time - g_ble_ll_sched_offset_ticks;
    sch->remainder = 0;
    sch->end_time = advsm->adv_secondary_start_time +
        os_cputime_usecs_to_ticks(max_usecs);
}
#endif

/**
 * Called when advertising need to be halted. This normally should not be called
 * and is only called when a scheduled item executes but advertising is still
 * running.
 *
 * Context: Interrupt
 */
void
ble_ll_adv_halt(void)
{
    struct ble_ll_adv_sm *advsm;

    if (g_ble_ll_cur_adv_sm != NULL) {
        advsm = g_ble_ll_cur_adv_sm;

        ble_phy_txpwr_set(MYNEWT_VAL(BLE_LL_TX_PWR_DBM));

        os_eventq_put(&g_ble_ll_data.ll_evq, &advsm->adv_txdone_ev);
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        os_eventq_put(&g_ble_ll_data.ll_evq, &advsm->adv_sec_txdone_ev);
#endif

        ble_ll_log(BLE_LL_LOG_ID_ADV_TXDONE, ble_ll_state_get(),
                   advsm->adv_instance, 0);
        ble_ll_state_set(BLE_LL_STATE_STANDBY);
        ble_ll_adv_active_chanset_clear(g_ble_ll_cur_adv_sm);
        g_ble_ll_cur_adv_sm = NULL;
    }
}

/**
 * Called by the HCI command parser when a set advertising parameters command
 * has been received.
 *
 * Context: Link Layer task (HCI command parser)
 *
 * @param cmd
 *
 * @return int
 */
int
ble_ll_adv_set_adv_params(uint8_t *cmd)
{
    uint8_t adv_type;
    uint8_t adv_filter_policy;
    uint8_t adv_chanmask;
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint16_t adv_itvl_min;
    uint16_t adv_itvl_max;
    uint16_t min_itvl;
    struct ble_ll_adv_sm *advsm;
    uint16_t props;

    advsm = &g_ble_ll_adv_sm[0];
    if (advsm->adv_enabled) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* Make sure intervals are OK (along with advertising type */
    adv_itvl_min = get_le16(cmd);
    adv_itvl_max = get_le16(cmd + 2);
    adv_type = cmd[4];

    /*
     * Get the filter policy now since we will ignore it if we are doing
     * directed advertising
     */
    adv_filter_policy = cmd[14];

    /* Assume min interval based on low duty cycle/indirect advertising */
    min_itvl = BLE_LL_ADV_ITVL_MIN;

    switch (adv_type) {
    case BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD:
        adv_filter_policy = BLE_HCI_ADV_FILT_NONE;
        memcpy(advsm->peer_addr, cmd + 7, BLE_DEV_ADDR_LEN);

        /* Ignore min/max interval */
        min_itvl = 0;
        adv_itvl_min = 0;
        adv_itvl_max = 0;

        props = BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY_HD_DIR ;
        break;
    case BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_LD:
        adv_filter_policy = BLE_HCI_ADV_FILT_NONE;
        memcpy(advsm->peer_addr, cmd + 7, BLE_DEV_ADDR_LEN);

        props = BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY_LD_DIR ;
        break;
    case BLE_HCI_ADV_TYPE_ADV_IND:
        props = BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY_IND;
        break;
    case BLE_HCI_ADV_TYPE_ADV_NONCONN_IND:
        min_itvl = 0;
        props = BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY_NONCONN;
        break;
    case BLE_HCI_ADV_TYPE_ADV_SCAN_IND:
        min_itvl = 0;
        props = BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY_SCAN;
        break;
    default:
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Make sure interval minimum is valid for the advertising type */
    if ((adv_itvl_min > adv_itvl_max) || (adv_itvl_min < min_itvl) ||
        (adv_itvl_min > BLE_HCI_ADV_ITVL_MAX) ||
        (adv_itvl_max > BLE_HCI_ADV_ITVL_MAX)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check own and peer address type */
    own_addr_type =  cmd[5];
    peer_addr_type = cmd[6];

    if ((own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) ||
        (peer_addr_type > BLE_HCI_ADV_PEER_ADDR_MAX)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    advsm->adv_txpwr = MYNEWT_VAL(BLE_LL_TX_PWR_DBM);

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    if (own_addr_type > BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        /* Copy peer address */
        memcpy(advsm->peer_addr, cmd + 7, BLE_DEV_ADDR_LEN);

        /* Reset RPA timer so we generate a new RPA */
        advsm->adv_rpa_timer = os_time_get();
    }
#else
    /* If we dont support privacy some address types wont work */
    if (own_addr_type > BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        return BLE_ERR_UNSUPPORTED;
    }
#endif

    /* There are only three adv channels, so check for any outside the range */
    adv_chanmask = cmd[13];
    if (((adv_chanmask & 0xF8) != 0) || (adv_chanmask == 0)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check for valid filter policy */
    if (adv_filter_policy > BLE_HCI_ADV_FILT_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Fill out rest of advertising state machine */
    advsm->own_addr_type = own_addr_type;
    advsm->peer_addr_type = peer_addr_type;
    advsm->adv_filter_policy = adv_filter_policy;
    advsm->adv_chanmask = adv_chanmask;
    advsm->adv_itvl_min = adv_itvl_min;
    advsm->adv_itvl_max = adv_itvl_max;
    advsm->props = props;

    return 0;
}

/**
 * Stop advertising state machine
 *
 * Context: Link Layer task.
 *
 * @param advsm
 */
static void
ble_ll_adv_sm_stop(struct ble_ll_adv_sm *advsm)
{
    os_sr_t sr;

    if (advsm->adv_enabled) {
        /* Remove any scheduled advertising items */
        ble_ll_sched_rmv_elem(&advsm->adv_sch);
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        ble_ll_sched_rmv_elem(&advsm->adv_secondary_sch);
#endif

        /* Set to standby if we are no longer advertising */
        OS_ENTER_CRITICAL(sr);
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        if (g_ble_ll_cur_adv_sm == advsm) {
            ble_phy_disable();
            ble_ll_wfr_disable();
            ble_ll_state_set(BLE_LL_STATE_STANDBY);
            g_ble_ll_cur_adv_sm = NULL;
            ble_ll_scan_chk_resume();
        }
#else
        if (ble_ll_state_get() == BLE_LL_STATE_ADV) {
            ble_phy_disable();
            ble_ll_wfr_disable();
            ble_ll_state_set(BLE_LL_STATE_STANDBY);
            g_ble_ll_cur_adv_sm = NULL;
            ble_ll_scan_chk_resume();
        }
#endif
#ifdef BLE_XCVR_RFCLK
        ble_ll_sched_rfclk_chk_restart();
#endif
        OS_EXIT_CRITICAL(sr);

        os_eventq_remove(&g_ble_ll_data.ll_evq, &advsm->adv_txdone_ev);
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        os_eventq_remove(&g_ble_ll_data.ll_evq, &advsm->adv_sec_txdone_ev);
#endif

        /* If there is an event buf we need to free it */
        if (advsm->conn_comp_ev) {
            ble_hci_trans_buf_free(advsm->conn_comp_ev);
            advsm->conn_comp_ev = NULL;
        }

        ble_ll_adv_active_chanset_clear(advsm);

        /* Disable advertising */
        advsm->adv_enabled = 0;
    }
}

static void
ble_ll_adv_scheduled(struct ble_ll_adv_sm *advsm, uint32_t sch_start)
{
    /* The event start time is when we start transmission of the adv PDU */
    advsm->adv_event_start_time = sch_start + g_ble_ll_sched_offset_ticks;
    advsm->adv_pdu_start_time = advsm->adv_event_start_time;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    /* this is validated for HD adv so no need to do additional checks here
     * duration is in 10ms units
     */
    if (advsm->duration) {
        advsm->adv_end_time = advsm->adv_event_start_time +
                             os_cputime_usecs_to_ticks(advsm->duration * 10000);
    }
#else
    /* Set the time at which we must end directed, high-duty cycle advertising.
     */
    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_HD_DIRECTED) {
        advsm->adv_end_time = advsm->adv_event_start_time +
                     os_cputime_usecs_to_ticks(BLE_LL_ADV_STATE_HD_MAX * 1000);
    }
#endif
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
static void
ble_ll_set_adv_secondary_start_time(struct ble_ll_adv_sm *advsm)
{
    static const uint8_t bits[8] = {0, 1, 1, 2, 1, 2, 2, 3};
    struct ble_ll_sched_item *sched = &advsm->adv_sch;
    uint32_t adv_pdu_dur;
    uint32_t adv_event_dur;
    uint8_t chans;

    assert(advsm->adv_chanmask > 0 &&
           advsm->adv_chanmask <= BLE_HCI_ADV_CHANMASK_DEF);

    chans = bits[advsm->adv_chanmask];

    /*
     * We want to schedule auxiliary packet as soon as possible after the end
     * of advertising event, but no sooner than T_MAFS. The interval between
     * advertising packets is 250 usecs (8.19 ticks) on LE Coded and a bit less
     * on 1M, but it can vary a bit due to scheduling which we can't really
     * control. Since we round ticks up for both interval and T_MAFS, we still
     * have some margin here. The worst thing that can happen is that we skip
     * last advertising packet which is not a bit problem so leave it as-is, no
     * need to make code more complicated.
     */

    /*
     * XXX: this could be improved if phy has TX-TX transition with controlled
     *      or predefined interval, but since it makes advertising code even
     *      more complicated let's skip it for now...
     */

    adv_pdu_dur = (int32_t)(sched->end_time - sched->start_time) -
                  g_ble_ll_sched_offset_ticks;

    /* 9 is 8.19 ticks rounded up - see comment above */
    adv_event_dur = (adv_pdu_dur * chans) + (9 * (chans - 1));

    advsm->adv_secondary_start_time = advsm->adv_event_start_time +
                                      adv_event_dur +
                                      ble_ll_usecs_to_ticks_round_up(BLE_LL_MAFS);
}

static void
ble_ll_adv_secondary_scheduled(struct ble_ll_adv_sm *advsm, uint32_t sch_start)
{
    advsm->adv_secondary_start_time = sch_start + g_ble_ll_sched_offset_ticks;
}
#endif

/**
 * Start the advertising state machine. This is called when the host sends
 * the "enable advertising" command and is not called again while in the
 * advertising state.
 *
 * Context: Link-layer task.
 *
 * @param advsm Pointer to advertising state machine
 *
 * @return int
 */
static int
ble_ll_adv_sm_start(struct ble_ll_adv_sm *advsm)
{
    uint8_t adv_chan;
    uint8_t *addr;
    uint8_t *evbuf;

    /* only clear flags that are not set from HCI */
    advsm->flags &= ~BLE_LL_ADV_SM_FLAG_TX_ADD;
    advsm->flags &= ~BLE_LL_ADV_SM_FLAG_RX_ADD;
    advsm->flags &= ~BLE_LL_ADV_SM_FLAG_CONN_RSP_TXD;

    if (advsm->own_addr_type == BLE_HCI_ADV_OWN_ADDR_RANDOM) {
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        if (!ble_ll_is_valid_random_addr(advsm->adv_random_addr)) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }
#else
        if (!ble_ll_is_valid_random_addr(g_random_addr)) {
            return BLE_ERR_CMD_DISALLOWED;
        }
#endif
    }

    /*
     * Get an event with which to send the connection complete event if
     * this is connectable
     */
    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_CONNECTABLE) {
        /* We expect this to be NULL but if not we wont allocate one... */
        if (advsm->conn_comp_ev == NULL) {
            evbuf = ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_HI);
            if (!evbuf) {
                return BLE_ERR_MEM_CAPACITY;
            }
            advsm->conn_comp_ev = evbuf;
        }
    }

    /* Set advertising address */
    if ((advsm->own_addr_type & 1) == 0) {
        addr = g_dev_addr;
    } else {
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        addr = advsm->adv_random_addr;
#else
        addr = g_random_addr;
#endif
        advsm->flags |= BLE_LL_ADV_SM_FLAG_TX_ADD;
    }
    memcpy(advsm->adva, addr, BLE_DEV_ADDR_LEN);

    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_DIRECTED) {
        memcpy(advsm->initiator_addr, advsm->peer_addr, BLE_DEV_ADDR_LEN);
        if (advsm->peer_addr_type & 1) {
            advsm->flags |= BLE_LL_ADV_SM_FLAG_RX_ADD;
        }
    }

    /* This will generate an RPA for both initiator addr and adva */
#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    ble_ll_adv_chk_rpa_timeout(advsm);
#endif

    /* Set flag telling us that advertising is enabled */
    advsm->adv_enabled = 1;

    /* Determine the advertising interval we will use */
    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_HD_DIRECTED) {
        /* Set it to max. allowed for high duty cycle advertising */
        advsm->adv_itvl_usecs = BLE_LL_ADV_PDU_ITVL_HD_MS_MAX;
    } else {
        advsm->adv_itvl_usecs = (uint32_t)advsm->adv_itvl_max;
        advsm->adv_itvl_usecs *= BLE_LL_ADV_ITVL;
    }

    /* Set first advertising channel */
    adv_chan = ble_ll_adv_first_chan(advsm);
    advsm->adv_chan = adv_chan;

    /*
     * XXX: while this may not be the most efficient, schedule the first
     * advertising event some time in the future (5 msecs). This will give
     * time to start up any clocks or anything and also avoid a bunch of code
     * to check if we are currently doing anything. Just makes this simple.
     *
     * Might also want to align this on a slot in the future.
     *
     * NOTE: adv_event_start_time gets set by the sched_adv_new
     */
    advsm->adv_pdu_start_time = os_cputime_get32() +
                                os_cputime_usecs_to_ticks(5000);

    /*
     * Schedule advertising. We set the initial schedule start and end
     * times to the earliest possible start/end.
     */
    ble_ll_adv_set_sched(advsm);
    ble_ll_sched_adv_new(&advsm->adv_sch, ble_ll_adv_scheduled);

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (!(advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY)) {
        ble_ll_set_adv_secondary_start_time(advsm);
        ble_ll_adv_secondary_set_sched(advsm);
        ble_ll_sched_adv_new(&advsm->adv_secondary_sch,
                             ble_ll_adv_secondary_scheduled);
    }
#endif

    return BLE_ERR_SUCCESS;
}

/**
 * Called when the LE HCI command read advertising channel tx power command
 * has been received. Returns the current advertising transmit power.
 *
 * Context: Link Layer task (HCI command parser)
 *
 * @return int
 */
int
ble_ll_adv_read_txpwr(uint8_t *rspbuf, uint8_t *rsplen)
{
    rspbuf[0] = MYNEWT_VAL(BLE_LL_TX_PWR_DBM);
    *rsplen = 1;
    return BLE_ERR_SUCCESS;
}

/**
 * Turn advertising on/off.
 *
 * Context: Link Layer task
 *
 * @param cmd
 *
 * @return int
 */
int
ble_ll_adv_set_enable(uint8_t instance, uint8_t enable, int duration,
                          uint8_t events)
{
    int rc;
    struct ble_ll_adv_sm *advsm;

    if (instance >= BLE_ADV_INSTANCES) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    advsm = &g_ble_ll_adv_sm[instance];

    rc = BLE_ERR_SUCCESS;
    if (enable == 1) {
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        /* handle specifics of HD dir adv enabled in legacy way */
        if (duration < 0) {
            if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_HD_DIRECTED) {
                duration = BLE_LL_ADV_STATE_HD_MAX / 10;
            } else {
                duration = 0;
            }
        }
        advsm->duration = duration;
        advsm->events_max = events;
        advsm->events = 0;
#endif

        /* If already enabled, do nothing */
        if (!advsm->adv_enabled) {
            /* Start the advertising state machine */
            rc = ble_ll_adv_sm_start(advsm);
        }
    } else if (enable == 0) {
        ble_ll_adv_sm_stop(advsm);
    } else {
        rc = BLE_ERR_INV_HCI_CMD_PARMS;
    }

    return rc;
}

/**
 * Set the scan response data that the controller will send.
 *
 * @param cmd
 * @param len
 *
 * @return int
 */
int
ble_ll_adv_set_scan_rsp_data(uint8_t *cmd, uint8_t instance, uint8_t operation)
{
    uint8_t datalen;
    uint8_t off = 0;
    struct ble_ll_adv_sm *advsm;

    if (instance >= BLE_ADV_INSTANCES) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    advsm = &g_ble_ll_adv_sm[instance];
    datalen = cmd[0];

    /* check if type of advertising support scan rsp */
    if (!(advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_SCANNABLE)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    switch (operation) {
    case BLE_HCI_LE_SET_EXT_SCAN_RSP_DATA_OPER_COMPLETE:
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
            if (datalen > BLE_SCAN_RSP_LEGACY_DATA_MAX_LEN) {
                return BLE_ERR_INV_HCI_CMD_PARMS;
            }
        }

        break;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    case BLE_HCI_LE_SET_EXT_SCAN_RSP_DATA_OPER_LAST:
        /* TODO mark scan rsp as complete? */
        /* fall through */
    case BLE_HCI_LE_SET_EXT_SCAN_RSP_DATA_OPER_INT:
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        if (advsm->adv_enabled) {
            return BLE_ERR_CMD_DISALLOWED;
        }

        if (!datalen) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        off = advsm->scan_rsp_len;
        break;
    case BLE_HCI_LE_SET_EXT_SCAN_RSP_DATA_OPER_FIRST:
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        if (advsm->adv_enabled) {
            return BLE_ERR_CMD_DISALLOWED;
        }

        if (!datalen) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        break;
#endif
    default:
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check for valid advertising data length */
    if (datalen + off > BLE_SCAN_RSP_DATA_MAX_LEN) {
        advsm->scan_rsp_len = 0;
        return BLE_ERR_MEM_CAPACITY;
    }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    /* DID shall be updated when host provides new scan response data */
    advsm->adi = (advsm->adi & 0xf000) | (rand() & 0x0fff);
#endif

    /* Copy the new data into the advertising structure. */
    advsm->scan_rsp_len = datalen + off;
    memcpy(advsm->scan_rsp_data + off, cmd + 1, datalen);

    return BLE_ERR_SUCCESS;
}

/**
 * Called by the LL HCI command parser when a set advertising
 * data command has been sent from the host to the controller.
 *
 * @param cmd Pointer to command data
 * @param len Length of command data
 *
 * @return int 0: success; BLE_ERR_INV_HCI_CMD_PARMS otherwise.
 */
int
ble_ll_adv_set_adv_data(uint8_t *cmd, uint8_t instance, uint8_t operation)
{
    uint8_t datalen;
    uint8_t off = 0;
    struct ble_ll_adv_sm *advsm;

    if (instance >= BLE_ADV_INSTANCES) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    advsm = &g_ble_ll_adv_sm[instance];
    datalen = cmd[0];

    /* check if type of advertising support adv data */
    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_DIRECTED) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }
    } else {
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_SCANNABLE) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }
    }

    switch (operation) {
    case BLE_HCI_LE_SET_EXT_ADV_DATA_OPER_COMPLETE:
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
            if (datalen > BLE_ADV_LEGACY_DATA_MAX_LEN) {
                return BLE_ERR_INV_HCI_CMD_PARMS;
            }
        }

        break;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    case BLE_HCI_LE_SET_EXT_ADV_DATA_OPER_UNCHANGED:
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        if (!advsm->adv_enabled || !advsm->adv_len || datalen) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        /* update DID only */
        advsm->adi = (advsm->adi & 0xf000) | (rand() & 0x0fff);
        return BLE_ERR_SUCCESS;
    case BLE_HCI_LE_SET_EXT_ADV_DATA_OPER_LAST:
        /* TODO mark adv data as complete? */
        /* fall through */
    case BLE_HCI_LE_SET_EXT_ADV_DATA_OPER_INT:
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        if (!datalen) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        if (advsm->adv_enabled) {
            return BLE_ERR_CMD_DISALLOWED;
        }

        off = advsm->adv_len;
        break;
    case BLE_HCI_LE_SET_EXT_ADV_DATA_OPER_FIRST:
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        if (advsm->adv_enabled) {
            return BLE_ERR_CMD_DISALLOWED;
        }

        if (!datalen) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        break;
#endif
    default:
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check for valid advertising data length */
    if (datalen + off > BLE_ADV_DATA_MAX_LEN) {
        advsm->adv_len = 0;
        return BLE_ERR_MEM_CAPACITY;
    }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    /* DID shall be updated when host provides new advertising data */
    advsm->adi = (advsm->adi & 0xf000) | (rand() & 0x0fff);
#endif

    /* Copy the new data into the advertising structure. */
    advsm->adv_len = datalen + off;
    memcpy(advsm->adv_data + off, cmd + 1, datalen);

    return BLE_ERR_SUCCESS;
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
int
ble_ll_adv_ext_set_param(uint8_t *cmdbuf, uint8_t *rspbuf, uint8_t *rsplen)
{
    uint8_t adv_filter_policy;
    uint8_t adv_chanmask;
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint32_t adv_itvl_min;
    uint32_t adv_itvl_max;
    uint16_t min_itvl = 0;
    uint16_t props;
    struct ble_ll_adv_sm *advsm;
    uint8_t pri_phy;
    uint8_t sec_phy;
    uint8_t sid;
    uint8_t scan_req_notif;
    int8_t tx_power;

    if (cmdbuf[0] >= BLE_ADV_INSTANCES) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    advsm = &g_ble_ll_adv_sm[cmdbuf[0]];
    if (advsm->adv_enabled) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    props = get_le16(&cmdbuf[1]);

    adv_itvl_min = cmdbuf[5] << 16 | cmdbuf[4] << 8 | cmdbuf[3];
    adv_itvl_max = cmdbuf[8] << 16 | cmdbuf[7] << 8 | cmdbuf[6];

    if (props & ~BLE_HCI_LE_SET_EXT_ADV_PROP_MASK) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
        if (advsm->adv_len > BLE_ADV_LEGACY_DATA_MAX_LEN ||
            advsm->scan_rsp_len > BLE_SCAN_RSP_LEGACY_DATA_MAX_LEN) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        /* if legacy bit is set possible values are limited */
        switch (props) {
        case BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY_IND:
        case BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY_LD_DIR:
        case BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY_HD_DIR:
        case BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY_SCAN:
        case BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY_NONCONN:
            break;
        default:
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }
    } else {
        /* HD directed advertising allowed only on legacy PDUs */
        if (props & BLE_HCI_LE_SET_EXT_ADV_PROP_HD_DIRECTED) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        /* if ext advertising PDUs are used then it shall not be both
         * connectable and scanable
         */
        if ((props & BLE_HCI_LE_SET_EXT_ADV_PROP_CONNECTABLE) &&
            (props & BLE_HCI_LE_SET_EXT_ADV_PROP_SCANNABLE)) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }
    }

    if (props & BLE_HCI_LE_SET_EXT_ADV_PROP_CONNECTABLE) {
        min_itvl = BLE_LL_ADV_ITVL_MIN;
    }

    if (props & BLE_HCI_LE_SET_EXT_ADV_PROP_HD_DIRECTED) {
        if (advsm->adv_len || advsm->scan_rsp_len) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        /* Ignore min/max interval */
        min_itvl = 0;
        adv_itvl_min = 0;
        adv_itvl_max = 0;
    }

    /* Make sure interval minimum is valid for the advertising type
     * TODO for now limit those to values from legacy advertising
     */
    if ((adv_itvl_min > adv_itvl_max) || (adv_itvl_min < min_itvl) ||
        (adv_itvl_min > BLE_HCI_ADV_ITVL_MAX) ||
        (adv_itvl_max > BLE_HCI_ADV_ITVL_MAX)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* There are only three adv channels, so check for any outside the range */
    adv_chanmask = cmdbuf[9];
    if (((adv_chanmask & 0xF8) != 0) || (adv_chanmask == 0)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check own and peer address type */
    own_addr_type = cmdbuf[10];
    peer_addr_type = cmdbuf[11];

    if ((own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) ||
        (peer_addr_type > BLE_HCI_ADV_PEER_ADDR_MAX)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    if (own_addr_type > BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        /* Reset RPA timer so we generate a new RPA */
        advsm->adv_rpa_timer = os_time_get();
    }
#else
    /* If we dont support privacy some address types wont work */
    if (own_addr_type > BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        return BLE_ERR_UNSUPPORTED;
    }
#endif

    adv_filter_policy = cmdbuf[18];
    /* Check filter policy (valid only for undirected */
    if (!(props & BLE_HCI_LE_SET_EXT_ADV_PROP_DIRECTED) &&
         adv_filter_policy > BLE_HCI_ADV_FILT_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    pri_phy = cmdbuf[20];
    if (pri_phy != BLE_HCI_LE_PHY_1M && pri_phy != BLE_HCI_LE_PHY_CODED) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    sec_phy = cmdbuf[22];
    if (sec_phy != BLE_HCI_LE_PHY_1M && sec_phy != BLE_HCI_LE_PHY_2M &&
            sec_phy != BLE_HCI_LE_PHY_CODED) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    sid = cmdbuf[23];
    if (sid > 0x0f) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    scan_req_notif = cmdbuf[24];
    if (scan_req_notif > 0x01) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (props & BLE_HCI_LE_SET_EXT_ADV_PROP_DIRECTED) {
        memcpy(advsm->peer_addr, &cmdbuf[12], BLE_DEV_ADDR_LEN);
    }

    tx_power = (int8_t) rspbuf[21];
    if (tx_power == 127) {
        /* no preference */
        advsm->adv_txpwr = MYNEWT_VAL(BLE_LL_TX_PWR_DBM);
    } else {
        advsm->adv_txpwr = ble_phy_txpower_round(tx_power);
    }

    advsm->own_addr_type = own_addr_type;
    advsm->peer_addr_type = peer_addr_type;
    advsm->adv_filter_policy = adv_filter_policy;
    advsm->adv_chanmask = adv_chanmask;
    advsm->adv_itvl_min = adv_itvl_min;
    advsm->adv_itvl_max = adv_itvl_max;
    advsm->pri_phy = pri_phy;
    advsm->sec_phy = sec_phy;
    /* Update SID only */
    advsm->adi = (advsm->adi & 0x0fff) | ((sid << 12));

    advsm->props = props;

    if (scan_req_notif) {
        advsm->flags |= BLE_LL_ADV_SM_FLAG_SCAN_REQ_NOTIF;
    } else {
        advsm->flags &= ~BLE_LL_ADV_SM_FLAG_SCAN_REQ_NOTIF;
    }

    rspbuf[0] = advsm->adv_txpwr;
    *rsplen = 1;

    return 0;
}

int
ble_ll_adv_ext_set_adv_data(uint8_t *cmdbuf, uint8_t cmdlen)
{
    /* check if length is correct */
    if (cmdlen < 4) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* TODO fragment preference ignored for now */

    return ble_ll_adv_set_adv_data(cmdbuf + 3, cmdbuf[0], cmdbuf[1]);
}

int
ble_ll_adv_ext_set_scan_rsp(uint8_t *cmdbuf, uint8_t cmdlen)
{
    /* check if length is correct */
    if (cmdlen < 4) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* TODO fragment preference ignored for now */

    return ble_ll_adv_set_scan_rsp_data(cmdbuf + 3, cmdbuf[0], cmdbuf[1]);
}

struct ext_adv_set {
    uint8_t handle;
    uint16_t duration;
    uint8_t events;
} __attribute__((packed));

/**
 * HCI LE extended advertising enable command
 *
 * @param cmd Pointer to command data
 * @param len Command data length
 *
 * @return int BLE error code
 */
int
ble_ll_adv_ext_set_enable(uint8_t *cmd, uint8_t len)
{
    struct ble_ll_adv_sm *advsm;
    struct ext_adv_set* set;
    uint8_t enable;
    uint8_t sets;
    int i, j, rc;

    if (len < 2) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    enable = cmd[0];
    sets = cmd[1];
    cmd += 2;

    /* check if length is correct */
    if (len != 2 + (sets * sizeof (*set))) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (sets > BLE_ADV_INSTANCES) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (sets == 0) {
        if (enable) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        /* disable all instances */
        for (i = 0; i < BLE_ADV_INSTANCES; i++) {
            ble_ll_adv_set_enable(i, 0, 0, 0);
        }

        return BLE_ERR_SUCCESS;
    }

    set = (void *) cmd;
    /* validate instances */
    for (i = 0; i < sets; i++) {
        if (set->handle >= BLE_ADV_INSTANCES) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }

        /* validate duplicated sets */
        for (j = 1; j < sets - i; j++) {
            if (set->handle == set[j].handle) {
                return BLE_ERR_INV_HCI_CMD_PARMS;
            }
        }

        if (enable) {
            advsm = &g_ble_ll_adv_sm[set->handle];

            if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_HD_DIRECTED) {
                if (set->duration == 0 || le16toh(set->duration) > 128) {
                    return BLE_ERR_INV_HCI_CMD_PARMS;
                }
            }
        }

        set++;
    }

    set = (void *) cmd;
    for (i = 0; i < sets; i++) {
        rc = ble_ll_adv_set_enable(set->handle, enable, le16toh(set->duration),
                                   set->events);
        if (rc) {
            return rc;
        }

        set++;
    }

    return BLE_ERR_SUCCESS;
}

int
ble_ll_adv_set_random_addr(uint8_t *addr, uint8_t instance)
{
    struct ble_ll_adv_sm *advsm;

    if (instance >= BLE_ADV_INSTANCES) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    advsm = &g_ble_ll_adv_sm[instance];

    /*
     * Reject if connectable advertising is on
     * Core Spec Vol. 2 Part E 7.8.52
     */
    if (advsm->adv_enabled &&
            (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_CONNECTABLE)) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    memcpy(advsm->adv_random_addr, addr, BLE_DEV_ADDR_LEN);
    return BLE_ERR_SUCCESS;
}

/**
 * HCI LE extended advertising remove command
 *
 * @param instance Advertising instance to be removed
 *
 * @return int BLE error code
 */
int
ble_ll_adv_remove(uint8_t instance)
{
    struct ble_ll_adv_sm *advsm;

    /* TODO
     * Should we allow any value for instance ID?
     */

    if (instance >= BLE_ADV_INSTANCES) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    advsm = &g_ble_ll_adv_sm[instance];

    if (advsm->adv_enabled) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    ble_ll_adv_sm_init(advsm);

    return BLE_ERR_SUCCESS;
}

/**
 * HCI LE extended advertising clear command
 *
 * @return int BLE error code
 */
int
ble_ll_adv_clear_all(void)
{
    int i;

    for (i = 0; i < BLE_ADV_INSTANCES; i++) {
        if (g_ble_ll_adv_sm[i].adv_enabled) {
            return BLE_ERR_CMD_DISALLOWED;
        }
    }

    ble_ll_adv_reset();

    return BLE_ERR_SUCCESS;
}
#endif


/**
 * Called when the LL receives a scan request or connection request
 *
 * Context: Called from interrupt context.
 *
 * @param rxbuf
 *
 * @return -1: request not for us or is a connect request.
 *          0: request (scan) is for us and we successfully went from rx to tx.
 *        > 0: PHY error attempting to go from rx to tx.
 */
static int
ble_ll_adv_rx_req(uint8_t pdu_type, struct os_mbuf *rxpdu)
{
    int rc;
    int resolved;
    uint8_t chk_wl;
    uint8_t txadd;
    uint8_t peer_addr_type;
    uint8_t *rxbuf;
    uint8_t *adva;
    uint8_t *peer;
    struct ble_mbuf_hdr *ble_hdr;
    struct ble_ll_adv_sm *advsm;
    struct os_mbuf *rsp;

    /* See if adva in the request (scan or connect) matches what we sent */
    advsm = g_ble_ll_cur_adv_sm;
    rxbuf = rxpdu->om_data;
    adva = rxbuf + BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN;
    if (memcmp(advsm->adva, adva, BLE_DEV_ADDR_LEN)) {
        return -1;
    }

    /* Set device match bit if we are whitelisting */
    if (pdu_type == BLE_ADV_PDU_TYPE_SCAN_REQ) {
        chk_wl = advsm->adv_filter_policy & 1;
    } else {
        chk_wl = advsm->adv_filter_policy & 2;
    }

    /* Get the peer address type */
    if (rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK) {
        txadd = BLE_ADDR_RANDOM;
    } else {
        txadd = BLE_ADDR_PUBLIC;
    }

    ble_hdr = BLE_MBUF_HDR_PTR(rxpdu);
    peer = rxbuf + BLE_LL_PDU_HDR_LEN;
    peer_addr_type = txadd;
    resolved = 0;

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    if (ble_ll_is_rpa(peer, txadd) && ble_ll_resolv_enabled()) {
        advsm->adv_rpa_index = ble_hw_resolv_list_match();
        if (advsm->adv_rpa_index >= 0) {
            ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_RESOLVED;
            if (chk_wl) {
                peer = g_ble_ll_resolv_list[advsm->adv_rpa_index].rl_identity_addr;
                peer_addr_type = g_ble_ll_resolv_list[advsm->adv_rpa_index].rl_addr_type;
                resolved = 1;
            }
        } else {
            if (chk_wl) {
                return -1;
            }
        }
    }
#endif

    /* Set device match bit if we are whitelisting */
    if (chk_wl && !ble_ll_whitelist_match(peer, peer_addr_type, resolved)) {
        return -1;
    }

    /*
     * We set the device match bit to tell the upper layer that we will
     * accept the request
     */
    ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_DEVMATCH;

    /* Setup to transmit the scan response if appropriate */
    rc = -1;

    if (pdu_type == BLE_ADV_PDU_TYPE_SCAN_REQ) {
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        if (advsm->flags & BLE_LL_ADV_SM_FLAG_SCAN_REQ_NOTIF) {
            ble_ll_hci_ev_send_scan_req_recv(advsm->adv_instance, peer,
                                             peer_addr_type);
        }

        rsp = ble_ll_adv_scan_rsp_pdu_make(advsm);
#else
        rsp = ble_ll_adv_scan_rsp_legacy_pdu_make(advsm);
#endif
        if (rsp) {
            /* XXX TODO: assume we do not need to change phy mode */
            ble_phy_set_txend_cb(ble_ll_adv_tx_done, advsm);
            rc = ble_phy_tx(rsp, BLE_PHY_TRANSITION_NONE);
            if (!rc) {
                ble_hdr->rxinfo.flags |= BLE_MBUF_HDR_F_SCAN_RSP_TXD;
                STATS_INC(ble_ll_stats, scan_rsp_txg);
            }
            os_mbuf_free_chain(rsp);
        }
    } else if (pdu_type == BLE_ADV_PDU_TYPE_AUX_CONNECT_REQ) {
        /*
         * Only accept connect requests from the desired address if we
         * are doing directed advertising
         */
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_DIRECTED) {
            if (memcmp(advsm->peer_addr, peer, BLE_DEV_ADDR_LEN)) {
                return -1;
            }
        }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
            return -1;
        }

        /* use remote address used over the air */
        rsp = ble_ll_adv_aux_conn_rsp_pdu_make(advsm,
                                               rxbuf + BLE_LL_PDU_HDR_LEN,
                                               rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK);
        if (rsp) {
            ble_phy_set_txend_cb(ble_ll_adv_tx_done, advsm);
            rc = ble_phy_tx(rsp, BLE_PHY_TRANSITION_NONE);
            if (!rc) {
                advsm->flags |= BLE_LL_ADV_SM_FLAG_CONN_RSP_TXD;
                STATS_INC(ble_ll_stats, aux_conn_rsp_tx);
            }
            os_mbuf_free_chain(rsp);
        }
#endif
    }

    return rc;
}

/**
 * Called when a connect request has been received.
 *
 * Context: Link Layer
 *
 * @param rxbuf
 * @param flags
 *
 * @return 0: no connection started. 1: connection started
 */
static int
ble_ll_adv_conn_req_rxd(uint8_t *rxbuf, struct ble_mbuf_hdr *hdr,
                        struct ble_ll_adv_sm *advsm)
{
    int valid;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    uint8_t resolved;
#endif
    uint8_t addr_type;
    uint8_t *inita;
    uint8_t *ident_addr;

    /* Don't create connection if AUX_CONNECT_RSP was not send */
    if (!(advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY)) {
        if (!(advsm->flags & BLE_LL_ADV_SM_FLAG_CONN_RSP_TXD)) {
            return 0;
        }
    }

    /* Check filter policy. */
    valid = 0;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    resolved = BLE_MBUF_HDR_RESOLVED(hdr);
#endif
    inita = rxbuf + BLE_LL_PDU_HDR_LEN;
    if (hdr->rxinfo.flags & BLE_MBUF_HDR_F_DEVMATCH) {

        valid = 1;
        if (rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK) {
            addr_type = BLE_ADDR_RANDOM;
        } else {
            addr_type = BLE_ADDR_PUBLIC;
        }

        /*
         * Only accept connect requests from the desired address if we
         * are doing directed advertising
         */
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_DIRECTED) {
            ident_addr = inita;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
            if (resolved) {
                ident_addr = g_ble_ll_resolv_list[advsm->adv_rpa_index].rl_identity_addr;
                addr_type = g_ble_ll_resolv_list[advsm->adv_rpa_index].rl_addr_type;
            }
#endif
            if ((addr_type != advsm->peer_addr_type) ||
                memcmp(advsm->peer_addr, ident_addr, BLE_DEV_ADDR_LEN)) {
                valid = 0;
            }
        }
    }

    if (valid) {
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
        if (resolved) {
            /* Retain the resolvable private address that we received. */
            memcpy(advsm->adv_rpa, inita, BLE_DEV_ADDR_LEN);

            /*
             * Overwrite received inita with identity address since that
             * is used from now on.
             */
            memcpy(inita,
                   g_ble_ll_resolv_list[advsm->adv_rpa_index].rl_identity_addr,
                   BLE_DEV_ADDR_LEN);

            /* Peer address type is an identity address */
            addr_type = g_ble_ll_resolv_list[advsm->adv_rpa_index].rl_addr_type;
            addr_type += 2;
        }
#endif

        /* Try to start slave connection. If successful, stop advertising */
        valid = ble_ll_conn_slave_start(rxbuf, addr_type, hdr,
                          !(advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY));
        if (valid) {
            /* stop advertising only if not transmitting connection response */
            if (!(advsm->flags & BLE_LL_ADV_SM_FLAG_CONN_RSP_TXD)) {
                ble_ll_adv_sm_stop(advsm);
            }
        }
    }

    return valid;
}

/**
 * Called on phy rx pdu end when in advertising state.
 *
 * There are only two pdu types we care about in this state: scan requests
 * and connection requests. When we receive a scan request we must determine if
 * we need to send a scan response and that needs to be acted on within T_IFS.
 *
 * When we receive a connection request, we need to determine if we will allow
 * this device to start a connection with us. However, no immediate response is
 * sent so we handle this at the link layer task.
 *
 * Context: Interrupt
 *
 * @param pdu_type Type of pdu received.
 * @param rxpdu Pointer to received PDU
 *
 * @return int
 *       < 0: Disable the phy after reception.
 *      == 0: Do not disable the PHY
 *       > 0: Do not disable PHY as that has already been done.
 */
int
ble_ll_adv_rx_isr_end(uint8_t pdu_type, struct os_mbuf *rxpdu, int crcok)
{
    int rc;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    struct ble_mbuf_hdr *rxhdr;
#endif

    rc = -1;
    if (rxpdu == NULL) {
        ble_ll_adv_tx_done(g_ble_ll_cur_adv_sm);
    } else {
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        rxhdr = BLE_MBUF_HDR_PTR(rxpdu);
        rxhdr->rxinfo.user_data = g_ble_ll_cur_adv_sm;
        if (ble_ll_adv_active_chanset_is_sec(g_ble_ll_cur_adv_sm)) {
            rxhdr->rxinfo.flags |= BLE_MBUF_HDR_F_EXT_ADV_SEC;
        } else {
            assert(ble_ll_adv_active_chanset_is_pri(g_ble_ll_cur_adv_sm));
        }
#endif
        if (crcok) {
            if ((pdu_type == BLE_ADV_PDU_TYPE_SCAN_REQ) ||
                (pdu_type == BLE_ADV_PDU_TYPE_CONNECT_REQ)) {
                /* Process request */
                rc = ble_ll_adv_rx_req(pdu_type, rxpdu);
            }
        }

        if (rc) {
            /* We no longer have a current state machine */
            g_ble_ll_cur_adv_sm = NULL;
        }
    }

    if (rc) {
        ble_ll_state_set(BLE_LL_STATE_STANDBY);
    }

    return rc;
}

/**
 * Process a received packet at the link layer task when in the advertising
 * state
 *
 * Context: Link Layer
 *
 *
 * @param ptype
 * @param rxbuf
 * @param hdr
 *
 * @return int
 */
void
ble_ll_adv_rx_pkt_in(uint8_t ptype, uint8_t *rxbuf, struct ble_mbuf_hdr *hdr)
{
    int adv_event_over;
    struct ble_ll_adv_sm *advsm;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    advsm = (struct ble_ll_adv_sm *)hdr->rxinfo.user_data;
#else
    advsm = &g_ble_ll_adv_sm[0];
#endif

    /*
     * It is possible that advertising was stopped and a packet plcaed on the
     * LL receive packet queue. In this case, just ignore the received packet
     * as the advertising state machine is no longer "valid"
     */
    if (!advsm->adv_enabled) {
        return;
    }

    /*
     * If we have received a scan request and we are transmitting a response
     * or we have received a valid connect request, dont "end" the advertising
     * event. In the case of a connect request we will stop advertising. In
     * the case of the scan response transmission we will get a transmit
     * end callback.
     */
    adv_event_over = 1;
    if (BLE_MBUF_HDR_CRC_OK(hdr)) {
        if (ptype == BLE_ADV_PDU_TYPE_CONNECT_REQ) {
            if (ble_ll_adv_conn_req_rxd(rxbuf, hdr, advsm)) {
                adv_event_over = 0;
            }
        } else {
            if ((ptype == BLE_ADV_PDU_TYPE_SCAN_REQ) &&
                (hdr->rxinfo.flags & BLE_MBUF_HDR_F_SCAN_RSP_TXD)) {
                adv_event_over = 0;
            }
        }
    }

    if (adv_event_over) {
        ble_ll_adv_make_done(advsm, hdr);
    }
}

/**
 * Called when a receive PDU has started and we are advertising.
 *
 * Context: interrupt
 *
 * @param pdu_type
 * @param rxpdu
 *
 * @return int
 *   < 0: A frame we dont want to receive.
 *   = 0: Continue to receive frame. Dont go from rx to tx
 *   > 0: Continue to receive frame and go from rx to tx when done
 */
int
ble_ll_adv_rx_isr_start(uint8_t pdu_type)
{
    int rc;
    struct ble_ll_adv_sm *advsm;

    /* Assume we will abort the frame */
    rc = -1;

    /* If we get a scan request we must tell the phy to go from rx to tx */
    advsm = g_ble_ll_cur_adv_sm;
    if (pdu_type == BLE_ADV_PDU_TYPE_SCAN_REQ) {
        /* Only accept scan requests if we are indirect adv or scan adv */
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_SCANNABLE) {
            rc = 1;
        }
    } else {
        /* Only accept connect requests if connectable advertising event */
        if (pdu_type == BLE_ADV_PDU_TYPE_CONNECT_REQ) {
            if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_CONNECTABLE) {
                rc = 0;
            }
        }
    }

    /*
     * If we abort the frame, we need to post the LL task to check if the
     * advertising event is over.
     */
    if (rc < 0) {
        ble_ll_adv_tx_done(advsm);
    }

    return rc;
}

static void
ble_ll_adv_drop_event(struct ble_ll_adv_sm *advsm)
{
    STATS_INC(ble_ll_stats, adv_drop_event);

    ble_ll_sched_rmv_elem(&advsm->adv_sch);
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    ble_ll_sched_rmv_elem(&advsm->adv_secondary_sch);
#endif

    advsm->adv_chan = ble_ll_adv_final_chan(advsm);
    os_eventq_put(&g_ble_ll_data.ll_evq, &advsm->adv_txdone_ev);
}

static void
ble_ll_adv_reschedule_event(struct ble_ll_adv_sm *advsm)
{
    int rc;
    uint32_t start_time;
    uint32_t max_delay_ticks;

    assert(advsm->adv_enabled);

    if (!advsm->adv_sch.enqueued) {
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_HD_DIRECTED) {
            max_delay_ticks = 0;
        } else {
            max_delay_ticks =
                    os_cputime_usecs_to_ticks(BLE_LL_ADV_DELAY_MS_MAX * 1000);
        }

        rc = ble_ll_sched_adv_reschedule(&advsm->adv_sch, &start_time,
                                         max_delay_ticks);
        if (rc) {
            ble_ll_adv_drop_event(advsm);
            return;
        }

        start_time += g_ble_ll_sched_offset_ticks;
        advsm->adv_event_start_time = start_time;
        advsm->adv_pdu_start_time = start_time;
    }

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (!(advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) &&
                                        !advsm->adv_secondary_sch.enqueued) {
        ble_ll_set_adv_secondary_start_time(advsm);
        ble_ll_adv_secondary_set_sched(advsm);

         rc = ble_ll_sched_adv_reschedule(&advsm->adv_secondary_sch,
                                          &advsm->adv_secondary_start_time, 0);
         if (rc) {
             ble_ll_adv_drop_event(advsm);
             return;
         }

         advsm->adv_secondary_start_time += g_ble_ll_sched_offset_ticks;
    }
#endif
}

/**
 * Called when an advertising event is over.
 *
 * Context: Link Layer task.
 *
 * @param arg Pointer to advertising state machine.
 */
static void
ble_ll_adv_done(struct ble_ll_adv_sm *advsm)

{
    int rc;
    int resched_pdu;
    uint8_t mask;
    uint8_t final_adv_chan;
    int32_t delta_t;
    uint32_t itvl;
    uint32_t tick_itvl;
    uint32_t start_time;

    assert(advsm->adv_enabled);

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
        /* stop advertising this was due to transmitting connection response */
        if (advsm->flags & BLE_LL_ADV_SM_FLAG_CONN_RSP_TXD) {
            ble_ll_adv_sm_stop(advsm);
            return;
        }
    }
#endif

    /* Remove the element from the schedule if it is still there. */
    ble_ll_sched_rmv_elem(&advsm->adv_sch);

    os_eventq_remove(&g_ble_ll_data.ll_evq, &advsm->adv_txdone_ev);

    /*
     * Check if we have ended our advertising event. If our last advertising
     * packet was sent on the last channel, it means we are done with this
     * event.
     */
    final_adv_chan = ble_ll_adv_final_chan(advsm);

    if (advsm->adv_chan == final_adv_chan) {
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        if (advsm->events_max) {
            advsm->events++;
        }
#endif

        /* Check if we need to resume scanning */
        ble_ll_scan_chk_resume();

        /* Turn off the clock if not doing anything else */
#ifdef BLE_XCVR_RFCLK
        ble_ll_sched_rfclk_chk_restart();
#endif

        /* This event is over. Set adv channel to first one */
        advsm->adv_chan = ble_ll_adv_first_chan(advsm);

        /*
         * Calculate start time of next advertising event. NOTE: we do not
         * add the random advDelay as the scheduling code will do that.
         */
        itvl = advsm->adv_itvl_usecs;
        tick_itvl = os_cputime_usecs_to_ticks(itvl);
        advsm->adv_event_start_time += tick_itvl;
        advsm->adv_pdu_start_time = advsm->adv_event_start_time;

        /*
         * The scheduled time better be in the future! If it is not, we will
         * just keep advancing until we the time is in the future
         */
        start_time = advsm->adv_pdu_start_time - g_ble_ll_sched_offset_ticks;

        delta_t = (int32_t)(start_time - os_cputime_get32());
        if (delta_t < 0) {
            /*
             * NOTE: we just the same interval that we calculated earlier.
             * No real need to keep recalculating a new interval.
             */
            while (delta_t < 0) {
                advsm->adv_event_start_time += tick_itvl;
                advsm->adv_pdu_start_time = advsm->adv_event_start_time;
                delta_t += (int32_t)tick_itvl;
            }
        }
        resched_pdu = 0;
    } else {
        /*
         * Move to next advertising channel. If not in the mask, just
         * increment by 1. We can do this because we already checked if we
         * just transmitted on the last advertising channel
         */
        ++advsm->adv_chan;
        mask = 1 << (advsm->adv_chan - BLE_PHY_ADV_CHAN_START);
        if ((mask & advsm->adv_chanmask) == 0) {
            ++advsm->adv_chan;
        }

        /*
         * We will transmit right away. Set next pdu start time to now
         * plus a xcvr start delay just so we dont count late adv starts
         */
        advsm->adv_pdu_start_time = os_cputime_get32() +
                                    g_ble_ll_sched_offset_ticks;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
        /* If we're past aux (unlikely, but can happen), just drop an event */
        if (!(advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) &&
                (advsm->adv_pdu_start_time > advsm->adv_secondary_start_time)) {
            ble_ll_adv_drop_event(advsm);
            return;
        }
#endif

        resched_pdu = 1;
    }

    /* check if advertising timed out */
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (advsm->duration &&
        advsm->adv_pdu_start_time >= advsm->adv_end_time) {
        /* Legacy PDUs need to be stop here, for ext adv it will be stopped when
         * AUX is done.
         */
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
            ble_ll_hci_ev_send_adv_set_terminated(BLE_ERR_DIR_ADV_TMO,
                                                  advsm->adv_instance, 0, 0);

            /*
             * For high duty directed advertising we need to send connection
             * complete event with proper status
             */
            if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_HD_DIRECTED) {
                ble_ll_conn_comp_event_send(NULL, BLE_ERR_DIR_ADV_TMO,
                                            advsm->conn_comp_ev, advsm);
                advsm->conn_comp_ev = NULL;
            }

            /* Disable advertising */
            advsm->adv_enabled = 0;
            ble_ll_scan_chk_resume();
        }

        return;
    }
#else
    if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_HD_DIRECTED) {
        if (advsm->adv_pdu_start_time >= advsm->adv_end_time) {
            /* Disable advertising */
            advsm->adv_enabled = 0;
            ble_ll_conn_comp_event_send(NULL, BLE_ERR_DIR_ADV_TMO,
                                        advsm->conn_comp_ev, advsm);
            advsm->conn_comp_ev = NULL;
            ble_ll_scan_chk_resume();
            return;
        }
    }
#endif

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (advsm->events_max && (advsm->events >= advsm->events_max)) {
        /* Legacy PDUs need to be stop here, for ext adv it will be stopped when
         * AUX is done.
         */
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY) {
            ble_ll_hci_ev_send_adv_set_terminated(BLE_RR_LIMIT_REACHED,
                                                  advsm->adv_instance, 0,
                                                  advsm->events);

            /* Disable advertising */
            advsm->adv_enabled = 0;
            ble_ll_scan_chk_resume();
        }

        return;
    }
#endif

    /* We need to regenerate our RPA's if we have passed timeout */
#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY) == 1)
    ble_ll_adv_chk_rpa_timeout(advsm);
#endif

    /* Schedule advertising transmit */
    ble_ll_adv_set_sched(advsm);

    if (!resched_pdu) {
        ble_ll_adv_reschedule_event(advsm);
        return;
    }

    /*
     * In the unlikely event we cant reschedule this, just post a done
     * event and we will reschedule the next advertising event
     */
    rc = ble_ll_sched_adv_resched_pdu(&advsm->adv_sch);
    if (rc) {
        STATS_INC(ble_ll_stats, adv_resched_pdu_fail);
        ble_ll_adv_drop_event(advsm);
    }
}

static void
ble_ll_adv_event_done(struct os_event *ev)
{
    ble_ll_adv_done(ev->ev_arg);
}

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
/**
 * Called when auxiliary packet is txd on secondary channel
 *
 * Context: Link Layer task.
 *
 * @param ev
 */
static void
ble_ll_adv_sec_done(struct ble_ll_adv_sm *advsm)
{
    assert(advsm->adv_enabled);

    /* Remove anything else scheduled for secondary channel */
    ble_ll_sched_rmv_elem(&advsm->adv_secondary_sch);
    os_eventq_remove(&g_ble_ll_data.ll_evq, &advsm->adv_sec_txdone_ev);

    /* Stop advertising due to transmitting connection response */
    if (advsm->flags & BLE_LL_ADV_SM_FLAG_CONN_RSP_TXD) {
        ble_ll_adv_sm_stop(advsm);
        return;
    }

    /* Check if we need to resume scanning */
    ble_ll_scan_chk_resume();

    /* Check if advertising timed out */
    if (advsm->duration &&
        advsm->adv_secondary_start_time >= advsm->adv_end_time) {
        ble_ll_hci_ev_send_adv_set_terminated(BLE_ERR_DIR_ADV_TMO,
                                              advsm->adv_instance, 0, 0);

        /*
         * For high duty directed advertising we need to send connection
         * complete event with proper status
         */
        if (advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_HD_DIRECTED) {
            ble_ll_conn_comp_event_send(NULL, BLE_ERR_DIR_ADV_TMO,
                                        advsm->conn_comp_ev, advsm);
            advsm->conn_comp_ev = NULL;
        }

        /* Disable advertising */
        advsm->adv_enabled = 0;
        return;
    }

    if (advsm->events_max && (advsm->events >= advsm->events_max)) {
        ble_ll_hci_ev_send_adv_set_terminated(BLE_RR_LIMIT_REACHED,
                                              advsm->adv_instance, 0,
                                              advsm->events);
         /* Disable advertising */
         advsm->adv_enabled = 0;
         return;
    }

    ble_ll_adv_reschedule_event(advsm);
}

static void
ble_ll_adv_sec_event_done(struct os_event *ev)
{
    ble_ll_adv_sec_done(ev->ev_arg);
}
#endif

static void
ble_ll_adv_make_done(struct ble_ll_adv_sm *advsm, struct ble_mbuf_hdr *hdr)
{
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    if (BLE_MBUF_HDR_EXT_ADV_SEC(hdr)) {
        assert(!(advsm->props & BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY));
        assert(ble_ll_adv_active_chanset_is_sec(advsm));
        ble_ll_adv_active_chanset_clear(advsm);
        ble_ll_adv_sec_done(advsm);
    } else {
        assert(ble_ll_adv_active_chanset_is_pri(advsm));
        ble_ll_adv_active_chanset_clear(advsm);
        ble_ll_adv_done(advsm);
    }
#else
    assert(ble_ll_adv_active_chanset_is_pri(advsm));
    ble_ll_adv_active_chanset_clear(advsm);
    ble_ll_adv_done(advsm);
#endif
}

/**
 * Checks if the controller can change the whitelist. If advertising is enabled
 * and is using the whitelist the controller is not allowed to change the
 * whitelist.
 *
 * @return int 0: not allowed to change whitelist; 1: change allowed.
 */
int
ble_ll_adv_can_chg_whitelist(void)
{
    struct ble_ll_adv_sm *advsm;
    int rc;
    int i;

    rc = 1;
    for (i = 0; i < BLE_ADV_INSTANCES; ++i) {
        advsm = &g_ble_ll_adv_sm[i];
        if (advsm->adv_enabled &&
            (advsm->adv_filter_policy != BLE_HCI_ADV_FILT_NONE)) {
            rc = 0;
            break;
        }
    }

    return rc;
}

/**
 * Sends the connection complete event when advertising a connection starts.
 *
 * @return uint8_t* Pointer to event buffer
 */
void
ble_ll_adv_send_conn_comp_ev(struct ble_ll_conn_sm *connsm,
                             struct ble_mbuf_hdr *rxhdr)
{
    uint8_t *evbuf;
    struct ble_ll_adv_sm *advsm;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    advsm = (struct ble_ll_adv_sm *)rxhdr->rxinfo.user_data;
#else
    advsm = &g_ble_ll_adv_sm[0];
#endif

    evbuf = advsm->conn_comp_ev;
    assert(evbuf != NULL);
    advsm->conn_comp_ev = NULL;

    ble_ll_conn_comp_event_send(connsm, BLE_ERR_SUCCESS, evbuf, advsm);

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    ble_ll_hci_ev_send_adv_set_terminated(0, advsm->adv_instance,
                                          connsm->conn_handle, advsm->events);
#endif

#if (MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_CSA2) == 1)
    ble_ll_hci_ev_le_csa(connsm);
#endif
}

/**
 * Returns the local resolvable private address currently being using by
 * the advertiser
 *
 * @return uint8_t*
 */
uint8_t *
ble_ll_adv_get_local_rpa(struct ble_ll_adv_sm *advsm)
{
    uint8_t *rpa;

    rpa = NULL;
    if (advsm->own_addr_type > BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        rpa = advsm->adva;
    }

    return rpa;
}

/**
 * Returns the peer resolvable private address of last device connecting to us
 *
 * @return uint8_t*
 */
uint8_t *
ble_ll_adv_get_peer_rpa(struct ble_ll_adv_sm *advsm)
{
    /* XXX: should this go into IRK list or connection? */
    return advsm->adv_rpa;
}

/**
 * Called when the LL wait for response timer expires while in the advertising
 * state. Disables the phy and
 *
 */
void
ble_ll_adv_wfr_timer_exp(void)
{
    ble_phy_disable();
    ble_ll_adv_tx_done(g_ble_ll_cur_adv_sm);
}

/**
 * Reset the advertising state machine.
 *
 * Context: Link Layer task
 *
 */
void
ble_ll_adv_reset(void)
{
    int i;
    struct ble_ll_adv_sm *advsm;

    for (i = 0; i < BLE_ADV_INSTANCES; ++i) {
        /* Stop advertising state machine */
        advsm = &g_ble_ll_adv_sm[i];
        ble_ll_adv_sm_stop(advsm);
    }

    /* re-initialize the advertiser state machine */
    ble_ll_adv_init();
}

/* Called to determine if advertising is enabled.
 *
 * NOTE: this currently only applies to the default advertising index.
 */
uint8_t
ble_ll_adv_enabled(void)
{
    return g_ble_ll_adv_sm[0].adv_enabled;
}

static void
ble_ll_adv_sm_init(struct ble_ll_adv_sm *advsm)
{
    uint8_t i = advsm->adv_instance;

    memset(advsm, 0, sizeof(struct ble_ll_adv_sm));

    advsm->adv_instance = i;
    advsm->adv_itvl_min = BLE_HCI_ADV_ITVL_DEF;
    advsm->adv_itvl_max = BLE_HCI_ADV_ITVL_DEF;
    advsm->adv_chanmask = BLE_HCI_ADV_CHANMASK_DEF;

    /* Initialize advertising tx done event */
    advsm->adv_txdone_ev.ev_cb = ble_ll_adv_event_done;
    advsm->adv_txdone_ev.ev_arg = advsm;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    advsm->adv_sec_txdone_ev.ev_cb = ble_ll_adv_sec_event_done;
    advsm->adv_sec_txdone_ev.ev_arg = advsm;
#endif

    /*XXX Configure instances to be legacy on start */
    advsm->props |= BLE_HCI_LE_SET_EXT_ADV_PROP_SCANNABLE;
    advsm->props |= BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY;
}

/**
 * Initialize the advertising functionality of a BLE device. This should
 * be called once on initialization
 */
void
ble_ll_adv_init(void)
{
    int i;

    /* Set default advertising parameters */
    for (i = 0; i < BLE_ADV_INSTANCES; ++i) {
        g_ble_ll_adv_sm[i].adv_instance = i;
        ble_ll_adv_sm_init(&g_ble_ll_adv_sm[i]);
    }
}

