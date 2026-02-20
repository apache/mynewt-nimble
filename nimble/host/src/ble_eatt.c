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

#include "host/ble_l2cap.h"
#include "os/os_mbuf.h"
#include "os/os_mempool.h"
#include "syscfg/syscfg.h"
#define BLE_NPL_LOG_MODULE BLE_EATT_LOG
#include <nimble/nimble_npl_log.h>

#if MYNEWT_VAL(BLE_EATT_CHAN_NUM) > 0

#include "os/mynewt.h"
#include "mem/mem.h"
#include "host/ble_hs_log.h"
#include "ble_att_cmd_priv.h"
#include "ble_hs_priv.h"
#include "ble_l2cap_priv.h"
#include "ble_eatt_priv.h"
#include "services/gatt/ble_svc_gatt.h"

struct ble_eatt {
    SLIST_ENTRY(ble_eatt) next;
    uint16_t conn_handle;
    struct ble_l2cap_chan *chan;
    uint8_t client_op;

    /* Packet transmit queue */
    STAILQ_HEAD(, os_mbuf_pkthdr) eatt_tx_q;

    struct ble_npl_event setup_ev;
    struct ble_npl_event wakeup_ev;
};

/**
 * Outgoing EATT multi-channel connect request context
 * Passed as cb_arg to ble_l2cap_enhanced_connect(); each successfull
 * connected CoC channel is handed of to its own ble_eatt strcut via chan->cb_arg.
 */
#define BLE_EATT_CONN_REQ_MAGIC (0x45545452UL)
struct ble_eatt_conn_req {
    uint32_t magic;
    uint16_t conn_handle;
    uint8_t wanted;
    uint8_t connected;
    struct ble_npl_event setup_ev;
};

SLIST_HEAD(ble_eatt_list, ble_eatt);

static struct ble_eatt_list g_ble_eatt_list;
static ble_eatt_att_rx_fn ble_eatt_att_rx_cb;

static uint8_t
ble_eatt_max_per_conn(void)
{
    uint8_t max;

    max = (uint8_t)MYNEWT_VAL(BLE_EATT_CHAN_PER_CONN);

    if (max > MYNEWT_VAL(BLE_EATT_CHAN_NUM)) {
        max = MYNEWT_VAL(BLE_EATT_CHAN_NUM);
    }

    return max;
}

static uint8_t
ble_eatt_count(uint16_t conn_handle)
{
    struct ble_eatt *eatt;
    uint8_t count;

    count = 0;

    SLIST_FOREACH(eatt, &g_ble_eatt_list, next) {
        if (eatt->conn_handle == conn_handle) {
            count++;
        }
    }

    return count;
}

#define BLE_EATT_DATABUF_SIZE  ( \
        MYNEWT_VAL(BLE_EATT_MTU) + \
        2 + \
        sizeof (struct os_mbuf_pkthdr) +   \
        sizeof (struct os_mbuf))

#define BLE_EATT_MEMBLOCK_SIZE   \
    (OS_ALIGN(BLE_EATT_DATABUF_SIZE, 4))

#define BLE_EATT_MEMPOOL_SIZE    \
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_EATT_CHAN_NUM) + 1, BLE_EATT_MEMBLOCK_SIZE)
static os_membuf_t ble_eatt_conn_mem[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_EATT_CHAN_NUM),
    sizeof(struct ble_eatt))
];
static struct os_mempool ble_eatt_conn_pool;
static os_membuf_t ble_eatt_conn_req_mem[OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_EATT_CHAN_NUM),
                                            sizeof(struct ble_eatt_conn_req))];
static struct os_mempool ble_eatt_conn_req_pool;
static os_membuf_t ble_eatt_sdu_coc_mem[BLE_EATT_MEMPOOL_SIZE];
struct os_mbuf_pool ble_eatt_sdu_os_mbuf_pool;
static struct os_mempool ble_eatt_sdu_mbuf_mempool;

static struct ble_gap_event_listener ble_eatt_listener;

static struct ble_npl_event g_read_sup_cl_feat_ev;

static void ble_eatt_setup_cb(struct ble_npl_event *ev);
static void ble_eatt_start(uint16_t conn_handle);

