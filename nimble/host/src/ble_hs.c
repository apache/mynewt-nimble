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
#include "os/os.h"
#include "host/host_hci.h"
#include "host/ble_hs.h"
#include "ble_hs_att.h"
#include "ble_hs_conn.h"
#include "ble_hs_ack.h"
#include "ble_hs_work.h"
#include "ble_gap_conn.h"

#ifdef ARCH_sim
#define BLE_HS_STACK_SIZE   (1024)
#else
#define BLE_HS_STACK_SIZE   (128)
#endif

struct os_task ble_hs_task;
os_stack_t ble_hs_stack[BLE_HS_STACK_SIZE];


#define HCI_CMD_BUFS        (8)
#define HCI_CMD_BUF_SIZE    (260)       /* XXX: temporary, Fix later */
struct os_mempool g_hci_cmd_pool;
os_membuf_t g_hci_cmd_buf[OS_MEMPOOL_SIZE(HCI_CMD_BUFS, HCI_CMD_BUF_SIZE)];

/* XXX: this might be transport layer*/
#define HCI_NUM_OS_EVENTS       (32)
#define HCI_OS_EVENT_BUF_SIZE   (sizeof(struct os_event))

struct os_mempool g_hci_os_event_pool;
os_membuf_t g_hci_os_event_buf[OS_MEMPOOL_SIZE(HCI_NUM_OS_EVENTS,
                                               HCI_OS_EVENT_BUF_SIZE)];

/* Host HCI Task Events */
struct os_eventq g_ble_host_hci_evq;
static struct os_event ble_hs_kick_ev;
#define BLE_HOST_HCI_EVENT_CTLR_EVENT   (OS_EVENT_T_PERUSER + 0)
#define BLE_HS_KICK_EVENT               (OS_EVENT_T_PERUSER + 1)

void
ble_hs_task_handler(void *arg)
{
    struct os_event *ev;
    struct os_callout_func *cf;

    while (1) {
        ev = os_eventq_get(&g_ble_host_hci_evq);
        switch (ev->ev_type) {
        case OS_EVENT_T_TIMER:
            cf = (struct os_callout_func *)ev;
            assert(cf->cf_func);
            cf->cf_func(cf->cf_arg);
            break;

        case BLE_HOST_HCI_EVENT_CTLR_EVENT:
            /* Process HCI event from controller */
            host_hci_os_event_proc(ev);
            break;

        case BLE_HS_KICK_EVENT:
            break;

        default:
            assert(0);
            break;
        }

        /* If a work event is not already in progress and there is another
         * event pending, begin processing it.
         */
        if (!ble_hs_work_busy) {
            ble_hs_work_process_next();
        }
    }
}

/**
 * Wakes the BLE host task so that it can process work events.
 */
void
ble_hs_kick(void)
{
    os_eventq_put(&g_ble_host_hci_evq, &ble_hs_kick_ev);
}

/**
 * Initializes the host portion of the BLE stack.
 */
int
ble_hs_init(uint8_t prio)
{
    int rc;

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
    os_eventq_init(&g_ble_host_hci_evq);

    host_hci_init();

    rc = ble_hs_conn_init();
    if (rc != 0) {
        return rc;
    }

    rc = ble_l2cap_init();
    if (rc != 0) {
        return rc;
    }

    rc = ble_hs_att_init();
    if (rc != 0) {
        return rc;
    }

    rc = ble_gap_conn_init();
    if (rc != 0) {
        return rc;
    }

    ble_hs_ack_init();

    rc = ble_hs_work_init();
    if (rc != 0) {
        return rc;
    }

    ble_hs_kick_ev.ev_queued = 0;
    ble_hs_kick_ev.ev_type = BLE_HS_KICK_EVENT;
    ble_hs_kick_ev.ev_arg = NULL;

    os_task_init(&ble_hs_task, "ble_hs", ble_hs_task_handler, NULL, prio,
                 OS_WAIT_FOREVER, ble_hs_stack, BLE_HS_STACK_SIZE);

    return 0;
}
