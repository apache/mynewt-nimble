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

#ifndef BT_MESH_HELPER
#define BT_MESH_HELPER

#include "mesh/mesh.h"
#include "syscfg/syscfg.h"

#define CID_VENDOR  0x05C3

#define MAX_OF_3(a,b,c) (((a) > (b)) ? ( ((a) > (c)) ? (a) : (c) ) : ( ((b) > (c)) ? (b) : (c) ))
#define MIN_OF_3(a,b,c) (((a) > (b)) ? ( ((b) > (c)) ? (c) : (b) ) : ( ((a) > (c)) ? (c) : (a) ))

struct rgbw_color_format {
    uint16_t red;
    uint16_t green;
    uint16_t blue;
    uint16_t white;
};

struct hsl_color_format {
   uint16_t lightness;
   uint16_t hue;
   uint16_t saturation;
};

struct network {
   u16_t local;
   u16_t dst;
   u16_t net_idx;
   u16_t app_idx;
};

struct bt_mesh_model *find_model(struct bt_mesh_elem *elem, uint16_t id);

void rgb_to_rgbw(uint16_t *red, uint16_t *green, uint16_t *blue, uint16_t *white);

void hsl_to_rgbw(struct hsl_color_format *hsl, struct rgbw_color_format *rgbw);

void bt_mesh_provisioning_info_init(struct bt_mesh_prov *prov, struct network *net);

void bt_mesh_health_model_srv_init(struct bt_mesh_health_srv *health_srv, struct bt_mesh_model_pub *health_pub);

void bt_mesh_cfg_model_srv_init(struct bt_mesh_cfg_srv *cfg_srv);

#endif
