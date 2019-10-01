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

#ifndef __LIGHT_HSL_MODEL_SRV_H__
#define __LIGHT_HSL_MODEL_SRV_H__

#include "mesh/mesh.h"
#include "model.h"

extern const struct bt_mesh_model_op light_hsl_srv_op[];

typedef void (*light_hsl_actual_state_change_cb) (
   struct bt_mesh_model *model
);

#define BT_MESH_MODEL_LIGHT_HSL_SRV(srv, pub)      \
   BT_MESH_MODEL(BT_MESH_MODEL_ID_LIGHT_HSL_SRV,   \
            light_hsl_srv_op, pub, srv)

struct bt_mesh_model_light_hsl_srv {
   /* extended models/states */
   struct bt_mesh_model_light_lightness_srv lightness_srv;
   struct bt_mesh_model_light_hsl_hue_srv hue_srv;
   struct bt_mesh_model_light_hsl_sat_srv sat_srv;

   /* The Light HSL state is a composite state that includes the Light HSL Hue,
    * the Light HSL Hue Default, the Light HSL Saturation, the Light HSL
    * Saturation Default, and the Light HSL Lightness states.
    */

   uint16_t hsl_hue_range_min_state;
   uint16_t hsl_hue_range_max_state;
   uint16_t hsl_hue_default_state;

   uint16_t hsl_sat_range_min_state;
   uint16_t hsl_sat_range_max_state;
   uint16_t hsl_sat_default_state;

   struct bt_mesh_model_pub pub;
   light_hsl_actual_state_change_cb cb;
};

void bt_mesh_model_light_hsl_srv_init(struct bt_mesh_model_light_hsl_srv *srv);

void bt_mesh_model_light_hsl_state_update(
    struct bt_mesh_model_light_hsl_srv *srv,
    uint16_t lightness,
    uint16_t hue,
    uint16_t saturation
);

#endif