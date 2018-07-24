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
#include "shell.h"
#include "mesh_priv.h"
#include "settings.h"

u8_t g_mesh_addr_type;

int bt_mesh_provision(const u8_t net_key[16], u16_t net_idx,
		      u8_t flags, u32_t iv_index, u16_t addr,
		      const u8_t dev_key[16])
{
	int err;

	BT_INFO("Primary Element: 0x%04x", addr);
	BT_DBG("net_idx 0x%04x flags 0x%02x iv_index 0x%04x",
	       net_idx, flags, iv_index);

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

	bt_mesh.seq = 0;

	bt_mesh_comp_provision(addr);

	memcpy(bt_mesh.dev_key, dev_key, 16);

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		BT_DBG("Storing network information persistently");
		bt_mesh_store_net();
		bt_mesh_store_subnet(&bt_mesh.sub[0]);
		bt_mesh_store_iv(false);
	}

	bt_mesh_net_start();

	return 0;
}

void bt_mesh_reset(void)
{
	if (!bt_mesh.valid) {
		return;
	}

	bt_mesh.iv_index = 0;
	bt_mesh.seq = 0;
	bt_mesh.iv_update = 0;
	bt_mesh.pending_update = 0;
	bt_mesh.valid = 0;
	bt_mesh.ivu_duration = 0;
	bt_mesh.ivu_initiator = 0;

	k_delayed_work_cancel(&bt_mesh.ivu_timer);

	bt_mesh_cfg_reset();

	bt_mesh_rx_reset();
	bt_mesh_tx_reset();

	if ((MYNEWT_VAL(BLE_MESH_LOW_POWER))) {
		bt_mesh_lpn_disable(true);
	}

	if ((MYNEWT_VAL(BLE_MESH_FRIEND))) {
		bt_mesh_friend_clear_net_idx(BT_MESH_KEY_ANY);
	}

	if ((MYNEWT_VAL(BLE_MESH_GATT_PROXY))) {
		bt_mesh_proxy_gatt_disable();
	}

	if ((MYNEWT_VAL(BLE_MESH_PB_GATT))) {
		bt_mesh_proxy_prov_enable();
	}

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		bt_mesh_clear_net();
	}

	memset(bt_mesh.dev_key, 0, sizeof(bt_mesh.dev_key));

	bt_mesh_scan_disable();
	bt_mesh_beacon_disable();

	bt_mesh_comp_unprovision();

	if (IS_ENABLED(CONFIG_BT_MESH_PROV)) {
		bt_mesh_prov_reset();
	}
}

bool bt_mesh_is_provisioned(void)
{
	return bt_mesh.valid;
}

int bt_mesh_prov_enable(bt_mesh_prov_bearer_t bearers)
{
	if (bt_mesh_is_provisioned()) {
		return -EALREADY;
	}

	if (IS_ENABLED(CONFIG_BT_MESH_PB_ADV) &&
	    (bearers & BT_MESH_PROV_ADV)) {
		/* Make sure we're scanning for provisioning inviations */
		bt_mesh_scan_enable();
		/* Enable unprovisioned beacon sending */
		bt_mesh_beacon_enable();
	}

	if (IS_ENABLED(CONFIG_BT_MESH_PB_GATT) &&
	    (bearers & BT_MESH_PROV_GATT)) {
		bt_mesh_proxy_prov_enable();
		bt_mesh_adv_update();
	}

	return 0;
}

int bt_mesh_prov_disable(bt_mesh_prov_bearer_t bearers)
{
	if (bt_mesh_is_provisioned()) {
		return -EALREADY;
	}

	if (IS_ENABLED(CONFIG_BT_MESH_PB_ADV) &&
	    (bearers & BT_MESH_PROV_ADV)) {
		bt_mesh_beacon_disable();
		bt_mesh_scan_disable();
	}

	if (IS_ENABLED(CONFIG_BT_MESH_PB_GATT) &&
	    (bearers & BT_MESH_PROV_GATT)) {
		bt_mesh_proxy_prov_disable();
		bt_mesh_adv_update();
	}

	return 0;
}

static int bt_mesh_gap_event(struct ble_gap_event *event, void *arg)
{
	ble_adv_gap_mesh_cb(event, arg);

#if (MYNEWT_VAL(BLE_MESH_PROXY))
	ble_mesh_proxy_gap_event(event, arg);
#endif

	return 0;
}

int bt_mesh_init(uint8_t own_addr_type, const struct bt_mesh_prov *prov,
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

#if (MYNEWT_VAL(BLE_MESH_PROV))
	err = bt_mesh_prov_init(prov);
	if (err) {
		return err;
	}
#endif

#if (MYNEWT_VAL(BLE_MESH_PROXY))
	bt_mesh_proxy_init();
#endif

#if (MYNEWT_VAL(BLE_MESH_PROV))
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

#if (MYNEWT_VAL(BLE_MESH_SETTINGS))
	bt_mesh_settings_init();
#endif

	return 0;
}
