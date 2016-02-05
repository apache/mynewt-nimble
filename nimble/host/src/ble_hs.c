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

#include <assert.h>
#include <errno.h>
#include "bsp/bsp.h"
#include "util/tpq.h"
#include "os/os.h"
#include "nimble/hci_transport.h"
#include "host/host_hci.h"
#include "ble_gatt_priv.h"
#include "ble_hs_priv.h"
#include "ble_att_priv.h"
#include "ble_hs_conn.h"
#include "ble_hs_startup.h"
#include "ble_hci_ack.h"
#include "ble_hci_sched.h"
#include "ble_gap_priv.h"
#ifdef PHONY_TRANSPORT
#include "host/ble_hs_test.h"
#endif

#ifdef ARCH_sim
#define BLE_HS_STACK_SIZE   (1024)
#else
#define BLE_HS_STACK_SIZE   (256)
#endif

static struct os_task ble_hs_task;
static os_stack_t ble_hs_stack[BLE_HS_STACK_SIZE] bssnz_t;

#define HCI_CMD_BUFS        (8)
#define HCI_CMD_BUF_SIZE    (260)       /* XXX: temporary, Fix later */
struct os_mempool g_hci_cmd_pool;
static os_membuf_t g_hci_cmd_buf[OS_MEMPOOL_SIZE(HCI_CMD_BUFS,
                                                 HCI_CMD_BUF_SIZE)] bssnz_t;

/* XXX: this might be transport layer*/
#define HCI_NUM_OS_EVENTS       (32)
#define HCI_OS_EVENT_BUF_SIZE   (sizeof(struct os_event))

#define BLE_HS_NUM_MBUFS             (8)
#define BLE_HS_MBUF_BUF_SIZE         (256)
#define BLE_HS_MBUF_MEMBLOCK_SIZE                                \
    (BLE_HS_MBUF_BUF_SIZE + sizeof(struct os_mbuf) +             \
     sizeof(struct os_mbuf_pkthdr))

#define BLE_HS_MBUF_MEMPOOL_SIZE                                 \
    OS_MEMPOOL_SIZE(BLE_HS_NUM_MBUFS, BLE_HS_MBUF_MEMBLOCK_SIZE)

struct os_mempool g_hci_os_event_pool;
static os_membuf_t
    g_hci_os_event_buf[OS_MEMPOOL_SIZE(HCI_NUM_OS_EVENTS,
                                       HCI_OS_EVENT_BUF_SIZE)] bssnz_t;

static os_membuf_t ble_hs_mbuf_mem[BLE_HS_MBUF_MEMPOOL_SIZE];
static struct os_mempool ble_hs_mbuf_mempool;
struct os_mbuf_pool ble_hs_mbuf_pool;

/* Host HCI Task Events */
struct os_eventq ble_hs_evq;
static struct os_event ble_hs_kick_hci_ev;
static struct os_event ble_hs_kick_gatt_ev;
static struct os_event ble_hs_kick_l2cap_sig_ev;

static struct os_mqueue ble_hs_rx_q;
static struct os_mqueue ble_hs_tx_q;

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

        default:
            assert(0);
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
 * Initializes the host portion of the BLE stack.
 */
int
ble_hs_init(uint8_t prio)
{
    int rc;

    ble_hs_cfg_init();

    os_task_init(&ble_hs_task, "ble_hs", ble_hs_task_handler, NULL, prio,
                 OS_WAIT_FOREVER, ble_hs_stack, BLE_HS_STACK_SIZE);

    /* Create memory pool of command buffers */
    rc = os_mempool_init(&g_hci_cmd_pool, HCI_CMD_BUFS, HCI_CMD_BUF_SIZE,
                         &g_hci_cmd_buf, "HCICmdPool");
    assert(rc == 0);

    /* XXX: really only needed by the transport */
    /* Create memory pool of OS events */
    rc = os_mempool_init(&g_hci_os_event_pool, HCI_NUM_OS_EVENTS,
                         HCI_OS_EVENT_BUF_SIZE, &g_hci_os_event_buf,
                         "HCIOsEventPool");
    assert(rc == 0);

    /* Initialize eventq */
    os_eventq_init(&ble_hs_evq);

    host_hci_init();

    rc = os_mempool_init(&ble_hs_mbuf_mempool, BLE_HS_NUM_MBUFS,
                         BLE_HS_MBUF_MEMBLOCK_SIZE,
                         ble_hs_mbuf_mem, "ble_hs_mbuf_pool");
    if (rc != 0) {
        return BLE_HS_EOS;
    }
    rc = os_mbuf_pool_init(&ble_hs_mbuf_pool, &ble_hs_mbuf_mempool,
                           BLE_HS_MBUF_MEMBLOCK_SIZE, BLE_HS_NUM_MBUFS);
    if (rc != 0) {
        return BLE_HS_EOS;
    }

    rc = ble_hs_conn_init();
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_init();
    if (rc != 0) {
        return rc;
    }

    ble_att_init();

    rc = ble_att_svr_init();
    if (rc != 0) {
        return rc;
    }

    rc = ble_gap_init();
    if (rc != 0) {
        return rc;
    }

    ble_hci_ack_init();

    rc = ble_hci_sched_init();
    if (rc != 0) {
        return rc;
    }

    rc = ble_gattc_init();
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_init();
    if (rc != 0) {
        return rc;
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

    os_mqueue_init(&ble_hs_rx_q, NULL);
    os_mqueue_init(&ble_hs_tx_q, NULL);

    return 0;
}
