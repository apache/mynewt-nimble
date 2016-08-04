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
#include "nimble/hci_common.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_hs_log.h"
#include "host/ble_hs_test.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/host_hci.h"
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

/* Defines the IO capabilities for the local device. */
#define BLE_HS_IO_DISPLAY_ONLY              0x00
#define BLE_HS_IO_DISPLAY_YESNO             0x01
#define BLE_HS_IO_KEYBOARD_ONLY             0x02
#define BLE_HS_IO_NO_INPUT_OUTPUT           0x03
#define BLE_HS_IO_KEYBOARD_DISPLAY          0x04

typedef void ble_hs_reset_fn(int reason);
typedef void ble_hs_sync_fn(void);

struct ble_hs_cfg {
    /**
     * An HCI buffer is a "flat" 260-byte buffer.  HCI buffers are used by the
     * controller to send unsolicited events to the host.
     *
     * HCI buffers can get tied up when the controller sends lots of
     * asynchronous / unsolicited events (i.e., non-acks).  When the controller
     * needs to send one of these events, it allocates an HCI buffer, fills it
     * with the event payload, and puts it on a host queue.  If the controller
     * sends a quick burst of these events, the buffer pool may be exhausted,
     * preventing the host from sending an HCI command to the controller.
     *
     * Every time the controller sends a non-ack HCI event to the host, it also
     * allocates an OS event (it is unfortunate that these are both called
     * "events").  The OS event is put on the host-parent-task's event queue;
     * it is what wakes up the host-parent-task and indicates that an HCI event
     * needs to be processsed.  The pool of OS events is allocated with the
     * same number of elements as the HCI buffer pool.
     */
    /* XXX: This should either be renamed to indicate it is only used for OS
     * events, or it should go away entirely (copy the number from the
     * transport's config).
     */
    uint8_t max_hci_bufs;

    /*** Connection settings. */
    /**
     * The maximum number of concurrent connections.  This is set
     * automatically according to the build-time option
     * NIMBLE_OPT_MAX_CONNECTIONS.
     */
    uint8_t max_connections;

    /*** GATT server settings. */
    /**
     * These are acquired at service registration time and never freed.  You
     * need one of these for every service that you register.
     */
    uint16_t max_services;

    /**
     * The total number of in-RAM client characteristic configuration
     * descriptors (CCCDs).  One of these is consumed each time a peer
     * subscribes to notifications or indications for a characteristic that
     * your device serves.  In addition, at service registration time, the host
     * uses one of these for each characteristic that supports notifications or
     * indications.  So, the formula which guarantees no resource exhaustion
     * is:
     *     (num-subscribable-characteristics) * (max-connections + 1)
     */
    uint16_t max_client_configs;

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

    /*** GATT client settings. */
    /**
     * The maximum number of concurrent GATT client procedures.  When you
     * initiate a GATT procedure (e.g., read a characteristic, discover
     * services, etc.), one of these is consumed.  The resource is freed when
     * the procedure completes.
     */
    uint8_t max_gattc_procs;

    /*** ATT server settings. */
    /**
     * The total number of local ATT attributes.  Attributes are consumed at
     * service registration time and are never freed.  Attributes are used by
     * GATT server entities: services, characteristics, and descriptors
     * according to the following formula:
     *     (num-services + (num-characteristics * 2) + num-descriptors)
     *
     * Every characteristic that supports indications or notifications
     * automatically gets a descriptor.  All other descriptors are specified by
     * the application at service registration time.
     */
    uint16_t max_attrs;

    /**
     * A GATT server uses these when a peer performs a "write long
     * characteristic values" or "write long characteristic descriptors"
     * procedure.  One of these resources is consumed each time a peer sends a
     * partial write.  These procedures are not used often.
     */
    uint8_t max_prep_entries;

    /*** L2CAP settings. */
    /**
     * Each connection requires three L2CAP channels (signal, ATT, and security
     * manager).  In addition, the nimble host may allow channels to be created
     * "on the fly" (connection-oriented channels).  This functionality is not
     * available at the moment, so a safe formula to use is:
     *     (max-connections * 3)
     */
    uint8_t max_l2cap_chans;

    /**
     * The maximum number of concurrent L2CAP signalling procedures.  Only one
     * L2CAP signalling procedure is supported: slave-initiated connection
     * update.  You will never need more of these than the max number of
     * connections.
     */
    uint8_t max_l2cap_sig_procs;

    /**
     * The maximum number of concurrent security manager procedures.  Security
     * manager procedures include pairing and restoration of a bonded link.
     */
    uint8_t max_l2cap_sm_procs;

    /*** Security manager settings. */
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
     */
    ble_store_read_fn *store_read_cb;
    ble_store_write_fn *store_write_cb;
    ble_store_delete_fn *store_delete_cb;

    /*** Privacy settings. */
    /**
     * The frequency at which new resovlable private addresses are generated.
     * Units are seconds.
     */
    uint16_t rpa_timeout;
};

extern const struct ble_hs_cfg ble_hs_cfg_dflt;

int ble_hs_synced(void);
int ble_hs_start(void);
int ble_hs_init(struct os_eventq *app_evq, struct ble_hs_cfg *cfg);

#endif
