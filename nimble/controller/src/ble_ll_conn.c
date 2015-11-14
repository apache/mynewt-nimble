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
#include "controller/ble_ll_hci.h"
#include "controller/ble_ll_conn.h"
#include "controller/ble_ll_scan.h"
#include "controller/ble_ll_whitelist.h"
#include "controller/ble_phy.h"

/* XXX TODO
 * 1) Implemement connection supervisor timeout.
 * 2) Add set channel map command and implement channel change procedure.
 * 3) Closing the connection: the MD bit.
 * 4) notifying host about connections: how often do we need to do this? On
 * creation, establishment and/or loss?
 * 5) Deal with window widening. Easy...
 * 6) Close connection if window widening gets too big! 4.5.7
 * 7) How does state get set to ESTABLISHED? Do that...
 * 8) In both cases when I start the connection SM I need to set up a
 * schedule item to deal with receiving packets and transmitting them.
 * 9) Implement flow control and all that.
 * 10) Implement getting a data packet. How do we do that?
 * 11) Send connection complete event when connection dies.
 */

/* XXX: Something I need to think about. At what point do I consider the
 * connection created? When I send the connect request or when I send
 * the connection complete event. I just dont want there to be weird race
 * conditions if I get cancel connection create command. I want to make
 * sure that I deal with that properly. I think the connection state will tell
 * me. Anyway, just make sure there are no issues here
 */

/* Channel map size */
#define BLE_LL_CONN_CHMAP_LEN           (5)

/* Configuration parameters */
#define BLE_LL_CONN_CFG_TX_WIN_SIZE     (1)
#define BLE_LL_CONN_CFG_TX_WIN_OFF      (0)
#define BLE_LL_CONN_CFG_MASTER_SCA      (BLE_MASTER_SCA_251_500_PPM << 5)
#define BLE_LL_CONN_CFG_MAX_CONNS       (8)

/* Roles */
#define BLE_LL_CONN_ROLE_NONE           (0)
#define BLE_LL_CONN_ROLE_MASTER         (1)
#define BLE_LL_CONN_ROLE_SLAVE          (2)

/* Connection request */
#define BLE_LL_CONN_REQ_ADVA_OFF        (BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN)

/* Scanning state machine */
struct ble_ll_conn_sm
{
    /* Used to calculate data channel index for connection */
    uint8_t unmapped_chan;
    uint8_t last_unmapped_chan;
    uint8_t num_used_chans;
    uint8_t chanmap[BLE_LL_CONN_CHMAP_LEN];
    uint8_t hop_inc;
    uint8_t conn_state;
    uint8_t conn_role;
    uint8_t master_sca;
    uint8_t conn_tx_win_size;
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint8_t peer_addr[BLE_DEV_ADDR_LEN];
    uint16_t conn_spvn_tmo;
    uint16_t conn_itvl;
    uint16_t conn_slave_latency;
    uint16_t conn_event_cntr;
    uint16_t conn_handle;
    uint16_t conn_tx_win_off;
    uint32_t access_addr;
    uint32_t crcinit;       /* only low 24 bits used */
};

struct ble_ll_conn_sm g_ble_ll_conn_sm;

/* Connection states */
#define BLE_LL_CONN_STATE_IDLE          (0)
#define BLE_LL_CONN_STATE_CREATED       (1)
#define BLE_LL_CONN_STATE_ESTABLISHED   (2)

/* Connection handles */
static uint8_t g_ble_ll_conn_handles[(BLE_LL_CONN_CFG_MAX_CONNS + 7) / 8];
static uint16_t g_ble_ll_conn_handles_out;

struct os_mempool g_ble_ll_conn_pool;
os_membuf_t g_ble_ll_conn_buf[OS_MEMPOOL_SIZE(BLE_LL_CONN_CFG_MAX_CONNS, 
                                              sizeof(struct ble_ll_conn_sm))];

/**
 * Calculates the number of used channels in the channel map 
 * 
 * @param chmap 
 * 
 * @return uint8_t Number of used channels
 */
uint8_t
ble_ll_conn_calc_used_chans(uint8_t *chmap)
{
    int i;
    int j;
    uint8_t mask;
    uint8_t chanbyte;
    uint8_t used_channels;

    used_channels = 0;
    for (i = 0; i < BLE_LL_CONN_CHMAP_LEN; ++i) {
        chanbyte = chmap[i];
        if (chanbyte) {
            if (chanbyte == 0xff) {
                used_channels += 8;
            } else {
                mask = 0x01;
                for (j = 0; j < 8; ++j) {
                    if (chanbyte & mask) {
                        ++used_channels;
                    }
                    mask <<= 1;
                }
            }
        }
    }
    return used_channels;
}


