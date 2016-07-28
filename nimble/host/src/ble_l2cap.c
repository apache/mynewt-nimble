/**
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

#include <string.h>
#include <errno.h>
#include "os/os.h"
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "host/host_hci.h"
#include "ble_hs_priv.h"

_Static_assert(sizeof (struct ble_l2cap_hdr) == BLE_L2CAP_HDR_SZ,
               "struct ble_l2cap_hdr must be 4 bytes");

struct os_mempool ble_l2cap_chan_pool;

static void *ble_l2cap_chan_mem;

STATS_SECT_DECL(ble_l2cap_stats) ble_l2cap_stats;
STATS_NAME_START(ble_l2cap_stats)
    STATS_NAME(ble_l2cap_stats, chan_create)
    STATS_NAME(ble_l2cap_stats, chan_delete)
    STATS_NAME(ble_l2cap_stats, update_init)
    STATS_NAME(ble_l2cap_stats, update_rx)
    STATS_NAME(ble_l2cap_stats, update_fail)
    STATS_NAME(ble_l2cap_stats, proc_timeout)
    STATS_NAME(ble_l2cap_stats, sig_tx)
    STATS_NAME(ble_l2cap_stats, sig_rx)
    STATS_NAME(ble_l2cap_stats, sm_tx)
    STATS_NAME(ble_l2cap_stats, sm_rx)
STATS_NAME_END(ble_l2cap_stats)

struct ble_l2cap_chan *
ble_l2cap_chan_alloc(void)
{
    struct ble_l2cap_chan *chan;

    chan = os_memblock_get(&ble_l2cap_chan_pool);
    if (chan == NULL) {
        return NULL;
    }

    memset(chan, 0, sizeof *chan);

    STATS_INC(ble_l2cap_stats, chan_create);

    return chan;
}

void
ble_l2cap_chan_free(struct ble_l2cap_chan *chan)
{
    int rc;

    if (chan == NULL) {
        return;
    }

    rc = os_memblock_put(&ble_l2cap_chan_pool, chan);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

    STATS_INC(ble_l2cap_stats, chan_delete);
}

uint16_t
ble_l2cap_chan_mtu(const struct ble_l2cap_chan *chan)
{
    uint16_t mtu;

    /* If either side has not exchanged MTU size, use the default.  Otherwise,
     * use the lesser of the two exchanged values.
     */
    if (!(chan->blc_flags & BLE_L2CAP_CHAN_F_TXED_MTU) ||
        chan->blc_peer_mtu == 0) {

        mtu = chan->blc_default_mtu;
    } else {
        mtu = min(chan->blc_my_mtu, chan->blc_peer_mtu);
    }

    BLE_HS_DBG_ASSERT(mtu >= chan->blc_default_mtu);

    return mtu;
}

int
ble_l2cap_parse_hdr(struct os_mbuf *om, int off,
                    struct ble_l2cap_hdr *l2cap_hdr)
{
    int rc;

    rc = os_mbuf_copydata(om, off, sizeof *l2cap_hdr, l2cap_hdr);
    if (rc != 0) {
        return BLE_HS_EMSGSIZE;
    }

    l2cap_hdr->blh_len = le16toh(&l2cap_hdr->blh_len);
    l2cap_hdr->blh_cid = le16toh(&l2cap_hdr->blh_cid);

    return 0;
}

struct os_mbuf *
ble_l2cap_prepend_hdr(struct os_mbuf *om, uint16_t cid, uint16_t len)
{
    struct ble_l2cap_hdr hdr;
    struct os_mbuf *om2;

    htole16(&hdr.blh_len, len);
    htole16(&hdr.blh_cid, cid);

    om2 = os_mbuf_prepend(om, sizeof hdr);
    if (om2 == NULL) {
        return NULL;
    }
    BLE_HS_DBG_ASSERT(om2 == om);

    memcpy(om->om_data, &hdr, sizeof hdr);

    return om;
}

static void
ble_l2cap_forget_rx(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan)
{
    conn->bhc_rx_chan = NULL;
    chan->blc_rx_buf = NULL;
    chan->blc_rx_len = 0;
}

static void
ble_l2cap_discard_rx(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan)
{
    os_mbuf_free_chain(chan->blc_rx_buf);
    ble_l2cap_forget_rx(conn, chan);
}

static int
ble_l2cap_rx_payload(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                     struct os_mbuf *om,
                     ble_l2cap_rx_fn **out_rx_cb, struct os_mbuf **out_rx_buf)
{
    int len_diff;
    int rc;

    if (chan->blc_rx_buf == NULL) {
        chan->blc_rx_buf = om;
    } else {
        os_mbuf_concat(chan->blc_rx_buf, om);
    }

    /* Determine if packet is fully reassembled. */
    len_diff = OS_MBUF_PKTLEN(chan->blc_rx_buf) - chan->blc_rx_len;
    if (len_diff > 0) {
        /* More data than expected; data corruption. */
        ble_l2cap_discard_rx(conn, chan);
        rc = BLE_HS_EBADDATA;
    } else if (len_diff == 0) {
        /* All fragments received. */
        *out_rx_cb = chan->blc_rx_fn;
        *out_rx_buf = chan->blc_rx_buf;
        ble_l2cap_forget_rx(conn, chan);
        rc = 0;
    } else {
        /* More fragments remain. */
        rc = BLE_HS_EAGAIN;
    }

    return rc;
}

