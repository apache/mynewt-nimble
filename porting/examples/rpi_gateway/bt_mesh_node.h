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

#ifdef __cplusplus
extern "C" {
#endif

void bt_mesh_node_init(void);

uint32_t gen_onoff_get(uint16_t dst, uint8_t *state);

uint32_t gen_onoff_set(uint16_t dst, uint8_t val);

uint32_t publish_gen_onoff_set(uint8_t state);

uint32_t gen_level_get(uint16_t dst, int16_t *level);

uint32_t gen_level_set(uint16_t dst, uint8_t level);

uint32_t publish_gen_level_set(uint16_t level);

#ifdef __cplusplus
}
#endif
