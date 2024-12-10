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

#include <assert.h>
#include "mesh/mesh.h"
#include "console/console.h"

/* BLE */
#include "nimble/nimble_port.h"
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "mesh/glue.h"
#include "shell/shell.h"
#include "console/console_port.h"

#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG))

/* Company ID */
#define CID_VENDOR 0x05C3
#define STANDARD_TEST_ID 0x00
#define TEST_ID 0x01
static int recent_test_id = STANDARD_TEST_ID;

#define FAULT_ARR_SIZE 2

static bool has_reg_fault = true;

static int
fault_get_cur(struct bt_mesh_model *model,
              uint8_t *test_id,
              uint16_t *company_id,
              uint8_t *faults,
              uint8_t *fault_count)
{
    uint8_t reg_faults[FAULT_ARR_SIZE] = { [0 ... FAULT_ARR_SIZE-1] = 0xff };

    console_printf("fault_get_cur() has_reg_fault %u\n", has_reg_fault);

    *test_id = recent_test_id;
    *company_id = CID_VENDOR;

    *fault_count = MIN(*fault_count, sizeof(reg_faults));
    memcpy(faults, reg_faults, *fault_count);

    return 0;
}

static int
fault_get_reg(struct bt_mesh_model *model,
              uint16_t company_id,
              uint8_t *test_id,
              uint8_t *faults,
              uint8_t *fault_count)
{
    if (company_id != CID_VENDOR) {
        return -BLE_HS_EINVAL;
    }

    console_printf("fault_get_reg() has_reg_fault %u\n", has_reg_fault);

    *test_id = recent_test_id;

    if (has_reg_fault) {
        uint8_t reg_faults[FAULT_ARR_SIZE] = { [0 ... FAULT_ARR_SIZE-1] = 0xff };

        *fault_count = MIN(*fault_count, sizeof(reg_faults));
        memcpy(faults, reg_faults, *fault_count);
    } else {
        *fault_count = 0;
    }

    return 0;
}

static int
fault_clear(struct bt_mesh_model *model, uint16_t company_id)
{
    if (company_id != CID_VENDOR) {
        return -BLE_HS_EINVAL;
    }

    has_reg_fault = false;

    return 0;
}

static int
fault_test(struct bt_mesh_model *model, uint8_t test_id, uint16_t company_id)
{
    if (company_id != CID_VENDOR) {
        return -BLE_HS_EINVAL;
    }

    if (test_id != STANDARD_TEST_ID && test_id != TEST_ID) {
        return -BLE_HS_EINVAL;
    }

    recent_test_id = test_id;
    has_reg_fault = true;
    bt_mesh_fault_update(bt_mesh_model_elem(model));

    return 0;
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
    .fault_get_cur = &fault_get_cur,
    .fault_get_reg = &fault_get_reg,
    .fault_clear = &fault_clear,
    .fault_test = &fault_test,
};

static struct bt_mesh_health_srv health_srv = {
    .cb = &health_srv_cb,
};

static struct bt_mesh_model_pub health_pub;

static void
health_pub_init(void)
{
    health_pub.msg = BT_MESH_HEALTH_FAULT_MSG(0);
}

static struct bt_mesh_model_pub gen_level_pub;
static struct bt_mesh_model_pub gen_onoff_pub;

static uint8_t gen_on_off_state;
static int16_t gen_level_state;

static int
gen_onoff_status(struct bt_mesh_model *model,
                 struct bt_mesh_msg_ctx *ctx)
{
    struct os_mbuf *msg = NET_BUF_SIMPLE(3);
    uint8_t *status;
    int rc;

    console_printf("#mesh-onoff STATUS\n");

    bt_mesh_model_msg_init(msg, BT_MESH_MODEL_OP_2(0x82, 0x04));
    status = net_buf_simple_add(msg, 1);
    *status = gen_on_off_state;

    rc = bt_mesh_model_send(model, ctx, msg, NULL, NULL);
    if (rc) {
        console_printf("#mesh-onoff STATUS: send status failed\n");
    }

    os_mbuf_free_chain(msg);
    return rc;
}

static int
gen_onoff_get(struct bt_mesh_model *model,
              struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
    console_printf("#mesh-onoff GET\n");

    return gen_onoff_status(model, ctx);
}

static int
gen_onoff_set(struct bt_mesh_model *model,
              struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
    console_printf("#mesh-onoff SET\n");

    gen_on_off_state = buf->om_data[0];

    return gen_onoff_status(model, ctx);
}

static int
gen_onoff_set_unack(struct bt_mesh_model *model,
                    struct bt_mesh_msg_ctx *ctx,
                    struct os_mbuf *buf)
{
    console_printf("#mesh-onoff SET-UNACK\n");

