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

#ifndef H_BT_MESH_STORE_CONFIG_PRIV_
#define H_BT_MESH_STORE_CONFIG_PRIV_

#ifdef __cplusplus
extern "C" {
#endif

//#if MYNEWT_VAL(BT_MESH_STORE_CONFIG_PERSIST)

void bt_mesh_store_config_init(void);
int bt_mesh_store_config_persist_net(void);
int bt_mesh_store_config_persist_subnets(void);
int bt_mesh_store_config_persist_app_keys(void);

//#else
//
//static inline int bt_mesh_store_config_persist_net(void)   { return 0; }
//static inline void bt_mesh_store_config_init(void)         { }
//
//#endif /* MYNEWT_VAL(BT_MESH_STORE_CONFIG_PERSIST) */

#ifdef __cplusplus
}
#endif

#endif
