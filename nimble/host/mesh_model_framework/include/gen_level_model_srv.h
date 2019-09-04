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

#ifndef __GEN_LEVEL_MODEL_SRV_H__
#define __GEN_LEVEL_MODEL_SRV_H__

#include "mesh/mesh.h"

extern const struct bt_mesh_model_op gen_level_srv_op[];

typedef void (*level_state_change_cb) (
   struct bt_mesh_model *model
);

#define BT_MESH_MODEL_GEN_LEVEL_SRV(srv, pub)      \
   BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_LEVEL_SRV,   \
            gen_level_srv_op, pub, srv)

struct bt_mesh_model_gen_level_srv {
   int16_t level_state;
   struct bt_mesh_model_pub pub;
   level_state_change_cb cb;
};

#endif