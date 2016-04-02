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

#include <assert.h>
#include <errno.h>
#include "bsp/bsp.h"
#include "stats/stats.h"
#include "util/tpq.h"
#include "os/os.h"
#include "nimble/hci_transport.h"
#include "host/host_hci.h"
#include "ble_hs_priv.h"
#ifdef PHONY_TRANSPORT
#include "host/ble_hs_test.h"
#endif

#ifdef ARCH_sim
#define BLE_HS_STACK_SIZE   (1024)
#else
#define BLE_HS_STACK_SIZE   (512)//(250)
#endif

static struct log_handler ble_hs_log_console_handler;
struct ble_hs_dev ble_hs_our_dev;
struct log ble_hs_log;

static struct os_task ble_hs_task;
static os_stack_t ble_hs_stack[BLE_HS_STACK_SIZE] bssnz_t;

#define HCI_CMD_BUF_SIZE    (260)       /* XXX: temporary, Fix later */
struct os_mempool g_hci_cmd_pool;
static void *ble_hs_hci_cmd_buf;

/* XXX: this might be transport layer*/
#define HCI_OS_EVENT_BUF_SIZE   (sizeof(struct os_event))

struct os_mempool g_hci_os_event_pool;
static void *ble_hs_hci_os_event_buf;

/* Host HCI Task Events */
struct os_eventq ble_hs_evq;
static struct os_event ble_hs_kick_hci_ev;
static struct os_event ble_hs_kick_gatt_ev;
static struct os_event ble_hs_kick_l2cap_sig_ev;
static struct os_event ble_hs_kick_l2cap_sm_ev;

static struct os_mqueue ble_hs_rx_q;
static struct os_mqueue ble_hs_tx_q;

STATS_SECT_DECL(ble_hs_stats) ble_hs_stats;
STATS_NAME_START(ble_hs_stats)
    STATS_NAME(ble_hs_stats, conn_create)
    STATS_NAME(ble_hs_stats, conn_delete)
    STATS_NAME(ble_hs_stats, hci_cmd)
    STATS_NAME(ble_hs_stats, hci_event)
    STATS_NAME(ble_hs_stats, hci_invalid_ack)
    STATS_NAME(ble_hs_stats, hci_unknown_event)
STATS_NAME_END(ble_hs_stats)

void
ble_hs_process_tx_data_queue(void)
{
    struct os_mbuf *om;

    while ((om = os_mqueue_get(&ble_hs_tx_q)) != NULL) {
#ifdef PHONY_TRANSPORT
        ble_hs_test_pkt_txed(om);
#else
        ble_hci_transport_host_acl_data_send(om);
#endif
    }
}

static void
ble_hs_process_rx_data_queue(void)
{
    struct os_mbuf *om;

    while ((om = os_mqueue_get(&ble_hs_rx_q)) != NULL) {
        host_hci_data_rx(om);
    }
}

static void
ble_hs_task_handler(void *arg)
{
    struct os_event *ev;
    struct os_callout_func *cf;
    int rc;

    ble_gattc_started();

    rc = ble_hs_startup_go();
    assert(rc == 0);

    while (1) {
        ev = os_eventq_get(&ble_hs_evq);
        switch (ev->ev_type) {
        case OS_EVENT_T_TIMER:
            cf = (struct os_callout_func *)ev;
            assert(cf->cf_func);
            cf->cf_func(ev->ev_arg);
            break;

        case BLE_HOST_HCI_EVENT_CTLR_EVENT:
            /* Process HCI event from controller */
            host_hci_os_event_proc(ev);
            break;

        case OS_EVENT_T_MQUEUE_DATA:
            ble_hs_process_tx_data_queue();
            ble_hs_process_rx_data_queue();
            break;

        case BLE_HS_KICK_HCI_EVENT:
            ble_hci_sched_wakeup();
            break;

        case BLE_HS_KICK_GATT_EVENT:
            ble_gattc_wakeup();
            break;

        case BLE_HS_KICK_L2CAP_SIG_EVENT:
            ble_l2cap_sig_wakeup();
            break;

        case BLE_HS_KICK_L2CAP_SM_EVENT:
            ble_l2cap_sm_wakeup();
            break;

        default:
            BLE_HS_DBG_ASSERT(0);
            break;
        }
    }
}

/**
 * Called when a data packet is received from the controller.  This function
 * consumes the supplied mbuf, regardless of the outcome.
 *
 * @param om                    The incoming data packet, beginning with the
 *                                  HCI ACL data header.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_hs_rx_data(struct os_mbuf *om)
{
    int rc;

    rc = os_mqueue_put(&ble_hs_rx_q, &ble_hs_evq, om);
    if (rc != 0) {
        return BLE_HS_EOS;
    }

    return 0;
}

int
ble_hs_tx_data(struct os_mbuf *om)
{
    int rc;

    rc = os_mqueue_put(&ble_hs_tx_q, &ble_hs_evq, om);
    if (rc != 0) {
        return BLE_HS_EOS;
    }

    return 0;
}

/**
 * Wakes the BLE host task so that it can process hci events.
 */
