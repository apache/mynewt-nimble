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

#define BT_INFO_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG_MODEL))

#define CID_NVAL   0xffff
#define CUR_FAULTS_MAX 4

static u8_t cur_faults[CUR_FAULTS_MAX];
static u8_t reg_faults[CUR_FAULTS_MAX * 2];

static void
get_faults(u8_t *faults, u8_t faults_size, u8_t *dst, u8_t *count)
{
   u8_t i, limit = *count;

   for (i = 0, *count = 0; i < faults_size && *count < limit; i++) {
      if (faults[i]) {
         *dst++ = faults[i];
         (*count)++;
      }
   }
}

static int
fault_get_cur(struct bt_mesh_model *model, u8_t *test_id,
              u16_t *company_id, u8_t *faults, u8_t *fault_count)
{
   BT_INFO("Sending current faults\n");

   *test_id = 0x00;
   *company_id = CID_VENDOR;

   get_faults(cur_faults, sizeof(cur_faults), faults, fault_count);

   return 0;
}

static int
fault_get_reg(struct bt_mesh_model *model, u16_t cid, u8_t *test_id,
              u8_t *faults, u8_t *fault_count)
{
   if (cid != CID_VENDOR) {
      BT_INFO("Faults requested for unknown Company ID 0x%04x\n", cid);
      return -EINVAL;
   }

   BT_INFO("Sending registered faults\n");

   *test_id = 0x00;

   get_faults(reg_faults, sizeof(reg_faults), faults, fault_count);

   return 0;
}

static int
fault_clear(struct bt_mesh_model *model, uint16_t cid)
{
   if (cid != CID_VENDOR) {
      return -EINVAL;
   }

   memset(reg_faults, 0, sizeof(reg_faults));

   return 0;
}

static int
fault_test(struct bt_mesh_model *model, uint8_t test_id, uint16_t cid)
{
   if (cid != CID_VENDOR) {
      return -EINVAL;
   }

   if (test_id != 0x00) {
      return -EINVAL;
   }

   return 0;
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
   .fault_get_cur = fault_get_cur,
   .fault_get_reg = fault_get_reg,
   .fault_clear = fault_clear,
   .fault_test = fault_test,
};


void
bt_mesh_health_model_srv_init(struct bt_mesh_health_srv *health_srv,
                              struct bt_mesh_model_pub *health_pub)
{
   if (health_srv == NULL)
      return;

   health_srv->cb = &health_srv_cb;

   if (health_pub == NULL)
      return;

   health_pub->msg  = BT_MESH_HEALTH_FAULT_MSG(CUR_FAULTS_MAX);

   return;
}