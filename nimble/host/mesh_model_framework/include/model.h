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

#ifndef __MODEL_H__
#define __MODEL_H__

#include "gen_level_model_cli.h"
#include "gen_level_model_srv.h"
#include "gen_onoff_model_cli.h"
#include "gen_onoff_model_srv.h"

#include "light_lightness_model_srv.h"
#include "light_hsl_hue_model_srv.h"
#include "light_hsl_sat_model_srv.h"
#include "light_hsl_model_srv.h"

#include "sensor_model_srv.h"

#define BT_MESH_KEY_PRIMARY 0x0000
#define BT_MESH_KEY_ANY     0xffff

#define BT_MESH_ADDR_IS_UNICAST(addr)   ((addr) && (addr) < 0x8000)
#define BT_MESH_ADDR_IS_GROUP(addr)     ((addr) >= 0xc000 && (addr) <= 0xff00)
#define BT_MESH_ADDR_IS_VIRTUAL(addr)   ((addr) >= 0x8000 && (addr) < 0xc000)
#define BT_MESH_ADDR_IS_RFU(addr)       ((addr) >= 0xff00 && (addr) <= 0xfffb)

#define OP_GEN_ONOFF_GET                    BT_MESH_MODEL_OP_2(0x82, 0x01)
#define OP_GEN_ONOFF_SET                    BT_MESH_MODEL_OP_2(0x82, 0x02)
#define OP_GEN_ONOFF_SET_UNACK              BT_MESH_MODEL_OP_2(0x82, 0x03)
#define OP_GEN_ONOFF_STATUS                 BT_MESH_MODEL_OP_2(0x82, 0x04)

#define OP_GEN_LEVEL_GET                    BT_MESH_MODEL_OP_2(0x82, 0x05)
#define OP_GEN_LEVEL_SET                    BT_MESH_MODEL_OP_2(0x82, 0x06)
#define OP_GEN_LEVEL_SET_UNACK              BT_MESH_MODEL_OP_2(0x82, 0x07)
#define OP_GEN_LEVEL_STATUS                 BT_MESH_MODEL_OP_2(0x82, 0x08)
#define OP_GEN_DELTA_SET                    BT_MESH_MODEL_OP_2(0x82, 0x09)
#define OP_GEN_DELTA_SET_UNACK              BT_MESH_MODEL_OP_2(0x82, 0x0a)
#define OP_GEN_MOVE_SET                     BT_MESH_MODEL_OP_2(0x82, 0x0b)
#define OP_GEN_MOVE_SET_UNACK               BT_MESH_MODEL_OP_2(0x82, 0x0c)

#define OP_LIGHT_LIGHTNESS_GET              BT_MESH_MODEL_OP_2(0x82, 0x4b)
#define OP_LIGHT_LIGHTNESS_SET              BT_MESH_MODEL_OP_2(0x82, 0x4c)
#define OP_LIGHT_LIGHTNESS_SET_UNACK        BT_MESH_MODEL_OP_2(0x82, 0x4d)
#define OP_LIGHT_LIGHTNESS_STATUS           BT_MESH_MODEL_OP_2(0x82, 0x4e)

#define OP_LIGHT_HSL_HUE_GET                BT_MESH_MODEL_OP_2(0x82, 0x6e)
#define OP_LIGHT_HSL_HUE_SET                BT_MESH_MODEL_OP_2(0x82, 0x6f)
#define OP_LIGHT_HSL_HUE_SET_UNACK          BT_MESH_MODEL_OP_2(0x82, 0x70)
#define OP_LIGHT_HSL_HUE_STATUS             BT_MESH_MODEL_OP_2(0x82, 0x71)

#define OP_LIGHT_HSL_SAT_GET                BT_MESH_MODEL_OP_2(0x82, 0x72)
#define OP_LIGHT_HSL_SAT_SET                BT_MESH_MODEL_OP_2(0x82, 0x73)
#define OP_LIGHT_HSL_SAT_SET_UNACK          BT_MESH_MODEL_OP_2(0x82, 0x74)
#define OP_LIGHT_HSL_SAT_STATUS             BT_MESH_MODEL_OP_2(0x82, 0x75)

#define OP_LIGHT_HSL_GET                    BT_MESH_MODEL_OP_2(0x82, 0x6d)
#define OP_LIGHT_HSL_SET                    BT_MESH_MODEL_OP_2(0x82, 0x76)
#define OP_LIGHT_HSL_SET_UNACK              BT_MESH_MODEL_OP_2(0x82, 0x77)
#define OP_LIGHT_HSL_STATUS                 BT_MESH_MODEL_OP_2(0x82, 0x78)

#define OP_SENSOR_DESCRIPTOR_GET            BT_MESH_MODEL_OP_2(0x82, 0x30)
#define OP_SENSOR_DESCRIPTOR_STATUS         BT_MESH_MODEL_OP_1(0x51)
#define OP_SENSOR_GET                       BT_MESH_MODEL_OP_2(0x82, 0x31)
#define OP_SENSOR_STATUS                    BT_MESH_MODEL_OP_1(0x52)
#define OP_SENSOR_COLUMN_GET                BT_MESH_MODEL_OP_2(0x82, 0x32)
#define OP_SENSOR_COLUMN_STATUS             BT_MESH_MODEL_OP_1(0x53)
#define OP_SENSOR_SERIES_GET                BT_MESH_MODEL_OP_2(0x82, 0x33)
#define OP_SENSOR_SERIES_STATUS             BT_MESH_MODEL_OP_1(0x54)

#endif