int
ble_l2cap_rx(struct ble_hs_conn *conn,
             struct hci_data_hdr *hci_hdr,
             struct os_mbuf *om,
             ble_l2cap_rx_fn **out_rx_cb,
             struct os_mbuf **out_rx_buf)
{
    struct ble_l2cap_chan *chan;
    struct ble_l2cap_hdr l2cap_hdr;
    uint8_t pb;
    int rc;

    pb = BLE_HCI_DATA_PB(hci_hdr->hdh_handle_pb_bc);
    switch (pb) {
    case BLE_HCI_PB_FIRST_FLUSH:
        /* First fragment. */
        rc = ble_l2cap_parse_hdr(om, 0, &l2cap_hdr);
        if (rc != 0) {
            goto err;
        }

        /* Strip L2CAP header from the front of the mbuf. */
        os_mbuf_adj(om, BLE_L2CAP_HDR_SZ);

        chan = ble_hs_conn_chan_find(conn, l2cap_hdr.blh_cid);
        if (chan == NULL) {
            rc = BLE_HS_ENOENT;

            /* Unsupported channel. If the target CID is the black hole
             * channel, quietly drop the packet.  Otherwise, send an invalid
             * CID response.
             */
            if (l2cap_hdr.blh_cid != BLE_L2CAP_CID_BLACK_HOLE) {
                BLE_HS_LOG(DEBUG, "rx on unknown L2CAP channel: %d\n",
                           l2cap_hdr.blh_cid);

                chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_SIG);
                if (chan != NULL) {
                    ble_l2cap_sig_reject_invalid_cid_tx(conn, chan, 0, 0,
                                                        l2cap_hdr.blh_cid);
                }
            }
            goto err;
        }

        if (chan->blc_rx_buf != NULL) {
            /* Previous data packet never completed.  Discard old packet. */
            ble_l2cap_discard_rx(conn, chan);
        }

        /* Remember channel and length of L2CAP data for reassembly. */
        conn->bhc_rx_chan = chan;
        chan->blc_rx_len = l2cap_hdr.blh_len;
        break;

    case BLE_HCI_PB_MIDDLE:
        chan = conn->bhc_rx_chan;
        if (chan == NULL || chan->blc_rx_buf == NULL) {
            /* Middle fragment without the start.  Discard new packet. */
            rc = BLE_HS_EBADDATA;
            goto err;
        }
        break;

    default:
        rc = BLE_HS_EBADDATA;
        goto err;
    }

    rc = ble_l2cap_rx_payload(conn, chan, om, out_rx_cb, out_rx_buf);
    om = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(om);
    return rc;
}

/**
 * Transmits the L2CAP payload contained in the specified mbuf.  The supplied
 * mbuf is consumed, regardless of the outcome of the function call.
 * 
 * @param chan                  The L2CAP channel to transmit over.
 * @param txom                  The data to transmit.
 *
 * @return                      0 on success; nonzero on error.
 */
int
ble_l2cap_tx(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
             struct os_mbuf *txom)
{
    int rc;

    txom = ble_l2cap_prepend_hdr(txom, chan->blc_cid, OS_MBUF_PKTLEN(txom));
    if (txom == NULL) {
        return BLE_HS_ENOMEM;
    }

    rc = host_hci_data_tx(conn, txom);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
ble_l2cap_free_mem(void)
{
    free(ble_l2cap_chan_mem);
    ble_l2cap_chan_mem = NULL;
}

int
ble_l2cap_init(void)
{
    int rc;

    ble_l2cap_free_mem();

    ble_l2cap_chan_mem = malloc(
        OS_MEMPOOL_BYTES(ble_hs_cfg.max_l2cap_chans,
                         sizeof (struct ble_l2cap_chan)));
    if (ble_l2cap_chan_mem == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = os_mempool_init(&ble_l2cap_chan_pool, ble_hs_cfg.max_l2cap_chans,
                         sizeof (struct ble_l2cap_chan),
                         ble_l2cap_chan_mem, "ble_l2cap_chan_pool");
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    rc = ble_l2cap_sig_init();
    if (rc != 0) {
        goto err;
    }

    rc = ble_sm_init();
    if (rc != 0) {
        goto err;
    }

    rc = stats_init_and_reg(
        STATS_HDR(ble_l2cap_stats), STATS_SIZE_INIT_PARMS(ble_l2cap_stats,
        STATS_SIZE_32), STATS_NAME_INIT_PARMS(ble_l2cap_stats), "ble_l2cap");
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    return 0;

err:
    ble_l2cap_free_mem();
    return rc;
}
