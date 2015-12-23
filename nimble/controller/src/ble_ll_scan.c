/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "bsp/bsp.h"
#include "os/os.h"
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "controller/ble_phy.h"
#include "controller/ble_ll.h"
#include "controller/ble_ll_sched.h"
#include "controller/ble_ll_adv.h"
#include "controller/ble_ll_scan.h"
#include "controller/ble_ll_hci.h"
#include "controller/ble_ll_whitelist.h"
#include "hal/hal_cputime.h"
#include "hal/hal_gpio.h"

/* 
 * XXX:
 * 1) Do I need to know if the address is random or public? Or
 * is there no chance that a public address will match a random
 * address? This applies to checking for advertisers that we have heard a scan 
 * response from or sent an advertising report for.
 * 
 * 2) I think I can guarantee that we dont process things out of order if
 * I send an event when a scan request is sent. The scan_rsp_pending flag
 * code might be made simpler.
 * 
 * 3) Interleave sending scan requests to different advertisers? I guess I need 
 * a list of advertisers to which I sent a scan request and have yet to
 * receive a scan response from? Implement this.
 */

/* The scanning state machine global object */
struct ble_ll_scan_sm g_ble_ll_scan_sm;

/* Scanning stats */
struct ble_ll_scan_stats
{
    uint32_t scan_starts;
    uint32_t scan_stops;
    uint32_t scan_win_misses;
    uint32_t cant_set_sched;
    uint32_t scan_req_txf;
    uint32_t scan_req_txg;
};

struct ble_ll_scan_stats g_ble_ll_scan_stats;

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

/* Contains list of advertisers that we have heard scan responses from */
uint8_t g_ble_ll_scan_num_rsp_advs;
struct ble_ll_scan_advertisers g_ble_ll_scan_rsp_advs[BLE_LL_SCAN_CFG_NUM_SCAN_RSP_ADVS];

/* Used to filter duplicate advertising events to host */
uint8_t g_ble_ll_scan_num_dup_advs;
struct ble_ll_scan_advertisers g_ble_ll_scan_dup_advs[BLE_LL_SCAN_CFG_NUM_DUP_ADVS];

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
        ++g_ble_ll_scan_stats.scan_req_txg;
    } else {
        scansm->scan_rsp_cons_ok = 0;
        ++scansm->scan_rsp_cons_fails;
        if (scansm->scan_rsp_cons_fails == 2) {
            scansm->scan_rsp_cons_fails = 0;
            if (scansm->upper_limit < 256) {
                scansm->upper_limit <<= 1;
            }
        }
        ++g_ble_ll_scan_stats.scan_req_txf;
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
 * @param addr_type 0 if public; non-zero if random
 */
static void
ble_ll_scan_req_pdu_make(struct ble_ll_scan_sm *scansm, uint8_t *adv_addr,
                         uint8_t adv_addr_type)
{
    uint8_t     *dptr;
    uint8_t     pdu_type;
    uint8_t     *addr;
    struct os_mbuf *m;

    /* Construct first PDU header byte */
    pdu_type = BLE_ADV_PDU_TYPE_SCAN_REQ;
    if (adv_addr_type) {
        pdu_type |= BLE_ADV_PDU_HDR_RXADD_RAND;
    }

    /* Get the advertising PDU */
    m = scansm->scan_req_pdu;
    assert(m != NULL);
    m->om_len = BLE_SCAN_REQ_LEN + BLE_LL_PDU_HDR_LEN;
    OS_MBUF_PKTHDR(m)->omp_len = m->om_len;

    /* Get pointer to our device address */
    if (scansm->own_addr_type == BLE_HCI_ADV_OWN_ADDR_PUBLIC) {
        addr = g_dev_addr;
    } else if (scansm->own_addr_type == BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        pdu_type |= BLE_ADV_PDU_HDR_TXADD_RAND;
        addr = g_random_addr;
    } else {
        /* XXX: unsupported for now  */
        addr = NULL;
        assert(0);
    }

    /* Construct the scan request */
    dptr = m->om_data;
    dptr[0] = pdu_type;
    dptr[1] = BLE_SCAN_REQ_LEN;
    memcpy(dptr + BLE_LL_PDU_HDR_LEN, addr, BLE_DEV_ADDR_LEN);
    memcpy(dptr + BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN, adv_addr, 
           BLE_DEV_ADDR_LEN);
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
                    continue;
                }
            } else {
                if (adv->sc_adv_flags & BLE_LL_SC_ADV_F_RANDOM_ADDR) {
                    continue;
                }
            }

            return adv;
        }
        ++adv;
        --num_advs;
    }

    return NULL;
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
 * @param addr 
 * @param Txadd. TxAdd bit (0 public, random otherwise)
 */
