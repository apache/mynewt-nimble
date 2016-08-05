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
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "os/os.h"
#include "nimble/ble_hci_trans.h"
#include "ble_hs_priv.h"
#include "ble_hs_dbg_priv.h"

#define BLE_HCI_CMD_TIMEOUT     (OS_TICKS_PER_SEC)

static struct os_mutex ble_hs_hci_mutex;
static struct os_sem ble_hs_hci_sem;

static uint8_t *ble_hs_hci_ack;
static uint16_t ble_hs_hci_buf_sz;
static uint8_t ble_hs_hci_max_pkts;

#if PHONY_HCI_ACKS
static ble_hs_hci_phony_ack_fn *ble_hs_hci_phony_ack_cb;
#endif

#if PHONY_HCI_ACKS
void
ble_hs_hci_set_phony_ack_cb(ble_hs_hci_phony_ack_fn *cb)
{
    ble_hs_hci_phony_ack_cb = cb;
}
#endif

static void
ble_hs_hci_lock(void)
{
    int rc;

    rc = os_mutex_pend(&ble_hs_hci_mutex, 0xffffffff);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0 || rc == OS_NOT_STARTED);
}

static void
ble_hs_hci_unlock(void)
{
    int rc;

    rc = os_mutex_release(&ble_hs_hci_mutex);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0 || rc == OS_NOT_STARTED);
}

int
ble_hs_hci_set_buf_sz(uint16_t pktlen, uint8_t max_pkts)
{
    if (pktlen == 0 || max_pkts == 0) {
        return BLE_HS_EINVAL;
    }

    ble_hs_hci_buf_sz = pktlen;
    ble_hs_hci_max_pkts = max_pkts;

    return 0;
}

static int
ble_hs_hci_rx_cmd_complete(uint8_t event_code, uint8_t *data, int len,
                           struct ble_hs_hci_ack *out_ack)
{
    uint16_t opcode;
    uint8_t *params;
    uint8_t params_len;
    uint8_t num_pkts;

    if (len < BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    num_pkts = data[2];
    opcode = le16toh(data + 3);
    params = data + 5;

    /* XXX: Process num_pkts field. */
    (void)num_pkts;

    out_ack->bha_opcode = opcode;

    params_len = len - BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN;
    if (params_len > 0) {
        out_ack->bha_status = BLE_HS_HCI_ERR(params[0]);
    } else if (opcode == BLE_HCI_OPCODE_NOP) {
        out_ack->bha_status = 0;
    } else {
        out_ack->bha_status = BLE_HS_ECONTROLLER;
    }

    /* Don't include the status byte in the parameters blob. */
    if (params_len > 1) {
        out_ack->bha_params = params + 1;
        out_ack->bha_params_len = params_len - 1;
    } else {
        out_ack->bha_params = NULL;
        out_ack->bha_params_len = 0;
    }

    return 0;
}

static int
ble_hs_hci_rx_cmd_status(uint8_t event_code, uint8_t *data, int len,
                         struct ble_hs_hci_ack *out_ack)
{
    uint16_t opcode;
    uint8_t num_pkts;
    uint8_t status;

    if (len < BLE_HCI_EVENT_CMD_STATUS_LEN) {
        return BLE_HS_ECONTROLLER;
    }

    status = data[2];
    num_pkts = data[3];
    opcode = le16toh(data + 4);

    /* XXX: Process num_pkts field. */
    (void)num_pkts;

    out_ack->bha_opcode = opcode;
    out_ack->bha_params = NULL;
    out_ack->bha_params_len = 0;
    out_ack->bha_status = BLE_HS_HCI_ERR(status);

    return 0;
}

static int
ble_hs_hci_process_ack(uint16_t expected_opcode,
                       uint8_t *params_buf, uint8_t params_buf_len,
                       struct ble_hs_hci_ack *out_ack)
{
    uint8_t event_code;
    uint8_t param_len;
    uint8_t event_len;
    int rc;

    BLE_HS_DBG_ASSERT(ble_hs_hci_ack != NULL);

    /* Count events received */
    STATS_INC(ble_hs_stats, hci_event);

    /* Display to console */
    ble_hs_dbg_event_disp(ble_hs_hci_ack);

    event_code = ble_hs_hci_ack[0];
    param_len = ble_hs_hci_ack[1];
    event_len = param_len + 2;

    /* Clear ack fields up front to silence spurious gcc warnings. */
    memset(out_ack, 0, sizeof *out_ack);

    switch (event_code) {
    case BLE_HCI_EVCODE_COMMAND_COMPLETE:
        rc = ble_hs_hci_rx_cmd_complete(event_code, ble_hs_hci_ack,
                                         event_len, out_ack);
        break;

    case BLE_HCI_EVCODE_COMMAND_STATUS:
        rc = ble_hs_hci_rx_cmd_status(event_code, ble_hs_hci_ack,
                                       event_len, out_ack);
        break;

    default:
        BLE_HS_DBG_ASSERT(0);
        rc = BLE_HS_EUNKNOWN;
        break;
    }

