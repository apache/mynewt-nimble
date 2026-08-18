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
#include <errno.h>

#include "os/mynewt.h"
#include "console/console.h"
#include "config/config.h"
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

#define ADV_INSTANCE_HANDLE_1M1M 0
#define ADV_INSTANCE_HANDLE_1M2M 1
#define ADV_INSTANCE_HANDLE_CODED 2

/* Used to preserve info about connection status from connect event callback to adv complete event callback */
static int conn_status;

static uint8_t g_own_addr_type;

static struct {
    /* These are made up, predefined uuids for central to recognize each advertising instance */
    const ble_uuid128_t predef_uuid;
    /* Connection handles for each of the advertising instances */
    uint16_t conn_handle;
} conns[3] = {
    {
        .predef_uuid = BLE_UUID128_INIT(0x2f, 0xce, 0xe2, 0x12, 0xb7, 0xa9, 0xdf, 0xaf,
                                        0xab, 0x4b, 0x99, 0x70, 0xe0, 0x69, 0x65, 0xb7)
    }, {
        .predef_uuid = BLE_UUID128_INIT(0x89, 0x53, 0xff, 0x58, 0x00, 0x10, 0x2f, 0xb2,
                                        0xad, 0x4b, 0x9f, 0x35, 0xde, 0xac, 0x15, 0x79)
    }, {
        .predef_uuid = BLE_UUID128_INIT(0x1d, 0x8e, 0x07, 0x24, 0xcb, 0x33, 0x0a, 0xa5,
                                        0x89, 0x47, 0x1b, 0xef, 0x74, 0x51, 0x4e, 0xf7)
    }
};

/* ext_scan_event() calls these functions, so forward declaration is required */
static void
start_1m1m_ext_adv(void);
static void
start_1m2m_ext_adv(void);
static void
start_coded_ext_adv(void);


static int
ext_scan_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed */
        MODLOG_DFLT(INFO, "connection %s; status=%d\n",
                       event->connect.status == 0 ? "established" : "failed",
                       event->connect.status);

        /* Save connection status to check in next callback (from BLE_GAP_EVENT_ADV_COMPLETE event)
         * if the connection was established */
        conn_status = event->connect.status;
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (conn_status != 0) {
            /* Connection failed; resume advertising */
            switch (event->adv_complete.instance) {
            case ADV_INSTANCE_HANDLE_1M1M:
                conns[ADV_INSTANCE_HANDLE_1M1M].conn_handle = BLE_HS_CONN_HANDLE_NONE;
                start_1m1m_ext_adv();
                break;
            case ADV_INSTANCE_HANDLE_1M2M:
                conns[ADV_INSTANCE_HANDLE_1M2M].conn_handle = BLE_HS_CONN_HANDLE_NONE;
                start_1m2m_ext_adv();
                break;
            case ADV_INSTANCE_HANDLE_CODED:
                conns[ADV_INSTANCE_HANDLE_CODED].conn_handle = BLE_HS_CONN_HANDLE_NONE;
                start_coded_ext_adv();
                break;
            default:
                /* Wrong advertising instance */
                assert(0);
            }
        } else {
            /* Save current advertising instance conn_handle */
            if (event->adv_complete.instance != ADV_INSTANCE_HANDLE_1M1M &&
                event->adv_complete.instance != ADV_INSTANCE_HANDLE_1M2M &&
                event->adv_complete.instance != ADV_INSTANCE_HANDLE_CODED) {

                /* Wrong advertising instance */
                assert(0);
            }

            conns[event->adv_complete.instance].conn_handle = event->adv_complete.conn_handle;
        }
        MODLOG_DFLT(INFO, "adv complete %d\n", conns[event->adv_complete.instance].conn_handle);
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO, "disconnect; reason=%d\n", event->disconnect.reason);
        if (event->disconnect.conn.conn_handle == conns[ADV_INSTANCE_HANDLE_1M1M].conn_handle) {
            conns[ADV_INSTANCE_HANDLE_1M1M].conn_handle = BLE_HS_CONN_HANDLE_NONE;
            start_1m1m_ext_adv();
        } else if (event->disconnect.conn.conn_handle == conns[ADV_INSTANCE_HANDLE_1M2M].conn_handle) {
            conns[ADV_INSTANCE_HANDLE_1M2M].conn_handle = BLE_HS_CONN_HANDLE_NONE;
            start_1m2m_ext_adv();
        } else if (event->disconnect.conn.conn_handle == conns[ADV_INSTANCE_HANDLE_CODED].conn_handle) {
            conns[ADV_INSTANCE_HANDLE_CODED].conn_handle = BLE_HS_CONN_HANDLE_NONE;
            start_coded_ext_adv();
        }
        break;

    default:
        MODLOG_DFLT(ERROR, "Scan event not handled\n");
        break;
    }

    return 0;
}

