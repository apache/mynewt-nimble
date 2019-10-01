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

#include <mesh/mesh.h>
#include "model.h"

#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG_MODEL))

#define ONOFF_SET_MSG_SIZE (2 + 2 + 4)
#define ONOFF_GET_MSG_SIZE (2 + 0 + 4)

static s32_t msg_timeout = K_SECONDS(5);

struct gen_onoff_param {
    uint8_t *state;
};

static void
gen_onoff_status(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
                 struct os_mbuf *buf)
{
   struct bt_mesh_gen_onoff_model_cli *cli = model->user_data;
   struct gen_onoff_param *param;
   uint8_t state;

   BT_DBG("net_idx 0x%04x app_idx 0x%04x src 0x%04x len %u: %s",
          ctx->net_idx, ctx->app_idx, ctx->addr, buf->om_len,
          bt_hex(buf->om_data, buf->om_len));

   if (cli->op_pending != OP_GEN_ONOFF_STATUS) {
      BT_WARN("Unexpected Generic OnOff Status message");
      return;
   }

   param = cli->op_param;

   state = net_buf_simple_pull_u8(buf);
   if (param->state) {
      *param->state = state;
   }

   BT_DBG("state: %u", state);

   k_sem_give(&cli->op_sync);
}


const struct bt_mesh_model_op gen_onoff_cli_op[] = {
   { OP_GEN_ONOFF_STATUS, 1, gen_onoff_status },
   BT_MESH_MODEL_OP_END,
};


static int
cli_wait(struct bt_mesh_gen_onoff_model_cli *cli, void *param, u32_t op)
{
   int err;

   BT_DBG("");

   cli->op_param = param;
   cli->op_pending = op;

   err = k_sem_take(&cli->op_sync, msg_timeout);

   cli->op_pending = 0;
   cli->op_param = NULL;

   return err;
}


int
bt_mesh_gen_onoff_model_cli_get(struct bt_mesh_gen_onoff_model_cli *cli,
                                struct bt_mesh_msg_ctx *ctx)
{
   if (cli == NULL)
      return BLE_HS_ENOADDR;

   struct os_mbuf *msg = NET_BUF_SIMPLE(ONOFF_GET_MSG_SIZE);
   struct gen_onoff_param param;
   int err;

   param.state = &cli->state;

   bt_mesh_model_msg_init(msg, OP_GEN_ONOFF_GET);

   err = bt_mesh_model_send(cli->model, ctx, msg, NULL, NULL);
   if (err) {
      BT_ERR("model_send() failed (err %d)", err);
      goto done;
   }

   err = cli_wait(cli, &param, OP_GEN_ONOFF_STATUS);
done:
   os_mbuf_free_chain(msg);
   return err;
}


int
bt_mesh_gen_onoff_model_cli_set(struct bt_mesh_gen_onoff_model_cli *cli,
                                struct bt_mesh_msg_ctx *ctx, uint8_t val)
{
   if (cli == NULL)
      return BLE_HS_ENOADDR;

   struct os_mbuf *msg = NET_BUF_SIMPLE(ONOFF_SET_MSG_SIZE);
   struct gen_onoff_param param;
   int err;

   param.state = &cli->state;

   bt_mesh_model_msg_init(msg, OP_GEN_ONOFF_SET);
   net_buf_simple_add_u8(msg, val);
   net_buf_simple_add_u8(msg, cli->transaction_id);

   BT_DBG("OnOff Model Client found 0x%p", cli->model);

   err = bt_mesh_model_send(cli->model, ctx, msg, NULL, NULL);
   if (err) {
      BT_ERR("model_send() failed (err %d)", err);
      goto done;
   }

   err = cli_wait(cli, &param, OP_GEN_ONOFF_STATUS);

done:
   if (err = 0) {
      cli->transaction_id++;
   }

   os_mbuf_free_chain(msg);
   return err;
}

int
bt_mesh_gen_onoff_model_cli_set_unack(struct bt_mesh_gen_onoff_model_cli *cli,
                                      struct bt_mesh_msg_ctx *ctx, uint8_t val)
{
   if (cli == NULL)
      return BLE_HS_ENOADDR;

   struct os_mbuf *msg = NET_BUF_SIMPLE(ONOFF_SET_MSG_SIZE);
   int err;

   bt_mesh_model_msg_init(msg, OP_GEN_ONOFF_SET_UNACK);
   net_buf_simple_add_u8(msg, val);
   net_buf_simple_add_u8(msg, cli->transaction_id);

   BT_DBG("OnOff Model Client found 0x%p", cli->model);

   err = bt_mesh_model_send(cli->model, ctx, msg, NULL, NULL);
   if (err) {
      BT_ERR("model_send() failed (err %d)", err);
   }

   os_mbuf_free_chain(msg);
   return err;
}

void
bt_mesh_gen_onoff_model_cli_init(struct bt_mesh_model *models,
                                 uint32_t models_count)
{
   if (models && models_count > 0) {
      uint32_t i;
      struct bt_mesh_model *model = models;

      for (i = 0; i < models_count; i++) {
         if (model->id == BT_MESH_MODEL_ID_GEN_ONOFF_CLI && model->user_data) {
            struct bt_mesh_gen_onoff_model_cli *cli = model->user_data;

            cli->model = model;
            cli->transaction_id = 0;
            k_sem_init(&cli->op_sync, 0, 1);

            cli->pub.msg = NET_BUF_SIMPLE(ONOFF_SET_MSG_SIZE);
         }

         model++;
      }
   }
}