static uint16_t
ble_ll_conn_get_unused_handle(void)
{
    uint8_t bytepos;
    uint8_t bitpos;
    int handle;

    /* Keep track of total number so you can deny it! */
    if (g_ble_ll_conn_handles_out == BLE_LL_CONN_CFG_MAX_CONNS) {
        /* Handles can only be in range 0 to 0x0EFF */
        return 0xFFFF;
    }

    /* Return an unused handle */
    for (handle = 0; handle < BLE_LL_CONN_CFG_MAX_CONNS; ++handle) {
        /* Get byte position and bit position */
        bytepos = handle / 8;
        bitpos = handle & 0x7;

        /* See if handle avaiable */
        if ((g_ble_ll_conn_handles[bytepos] & (1 << bitpos)) == 0) {
            g_ble_ll_conn_handles[bytepos] |= (1 << bitpos);
            ++g_ble_ll_conn_handles_out;
            break;
        }
    }

    /* We better have a free one! If not, something wrong! */
    assert(handle < BLE_LL_CONN_CFG_MAX_CONNS);

    return handle;
}

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
        for (i = 0; i < BLE_LL_CONN_CHMAP_LEN; i++) {
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
 * when the HCI command "create connection" is issued to the controller or 
 * when a slave receives a connect request, 
 * 
 * @param connsm 
 */
static void
ble_ll_conn_sm_init(struct ble_ll_conn_sm *connsm, struct hci_create_conn *hcc)
{
    /* Reset event counter and last unmapped channel; get a handle */
    connsm->last_unmapped_chan = 0;
    connsm->conn_event_cntr = 0;
    connsm->conn_handle = ble_ll_conn_get_unused_handle();

    if (hcc != NULL) {
        /* Must be master */
        connsm->conn_role = BLE_LL_CONN_ROLE_MASTER;
        connsm->conn_tx_win_size = BLE_LL_CONN_CFG_TX_WIN_SIZE;
        connsm->conn_tx_win_off = BLE_LL_CONN_CFG_TX_WIN_OFF;
        connsm->master_sca = BLE_LL_CONN_CFG_MASTER_SCA;

        /* Hop increment is a random value between 5 and 16. */
        connsm->hop_inc = (rand() % 12) + 5;

        /* Set slave latency, supervision timeout and connection interval */
        connsm->conn_slave_latency = hcc->conn_latency;
        connsm->conn_spvn_tmo = hcc->supervision_timeout;
        /* XXX: for now, just make connection interval equal to max */
        connsm->conn_itvl = hcc->conn_itvl_max;

        /* Set own address type and peer address if needed */
        connsm->own_addr_type = hcc->own_addr_type;
        if (hcc->filter_policy == 0) {
            memcpy(&connsm->peer_addr, &hcc->peer_addr, BLE_DEV_ADDR_LEN);
            connsm->peer_addr_type = hcc->peer_addr_type;
        }

        /* XXX: what to do with min/max connection event length? */

        /* XXX: for now, just set the channel map to all 1's. Needs to get
           set to default or initialized or something */
        connsm->num_used_chans = 37;
        memset(connsm->chanmap, 0xff, BLE_LL_CONN_CHMAP_LEN - 1);
        connsm->chanmap[4] = 0x1f;

        /*  Calculate random access address and crc initialization value */
        connsm->access_addr = ble_ll_conn_calc_access_addr();
        connsm->crcinit = rand() & 0xffffff;
    } else {
        connsm->conn_role = BLE_LL_CONN_ROLE_SLAVE;
    }
}

/**
 * Send a connection complete event 
 * 
 * @param status The BLE error code associated with the event
 */
static void
ble_ll_conn_comp_event_send(uint8_t status)
{
    uint8_t *evbuf;
    struct ble_ll_conn_sm *connsm;

    if (ble_ll_hci_is_le_event_enabled(BLE_HCI_LE_SUBEV_CONN_COMPLETE - 1)) {
        evbuf = os_memblock_get(&g_hci_cmd_pool);
        if (evbuf) {
            connsm = &g_ble_ll_conn_sm;
            evbuf[0] = BLE_HCI_EVCODE_LE_META;
            evbuf[1] = BLE_HCI_LE_CONN_COMPLETE_LEN;
            evbuf[2] = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
            evbuf[3] = status;
            if (status == BLE_ERR_SUCCESS) {
                htole16(evbuf + 4, connsm->conn_handle);
                evbuf[6] = connsm->conn_role;
                evbuf[7] = connsm->peer_addr_type;
                memcpy(evbuf + 8, connsm->peer_addr, BLE_DEV_ADDR_LEN);
                htole16(evbuf + 14, connsm->conn_itvl);
                htole16(evbuf + 16, connsm->conn_slave_latency);
                evbuf[18] = connsm->master_sca;
            }

            /* XXX: what to do if we fail to send event? */
            ble_ll_hci_event_send(evbuf);
        }
    }
}

/**
 * Update the connection request PDU with the address type and address of 
 * advertiser we are going to send connect request to. 
 * 
 * @param m 
 * @param adva 
 * @param addr_type 
 */
void
ble_ll_conn_req_pdu_update(struct os_mbuf *m, uint8_t *adva, uint8_t addr_type)
{
    uint8_t pdu_type;
    uint8_t *dptr;

    assert(m != NULL);

    dptr = m->om_data;
    pdu_type = dptr[0];
    if (addr_type) {
        /* Set random address */
        pdu_type |= BLE_ADV_PDU_HDR_RXADD_MASK;
    } else {
        /* Set public device address */
        pdu_type &= ~BLE_ADV_PDU_HDR_RXADD_MASK;
    }
    dptr[0] = pdu_type;
    memcpy(dptr + BLE_LL_CONN_REQ_ADVA_OFF, adva, BLE_DEV_ADDR_LEN); 
}

/**
 * Make a connect request PDU 
 * 
 * @param connsm 
 */
void
ble_ll_conn_req_pdu_make(struct ble_ll_conn_sm *connsm)
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
    dptr[0] = pdu_type;
    dptr[1] = BLE_CONNECT_REQ_LEN;
    memcpy(dptr + BLE_LL_PDU_HDR_LEN, addr, BLE_DEV_ADDR_LEN);

    /* Skip the advertiser's address as we dont know that yet */
    dptr += (BLE_LL_CONN_REQ_ADVA_OFF + BLE_DEV_ADDR_LEN);

    /* Access address */
    htole32(dptr, connsm->access_addr);
    dptr[4] = connsm->crcinit >> 16;
    dptr[5] = connsm->crcinit >> 8;
    dptr[6] = (uint8_t)connsm->crcinit;
    dptr[7] = connsm->conn_tx_win_size;
    htole16(dptr + 8, connsm->conn_tx_win_off);
    htole16(dptr + 10, connsm->conn_itvl);
    htole16(dptr + 12, connsm->conn_slave_latency);
    htole16(dptr + 14, connsm->conn_spvn_tmo);
    memcpy(dptr + 16, &connsm->chanmap, BLE_LL_CONN_CHMAP_LEN);
    dptr[21] = connsm->hop_inc | connsm->master_sca;
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

    /* XXX: what happens if we have too many! Do something about this */

    /* 
     * XXX: right now, we only allow one connection, so if the connection
     * state machine is used we return error. Otherewise, we would only need
     * to check the scanner. If it is enabled, it means we are initiating.
     */
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

    /* Initialize the connection sm */
    ble_ll_conn_sm_init(connsm, hcc);

    /* Create the connection request */
    ble_ll_conn_req_pdu_make(connsm);

    /* Start scanning */
    return ble_ll_scan_initiator_start(hcc);
}