static void
start_1m1m_ext_adv(void)
{
    struct ble_gap_ext_adv_params params;
    struct ble_hs_adv_fields adv_fields;
    struct os_mbuf *data;
    uint8_t instance = ADV_INSTANCE_HANDLE_1M1M;
    ble_addr_t addr;
    int rc;

    /* use defaults for non-set params */
    memset (&params, 0, sizeof(params));

    /* enable connectable advertising */
    params.connectable = 1;

    /* advertise using random addr */
    params.own_addr_type = BLE_OWN_ADDR_RANDOM;
    params.primary_phy = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_1M;
    params.tx_power = 127;
    params.sid = 1;

    /* configure instance 1 */
    rc = ble_gap_ext_adv_configure(instance, &params, NULL,
                                   ext_scan_event, NULL);
    assert (rc == 0);

    /* set random (NRPA) address for instance */
    rc = ble_hs_id_gen_rnd(1, &addr);
    assert (rc == 0);
    rc = ble_gap_ext_adv_set_addr(instance, &addr);
    assert (rc == 0);

    /* Adding uuid to adv_fields */
    memset(&adv_fields, 0, sizeof(adv_fields));
    adv_fields.num_uuids128 = 1;
    adv_fields.uuids128_is_complete = 1;
    adv_fields.uuids128 = &conns[ADV_INSTANCE_HANDLE_1M1M].predef_uuid;

    /* Attaching adv_fields with uuid to the advertising data */
    data = os_msys_get_pkthdr(BLE_HCI_MAX_ADV_DATA_LEN, 0);
    assert(data);
    rc = ble_hs_adv_set_fields_mbuf(&adv_fields, data);
    assert(rc == 0);
    rc = ble_gap_ext_adv_set_data(instance, data);
    assert(rc == 0);

    /* start advertising */
    rc = ble_gap_ext_adv_start(instance, 0, 0);
    assert (rc == 0);

    MODLOG_DFLT(INFO, "instance %u started (primary_phy 1M, secondary phy 1M)\n", instance);
}

