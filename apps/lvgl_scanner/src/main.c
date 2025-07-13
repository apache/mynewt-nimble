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
#include "lvgl.h"

#define MAX_DEVICES     MYNEWT_VAL(MAX_DEVICES)

int connect_to_device(ble_addr_t *peer_addr, uint8_t own_addr_type);

static uint8_t g_own_addr_type;
static lv_style_t style;
static lv_obj_t *entries_list;
static lv_obj_t *filters_list;
static lv_obj_t *main_scr;

struct device_data {
    int8_t last_rssi;
    ble_addr_t addr;
    lv_obj_t *list_entry;
};

struct filter_settings {
    int8_t rssi;
    uint8_t connectable_only;
};

static struct device_data devices[MAX_DEVICES];
static struct filter_settings filter_settings = {
    .rssi = -75,
    .connectable_only = 1
};

void ret_to_main_scr(void)
{
    lv_scr_load(main_scr);
}

static int
rssi_compare(const void *p1, const void *p2)
{
    const struct device_data *d1 = p1;
    const struct device_data *d2 = p2;

    return (d2->last_rssi - d1->last_rssi);
}

static void
sort_devices(void)
{
    uint8_t i;
    uint8_t n = lv_obj_get_child_cnt(entries_list);

    qsort(devices, n, sizeof(*devices), rssi_compare);

    for (i = 0; i < n; i++) {
        lv_obj_move_to_index(devices[i].list_entry, i);
    }
}

static int
get_device_id_by_addr(ble_addr_t *addr)
{
    uint8_t id;
    uint8_t n = lv_obj_get_child_cnt(entries_list);
    assert (n <= MAX_DEVICES);

    for (id = 0; id < n; id++) {
        if (!ble_addr_cmp(addr, &devices[id].addr)) {
            return id;
        }
    }
    return -1;
}

static void
fill_device_labels(struct ble_gap_ext_disc_desc *desc, uint8_t id)
{
    lv_obj_t *entry = devices[id].list_entry;
    lv_obj_t *label;
    struct ble_hs_adv_fields fields;
    char name_buf[255];

    ble_hs_adv_parse_fields(&fields, desc->data, desc->length_data);

    label = lv_obj_get_child(entry, 0);
    lv_label_set_text_fmt(label, "%02x:%02x:%02x:%02x:%02x:%02x (%u)", desc->addr.val[5],
                          desc->addr.val[4], desc->addr.val[3], desc->addr.val[2],
                          desc->addr.val[1], desc->addr.val[0], desc->addr.type);

    label = lv_obj_get_child(entry, 1);
    lv_label_set_text_fmt(label, "%ddB", desc->rssi);

    label = lv_obj_get_child(entry, 2);
    lv_label_set_text_fmt(label, "%s", desc->prim_phy == BLE_HCI_LE_PHY_1M ? "1M" : "Coded");
    switch (desc->sec_phy) {
    case BLE_HCI_LE_PHY_1M:
        lv_label_ins_text(label, LV_LABEL_POS_LAST, "/1M");
        break;
    case BLE_HCI_LE_PHY_2M:
        lv_label_ins_text(label, LV_LABEL_POS_LAST, "/2M");
        break;
    case BLE_HCI_LE_PHY_CODED:
        lv_label_ins_text(label, LV_LABEL_POS_LAST, "/Coded");
        break;
    default:
        break;
    }

    if (fields.name != NULL) {
        label = lv_obj_get_child(entry, 3);
        memcpy(name_buf, fields.name, fields.name_len);
        name_buf[fields.name_len] = '\0';
        lv_label_set_text_fmt(label, "%s", name_buf);
    }
}

static void
create_device_labels(uint8_t id)
{
    lv_obj_t *entry;
    lv_obj_t *label;

    entry = devices[id].list_entry;

    label = lv_label_create(entry);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    label = lv_label_create(entry);
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    label = lv_label_create(entry);
    lv_obj_align_to(label, lv_obj_get_child(entry, 1), LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    label = lv_label_create(entry);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_label_set_text(label, "");
}

static void
entry_click_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *entry = lv_event_get_target(e);

    uint8_t id = lv_obj_get_index(entry);

    if (code == LV_EVENT_CLICKED) {
        ble_gap_disc_cancel();
        connect_to_device(&devices[id].addr, g_own_addr_type);
    }
}

static void
add_new_device(struct ble_gap_ext_disc_desc *desc)
{
    uint8_t id;

    lv_obj_t *entry = lv_btn_create(entries_list);
    lv_obj_add_event_cb(entry, entry_click_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_size(entry, LV_PCT(100), 40);
    lv_obj_add_style(entry, &style, 0);

    id = lv_obj_get_child_cnt(entries_list) - 1;
    devices[id].list_entry = entry;
    devices[id].last_rssi = desc->rssi;
    memcpy(&devices[id].addr, &desc->addr, 7);

    create_device_labels(id);
    fill_device_labels(desc, id);
}

static void
update_device(struct ble_gap_ext_disc_desc *desc, uint8_t id)
{
    uint8_t rssi_change;
    fill_device_labels(desc, id);

    /* Sort the list if and update RSSI if it changed significantly */
    rssi_change = abs(devices[id].last_rssi - desc->rssi);
    if (rssi_change > 5) {
        devices[id].last_rssi = desc->rssi;
        sort_devices();
    }
}

static uint8_t
is_event_connectable(struct ble_gap_ext_disc_desc *desc)
{
    return desc->props & BLE_HCI_ADV_CONN_MASK;
}

static void
list_init(void)
{
    entries_list = lv_obj_create(lv_scr_act());
    lv_obj_set_size(entries_list, LV_PCT(100), LV_PCT(80));
    lv_obj_align(entries_list, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_flex_flow(entries_list, LV_FLEX_FLOW_COLUMN);
}

static void
handle_disc_report(struct ble_gap_ext_disc_desc *desc)
{
    int id;

    if (desc->rssi < filter_settings.rssi ||
        (filter_settings.connectable_only && !is_event_connectable(desc))) {
        return;
    }
    if (lv_obj_get_child_cnt(entries_list) >= MAX_DEVICES) {
        lv_obj_del(entries_list);
        list_init();
    }

    id = get_device_id_by_addr(&desc->addr);

    if (id < 0) {
        add_new_device(desc);
        printf("New device\n");
        sort_devices();
    } else {
        update_device(desc, id);
    }
}

static int
scan_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_EXT_DISC:
        handle_disc_report(&event->ext_disc);
        return 0;

    default:
        return 0;
    }
}

