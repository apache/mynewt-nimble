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

#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG_MODEL))

static void
gen_onoff_status(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx)
{
    struct bt_mesh_model_gen_onoff_srv *srv = model->user_data;
    struct os_mbuf *msg = NET_BUF_SIMPLE(3);
    u8_t *state;

    bt_mesh_model_msg_init(msg, OP_GEN_ONOFF_STATUS);
    state = net_buf_simple_add(msg, 1);
    if (srv) {
      *state = srv->onoff_state;
    }

    BT_DBG("state = %u", *state);

    if (bt_mesh_model_send(model, ctx, msg, NULL, NULL)) {
      BT_ERR("Send status failed");
    }

    os_mbuf_free_chain(msg);
}

static void
gen_onoff_get(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
    gen_onoff_status(model, ctx);
}

static void
gen_onoff_set_unack(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
                    struct os_mbuf *buf)
{
    struct bt_mesh_model_gen_onoff_srv *srv = model->user_data;
    u8_t state;
    uint8_t tid;

    state = net_buf_simple_pull_u8(buf);
    tid = net_buf_simple_pull_u8(buf);

    BT_DBG("state = %u tid = %u", state, tid);

    if (srv && srv->cb) {
      srv->onoff_state = state;
      srv->cb(model);
    }
}

static void
gen_onoff_set(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
    gen_onoff_set_unack(model, ctx, buf);
    gen_onoff_status(model, ctx);
}

const struct bt_mesh_model_op gen_onoff_srv_op[] = {
    { OP_GEN_ONOFF_GET,     0, gen_onoff_get },
    { OP_GEN_ONOFF_SET,     2, gen_onoff_set },
    { OP_GEN_ONOFF_SET_UNACK,  2, gen_onoff_set_unack },
    BT_MESH_MODEL_OP_END,
};