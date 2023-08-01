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

/* l2cap.c - Bluetooth L2CAP Tester */

/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "syscfg/syscfg.h"

#if MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM)

#include "console/console.h"
#include "host/ble_gap.h"
#include "host/ble_l2cap.h"

#include "../../../nimble/host/src/ble_l2cap_priv.h"

#include "btp/btp.h"

#define CONTROLLER_INDEX             0
#define CHANNELS                     MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM)
#define TESTER_COC_MTU               MYNEWT_VAL(BTTESTER_L2CAP_COC_MTU)
#define TESTER_COC_BUF_COUNT         (3 * MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM))

static os_membuf_t tester_sdu_coc_mem[
    OS_MEMPOOL_SIZE(TESTER_COC_BUF_COUNT, TESTER_COC_MTU)
];

struct os_mbuf_pool sdu_os_mbuf_pool;
static struct os_mempool sdu_coc_mbuf_mempool;
static bool hold_credit = false;

static struct channel {
    uint8_t chan_id; /* Internal number that identifies L2CAP channel. */
    uint8_t state;
    struct ble_l2cap_chan *chan;
} channels[CHANNELS];

static uint8_t
    recv_cb_buf[TESTER_COC_MTU + sizeof(struct btp_l2cap_data_received_ev)];

static struct channel *
get_free_channel(void)
{
    uint8_t i;
    struct channel *chan;

    for (i = 0; i < CHANNELS; i++) {
        if (channels[i].state) {
            continue;
        }

        chan = &channels[i];
        chan->chan_id = i;
        return chan;
    }

    return NULL;
}

struct channel *
find_channel(struct ble_l2cap_chan *chan)
{
    int i;

    for (i = 0; i < CHANNELS; ++i) {
        if (channels[i].chan == chan) {
            return &channels[i];
        }
    }

    return NULL;
}

struct channel *
get_channel(uint8_t chan_id)
{
    if (chan_id >= CHANNELS) {
        return NULL;
    }

    return &channels[chan_id];
}

static void
tester_l2cap_coc_recv(struct ble_l2cap_chan *chan, struct os_mbuf *sdu)
{
    SYS_LOG_DBG("LE CoC SDU received, chan: 0x%08lx, data len %d",
                (uint32_t) chan, OS_MBUF_PKTLEN(sdu));

    os_mbuf_free_chain(sdu);
    if (!hold_credit) {
        sdu = os_mbuf_get_pkthdr(&sdu_os_mbuf_pool, 0);
        assert(sdu != NULL);

        ble_l2cap_recv_ready(chan, sdu);
    }
}

static void
recv_cb(uint16_t conn_handle, struct ble_l2cap_chan *chan,
        struct os_mbuf *buf, void *arg)
{
    struct btp_l2cap_data_received_ev *ev = (void *) recv_cb_buf;
    struct channel *channel = find_channel(chan);
    assert(channel != NULL);

    ev->chan_id = channel->chan_id;
    ev->data_length = OS_MBUF_PKTLEN(buf);

    if (ev->data_length > TESTER_COC_MTU) {
        SYS_LOG_ERR("Too large sdu received, truncating data");
        ev->data_length = TESTER_COC_MTU;
    }
    os_mbuf_copydata(buf, 0, ev->data_length, ev->data);

    tester_event(BTP_SERVICE_ID_L2CAP, BTP_L2CAP_EV_DATA_RECEIVED,
                 recv_cb_buf, sizeof(*ev) + ev->data_length);

    tester_l2cap_coc_recv(chan, buf);
}

static void
reconfigured_ev(uint16_t conn_handle, struct ble_l2cap_chan *chan,
                struct ble_l2cap_chan_info *chan_info,
                int status)
{
    struct btp_l2cap_reconfigured_ev ev;
    struct channel *channel;

    if (status != 0) {
        return;
    }

    channel = find_channel(chan);
    assert(channel != NULL);

    ev.chan_id = channel->chan_id;
    ev.peer_mtu = chan_info->peer_coc_mtu;
    ev.peer_mps = chan_info->peer_l2cap_mtu;
    ev.our_mtu = chan_info->our_coc_mtu;
    ev.our_mps = chan_info->our_l2cap_mtu;

    tester_event(BTP_SERVICE_ID_L2CAP, BTP_L2CAP_EV_RECONFIGURED,
                 (uint8_t *) &ev, sizeof(ev));
}