static struct ble_eatt *
ble_eatt_find_not_busy(uint16_t conn_handle)
{
    struct ble_eatt *eatt;

    SLIST_FOREACH(eatt, &g_ble_eatt_list, next) {
        if ((eatt->conn_handle == conn_handle) && !eatt->client_op) {
            return eatt;
        }
    }

    return NULL;
}

static struct ble_eatt *
ble_eatt_find_by_conn_handle(uint16_t conn_handle)
{
    struct ble_eatt *eatt;

    SLIST_FOREACH(eatt, &g_ble_eatt_list, next) {
        if (eatt->conn_handle == conn_handle) {
            return eatt;
        }
    }

    return NULL;
}

static struct ble_eatt *
ble_eatt_find_by_conn_handle_and_busy_op(uint16_t conn_handle, uint8_t op)
{
    struct ble_eatt *eatt;

    SLIST_FOREACH(eatt, &g_ble_eatt_list, next) {
        if (eatt->conn_handle == conn_handle && eatt->client_op == op) {
            return eatt;
        }
    }

    return NULL;
}

static struct ble_eatt *
ble_eatt_find(uint16_t conn_handle, uint16_t cid)
{
    struct ble_eatt *eatt;

    SLIST_FOREACH(eatt, &g_ble_eatt_list, next) {
        if ((eatt->conn_handle == conn_handle) &&
            (eatt->chan) &&
            (eatt->chan->scid == cid)) {
            return eatt;
        }
    }

    return NULL;
}

static int
ble_eatt_prepare_rx_sdu(struct ble_l2cap_chan *chan)
{
    int rc;
    struct os_mbuf *om;

    om = os_mbuf_get_pkthdr(&ble_eatt_sdu_os_mbuf_pool, 0);
    if (!om) {
        BLE_EATT_LOG_ERROR("eatt: no memory for sdu\n");
        return BLE_HS_ENOMEM;
    }

    rc = ble_l2cap_recv_ready(chan, om);
    if (rc) {
        BLE_EATT_LOG_ERROR("eatt: Failed to supply RX SDU conn_handle 0x%04x (status=%d)\n",
                            chan->conn_handle, rc);
        os_mbuf_free_chain(om);
    }

    return rc;
}

static void
ble_eatt_wakeup_cb(struct ble_npl_event *ev)
{
    struct ble_eatt *eatt;
    struct os_mbuf *txom;
    struct os_mbuf_pkthdr *omp;
    struct ble_l2cap_chan_info info;

    eatt = ble_npl_event_get_arg(ev);
    assert(eatt);

    omp = STAILQ_FIRST(&eatt->eatt_tx_q);
    if (omp != NULL) {
        STAILQ_REMOVE_HEAD(&eatt->eatt_tx_q, omp_next);

        txom = OS_MBUF_PKTHDR_TO_MBUF(omp);
        ble_l2cap_get_chan_info(eatt->chan, &info);
        ble_eatt_tx(eatt->conn_handle, info.dcid, txom);
    }
}

static struct ble_eatt *
ble_eatt_alloc(void)
{
    struct ble_eatt *eatt;

    eatt = os_memblock_get(&ble_eatt_conn_pool);
    if (!eatt) {
        BLE_EATT_LOG_WARN("eatt: Failed to allocate new eatt context\n");
        return NULL;
    }

    SLIST_INSERT_HEAD(&g_ble_eatt_list, eatt, next);

    eatt->conn_handle = BLE_HS_CONN_HANDLE_NONE;
    eatt->chan = NULL;
    eatt->client_op = 0;

    STAILQ_INIT(&eatt->eatt_tx_q);
    ble_npl_event_init(&eatt->setup_ev, ble_eatt_setup_cb, eatt);
    ble_npl_event_init(&eatt->wakeup_ev, ble_eatt_wakeup_cb, eatt);

    return eatt;
}

static void
ble_eatt_free(struct ble_eatt *eatt)
{
    struct os_mbuf_pkthdr *omp;

    while ((omp = STAILQ_FIRST(&eatt->eatt_tx_q)) != NULL) {
        STAILQ_REMOVE_HEAD(&eatt->eatt_tx_q, omp_next);
        os_mbuf_free_chain(OS_MBUF_PKTHDR_TO_MBUF(omp));
    }

    SLIST_REMOVE(&g_ble_eatt_list, eatt, ble_eatt, next);
    os_memblock_put(&ble_eatt_conn_pool, eatt);
}

