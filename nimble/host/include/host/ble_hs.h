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

#ifndef H_BLE_HS_
#define H_BLE_HS_

#include <inttypes.h>
#include "nimble/hci_common.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_hs_hci.h"
#include "host/ble_hs_log.h"
#include "host/ble_hs_test.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_ibeacon.h"
#include "host/ble_l2cap.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#ifdef __cplusplus
extern "C" {
#endif

struct os_eventq;
struct os_event;

#define BLE_HS_FOREVER              INT32_MAX

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
#define BLE_HS_ECONTROLLER          12
#define BLE_HS_ETIMEOUT             13
#define BLE_HS_EDONE                14
#define BLE_HS_EBUSY                15
#define BLE_HS_EREJECT              16
#define BLE_HS_EUNKNOWN             17
#define BLE_HS_EROLE                18
#define BLE_HS_ETIMEOUT_HCI         19
#define BLE_HS_ENOMEM_EVT           20
#define BLE_HS_ENOADDR              21
#define BLE_HS_ENOTSYNCED           22
#define BLE_HS_EAUTHEN              23
#define BLE_HS_EAUTHOR              24
#define BLE_HS_EENCRYPT             25
#define BLE_HS_EENCRYPT_KEY_SZ      26
#define BLE_HS_ESTORE_CAP           27
#define BLE_HS_ESTORE_FAIL          28

#define BLE_HS_ERR_ATT_BASE         0x100   /* 256 */
#define BLE_HS_ATT_ERR(x)           ((x) ? BLE_HS_ERR_ATT_BASE + (x) : 0)

#define BLE_HS_ERR_HCI_BASE         0x200   /* 512 */
#define BLE_HS_HCI_ERR(x)           ((x) ? BLE_HS_ERR_HCI_BASE + (x) : 0)

#define BLE_HS_ERR_L2C_BASE         0x300   /* 768 */
#define BLE_HS_L2C_ERR(x)           ((x) ? BLE_HS_ERR_L2C_BASE + (x) : 0)

#define BLE_HS_ERR_SM_US_BASE       0x400   /* 1024 */
#define BLE_HS_SM_US_ERR(x)         ((x) ? BLE_HS_ERR_SM_US_BASE + (x) : 0)

#define BLE_HS_ERR_SM_PEER_BASE     0x500   /* 1280 */
#define BLE_HS_SM_PEER_ERR(x)       ((x) ? BLE_HS_ERR_SM_PEER_BASE + (x) : 0)

/* Note: A hardware error of 0 is not success. */
#define BLE_HS_ERR_HW_BASE          0x600   /* 1536 */
#define BLE_HS_HW_ERR(x)            (BLE_HS_ERR_HW_BASE + (x))

/* Defines the IO capabilities for the local device. */
#define BLE_HS_IO_DISPLAY_ONLY              0x00
#define BLE_HS_IO_DISPLAY_YESNO             0x01
#define BLE_HS_IO_KEYBOARD_ONLY             0x02
#define BLE_HS_IO_NO_INPUT_OUTPUT           0x03
#define BLE_HS_IO_KEYBOARD_DISPLAY          0x04

typedef void ble_hs_reset_fn(int reason);
typedef void ble_hs_sync_fn(void);

struct ble_hs_cfg {
    /*** GATT server settings. */
    /**
     * An optional callback that gets executed upon registration of each GATT
     * resource (service, characteristic, or descriptor).
     */
    ble_gatt_register_fn *gatts_register_cb;

    /**
     * An optional argument that gets passed to the GATT registration
     * callback.
     */
    void *gatts_register_arg;

    /***
     * Security manager settings.  The only reason these are configurable at
     * runtime is to simplify security testing.
     */
    uint8_t sm_io_cap;
    unsigned sm_oob_data_flag:1;
    unsigned sm_bonding:1;
    unsigned sm_mitm:1;
    unsigned sm_sc:1;
    unsigned sm_keypress:1;
    uint8_t sm_our_key_dist;
    uint8_t sm_their_key_dist;

    /*** HCI settings */
    /**
     * This callback is executed when the host resets itself and the controller
     * due to fatal error.
     */
    ble_hs_reset_fn *reset_cb;

    /**
     * This callback is executed when the host and controller become synced.
     * This happens at startup and after a reset.
     */
    ble_hs_sync_fn *sync_cb;

    /*** Store settings. */
    /**
     * These function callbacks handle persistence of sercurity material
     * (bonding).
     * XXX: These need to go away.  Instead, the nimble host package should
     * require the host-store API (not yet implemented)..
     */
    ble_store_read_fn *store_read_cb;
    ble_store_write_fn *store_write_cb;
    ble_store_delete_fn *store_delete_cb;

    /**
     * This callback gets executed when a persistence operation cannot be
     * performed or a persistence failure is imminent.  For example, if is
     * insufficient storage capacity for a record to be persisted, this
     * function gets called to give the application the opportunity to make
     * room.
     */
    ble_store_status_fn *store_status_cb;
    void *store_status_arg;
};

extern struct ble_hs_cfg ble_hs_cfg;

int ble_hs_synced(void);
int ble_hs_start(void);
void ble_hs_sched_reset(int reason);
void ble_hs_evq_set(struct os_eventq *evq);
void ble_hs_init(void);

#ifdef __cplusplus
}
#endif

#endif