static void
connected_cb(uint16_t conn_handle, struct ble_l2cap_chan *chan,
             struct ble_l2cap_chan_info *chan_info, void *arg)
{
    struct btp_l2cap_connected_ev ev;
    struct ble_gap_conn_desc desc;
    struct channel *channel = find_channel(chan);

    if (channel == NULL) {
        channel = get_free_channel();
    }

    ev.chan_id = channel->chan_id;
    ev.psm = chan_info->psm;
    ev.peer_mtu = chan_info->peer_coc_mtu;
    ev.peer_mps = chan_info->peer_l2cap_mtu;
    ev.our_mtu = chan_info->our_coc_mtu;
    ev.our_mps = chan_info->our_l2cap_mtu;
    channel->state = 1;
    channel->chan = chan;

    if (!ble_gap_conn_find(conn_handle, &desc)) {
        memcpy(&ev.address, &desc.peer_ota_addr, sizeof(ev.address));
    }

    tester_event(BTP_SERVICE_ID_L2CAP, BTP_L2CAP_EV_CONNECTED,
                 (uint8_t *) &ev, sizeof(ev));
}

static void
disconnected_cb(uint16_t conn_handle, struct ble_l2cap_chan *chan,
                struct ble_l2cap_chan_info *chan_info, void *arg)
{
    struct btp_l2cap_disconnected_ev ev;
    struct ble_gap_conn_desc desc;
    struct channel *channel;

    memset(&ev, 0, sizeof(struct btp_l2cap_disconnected_ev));

    channel = find_channel(chan);
    assert(channel != NULL);

    channel->state = 0;
    channel->chan = chan;
    ev.chan_id = channel->chan_id;
    ev.psm = chan_info->psm;

    if (!ble_gap_conn_find(conn_handle, &desc)) {
        memcpy(&ev.address, &desc.peer_ota_addr, sizeof(ev.address));
    }

    tester_event(BTP_SERVICE_ID_L2CAP, BTP_L2CAP_EV_DISCONNECTED,
                 (uint8_t *) &ev, sizeof(ev));
}

static int
accept_cb(uint16_t conn_handle, uint16_t peer_mtu,
          struct ble_l2cap_chan *chan)
{
    struct os_mbuf *sdu_rx;

    SYS_LOG_DBG("LE CoC accepting, chan: 0x%08lx, peer_mtu %d",
                (uint32_t) chan, peer_mtu);

    sdu_rx = os_mbuf_get_pkthdr(&sdu_os_mbuf_pool, 0);
    if (!sdu_rx) {
        return BLE_HS_ENOMEM;
    }

    ble_l2cap_recv_ready(chan, sdu_rx);

    return 0;
}

