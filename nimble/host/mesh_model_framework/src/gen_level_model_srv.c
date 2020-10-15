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

#include "mesh/mesh.h"
#include "model.h"

#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG_MODEL))

static void
gen_level_status(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx)
{
   struct bt_mesh_model_gen_level_srv *srv = model->user_data;
   struct os_mbuf *msg = NET_BUF_SIMPLE(3);
   int16_t *level;

   bt_mesh_model_msg_init(msg, OP_GEN_LEVEL_STATUS);
   level = net_buf_simple_add(msg, 2);
   if (srv) {
      *level = srv->level_state;
   }

   BT_DBG("singed level = %d", *level);
   BT_DBG("unsigned level = %u", (uint16_t) *level);

   if (bt_mesh_model_send(model, ctx, msg, NULL, NULL)) {
      BT_ERR("Send status failed");
   }

   os_mbuf_free_chain(msg);
}

static void
gen_level_get(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
   gen_level_status(model, ctx);
}

static void
gen_level_set_unack(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
                    struct os_mbuf *buf)
{
   struct bt_mesh_model_gen_level_srv *srv = model->user_data;
   int16_t level;

   level = (int16_t) net_buf_simple_pull_le16(buf);

   /*
    * if required, handle Transaction ID here
    *
    * uint8_t tid;
    * tid = net_buf_simple_pull_u8(buf);
    */

   if (srv && srv->cb) {
      srv->level_state = level;
      srv->cb(model);
   }
}

static void
gen_level_set(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
   gen_level_set_unack(model, ctx, buf);
   gen_level_status(model, ctx);
}

const struct bt_mesh_model_op gen_level_srv_op[] = {
   { OP_GEN_LEVEL_GET,     0, gen_level_get },
   { OP_GEN_LEVEL_SET,     2, gen_level_set },
   { OP_GEN_LEVEL_SET_UNACK,  2, gen_level_set_unack },
   BT_MESH_MODEL_OP_END,
};