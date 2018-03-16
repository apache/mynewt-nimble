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

#include "syscfg/syscfg.h"

//#if MYNEWT_VAL(BT_MESH_STORE_CONFIG_PERSIST)

#include <inttypes.h>

#include "sysinit/sysinit.h"

#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG))

#include "config/config.h"
#include "base64/base64.h"
#include "mesh/glue.h"
#include "store.h"
#include "store_config.h"

extern struct bt_mesh_store_net mesh_store_net;
extern struct bt_mesh_store_sub mesh_store_subs[MYNEWT_VAL(BLE_MESH_SUBNET_COUNT)];
extern struct bt_mesh_store_app_key mesh_store_app_keys[MYNEWT_VAL(BLE_MESH_APP_KEY_COUNT)];

static int bt_mesh_store_config_conf_set(int argc, char **argv, char *val);
static int bt_mesh_store_config_conf_export(void (*func)(char *name, char *val),
					    enum conf_export_tgt tgt);

static struct conf_handler bt_mesh_store_config_conf_handler = {
	.ch_name = "bt_mesh",
	.ch_get = NULL,
	.ch_set = bt_mesh_store_config_conf_set,
	.ch_commit = NULL,
	.ch_export = bt_mesh_store_config_conf_export
};

#define BT_STORE_CONFIG_NET_ENCODE_SZ \
            (BASE64_ENCODE_SIZE(sizeof(mesh_store_net)))

#define BT_STORE_CONFIG_SUBNETS_ENCODE_SZ \
            (BASE64_ENCODE_SIZE(sizeof(mesh_store_subs)))

#define BT_STORE_CONFIG_APP_KEYS_ENCODE_SZ \
            (BASE64_ENCODE_SIZE(sizeof(mesh_store_app_keys)))

static void bt_mesh_store_config_serialize_arr(const void *arr, int obj_sz,
					       int num_objs, char *out_buf,
					       int buf_sz)
{
	int arr_size;

	arr_size = obj_sz * num_objs;
	assert(arr_size <= buf_sz);

	base64_encode(arr, arr_size, out_buf, 1);
}

static int bt_mesh_store_config_deserialize_arr(const char *enc, void *out_arr,
						int obj_sz, int *out_num_objs)
{
	int len;

	len = base64_decode(enc, out_arr);
	if (len < 0) {
		return OS_EINVAL;
	}

	*out_num_objs = len / obj_sz;
	return 0;
}

static int bt_mesh_store_config_conf_set(int argc, char **argv, char *val)
{
	int rc;

	BT_DBG("");

	if (argc == 1) {
		if (strcmp(argv[0], "net") == 0) {
			rc = bt_mesh_store_config_deserialize_arr(
				val,
				&mesh_store_net,
				sizeof(struct bt_mesh_store_net),
				NULL);
			return rc;
		}
		if (strcmp(argv[0], "subnets") == 0) {
			rc = bt_mesh_store_config_deserialize_arr(
				val,
				mesh_store_subs,
				sizeof(struct bt_mesh_store_sub),
				NULL);
			return rc;
		}
		if (strcmp(argv[0], "app_keys") == 0) {
			rc = bt_mesh_store_config_deserialize_arr(
				val,
				mesh_store_app_keys,
				sizeof(struct bt_mesh_store_app_key),
				NULL);
			return rc;
		}
	}
	return OS_ENOENT;
}

static int bt_mesh_store_config_conf_export(void (*func)(char *name, char *val),
					    enum conf_export_tgt tgt)
{
	union {
	    char net[BT_STORE_CONFIG_NET_ENCODE_SZ];
	    char subs[BT_STORE_CONFIG_SUBNETS_ENCODE_SZ];
	    char app_keys[BT_STORE_CONFIG_APP_KEYS_ENCODE_SZ];
	} buf;

	BT_DBG("");

	bt_mesh_store_config_serialize_arr(&mesh_store_net,
					   sizeof(struct bt_mesh_store_net),
					   1,
					   buf.net,
					   sizeof(buf.net));
	func("bt_mesh/net", buf.net);

	bt_mesh_store_config_serialize_arr(&mesh_store_subs,
					   sizeof(struct bt_mesh_store_sub),
					   ARRAY_SIZE(mesh_store_subs),
					   buf.subs,
					   sizeof(buf.subs));
	func("bt_mesh/subnets", buf.subs);

	bt_mesh_store_config_serialize_arr(&mesh_store_app_keys,
					   sizeof(struct bt_mesh_store_app_key),
					   ARRAY_SIZE(mesh_store_app_keys),
					   buf.app_keys,
					   sizeof(buf.app_keys));
	func("bt_mesh/app_keys", buf.app_keys);

	return 0;
}

int bt_mesh_store_config_persist_net(void)
{
	char buf[BT_STORE_CONFIG_NET_ENCODE_SZ];
	int rc;

	bt_mesh_store_config_serialize_arr(&mesh_store_net,
					   sizeof(struct bt_mesh_store_net),
					   1, buf, sizeof(buf));
	rc = conf_save_one("bt_mesh/net", buf);

	BT_DBG("");

	if (rc != 0) {
		return rc;
	}

	return 0;
}

int bt_mesh_store_config_persist_subnets(void)
{
	char buf[BT_STORE_CONFIG_SUBNETS_ENCODE_SZ];
	int rc;

	bt_mesh_store_config_serialize_arr(mesh_store_subs,
					   sizeof(struct bt_mesh_store_sub),
					   ARRAY_SIZE(mesh_store_subs),
					   buf, sizeof(buf));
	rc = conf_save_one("bt_mesh/subnets", buf);

	BT_DBG("");

	if (rc != 0) {
		return rc;
	}

	return 0;
}

int bt_mesh_store_config_persist_app_keys(void)
{
	char buf[BT_STORE_CONFIG_APP_KEYS_ENCODE_SZ];
	int rc;

	bt_mesh_store_config_serialize_arr(mesh_store_app_keys,
					   sizeof(struct bt_mesh_store_app_keys),
					   ARRAY_SIZE(mesh_store_app_keys),
					   buf, sizeof(buf));
	rc = conf_save_one("bt_mesh/app_keys", buf);

	BT_DBG("");

	if (rc != 0) {
		return rc;
	}

	return 0;
}

void bt_mesh_store_config_init(void)
{
	int rc;

	/* Ensure this function only gets called by sysinit. */
	SYSINIT_ASSERT_ACTIVE();

	rc = conf_register(&bt_mesh_store_config_conf_handler);
	SYSINIT_PANIC_ASSERT_MSG(rc == 0,
				 "Failed to register bt_mesh_store_config conf");

	rc = conf_load();
	SYSINIT_PANIC_ASSERT_MSG(rc == 0,
				 "Failed to load config");
}

//#endif /* MYNEWT_VAL(BLE_STORE_CONFIG_PERSIST) */