static struct ble_eatt_conn_req *
ble_eatt_conn_req_alloc(void)
{
    struct ble_eatt_conn_req *req;

    req = os_memblock_get(&ble_eatt_conn_req_pool);
    if (!req) {
        BLE_EATT_LOG_WARN("eatt: Failed to allocate eatt connect request\n");
        return NULL;
    }

    memset(req, 0, sizeof(*req));
    req->magic = BLE_EATT_CONN_REQ_MAGIC;
    ble_npl_event_init(&req->setup_ev, ble_eatt_setup_cb, req);

    return req;
}

static void
ble_eatt_conn_req_free(struct ble_eatt_conn_req *req)
{
    if (!req) {
        return;
    }

    req->magic = 0;
    os_memblock_put(&ble_eatt_conn_req_pool, req);
}

static int
ble_eatt_l2cap_event_fn(struct ble_l2cap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_eatt_conn_req *req;
    struct ble_eatt *eatt;
    uint8_t opcode;
    int rc;

    /**
     * Outgoing multi-channel setup arg is a conn-req  for CONNECTED callbacks.
     * After allocating per-channel context, we hand off by setting chan->cb_arg.
     */
    if (event->type == BLE_L2CAP_EVENT_COC_CONNECTED) {
        req = (struct ble_eatt_conn_req *)arg;
        if (req && req->magic == BLE_EATT_CONN_REQ_MAGIC && req->conn_handle == event->connect.conn_handle) {
            BLE_EATT_LOG_DEBUG("eatt: Connected (outgoing)\n");

            if (event->connect.status) {
                req->connected++;
                if (req->connected >= req->wanted) {
                    ble_eatt_conn_req_free(req);
                }
                return 0;
            }
            eatt = ble_eatt_alloc();
            if (!eatt) {
                ble_l2cap_disconnect(event->connect.chan);
                req->connected++;
                if (req->connected >= req->wanted) {
                    ble_eatt_conn_req_free(req);
                }
                return 0;
            }

            eatt->conn_handle = event->connect.conn_handle;
            eatt->chan = event->connect.chan;

            /* Handoff: Future events for this channel user per-channel eatt context */
            event->connect.chan->cb_arg = eatt;

            req->connected++;
            if (req->connected >= req->wanted) {
                ble_eatt_conn_req_free(req);
            }

            return 0;
        }
    }

    /* Default: per channel callbacks use struct ble_eatt */
    eatt = (struct ble_eatt *)arg;

    switch (event->type) {
    case BLE_L2CAP_EVENT_COC_CONNECTED:
        BLE_EATT_LOG_DEBUG("eatt: Connected \n");
        if (event->connect.status) {
            ble_eatt_free(eatt);
            return 0;
        }
        eatt->chan = event->connect.chan;

        break;
    case BLE_L2CAP_EVENT_COC_DISCONNECTED:
        BLE_EATT_LOG_DEBUG("eatt: Disconnected \n");
        ble_eatt_free(eatt);
        break;
    case BLE_L2CAP_EVENT_COC_ACCEPT:
        BLE_EATT_LOG_DEBUG("eatt: Accept request\n");
        if (ble_eatt_count(event->accept.conn_handle) >= ble_eatt_max_per_conn()) {
            /* EATT bearer channel limit reached */
            return BLE_HS_ENOMEM;
        }

        eatt = ble_eatt_alloc();
        if (!eatt) {
            return BLE_HS_ENOMEM;
        }

        eatt->conn_handle = event->accept.conn_handle;
        event->accept.chan->cb_arg = eatt;

        rc = ble_eatt_prepare_rx_sdu(event->accept.chan);
        if (rc) {
            ble_eatt_free(eatt);
            return rc;
        }

        break;
    case BLE_L2CAP_EVENT_COC_TX_UNSTALLED:
        ble_npl_eventq_put(ble_hs_evq_get(), &eatt->wakeup_ev);
        break;
    case BLE_L2CAP_EVENT_COC_DATA_RECEIVED:
        assert(eatt->chan == event->receive.chan);
        opcode = event->receive.sdu_rx->om_data[0];
        if (ble_eatt_supported_rsp(opcode)) {
            ble_npl_eventq_put(ble_hs_evq_get(), &eatt->wakeup_ev);
        } else if (!ble_eatt_supported_req(opcode)) {
            /* If an ATT PDU is supported on any ATT bearer, then it shall be
             * supported on all supported ATT bearers with the following
             * exceptions:
             *  • The Exchange MTU sub-procedure shall only be supported on the
             *  LE Fixed Channel Unenhanced ATT bearer.
             *  • The Signed Write Without Response sub-procedure shall only be
             *  supported on the LE Fixed Channel Unenhanced ATT bearer.
             */
            ble_l2cap_disconnect(eatt->chan);
            return BLE_HS_EREJECT;
        }

        assert (!ble_gap_conn_find(event->receive.conn_handle, &desc));
        /* As per BLE 5.4 Standard, Vol. 3, Part F, section 5.3.2
         * (ENHANCED ATT BEARER L2CAP INTEROPERABILITY REQUIREMENTS:
         * Channel Requirements):
         * The channel shall be encrypted.
         *
         * Disconnect peer with invalid behavior - ATT PDU received before
         * encryption.
         */
        if (!desc.sec_state.encrypted) {
            ble_l2cap_disconnect(eatt->chan);
            return BLE_HS_EREJECT;
        }

        ble_eatt_att_rx_cb(event->receive.conn_handle, eatt->chan->scid, &event->receive.sdu_rx);
        if (event->receive.sdu_rx) {
            os_mbuf_free_chain(event->receive.sdu_rx);
            event->receive.sdu_rx = NULL;
        }

        /* Receiving L2CAP data is no longer possible, terminate connection */
        rc = ble_eatt_prepare_rx_sdu(event->receive.chan);
        if (rc) {
            ble_l2cap_disconnect(eatt->chan);
            return BLE_HS_ENOMEM;
        }
        break;
    default:
        break;
    }

    return 0;
}

