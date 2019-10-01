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

//=============================================================================
// Globale definitions and variables
//=============================================================================

#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG_MODEL))

//=============================================================================
// Utility functions
//=============================================================================

static struct
bt_mesh_model *find_model(struct bt_mesh_elem *elem, uint16_t id)
{
   if (elem == NULL || elem->model_count == 0 || elem->models == NULL)
      return NULL;

   int j;

   for (j = 0; j < elem->model_count; j++) {
      struct bt_mesh_model *model = &elem->models[j];

      if (model->id == id) {
         return model;
      }
   }

   return NULL;
}

//=============================================================================
// Light HSL Hue Server
//=============================================================================

static void
light_hsl_hue_status(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx)
{
    struct bt_mesh_model_light_hsl_hue_srv *srv = model->user_data;
    struct os_mbuf *msg = NET_BUF_SIMPLE(4);
    uint16_t *hue_state;

    bt_mesh_model_msg_init(msg, OP_LIGHT_HSL_HUE_STATUS);
    hue_state = net_buf_simple_add(msg, 2);
    if (srv) {
        *hue_state = srv->hue_state;
    }

    BT_DBG("hue_state = %u", *hue_state);

    if (bt_mesh_model_send(model, ctx, msg, NULL, NULL)) {
        BT_ERR("Send status failed");
    }

    os_mbuf_free_chain(msg);
}


static void
light_hsl_hue_get(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
                  struct os_mbuf *buf)
{
    light_hsl_hue_status(model, ctx);
}


static void
light_hsl_hue_set_unack(struct bt_mesh_model *model,
                        struct bt_mesh_msg_ctx *ctx, struct os_mbuf *buf)
{
   struct bt_mesh_model_light_hsl_hue_srv *srv = model->user_data;
   uint16_t hue_state;
   uint8_t tid;

   hue_state = (uint16_t) net_buf_simple_pull_le16(buf);
   tid = net_buf_simple_pull_u8(buf);

   BT_DBG("hue_state = %u tid = %u", hue_state, tid);

   if (srv && srv->cb) {
      bt_mesh_model_light_hsl_hue_state_update(srv, hue_state);
      srv->cb(model);
   }
}


static void
light_hsl_hue_set(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
                  struct os_mbuf *buf)
{
   light_hsl_hue_set_unack(model, ctx, buf);
   light_hsl_hue_status(model, ctx);
}


const struct bt_mesh_model_op light_hsl_hue_srv_op[] = {
   { OP_LIGHT_HSL_HUE_GET,        0, light_hsl_hue_get },
   { OP_LIGHT_HSL_HUE_SET,        3, light_hsl_hue_set },
   { OP_LIGHT_HSL_HUE_SET_UNACK,  3, light_hsl_hue_set_unack },
   BT_MESH_MODEL_OP_END,
};


//=============================================================================
// Generic Level Server
//=============================================================================

static void
gen_level_state_change_cb (struct bt_mesh_model *model)
{
    if (model && model->user_data) {
        struct bt_mesh_model_gen_level_srv *level_srv = model->user_data;
        struct bt_mesh_model *hue_model = find_model(bt_mesh_model_elem(model),
                                    BT_MESH_MODEL_ID_LIGHT_HSL_HUE_SRV);

        if (hue_model && hue_model->user_data) {
            struct bt_mesh_model_light_hsl_hue_srv *srv = hue_model->user_data;

            srv->hue_state = level_srv->level_state + 32768;

            BT_DBG("Light HSL Hue state changed = %d", srv->hue_state);

            if (srv->cb)
                srv->cb(hue_model);

            /* From this point onwards light hsl_hue status needs to be send to
            the publish group.
            bt_mesh_model_publish(hue_model);
            */
        }
    }
}


//=============================================================================
// Exposed Interfaces
//=============================================================================

void
bt_mesh_model_light_hsl_hue_state_update(
    struct bt_mesh_model_light_hsl_hue_srv *srv, uint16_t hue_state)
{
    if (srv) {
        srv->hue_state = hue_state;
        srv->gen_level_srv.level_state = hue_state - 32768;
    }
}


void bt_mesh_model_light_hsl_hue_srv_init(
    struct bt_mesh_model_light_hsl_hue_srv *srv)
{
    if (srv) {
        srv->hue_state = 0x0000;
        srv->gen_level_srv.cb = gen_level_state_change_cb;
    }
}
