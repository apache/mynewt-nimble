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
#include "light_model.h"
#include "bt_mesh_node.h"
#include "mesh_helper/include/bt_mesh_helper.h"
#include "console/console.h"

//=============================================================================
// Definations & Configurations
//=============================================================================

/* Vendor Model data */
#define VND_MODEL_ID_1 0x1234

static struct bt_mesh_prov prov;
static struct bt_mesh_cfg_srv cfg_srv;
static struct bt_mesh_health_srv health_srv;
static struct bt_mesh_model_pub health_pub;

static struct bt_mesh_model vnd_models[] = {
   BT_MESH_MODEL_VND(CID_VENDOR, VND_MODEL_ID_1,
           BT_MESH_MODEL_NO_OPS, NULL, NULL),
};

static struct network net = {
   .local = BT_MESH_ADDR_UNASSIGNED,
   .dst = BT_MESH_ADDR_UNASSIGNED,
};

//=============================================================================
// Primary Element
//=============================================================================

static void primary_elem_onoff_state_cb (struct bt_mesh_model *model)
{
   if (model && model->user_data) {
      struct bt_mesh_model_gen_onoff_srv *srv = model->user_data;

      console_printf("Primary element OnOff state changed = %u\n", srv->onoff_state);

      light_model_gen_onoff_set(model, srv->onoff_state);
   }
}

static void primary_elem_level_state_cb (
   struct bt_mesh_model *model)
{
   if (model && model->user_data) {
      struct bt_mesh_model_gen_level_srv *srv = model->user_data;

      console_printf("Primary element Level state changed = %d\n", srv->level_state);

      light_model_gen_level_set(model, srv->level_state);
   }
}

static void primary_elem_ll_actual_state_cb (struct bt_mesh_model *model)
{
   if (model && model->user_data) {
      struct bt_mesh_model_light_lightness_srv *srv = model->user_data;

      console_printf("Primary element Light Lightness Actual state changed = %u\n", srv->actual_state);

      light_model_light_lightness_set(model, srv->actual_state);
   }
}

static struct bt_mesh_model_light_lightness_srv primary_ll_srv = {
   .default_state = 0,
   .cb = primary_elem_ll_actual_state_cb,
   .gen_onoff_srv.onoff_state = 0,
   .gen_onoff_srv.cb = primary_elem_onoff_state_cb,
   .gen_level_srv.level_state = 0,
   .gen_level_srv.cb = primary_elem_level_state_cb
};

static struct bt_mesh_model primery_element_models[] = {
   BT_MESH_MODEL_CFG_SRV(&cfg_srv),
   BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
   BT_MESH_MODEL_GEN_ONOFF_SRV(&primary_ll_srv.gen_onoff_srv, &primary_ll_srv.gen_onoff_srv.pub),
   BT_MESH_MODEL_GEN_LEVEL_SRV(&primary_ll_srv.gen_level_srv, &primary_ll_srv.gen_level_srv.pub),
   BT_MESH_MODEL_LIGHT_LIGHTNESS_SRV(&primary_ll_srv, &primary_ll_srv.pub),
};

//=============================================================================
// Secondary Element
//=============================================================================

static void secondary_elem_ll_actual_state_cb (struct bt_mesh_model *model)
{
   if (model && model->user_data) {
      struct bt_mesh_model_light_lightness_srv *srv = model->user_data;

      console_printf("Secondary element Light Lightness Actual state changed = %u\n", srv->actual_state);
   }
}

static struct bt_mesh_model_light_lightness_srv secondary_ll_srv = {
   .default_state = 0,
   .cb = secondary_elem_ll_actual_state_cb,
};

static struct bt_mesh_model secondary_element_models[] = {
   BT_MESH_MODEL_GEN_ONOFF_SRV(&secondary_ll_srv.gen_onoff_srv, &secondary_ll_srv.gen_onoff_srv.pub),
   BT_MESH_MODEL_GEN_LEVEL_SRV(&secondary_ll_srv.gen_level_srv, &secondary_ll_srv.gen_level_srv.pub),
   BT_MESH_MODEL_LIGHT_LIGHTNESS_SRV(&secondary_ll_srv, &secondary_ll_srv.pub),
};

//=============================================================================
// Global Definations & Configurations
//=============================================================================

static struct bt_mesh_elem elements[] = {
   BT_MESH_ELEM(0, primery_element_models, vnd_models),
   BT_MESH_ELEM(0, secondary_element_models, vnd_models),
};

static const struct bt_mesh_comp comp = {
   .cid = CID_VENDOR,
   .elem = elements,
   .elem_count = ARRAY_SIZE(elements),
};

//=============================================================================
// Initialization
//=============================================================================

void bt_mesh_node_init(void)
{
   int err;
   ble_addr_t addr;

   /* Initialize light hardware */
   light_model_init();

   /* Initialize node configuration */
   bt_mesh_cfg_model_srv_init(&cfg_srv);

   /* Initialize health pub message */
   bt_mesh_health_model_srv_init(&health_srv, &health_pub);

   /* As of now, all controller boards have same public address.
    * Therefore, mobile phone app can list only one node. In order to
    * differentiat and list all the nodes we are going with ramdom
    * address. Hence, using NRPA.
   */
   err = ble_hs_id_gen_rnd(1, &addr);
   assert(err == 0);
   err = ble_hs_id_set_rnd(addr.val);
   assert(err == 0);

   bt_mesh_provisioning_info_init(&prov, &net);

   bt_mesh_model_light_lightness_srv_init(&primary_ll_srv);
   bt_mesh_model_light_lightness_srv_init(&secondary_ll_srv);

   err = bt_mesh_init(addr.type, &prov, &comp);

   if (err) {
      console_printf("Mesh initialization failed (err %d)\n", err);
   }

   console_printf("Mesh initialized\n");
   console_printf("Use \"pb-adv on\" or \"pb-gatt on\" to enable advertising\n");
}
