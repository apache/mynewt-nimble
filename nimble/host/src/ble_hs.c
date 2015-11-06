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
#include "host/ble_hs.h"
#include "ble_hs_att.h"
#include "ble_hs_conn.h"
#ifdef ARCH_sim
#include "ble_hs_itf.h"
#endif


int ble_host_listen_enabled;

static struct os_eventq host_task_evq;
static struct os_callout ble_host_task_timer;

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
ble_host_poll(void)
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
ble_host_task_handler(void *arg)
{
    struct os_event *ev;

    os_callout_reset(&ble_host_task_timer, 50);

    /**
     * How do we decide what channels to listen on for data?  This must be 
     * configured to the host task.  Maintain a list of channels to 
     *
     */
    while (1) {
        ev = os_eventq_get(&host_task_evq);
        switch (ev->ev_type) {
            case OS_EVENT_T_TIMER:
                /* Poll the attribute channel */
                /* Reset callout, wakeup every 50ms */
                os_callout_reset(&ble_host_task_timer, 50);
                break;
        }
    }
}

/**
 * Initialize the host portion of the BLE stack.
 */
int
host_init(void)
{
    int rc;

    os_eventq_init(&host_task_evq);
    os_callout_init(&ble_host_task_timer, &host_task_evq, NULL);

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