    gen_on_off_state = buf->om_data[0];
    return 0;
}

static const struct bt_mesh_model_op gen_onoff_op[] = {
    { BT_MESH_MODEL_OP_2(0x82, 0x01), 0, gen_onoff_get },
    { BT_MESH_MODEL_OP_2(0x82, 0x02), 2, gen_onoff_set },
    { BT_MESH_MODEL_OP_2(0x82, 0x03), 2, gen_onoff_set_unack },
    BT_MESH_MODEL_OP_END,
};

static void
gen_level_status(struct bt_mesh_model *model,
                 struct bt_mesh_msg_ctx *ctx)
{
    struct os_mbuf *msg = NET_BUF_SIMPLE(4);

    console_printf("#mesh-level STATUS\n");

    bt_mesh_model_msg_init(msg, BT_MESH_MODEL_OP_2(0x82, 0x08));
    net_buf_simple_add_le16(msg, gen_level_state);

    if (bt_mesh_model_send(model, ctx, msg, NULL, NULL)) {
        console_printf("#mesh-level STATUS: send status failed\n");
    }

    os_mbuf_free_chain(msg);
}

static int
gen_level_get(struct bt_mesh_model *model,
              struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
    console_printf("#mesh-level GET\n");

    gen_level_status(model, ctx);
    return 0;
}

static int
gen_level_set(struct bt_mesh_model *model,
              struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
    int16_t level;

    level = (int16_t) net_buf_simple_pull_le16(buf);
    console_printf("#mesh-level SET: level=%d\n", level);

    gen_level_status(model, ctx);

    gen_level_state = level;
    console_printf("#mesh-level: level=%d\n", gen_level_state);
    return 0;
}

static int
gen_level_set_unack(struct bt_mesh_model *model,
                    struct bt_mesh_msg_ctx *ctx,
                    struct os_mbuf *buf)
{
    int16_t level;

    level = (int16_t) net_buf_simple_pull_le16(buf);
    console_printf("#mesh-level SET-UNACK: level=%d\n", level);

    gen_level_state = level;
    console_printf("#mesh-level: level=%d\n", gen_level_state);
    return 0;
}

static int
gen_delta_set(struct bt_mesh_model *model,
              struct bt_mesh_msg_ctx *ctx,
              struct os_mbuf *buf)
{
    int16_t delta_level;

    delta_level = (int16_t) net_buf_simple_pull_le16(buf);
    console_printf("#mesh-level DELTA-SET: delta_level=%d\n", delta_level);

    gen_level_status(model, ctx);

    gen_level_state += delta_level;
    console_printf("#mesh-level: level=%d\n", gen_level_state);
    return 0;
}

static int
gen_delta_set_unack(struct bt_mesh_model *model,
                    struct bt_mesh_msg_ctx *ctx,
                    struct os_mbuf *buf)
{
    int16_t delta_level;

    delta_level = (int16_t) net_buf_simple_pull_le16(buf);
    console_printf("#mesh-level DELTA-SET: delta_level=%d\n", delta_level);

    gen_level_state += delta_level;
    console_printf("#mesh-level: level=%d\n", gen_level_state);
    return 0;
}

static int
gen_move_set(struct bt_mesh_model *model,
             struct bt_mesh_msg_ctx *ctx,
             struct os_mbuf *buf)
{
    return 0;
}

static int
gen_move_set_unack(struct bt_mesh_model *model,
                   struct bt_mesh_msg_ctx *ctx,
                   struct os_mbuf *buf)
{
    return 0;
}

static const struct bt_mesh_model_op gen_level_op[] = {
    { BT_MESH_MODEL_OP_2(0x82, 0x05), 0, gen_level_get },
    { BT_MESH_MODEL_OP_2(0x82, 0x06), 3, gen_level_set },
    { BT_MESH_MODEL_OP_2(0x82, 0x07), 3, gen_level_set_unack },
    { BT_MESH_MODEL_OP_2(0x82, 0x09), 5, gen_delta_set },
    { BT_MESH_MODEL_OP_2(0x82, 0x0a), 5, gen_delta_set_unack },
    { BT_MESH_MODEL_OP_2(0x82, 0x0b), 3, gen_move_set },
    { BT_MESH_MODEL_OP_2(0x82, 0x0c), 3, gen_move_set_unack },
    BT_MESH_MODEL_OP_END,
};

static struct bt_mesh_model root_models[] = {
    BT_MESH_MODEL_CFG_SRV,
    BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
    BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_ONOFF_SRV, gen_onoff_op,
              &gen_onoff_pub, NULL),
    BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_LEVEL_SRV, gen_level_op,
              &gen_level_pub, NULL),
};

static struct bt_mesh_model_pub vnd_model_pub;

