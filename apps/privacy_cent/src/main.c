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
#include "os/os_cputime.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "controller/ble_ll_resolv.h"
#include "services/gap/ble_svc_gap.h"
#include "log/log.h"

#define SHELL_MODULE "pairing_shell"
#define GEN_TASK_PRIO       3
#define GEN_TASK_STACK_SZ   512

struct ble_gap_adv_params adv_params;
struct ble_hs_adv_fields fields, rsp_fields;
static uint8_t own_addr_type = BLE_OWN_ADDR_RPA_RANDOM_DEFAULT;
static ble_addr_t peer_addr;

static os_time_t rpa_timeout_ticks;

static int disconnect(uint16_t conn_handle);
static int discovery(void);
struct ble_gap_conn_desc conn_desc;
bool reconnection = 0;

static void
ble_app_set_addr(void)
{
    int rc;

    /*define address variable*/
    ble_addr_t addr;

    /*generate new non-resolvable private address*/
    rc = ble_hs_id_gen_rnd(0, &addr);
    assert(rc == 0);

    /*set generated address*/
    rc = ble_hs_id_set_rnd(addr.val);
    assert(rc == 0);
}

/* to wait for RPA timeout we must use timer callout */
static struct os_callout rpa_exp_timer;

/* connection has separate event handler from scan */
static int
gap_event(struct ble_gap_event *event, void *arg)
{
    int rc;

    /* predef_uuid stores information about UUID of device,
       that we connect to */
    const ble_uuid128_t predef_uuid =
        BLE_UUID128_INIT(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                         0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff);
    struct ble_hs_adv_fields parsed_fields;
    int uuid_cmp_result;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (!reconnection) {
            if (event->connect.status == 0) {
                MODLOG_DFLT(INFO,"Connection was established\n");
                MODLOG_DFLT(INFO,"Role: ");
                if (conn_desc.role == 0) {
                    MODLOG_DFLT(INFO,"master\n");
                } else {
                    MODLOG_DFLT(INFO,"slave\n");
                }
                rc = ble_gap_security_initiate(event->connect.conn_handle);
                if (rc == 0) {
                    MODLOG_DFLT(INFO, "Pairing initiated!\n");
                    break;
                } else if (rc == BLE_HS_ENOTCONN) {
                    MODLOG_DFLT(INFO, "There is no connection with this handle\n");
                } else if (BLE_HS_EALREADY) {
                    MODLOG_DFLT(INFO, "Devices already trying to pair\n");
                } else {
                    MODLOG_DFLT(ERROR, "Error occured during pairing\n");
                }
                if (!rc) {
                    MODLOG_DFLT(INFO, "Pairing finished with rc=0\n");
                }
            } else if (event->connect.status == BLE_ERR_ACL_CONN_EXISTS) {
                MODLOG_DFLT(INFO, "Connection already exists\n");
                break;
            } else {
                MODLOG_DFLT(INFO,"Connection failed, error code: %i\n",
                            event->connect.status);
            }
        } else {
            MODLOG_DFLT(INFO, "Reconection ");
            if (event->connect.status == 0) {
                MODLOG_DFLT(INFO, "successful\n");
            } else {
                MODLOG_DFLT(INFO, "failed with error code: %d\n",
                            event->connect.status);
                discovery();
            }
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO,"Disconnected, reason code: %d\n",
                    event->disconnect.reason);
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        MODLOG_DFLT(INFO,"Encryption state of connection's handle %d ",
                    event->enc_change.conn_handle);
        if (event->enc_change.status == 0) {
            MODLOG_DFLT(INFO, "changed with success\n");
        } else {
            MODLOG_DFLT(INFO, "failed to change state with error code: %d \n",
                        event->enc_change.status);
        }
        break;
    case BLE_GAP_EVENT_IDENTITY_RESOLVED:
        MODLOG_DFLT(INFO,"Identity resolved\n");
        /* Remember peer's address */
        ble_gap_conn_find(event->identity_resolved.conn_handle, &conn_desc);
        ble_gap_wl_set(&conn_desc.peer_id_addr, 1);
        /* Disconnect */
        disconnect(conn_desc.conn_handle);
        reconnection = true;
        break;
    case BLE_GAP_EVENT_DISC:
        if (!reconnection) {
            memset(&parsed_fields, 0, sizeof(parsed_fields));

            ble_hs_adv_parse_fields(&parsed_fields, event->disc.data,
                event->disc.length_data);
            MODLOG_DFLT(INFO, "Advertising report was received! Contents:\n");
            MODLOG_DFLT(INFO, " event type: %u\n",event->disc.event_type);
            MODLOG_DFLT(INFO, " data packet length: %u\n",
                        event->disc.length_data);
            MODLOG_DFLT(INFO, " advertiser address: %u\n",
                        event->disc.addr.val);
            peer_addr = event->disc.addr;
            MODLOG_DFLT(INFO, " received signal RSSI: %i\n",
                event->disc.rssi);
            MODLOG_DFLT(INFO, " received data: %u\n",
                        &(event->disc.data));
            /* Thi block of code executes if user chose to connect. Predefined
               UUID is compared to recieved one; if doesn't fit - end procedure and
               go back to scanning, else - connect. */
            uuid_cmp_result = ble_uuid_cmp(&predef_uuid.u,
                                           &parsed_fields.uuids128->u);

            if (uuid_cmp_result) {
                MODLOG_DFLT(INFO, "UUID doesn't fit\n");
            } else {
                MODLOG_DFLT(INFO, "UUID fits, connecting...\n");
                ble_gap_disc_cancel();
                ble_gap_connect(own_addr_type, &(event->disc.addr), 1000,
                                NULL, gap_event, NULL);
            }
        } else {
            ble_gap_disc_cancel();
            rc = ble_gap_connect(own_addr_type,
                                &(conn_desc.peer_id_addr),
                                1000, NULL, gap_event, NULL);
            if (rc) {
                MODLOG_DFLT(INFO, "failed to reconnect, rc = %d\n", rc);
            }
        }
        break;
    /* discovery procedure has terminated */
    case BLE_GAP_EVENT_DISC_COMPLETE:
        MODLOG_DFLT(INFO,"Code of termination reason: %d\n",
                    event->disc_complete.reason);
        break;
    default:
        MODLOG_DFLT(INFO,"GAP event type not supported; "
                    "event code: %d\n", event->type);
        break;
    }

    return 0;
}

