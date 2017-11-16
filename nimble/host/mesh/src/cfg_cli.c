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
#include "mesh/mesh.h"


const struct bt_mesh_model_op bt_mesh_cfg_cli_op[] = {
	BT_MESH_MODEL_OP_END,
};