void
ble_hs_kick_hci(void)
{
    os_eventq_put(&ble_hs_evq, &ble_hs_kick_hci_ev);
}

/**
 * Wakes the BLE host task so that it can process GATT events.
 */
void
ble_hs_kick_gatt(void)
{
    os_eventq_put(&ble_hs_evq, &ble_hs_kick_gatt_ev);
}

/**
 * Wakes the BLE host task so that it can process L2CAP sig events.
 */
void
ble_hs_kick_l2cap_sig(void)
{
    os_eventq_put(&ble_hs_evq, &ble_hs_kick_l2cap_sig_ev);
}

/**
 * Wakes the BLE host task so that it can process L2CAP security events.
 */
void
ble_hs_kick_l2cap_sm(void)
{
    os_eventq_put(&ble_hs_evq, &ble_hs_kick_l2cap_sm_ev);
}

static void
ble_hs_free_mem(void)
{
    free(ble_hs_hci_cmd_buf);
    ble_hs_hci_cmd_buf = NULL;

    free(ble_hs_hci_os_event_buf);
    ble_hs_hci_os_event_buf = NULL;
}

/**
 * Initializes the host portion of the BLE stack.
 */
int
ble_hs_init(uint8_t prio, struct ble_hs_cfg *cfg)
{
    int rc;

    ble_hs_free_mem();

    ble_hs_cfg_init(cfg);

    log_init();
    log_console_handler_init(&ble_hs_log_console_handler);
    log_register("ble_hs", &ble_hs_log, &ble_hs_log_console_handler);

    os_task_init(&ble_hs_task, "ble_hs", ble_hs_task_handler, NULL, prio,
                 OS_WAIT_FOREVER, ble_hs_stack, BLE_HS_STACK_SIZE);

    ble_hs_hci_cmd_buf = malloc(OS_MEMPOOL_BYTES(ble_hs_cfg.max_hci_bufs,
                                                 HCI_CMD_BUF_SIZE));
    if (ble_hs_hci_cmd_buf == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    /* Create memory pool of command buffers */
    rc = os_mempool_init(&g_hci_cmd_pool, ble_hs_cfg.max_hci_bufs,
                         HCI_CMD_BUF_SIZE, ble_hs_hci_cmd_buf,
                         "HCICmdPool");
    assert(rc == 0);

    ble_hs_hci_os_event_buf = malloc(OS_MEMPOOL_BYTES(ble_hs_cfg.max_hci_bufs,
                                                      HCI_OS_EVENT_BUF_SIZE));
    if (ble_hs_hci_os_event_buf == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    /* Create memory pool of OS events */
    rc = os_mempool_init(&g_hci_os_event_pool, ble_hs_cfg.max_hci_bufs,
                         HCI_OS_EVENT_BUF_SIZE, ble_hs_hci_os_event_buf,
                         "HCIOsEventPool");
    assert(rc == 0);

    /* Initialize eventq */
    os_eventq_init(&ble_hs_evq);

    /* Initialize stats. */
    rc = stats_module_init();
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    host_hci_init();

    rc = ble_hs_conn_init();
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_init();
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_init();
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_svr_init();
    if (rc != 0) {
        goto err;
    }

    rc = ble_gap_init();
    if (rc != 0) {
        goto err;
    }

    rc = ble_hci_sched_init();
    if (rc != 0) {
        goto err;
    }

    rc = ble_gattc_init();
    if (rc != 0) {
        goto err;
    }

    rc = ble_gatts_init();
    if (rc != 0) {
        goto err;
    }

    ble_hs_kick_hci_ev.ev_queued = 0;
    ble_hs_kick_hci_ev.ev_type = BLE_HS_KICK_HCI_EVENT;
    ble_hs_kick_hci_ev.ev_arg = NULL;

    ble_hs_kick_gatt_ev.ev_queued = 0;
    ble_hs_kick_gatt_ev.ev_type = BLE_HS_KICK_GATT_EVENT;
    ble_hs_kick_gatt_ev.ev_arg = NULL;

    ble_hs_kick_l2cap_sig_ev.ev_queued = 0;
    ble_hs_kick_l2cap_sig_ev.ev_type = BLE_HS_KICK_L2CAP_SIG_EVENT;
    ble_hs_kick_l2cap_sig_ev.ev_arg = NULL;

    ble_hs_kick_l2cap_sm_ev.ev_queued = 0;
    ble_hs_kick_l2cap_sm_ev.ev_type = BLE_HS_KICK_L2CAP_SM_EVENT;
    ble_hs_kick_l2cap_sm_ev.ev_arg = NULL;

    os_mqueue_init(&ble_hs_rx_q, NULL);
    os_mqueue_init(&ble_hs_tx_q, NULL);

    rc = stats_init_and_reg(
        STATS_HDR(ble_hs_stats), STATS_SIZE_INIT_PARMS(ble_hs_stats,
        STATS_SIZE_32), STATS_NAME_INIT_PARMS(ble_hs_stats), "ble_hs");
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    return 0;

err:
    ble_hs_free_mem();
    return rc;
}
