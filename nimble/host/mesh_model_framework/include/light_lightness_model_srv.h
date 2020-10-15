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

#ifndef __LIGHT_LIGHTNESS_MODEL_SRV_H__
#define __LIGHT_LIGHTNESS_MODEL_SRV_H__

#include "mesh/mesh.h"
#include "model.h"

extern const struct bt_mesh_model_op light_lightness_srv_op[];

typedef void (*light_lightness_actual_state_change_cb) (
   struct bt_mesh_model *model
);

#define BT_MESH_MODEL_LIGHT_LIGHTNESS_SRV(srv, pub)      \
   BT_MESH_MODEL(BT_MESH_MODEL_ID_LIGHT_LIGHTNESS_SRV,   \
            light_lightness_srv_op, pub, srv)

struct bt_mesh_model_light_lightness_srv {
   /* extended models/states */
   struct bt_mesh_model_gen_onoff_srv gen_onoff_srv;
   struct bt_mesh_model_gen_level_srv gen_level_srv;

   uint16_t actual_state;
   uint16_t linear_state;
   uint16_t last_state;
   uint16_t default_state;
   struct bt_mesh_model_pub pub;
   light_lightness_actual_state_change_cb cb;
};

void bt_mesh_model_light_lightness_srv_init(
    struct bt_mesh_model_light_lightness_srv *srv
);

void bt_mesh_model_light_lightness_actual_state_update(
    struct bt_mesh_model_light_lightness_srv *srv,
    uint16_t actual_state
);

#endif