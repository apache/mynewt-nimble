/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "net.h"
#include "store.h"
#include "store_config.h"
#include "mesh_priv.h"

#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG))

struct bt_mesh_store_net mesh_store_net;
struct bt_mesh_store_sub mesh_store_subs[MYNEWT_VAL(BLE_MESH_SUBNET_COUNT)];
struct bt_mesh_store_app_key mesh_store_app_keys[MYNEWT_VAL(BLE_MESH_APP_KEY_COUNT)];

const struct bt_mesh_store_net *bt_mesh_store_net_get(void)
{
	BT_DBG("");

	return &mesh_store_net;
}

void bt_mesh_store_net_set(struct bt_mesh_net *net)
{
	BT_DBG("");

	mesh_store_net.iv_index = net->iv_index;
	mesh_store_net.provisioned = net->provisioned;
	mesh_store_net.dev_primary_addr = net->dev_primary_addr;
	mesh_store_net.seq = net->seq;
	mesh_store_net.iv_update = net->iv_update;
	memcpy(mesh_store_net.dev_key, net->dev_key, sizeof(net->dev_key));

	bt_mesh_store_config_persist_net();
}

const struct bt_mesh_store_sub *bt_mesh_store_sub_get_next(const struct bt_mesh_store_sub *p)
{
	const struct bt_mesh_store_sub *last = mesh_store_subs + ARRAY_SIZE(mesh_store_subs) - 1;

	if (!p) {
		return &mesh_store_subs[0];
	}

	if ((p < mesh_store_subs) || (p >= (last))) {
		return NULL;
	}

	p++;

	while(p < last) {
		if (p->net_idx != BT_MESH_KEY_UNUSED) {
			return p;
		}

		p++;
	}

	return NULL;
}

static struct bt_mesh_store_sub *mesh_store_subs_get(u16_t net_idx)
{
	int i;

	if (net_idx == BT_MESH_KEY_ANY) {
		return &mesh_store_subs[0];
	}

	for (i = 0; i < ARRAY_SIZE(mesh_store_subs); i++) {
		if (mesh_store_subs[i].net_idx == net_idx) {
			return &mesh_store_subs[i];
		}
	}

	return NULL;
}

static struct bt_mesh_store_sub *mesh_store_subs_alloc(void)
{
	return mesh_store_subs_get(BT_MESH_KEY_UNUSED);
}

static void mesh_store_sub_set(struct bt_mesh_store_sub *store_sub,
			       struct bt_mesh_subnet *sub)
{
	store_sub->net_idx = sub->net_idx;
	store_sub->kr_flag = sub->kr_flag;
	store_sub->kr_phase = sub->kr_phase;

	memcpy(store_sub->keys[0].net, sub->keys[0].net,
	       sizeof(store_sub->keys[0].net));
	memcpy(store_sub->keys[1].net, sub->keys[1].net,
	       sizeof(store_sub->keys[1].net));
}

void bt_mesh_store_sub_set(struct bt_mesh_subnet *sub)
{
	struct bt_mesh_store_sub *store_sub;

	store_sub = mesh_store_subs_get(sub->net_idx);
	if (!store_sub) {
		store_sub = mesh_store_subs_alloc();
	}

	mesh_store_sub_set(store_sub, sub);

	bt_mesh_store_config_persist_subnets();
}

void bt_mesh_store_sub_set_all(struct bt_mesh_subnet *sub, size_t count)
{
	int i;

	for (i = 0; i < count ; ++i) {
		mesh_store_sub_set(&mesh_store_subs[i], sub++);
	}

	bt_mesh_store_config_persist_subnets();
}

u8_t bt_mesh_store_net_flags(const struct bt_mesh_store_sub *sub)
{
	u8_t flags = 0x00;

	if (sub && sub->kr_flag) {
		flags |= BT_MESH_NET_FLAG_KR;
	}

	if (mesh_store_net.iv_update) {
		flags |= BT_MESH_NET_FLAG_IVU;
	}

	return flags;
}

static struct bt_mesh_store_app_key *app_key_alloc()
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mesh_store_app_keys); i++) {
		struct bt_mesh_store_app_key *key = &mesh_store_app_keys[i];

		if (key->net_idx == BT_MESH_KEY_UNUSED) {
			return key;
		}
	}

	return NULL;
}

static struct bt_mesh_store_app_key *app_key_find(u16_t app_idx)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mesh_store_app_keys); i++) {
		struct bt_mesh_store_app_key *key = &mesh_store_app_keys[i];

		if (key->net_idx != BT_MESH_KEY_UNUSED &&
		    key->app_idx == app_idx) {
			return key;
		}
	}

	return NULL;
}

void bt_mesh_store_app_key_del(struct bt_mesh_app_key *key)
{
	struct bt_mesh_store_app_key *found_key;

	found_key = app_key_find(key->app_idx);
	if (!found_key) {
		return;
	}

	found_key->net_idx = BT_MESH_KEY_UNUSED;
	memset(found_key->keys, 0, sizeof(found_key->keys));

	bt_mesh_store_config_persist_app_keys();
}

static void mesh_store_app_key_set(struct bt_mesh_store_app_key *store_key,
				   struct bt_mesh_app_key *key)
{
	store_key->net_idx = key->net_idx;
	store_key->app_idx = key->app_idx;
	store_key->updated = key->updated;
	store_key->keys[0].id = key->keys[0].id;
	store_key->keys[1].id = key->keys[1].id;
	memcpy(store_key->keys[0].val, key->keys[0].val,
	       sizeof(store_key->keys[0].val));
	memcpy(store_key->keys[1].val, key->keys[1].val,
	       sizeof(store_key->keys[1].val));
}

static void mesh_store_app_key_restore(struct bt_mesh_app_key *key,
				       struct bt_mesh_store_app_key *store_key)
{
	key->net_idx = store_key->net_idx ;
	key->app_idx = store_key->app_idx;
	key->updated = store_key->updated;
	key->keys[0].id = store_key->keys[0].id ;
	key->keys[1].id = store_key->keys[1].id;
	memcpy(key->keys[0].val, store_key->keys[0].val,
	       sizeof(key->keys[0].val));
	memcpy(key->keys[1].val, store_key->keys[1].val,
	       sizeof(key->keys[1].val));
}

void bt_mesh_store_app_key_set(struct bt_mesh_app_key *key)
{
	struct bt_mesh_store_app_key *found_key = app_key_find(key->app_idx);
	if (!found_key) {
		found_key = app_key_alloc();
	}

	mesh_store_app_key_set(found_key, key);

	bt_mesh_store_config_persist_app_keys();
}

void bt_mesh_store_app_key_set_all(struct bt_mesh_app_key *key, size_t count)
{
	int i;

	for (i = 0; i < count ; ++i) {
		mesh_store_app_key_set(&mesh_store_app_keys[i], key++);
	}

	bt_mesh_store_config_persist_app_keys();
}

void bt_mesh_store_app_key_restore(struct bt_mesh_app_key *key, size_t num)
{
	int i;

	for (i = 0; i < num; ++i) {
		mesh_store_app_key_restore(&key[i], &mesh_store_app_keys[i]);
	}
}