static void
start_1m2m_ext_adv(void)
{
    struct ble_gap_ext_adv_params params;
    uint8_t instance = ADV_INSTANCE_HANDLE_1M2M;
    struct ble_hs_adv_fields adv_fields;
    struct os_mbuf *data;
    ble_addr_t addr;
    int rc;

    /* use defaults for non-set params */
    memset (&params, 0, sizeof(params));

    /* enable connectable advertising */
    params.connectable = 1;

    /* advertise using random addr */
    params.own_addr_type = BLE_OWN_ADDR_RANDOM;
    params.primary_phy = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_2M;
    params.tx_power = 127;
    params.sid = 2;

    /* configure instance 2 */
    rc = ble_gap_ext_adv_configure(instance, &params, NULL,
                                   ext_scan_event, NULL);
    assert (rc == 0);

    /* set random (NRPA) address for instance */
    rc = ble_hs_id_gen_rnd(1, &addr);
    assert (rc == 0);

    rc = ble_gap_ext_adv_set_addr(instance, &addr);
    assert (rc == 0);

    /* Adding uuid to adv_fields */
    memset(&adv_fields, 0, sizeof(adv_fields));
    adv_fields.num_uuids128 = 1;
    adv_fields.uuids128_is_complete = 1;
    adv_fields.uuids128 = &conns[ADV_INSTANCE_HANDLE_1M2M].predef_uuid;

    /* Attaching adv_fields with uuid to the advertising data */
    data = os_msys_get_pkthdr(BLE_HCI_MAX_ADV_DATA_LEN, 0);
    assert(data);
    rc = ble_hs_adv_set_fields_mbuf(&adv_fields, data);
    assert(rc == 0);
    rc = ble_gap_ext_adv_set_data(instance, data);
    assert(rc == 0);

    /* start advertising */
    rc = ble_gap_ext_adv_start(instance, 0, 0);
    assert (rc == 0);

    MODLOG_DFLT(INFO, "instance %u started (primary_phy 1M, secondary phy 2M)\n", instance);
}

static void
start_coded_ext_adv(void)
{
    struct ble_gap_ext_adv_params params;
    uint8_t instance = ADV_INSTANCE_HANDLE_CODED;
    struct ble_hs_adv_fields adv_fields;
    struct os_mbuf *data;
    ble_addr_t addr;
    int rc;

    /* use defaults for non-set params */
    memset (&params, 0, sizeof(params));

    /* enable connectable advertising */
    params.connectable = 1;

    /* advertise using random addr */
    params.own_addr_type = BLE_OWN_ADDR_RANDOM;
    params.primary_phy = BLE_HCI_LE_PHY_CODED;
    params.secondary_phy = BLE_HCI_LE_PHY_CODED;
    params.tx_power = 127;
    params.sid = 3;

    rc = ble_gap_ext_adv_configure(instance, &params, NULL,
                                   ext_scan_event, NULL);
    assert(rc == 0);

    /* set random (NRPA) address for instance */
    rc = ble_hs_id_gen_rnd(1, &addr);
    assert(rc == 0);
    rc = ble_gap_ext_adv_set_addr(instance, &addr);
    assert(rc == 0);

    /* Adding uuid to adv_fields */
    memset(&adv_fields, 0, sizeof(adv_fields));
    adv_fields.num_uuids128 = 1;
    adv_fields.uuids128_is_complete = 1;
    adv_fields.uuids128 = &conns[ADV_INSTANCE_HANDLE_CODED].predef_uuid;

    /* Attaching adv_fields with uuid to the advertising data */
    data = os_msys_get_pkthdr(BLE_HCI_MAX_ADV_DATA_LEN, 0);
    assert(data);
    rc = ble_hs_adv_set_fields_mbuf(&adv_fields, data);
    assert(rc == 0);
    rc = ble_gap_ext_adv_set_data(instance, data);
    assert(rc == 0);

    /* start advertising */
    rc = ble_gap_ext_adv_start(instance, 0, 0);
    assert (rc == 0);

    MODLOG_DFLT(INFO, "instance number %u started (coded)\n", instance);
}


static void
on_sync(void)
{
    int rc;

    MODLOG_DFLT(INFO, "Synced, starting advertising\n");

    /* Generate a non-resolvable private address. */
    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    /* configure global address */
    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    assert(rc == 0);

    start_1m1m_ext_adv();
    start_1m2m_ext_adv();
    start_coded_ext_adv();
}

/*
 * main
 *
 * The main task for the project. This function initializes the packages,
 * then starts serving events from default event queue.
 *
 * @return int NOTE: this function should never return!
 */
int
main(void)
{
    /* Initialize OS */
    sysinit();

    /* Set sync callback */
    ble_hs_cfg.sync_cb = on_sync;

    /* As the last thing, process events from default event queue */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }
    return 0;
}
