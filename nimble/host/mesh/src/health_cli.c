/*  Bluetooth Mesh */

/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include "syscfg/syscfg.h"
#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG_MODEL))
#include "host/ble_hs_log.h"

#include "mesh/mesh.h"
#include "mesh_priv.h"
#include "adv.h"
#include "net.h"
#include "transport.h"
#include "access.h"
#include "foundation.h"

static s32_t msg_timeout = K_SECONDS(2);

static struct bt_mesh_health_cli *health_cli;

const struct bt_mesh_model_op bt_mesh_health_cli_op[] = {
	BT_MESH_MODEL_OP_END,
};

s32_t bt_mesh_health_cli_timeout_get(void)
{
	return msg_timeout;
}

void bt_mesh_health_cli_timeout_set(s32_t timeout)
{
	msg_timeout = timeout;
}

int bt_mesh_health_cli_set(struct bt_mesh_model *model)
{
	if (!model->user_data) {
		BT_ERR("No Health Client context for given model");
		return -EINVAL;
	}

	health_cli = model->user_data;

	return 0;
}

int bt_mesh_health_cli_init(struct bt_mesh_model *model, bool primary)
{
	struct bt_mesh_health_cli *cli = model->user_data;

	BT_DBG("primary %u", primary);

	if (!cli) {
		BT_ERR("No Health Client context provided");
		return -EINVAL;
	}

	cli = model->user_data;
	cli->model = model;

	k_sem_init(&cli->op_sync, 0, 1);

	/* Set the default health client pointer */
	if (!health_cli) {
		health_cli = cli;
	}

	return 0;
}
