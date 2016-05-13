/**
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

#ifndef H_BLE_HS_
#define H_BLE_HS_

#include <inttypes.h>
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_test.h"
#include "host/ble_uuid.h"
#include "host/ble_store.h"
#include "host/host_hci.h"
struct os_eventq;
struct os_event;

#define BLE_HS_CONN_HANDLE_NONE     0xffff

#define BLE_HS_EAGAIN               1
#define BLE_HS_EALREADY             2
#define BLE_HS_EINVAL               3
#define BLE_HS_EMSGSIZE             4
#define BLE_HS_ENOENT               5
#define BLE_HS_ENOMEM               6
#define BLE_HS_ENOTCONN             7
#define BLE_HS_ENOTSUP              8
#define BLE_HS_EAPP                 9
#define BLE_HS_EBADDATA             10
#define BLE_HS_EOS                  11
#define BLE_HS_ECONGESTED           12
#define BLE_HS_ECONTROLLER          13
#define BLE_HS_ETIMEOUT             14
#define BLE_HS_EDONE                15
#define BLE_HS_EBUSY                16
#define BLE_HS_EREJECT              17
#define BLE_HS_EUNKNOWN             18
#define BLE_HS_EROLE                19

#define BLE_HS_ERR_ATT_BASE         0x100   /* 256 */
#define BLE_HS_ATT_ERR(x)           ((x) ? BLE_HS_ERR_ATT_BASE + (x) : 0)

#define BLE_HS_ERR_HCI_BASE         0x200   /* 512 */
#define BLE_HS_HCI_ERR(x)           ((x) ? BLE_HS_ERR_HCI_BASE + (x) : 0)

#define BLE_HS_ERR_L2C_BASE         0x300   /* 768 */
#define BLE_HS_L2C_ERR(x)           ((x) ? BLE_HS_ERR_L2C_BASE + (x) : 0)

#define BLE_HS_ERR_SM_US_BASE       0x400   /* 1024 */
#define BLE_HS_SM_US_ERR(x)         ((x) ? BLE_HS_ERR_SM_US_BASE + (x) : 0)

#define BLE_HS_ERR_SM_THEM_BASE     0x500   /* 1280 */
#define BLE_HS_SM_THEM_ERR(x)       ((x) ? BLE_HS_ERR_SM_THEM_BASE + (x) : 0)

/* defines the input output (io) capabilities for the host device */
#define BLE_HS_IO_DISPLAY_ONLY              0x00
#define BLE_HS_IO_DISPLAY_YESNO             0x01
#define BLE_HS_IO_KEYBOARD_ONLY             0x02
#define BLE_HS_IO_NO_INPUT_OUTPUT           0x03
#define BLE_HS_IO_KEYBOARD_DISPLAY          0x04

struct ble_hs_cfg {
    /** HCI settings. */
    uint8_t max_hci_bufs;

    /** Connection settings. */
    uint16_t max_outstanding_pkts_per_conn;
    uint8_t max_connections;
    uint8_t max_conn_update_entries;

    /** GATT server settings. */
    uint16_t max_services;
    uint16_t max_client_configs;

    /** GATT client settings. */
    uint8_t max_gattc_procs;

    /** ATT server settings. */
    uint16_t max_attrs;
    uint8_t max_prep_entries;

    /** L2CAP settings. */
    uint8_t max_l2cap_chans;
    uint8_t max_l2cap_sig_procs;
    uint8_t max_l2cap_sm_procs;

    /** Security manager settings. */
    uint8_t sm_io_cap;
    unsigned sm_oob_data_flag:1;
    unsigned sm_bonding:1;
    unsigned sm_mitm:1;
    unsigned sm_sc:1;
    unsigned sm_keypress:1;
    uint8_t sm_our_key_dist;
    uint8_t sm_their_key_dist;
    /** Store settings. */
    ble_store_read_fn *store_read_cb;
    ble_store_write_fn *store_write_cb;
    ble_store_delete_fn *store_delete_cb;
    /* privacy settings */
    uint8_t privacy_mode;
    #define BLE_HS_PRIVACY_MODE_NONE            (0)
    #define BLE_HS_PRIVACY_MODE_RANDOM_STATIC   (1)
    #define BLE_HS_PRIVACY_MODE_RESOLV_RAND     (2)
    uint16_t privacy_resolvable_addr_timeout;
};

extern const struct ble_hs_cfg ble_hs_cfg_dflt;

int ble_hs_start(void);
void ble_hs_event_enqueue(struct os_event *ev);
int ble_ibeacon_set_adv_data(void *uuid128, uint16_t major, uint16_t minor);
int ble_hs_init(struct os_eventq *app_evq, struct ble_hs_cfg *cfg);

#endif
