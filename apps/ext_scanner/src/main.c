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

#define LEGACY_SCAN     0
#define UNCODED_SCAN    1
#define CODED_SCAN      2
#define BOTH_SCAN       3

uint8_t current_scan_mode = LEGACY_SCAN;

static void legacy_scan(void);
static void ext_scan_uncoded(void);
static void ext_scan_coded(void);
static void ext_scan_both(void);

static void
ble_app_set_addr(void)
{
    ble_addr_t addr;
    int rc;

    /* generate new non-resolvable private address */
    rc = ble_hs_id_gen_rnd(1, &addr);
    assert(rc == 0);

    /* set generated address */
    rc = ble_hs_id_set_rnd(addr.val);
    assert(rc == 0);
}

void
print_addr(const void *addr)
{
    const uint8_t *u8p;

    u8p = addr;
    console_printf("%02x:%02x:%02x:%02x:%02x:%02x",
                   u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);
}

void
print_bytes(const uint8_t *bytes, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        console_printf("%s0x%02x", i != 0 ? ":" : "", bytes[i]);
    }
}

static void
print_adv_fields(const struct ble_hs_adv_fields *fields)
{
    /*
     * There is much more advertising fields possible to print. However, in order to
     * not take too much time for printing interrupts, only flags and name are printed.
     * */
    if (fields->flags != 0) {
        console_printf("    flags=0x%02x:\n", fields->flags);

        if (!(fields->flags & BLE_HS_ADV_F_DISC_LTD) &&
            !(fields->flags & BLE_HS_ADV_F_DISC_GEN)) {
            console_printf("        Non-discoverable mode\n");
        }

        if (fields->flags & BLE_HS_ADV_F_DISC_LTD) {
            console_printf("        Limited discoverable mode\n");
        }

        if (fields->flags & BLE_HS_ADV_F_DISC_GEN) {
            console_printf("        General discoverable mode\n");
        }

        if (fields->flags & BLE_HS_ADV_F_BREDR_UNSUP) {
            console_printf("        BR/EDR not supported\n");
        }
    }

    if (fields->name != NULL) {
        console_printf("    name(%scomplete)=",
                       fields->name_is_complete ? "" : "in");
        console_write((char *)fields->name, fields->name_len);
        console_printf("\n");
    }
}

static void
decode_adv_data(const uint8_t *adv_data, uint8_t adv_data_len)
{
    struct ble_hs_adv_fields fields;

    console_printf(" data_length=%d data=", adv_data_len);


    print_bytes(adv_data, adv_data_len);

    console_printf(" fields:\n");
    ble_hs_adv_parse_fields(&fields, adv_data, adv_data_len);
    print_adv_fields(&fields);
}

static void
decode_event_type(struct ble_gap_ext_disc_desc *desc)
{
    uint8_t directed = 0;

    if (desc->props & BLE_HCI_ADV_LEGACY_MASK) {
        console_printf("Legacy PDU type %d", desc->legacy_event_type);

        if (desc->legacy_event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
            directed = 1;
        }

        goto common_data;
    }

    console_printf("Extended adv: ");
    if (desc->props & BLE_HCI_ADV_CONN_MASK) {
        console_printf("'conn' ");
    }
    if (desc->props & BLE_HCI_ADV_SCAN_MASK) {
        console_printf("'scan' ");
    }
    if (desc->props & BLE_HCI_ADV_DIRECT_MASK) {
        console_printf("'dir' ");
        directed = 1;
    }

    if (desc->props & BLE_HCI_ADV_SCAN_RSP_MASK) {
        console_printf("'scan rsp' ");
    }

    switch (desc->data_status) {
    case BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE:
        console_printf("complete");
        break;
    case BLE_GAP_EXT_ADV_DATA_STATUS_INCOMPLETE:
        console_printf("incomplete");
        break;
    case BLE_GAP_EXT_ADV_DATA_STATUS_TRUNCATED:
        console_printf("truncated");
        break;
    default:
        console_printf("reserved %d", desc->data_status);
        break;
    }

common_data:
    console_printf(" rssi=%d txpower=%d, pphy=%d, sphy=%d, sid=%d,"
    " periodic_adv_itvl=%u, addr_type=%d addr=",
    desc->rssi, desc->tx_power, desc->prim_phy, desc->sec_phy,
    desc->sid, desc->periodic_adv_itvl, desc->addr.type);
    print_addr(desc->addr.val);

    if (directed) {
        console_printf(" init_addr_type=%d inita=", desc->direct_addr.type);
        print_addr(desc->direct_addr.val);
    }

    console_printf("\n");

    if (!desc->length_data) {
        return;
    }

    decode_adv_data(desc->data, desc->length_data);
}

