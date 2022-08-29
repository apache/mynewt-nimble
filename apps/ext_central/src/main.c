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
#include "log/log.h"

/* Values to use with current_scan variable */
#define SCAN_FOR_1M1M   0
#define SCAN_FOR_1M2M   1
#define SCAN_FOR_CODED  2

/* Variable used to keep track on what kind of advertising we are currently scanning for */
static uint8_t current_scan;

static uint8_t g_own_addr_type;

static void ext_scan_uncoded(void);
static void ext_scan_coded(void);
static int conn_event(struct ble_gap_event *event, void *arg);

static void
resume_scan(void)
{
    if (current_scan == SCAN_FOR_CODED) {
        ext_scan_coded();
    } else {
        ext_scan_uncoded();
    }
}

static int
scan_event(struct ble_gap_event *event, void *arg)
{
    /* predef_uuid array stores information about UUID of instances,
     * that we connect to */
    const ble_uuid128_t predef_uuids[3] = {
        BLE_UUID128_INIT(0x2f, 0xce, 0xe2, 0x12, 0xb7, 0xa9, 0xdf, 0xaf,
                         0xab, 0x4b, 0x99, 0x70, 0xe0, 0x69, 0x65, 0xb7),
        BLE_UUID128_INIT(0x89, 0x53, 0xff, 0x58, 0x00, 0x10, 0x2f, 0xb2,
                         0xad, 0x4b, 0x9f, 0x35, 0xde, 0xac, 0x15, 0x79),
        BLE_UUID128_INIT(0x1d, 0x8e, 0x07, 0x24, 0xcb, 0x33, 0x0a, 0xa5,
                         0x89, 0x47, 0x1b, 0xef, 0x74, 0x51, 0x4e, 0xf7)
    };
    static struct ble_hs_adv_fields parsed_fields;

    int rc;

    int uuid_cmp_result;

    memset(&parsed_fields, 0, sizeof(parsed_fields));

    switch (event->type) {
    /* advertising report has been received during discovery procedure */
    case BLE_GAP_EVENT_EXT_DISC:
        if (event->ext_disc.data_status == BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE) {
            ble_hs_adv_parse_fields(&parsed_fields, event->ext_disc.data,
                                    event->ext_disc.length_data);

            if (current_scan != SCAN_FOR_1M1M &&
                current_scan != SCAN_FOR_1M2M &&
                current_scan != SCAN_FOR_CODED) {

                /* Wrong current_scan value */
                assert(0);
            }

            /* Predefined UUID is compared to received one;
             *  if doesn't fit - end procedure and go back to scanning,
             *  else - connect. */
            uuid_cmp_result = ble_uuid_cmp(&predef_uuids[current_scan].u, &parsed_fields.uuids128->u);

            if (uuid_cmp_result == 0) {
                MODLOG_DFLT(INFO, "UUID fits, primary phy: %d, secondary phy: %d connecting...\n",
                            event->ext_disc.prim_phy, event->ext_disc.sec_phy);
                rc = ble_gap_disc_cancel();

                MODLOG_DFLT(ERROR, "ble_gap_disc_cancel_rc = %d\n", rc);

                switch (current_scan) {
                case SCAN_FOR_1M1M:
                    rc = ble_gap_ext_connect(g_own_addr_type, &(event->ext_disc.addr), 10000,
                                             BLE_GAP_LE_PHY_1M_MASK,
                                             NULL, NULL, NULL, conn_event, NULL);
                    break;
                case SCAN_FOR_1M2M:
                    rc = ble_gap_ext_connect(g_own_addr_type, &(event->ext_disc.addr), 10000,
                                             BLE_GAP_LE_PHY_2M_MASK,
                                             NULL, NULL, NULL, conn_event, NULL);
                    break;
                case SCAN_FOR_CODED:
                    rc = ble_gap_ext_connect(g_own_addr_type, &(event->ext_disc.addr), 10000,
                                             BLE_GAP_LE_PHY_CODED_MASK,
                                             NULL, NULL, NULL, conn_event, NULL);
                    break;
                default:
                    assert(0);
                }

                MODLOG_DFLT(ERROR, "ble_gap_ext_connect_rc = %d\n", rc);
            }
        }
        return 0;

    /* discovery procedure has terminated */
    case BLE_GAP_EVENT_DISC_COMPLETE:
        MODLOG_DFLT(INFO, "Discovery completed, terminaton code: %d\n",
                    event->disc_complete.reason);

        MODLOG_DFLT(INFO, "Resuming scan\n");
        resume_scan();
        return 0;

    default:
        MODLOG_DFLT(ERROR, "Discovery event not handled\n");
        return 0;
    }
}


