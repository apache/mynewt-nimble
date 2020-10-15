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
// Light HSL Server
//=============================================================================

static void
light_hsl_status(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx)
{
    struct bt_mesh_model_light_hsl_srv *srv = model->user_data;
    struct os_mbuf *msg = NET_BUF_SIMPLE(4);
    uint16_t *lightness;
    uint16_t *hue;
    uint16_t *saturation;

    bt_mesh_model_msg_init(msg, OP_LIGHT_HSL_STATUS);
    lightness = net_buf_simple_add(msg, 2);
    hue = net_buf_simple_add(msg, 2);
    saturation = net_buf_simple_add(msg, 2);

    if (srv && lightness && hue && saturation) {
        *lightness = srv->lightness_srv.actual_state;
        *hue = srv->hue_srv.hue_state;
        *saturation = srv->sat_srv.sat_state;
    }

    BT_DBG("hsl_state.lightness = %u", *lightness);
    BT_DBG("hsl_state.hue = %u", *hue);
    BT_DBG("hsl_state.saturation = %u", *saturation);

    if (bt_mesh_model_send(model, ctx, msg, NULL, NULL)) {
        BT_ERR("Send status failed");
    }

    os_mbuf_free_chain(msg);
}


static void
light_hsl_get(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
    light_hsl_status(model, ctx);
}


static void
light_hsl_set_unack(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
                    struct os_mbuf *buf)
{
    struct bt_mesh_model_light_hsl_srv *srv = model->user_data;
    uint8_t tid;
    uint16_t lightness, hue, saturation;

    lightness = (uint16_t) net_buf_simple_pull_le16(buf);
    hue = (uint16_t) net_buf_simple_pull_le16(buf);
    saturation = (uint16_t) net_buf_simple_pull_le16(buf);
    tid = net_buf_simple_pull_u8(buf);

    if (srv && srv->cb) {
        bt_mesh_model_light_hsl_state_update(srv, lightness, hue, saturation);

        BT_DBG("hsl_state.lightness = %u", lightness);
        BT_DBG("hsl_state.hue = %u", hue);
        BT_DBG("hsl_state.saturation = %u", saturation);
        BT_DBG("tid = %u", tid);

        srv->cb(model);
    }
}

static void
light_hsl_set(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
   light_hsl_set_unack(model, ctx, buf);
   light_hsl_status(model, ctx);
}


const struct bt_mesh_model_op light_hsl_srv_op[] = {
   { OP_LIGHT_HSL_GET,        0, light_hsl_get },
   { OP_LIGHT_HSL_SET,        7, light_hsl_set },
   { OP_LIGHT_HSL_SET_UNACK,  7, light_hsl_set_unack },
   BT_MESH_MODEL_OP_END,
};

//=============================================================================
// Lightness Server Callback
//=============================================================================

static void
lightness_state_change_cb (struct bt_mesh_model *model)
{
    if (model && model->user_data) {
        struct bt_mesh_model_light_lightness_srv *lightness_srv = model->user_data;
        struct bt_mesh_model *hsl_model = find_model(
                                      bt_mesh_model_elem(model),
                                      BT_MESH_MODEL_ID_LIGHT_HSL_SRV
                                   );

        if (hsl_model && hsl_model->user_data) {
            struct bt_mesh_model_light_hsl_srv *srv = hsl_model->user_data;
            BT_DBG("Light HSL lightness state changed = %d",
                lightness_srv->actual_state);

            if (srv->cb)
                srv->cb(hsl_model);

            /* From this point onwards light hsl status needs to be send to
            the publish group.
            bt_mesh_model_publish(ll_model);
            */
        }
    }
}

//=============================================================================
// HSL Hue Server Callback
//=============================================================================

static void
hue_state_change_cb (struct bt_mesh_model *model)
{
    if (model && model->user_data) {
        struct bt_mesh_model_light_hsl_hue_srv *hue_srv = model->user_data;
        struct bt_mesh_model *hsl_model = find_model(
                                      bt_mesh_model_elem(model),
                                      BT_MESH_MODEL_ID_LIGHT_HSL_SRV
                                   );

        if (hsl_model && hsl_model->user_data) {
            struct bt_mesh_model_light_hsl_srv *srv = hsl_model->user_data;
            BT_DBG("Light HSL Hue state changed = %d", hue_srv->hue_state);

            if (srv->cb)
                srv->cb(hsl_model);

            /* From this point onwards light hsl status needs to be send to
            the publish group.
            bt_mesh_model_publish(ll_model);
            */
        }
    }
}

//=============================================================================
// HSL Saturation Server Callback
//=============================================================================

static void
sat_state_change_cb (struct bt_mesh_model *model)
{
    if (model && model->user_data) {
        struct bt_mesh_model_light_hsl_sat_srv *sat_srv = model->user_data;
        struct bt_mesh_model *hsl_model = find_model(
                                      bt_mesh_model_elem(model),
                                      BT_MESH_MODEL_ID_LIGHT_HSL_SRV
                                   );

        if (hsl_model && hsl_model->user_data) {
            struct bt_mesh_model_light_hsl_srv *srv = hsl_model->user_data;
            BT_DBG("Light HSL lightness state changed = %d",
                sat_srv->sat_state);

            if (srv->cb)
                srv->cb(hsl_model);

            /* From this point onwards light hsl status needs to be send to
            the publish group.
            bt_mesh_model_publish(hsl_model);
            */
        }
    }
}


//=============================================================================
// Exposed Interfaces
//=============================================================================

void
bt_mesh_model_light_hsl_state_update(struct bt_mesh_model_light_hsl_srv *srv,
                                    uint16_t lightness, uint16_t hue,
                                    uint16_t saturation)
{
    if (srv) {
        bt_mesh_model_light_lightness_actual_state_update(
            &(srv->lightness_srv),
            lightness
        );

        bt_mesh_model_light_hsl_hue_state_update(
            &(srv->hue_srv),
            hue
        );

        bt_mesh_model_light_hsl_sat_state_update(
            &(srv->sat_srv),
            saturation
        );
    }
}

void
bt_mesh_model_light_hsl_srv_init(struct bt_mesh_model_light_hsl_srv *srv)
{
    if (srv) {
        srv->lightness_srv.cb = lightness_state_change_cb;
        srv->hue_srv.cb = hue_state_change_cb;
        srv->sat_srv.cb = sat_state_change_cb;

        srv->hsl_hue_range_min_state = 0x0000;
        srv->hsl_hue_range_max_state = 0xffff;
        srv->hsl_hue_default_state = 0x0000;
        srv->hsl_sat_range_min_state = 0x0000;
        srv->hsl_sat_range_max_state = 0xffff;
        srv->hsl_sat_default_state = 0x0000;

        bt_mesh_model_light_lightness_srv_init(&(srv->lightness_srv));
        bt_mesh_model_light_hsl_hue_srv_init(&(srv->hue_srv));
        bt_mesh_model_light_hsl_sat_srv_init(&(srv->sat_srv));
    }
}