static void
ble_eatt_setup_cb(struct ble_npl_event *ev)
{
    struct os_mbuf *om[MYNEWT_VAL(BLE_EATT_CHAN_PER_CONN)];
    struct ble_eatt_conn_req *req;
    uint8_t channels;
    int rc;
    int i;
    int j;

    req = ble_npl_event_get_arg(ev);
    assert(req);
    assert(req->magic == BLE_EATT_CONN_REQ_MAGIC);

    channels = req->wanted;
    if (channels == 0) {
        ble_eatt_conn_req_free(req);

        return;
    }

    for (i = 0; i < channels; i++) {
        om[i] = os_mbuf_get_pkthdr(&ble_eatt_sdu_os_mbuf_pool, 0);
        if (!om[i]) {
            BLE_EATT_LOG_ERROR("eatt: no memory for sdu");

            for (j = 0;  j < i; j++) {
                os_mbuf_free_chain(om[j]);
            }
            ble_eatt_conn_req_free(req);
            return;
        }
    }

    BLE_EATT_LOG_DEBUG("eatt: connecting %u eatt channels on conn_handle 0x%04x", channels, req->conn_handle);

    rc = ble_l2cap_enhanced_connect(req->conn_handle, BLE_EATT_PSM,
                                    MYNEWT_VAL(BLE_EATT_MTU), channels, om,
                                    ble_eatt_l2cap_event_fn, req);

    if (rc) {
        BLE_EATT_LOG_ERROR("eatt: Failed to connect EATT on conn_handle 0x%04x (status=%d)\n",
                            req->conn_handle, rc);
        for (i = 0; i < channels; i++) {
            os_mbuf_free_chain(om[i]);
        }
        ble_eatt_conn_req_free(req);
        return;
    }
    /* On success L2CAP owns 'om[]'. The req is freed when all CONNECTED
     * callbacks (success or failure) have been delivered
     */
}

static int
ble_gatt_eatt_write_cl_cb(uint16_t conn_handle,
                          const struct ble_gatt_error *error,
                          struct ble_gatt_attr *attr, void *arg)
{
    if (error == NULL || (error->status != 0 && error->status != BLE_HS_EDONE)) {
        BLE_EATT_LOG_DEBUG("eatt: Cannot write to Client Supported features on peer device\n");
        return 0;
    }

    ble_eatt_start(conn_handle);

    return 0;
}

static int
ble_gatt_eatt_read_cl_uuid_cb(uint16_t conn_handle,
                              const struct ble_gatt_error *error,
                              struct ble_gatt_attr *attr, void *arg)
{
    uint8_t client_supported_feat;
    int rc;