/**
 * Called when HCI command to cancel a create connection command has been 
 * received. 
 *  
 * Context: Link Layer (HCI command parser) 
 * 
 * @return int 
 */
int
ble_ll_conn_create_cancel(void)
{
    int rc;

    /* 
     * If we receive this command and we have not got a connection
     * create command, we have to return disallowed. The spec does not say
     * what happens if the connection has already been established.
     */
    if (ble_ll_scan_enabled()) {
        /* stop scanning */
        ble_ll_scan_sm_stop(ble_ll_scan_sm_get());

        /* XXX: what to do with connection state machine? */

        /* Send connection complete event */
        ble_ll_conn_comp_event_send(BLE_ERR_UNK_CONN_ID);
        rc = BLE_ERR_SUCCESS;
    } else {
        /* If we have established connection or we are idle do not allow */
        rc = BLE_ERR_CMD_DISALLOWED;
    }

    return rc;
}

/* Returns true if the address matches the connection peer address */
int
ble_ll_conn_is_peer_adv(uint8_t addr_type, uint8_t *adva)
{
    int rc;
    struct ble_ll_conn_sm *connsm;

    rc = 0;
    connsm = &g_ble_ll_conn_sm;
    if ((connsm->peer_addr_type == addr_type) &&
        !memcmp(adva, connsm->peer_addr, BLE_DEV_ADDR_LEN)) {
        rc = 1;
    }

    return rc;
}

