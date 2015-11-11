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
#include "controller/ble_ll.h"
#include "controller/ble_ll_conn.h"
#include "controller/ble_ll_scan.h"
#include "controller/ble_phy.h"

/* XXX TODO
 * 1) Implemement connection supervisor timeout.
 * 2) Create connection request.
 * 3) Process rxd connection request.
 * 4) What about channel map. Where is that set?
 * 5) Review data channel index selection
 * 6) Closing the connection: the MD bit.
 * 7) notifying host about connections: how often do we need to do this? On
 * creation, establishment and/or loss?
 * 8) Deal with window widening. Easy...
 * 9) Close connection if window widening gets too big! 4.5.7
 * 10) portions of the connect request need to be modified (possibly) based
 * on the advertiser.
 */

/* Scanning state machine */
struct ble_ll_conn_sm
{
    /* Used to calculate data channel index for connection */
    uint8_t unmapped_chan;
    uint8_t last_unmapped_chan;
    uint8_t num_used_chans;
    uint8_t chanmap[5];
    uint8_t hop_inc;
    uint8_t conn_state;
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint8_t peer_addr[BLE_DEV_ADDR_LEN];
    uint16_t conn_spvn_tmo;
    uint16_t conn_slave_latency;
    uint16_t conn_event_cntr;
    uint32_t access_addr;
    struct os_mbuf *conn_req_pdu;
};

struct ble_ll_conn_sm g_ble_ll_conn_sm;

/* Connection states */
#define BLE_LL_CONN_STATE_IDLE          (0)
#define BLE_LL_CONN_STATE_CREATED       (1)
#define BLE_LL_CONN_STATE_ESTABLISHED   (2)

static uint32_t
ble_ll_conn_calc_access_addr(void)
{
    uint32_t aa;
    uint16_t aa_low;
    uint16_t aa_high;
    uint32_t temp;
    uint32_t mask;
    uint32_t prev_bit;
    uint8_t bits_diff;
    uint8_t consecutive;
    uint8_t transitions;

    /* Calculate a random access address */
    aa = 0;
    while (1) {
        /* Get two, 16-bit random numbers */
        aa_low = rand() & 0xFFFF;
        aa_high = rand() & 0xFFFF;

        /* All four bytes cannot be equal */
        if (aa_low == aa_high) {
            continue;
        }

        /* Upper 6 bits must have 2 transitions */
        temp = aa_high & 0xFC00;
        if ((temp == 0) || (temp == 0xFC00)) {
            continue;
        }

        /* Cannot be access address or be 1 bit different */
        aa = aa_high;
        aa = (aa_high << 16) | aa_low;
        bits_diff = 0;
        mask = 0x00000001;
        temp = aa ^ BLE_ACCESS_ADDR_ADV;
        for (mask = 0; mask <= 0x80000000; mask <<= 1) {
            if (mask & temp) {
                ++bits_diff;
                if (bits_diff > 1) {
                    break;
                }
            }
        }
        if (bits_diff <= 1) {
            continue;
        }

        /* Cannot have more than 24 transitions */
        transitions = 0;
        consecutive = 0;
        mask = 0x00000001;
        prev_bit = aa & mask;
        while (mask < 0x80000000) {
            mask <<= 1;
            if (mask & aa) {
                if (prev_bit == 0) {
                    ++transitions;
                    consecutive = 0;
                } else {
                    ++consecutive;
                }
            } else {
                if (prev_bit == 0) {
                    ++consecutive;
                } else {
                    ++transitions;
                    consecutive = 0;
                }
            }

            /* This is invalid! */
            if (consecutive > 6) {
                break;
            }
        }

        /* Cannot be more than 24 transitions */
        if ((consecutive > 6) || (transitions > 24)) {
            continue;
        }
    }
    return aa;
}

/**
 * Determine the next data channel to be used for the connection. 
 * 
 * @param conn 
 * 
 * @return uint8_t 
 */