void
ble_ll_scan_add_dup_adv(uint8_t *addr, uint8_t txadd)
{
    uint8_t num_advs;
    struct ble_ll_scan_advertisers *adv;

    /* Check to see if on list. */
    adv = ble_ll_scan_find_dup_adv(addr, txadd);
    if (!adv) {
        /* XXX: for now, if we dont have room, just leave */
        num_advs = g_ble_ll_scan_num_dup_advs;
        if (num_advs == BLE_LL_SCAN_CFG_NUM_DUP_ADVS) {
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

    /* 
     * XXX: need to set correct flag based on type of report being sent
     * for now, we dont send direct advertising reports
     */
    adv->sc_adv_flags |= BLE_LL_SC_ADV_F_ADV_RPT_SENT;
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
    if (num_advs == BLE_LL_SCAN_CFG_NUM_SCAN_RSP_ADVS) {
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

/**
 * Send an advertising report to the host.
 * 
 * NOTE: while we are allowed to send multiple devices in one report, we 
 * will just send for one for now. 
 * 
 * @param pdu_type 
 * @param txadd 
 * @param rxbuf 
 * @param rssi 
 */
static void
ble_ll_hci_send_adv_report(uint8_t pdu_type, uint8_t txadd, uint8_t *rxbuf,
                           int8_t rssi)
{
    int rc;
    uint8_t evtype;
    uint8_t subev;
    uint8_t *evbuf;
    uint8_t adv_data_len;

    subev = BLE_HCI_LE_SUBEV_ADV_RPT;
    if (pdu_type == BLE_ADV_PDU_TYPE_ADV_DIRECT_IND) {
        /* XXX: NOTE: the direct advertising report is only used when InitA
           is a resolvable private address. We dont support that yet! */
        //subev = BLE_HCI_LE_SUBEV_DIRECT_ADV_RPT;
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
        subev = BLE_HCI_LE_SUBEV_ADV_RPT;

        adv_data_len = rxbuf[1] & BLE_ADV_PDU_HDR_LEN_MASK;
        adv_data_len -= BLE_DEV_ADDR_LEN;
    }

    if (ble_ll_hci_is_le_event_enabled(subev - 1)) {
        evbuf = os_memblock_get(&g_hci_cmd_pool);
        if (evbuf) {
            evbuf[0] = BLE_HCI_EVCODE_LE_META;
            evbuf[1] = 12 + adv_data_len;
            evbuf[2] = subev;
            evbuf[3] = 1;       /* number of reports */
            evbuf[4] = evtype;

            /* XXX: need to deal with resolvable addresses here! */
            if (txadd) {
                evbuf[5] = BLE_HCI_ADV_OWN_ADDR_RANDOM;
            } else {
                evbuf[5] = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
            }
            rxbuf += BLE_LL_PDU_HDR_LEN;
            memcpy(evbuf + 6, rxbuf, BLE_DEV_ADDR_LEN);
            evbuf[12] = adv_data_len;
            memcpy(evbuf + 13, rxbuf + BLE_DEV_ADDR_LEN,
                   adv_data_len);
            evbuf[13 + adv_data_len] = rssi;

            rc = ble_ll_hci_event_send(evbuf);
            if (!rc) {
                /* If filtering, add it to list of duplicate addresses */
                if (g_ble_ll_scan_sm.scan_filt_dups) {
                    ble_ll_scan_add_dup_adv(rxbuf, txadd);
                }
            }
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
 * @param rxbuf 
 * 
 * @return int 0: pdu allowed by filter policy. 1: pdu not allowed
 */
int
ble_ll_scan_chk_filter_policy(uint8_t pdu_type, uint8_t *rxbuf, uint8_t flags)
{
    uint8_t *addr;
    uint8_t addr_type;
    int use_whitelist;
    int chk_inita;

    use_whitelist = 0;
    chk_inita = 0;

    switch (g_ble_ll_scan_sm.scan_filt_policy) {
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
        if (flags & BLE_MBUF_HDR_F_DEVMATCH) {
            return 0;
        } else {
            return 1;
        }
    }

    /* If this is a directed advertisement, check that it is for us */
    if (pdu_type == BLE_ADV_PDU_TYPE_ADV_DIRECT_IND) {
        /* Is this for us? If not, is it resolvable */
        addr = rxbuf + BLE_LL_PDU_HDR_LEN;
        addr_type = rxbuf[0] & BLE_ADV_PDU_HDR_RXADD_MASK;
        if (!ble_ll_is_our_devaddr(addr + BLE_DEV_ADDR_LEN, addr_type)) {
            if (!chk_inita || !ble_ll_is_resolvable_priv_addr(addr)) {
                return 1;
            }
        }
    }

    return 0;
}

static int
ble_ll_scan_win_end_cb(struct ble_ll_sched_item *sch)
{
    ble_phy_disable();
    ble_ll_event_send(&g_ble_ll_scan_sm.scan_win_end_ev);
    return BLE_LL_SCHED_STATE_DONE;
}

/**
 * Schedule callback for the start of a scan window 
 *  
 * Context: Interrupt 
 * 
 * @param sch 
 * 
 * @return int 
 */
static int
ble_ll_scan_start_cb(struct ble_ll_sched_item *sch)
{
    int rc;
    struct ble_ll_scan_sm *scansm;

    /* Toggle the LED */
    gpio_toggle(LED_BLINK_PIN);

    /* Get the state machine for the event */
    scansm = (struct ble_ll_scan_sm *)sch->cb_arg;

    /* Set channel */
    rc = ble_phy_setchan(scansm->scan_chan, 0, 0);
    assert(rc == 0);

    /* Start receiving */
    rc = ble_phy_rx();
    if (rc) {
        /* Failure to go into receive. Just call the window end function */
        ble_ll_event_send(&g_ble_ll_scan_sm.scan_win_end_ev);
        rc =  BLE_LL_SCHED_STATE_DONE;
    } else {
        /* Set link layer state to scanning */
        if (g_ble_ll_scan_sm.scan_type == BLE_SCAN_TYPE_INITIATE) {
            ble_ll_state_set(BLE_LL_STATE_INITIATING);
        } else {
            ble_ll_state_set(BLE_LL_STATE_SCANNING);
        }

        /* Set end time to end of scan window */
        sch->next_wakeup = sch->end_time;
        sch->sched_cb = ble_ll_scan_win_end_cb;
        rc = BLE_LL_SCHED_STATE_RUNNING;
    }

    return rc;
}

/**
 * Stop the scanning state machine 
 */
void
ble_ll_scan_sm_stop(struct ble_ll_scan_sm *scansm, int conn_created)
{
    /* Remove any scheduled advertising items */
    ble_ll_sched_rmv(BLE_LL_SCHED_TYPE_SCAN, NULL);

    /* Disable scanning state machine */
    scansm->scan_enabled = 0;

    /* Count # of times stopped */
    ++g_ble_ll_scan_stats.scan_stops;

    /* Only set state if we are current in a scan window */
    if ((g_ble_ll_data.ll_state == BLE_LL_STATE_SCANNING) ||
        (g_ble_ll_data.ll_state == BLE_LL_STATE_INITIATING)) {
        /* Disable the PHY */
        if (!conn_created) {
            ble_phy_disable();
        }

        /* Disable whitelisting  */
        ble_ll_whitelist_disable();

        /* Set LL state to standby */
        ble_ll_state_set(BLE_LL_STATE_STANDBY);
    }
}

static struct ble_ll_sched_item *
ble_ll_scan_sched_set(struct ble_ll_scan_sm *scansm)
{
    int rc;
    struct ble_ll_sched_item *sch;

    sch = ble_ll_sched_get_item();
    if (sch) {
        /* Set sched type */
        sch->sched_type = BLE_LL_SCHED_TYPE_SCAN;

        /* Set the start time of the event */
        sch->start_time = scansm->scan_win_start_time - 
            cputime_usecs_to_ticks(XCVR_RX_SCHED_DELAY_USECS);

        /* Set the callback, arg, start and end time */
        sch->cb_arg = scansm;
        sch->sched_cb = ble_ll_scan_start_cb;
        sch->end_time = scansm->scan_win_start_time + 
            cputime_usecs_to_ticks(scansm->scan_window * BLE_HCI_SCAN_ITVL);

        /* XXX: for now, we cant get an overlap so assert on error. */
        /* Add the item to the scheduler */
        rc = ble_ll_sched_add(sch);
        assert(rc == 0);
    } else {
        ++g_ble_ll_scan_stats.cant_set_sched;
    }

    return sch;
}

static int
ble_ll_scan_sm_start(struct ble_ll_scan_sm *scansm)
{
    struct ble_ll_sched_item *sch;

    /* 
     * XXX: not sure if I should do this or just report whatever random
     * address the host sent. For now, I will reject the command with a
     * command disallowed error. All the parameter errors refer to the command
     * parameter (which in this case is just enable or disable).
     */ 
    if (scansm->own_addr_type != BLE_HCI_ADV_OWN_ADDR_PUBLIC) {
        if (!ble_ll_is_valid_random_addr(g_random_addr)) {
            return BLE_ERR_CMD_DISALLOWED;
        }

        /* XXX: support these other types */
        if (scansm->own_addr_type != BLE_HCI_ADV_OWN_ADDR_RANDOM) {
            assert(0);
        }
    }

    /* Enable/disable whitelisting */
    if (scansm->scan_filt_policy & 1) {
        ble_ll_whitelist_enable();
    } else {
        ble_ll_whitelist_disable();
    }

    /* Count # of times started */
    ++g_ble_ll_scan_stats.scan_starts;

    /* Set flag telling us that scanning is enabled */
    scansm->scan_enabled = 1;

    /* Set first advertising channel */
    scansm->scan_chan = BLE_PHY_ADV_CHAN_START;

    /* Reset scan request backoff parameters to default */
    scansm->upper_limit = 1;
    scansm->backoff_count = 1;
    scansm->scan_rsp_pending = 0;

    /* Schedule start time now */
    scansm->scan_win_start_time = cputime_get32();

    /* Set packet in schedule */
    sch = ble_ll_scan_sched_set(scansm);
    if (!sch) {
        /* XXX: set a wakeup timer to deal with this. For now, assert */
        assert(0);
    }

    return BLE_ERR_SUCCESS;
}

void
ble_ll_scan_win_end_proc(void *arg)
{
    int32_t delta_t;
    uint32_t itvl;
    uint32_t win_ticks;
    struct ble_ll_scan_sm *scansm;

    /* Toggle the LED */
    gpio_toggle(LED_BLINK_PIN);

    /* We are no longer scanning  */
    scansm = (struct ble_ll_scan_sm *)arg;
    ble_ll_state_set(BLE_LL_STATE_STANDBY);

    /* Move to next channel */
    ++scansm->scan_chan;
    if (scansm->scan_chan == BLE_PHY_NUM_CHANS) {
        scansm->scan_chan = BLE_PHY_ADV_CHAN_START;
    }

    /* If there is a scan response pending, it means we failed a scan req */
    if (scansm->scan_rsp_pending) {
        ble_ll_scan_req_backoff(scansm, 0);
    }

    /* Calculate # of ticks per scan window */
    win_ticks = cputime_usecs_to_ticks(scansm->scan_window * BLE_HCI_SCAN_ITVL);

    /* Set next scan window start time */
    itvl = cputime_usecs_to_ticks(scansm->scan_itvl * BLE_HCI_SCAN_ITVL);
    scansm->scan_win_start_time += itvl;
      
    /* Set next scanning window start. */        
    delta_t = (int32_t)(cputime_get32() - scansm->scan_win_start_time);
    while (delta_t >= (int32_t)win_ticks) {
        /* 
         * Since it is possible to scan continuously, it is possible
         * that we will be late here if the scan window is equal to the
         * scan interval (or very close). So what we will do here is only
         * increment a stat if we missed a scan window
         */
        /* Count times we were late */
        ++g_ble_ll_scan_stats.scan_win_misses;

        /* Calculate start time of next scan windowe */
        scansm->scan_win_start_time += itvl;
        delta_t -= (int32_t)itvl;
    }

    if (!ble_ll_scan_sched_set(scansm)) {
        /* XXX: we will need to set a timer here to wake us up */
        assert(0);
    }
}

/**
 * ble ll scan rx pdu start 
 *  
 * Called when a PDU reception has started and the Link Layer is in the 
 * scanning state. 
 * 
 * Context: Interrupt 
 *  
 * @param rxpdu Pointer to where received data is being stored.
 * 
 * @return int 
 *  0: we will not attempt to reply to this frame
 *  1: we may send a response to this frame.
 */
int
ble_ll_scan_rx_pdu_start(uint8_t pdu_type, struct os_mbuf *rxpdu)
{
    int rc;
    struct ble_ll_scan_sm *scansm;
    struct ble_mbuf_hdr *ble_hdr;

    rc = 0;
    scansm = &g_ble_ll_scan_sm;

    switch (scansm->scan_type) {
    case BLE_SCAN_TYPE_ACTIVE:
        /* If adv ind or scan ind, we may send scan request */
        if ((pdu_type == BLE_ADV_PDU_TYPE_ADV_IND) ||
            (pdu_type == BLE_ADV_PDU_TYPE_ADV_SCAN_IND)) {
            rc = 1;
        }

        /* 
         * If this is the first PDU after we sent the scan response (as
         * denoted by the scan rsp pending flag, we set a bit in the ble
         * header so the link layer can check to see if the scan request
         * was successful. We do it this way to let the Link Layer do the
         * work for successful scan requests.
         */ 
        if (scansm->scan_rsp_pending) {
            ble_hdr = BLE_MBUF_HDR_PTR(rxpdu);
            ble_hdr->flags |= BLE_MBUF_HDR_F_SCAN_RSP_CHK;
        }
        break;
    case BLE_SCAN_TYPE_PASSIVE:
    default:
        break;
    }

    return rc;
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
ble_ll_scan_rx_pdu_end(struct os_mbuf *rxpdu)
{
    int rc;
    int chk_send_req;
    int chk_whitelist;
    uint8_t pdu_type;
    uint8_t addr_type;
    uint8_t *adv_addr;
    uint8_t *rxbuf;
    struct ble_mbuf_hdr *ble_hdr;
    struct ble_ll_scan_sm *scansm;

    /* If passively scanning return (with -1) since no scan request is sent */
    scansm = &g_ble_ll_scan_sm;

    /* Get pdu type, pointer to address and address "type"  */
    rxbuf = rxpdu->om_data;
    pdu_type = rxbuf[0] & BLE_ADV_PDU_HDR_TYPE_MASK;
    adv_addr = rxbuf + BLE_LL_PDU_HDR_LEN;
    if (rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK) {
        addr_type = BLE_ADDR_TYPE_RANDOM;
    } else {
        addr_type = BLE_ADDR_TYPE_PUBLIC;
    }

    /* Determine if request may be sent and if whitelist needs to be checked */
    chk_send_req = 0;
    switch (pdu_type) {
    case BLE_ADV_PDU_TYPE_ADV_IND:
    case BLE_ADV_PDU_TYPE_ADV_SCAN_IND:
        if (scansm->scan_type == BLE_SCAN_TYPE_ACTIVE) {
            chk_send_req = 1;
        }
        chk_whitelist = 1;
        break;
    case BLE_ADV_PDU_TYPE_ADV_NONCONN_IND:
    case BLE_ADV_PDU_TYPE_ADV_DIRECT_IND:
        chk_whitelist = 1;
        break;
    default:
        chk_whitelist = 0;
        break;
    }

    /* Set device match bit if we are whitelisting */
    if (chk_whitelist && (scansm->scan_filt_policy & 1)) {
        /* Check if device is on whitelist. If not, leave */
        if (!ble_ll_whitelist_match(adv_addr, addr_type)) {
            return -1;
        }
        ble_hdr = BLE_MBUF_HDR_PTR(rxpdu);
        ble_hdr->flags |= BLE_MBUF_HDR_F_DEVMATCH;
    }

    /* Should we send a scan request? */
    rc = -1;
    if (chk_send_req) {
        /* 
         * Check to see if we have received a scan response from this
         * advertisor. If so, no need to send scan request.
         */
        if (ble_ll_scan_have_rxd_scan_rsp(adv_addr, addr_type)) {
            return -1;
        }

        /* Better not be a scan response pending */
        assert(scansm->scan_rsp_pending == 0);

        /* We want to send a request. See if backoff allows us */
        --scansm->backoff_count;
        if (scansm->backoff_count == 0) {
            /* Setup to transmit the scan request */
            ble_ll_scan_req_pdu_make(scansm, adv_addr, addr_type);
            rc = ble_phy_tx(scansm->scan_req_pdu, BLE_PHY_TRANSITION_RX_TX,
                            BLE_PHY_TRANSITION_TX_RX, NULL, NULL);

            /* Set "waiting for scan response" flag */
            scansm->scan_rsp_pending = 1;
        }
    }

    return rc;
}

/**
 * Called to resume scanning, usually after a packet has been received 
 * while in the scanning or inititating state.
 */
void
ble_ll_scan_resume(void)
{
    /* We need to re-enable the PHY if we are in idle state */
    if (ble_phy_state_get() == BLE_PHY_STATE_IDLE) {
        if (ble_phy_rx()) {
            /* If we fail, we will end the current scan window. */
            ble_ll_sched_rmv(BLE_LL_SCHED_TYPE_SCAN, NULL);
            ble_ll_event_send(&g_ble_ll_scan_sm.scan_win_end_ev);
        }
    }
}

/**
 * Called when the wait for response timer expires while in the scanning 
 * state. 
 */
void
ble_ll_scan_wfr_timer_exp(void)
{
    struct ble_ll_scan_sm *scansm;

    ble_phy_disable();

    /* 
     * If we timed out waiting for a response, the scan response pending
     * flag should be set. Deal with scan backoff.
     */ 
    scansm = &g_ble_ll_scan_sm;
    if (scansm->scan_rsp_pending) {
        ble_ll_scan_req_backoff(scansm, 0);
    }
    
    ble_ll_scan_resume();
}

/**
 * Process a received PDU while in the scanning state.
 *  
 * Context: Link Layer task. 
 * 
 * @param pdu_type 
 * @param rxbuf 
 */
void
ble_ll_scan_rx_pkt_in(uint8_t ptype, uint8_t *rxbuf, struct ble_mbuf_hdr *hdr)
{
    uint8_t *adv_addr;
    uint8_t *adva;
    uint8_t txadd;
    uint8_t rxadd;
    struct ble_ll_scan_sm *scansm;
    uint8_t scan_rsp_chk;

    /* Set scan response check flag */
    scan_rsp_chk = hdr->flags & BLE_MBUF_HDR_F_SCAN_RSP_CHK;

    /* We dont care about scan requests or connect requests */
    if ((!hdr->crcok) || (ptype == BLE_ADV_PDU_TYPE_SCAN_REQ) || 
        (ptype == BLE_ADV_PDU_TYPE_CONNECT_REQ)) {
        goto scan_continue;
    }

    /* Check the scanner filter policy */
    if (ble_ll_scan_chk_filter_policy(ptype, rxbuf, hdr->flags)) {
        goto scan_continue;
    }

    /* Get advertisers address type and a pointer to the address */
    txadd = rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK;
    adv_addr = rxbuf + BLE_LL_PDU_HDR_LEN;

    /* 
     * XXX: The BLE spec is a bit unclear here. What if we get a scan
     * response from an advertiser that we did not send a request to?
     * Do we send an advertising report? Do we add it to list of devices
     * that we have heard a scan response from?
     */
    scansm = &g_ble_ll_scan_sm;
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
            adva = scansm->scan_req_pdu->om_data + BLE_LL_PDU_HDR_LEN +
                   BLE_DEV_ADDR_LEN;
            rxadd = scansm->scan_req_pdu->om_data[0] & 
                    BLE_ADV_PDU_HDR_RXADD_MASK;
            if (((txadd && rxadd) || ((txadd + rxadd) == 0)) &&
                !memcmp(adv_addr, adva, BLE_DEV_ADDR_LEN)) {
                /* We have received a scan response. Add to list */
                ble_ll_scan_add_scan_rsp_adv(adv_addr, txadd);

                /* Perform scan request backoff procedure */
                ble_ll_scan_req_backoff(scansm, 1);
            }
        }
    }

    /* Filter duplicates */
    if (scansm->scan_filt_dups) {
        if (ble_ll_scan_is_dup_adv(ptype, txadd, adv_addr)) {
            goto scan_continue;
        }
    }

    /* Send the advertising report */
    ble_ll_hci_send_adv_report(ptype, txadd, rxbuf, hdr->rssi);

scan_continue:
    /* 
     * If the scan response check bit is set and we are pending a response,
     * we have failed the scan request (as we would have reset the scan rsp
     * pending flag if we received a valid response
     */
    if (scansm->scan_rsp_pending && scan_rsp_chk) {
        ble_ll_scan_req_backoff(scansm, 1);
    }
    ble_ll_scan_resume();
    return;
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

    /* If already enabled, we return an error */
    scansm = &g_ble_ll_scan_sm;
    if (scansm->scan_enabled) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* Get the scan interval and window */
    scan_type = cmd[0];
    scan_itvl  = le16toh(cmd + 1);
    scan_window = le16toh(cmd + 3);
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

    /* Set state machine parameters */
    scansm->scan_type = scan_type;
    scansm->scan_itvl = scan_itvl;
    scansm->scan_window = scan_window;
    scansm->scan_filt_policy = filter_policy;
    scansm->own_addr_type = own_addr_type;

    return 0;
}

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
ble_ll_scan_set_enable(uint8_t *cmd)
{
    int rc;
    uint8_t filter_dups;
    uint8_t enable;
    struct ble_ll_scan_sm *scansm;

    /* Check for valid parameters */
    enable = cmd[0];
    filter_dups = cmd[1];
    if ((filter_dups > 1) || (enable > 1)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    rc = BLE_ERR_SUCCESS;
    scansm = &g_ble_ll_scan_sm;
    if (enable) {
        /* If already enabled, do nothing */
        if (!scansm->scan_enabled) {
            /* Start the scanning state machine */
            scansm->scan_filt_dups = filter_dups;
            rc = ble_ll_scan_sm_start(scansm);
        }
    } else {
        if (scansm->scan_enabled) {
            ble_ll_scan_sm_stop(scansm, 0);
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

    scansm = &g_ble_ll_scan_sm;
    if (scansm->scan_enabled && (scansm->scan_filt_policy & 1)) {
        rc = 0;
    } else {
        rc = 1;
    }

    return rc;
}

int
ble_ll_scan_initiator_start(struct hci_create_conn *hcc)
{
    struct ble_ll_scan_sm *scansm;

    scansm = &g_ble_ll_scan_sm;
    scansm->scan_type = BLE_SCAN_TYPE_INITIATE;
    scansm->scan_itvl = hcc->scan_itvl;
    scansm->scan_window = hcc->scan_window;
    scansm->scan_filt_policy = hcc->filter_policy;
    scansm->own_addr_type = hcc->own_addr_type;
    return ble_ll_scan_sm_start(scansm);
}

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

/* Returns the PDU allocated by the scanner */
struct os_mbuf *
ble_ll_scan_get_pdu(void)
{
    return g_ble_ll_scan_sm.scan_req_pdu;
}

/* Returns the global scanning state machine */
struct ble_ll_scan_sm *
ble_ll_scan_sm_get(void)
{
    return &g_ble_ll_scan_sm;
}

/* Returns true if whitelist is enabled for scanning */
int
ble_ll_scan_whitelist_enabled(void)
{
    return g_ble_ll_scan_sm.scan_filt_policy & 1;
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
        ble_ll_scan_sm_stop(scansm, 0);
    }

    /* Reset all statistics */
    memset(&g_ble_ll_scan_stats, 0, sizeof(struct ble_ll_scan_stats));

    /* Free the scan request pdu */
    os_mbuf_free(scansm->scan_req_pdu);

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

    /* Clear state machine in case re-initialized */
    scansm = &g_ble_ll_scan_sm;
    memset(scansm, 0, sizeof(struct ble_ll_scan_sm));

    /* Initialize scanning window end event */
    scansm->scan_win_end_ev.ev_type = BLE_LL_EVENT_SCAN_WIN_END;
    scansm->scan_win_end_ev.ev_arg = scansm;

    /* Set all non-zero default parameters */
    scansm->scan_itvl = BLE_HCI_SCAN_ITVL_DEF;
    scansm->scan_window = BLE_HCI_SCAN_WINDOW_DEF;

    /* Get a scan request mbuf (packet header) and attach to state machine */
    ble_get_packet(scansm->scan_req_pdu);
    assert(scansm->scan_req_pdu != NULL);
}