static int
scan_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    /* advertising report has been received during discovery procedure */
    case BLE_GAP_EVENT_EXT_DISC:
        switch (current_scan_mode) {
        case 0:
            console_printf("Legacy scan advertising report received!\n");
            break;
        case 1:
            console_printf("Uncoded scan advertising report received!\n");
            break;
        case 2:
            console_printf("Coded scan advertising report received!\n");
            break;
        case 3:
            console_printf("Uncoded/coded scan advertising report received!\n");
            break;
        }
        decode_event_type(&event->ext_disc);
        return 0;

    /* discovery procedure has terminated */
    case BLE_GAP_EVENT_DISC_COMPLETE:
        MODLOG_DFLT(INFO, "Discovery completed, terminaton code: %d\n",
                    event->disc_complete.reason);

        switch (current_scan_mode) {
        case BOTH_SCAN:
            console_printf("START LEGACY SCAN\n");
            legacy_scan();
            return 0;
        case LEGACY_SCAN:
            console_printf("START UNCODED SCAN\n");
            ext_scan_uncoded();
            return 0;
        case UNCODED_SCAN:
            console_printf("START CODED SCAN\n");
            ext_scan_coded();
            return 0;
        case CODED_SCAN:
            console_printf("START BOTH SCAN\n");
            ext_scan_both();
            return 0;
        }
    default:

        MODLOG_DFLT(ERROR, "Discovery event not handled\n");
        return 0;
    }
}

static void
legacy_scan(void)
{
    /* set scan parameters */
    struct ble_gap_disc_params scan_params;
    scan_params.itvl = 500;
    scan_params.window = 250;
    scan_params.filter_policy = 0;
    scan_params.limited = 0;
    scan_params.passive = 1;
    scan_params.filter_duplicates = 1;

    current_scan_mode = LEGACY_SCAN;
    /* performs discovery procedure; value of own_addr_type is hard-coded,
       because NRPA is used */
    ble_gap_disc(BLE_OWN_ADDR_RANDOM, 1000, &scan_params, scan_event, NULL);
}

static void
ext_scan_uncoded(void)
{
    struct ble_gap_ext_disc_params uncoded;
    uint8_t filter_duplicates = 1;
    uint8_t filter_policy = 0;
    uint8_t limited = 0;
    uncoded.passive = 1;
    uncoded.itvl = 500;
    uncoded.window = 250;

    current_scan_mode = UNCODED_SCAN;
    ble_gap_ext_disc(BLE_OWN_ADDR_RANDOM, 100, 0, filter_duplicates, filter_policy,
                     limited,&uncoded, NULL, scan_event, NULL);
}

static void
ext_scan_coded(void)
{
    struct ble_gap_ext_disc_params coded;
    uint8_t filter_duplicates = 1;
    uint8_t filter_policy = 0;
    uint8_t limited = 0;
    coded.passive = 1;
    coded.itvl = 500;
    coded.window = 250;

    current_scan_mode = CODED_SCAN;
    ble_gap_ext_disc(BLE_OWN_ADDR_RANDOM, 100, 0, filter_duplicates, filter_policy,
                     limited,NULL, &coded, scan_event, NULL);
}

static void
ext_scan_both(void)
{
    struct ble_gap_ext_disc_params coded;
    struct ble_gap_ext_disc_params uncoded;
    uint8_t filter_duplicates = 1;
    uint8_t filter_policy = 0;
    uint8_t limited = 0;
    coded.passive = 1;
    coded.itvl = 500;
    coded.window = 250;
    uncoded.passive = 1;
    uncoded.itvl = 500;
    uncoded.window = 250;

    current_scan_mode = BOTH_SCAN;
    ble_gap_ext_disc(BLE_OWN_ADDR_RANDOM, 100, 0, filter_duplicates, filter_policy,
                     limited,&uncoded, &coded, scan_event, NULL);
}

static void
on_sync(void)
{
    /* Generate a non-resolvable private address. */
    ble_app_set_addr();

    /* begin scanning */
    legacy_scan();
}

static void
on_reset(int reason)
{
    console_printf("Resetting state; reason=%d\n", reason);
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
