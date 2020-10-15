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
// Light Lightness Server
//=============================================================================

static void
light_lightness_status(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx)
{
   struct bt_mesh_model_light_lightness_srv *srv = model->user_data;
   struct os_mbuf *msg = NET_BUF_SIMPLE(4);
   uint16_t *actual_state;

   bt_mesh_model_msg_init(msg, OP_LIGHT_LIGHTNESS_STATUS);
   actual_state = net_buf_simple_add(msg, 2);
   if (srv) {
      *actual_state = srv->actual_state;
   }

   BT_DBG("actual_state = %u", *actual_state);

   if (bt_mesh_model_send(model, ctx, msg, NULL, NULL)) {
      BT_ERR("Send status failed");
   }

   os_mbuf_free_chain(msg);
}


static void
light_lightness_get(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
                    struct os_mbuf *buf)
{
   light_lightness_status(model, ctx);
}

static void
light_lightness_set_unack(struct bt_mesh_model *model,
                          struct bt_mesh_msg_ctx *ctx, struct os_mbuf *buf)
{
   struct bt_mesh_model_light_lightness_srv *srv = model->user_data;
   uint16_t actual_state;
   uint8_t tid;

   actual_state = (uint16_t) net_buf_simple_pull_le16(buf);
   tid = net_buf_simple_pull_u8(buf);

   BT_DBG("actual_state = %u tid = %u", actual_state, tid);

   if (srv && srv->cb) {
      bt_mesh_model_light_lightness_actual_state_update(srv, actual_state);
      srv->cb(model);
   }
}


static void
light_lightness_set(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,

                    struct os_mbuf *buf)
{
   light_lightness_set_unack(model, ctx, buf);
   light_lightness_status(model, ctx);
}

const struct bt_mesh_model_op light_lightness_srv_op[] = {
   { OP_LIGHT_LIGHTNESS_GET,     0, light_lightness_get },
   { OP_LIGHT_LIGHTNESS_SET,     3, light_lightness_set },
   { OP_LIGHT_LIGHTNESS_SET_UNACK,  3, light_lightness_set_unack },
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
      struct bt_mesh_model *ll_model = find_model(
                                          bt_mesh_model_elem(model),
                                          BT_MESH_MODEL_ID_LIGHT_LIGHTNESS_SRV
                                       );

      if (ll_model && ll_model->user_data) {
         struct bt_mesh_model_light_lightness_srv *srv = ll_model->user_data;
         uint16_t actual_state = level_srv->level_state + 32768;

         bt_mesh_model_light_lightness_actual_state_update(srv, actual_state);

         BT_DBG("Light Lightness Actual state changed = %d", actual_state);

         if (srv->cb)
             srv->cb(ll_model);

         /* From this point onwards light lightness status needs to be send to
           the publish group.
           bt_mesh_model_publish(ll_model);
         */
      }
   }
}

//=============================================================================
// Generic OnOff Server
//=============================================================================

static void
gen_onoff_state_change_cb (struct bt_mesh_model *model)
{
   if (model && model->user_data) {
      struct bt_mesh_model_gen_onoff_srv *onoff_srv = model->user_data;
      struct bt_mesh_model *ll_model = find_model(
                                           bt_mesh_model_elem(model),
                                           BT_MESH_MODEL_ID_LIGHT_LIGHTNESS_SRV
                                        );

      if (ll_model && ll_model->user_data) {
         struct bt_mesh_model_light_lightness_srv *srv = ll_model->user_data;

         if (onoff_srv->onoff_state == 0) {
            srv->actual_state = 0;
         } else {
            if (srv->default_state == 0) {
               srv->actual_state = srv->last_state;
            } else {
               srv->actual_state = srv->default_state;
            }
         }

         srv->gen_level_srv.level_state = srv->actual_state - 32768;

         BT_DBG("Light Lightness Actual state changed = %u", srv->actual_state);

         if (srv->cb)
             srv->cb(ll_model);

        /* From this point onwards light lightness status needs to be send to
           the publish group.
           bt_mesh_model_publish(ll_model);
        */
      }
   }
}

//=============================================================================
// Exposed Interfaces
//=============================================================================

void
bt_mesh_model_light_lightness_actual_state_update(
    struct bt_mesh_model_light_lightness_srv *srv, uint16_t actual_state)
{
    if (srv) {
        srv->actual_state = actual_state;
        srv->gen_level_srv.level_state = actual_state - 32768;
        if (actual_state) {
            srv->gen_onoff_srv.onoff_state = 1;
            srv->last_state = actual_state;
        } else {
            srv->gen_onoff_srv.onoff_state = 0;
        }
    }
}

void
bt_mesh_model_light_lightness_srv_init(
    struct bt_mesh_model_light_lightness_srv *srv)
{
    if (srv) {
        srv->actual_state = 0x0000;
        srv->linear_state = 0x0000;
        srv->last_state = 0xffff;
        srv->default_state = 0x0000;
        srv->gen_onoff_srv.cb = gen_onoff_state_change_cb;
        srv->gen_level_srv.cb = gen_level_state_change_cb;
    }
}
