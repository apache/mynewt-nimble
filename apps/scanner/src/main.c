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
#include "sysinit/sysinit.h"
#include "os/os.h"
#include "console/console.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "log/log.h"

static uint8_t g_own_addr_type;

static void 
ble_app_set_addr(void)
{
    /*define address variable*/
    ble_addr_t addr;
    int rc;

    /*generate new non-resolvable private address*/
    rc = ble_hs_id_gen_rnd(1, &addr);
    assert(rc == 0);

    /*set generated address*/
    rc = ble_hs_id_set_rnd(addr.val);
    assert(rc == 0);
}

/*scan_event() calls scan(), so forward declaration is required*/
static void scan();

static int
scan_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type){
        default:
            MODLOG_DFLT(ERROR, "Discovery event not handled\n");
            return 0;
        /*advertising report has been received during discovery procedure*/   
        case BLE_GAP_EVENT_DISC:
            MODLOG_DFLT(INFO, "Advertising report was received! Contents:\n");
            MODLOG_DFLT(INFO, " event type: %u\n",event->disc.event_type);
            MODLOG_DFLT(INFO, " data packet length: %u\n",
                        event->disc.length_data);
            /*TODO: parse address to xx:xx:xx:xx format (string)*/
            MODLOG_DFLT(INFO, " advertiser address: %u\n",
                event->disc.addr.val);
            MODLOG_DFLT(INFO, " received signal RSSI: %i\n",
                event->disc.rssi);
            MODLOG_DFLT(INFO, " received data: %u\n",
                &(event->disc.data));
            return 0;
        /*discovery procedure has terminated*/
        case BLE_GAP_EVENT_DISC_COMPLETE:
            MODLOG_DFLT(INFO,"Code of termination reason: %d\n",
                        event->disc_complete.reason);
            scan();
            return 0;
    }
}

static void
scan()
{
    /*set scan parameters: interval, window, filter policy, 
    limited discovery procedure, passive scan, duplicates filtering*/
    const struct ble_gap_disc_params scan_params = {500, 16, 0, 0, 1, 0};
    /*initialize pointer to scan_params*/
    const struct ble_gap_disc_params *scan_params_p = &scan_params;
    /*performs discovery procedure*/
    ble_gap_disc(g_own_addr_type, 1000, scan_params_p,scan_event, NULL);
}



static void 
on_sync(void)
{
    /* Generate a non-resolvable private address. */
    ble_app_set_addr();
    
    /*g_own_addr_type will store type of addres our BSP uses*/
    int rc;
    rc = ble_hs_util_ensure_addr(0);
    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    assert(rc == 0);
    /*begin scanning*/
    scan();
}

static void
on_reset(int reason)
{
    console_printf("Resetting state; reason=%d\n", reason);
}

int main(int argc, char **argv)
{
    /* Initialize all packages. */
    sysinit();

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    /* As the last thing, process events from default event queue. */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
