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

#include "sysinit/sysinit.h"
#include "os/os.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "log/log.h"

#define SHELL_MODULE "pairing_shell"

static uint16_t conn_handle;
static const char *device_name = "Mynewt";
struct ble_gap_adv_params adv_params;
struct ble_hs_adv_fields fields, rsp_fields, parsed_fields;
static uint8_t own_addr_type;
/* predef_uuid stores information about UUID of device, that we connect to */
const uint8_t predef_uuid[16] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
};
ble_addr_t conn_addr;
struct ble_gap_conn_desc conn_desc;

static int advertise();

static void
ble_app_set_addr(void)
{
    int rc;

    /* define address variable */
    ble_addr_t addr;

    /* generate new random static address */
    rc = ble_hs_id_gen_rnd(0, &addr);
    assert(rc == 0);

    /* set generated address */
    rc = ble_hs_id_set_rnd(addr.val);
    assert(rc == 0);
}

static int
adv_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_ADV_COMPLETE:
        MODLOG_DFLT(INFO,"Advertising completed, termination code: %d\n",
                    event->adv_complete.reason);
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ble_gap_conn_find(event->connect.conn_handle, &conn_desc);
            MODLOG_DFLT(INFO,"Connection was established\n");
            MODLOG_DFLT(INFO,"Role: ");
            if (conn_desc.role == 0) {
                MODLOG_DFLT(INFO,"master\n");
            } else {
                MODLOG_DFLT(INFO,"slave\n");
            }
        } else if (event->connect.status == BLE_ERR_ACL_CONN_EXISTS) {
            MODLOG_DFLT(INFO, "Connection already exists\n");
            break;
        } else {
            MODLOG_DFLT(INFO,"Connection failed, error code: %i\n",
                            event->connect.status);
        }
        break;
    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        /* connected device requests update of connection parameters,
           and these are being filled in - NULL sets default values */
        MODLOG_DFLT(INFO, "updating conncetion parameters...\n");
        event->conn_update_req.conn_handle = conn_handle;
        event->conn_update_req.peer_params = NULL;
        MODLOG_DFLT(INFO, "connection parameters updated!\n");
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO, "disconnect; reason=%d\n",
                    event->disconnect.reason);
        /* reset conn_handle */
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        /* back to advertising */
        advertise();
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        MODLOG_DFLT(INFO,"Encryption state of connection's handle %d",
                    event->enc_change.conn_handle);
        if (event->enc_change.status == 0) {
            MODLOG_DFLT(INFO, "changed with success\n");
        } else {
            MODLOG_DFLT(INFO, "failed to change with error code: %d\n",
                        event->enc_change.status);
        }
        break;
    case BLE_GAP_EVENT_IDENTITY_RESOLVED:
        MODLOG_DFLT(INFO, "Identity resolved\n");
        break;
    default:
        MODLOG_DFLT(ERROR, "Advertising event not handled, "
                    "event code: %u\n", event->type);
        break;
    }
    return 0;
}

static int
set_adv_data(void)
{
    int rc;

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = BLE_UUID128(BLE_UUID128_DECLARE(
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff));
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.tx_pwr_lvl_is_present = 1;
    rsp_fields.name = (uint8_t *)device_name;
    rsp_fields.name_len = strlen(device_name);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    assert(rc == 0);

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    assert(rc == 0);

    MODLOG_DFLT(INFO, "Advertising data set\n");

    assert(rc == 0);
    return 0;
}

static int
advertise(void)
{
    set_adv_data();
    int rc;
    own_addr_type = BLE_OWN_ADDR_RPA_RANDOM_DEFAULT;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                               &adv_params, adv_event, NULL);

    assert(rc == 0);
    return 0;
}

static void
on_sync(void)
{
    /* Generate a non-resolvable private address. */
    ble_app_set_addr();

    MODLOG_DFLT(INFO,"Host and controller synced\n");
    advertise();
}

static void
on_reset(int reason)
{
    MODLOG_DFLT(INFO, "Resetting state; reason=%d\n", reason);
}

int
main(int argc, char **argv)
{
    /* Initialize all packages. */
    sysinit();

    /*fill all fields and parameters with zeros*/
    memset(&adv_params, 0, sizeof(adv_params));
    memset(&fields, 0, sizeof(fields));
    memset(&rsp_fields, 0, sizeof(rsp_fields));

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    /* As the last thing, process events from default event queue. */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