static int
vnd_model_recv(struct bt_mesh_model *model,
               struct bt_mesh_msg_ctx *ctx,
               struct os_mbuf *buf)
{
    struct os_mbuf *msg = NET_BUF_SIMPLE(3);
    int rc;

    console_printf("#vendor-model-recv\n");

    console_printf("data:%s len:%d\n", bt_hex(buf->om_data, buf->om_len),
                   buf->om_len);

    bt_mesh_model_msg_init(msg, BT_MESH_MODEL_OP_3(0x01, CID_VENDOR));
    os_mbuf_append(msg, buf->om_data, buf->om_len);

    rc = bt_mesh_model_send(model, ctx, msg, NULL, NULL);
    if (rc) {
        console_printf("#vendor-model-recv: send rsp failed\n");
    }

    os_mbuf_free_chain(msg);
    return rc;
}

static const struct bt_mesh_model_op vnd_model_op[] = {
    { BT_MESH_MODEL_OP_3(0x01, CID_VENDOR), 0, vnd_model_recv },
    BT_MESH_MODEL_OP_END,
};

static struct bt_mesh_model vnd_models[] = {
    BT_MESH_MODEL_VND(CID_VENDOR, BT_MESH_MODEL_ID_GEN_ONOFF_SRV, vnd_model_op,
              &vnd_model_pub, NULL),
};

static struct bt_mesh_elem elements[] = {
    BT_MESH_ELEM(0, root_models, vnd_models),
};

static const struct bt_mesh_comp comp = {
    .cid = CID_VENDOR,
    .elem = elements,
    .elem_count = ARRAY_SIZE(elements),
};

static int
output_number(bt_mesh_output_action_t action, uint32_t number)
{
    console_printf("!!! app laer: output OOB Number: %u\n", number);

    return 0;
}

static void
prov_complete(uint16_t net_idx, uint16_t addr)
{
    console_printf("Local node provisioned, primary address 0x%04x\n", addr);
}

static const uint8_t dev_uuid[16] = MYNEWT_VAL(BLE_MESH_DEV_UUID);

static const struct bt_mesh_prov prov = {
    .uuid = dev_uuid,
    .output_size = 0,
    .output_actions = 0,
    .output_number = output_number,
    .complete = prov_complete,
};

static void
blemesh_on_reset(int reason)
{
    BLE_HS_LOG(ERROR, "Resetting state; reason=%d\n", reason);
}

void mesh_initialized(void);


#include "mesh/glue.h"
#include "mesh/testing.h"


void
net_recv_ev(uint8_t ttl, uint8_t ctl, uint16_t src, uint16_t dst,
            const void *payload, size_t payload_len)
{
    console_printf("Received net packet: ttl 0x%02x ctl 0x%02x src 0x%04x "
                   "dst 0x%04x " "payload_len %d\n", ttl, ctl, src, dst,
                   payload_len);
}

static void
model_bound_cb(uint16_t addr, struct bt_mesh_model *model,
               uint16_t key_idx)
{
    console_printf("Model bound: remote addr 0x%04x key_idx 0x%04x model %p\n",
                   addr, key_idx, model);
}

static void
model_unbound_cb(uint16_t addr, struct bt_mesh_model *model,
                 uint16_t key_idx)
{
    console_printf("Model unbound: remote addr 0x%04x key_idx 0x%04x "
                   "model %p\n", addr, key_idx, model);
}

static void
invalid_bearer_cb(uint8_t opcode)
{
    console_printf("Invalid bearer: opcode 0x%02x\n", opcode);
}

static void
incomp_timer_exp_cb(void)
{
    console_printf("Incomplete timer expired\n");
}

static struct bt_test_cb bt_test_cb = {
    .mesh_net_recv = net_recv_ev,
    .mesh_model_bound = model_bound_cb,
    .mesh_model_unbound = model_unbound_cb,
    .mesh_prov_invalid_bearer = invalid_bearer_cb,
    .mesh_trans_incomp_timer_exp = incomp_timer_exp_cb,
};


static void
blemesh_on_sync(void)
{
    int err;

    console_printf("Bluetooth initialized\n");

    console_printf("Init mesh and shell task now.\n");
    ble_mesh_shell_init();

    /* init mesh node. it call bt_mesh_init. */
    cmd_mesh_init(0, NULL);

    console_printf("Start mesh adv task.\n");
    mesh_initialized();

    if (IS_ENABLED(CONFIG_BT_TESTING)) {
        bt_test_cb_register(&bt_test_cb);
    }

    console_init();
}

void
nimble_host_task(void *param)
{
    health_pub_init();

    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = blemesh_on_reset;
    ble_hs_cfg.sync_cb = blemesh_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    nimble_port_run();
}
