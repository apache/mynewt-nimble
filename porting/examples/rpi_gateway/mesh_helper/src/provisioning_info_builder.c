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

#include "bt_mesh_helper.h"

static struct network *p_net = NULL;

static u8_t dev_uuid[16] = MYNEWT_VAL(BLE_MESH_DEV_UUID);

static void
prov_complete(u16_t net_idx, u16_t addr)
{
   BT_INFO("Local node provisioned, net_idx 0x%04x address 0x%04x\n",
          net_idx, addr);

   if (p_net) {
       p_net->net_idx = net_idx,
       p_net->local = addr;
       p_net->dst = addr;
   }
}

static void
prov_reset(void)
{
   BT_INFO("The local node has been reset and needs reprovisioning\n");
}

static int
output_number(bt_mesh_output_action_t action, uint32_t number)
{
   BT_INFO("OOB Number: %u\n", number);
   return 0;
}

static int
output_string(const char *str)
{
   BT_INFO("OOB String: %s\n", str);
   return 0;
}

static bt_mesh_input_action_t input_act;
static u8_t input_size;

static const char *
bearer2str(bt_mesh_prov_bearer_t bearer)
{
   switch (bearer) {
   case BT_MESH_PROV_ADV:
      return "PB-ADV";
   case BT_MESH_PROV_GATT:
      return "PB-GATT";
   default:
      return "unknown";
   }
}

static void
link_open(bt_mesh_prov_bearer_t bearer)
{
   BT_INFO("Provisioning link opened on %s\n", bearer2str(bearer));
}

static void
link_close(bt_mesh_prov_bearer_t bearer)
{
   BT_INFO("Provisioning link closed on %s\n", bearer2str(bearer));
}

static int
input(bt_mesh_input_action_t act, u8_t size)
{
   switch (act) {
   case BT_MESH_ENTER_NUMBER:
      BT_INFO("Enter a number (max %u digits) with: input-num <num>\n",
             size);
      break;
   case BT_MESH_ENTER_STRING:
      BT_INFO("Enter a string (max %u chars) with: input-str <str>\n",
             size);
      break;
   default:
      BT_INFO("Unknown input action %u (size %u) requested!\n",
             act, size);
      return -EINVAL;
   }

   input_act = act;
   input_size = size;
   return 0;
}

void
bt_mesh_provisioning_info_init(struct bt_mesh_prov *prov, struct network *net)
{
   if (prov == NULL)
      return;

   p_net = net;

   prov->uuid = dev_uuid;
   prov->link_open = link_open;
   prov->link_close = link_close;
   prov->complete = prov_complete;
   prov->reset = prov_reset;
   prov->static_val = NULL;
   prov->static_val_len = 0;
   prov->output_size = MYNEWT_VAL(BLE_MESH_OOB_OUTPUT_SIZE);
   prov->output_actions = MYNEWT_VAL(BLE_MESH_OOB_OUTPUT_ACTIONS);
   prov->output_number = output_number;
   prov->output_string = output_string;
   prov->input_size = MYNEWT_VAL(BLE_MESH_OOB_INPUT_SIZE);
   prov->input_actions = MYNEWT_VAL(BLE_MESH_OOB_INPUT_ACTIONS);
   prov->input = input;

   return;
}
