/*  Bluetooth Mesh */

/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <errno.h>

#include "os/os_mbuf.h"
#include "mesh/mesh.h"

#include "syscfg/syscfg.h"
#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG))
#include "host/ble_hs_log.h"

#include "adv.h"
#include "prov.h"
#include "net.h"
#include "beacon.h"
#include "lpn.h"
#include "friend.h"
#include "transport.h"
#include "access.h"
#include "foundation.h"
#include "proxy.h"
#include "mesh_priv.h"

u8_t g_mesh_addr_type;
static bool provisioned;

int
bt_mesh_provision(const u8_t net_key[16], u16_t net_idx, u8_t flags,
                  u32_t iv_index, u32_t seq, u16_t addr, const u8_t dev_key[16])
{
    int err;

    BLE_HS_LOG(INFO, "Primary Element: 0x%04x", addr);

    if ((MYNEWT_VAL(BLE_MESH_PB_GATT))) {
        bt_mesh_proxy_prov_disable();
    }

    err = bt_mesh_net_create(net_idx, flags, net_key, iv_index);
    if (err) {
        if ((MYNEWT_VAL(BLE_MESH_PB_GATT))) {
            bt_mesh_proxy_prov_enable();
        }

        return err;
    }

    bt_mesh.seq = seq;

    bt_mesh_comp_provision(addr);

    memcpy(bt_mesh.dev_key, dev_key, 16);

    provisioned = true;

    if (bt_mesh_beacon_get() == BT_MESH_BEACON_ENABLED) {
        bt_mesh_beacon_enable();
    }
    else {
        bt_mesh_beacon_disable();
    }

    if ((MYNEWT_VAL(BLE_MESH_GATT_PROXY))
            && bt_mesh_gatt_proxy_get() != BT_MESH_GATT_PROXY_NOT_SUPPORTED) {
        bt_mesh_proxy_gatt_enable();
        bt_mesh_adv_update();
    }

    /* If PB-ADV is disabled then scanning will have been disabled */
    if (!(MYNEWT_VAL(BLE_MESH_PB_ADV))) {
        bt_mesh_scan_enable();
    }

    if ((MYNEWT_VAL(BLE_MESH_LOW_POWER))) {
        bt_mesh_lpn_init();
    }

    if ((MYNEWT_VAL(BLE_MESH_FRIEND))) {
        bt_mesh_friend_init();
    }

    return 0;
}

void
bt_mesh_reset(void)
{
    if (!provisioned) {
        goto enable_beacon;
    }

    bt_mesh_comp_unprovision();

    bt_mesh.iv_index = 0;
    bt_mesh.seq = 0;
    bt_mesh.iv_update = 0;
    bt_mesh.valid = 0;
    bt_mesh.last_update = 0;
    bt_mesh.ivu_initiator = 0;

    k_delayed_work_cancel(&bt_mesh.ivu_complete);

    if ((MYNEWT_VAL(BLE_MESH_LOW_POWER))) {
        bt_mesh_lpn_disable();
    }

    if ((MYNEWT_VAL(BLE_MESH_GATT_PROXY))) {
        bt_mesh_proxy_gatt_disable();
    }

    if ((MYNEWT_VAL(BLE_MESH_PB_GATT))) {
        bt_mesh_proxy_prov_enable();
    }

    memset(bt_mesh.dev_key, 0, sizeof(bt_mesh.dev_key));

    memset(bt_mesh.rpl, 0, sizeof(bt_mesh.rpl));

    provisioned = false;

enable_beacon:
    if ((MYNEWT_VAL(BLE_MESH_PB_ADV))) {
        /* Make sure we're scanning for provisioning inviations */
        bt_mesh_scan_enable();
        /* Enable unprovisioned beacon sending */
        bt_mesh_beacon_enable();
    }
    else {
        bt_mesh_scan_disable();
        bt_mesh_beacon_disable();
    }
}

bool
bt_mesh_is_provisioned(void)
{
    return provisioned;
}

static int
bt_mesh_gap_event(struct ble_gap_event *event, void *arg)
{
    ble_adv_gap_mesh_cb(event, arg);

#if (MYNEWT_VAL(BLE_MESH_PROXY))
    ble_mesh_proxy_gap_event(event, arg);
#endif

    return 0;
}

int
bt_mesh_init(uint8_t own_addr_type, const struct bt_mesh_prov *prov,
             const struct bt_mesh_comp *comp)
{
    int err;

    g_mesh_addr_type = own_addr_type;

    /* initialize SM alg ECC subsystem (it is used directly from mesh code) */
    ble_sm_alg_ecc_init();

    err = bt_mesh_comp_register(comp);
    if (err) {
        return err;
    }

    if (MYNEWT_VAL(BLE_MESH_PROV)) {
        err = bt_mesh_prov_init(prov);
        if (err) {
            return err;
        }

    }

#if (MYNEWT_VAL(BLE_MESH_PROXY))
    bt_mesh_proxy_init();
    /* Need this to proper link.rx.buf allocation */
    bt_mesh_prov_reset_link();
#endif

    bt_mesh_net_init();

    bt_mesh_trans_init();

    bt_mesh_beacon_init();

    bt_mesh_adv_init();

#if (MYNEWT_VAL(BLE_MESH_PB_ADV))
    /* Make sure we're scanning for provisioning inviations */
    bt_mesh_scan_enable();
    /* Enable unprovisioned beacon sending */

    bt_mesh_beacon_enable();
#endif

#if (MYNEWT_VAL(BLE_MESH_PB_GATT))
    bt_mesh_proxy_prov_enable();
#endif

    ble_gap_mesh_cb_register(bt_mesh_gap_event, NULL);

    return 0;
}
