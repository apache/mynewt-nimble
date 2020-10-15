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

#ifndef __LIGHT_HSL_SAT_MODEL_SRV_H__
#define __LIGHT_HSL_SAT_MODEL_SRV_H__

#include "mesh/mesh.h"
#include "model.h"

extern const struct bt_mesh_model_op light_hsl_sat_srv_op[];

typedef void (*light_hsl_sat_state_change_cb) (
    struct bt_mesh_model *model
);

#define BT_MESH_MODEL_LIGHT_HSL_SAT_SRV(srv, pub)       \
    BT_MESH_MODEL(BT_MESH_MODEL_ID_LIGHT_HSL_SAT_SRV,   \
        light_hsl_sat_srv_op, pub, srv)

struct bt_mesh_model_light_hsl_sat_srv {
    /* extended models/states */
    struct bt_mesh_model_gen_level_srv gen_level_srv;
    struct bt_mesh_model_pub pub;
    light_hsl_sat_state_change_cb cb;

    /* The Light HSL Saturation state determines the saturation of a color light emitted by
     * an element. The values for the state are defined in the following table.
     *
     * 0x0000        : The lowest perceived saturation of a color light
     * 0x0001–0xFFFE : The 16-bit value representing the saturation of a color light
     * 0xFFFF        : The highest perceived saturation of a color light
     *
     */
    uint16_t sat_state;
};

void bt_mesh_model_light_hsl_sat_srv_init(
    struct bt_mesh_model_light_hsl_sat_srv *srv
);

void bt_mesh_model_light_hsl_sat_state_update(
    struct bt_mesh_model_light_hsl_sat_srv *srv,
    uint16_t sat_state
);

#endif