uint8_t
ble_ll_next_data_channel(struct ble_ll_conn_sm *conn)
{
    int     i;
    int     j;
    uint8_t curchan;
    uint8_t remap_index;
    uint8_t bitpos;
    uint8_t cntr;
    uint8_t mask;
    uint8_t usable_chans;

    /* Get next un mapped channel */
    curchan = (conn->last_unmapped_chan + conn->hop_inc) % 
               BLE_PHY_NUM_DATA_CHANS;

    /* Set the current unmapped channel */
    conn->unmapped_chan = curchan;

    /* Is this a valid channel? */
    bitpos = 1 << (curchan & 0x07);
    if ((conn->chanmap[curchan >> 3] & bitpos) == 0) {

        /* Calculate remap index */
        remap_index = curchan % conn->num_used_chans;

        /* NOTE: possible to build a map but this would use memory. For now,
           we just calculate */
        /* Iterate through channel map to find this channel */
        cntr = 0;
        for (i = 0; i < 5; i++) {
            usable_chans = conn->chanmap[i];
            if (usable_chans != 0) {
                mask = 0x01;
                for (j = 0; j < 8; j++) {
                    if (usable_chans & mask) {
                        if (cntr == remap_index) {
                            return cntr;
                        }
                        ++cntr;
                    }
                    mask <<= 1;
                }
            }
        }
    }

    return curchan;
}

/**
 * Initialize the connection state machine. This is done once per connection 
 * when the HCI command "create connection" is issued to the controller. 
 * 
 * @param connsm 
 */
void
ble_ll_conn_sm_init(struct ble_ll_conn_sm *connsm, struct hci_create_conn *hcc)
{
    /* Hop increment is a random value between 5 and 16. */
    connsm->hop_inc = (rand() % 12) + 5;

    /* Reset event counter */
    connsm->conn_event_cntr = 0;

    /* Set slave latency */
    connsm->conn_slave_latency = hcc->conn_latency;
    connsm->conn_spvn_tmo = hcc->supervision_timeout;

    /* Set own address type and peer address if needed */
    connsm->own_addr_type = hcc->own_addr_type;
    if (hcc->filter_policy == 0) {
        memcpy(&connsm->peer_addr, &hcc->peer_addr, BLE_DEV_ADDR_LEN);
        connsm->peer_addr_type = hcc->peer_addr_type;
    }

    /* XXX: set connection interval */
    /* XXX: what to do with min/max connection event? */
    /* XXX: where does the channel map get set? */

    /* XXX: Calculate random access address */
    connsm->access_addr = ble_ll_conn_calc_access_addr();
}

void
ble_ll_conn_req_make(struct ble_ll_conn_sm *connsm)
{
    uint8_t pdu_type;
    uint8_t *addr;
    uint8_t *dptr;
    struct os_mbuf *m;

    m = ble_ll_scan_get_pdu();
    assert(m != NULL);
    m->om_len = BLE_CONNECT_REQ_LEN + BLE_LL_PDU_HDR_LEN;
    OS_MBUF_PKTHDR(m)->omp_len = m->om_len;

    /* Construct first PDU header byte */
    pdu_type = BLE_ADV_PDU_TYPE_CONNECT_REQ;

    /* Get pointer to our device address */
    if (connsm->own_addr_type == BLE_HCI_ADV_OWN_ADDR_PUBLIC) {
        addr = g_dev_addr;
    } else if (connsm->own_addr_type == BLE_HCI_ADV_OWN_ADDR_RANDOM) {
        pdu_type |= BLE_ADV_PDU_HDR_TXADD_RAND;
        addr = g_random_addr;
    } else {
        /* XXX: unsupported for now  */
        addr = NULL;
        assert(0);
    }

    /* Construct the connect request */
    dptr = m->om_data;
    dptr[0] = pdu_type;     /* XXX: this can change based on advertiser addr */
    dptr[1] = BLE_CONNECT_REQ_LEN;
    memcpy(dptr + BLE_LL_PDU_HDR_LEN, addr, BLE_DEV_ADDR_LEN);
    /* XXX: what about this next address? I cant put this in here yet, right? */
#if 0
    memcpy(dptr + BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN, adv_addr, 
           BLE_DEV_ADDR_LEN);
#endif
    dptr += BLE_LL_PDU_HDR_LEN + (2 * BLE_DEV_ADDR_LEN);

    /* Access address */
}

