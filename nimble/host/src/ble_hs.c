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
#ifdef ARCH_sim
#include "ble_hs_itf.h"
#endif

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
#define BLE_HOST_HCI_EVENT_CTLR_EVENT   (OS_EVENT_T_PERUSER)

int ble_host_listen_enabled;

int
ble_host_send_data_connectionless(uint16_t con_handle, uint16_t cid,
                                  uint8_t *data, uint16_t len)
{
    int rc;

#ifdef ARCH_sim
    rc = ble_host_sim_send_data_connectionless(con_handle, cid, data, len);
#else
    rc = -1;
#endif

    return rc;
}

/**
 * XXX: This is only here for testing.
 */
int 
ble_hs_poll(void)
{
    int rc;

#ifdef ARCH_sim
    rc = ble_host_sim_poll();
#else
    rc = -1;
#endif

    return rc;
}

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
            host_hci_event_proc(ev);
            break;
        default:
            assert(0);
            break;
        }
    }
}

/**
 * Initialize the host portion of the BLE stack.
 */
int
ble_hs_init(void)
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

#ifdef ARCH_sim
    if (ble_host_listen_enabled) {
        int rc;

        rc = ble_sim_listen(1);
        assert(rc == 0);
    }
#endif

    return 0;
}