    if (rc == 0) {
        if (params_buf == NULL) {
            out_ack->bha_params_len = 0;
        } else {
            if (out_ack->bha_params_len > params_buf_len) {
                out_ack->bha_params_len = params_buf_len;
                rc = BLE_HS_ECONTROLLER;
            }
            memcpy(params_buf, out_ack->bha_params, out_ack->bha_params_len);
        }
        out_ack->bha_params = params_buf;

        if (out_ack->bha_opcode != expected_opcode) {
            rc = BLE_HS_ECONTROLLER;
        }
    }

    if (rc != 0) {
        STATS_INC(ble_hs_stats, hci_invalid_ack);
    }

    return rc;
}

static int
ble_hs_hci_wait_for_ack(void)
{
    int rc;

#if PHONY_HCI_ACKS
    if (ble_hs_hci_phony_ack_cb == NULL) {
        rc = BLE_HS_ETIMEOUT_HCI;
    } else {
        ble_hs_hci_ack =
            ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_CMD);
        BLE_HS_DBG_ASSERT(ble_hs_hci_ack != NULL);
        rc = ble_hs_hci_phony_ack_cb(ble_hs_hci_ack, 260);
    }
#else
    rc = os_sem_pend(&ble_hs_hci_sem, BLE_HCI_CMD_TIMEOUT);
    switch (rc) {
    case 0:
        BLE_HS_DBG_ASSERT(ble_hs_hci_ack != NULL);
        break;
    case OS_TIMEOUT:
        rc = BLE_HS_ETIMEOUT_HCI;
        STATS_INC(ble_hs_stats, hci_timeout);
        break;
    default:
        rc = BLE_HS_EOS;
        break;
    }
#endif

    return rc;
}

int
ble_hs_hci_cmd_tx(void *cmd, void *evt_buf, uint8_t evt_buf_len,
                  uint8_t *out_evt_buf_len)
{
    struct ble_hs_hci_ack ack;
    uint16_t opcode;
    int rc;

    opcode = le16toh(cmd);

    BLE_HS_DBG_ASSERT(ble_hs_hci_ack == NULL);
    ble_hs_hci_lock();

    rc = ble_hs_hci_cmd_send_buf(cmd);
    if (rc != 0) {
        goto done;
    }

    rc = ble_hs_hci_wait_for_ack();
    if (rc != 0) {
        ble_hs_sched_reset(rc);
        goto done;
    }

    rc = ble_hs_hci_process_ack(opcode, evt_buf, evt_buf_len, &ack);
    if (rc != 0) {
        ble_hs_sched_reset(rc);
        goto done;
    }

    if (out_evt_buf_len != NULL) {
        *out_evt_buf_len = ack.bha_params_len;
    }

    rc = ack.bha_status;

done:
    if (ble_hs_hci_ack != NULL) {
        ble_hci_trans_buf_free(ble_hs_hci_ack);
        ble_hs_hci_ack = NULL;
    }

    ble_hs_hci_unlock();
    return rc;
}