static int
tester_l2cap_event(struct ble_l2cap_event *event, void *arg)
{
    struct ble_l2cap_chan_info chan_info;
    struct ble_gap_conn_desc conn;

    switch (event->type) {
    case BLE_L2CAP_EVENT_COC_CONNECTED:
        if (ble_l2cap_get_chan_info(event->connect.chan, &chan_info)) {
            assert(0);
        }

        if (event->connect.status) {
            console_printf("LE COC error: %d\n", event->connect.status);
            return 0;
        }

        console_printf("LE COC connected, conn: %d, chan: 0x%08lx, "
                       "psm: 0x%02x, scid: 0x%04x, dcid: 0x%04x, "
                       "our_mps: %d, our_mtu: %d, peer_mps: %d, "
                       "peer_mtu: %d\n", event->connect.conn_handle,
                       (uint32_t) event->connect.chan, chan_info.psm,
                       chan_info.scid, chan_info.dcid,
                       chan_info.our_l2cap_mtu, chan_info.our_coc_mtu,
                       chan_info.peer_l2cap_mtu, chan_info.peer_coc_mtu);

        connected_cb(event->connect.conn_handle,
                     event->connect.chan, &chan_info, arg);

        return 0;
    case BLE_L2CAP_EVENT_COC_DISCONNECTED:
        if (ble_l2cap_get_chan_info(event->disconnect.chan,
                                    &chan_info)) {
            assert(0);
        }
        console_printf("LE CoC disconnected, chan: 0x%08lx\n",
                       (uint32_t) event->disconnect.chan);

        disconnected_cb(event->disconnect.conn_handle,
                        event->disconnect.chan, &chan_info, arg);
        return 0;
    case BLE_L2CAP_EVENT_COC_ACCEPT:
        ble_l2cap_get_chan_info(event->accept.chan,
                                &chan_info);
        if (chan_info.psm == 0x00F2) {
            /* TSPX_psm_authentication_required */
            ble_gap_conn_find(event->accept.conn_handle, &conn);
            if (!conn.sec_state.authenticated) {
                return BLE_HS_EAUTHEN;
            }
        } else if (chan_info.psm == 0x00F3) {
            /* TSPX_psm_authorization_required */
            ble_gap_conn_find(event->accept.conn_handle, &conn);
            if (!conn.sec_state.encrypted) {
                return BLE_HS_EAUTHOR;
            }
            return BLE_HS_EAUTHOR;
        } else if (chan_info.psm == 0x00F4) {
            /* TSPX_psm_encryption_key_size_required */
            ble_gap_conn_find(event->accept.conn_handle, &conn);
            if (conn.sec_state.key_size < 16) {
                return BLE_HS_EENCRYPT_KEY_SZ;
            }
        } else if (chan_info.psm == 0x00F5) {
            /* TSPX_psm_encryption_required */
            ble_gap_conn_find(event->accept.conn_handle, &conn);
            if (conn.sec_state.key_size == 0) {
                return BLE_HS_EENCRYPT;
            }
        }

        console_printf(
            "LE CoC accept, chan: 0x%08lx, handle: %u, sdu_size: %u\n",
            (uint32_t) event->accept.chan,
            event->accept.conn_handle,
            event->accept.peer_sdu_size);

        return accept_cb(event->accept.conn_handle,
                         event->accept.peer_sdu_size,
                         event->accept.chan);

    case BLE_L2CAP_EVENT_COC_DATA_RECEIVED:
        console_printf(
            "LE CoC data received, chan: 0x%08lx, handle: %u, sdu_len: %u\n",
            (uint32_t) event->receive.chan,
            event->receive.conn_handle,
            OS_MBUF_PKTLEN(event->receive.sdu_rx));

        recv_cb(event->receive.conn_handle, event->receive.chan,
                event->receive.sdu_rx, arg);
        return 0;
    case BLE_L2CAP_EVENT_COC_TX_UNSTALLED:
        console_printf(
            "LE CoC tx unstalled, chan: 0x%08lx, handle: %u, status: %d\n",
            (uint32_t) event->tx_unstalled.chan,
            event->tx_unstalled.conn_handle,
            event->tx_unstalled.status);
        return 0;
    case BLE_L2CAP_EVENT_COC_RECONFIG_COMPLETED:
        if (ble_l2cap_get_chan_info(event->reconfigured.chan,
                                    &chan_info)) {
            assert(0);
        }
        console_printf("LE CoC reconfigure completed status 0x%02x, "
                       "chan: 0x%08lx\n", event->reconfigured.status,
                       (uint32_t) event->reconfigured.chan);

        if (event->reconfigured.status == 0) {
            console_printf("\t our_mps: %d our_mtu %d\n",
                           chan_info.our_l2cap_mtu, chan_info.our_coc_mtu);
        }

        reconfigured_ev(event->reconfigured.conn_handle,
                        event->reconfigured.chan,
                        &chan_info,
                        event->reconfigured.status);
        return 0;
    case BLE_L2CAP_EVENT_COC_PEER_RECONFIGURED:
        if (ble_l2cap_get_chan_info(event->reconfigured.chan,
                                    &chan_info)) {
            assert(0);
        }
        console_printf("LE CoC peer reconfigured status 0x%02x, "
                       "chan: 0x%08lx\n", event->reconfigured.status,
                       (uint32_t) event->reconfigured.chan);

        if (event->reconfigured.status == 0) {
            console_printf("\t peer_mps: %d peer_mtu %d\n",
                           chan_info.peer_l2cap_mtu,
                           chan_info.peer_coc_mtu);
        }

        reconfigured_ev(event->reconfigured.conn_handle,
                        event->reconfigured.chan,
                        &chan_info,
                        event->reconfigured.status);
        return 0;
    default:
        return 0;
    }
}