/* Event callback for RPA timecout callout.
   Performs discovery with connection. */
static void
rpa_exp_timer_cb(struct os_event *ev)
{
    assert(ev != NULL);
    MODLOG_DFLT(INFO, "RPA expired, reconnecting to peer\n");

    const struct ble_gap_disc_params scan_params = {
        .itvl = 10000, .window = 2000, .filter_policy = 1,
        .limited = 0, .passive = 1, .filter_duplicates = 1
    };
    ble_gap_disc(own_addr_type, 1000, &scan_params,
                      gap_event, NULL);
}

static int
discovery(void)
{
    int rc;

    /* set scan parameters */
    const struct ble_gap_disc_params scan_params = {
        .itvl = 10000, .window = 2000, .filter_policy = 0,
        .limited = 0, .passive = 1, .filter_duplicates = 1
    };
    /* performs discovery procedure */
    rc = ble_gap_disc(own_addr_type, 1000, &scan_params,
                      gap_event, NULL);
    MODLOG_DFLT(INFO, "discovery finished with rc = %d\n", rc);
    assert(rc == 0);

    return 0;
}

static int
disconnect(uint16_t conn_handle)
{
    /* 0x13 value is host code for disconnect command */
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    MODLOG_DFLT(INFO, "Connection terminated\n");

    os_callout_init(&rpa_exp_timer, os_eventq_dflt_get(),
                    rpa_exp_timer_cb, NULL);
    os_time_ms_to_ticks(1000*MYNEWT_VAL(BLE_RPA_TIMEOUT), &rpa_timeout_ticks);
    os_callout_reset(&rpa_exp_timer, rpa_timeout_ticks);
    return 0;
}

static void
on_sync(void)
{
    int rc;
    /* Generate a non-resolvable private address. */
    ble_app_set_addr();

    /* g_own_addr_type will store type of addres our BSP uses */

    rc = ble_hs_util_ensure_addr(0);
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    assert(rc == 0);

    discovery();

    MODLOG_DFLT(INFO,"Host and controller synced\n");
}

static void
on_reset(int reason)
{
    MODLOG_DFLT(INFO, "Resetting state; reason=%d\n", reason);
}

int
main(void)
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
