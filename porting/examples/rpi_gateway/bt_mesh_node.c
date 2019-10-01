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

#include "bt_mesh_helper.h"
#include "bt_mesh_node.h"
#include "model.h"
#include "mesh/access.h"
#include "console/console.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// OnOff Model Definations & Configurations
//=============================================================================

/* Vendor Model data */
#define VND_MODEL_ID_1 0x1234

static struct bt_mesh_prov prov;
static struct bt_mesh_cfg_srv cfg_srv;
static struct bt_mesh_health_srv health_srv;
static struct bt_mesh_model_pub health_pub;

static struct bt_mesh_model vnd_models[] = {
    BT_MESH_MODEL_VND(CID_VENDOR, VND_MODEL_ID_1,
        BT_MESH_MODEL_NO_OPS, NULL, NULL),
};


static struct network net = {
   .local = BT_MESH_ADDR_UNASSIGNED,
   .dst = BT_MESH_ADDR_UNASSIGNED,
};

//=============================================================================
// Primary Element
//=============================================================================

static struct bt_mesh_gen_onoff_model_cli primary_gen_onoff_cli;

uint32_t gen_onoff_get(uint16_t dst, uint8_t *state)
{
	int err;

	if (net.local == BT_MESH_ADDR_UNASSIGNED)
	{
		printf("ERR: node provisioning is required.\n");
		return 1;
	}

	struct bt_mesh_msg_ctx ctx = {
	   .net_idx = net.net_idx,
	   .app_idx = net.app_idx,
	   .addr = dst,
	   .send_ttl = BT_MESH_TTL_DEFAULT,
    };

	err = bt_mesh_gen_onoff_model_cli_get(&primary_gen_onoff_cli, &ctx);
	if (err == 0) {
		printf("onoff state for element=0x%x is %d\n", dst, primary_gen_onoff_cli.state);

		if(state)
			*state = primary_gen_onoff_cli.state;
	} else {
		printf("failed to get onoff state for element=0x%x\n", dst);
	}

	return err;
}

uint32_t gen_onoff_set(uint16_t dst, uint8_t val)
{
	int err;

	if (net.local == BT_MESH_ADDR_UNASSIGNED)
	{
		printf("ERR: node provisioning is required.\n");
		return 1;
	}

	struct bt_mesh_msg_ctx ctx = {
		.net_idx = net.net_idx,
		.app_idx = net.app_idx,
		.addr = dst,
		.send_ttl = BT_MESH_TTL_DEFAULT,
	};

	printf("gen_onoff_set requested val=%d\n", val);

	err = bt_mesh_gen_onoff_model_cli_set(&primary_gen_onoff_cli, &ctx, val);
	if (err) {
		printf("Failed to send Generic OnOff Get (err %d)\n", err);
	} else {
		printf("Primary Gen OnOff State %d\n", primary_gen_onoff_cli.state);
	}

	return err;
}

uint32_t publish_gen_onoff_set(uint8_t state)
{
	if (net.local == BT_MESH_ADDR_UNASSIGNED)
	{
		printf("ERR: node provisioning is required.\n");
		return 1;
	}

	int err;
	struct os_mbuf *msg = NET_BUF_SIMPLE(2+2+4);

	printf("Primary state change requested state=%d\n", state);

	primary_gen_onoff_cli.pub.msg = msg;
	bt_mesh_model_msg_init(msg, OP_GEN_ONOFF_SET_UNACK);
	net_buf_simple_add_u8(msg, state);
	net_buf_simple_add_u8(msg, primary_gen_onoff_cli.transaction_id);

	err = bt_mesh_model_publish(primary_gen_onoff_cli.model);
	if (err) {
		printf("Failed to send Generic OnOff Get (err %d)\n", err);
	}
	else {
		primary_gen_onoff_cli.transaction_id++;
	}

	os_mbuf_free_chain(msg);
	primary_gen_onoff_cli.pub.msg = NULL;

	return err;
}

static struct bt_mesh_gen_level_model_cli primary_gen_level_cli;

uint32_t gen_level_get(uint16_t dst, int16_t *level)
{
	int err;

	if (net.local == BT_MESH_ADDR_UNASSIGNED)
	{
		printf("ERR: node provisioning is required.\n");
		return 1;
	}

	struct bt_mesh_msg_ctx ctx = {
	   .net_idx = net.net_idx,
	   .app_idx = net.app_idx,
	   .addr = dst,
	   .send_ttl = BT_MESH_TTL_DEFAULT,
    };

	err = bt_mesh_gen_level_model_cli_get(&primary_gen_level_cli, &ctx);
	if (err == 0) {
		printf("level state for element=0x%x is %d\n", dst, primary_gen_level_cli.level);

		if (level)
			*level = primary_gen_level_cli.level;
	} else {
		printf("failed to get level state for element=0x%x\n", dst);
	}

	return err;
}