static uint8_t
connect(const void *cmd, uint16_t cmd_len,
                       void *rsp, uint16_t *rsp_len)
{
    const struct btp_l2cap_connect_cmd *cp = cmd;
    struct btp_l2cap_connect_rp *rp = rsp;
    struct ble_gap_conn_desc desc;
    struct channel *chan;
    struct os_mbuf *sdu_rx[cp->num];
    ble_addr_t *addr = (void *)&cp->address;
    uint16_t mtu = le16toh(cp->mtu);
    uint16_t psm = le16toh(cp->psm);
    int rc;
    int i, j;
    uint8_t status = BTP_STATUS_SUCCESS;
    bool ecfc = cp->options & BTP_L2CAP_CONNECT_OPT_ECFC;
    hold_credit = cp->options & BTP_L2CAP_CONNECT_OPT_HOLD_CREDIT;

    SYS_LOG_DBG("connect: type: %d addr: %s",
                addr->type,
                string_from_bytes(addr->val, 6));

    rc = ble_gap_conn_find_by_addr(addr, &desc);
    if (cp->num == 0 || cp->num > CHANNELS ||
        mtu > TESTER_COC_MTU || mtu == 0) {
        return BTP_STATUS_FAILED;
    }

    if (rc) {
        SYS_LOG_ERR("GAP conn find failed");
        return BTP_STATUS_FAILED;
    }

    for (i = 0; i < cp->num; i++) {
        chan = get_free_channel();
        if (!chan) {
            SYS_LOG_ERR("No free channels");
            status = BTP_STATUS_FAILED;
            goto done;
        }
        /* temporarily mark channel as used to select next one */
        chan->state = 1;

        rp->chan_ids[i] = chan->chan_id;

        sdu_rx[i] = os_mbuf_get_pkthdr(&sdu_os_mbuf_pool, 0);
        if (sdu_rx[i] == NULL) {
            SYS_LOG_ERR("Failed to alloc buf");
            status = BTP_STATUS_FAILED;
            goto done;
        }
    }

    if (cp->num == 1 && !ecfc) {
        rc = ble_l2cap_connect(desc.conn_handle, psm,
                               mtu, sdu_rx[0],
                               tester_l2cap_event, NULL);
    } else if (ecfc) {
        rc = ble_l2cap_enhanced_connect(desc.conn_handle,
                                        psm, mtu,
                                        cp->num, sdu_rx,
                                        tester_l2cap_event, NULL);
    } else {
        SYS_LOG_ERR("Invalid 'num' parameter value");
        status = BTP_STATUS_FAILED;
        goto done;
    }

    if (rc) {
        SYS_LOG_ERR("L2CAP connect failed\n");
        status = BTP_STATUS_FAILED;
        goto done;
    }

    rp->num = cp->num;

    *rsp_len = sizeof(*rp) + (rp->num * sizeof(rp->chan_ids[0]));
done:
    /* mark selected channels as unused again */
    for (i = 0; i < cp->num; i++) {
        for (j = 0; j < CHANNELS; j++) {
            if (rp->chan_ids[i] == channels[j].chan_id) {
                channels[j].state = 0;
            }
        }
    }

    return status;
}

