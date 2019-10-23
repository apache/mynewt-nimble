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

#include "mesh_helper/include/bt_mesh_helper.h"

void
bt_mesh_cfg_model_srv_init (struct bt_mesh_cfg_srv *cfg_srv)
{
   if (cfg_srv == NULL)
      return;

   cfg_srv->relay = BT_MESH_RELAY_DISABLED;
   cfg_srv->beacon = BT_MESH_BEACON_DISABLED;
   cfg_srv->frnd = BT_MESH_FRIEND_NOT_SUPPORTED;
   cfg_srv->gatt_proxy = BT_MESH_GATT_PROXY_ENABLED;
   cfg_srv->default_ttl = 7;

   /* 3 transmissions with 20ms interval */
   cfg_srv->net_transmit = BT_MESH_TRANSMIT(2, 20);
   cfg_srv->relay_retransmit = BT_MESH_TRANSMIT(2, 20);

   return;
}