    if (error == NULL || (error->status != 0 && error->status != BLE_HS_EDONE)) {
        BLE_EATT_LOG_DEBUG("eatt: Cannot find Client Supported features on peer device\n");
        return BLE_HS_EDONE;
    }

    if (attr == NULL) {
        BLE_EATT_LOG_ERROR("eatt: Invalid attribute \n");
        return BLE_HS_EDONE;
    }

    if (error->status == 0) {
        client_supported_feat = ble_svc_gatt_get_local_cl_supported_feat();
        rc = ble_gattc_write_flat(conn_handle, attr->handle, &client_supported_feat, 1,
                                  ble_gatt_eatt_write_cl_cb, NULL);
        BLE_EATT_LOG_DEBUG("eatt: %s , write rc = %d \n", __func__, rc);
        assert(rc == 0);
        return 0;
    }

    return BLE_HS_EDONE;
}

static void
ble_gatt_eatt_read_cl_uuid(struct ble_npl_event *ev)
{
    uint16_t conn_handle;

    conn_handle = POINTER_TO_UINT(ble_npl_event_get_arg(ev));

    ble_gattc_read_by_uuid(conn_handle, 1, 0xffff,
                           BLE_UUID16_DECLARE(BLE_SVC_GATT_CHR_CLIENT_SUPPORTED_FEAT_UUID16),
                           ble_gatt_eatt_read_cl_uuid_cb, NULL);
}

static int
ble_eatt_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_eatt *eatt;

    switch (event->type) {
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status != 0) {
            return 0;
        }

#if  MYNEWT_VAL(BLE_EATT_AUTO_CONNECT)
        /* Don't try to connect if we already reached conn limit */
        if (ble_eatt_count(event->enc_change.conn_handle) >= ble_eatt_max_per_conn()) {
            return 0;
        }

        BLE_EATT_LOG_DEBUG("eatt: Encryption enabled, connecting EATT (conn_handle=0x%04x)\n",
                            event->enc_change.conn_handle);

        ble_npl_event_set_arg(&g_read_sup_cl_feat_ev, UINT_TO_POINTER(event->enc_change.conn_handle));
        ble_npl_eventq_put(ble_hs_evq_get(), &g_read_sup_cl_feat_ev);
#endif
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        /* Free all EATT contexts associated with this connection */
        while ((eatt = ble_eatt_find_by_conn_handle(event->disconnect.conn.conn_handle)) != NULL) {
                ble_eatt_free(eatt);
        }
        break;
    default:
        break;
    }

    return 0;
}

uint16_t
ble_eatt_get_available_chan_cid(uint16_t conn_handle, uint8_t op)
{
    struct ble_eatt * eatt;

    eatt = ble_eatt_find_not_busy(conn_handle);
    if (!eatt) {
        return BLE_L2CAP_CID_ATT;
    }

    eatt->client_op = op;

    return eatt->chan->scid;
}

void
ble_eatt_release_chan(uint16_t conn_handle, uint8_t op)
{
    struct ble_eatt * eatt;

    eatt = ble_eatt_find_by_conn_handle_and_busy_op(conn_handle, op);
    if (!eatt) {
        BLE_EATT_LOG_WARN("ble_eatt_release_chan:"
                          "EATT not found for conn_handle 0x%04x, operation 0x%02\n", conn_handle, op);
        return;
    }

    eatt->client_op = 0;
}

int
ble_eatt_tx(uint16_t conn_handle, uint16_t cid, struct os_mbuf *txom)
{
    struct ble_eatt *eatt;
    int rc;

    BLE_EATT_LOG_DEBUG("eatt: %s, size %d ", __func__, OS_MBUF_PKTLEN(txom));
    eatt = ble_eatt_find(conn_handle, cid);
    if (!eatt || !eatt->chan) {
        BLE_EATT_LOG_ERROR("Eatt not available");
        rc = BLE_HS_ENOENT;
        goto error;
    }

    rc = ble_l2cap_send(eatt->chan, txom);
    if (rc == 0) {
        goto done;
    }

    if (rc == BLE_HS_ESTALLED) {
        BLE_EATT_LOG_DEBUG("ble_eatt_tx: Eatt stalled");
    } else if (rc == BLE_HS_EBUSY) {
        BLE_EATT_LOG_DEBUG("ble_eatt_tx: Message queued");
        STAILQ_INSERT_HEAD(&eatt->eatt_tx_q, OS_MBUF_PKTHDR(txom), omp_next);
        ble_npl_eventq_put(ble_hs_evq_get(), &eatt->wakeup_ev);
    } else {
        BLE_EATT_LOG_ERROR("eatt: %s, ERROR %d ", __func__, rc);
        assert(0);
    }
done:
    return 0;

error:
    os_mbuf_free_chain(txom);

    return rc;
}

