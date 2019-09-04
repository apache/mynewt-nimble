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
#include "bt_mesh_helper.h"

//=============================================================================
// Globale definitions and variables
//=============================================================================

#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG_MODEL))

//=============================================================================
// Light HSL Saturation Server
//=============================================================================

static void light_hsl_sat_status(struct bt_mesh_model *model,
              struct bt_mesh_msg_ctx *ctx)
{
   struct bt_mesh_model_light_hsl_sat_srv *srv = model->user_data;
   struct os_mbuf *msg = NET_BUF_SIMPLE(4);
   uint16_t *sat_state;

   bt_mesh_model_msg_init(msg, OP_LIGHT_HSL_SAT_STATUS);
   sat_state = net_buf_simple_add(msg, 2);

   if (srv) {
      *sat_state = srv->sat_state;
   }

   BT_DBG("sat_state = %u", *sat_state);

   if (bt_mesh_model_send(model, ctx, msg, NULL, NULL)) {
      BT_ERR("Send status failed");
   }

   os_mbuf_free_chain(msg);
}


static void light_hsl_sat_get(struct bt_mesh_model *model,
           struct bt_mesh_msg_ctx *ctx,
           struct os_mbuf *buf)
{
   light_hsl_sat_status(model, ctx);
}

static void light_hsl_sat_set_unack(struct bt_mesh_model *model,
            struct bt_mesh_msg_ctx *ctx,
            struct os_mbuf *buf) {
   struct bt_mesh_model_light_hsl_sat_srv *srv = model->user_data;
   uint16_t sat_state;
   uint8_t tid;

   sat_state = (uint16_t) net_buf_simple_pull_le16(buf);
   tid = net_buf_simple_pull_u8(buf);

   BT_DBG("sat_state = %u tid = %u", sat_state, tid);

   if (srv && srv->cb) {
      bt_mesh_model_light_hsl_sat_state_update(srv, sat_state);
      srv->cb(model);
   }
}


static void light_hsl_sat_set(struct bt_mesh_model *model,
           struct bt_mesh_msg_ctx *ctx,
           struct os_mbuf *buf)
{
   light_hsl_sat_set_unack(model, ctx, buf);
   light_hsl_sat_status(model, ctx);
}


const struct bt_mesh_model_op light_hsl_sat_srv_op[] = {
   { OP_LIGHT_HSL_SAT_GET,       0, light_hsl_sat_get },
   { OP_LIGHT_HSL_SAT_SET,       3, light_hsl_sat_set },
   { OP_LIGHT_HSL_SAT_SET_UNACK, 3, light_hsl_sat_set_unack },
   BT_MESH_MODEL_OP_END,
};

//=============================================================================
// Generic Level Server
//=============================================================================

static void gen_level_state_change_cb (struct bt_mesh_model *model)
{
   if (model && model->user_data)
   {
      struct bt_mesh_model_gen_level_srv *level_srv = model->user_data;
      struct bt_mesh_model *sat_model = find_model(bt_mesh_model_elem(model),
                                        BT_MESH_MODEL_ID_LIGHT_HSL_SAT_SRV);

      if (sat_model && sat_model->user_data) {
         struct bt_mesh_model_light_hsl_sat_srv *srv = sat_model->user_data;

         srv->sat_state = level_srv->level_state + 32768;

         BT_DBG("Light HSL Saturation state changed = %d", srv->sat_state);

         if (srv->cb)
             srv->cb(sat_model);

         /* From this point onwards light hsl_sat status needs to be send to
           the publish group.
           bt_mesh_model_publish(ll_model);
        */
      }
   }
}

//=============================================================================
// Exposed Interfaces
//=============================================================================

void bt_mesh_model_light_hsl_sat_state_update(
    struct bt_mesh_model_light_hsl_sat_srv *srv,
    uint16_t sat_state
)
{
    if (srv)
    {
        srv->sat_state = sat_state;
        srv->gen_level_srv.level_state = sat_state - 32768;
    }
}

void bt_mesh_model_light_hsl_sat_srv_init(struct bt_mesh_model_light_hsl_sat_srv *srv)
{
    if (srv)
    {
        srv->sat_state = 0x0000;
        srv->gen_level_srv.cb = gen_level_state_change_cb;
    }
}
