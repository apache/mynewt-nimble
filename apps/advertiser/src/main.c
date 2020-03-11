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

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "sysinit/sysinit.h"
#include "os/os.h"
#include "console/console.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "console/console.h"
#include "log/log.h"

static uint8_t g_own_addr_type;

static const char *device_name = "Apache Mynewt";

static void 
ble_app_set_addr(void)
{
    int rc;

    /*define address variable*/
    ble_addr_t addr;
    
    /*generate new non-resolvable private address*/
    rc = ble_hs_id_gen_rnd(1, &addr);
    assert(rc == 0);

    /*set generated address*/
    rc = ble_hs_id_set_rnd(addr.val);
    assert(rc == 0);
}

/*adv_event() calls advertise(), so forward declaration is required*/
static void advertise(void);

static int
adv_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        default:
            MODLOG_DFLT(ERROR, "Advertising event not handled\n");
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            MODLOG_DFLT(INFO,"Code of termination reason: %d\n",
                        event->adv_complete.reason);
            advertise();
            return 0;
    }
}

static void
advertise(void)
{
    int rc;

    /*set adv parameters*/
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
    adv_params.disc_mode =  BLE_GAP_DISC_MODE_GEN;

    memset(&fields, 0, sizeof(fields));

    /*Fill the fields with advertising data - flags, tx power level, name*/
    fields.flags = BLE_HS_ADV_F_DISC_GEN;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;
    
    rc = ble_gap_adv_set_fields(&fields);
    assert(rc == 0);
    
    MODLOG_DFLT(INFO,"Starting advertising...\n");
    /*performs discovery procedure*/
    rc = ble_gap_adv_start(g_own_addr_type, NULL, 10000,
                      &adv_params,adv_event, NULL);
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
    assert(rc == 0);
    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    assert(rc == 0);
    /*begin advertising*/
    advertise();
}

static void
on_reset(int reason)
{
    console_printf("Resetting state; reason=%d\n", reason);
}

int main(int argc, char **argv)
{
    int rc;

    /* Initialize all packages. */
    sysinit();

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    rc = ble_svc_gap_device_name_set(device_name);
    assert(rc == 0);

    /* As the last thing, process events from default event queue. */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