static void
ble_eatt_start(uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;
    struct ble_eatt_conn_req *req;
    uint8_t cur_cnt;
    uint8_t max_cnt;
    uint8_t add;
    int rc;

    rc = ble_gap_conn_find(conn_handle, &desc);
    assert(rc == 0);
    if (desc.role != BLE_GAP_ROLE_MASTER) {
        /* Let master to create ecoc.
         * TODO: Slave could setup after some timeout
         */
        return;
    }

    cur_cnt = ble_eatt_count(conn_handle);
    max_cnt = ble_eatt_max_per_conn();
    if (cur_cnt >= max_cnt) {
        return;
    }

    add = max_cnt - cur_cnt;

    req = ble_eatt_conn_req_alloc();
    if (!req) {
        return;
    }

    req->conn_handle = conn_handle;
    req->wanted = add;
    req->connected = 0;

    ble_npl_eventq_put(ble_hs_evq_get(), &req->setup_ev);
}

int
ble_eatt_connect(uint16_t conn_handle, uint8_t chan_num)
{
    struct ble_gap_conn_desc desc;
    struct ble_eatt_conn_req *req;
    uint8_t target_cnt;
    uint8_t cur_cnt;
    uint8_t max_cnt;
    uint8_t add;
    int rc;

    rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        return rc;
    }

    max_cnt = ble_eatt_max_per_conn();
    cur_cnt = ble_eatt_count(conn_handle);

    /* If CHAN_NUM == 0 -> connect max channels */
    if (chan_num == 0) {
        target_cnt = max_cnt;
    } else {
        if (chan_num > max_cnt) {
            return BLE_HS_EINVAL;
        }
        target_cnt = chan_num;
    }

    if (cur_cnt >= target_cnt) {
        return 0;
    }

    add = target_cnt - cur_cnt;

    req = ble_eatt_conn_req_alloc();
    if (!req) {
        return BLE_HS_ENOMEM;
    }

    req->conn_handle = conn_handle;
    req->wanted = add;
    req->connected = 0;

    ble_npl_eventq_put(ble_hs_evq_get(), &req->setup_ev);

    return 0;
}

void
ble_eatt_init(ble_eatt_att_rx_fn att_rx_cb)
{
    int rc;

    rc = mem_init_mbuf_pool(ble_eatt_sdu_coc_mem,
                            &ble_eatt_sdu_mbuf_mempool,
                            &ble_eatt_sdu_os_mbuf_pool,
                            MYNEWT_VAL(BLE_EATT_CHAN_NUM) + 1,
                            BLE_EATT_MEMBLOCK_SIZE,
                            "ble_eatt_sdu");
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

    rc = os_mempool_init(&ble_eatt_conn_pool, MYNEWT_VAL(BLE_EATT_CHAN_NUM),
                         sizeof (struct ble_eatt),
                         ble_eatt_conn_mem, "ble_eatt_conn_pool");
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

    rc = os_mempool_init(&ble_eatt_conn_req_pool, MYNEWT_VAL(BLE_EATT_CHAN_NUM),
                         sizeof(struct ble_eatt_conn_req),
                         ble_eatt_conn_req_mem, "ble_eatt_conn_req_pool");
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

    ble_gap_event_listener_register(&ble_eatt_listener, ble_eatt_gap_event, NULL);
    ble_l2cap_create_server(BLE_EATT_PSM, MYNEWT_VAL(BLE_EATT_MTU), ble_eatt_l2cap_event_fn, NULL);

    ble_npl_event_init(&g_read_sup_cl_feat_ev, ble_gatt_eatt_read_cl_uuid, NULL);

    ble_eatt_att_rx_cb = att_rx_cb;
}
#endif
