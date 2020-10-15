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

#ifndef __GEN_LEVEL_MODEL_CLI_H__
#define __GEN_LEVEL_MODEL_CLI_H__

struct bt_mesh_gen_level_model_cli {
    struct bt_mesh_model *model;
    struct bt_mesh_model_pub pub;
    uint8_t transaction_id;
    int16_t level;

    struct k_sem          op_sync;
    u32_t                 op_pending;
    void                 *op_param;
};

extern const struct bt_mesh_model_op gen_level_cli_op[];

#define BT_MESH_MODEL_GEN_LEVEL_CLI(cli, pub)       \
    BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_LEVEL_CLI,   \
              gen_level_cli_op, pub, cli)

int bt_mesh_gen_level_model_cli_get(
    struct bt_mesh_gen_level_model_cli *cli,
    struct bt_mesh_msg_ctx *ctx
);

int bt_mesh_gen_level_model_cli_set(
    struct bt_mesh_gen_level_model_cli *cli,
    struct bt_mesh_msg_ctx *ctx,
    int16_t level
);

int bt_mesh_gen_level_model_cli_set_unack(
    struct bt_mesh_gen_level_model_cli *cli,
    struct bt_mesh_msg_ctx *ctx,
    int16_t level
);

void bt_mesh_gen_level_model_cli_init(
    struct bt_mesh_model *models,
    uint32_t models_count
);

#endif