uint32_t gen_level_set(uint16_t dst, uint8_t level)
{
	if (net.local == BT_MESH_ADDR_UNASSIGNED)
	{
		printf("ERR: node provisioning is required.\n");
		return 1;
	}

	int err;
	struct bt_mesh_msg_ctx ctx = {
		.net_idx = net.net_idx,
		.app_idx = net.app_idx,
		.addr = dst,
		.send_ttl = BT_MESH_TTL_DEFAULT,
	};

	printf("gen_level_set requested val=%d\n", level);

	err = bt_mesh_gen_level_model_cli_set(&primary_gen_level_cli, &ctx, level);
	if (err) {
		printf("Failed to send Generic Level Get (err %d)\n", err);
	} else {
		printf("Primary Gen Level State %d\n", primary_gen_level_cli.level);
	}

	return err;
}

uint32_t publish_gen_level_set(uint16_t level)
{
	if (net.local == BT_MESH_ADDR_UNASSIGNED)
	{
		printf("ERR: node provisioning is required.\n");
		return 1;
	}

	int err;
	struct os_mbuf *msg = NET_BUF_SIMPLE(2 + 3 + 4);

	printf("Primary state change requested state=%d\n", level);

	primary_gen_level_cli.pub.msg = msg;
	bt_mesh_model_msg_init(msg, OP_GEN_ONOFF_SET_UNACK);
	net_buf_simple_add_le16(msg, level);
	net_buf_simple_add_u8(msg, primary_gen_level_cli.transaction_id);

	err = bt_mesh_model_publish(primary_gen_level_cli.model);
	if (err) {
		printf("Failed to send Generic OnOff Get (err %d)\n", err);
	}
	else {
		primary_gen_level_cli.transaction_id++;
	}

	os_mbuf_free_chain(msg);
	primary_gen_level_cli.pub.msg = NULL;

	return err;
}

static struct bt_mesh_model primery_element_models[] = {
    BT_MESH_MODEL_CFG_SRV(&cfg_srv),
    BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
	BT_MESH_MODEL_GEN_ONOFF_CLI(
			&primary_gen_onoff_cli,
			&primary_gen_onoff_cli.pub
	),
	BT_MESH_MODEL_GEN_LEVEL_CLI(
			&primary_gen_level_cli,
			&primary_gen_level_cli.pub
	),
};

//=============================================================================
// Global Definations & Configurations
//=============================================================================

static struct bt_mesh_elem elements[] = {
   BT_MESH_ELEM(0, primery_element_models, vnd_models),
};

static const struct bt_mesh_comp comp = {
   .cid = CID_VENDOR,
   .elem = elements,
   .elem_count = ARRAY_SIZE(elements),
};

//=============================================================================
// Initialization
//=============================================================================

void bt_mesh_node_init(void)
{
    int err;
    ble_addr_t addr;

    /* Initialize node configuration */
    bt_mesh_cfg_model_srv_init(&cfg_srv);

    /* Initialize health pub message */
    bt_mesh_health_model_srv_init(&health_srv, &health_pub);

    /* As of now, all controller boards have same public address.
    ** Therefore, mobile phone app can list only one node. In order to
    ** differentiat and list all the nodes we are going with ramdom
    ** address. Hence, using NRPA.
    */
    err = ble_hs_id_gen_rnd(1, &addr);
    assert(err == 0);
    err = ble_hs_id_set_rnd(addr.val);
    assert(err == 0);

    bt_mesh_provisioning_info_init(&prov, &net);

    bt_mesh_gen_onoff_model_cli_init(primery_element_models, ARRAY_SIZE(primery_element_models));
    bt_mesh_gen_level_model_cli_init(primery_element_models, ARRAY_SIZE(primery_element_models));

    err = bt_mesh_init(addr.type, &prov, &comp);

    if (err) {
        console_printf("ERR: Mesh initialization failed (err %d)\n", err);
        return;
    }

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
    }

    if (bt_mesh_is_provisioned()) {
        console_printf("Mesh network restored from flash\n");
    } else {
        console_printf("Mesh network starting fresh\n");
    }

    console_printf("Mesh initialized\n");
    console_printf("Use \"pb-adv on\" or \"pb-gatt on\" to enable advertising\n");
}

#ifdef __cplusplus
}
#endif