/**
 * Process the HCI command to create a connection. 
 * 
 * @param cmdbuf 
 * 
 * @return int 
 */
int
ble_ll_conn_create(uint8_t *cmdbuf)
{
    uint32_t spvn_tmo_msecs;
    uint32_t min_spvn_tmo_msecs;
    struct hci_create_conn ccdata;
    struct hci_create_conn *hcc;
    struct ble_ll_conn_sm *connsm;

    /* If already enabled, we return an error */
    connsm = &g_ble_ll_conn_sm;
    if (ble_ll_scan_enabled() || (connsm->conn_state != BLE_LL_CONN_STATE_IDLE)) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* Retrieve command data */
    hcc = &ccdata;
    hcc->scan_itvl = le16toh(cmdbuf);
    hcc->scan_window = le16toh(cmdbuf + 2);

    /* Check interval and window */
    if ((hcc->scan_itvl < BLE_HCI_SCAN_ITVL_MIN) || 
        (hcc->scan_itvl > BLE_HCI_SCAN_ITVL_MAX) ||
        (hcc->scan_window < BLE_HCI_SCAN_WINDOW_MIN) ||
        (hcc->scan_window > BLE_HCI_SCAN_WINDOW_MAX) ||
        (hcc->scan_itvl < hcc->scan_window)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check filter policy */
    hcc->filter_policy = cmdbuf[4];
    if (hcc->filter_policy > BLE_HCI_INITIATOR_FILT_POLICY_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Get peer address type and address only if no whitelist used */
    if (hcc->filter_policy == 0) {
        hcc->peer_addr_type = cmdbuf[5];
        if (hcc->peer_addr_type > BLE_HCI_CONN_PEER_ADDR_MAX) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }
        memcpy(&hcc->peer_addr, cmdbuf + 6, BLE_DEV_ADDR_LEN);
    }

    /* Get own address type (used in connection request) */
    hcc->own_addr_type = cmdbuf[12];
    if (hcc->own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check connection interval */
    hcc->conn_itvl_min = le16toh(cmdbuf + 13);
    hcc->conn_itvl_max = le16toh(cmdbuf + 15);
    hcc->conn_latency = le16toh(cmdbuf + 17);
    if ((hcc->conn_itvl_min > hcc->conn_itvl_max)       ||
        (hcc->conn_itvl_min < BLE_HCI_CONN_ITVL_MIN)    ||
        (hcc->conn_itvl_min > BLE_HCI_CONN_ITVL_MAX)    ||
        (hcc->conn_latency > BLE_HCI_CONN_LATENCY_MAX)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check supervision timeout */
    hcc->supervision_timeout = le16toh(cmdbuf + 19);
    if ((hcc->supervision_timeout < BLE_HCI_CONN_SPVN_TIMEOUT_MIN) ||
        (hcc->supervision_timeout > BLE_HCI_CONN_SPVN_TIMEOUT_MAX))
    {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* 
     * Supervision timeout (in msecs) must be more than:
     *  (1 + connLatency) * connIntervalMax * 1.25 msecs * 2.
     */
    spvn_tmo_msecs = hcc->supervision_timeout * BLE_HCI_CONN_SPVN_TMO_UNITS;
    min_spvn_tmo_msecs = (hcc->conn_itvl_max << 1) + (hcc->conn_itvl_max >> 1);
    min_spvn_tmo_msecs *= (1 + hcc->conn_latency);
    if (spvn_tmo_msecs <= min_spvn_tmo_msecs) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    hcc->min_ce_len = le16toh(cmdbuf + 21);
    hcc->max_ce_len = le16toh(cmdbuf + 23);
    if (hcc->min_ce_len > hcc->max_ce_len) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Set state machine parameters */
    ble_ll_scan_initiator_start(hcc);

    /* XXX: I have not dealt with lots of the state machine parameters! */

    /* Initialize the connection sm */
    ble_ll_conn_sm_init(connsm, hcc);

    /* Create the connection request */
    ble_ll_conn_req_make(connsm);

    /* XXX: gotta start scanning! */

    return BLE_ERR_SUCCESS;
}

int
ble_ll_conn_create_cancel(void)
{
    /* XXX: implement this */
    return BLE_ERR_SUCCESS;
}
