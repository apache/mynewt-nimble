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
#include "host/ble_hs.h"
#include "lvgl.h"

void ret_to_main_scr(void);

uint16_t curr_conn_handle;
lv_obj_t *keyboard;
lv_obj_t *active_window;
lv_obj_t *svcs_list;
lv_obj_t *chrs_list;
lv_obj_t *rw_menu;
lv_obj_t *ret_btn;

uint16_t svc_start_handles[20];
uint16_t svc_end_handles[20];

struct ble_gatt_chr curr_chars[20];

static void
svcs_list_init(void)
{
    svcs_list = lv_obj_create(lv_scr_act());
    lv_obj_set_size(svcs_list, LV_PCT(100), LV_PCT(80));
    lv_obj_align(svcs_list, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_flex_flow(svcs_list, LV_FLEX_FLOW_COLUMN);
}

static void
chrs_list_init(void)
{
    chrs_list = lv_obj_create(lv_scr_act());
    lv_obj_set_size(chrs_list, LV_PCT(100), LV_PCT(80));
    lv_obj_align(chrs_list, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_flex_flow(chrs_list, LV_FLEX_FLOW_COLUMN);
}

static void
text_area_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(keyboard);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void
test_func(lv_event_t *e)
{
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void
rw_menu_init(void)
{
    lv_obj_t *obj;
    lv_obj_t *label;

    rw_menu = lv_obj_create(lv_scr_act());
    lv_obj_set_size(rw_menu, LV_PCT(100), LV_PCT(80));
    lv_obj_align(rw_menu, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_set_flex_flow(rw_menu, LV_FLEX_FLOW_ROW_WRAP);

    /* Value label */
    obj = lv_obj_create(rw_menu);
    lv_obj_set_size(obj, LV_PCT(70), 40);
    label = lv_label_create(obj);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_label_set_text(label, "Value: ");

    /* Read button */
    obj = lv_btn_create(rw_menu);
    lv_obj_set_height(obj, 40);
    lv_obj_set_flex_grow(obj, 1);
    label = lv_label_create(obj);
    lv_label_set_text(label, "Read");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    /* Write value text area  */
    obj = lv_textarea_create(rw_menu);
    lv_obj_set_size(obj, LV_PCT(60), 40);
    lv_textarea_set_accepted_chars(obj, "0123456789abcdefABCDEF");
    lv_textarea_set_max_length(obj, 8);
    lv_textarea_set_text(obj, "");
    lv_obj_add_event_cb(obj, text_area_cb, LV_EVENT_ALL, NULL);

    /* Keyboard */
    keyboard = lv_keyboard_create(lv_scr_act());
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(keyboard, obj);
    lv_obj_add_event_cb(keyboard, test_func, LV_EVENT_READY, NULL);

    /* Value type dropdown */
    obj = lv_btn_create(rw_menu);
    lv_obj_set_height(obj, 40);
    lv_obj_set_flex_grow(obj, 1);
    label = lv_label_create(obj);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(label, "Write");
}

static void
rw_menu_hide_write_fields(void)
{
    lv_obj_t *obj;

    /* Hide write text area */
    obj = lv_obj_get_child(rw_menu, 2);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);

    /* Hide write button */
    obj = lv_obj_get_child(rw_menu, 3);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void
rw_menu_hide_read_fields(void)
{
    lv_obj_t *obj;

    /* Hide read button */
    obj = lv_obj_get_child(rw_menu, 1);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static int read_char_val_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr, void *arg)
{
    lv_obj_t *obj;
    lv_obj_t *label;
    uint32_t val = 0;
    int i;

    if (error->status) {
        return error->status;
    }

    /* Set new value on the label */
    obj = lv_obj_get_child(rw_menu, 0);
    label = lv_obj_get_child(obj, 0);

    /* For now we can only read/write values of max length 4 bytes (FIXME) */
    if (attr->om->om_len > 4) {
        return 0;
    }

    for (i = 0; attr->om->om_len > i; i++) {
        val |= attr->om->om_data[i] << (8 * i);
    }

    lv_label_set_text_fmt(label, "Value: 0x%lx", val);


    return 0;
}

static void
read_chr_btn_cb(lv_event_t *e)
{
    struct ble_gatt_chr *chr;

    chr = e->user_data;

    ble_gattc_read(curr_conn_handle, chr->val_handle, read_char_val_cb, NULL);
}

static void
write_chr_btn_cb(lv_event_t *e)
{
    struct ble_gatt_chr *chr;
    lv_obj_t *obj;
    const char *txt;
    uint32_t val;
    uint8_t len;

    chr = e->user_data;

    /* Get value from text area */
    obj = lv_obj_get_child(rw_menu, 2);
    txt = lv_textarea_get_text(obj);
    if (strlen(txt) == 0) {
        return;
    }

    val = strtoul(txt, NULL, 16);
    len = (strlen(txt) + 1) / 2;

    ble_gattc_write_flat(curr_conn_handle, chr->val_handle, (void*) &val, len, NULL, NULL);
}

static void
open_read_write_menu(uint8_t chr_id, uint8_t chr_properties)
{
    lv_obj_t *obj;

    active_window = rw_menu;
    lv_obj_move_foreground(rw_menu);

    if (!(chr_properties & (BLE_GATT_CHR_PROP_WRITE_NO_RSP | BLE_GATT_CHR_PROP_WRITE))) {
        rw_menu_hide_write_fields();
    } else {
        /* Set read button callback */
        obj = lv_obj_get_child(rw_menu, 1);
        lv_obj_add_event_cb(obj, read_chr_btn_cb, LV_EVENT_PRESSED, &curr_chars[chr_id]);
    }

    if (!(chr_properties & BLE_GATT_CHR_PROP_READ)) {
        rw_menu_hide_read_fields();
    } else {
        /* Set write button calback */
        obj = lv_obj_get_child(rw_menu, 3);
        lv_obj_add_event_cb(obj, write_chr_btn_cb, LV_EVENT_PRESSED, &curr_chars[chr_id]);
    }

}

static void
chr_click_event_cb(lv_event_t *e)
{
    uint8_t id;

    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *chr = lv_event_get_target(e);
    id = lv_obj_get_index(chr);

    assert(id >= 0 && id < 20);

    if (code == LV_EVENT_CLICKED) {
        open_read_write_menu(id, curr_chars[id].properties);
    }
}

static void
add_new_chr(const struct ble_gatt_chr *characteristic)
{
    lv_obj_t *chr;
    lv_obj_t *label;
    uint8_t chr_id;

    char uuid_buf[BLE_UUID_STR_LEN];

    chr = lv_btn_create(chrs_list);
    chr_id = lv_obj_get_index(chr);

    curr_chars[chr_id] = *characteristic;

    lv_obj_add_event_cb(chr, chr_click_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_size(chr, LV_PCT(100), 40);

    label = lv_label_create(chr);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_text_fmt(label, "%s", ble_uuid_to_str((ble_uuid_t*) &characteristic->uuid, uuid_buf));


    label = lv_label_create(chr);
    lv_obj_align(label, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_label_set_text(label, "");


    if (characteristic->properties & BLE_GATT_CHR_PROP_READ) {
        lv_label_ins_text(label, LV_LABEL_POS_LAST, "R");
    }

    if ((characteristic->properties & BLE_GATT_CHR_PROP_WRITE) ||
        (characteristic->properties & BLE_GATT_CHR_PROP_WRITE_NO_RSP)) {
        lv_label_ins_text(label, LV_LABEL_POS_LAST, "W");
    }
}

static int
disc_chr_func(uint16_t conn_handle,
              const struct ble_gatt_error *error,
              const struct ble_gatt_chr *chr, void *arg)
{
    if (!error->status) {
        add_new_chr(chr);
    } else {
        printf("dsc chr err: %d\n", error->status);
    }

    return 0;
}


void
svc_click_event_cb(lv_event_t *e) {
    lv_obj_t *svc;
    uint8_t svc_id;

    active_window = chrs_list;
    lv_obj_move_foreground(chrs_list);
    svc = lv_event_get_target(e);
    svc_id = lv_obj_get_index(svc);
    lv_obj_clear_flag(ret_btn, LV_OBJ_FLAG_HIDDEN);
    ble_gattc_disc_all_chrs(curr_conn_handle, svc_start_handles[svc_id], svc_end_handles[svc_id], disc_chr_func, NULL);
}

static void
add_new_svc(const struct ble_gatt_svc *service)
{
    lv_obj_t *svc;
    lv_obj_t *label;
    uint8_t svc_id;

    char uuid_buf[BLE_UUID_STR_LEN];

    svc = lv_btn_create(svcs_list);
    lv_obj_add_event_cb(svc, svc_click_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_size(svc, LV_PCT(100), 40);
    svc_id = lv_obj_get_index(svc);

    svc_start_handles[svc_id] = service->start_handle;
    svc_end_handles[svc_id] = service->end_handle;

    label = lv_label_create(svc);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(90));
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text_fmt(label, "%s", ble_uuid_to_str((ble_uuid_t*) &service->uuid, uuid_buf));
}

static int
disc_svc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
              const struct ble_gatt_svc *service, void *arg)
{
    if (!error->status) {
        add_new_svc(service);
    } else {
        printf("dsc svc err: %d\n", error->status);
    }
    return 0;
}

static void
disc_btn_ev_cb(lv_event_t *e)
{
    int rc;
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        rc = ble_gap_terminate(curr_conn_handle, 0x13);
        if (rc) {
            ret_to_main_scr();
        }
    }
}

static void
disc_btn_init(void)
{
    lv_obj_t *btn;
    btn = lv_btn_create(lv_scr_act());

    lv_obj_set_size(btn, 100, 50);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 5, -5);
    lv_obj_add_event_cb(btn, disc_btn_ev_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "Disconnect");
    lv_obj_center(label);
}

static void
ret_btn_event_cb(lv_event_t *e)
{
    if (active_window == chrs_list) {
        lv_obj_add_flag(ret_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del(chrs_list);
        chrs_list_init();
        lv_obj_move_foreground(svcs_list);
    } else if (active_window == rw_menu) {
        active_window = chrs_list;
        lv_obj_del(rw_menu);
        rw_menu_init();
        lv_obj_move_foreground(chrs_list);
    }
}

static void
ret_btn_init(void)
{
    lv_obj_t *label;

    ret_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(ret_btn, 100, 50);
    lv_obj_align(ret_btn, LV_ALIGN_BOTTOM_RIGHT, -5, -5);
    lv_obj_add_event_cb(ret_btn, ret_btn_event_cb, LV_EVENT_CLICKED, NULL);

    label = lv_label_create(ret_btn);
    lv_label_set_text(label, "Return");
    lv_obj_center(label);
    lv_obj_add_flag(ret_btn, LV_OBJ_FLAG_HIDDEN);
}

static void
conn_scr_init(void)
{
    lv_obj_t *conn_scr;

    conn_scr = lv_obj_create(NULL);
    lv_scr_load(conn_scr);

    chrs_list_init();
    rw_menu_init();
    svcs_list_init();

    disc_btn_init();
    ret_btn_init();
}

static int
conn_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        conn_scr_init();
        curr_conn_handle = event->connect.conn_handle;
        ble_gattc_disc_all_svcs(curr_conn_handle, disc_svc_cb, NULL);
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ret_to_main_scr();
        break;
    }

    return 0;
}

void
connect_to_device(ble_addr_t *peer_addr, uint8_t own_addr_type)
{
    int rc;

    rc = ble_gap_connect(own_addr_type, peer_addr, 1000, NULL, conn_cb, NULL);

    if (rc) {
        printf("Connection error\n");
    }
}