int
ble_hs_hci_cmd_tx_empty_ack(void *cmd)
{
    int rc;

    rc = ble_hs_hci_cmd_tx(cmd, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_hs_hci_rx_ack(uint8_t *ack_ev)
{
    if (ble_hs_hci_sem.sem_tokens != 0) {
        /* This ack is unexpected; ignore it. */
        ble_hci_trans_buf_free(ack_ev);
        return;
    }
    BLE_HS_DBG_ASSERT(ble_hs_hci_ack == NULL);

    /* Unblock the application now that the HCI command buffer is populated
     * with the acknowledgement.
     */
    ble_hs_hci_ack = ack_ev;
    os_sem_release(&ble_hs_hci_sem);
}

int
ble_hs_hci_rx_evt(uint8_t *hci_ev, void *arg)
{
    int enqueue;

    BLE_HS_DBG_ASSERT(hci_ev != NULL);

    switch (hci_ev[0]) {
    case BLE_HCI_EVCODE_COMMAND_COMPLETE:
    case BLE_HCI_EVCODE_COMMAND_STATUS:
        if (hci_ev[3] == 0 && hci_ev[4] == 0) {
            enqueue = 1;
        } else {
            ble_hs_hci_rx_ack(hci_ev);
            enqueue = 0;
        }
        break;

    default:
        enqueue = 1;
        break;
    }

    if (enqueue) {
        ble_hs_enqueue_hci_event(hci_ev);
    }

    return 0;
}


/**
 * Splits an appropriately-sized fragment from the front of an outgoing ACL
 * data packet, if necessary.  If the packet size is within the controller's
 * buffer size requirements, no splitting is performed.  The fragment data is
 * removed from the data packet mbuf.
 *
 * @param om                    The ACL data packet.  If this constitutes a
 *                                  single fragment, it gets set to NULL on
 *                                  success.
 * @param out_frag              On success, this points to the fragment to
 *                                  send.  If the entire packet can fit within
 *                                  a single fragment, this will point to the
 *                                  ACL data packet itself ('om').
 *
 * @return                      0 on success;
 *                              BLE host core return code on error.
 */
static int
ble_hs_hci_split_frag(struct os_mbuf **om, struct os_mbuf **out_frag)
{
    struct os_mbuf *frag;
    int rc;

    /* Assume failure. */
    *out_frag = NULL;

    if (OS_MBUF_PKTLEN(*om) <= ble_hs_hci_buf_sz) {
        /* Final fragment. */
        *out_frag = *om;
        *om = NULL;
        return 0;
    }

    frag = ble_hs_mbuf_acm_pkt();
    if (frag == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    /* Move data from the front of the packet into the fragment mbuf. */
    rc = os_mbuf_appendfrom(frag, *om, 0, ble_hs_hci_buf_sz);
    if (rc != 0) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }
    os_mbuf_adj(*om, ble_hs_hci_buf_sz);

    /* More fragments to follow. */
    *out_frag = frag;
    return 0;

err:
    os_mbuf_free_chain(frag);
    return rc;
}

static struct os_mbuf *
ble_hs_hci_acl_hdr_prepend(struct os_mbuf *om, uint16_t handle,
                           uint8_t pb_flag)
{
    struct hci_data_hdr hci_hdr;
    struct os_mbuf *om2;

    hci_hdr.hdh_handle_pb_bc =
        ble_hs_hci_util_handle_pb_bc_join(handle, pb_flag, 0);
    htole16(&hci_hdr.hdh_len, OS_MBUF_PKTHDR(om)->omp_len);

    om2 = os_mbuf_prepend(om, sizeof hci_hdr);
    if (om2 == NULL) {
        return NULL;
    }

    om = om2;
    om = os_mbuf_pullup(om, sizeof hci_hdr);
    if (om == NULL) {
        return NULL;
    }

    memcpy(om->om_data, &hci_hdr, sizeof hci_hdr);

    BLE_HS_LOG(DEBUG, "host tx hci data; handle=%d length=%d\n", handle,
               le16toh(&hci_hdr.hdh_len));

    return om;
}

/**
 * Transmits an HCI ACL data packet.  This function consumes the supplied mbuf,
 * regardless of the outcome.
 *
 * XXX: Ensure the controller has sufficient buffer capacity for the outgoing
 * fragments.
 */
int
ble_hs_hci_acl_tx(struct ble_hs_conn *connection, struct os_mbuf *txom)
{
    struct os_mbuf *frag;
    uint8_t pb;
    int rc;

    /* The first fragment uses the first-non-flush packet boundary value.
     * After sending the first fragment, pb gets set appropriately for all
     * subsequent fragments in this packet.
     */
    pb = BLE_HCI_PB_FIRST_NON_FLUSH;

    /* Send fragments until the entire packet has been sent. */
    while (txom != NULL) {
        rc = ble_hs_hci_split_frag(&txom, &frag);
        if (rc != 0) {
            goto err;
        }

        frag = ble_hs_hci_acl_hdr_prepend(frag, connection->bhc_handle, pb);
        if (frag == NULL) {
            rc = BLE_HS_ENOMEM;
            goto err;
        }
        pb = BLE_HCI_PB_MIDDLE;

        BLE_HS_LOG(DEBUG, "ble_hs_hci_acl_tx(): ");
        ble_hs_log_mbuf(frag);
        BLE_HS_LOG(DEBUG, "\n");

        /* XXX: Try to pullup the entire fragment.  The controller currently
         * requires the entire fragment to fit in a single buffer.  When this
         * restriction is removed from the controller, this operation can be
         * removed.
         */
        frag = os_mbuf_pullup(frag, OS_MBUF_PKTLEN(frag));
        if (frag == NULL) {
            rc = BLE_HS_ENOMEM;
            goto err;
        }

        rc = ble_hs_tx_data(frag);
        if (rc != 0) {
            goto err;
        }

        connection->bhc_outstanding_pkts++;
    }

    return 0;

err:
    BLE_HS_DBG_ASSERT(rc != 0);

    os_mbuf_free_chain(txom);
    return rc;
}

void
ble_hs_hci_init(void)
{
    int rc;

    rc = os_sem_init(&ble_hs_hci_sem, 0);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

    rc = os_mutex_init(&ble_hs_hci_mutex);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);
}