static void
start_scan(void)
{
    struct ble_gap_ext_disc_params coded;
    struct ble_gap_ext_disc_params uncoded;

    coded.passive = 0;
    coded.itvl = BLE_GAP_SCAN_ITVL_MS(500);
    coded.window = BLE_GAP_SCAN_WIN_MS(500);
    uncoded.passive = 0;
    uncoded.itvl = BLE_GAP_SCAN_ITVL_MS(500);
    uncoded.window = BLE_GAP_SCAN_WIN_MS(500);

    ble_gap_ext_disc(BLE_OWN_ADDR_RANDOM, 0, 0, 0, 0,
                     0, &uncoded, &coded, scan_event, NULL);
}

static void
ble_app_set_addr(void)
{
    ble_addr_t addr;
    int rc;

    /* Generate new non-resolvable private address */
    rc = ble_hs_id_gen_rnd(1, &addr);
    assert(rc == 0);

    /* Set generated address */
    rc = ble_hs_id_set_rnd(addr.val);
    assert(rc == 0);
}

static void
on_sync(void)
{
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    assert(rc == 0);
    ble_app_set_addr();
}

static void
on_reset(int reason)
{
    console_printf("Resetting state; reason=%d\n", reason);
}

static void
scan_btn_event_cb(lv_event_t *e)
{
    static uint8_t scanning = 0;
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);

    if (code == LV_EVENT_CLICKED) {
        if (scanning) {
            scanning = 0;
            lv_label_set_text_fmt(label, "Start\nscan");
            ble_gap_disc_cancel();
        } else {
            scanning = 1;
            lv_label_set_text_fmt(label, "Stop\nscan");
            lv_obj_del(entries_list);
            list_init();
            start_scan();
        }
    }
}

static void
filter_btn_event_cb(lv_event_t *e)
{
    static uint8_t filter_btn_active = 0;
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        if (filter_btn_active) {
            filter_btn_active = 0;
            lv_obj_move_background(filters_list);
        } else {
            filter_btn_active = 1;
            lv_obj_move_foreground(filters_list);
        }
    }
}

static void
rssi_dropdown_event_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    char buf[4];
    lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
    filter_settings.rssi = atoi(buf);
    printf("%d\n", filter_settings.rssi);
}

static void
conn_only_cb_event_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);

    if (lv_obj_get_state(cb) & LV_STATE_CHECKED) {
        filter_settings.connectable_only = 1;
    } else {
        filter_settings.connectable_only = 0;
    }

    printf("Conn only: %d\n", filter_settings.connectable_only);
}

static void
style_init(void)
{
    lv_style_init(&style);
    lv_style_set_pad_ver(&style, 0);
    lv_style_set_pad_hor(&style, 2);
}

static void
btns_init(void)
{
    lv_obj_t *btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn, 100, 50);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 5, -5);
    lv_obj_add_event_cb(btn, scan_btn_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "Start\nscan");
    lv_obj_center(label);

    btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn, 100, 50);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -5, -5);
    lv_obj_add_event_cb(btn, filter_btn_event_cb, LV_EVENT_ALL, NULL);

    label = lv_label_create(btn);
    lv_label_set_text(label, "Filter\noptions");
    lv_obj_center(label);
}

static void
filter_list_init(void)
{
    lv_obj_t *obj;

    filters_list = lv_obj_create(lv_scr_act());
    lv_obj_set_size(filters_list, LV_PCT(100), LV_PCT(80));
    lv_obj_align(filters_list, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_set_flex_flow(filters_list, LV_FLEX_FLOW_COLUMN);

    obj = lv_label_create(filters_list);
    lv_label_set_text(obj, "Filter options");

    obj = lv_label_create(filters_list);
    lv_label_set_text(obj, "Minimum RSSI:");

    obj = lv_dropdown_create(filters_list);
    lv_dropdown_set_options(obj, "-75 dB\n" "-70 dB\n" "-65 dB\n" "-60 dB\n"
                            "-55 dB\n" "-50 dB\n" "-45 dB\n" "-40 dB\n");
    lv_obj_add_event_cb(obj, rssi_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    obj = lv_checkbox_create(filters_list);
    lv_checkbox_set_text(obj, "Connectable only");
    lv_obj_add_state(obj, LV_STATE_CHECKED);
    lv_obj_add_event_cb(obj, conn_only_cb_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

}

static void
display_init(void)
{
    style_init();
    filter_list_init();
    list_init();
    btns_init();
    main_scr = lv_scr_act();
}

int
main(int argc, char **argv)
{
    /* Initialize all packages. */
    sysinit();

    display_init();

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    /* As the last thing, process events from default event queue. */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
