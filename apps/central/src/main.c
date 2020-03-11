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
static void scan(void);

/*connection has separate event handler from scan*/
static int
conn_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        default:
            MODLOG_DFLT(INFO,"Connection event type not supported\n");
            break;
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                MODLOG_DFLT(INFO,"Connection was fully established\n");
            } else {
                MODLOG_DFLT(INFO,"Connection failed, error code: %i\n",
                 event->connect.status);
            } 
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            MODLOG_DFLT(INFO,"Disconnected, reason code: %i\n",
            event->disconnect.reason);
    }
    return 0;
}

static int
scan_event(struct ble_gap_event *event, void *arg)
{
    struct ble_hs_adv_fields parsed_fields;
    memset(&parsed_fields, 0, sizeof(parsed_fields));

    /*predef_uuid stores information about UUID of device, that we connect to*/
    const uint8_t predef_uuid[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };

    switch (event->type) {
        default:
            MODLOG_DFLT(ERROR, "Discovery event not handled\n");
            break;
        /*advertising report has been received during discovery procedure*/   
        case BLE_GAP_EVENT_DISC:
            ble_hs_adv_parse_fields(&parsed_fields, event->disc.data,
                event->disc.length_data);
            MODLOG_DFLT(INFO, "Advertising report was received! Contents:\n");
            MODLOG_DFLT(INFO, " event type: %u\n",event->disc.event_type);
            MODLOG_DFLT(INFO, " data packet length: %u\n",
                        event->disc.length_data);
            MODLOG_DFLT(INFO, " advertiser address: %u\n",
                event->disc.addr.val);
            MODLOG_DFLT(INFO, " received signal RSSI: %i\n",
                event->disc.rssi);
            MODLOG_DFLT(INFO, " received data: %u\n",
                &(event->disc.data));

            MODLOG_DFLT(INFO, "UUID: ");

            /*Predefined UUID is compared to recieved one;
            if doesn't fit - end procedure and go back to scanning,
            else - connect*/
            for (int i = 0; i < sizeof(predef_uuid); i++) {                
                MODLOG_DFLT(INFO, "%d, ", parsed_fields.uuids128->value[i]);
                if (parsed_fields.uuids128->value[i] != predef_uuid[i]) {
                    MODLOG_DFLT(INFO, "doesn't fit\n");
                    return 0;
                }
            }
            MODLOG_DFLT(INFO, "\n UUID fits, connecting... \n");
            ble_gap_disc_cancel();
            ble_gap_connect(g_own_addr_type, &(event->disc.addr), 10000,
                NULL, conn_event, NULL);
            break;
        /*discovery procedure has terminated*/
        case BLE_GAP_EVENT_DISC_COMPLETE:
            MODLOG_DFLT(INFO,"Code of termination reason: %d\n",
                        event->disc_complete.reason);
            scan();
            break;
    }
    return 0;
}

static void
scan(void)
{
    int rc;

    /*set scan parameters: interval, window, filter policy, 
    limited discovery procedure, passive scan, duplicates filtering*/
    const struct ble_gap_disc_params scan_params = {10000, 200, 0, 0, 0, 1};

    /*initialize pointer to scan_params*/
    const struct ble_gap_disc_params *scan_params_p = &scan_params;
    /*performs discovery procedure*/
    rc = ble_gap_disc(g_own_addr_type, 1000, scan_params_p,scan_event, NULL);
    assert(rc == 0);

}



static void 
on_sync(void)
{
    int rc;
    /* Generate a non-resolvable private address. */
    ble_app_set_addr();
    
    /*g_own_addr_type will store type of addres our BSP uses*/

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