/**
 * Send a connection requestion to an advertiser 
 * 
 * @param addr_type Address type of advertiser
 * @param adva Address of advertiser
 */
int
ble_ll_conn_request_send(uint8_t addr_type, uint8_t *adva)
{
    int rc;
    struct os_mbuf *m;
    struct ble_ll_conn_sm *connsm;

    m = ble_ll_scan_get_pdu();
    ble_ll_conn_req_pdu_update(m, adva, addr_type);
    rc = ble_phy_tx(m, BLE_PHY_TRANSITION_RX_TX, BLE_PHY_TRANSITION_NONE);
    if (!rc) {
        connsm = &g_ble_ll_conn_sm;
        connsm->peer_addr_type = addr_type;
        memcpy(connsm->peer_addr, adva, BLE_DEV_ADDR_LEN);
        connsm->conn_state = BLE_LL_CONN_STATE_CREATED;
    }

    return rc;
}

/**
 * Process a received PDU while in the initiating state.
 *  
 * Context: Link Layer task. 
 * 
 * @param pdu_type 
 * @param rxbuf 
 */
void
ble_ll_init_rx_pdu_proc(uint8_t pdu_type, uint8_t *rxbuf, uint8_t crcok)
{
    struct ble_ll_scan_sm *scansm;
    struct ble_ll_conn_sm *connsm;

    /* XXX: Do I care what pdu type I receive? Not sure exactly how to
       deal with knowing a connection request was sent. */

    /* 
     * XXX: Not sure I like this but I will deal with it for now. Just
     * check the connection state machine. If state is created, we move
     * to the connected state.
     */
    connsm = &g_ble_ll_conn_sm;
    if (crcok && (connsm->conn_state == BLE_LL_CONN_STATE_CREATED)) {
        /* Stop scanning */
        scansm = ble_ll_scan_sm_get();
        ble_ll_scan_sm_stop(scansm);

        /* We are now in connection state */
        ble_ll_state_set(BLE_LL_STATE_CONNECTION);

        /* Send connection complete event */
        ble_ll_conn_comp_event_send(BLE_ERR_SUCCESS);
    } else {
        /* We need to re-enable the PHY if we are in idle state */
        if (ble_phy_state_get() == BLE_PHY_STATE_IDLE) {
            /* XXX: If this returns error, we will need to attempt to
               re-start scanning! */
            ble_phy_rx();
        }
    }
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
ble_ll_init_rx_pdu_end(struct os_mbuf *rxpdu)
{
    int rc;
    int chk_send_req;
    uint8_t pdu_type;
    uint8_t addr_type;
    uint8_t *adv_addr;
    uint8_t *init_addr;
    uint8_t *rxbuf;
    struct ble_mbuf_hdr *ble_hdr;

    /* Only interested in ADV IND or ADV DIRECT IND */
    rxbuf = rxpdu->om_data;
    pdu_type = rxbuf[0] & BLE_ADV_PDU_HDR_TYPE_MASK;

    switch (pdu_type) {
    case BLE_ADV_PDU_TYPE_ADV_IND:
        chk_send_req = 1;
        break;
    case BLE_ADV_PDU_TYPE_ADV_DIRECT_IND:
        init_addr = rxbuf + BLE_LL_PDU_HDR_LEN + BLE_DEV_ADDR_LEN;
        addr_type = rxbuf[0] & BLE_ADV_PDU_HDR_RXADD_MASK;
        if (ble_ll_is_our_devaddr(init_addr, addr_type)) {
            chk_send_req = 1;
        } else {
            chk_send_req = 0;
        }
        break;
    default:
        chk_send_req = 0;
        break;
    }

    /* Should we send a connect request? */
    rc = -1;
    if (chk_send_req) {
        /* Check filter policy */
        adv_addr = rxbuf + BLE_LL_PDU_HDR_LEN;
        if (rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK) {
            addr_type = BLE_ADDR_TYPE_RANDOM;
        } else {
            addr_type = BLE_ADDR_TYPE_PUBLIC;
        }

        /* Check filter policy */
        if (ble_ll_scan_whitelist_enabled()) {
            /* Check if device is on whitelist. If not, leave */
            if (!ble_ll_whitelist_match(adv_addr, addr_type)) {
                return -1;
            }

            /* Set BLE mbuf header flags */
            ble_hdr = BLE_MBUF_HDR_PTR(rxpdu);
            ble_hdr->flags |= BLE_MBUF_HDR_F_DEVMATCH;
        } else {
            /* XXX: Resolvable? Deal with those */
            /* XXX: HW device matching? If so, implement */
            /* Must match the connection address */
            if (!ble_ll_conn_is_peer_adv(addr_type, adv_addr)) {
                return -1;
            }
        }

        /* Setup to transmit the connect request */
        rc = ble_ll_conn_request_send(addr_type, adv_addr);
    }

    return rc;
}

/**
 * Called on pdu start (by link layer) when in initiating state. 
 * 
 * @param pdu_type 
 * 
 * @return int 
 */
int
ble_ll_init_rx_pdu_start(uint8_t pdu_type)
{
    int rc;

    /* We might respond to adv ind or adv direct ind only */
    rc = 0;
    if ((pdu_type == BLE_ADV_PDU_TYPE_ADV_IND) ||
        (pdu_type == BLE_ADV_PDU_TYPE_ADV_DIRECT_IND)) {
        rc = 1;
    }

    return rc;
}

/**
 * Called when a device has received a connect request while advertising and 
 * the connect request has passed the advertising filter policy and is for 
 * us. This will start a connection in the slave role. 
 * 
 * @param rxbuf Pointer to received PDU
 */
void
ble_ll_conn_slave_start(uint8_t *rxbuf)
{
    uint32_t crcinit;
    uint8_t *dptr;
    struct ble_ll_conn_sm *connsm;

    /* XXX: Should we error check the parameters? */

    /* Set the pointer at the start of the connection data */
    dptr = rxbuf + BLE_LL_CONN_REQ_ADVA_OFF + BLE_DEV_ADDR_LEN;

    connsm = &g_ble_ll_conn_sm;
    connsm->access_addr = le32toh(dptr);
    crcinit = dptr[4];
    crcinit <<= 16;
    crcinit |= dptr[5];
    crcinit <<= 8;
    crcinit |= dptr[6];
    connsm->crcinit = crcinit;
    connsm->conn_tx_win_size = dptr[7];
    connsm->conn_tx_win_off = le16toh(dptr + 8);
    connsm->conn_itvl = le16toh(dptr + 10);
    connsm->conn_slave_latency = le16toh(dptr + 12);
    connsm->conn_spvn_tmo = le16toh(dptr + 14);
    memcpy(&connsm->chanmap, dptr + 16, BLE_LL_CONN_CHMAP_LEN);
    connsm->hop_inc = dptr[21] & 0x1F;
    connsm->master_sca = dptr[21] >> 5;

    /* Set the address of device that we are connecting with */
    memcpy(&connsm->peer_addr, rxbuf + BLE_LL_PDU_HDR_LEN, BLE_DEV_ADDR_LEN);
    if (rxbuf[0] & BLE_ADV_PDU_HDR_TXADD_MASK) {
        connsm->peer_addr_type = BLE_ADDR_TYPE_RANDOM;
    } else {
        connsm->peer_addr_type = BLE_ADDR_TYPE_PUBLIC;
    }

    connsm->num_used_chans = ble_ll_conn_calc_used_chans(connsm->chanmap);

    /* Need to initialize the connection state machine */
    ble_ll_conn_sm_init(connsm, NULL);

    /* Set connection state to created */
    connsm->conn_state = BLE_LL_CONN_STATE_CREATED;

    /* Set state to connection */
    ble_ll_state_set(BLE_LL_STATE_CONNECTION);

    /* Send connection complete event */
    ble_ll_conn_comp_event_send(BLE_ERR_SUCCESS);
}

/* Initializet the connection module */
void
ble_ll_conn_init(void)
{
    int rc;

    /* Create connection memory pool */
    rc = os_mempool_init(&g_ble_ll_conn_pool, BLE_LL_CONN_CFG_MAX_CONNS, 
                         sizeof(struct ble_ll_conn_sm), &g_ble_ll_conn_buf, 
                         "LLConnPool");

    assert(rc == 0);
}