static uint8_t
disconnect(const void *cmd, uint16_t cmd_len,
           void *rsp, uint16_t *rsp_len)
{
    const struct btp_l2cap_disconnect_cmd *cp = cmd;
    struct channel *chan;
    int err;

    SYS_LOG_DBG("");

    if (cp->chan_id >= CHANNELS) {
        return BTP_STATUS_FAILED;
    }

    chan = get_channel(cp->chan_id);
    assert(chan != NULL);

    err = ble_l2cap_disconnect(chan->chan);
    if (err) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
send_data(const void *cmd, uint16_t cmd_len,
          void *rsp, uint16_t *rsp_len)
{
    const struct btp_l2cap_send_data_cmd *cp = cmd;
    struct channel *chan;
    struct os_mbuf *sdu_tx = NULL;
    int rc;
    uint16_t data_len;

    SYS_LOG_DBG("cmd->chan_id=%d", cp->chan_id);

    if (cmd_len < sizeof(*cp) ||
        cmd_len != sizeof(*cp) + le16toh(cp->data_len)) {
        return BTP_STATUS_FAILED;
    }

    if (cp->chan_id >= CHANNELS) {
        return BTP_STATUS_FAILED;
    }

    chan = get_channel(cp->chan_id);
    data_len = le16toh(cp->data_len);

    if (!chan) {
        SYS_LOG_ERR("Invalid channel\n");
        return BTP_STATUS_FAILED;
    }

    /* FIXME: For now, fail if data length exceeds buffer length */
    if (data_len > TESTER_COC_MTU) {
        SYS_LOG_ERR("Data length exceeds buffer length");
        return BTP_STATUS_FAILED;
    }

    sdu_tx = os_mbuf_get_pkthdr(&sdu_os_mbuf_pool, 0);
    if (sdu_tx == NULL) {
        SYS_LOG_ERR("No memory in the test sdu pool\n");
        rc = BLE_HS_ENOMEM;
        goto fail;
    }

    os_mbuf_append(sdu_tx, cp->data, data_len);

    /* ble_l2cap_send takes ownership of the sdu */
    rc = ble_l2cap_send(chan->chan, sdu_tx);
    if (rc == 0 || rc == BLE_HS_ESTALLED) {
        return BTP_STATUS_SUCCESS;
    }

fail:
    SYS_LOG_ERR("Unable to send data: %d", rc);
    os_mbuf_free_chain(sdu_tx);

    return BTP_STATUS_FAILED;
}

static uint8_t
listen(const void *cmd, uint16_t cmd_len,
       void *rsp, uint16_t *rsp_len)
{
    const struct btp_l2cap_listen_cmd *cp = cmd;
    uint16_t mtu = htole16(cp->mtu);
    uint16_t psm = htole16(cp->psm);
    int rc;

    SYS_LOG_DBG("");

    if (psm == 0) {
        return BTP_STATUS_FAILED;
    }

    if (mtu == 0 || mtu > TESTER_COC_MTU) {
        mtu = TESTER_COC_MTU;
    }

    /* We do not support BR/EDR transport */
    if (cp->transport == 0) {
        return BTP_STATUS_FAILED;
    }

    rc = ble_l2cap_create_server(psm, mtu, tester_l2cap_event, NULL);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
credits(const void *cmd, uint16_t cmd_len,
        void *rsp, uint16_t *rsp_len)
{
    const struct btp_l2cap_credits_cmd *cp = cmd;
    struct channel *chan;
    struct os_mbuf *sdu;
    int rc;

    if (cp->chan_id >= CHANNELS) {
        return BTP_STATUS_FAILED;
    }

    chan = get_channel(cp->chan_id);
    if (chan == NULL) {
        return BTP_STATUS_FAILED;
    }

    sdu = os_mbuf_get_pkthdr(&sdu_os_mbuf_pool, 0);
    if (sdu == NULL) {
        os_mbuf_free_chain(sdu);
        return BTP_STATUS_FAILED;
    }

    rc = ble_l2cap_recv_ready(chan->chan, sdu);
    if (rc != 0) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
reconfigure(const void *cmd, uint16_t cmd_len,
            void *rsp, uint16_t *rsp_len)
{
    const struct btp_l2cap_reconfigure_cmd *cp = cmd;
    uint16_t mtu = le16toh(cp->mtu);
    struct ble_gap_conn_desc desc;
    ble_addr_t *addr = (ble_addr_t *)&cp->address;
    struct ble_l2cap_chan *chans[cp->num];
    struct channel *channel;
    int rc;
    int i;

    SYS_LOG_DBG("");

    if (mtu == 0 || mtu > TESTER_COC_MTU) {
        mtu = TESTER_COC_MTU;
    }

    rc = ble_gap_conn_find_by_addr(addr, &desc);
    if (rc) {
        SYS_LOG_ERR("GAP conn find failed");
        return BTP_STATUS_FAILED;
    }

    if (cmd_len < sizeof(*cp) ||
        cmd_len != sizeof(*cp) + cp->num) {
        return BTP_STATUS_FAILED;
    }

    if (cp->num > CHANNELS) {
        return BTP_STATUS_FAILED;
    }

    mtu = le16toh(cp->mtu);
    if (mtu > TESTER_COC_MTU) {
        return BTP_STATUS_FAILED;
    }

    for (i = 0; i < cp->num; ++i) {
        channel = get_channel(cp->idxs[i]);
        if (channel == NULL) {
            return BTP_STATUS_FAILED;
        }
        chans[i] = channel->chan;
    }

    rc = ble_l2cap_reconfig(chans, cp->num, mtu);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
supported_commands(const void *cmd, uint16_t cmd_len,
                   void *rsp, uint16_t *rsp_len)
{
    struct btp_l2cap_read_supported_commands_rp *rp = rsp;

    /* octet 0 */
    tester_set_bit(rp->data, BTP_L2CAP_READ_SUPPORTED_COMMANDS);
    tester_set_bit(rp->data, BTP_L2CAP_CONNECT);
    tester_set_bit(rp->data, BTP_L2CAP_DISCONNECT);
    tester_set_bit(rp->data, BTP_L2CAP_SEND_DATA);
    tester_set_bit(rp->data, BTP_L2CAP_LISTEN);
    /* octet 1 */
    tester_set_bit(rp->data, BTP_L2CAP_CREDITS);
    *rsp_len = sizeof(*rp) + 2;

    return BTP_STATUS_SUCCESS;
}

static const struct btp_handler handlers[] = {
    {
        .opcode = BTP_L2CAP_READ_SUPPORTED_COMMANDS,
        .index = BTP_INDEX_NONE,
        .expect_len = 0,
        .func = supported_commands,
    },
    {
        .opcode = BTP_L2CAP_CONNECT,
        .expect_len = sizeof(struct btp_l2cap_connect_cmd),
        .func = connect,
    },
    {
        .opcode = BTP_L2CAP_DISCONNECT,
        .expect_len = sizeof(struct btp_l2cap_disconnect_cmd),
        .func = disconnect,
    },
    {
        .opcode = BTP_L2CAP_SEND_DATA,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = send_data,
    },
    {
        .opcode = BTP_L2CAP_LISTEN,
        .expect_len = sizeof(struct btp_l2cap_listen_cmd),
        .func = listen,
    },
    {
        .opcode = BTP_L2CAP_RECONFIGURE,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = reconfigure,
    },
    {
        .opcode = BTP_L2CAP_CREDITS,
        .expect_len = sizeof(struct btp_l2cap_credits_cmd),
        .func = credits,
    },
};

uint8_t
tester_init_l2cap(void)
{
    int rc;

    /* For testing we want to support all the available channels */
    rc = os_mempool_init(&sdu_coc_mbuf_mempool, TESTER_COC_BUF_COUNT,
                         TESTER_COC_MTU, tester_sdu_coc_mem,
                         "tester_coc_sdu_pool");
    assert(rc == 0);

    rc = os_mbuf_pool_init(&sdu_os_mbuf_pool, &sdu_coc_mbuf_mempool,
                           TESTER_COC_MTU, TESTER_COC_BUF_COUNT);
    assert(rc == 0);

    tester_register_command_handlers(BTP_SERVICE_ID_L2CAP, handlers,
                                     ARRAY_SIZE(handlers));

    return BTP_STATUS_SUCCESS;
}

uint8_t
tester_unregister_l2cap(void)
{
    return BTP_STATUS_SUCCESS;
}

#endif