/* connection has separate event handler from scan */
static int
conn_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            MODLOG_DFLT(INFO,"Connection was established %d\n", current_scan);
            ble_gap_terminate(event->connect.conn_handle, 0x13);
        } else {
            MODLOG_DFLT(INFO,"Connection failed, error code: %i\n",
                        event->connect.status);
            resume_scan();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO,"Disconnected, reason code: %i\n",
                    event->disconnect.reason);
        if (current_scan == SCAN_FOR_CODED) {
            current_scan = SCAN_FOR_1M1M;
        } else {
            ++current_scan;
        }
        resume_scan();
        break;
    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        MODLOG_DFLT(INFO,"Connection update request received\n");
        break;
    case BLE_GAP_EVENT_CONN_UPDATE:
        if (event->conn_update.status == 0) {
            MODLOG_DFLT(INFO,"Connection update successful\n");
        } else {
            MODLOG_DFLT(INFO,"Connection update failed; reason: %d\n",
                        event->conn_update.status);
        }
        break;
    default:
        MODLOG_DFLT(INFO,"Connection event type not supported, %d\n",
                    event->type);
        resume_scan();
        break;
    }
    return 0;
}


/*
 * Function to start uncoded scan. Parameters:
 * - Duplicate packets are filtered
 * - Filter policy - white list not used
 * - Limited discovery procedure won't be used
 * - Passive scan will be performed
 * - Scanning interval is set to 500 * 0.625 ms
 * - Scanning window is set to 500 * 0.625 ms
 * - Scan interval and window are equal, so scanning will
 *   be continuous
 * */
static void
ext_scan_uncoded(void)
{
    struct ble_gap_ext_disc_params uncoded;
    uint8_t filter_duplicates = 1;
    uint8_t filter_policy = BLE_HCI_SCAN_FILT_NO_WL;
    uint8_t limited = 0;
    uncoded.passive = 1;
    uncoded.itvl = 500;
    uncoded.window = 500;

    ble_gap_ext_disc(g_own_addr_type, 1000, 0, filter_duplicates, filter_policy,
                     limited,&uncoded, NULL, scan_event, NULL);
}

/* Coded scan takes the same parameters as uncoded */
static void
ext_scan_coded(void)
{
    struct ble_gap_ext_disc_params coded;
    uint8_t filter_duplicates = 1;
    uint8_t filter_policy = BLE_HCI_SCAN_FILT_NO_WL;
    uint8_t limited = 0;
    coded.passive = 1;
    coded.itvl = 500;
    coded.window = 250;

    ble_gap_ext_disc(BLE_OWN_ADDR_RANDOM, 100, 0, filter_duplicates, filter_policy,
                     limited,NULL, &coded, scan_event, NULL);
}

static void
on_sync(void)
{
    int rc;

    MODLOG_DFLT(INFO, "Synced, starting scanning\n");

    /* Generate a non-resolvable private address. */
    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    /* configure global address */
    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    assert(rc == 0);

    /* begin scanning */
    ext_scan_uncoded();
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

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    /* As the last thing, process events from default event queue. */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
