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

/* mesh.c - Bluetooth Mesh Tester */

/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "syscfg/syscfg.h"

#if MYNEWT_VAL(BLE_MESH)

#include <errno.h>

#include "mesh/mesh.h"
#include "mesh/glue.h"
#include "mesh/testing.h"
#include "console/console.h"

#include "btp/btp.h"

extern uint8_t own_addr_type;

#define CONTROLLER_INDEX 0
#define CID_LOCAL 0x0002

/* Health server data */
#define CUR_FAULTS_MAX 4
#define HEALTH_TEST_ID 0x00

static uint8_t cur_faults[CUR_FAULTS_MAX];
static uint8_t reg_faults[CUR_FAULTS_MAX * 2];

/* Provision node data */
static uint8_t net_key[16];
static uint16_t net_key_idx;
static uint8_t flags;
static uint32_t iv_index;
static uint16_t addr;
static uint8_t dev_key[16];
static uint8_t input_size;

/* Configured provisioning data */
static uint8_t dev_uuid[16];
static uint8_t static_auth[16];

/* Vendor Model data */
#define VND_MODEL_ID_1 0x1234

/* Model send data */
#define MODEL_BOUNDS_MAX 2

static struct model_data {
    struct bt_mesh_model *model;
    uint16_t addr;
    uint16_t appkey_idx;
} model_bound[MODEL_BOUNDS_MAX];

static struct {
    uint16_t local;
    uint16_t dst;
    uint16_t net_idx;
} net = {
    .local = BT_MESH_ADDR_UNASSIGNED,
    .dst = BT_MESH_ADDR_UNASSIGNED,
};

static uint8_t
supported_commands(const void *cmd, uint16_t cmd_len,
                   void *rsp, uint16_t *rsp_len)
{
    struct btp_mesh_read_supported_commands_rp *rp = rsp;

    /* octet 0 */
    tester_set_bit(rp->data, BTP_MESH_READ_SUPPORTED_COMMANDS);
    tester_set_bit(rp->data, BTP_MESH_CONFIG_PROVISIONING);
    tester_set_bit(rp->data, BTP_MESH_PROVISION_NODE);
    tester_set_bit(rp->data, BTP_MESH_INIT);
    tester_set_bit(rp->data, BTP_MESH_RESET);
    tester_set_bit(rp->data, BTP_MESH_INPUT_NUMBER);
    tester_set_bit(rp->data, BTP_MESH_INPUT_STRING);
    /* octet 1 */
    tester_set_bit(rp->data, BTP_MESH_IVU_TEST_MODE);
    tester_set_bit(rp->data, BTP_MESH_IVU_TOGGLE_STATE);
    tester_set_bit(rp->data, BTP_MESH_NET_SEND);
    tester_set_bit(rp->data, BTP_MESH_HEALTH_GENERATE_FAULTS);
    tester_set_bit(rp->data, BTP_MESH_HEALTH_CLEAR_FAULTS);
    tester_set_bit(rp->data, BTP_MESH_LPN);
    tester_set_bit(rp->data, BTP_MESH_LPN_POLL);
    tester_set_bit(rp->data, BTP_MESH_MODEL_SEND);
    /* octet 2 */
#if MYNEWT_VAL(BLE_MESH_TESTING)
    tester_set_bit(rp->data, BTP_MESH_LPN_SUBSCRIBE);
    tester_set_bit(rp->data, BTP_MESH_LPN_UNSUBSCRIBE);
    tester_set_bit(rp->data, BTP_MESH_RPL_CLEAR);
#endif /* CONFIG_BT_TESTING */
    tester_set_bit(rp->data, BTP_MESH_PROXY_IDENTITY);

    *rsp_len = sizeof(*rp) + 3;

    return BTP_STATUS_SUCCESS;
}

static void
get_faults(uint8_t *faults, uint8_t faults_size, uint8_t *dst, uint8_t *count)
{
    uint8_t i, limit = *count;

    for (i = 0, *count = 0; i < faults_size && *count < limit; i++) {
        if (faults[i]) {
            *dst++ = faults[i];
            (*count)++;
        }
    }
}

static int
fault_get_cur(struct bt_mesh_model *model, uint8_t *test_id,
              uint16_t *company_id, uint8_t *faults, uint8_t *fault_count)
{
    SYS_LOG_DBG("");

    *test_id = HEALTH_TEST_ID;
    *company_id = CID_LOCAL;

    get_faults(cur_faults, sizeof(cur_faults), faults, fault_count);

    return 0;
}

static int
fault_get_reg(struct bt_mesh_model *model, uint16_t company_id,
              uint8_t *test_id, uint8_t *faults, uint8_t *fault_count)
{
    SYS_LOG_DBG("company_id 0x%04x", company_id);

    if (company_id != CID_LOCAL) {
        return -EINVAL;
    }

    *test_id = HEALTH_TEST_ID;

    get_faults(reg_faults, sizeof(reg_faults), faults, fault_count);

    return 0;
}

static int
fault_clear(struct bt_mesh_model *model, uint16_t company_id)
{
    SYS_LOG_DBG("company_id 0x%04x", company_id);

    if (company_id != CID_LOCAL) {
        return -EINVAL;
    }

    memset(reg_faults, 0, sizeof(reg_faults));

    return 0;
}

static int
fault_test(struct bt_mesh_model *model, uint8_t test_id,
           uint16_t company_id)
{
    SYS_LOG_DBG("test_id 0x%02x company_id 0x%04x", test_id, company_id);

    if (company_id != CID_LOCAL || test_id != HEALTH_TEST_ID) {
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

static struct bt_mesh_health_srv health_srv = {
    .cb = &health_srv_cb,
};

static struct bt_mesh_model_pub health_pub;

static void
health_pub_init(void)
{
    health_pub.msg = BT_MESH_HEALTH_FAULT_MSG(CUR_FAULTS_MAX);
}

static struct bt_mesh_cfg_cli cfg_cli = {
};

void
show_faults(uint8_t test_id, uint16_t cid, uint8_t *faults, size_t fault_count)
{
    size_t i;

    if (!fault_count) {
        SYS_LOG_DBG("Health Test ID 0x%02x Company ID 0x%04x: "
                    "no faults", test_id, cid);
        return;
    }

    SYS_LOG_DBG("Health Test ID 0x%02x Company ID 0x%04x Fault Count %zu: ",
                test_id, cid, fault_count);

    for (i = 0; i < fault_count; i++) {
        SYS_LOG_DBG("0x%02x", faults[i]);
    }
}

static void
health_current_status(struct bt_mesh_health_cli *cli, uint16_t addr,
                      uint8_t test_id, uint16_t cid, uint8_t *faults,
                      size_t fault_count)
{
    SYS_LOG_DBG("Health Current Status from 0x%04x", addr);
    show_faults(test_id, cid, faults, fault_count);
}

static struct bt_mesh_health_cli health_cli = {
    .current_status = health_current_status,
};

static struct bt_mesh_model root_models[] = {
    BT_MESH_MODEL_CFG_SRV,
    BT_MESH_MODEL_CFG_CLI(&cfg_cli),
    BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
    BT_MESH_MODEL_HEALTH_CLI(&health_cli),
};

static struct bt_mesh_model vnd_models[] = {
    BT_MESH_MODEL_VND(CID_LOCAL, VND_MODEL_ID_1, BT_MESH_MODEL_NO_OPS, NULL,
                      NULL),
};

static struct bt_mesh_elem elements[] = {
    BT_MESH_ELEM(0, root_models, vnd_models),
};

static void
link_open(bt_mesh_prov_bearer_t bearer)
{
    struct btp_mesh_prov_link_open_ev ev;

    SYS_LOG_DBG("bearer 0x%02x", bearer);

    switch (bearer) {
    case BT_MESH_PROV_ADV:
        ev.bearer = BTP_MESH_PROV_BEARER_PB_ADV;
        break;
    case BT_MESH_PROV_GATT:
        ev.bearer = BTP_MESH_PROV_BEARER_PB_GATT;
        break;
    default:
        SYS_LOG_ERR("Invalid bearer");

        return;
    }

    tester_event(BTP_SERVICE_ID_MESH, BTP_MESH_EV_PROV_LINK_OPEN,
                 (uint8_t *) &ev, sizeof(ev));
}

static void
link_close(bt_mesh_prov_bearer_t bearer)
{
    struct btp_mesh_prov_link_closed_ev ev;

    SYS_LOG_DBG("bearer 0x%02x", bearer);

    switch (bearer) {
    case BT_MESH_PROV_ADV:
        ev.bearer = BTP_MESH_PROV_BEARER_PB_ADV;
        break;
    case BT_MESH_PROV_GATT:
        ev.bearer = BTP_MESH_PROV_BEARER_PB_GATT;
        break;
    default:
        SYS_LOG_ERR("Invalid bearer");

        return;
    }

    tester_event(BTP_SERVICE_ID_MESH, BTP_MESH_EV_PROV_LINK_CLOSED,
                 (uint8_t *) &ev, sizeof(ev));
}

static int
output_number(bt_mesh_output_action_t action, uint32_t number)
{
    struct btp_mesh_out_number_action_ev ev;

    SYS_LOG_DBG("action 0x%04x number 0x%08lx", action, number);

    ev.action = htole16(action);
    ev.number = htole32(number);

    tester_event(BTP_SERVICE_ID_MESH, BTP_MESH_EV_OUT_NUMBER_ACTION,
                 (uint8_t *) &ev, sizeof(ev));

    return 0;
}

static int
output_string(const char *str)
{
    struct btp_mesh_out_string_action_ev *ev;
    struct os_mbuf *buf = NET_BUF_SIMPLE(BTP_DATA_MAX_SIZE);

    SYS_LOG_DBG("str %s", str);

    net_buf_simple_init(buf, 0);

    ev = net_buf_simple_add(buf, sizeof(*ev));
    ev->string_len = strlen(str);

    net_buf_simple_add_mem(buf, str, ev->string_len);

    tester_send_buf(BTP_SERVICE_ID_MESH, BTP_MESH_EV_OUT_STRING_ACTION,
                    CONTROLLER_INDEX, buf);

    os_mbuf_free_chain(buf);
    return 0;
}

static int
input(bt_mesh_input_action_t action, uint8_t size)
{
    struct btp_mesh_in_action_ev ev;

    SYS_LOG_DBG("action 0x%04x number 0x%02x", action, size);

    input_size = size;

    ev.action = htole16(action);
    ev.size = size;

    tester_event(BTP_SERVICE_ID_MESH, BTP_MESH_EV_IN_ACTION,
                 (uint8_t *) &ev, sizeof(ev));

    return 0;
}

static uint8_t vnd_app_key[16];
static uint16_t vnd_app_key_idx = 0x000f;

static void
prov_complete(uint16_t net_idx, uint16_t addr)
{
    SYS_LOG_DBG("net_idx 0x%04x addr 0x%04x", net_idx, addr);

    net.net_idx = net_idx,
    net.local = addr;
    net.dst = addr;

    tester_event(BTP_SERVICE_ID_MESH, BTP_MESH_EV_PROVISIONED,
                 NULL, 0);
}

static void
prov_reset(void)
{
    SYS_LOG_DBG("");

    bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);
}

static const struct bt_mesh_comp comp = {
    .cid = CID_LOCAL,
    .elem = elements,
    .elem_count = ARRAY_SIZE(elements),
};

static struct bt_mesh_prov prov = {
    .uuid = dev_uuid,
    .static_val = static_auth,
    .static_val_len = sizeof(static_auth),
    .output_number = output_number,
    .output_string = output_string,
    .input = input,
    .link_open = link_open,
    .link_close = link_close,
    .complete = prov_complete,
    .reset = prov_reset,
};

static uint8_t
config_prov(const void *cmd, uint16_t cmd_len,
            void *rsp, uint16_t *rsp_len)
{
    const struct btp_mesh_config_provisioning_cmd *cp = cmd;

    SYS_LOG_DBG("");

    if (cmd_len < sizeof(*cp)) {
        return BTP_STATUS_FAILED;
    }

    memcpy(dev_uuid, cp->uuid, sizeof(dev_uuid));
    memcpy(static_auth, cp->static_auth, sizeof(static_auth));

    prov.output_size = cp->out_size;
    prov.output_actions = sys_le16_to_cpu(cp->out_actions);
    prov.input_size = cp->in_size;
    prov.input_actions = sys_le16_to_cpu(cp->in_actions);

    return BTP_STATUS_SUCCESS;
}

static uint8_t
provision_node(const void *cmd, uint16_t cmd_len,
               void *rsp, uint16_t *rsp_len)
{
    const struct btp_mesh_provision_node_cmd *cp = cmd;

    SYS_LOG_DBG("");

    if (cmd_len != sizeof(*cp)) {
        return BTP_STATUS_FAILED;
    }

    memcpy(dev_key, cp->dev_key, sizeof(dev_key));
    memcpy(net_key, cp->net_key, sizeof(net_key));

    addr = sys_le16_to_cpu(cp->addr);
    flags = cp->flags;
    iv_index = le32toh(cp->iv_index);
    net_key_idx = sys_le16_to_cpu(cp->net_key_idx);

    return BTP_STATUS_SUCCESS;
}

static uint8_t
init(const void *cmd, uint16_t cmd_len,
     void *rsp, uint16_t *rsp_len)
{
    int err;

    SYS_LOG_DBG("");

    err = bt_mesh_init(own_addr_type, &prov, &comp);
    if (err) {
        return BTP_STATUS_FAILED;
    }

    if (addr) {
        err = bt_mesh_provision(net_key, net_key_idx, flags, iv_index,
                                addr, dev_key);
        if (err) {
            return BTP_STATUS_FAILED;
        }
    } else {
        err = bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);
        if (err) {
            return BTP_STATUS_FAILED;
        }
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
reset(const void *cmd, uint16_t cmd_len,
      void *rsp, uint16_t *rsp_len)
{
    SYS_LOG_DBG("");

    bt_mesh_reset();

    return BTP_STATUS_SUCCESS;
}

static uint8_t
input_number(const void *cmd, uint16_t cmd_len,
             void *rsp, uint16_t *rsp_len)
{
    const struct btp_mesh_input_number_cmd *cp = cmd;
    uint32_t number;
    int err;

    number = le32toh(cp->number);

    SYS_LOG_DBG("number 0x%04lx", number);

    err = bt_mesh_input_number(number);
    if (err) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
input_string(const void *cmd, uint16_t cmd_len,
             void *rsp, uint16_t *rsp_len)
{
    const struct btp_mesh_input_string_cmd *cp = cmd;
    int err;

    SYS_LOG_DBG("");

    if (cmd_len < sizeof(*cp) &&
        cmd_len != (sizeof(*cp) + cp->string_len)) {
        return BTP_STATUS_FAILED;
    }

    /* for historical reasons this commands must send NULL terminated
     * string
     */
    if (cp->string[cp->string_len] != '\0') {
        return BTP_STATUS_FAILED;
    }

    if (strlen((char *)cp->string) < input_size) {
        SYS_LOG_ERR("Too short input (%u chars required)", input_size);
        return BTP_STATUS_FAILED;
    }
    err = bt_mesh_input_string((char *)cp->string);
    if (err) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
ivu_test_mode(const void *cmd, uint16_t cmd_len,
              void *rsp, uint16_t *rsp_len)
{
    const struct btp_mesh_ivu_test_mode_cmd *cp = cmd;

    SYS_LOG_DBG("enable 0x%02x", cp->enable);

    bt_mesh_iv_update_test(cp->enable ? true : false);

    return BTP_STATUS_SUCCESS;
}

static uint8_t
ivu_toggle_state(const void *cmd, uint16_t cmd_len,
                 void *rsp, uint16_t *rsp_len)
{
    bool result;

    SYS_LOG_DBG("");

    result = bt_mesh_iv_update();
    if (!result) {
        SYS_LOG_ERR("Failed to toggle the IV Update state");
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
lpn(const void *cmd, uint16_t cmd_len,
    void *rsp, uint16_t *rsp_len)
{
    const struct btp_mesh_lpn_set_cmd *cp = cmd;
    bool enable;
    int err;

    SYS_LOG_DBG("enable 0x%02x", cp->enable);

    enable = cp->enable ? true : false;
    err = bt_mesh_lpn_set(enable);
    if (err) {
        SYS_LOG_ERR("Failed to toggle LPN (err %d)", err);
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
lpn_poll(const void *cmd, uint16_t cmd_len,
         void *rsp, uint16_t *rsp_len)
{
    int err;

    SYS_LOG_DBG("");

    err = bt_mesh_lpn_poll();
    if (err) {
        SYS_LOG_ERR("Failed to send poll msg (err %d)", err);
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
net_send(const void *cmd, uint16_t cmd_len,
         void *rsp, uint16_t *rsp_len)
{
    const struct btp_mesh_net_send_cmd *cp = cmd;
    struct os_mbuf *msg = NET_BUF_SIMPLE(UINT8_MAX);
    struct bt_mesh_msg_ctx ctx = {
        .net_idx = net.net_idx,
        .app_idx = vnd_app_key_idx,
        .addr = sys_le16_to_cpu(cp->dst),
        .send_ttl = cp->ttl,
    };
    int err;
    int status = BTP_STATUS_SUCCESS;

    if (cmd_len < sizeof(*cp) &&
        cmd_len != (sizeof(*cp) + cp->payload_len)) {
        return BTP_STATUS_FAILED;
    }

    SYS_LOG_DBG("ttl 0x%02x dst 0x%04x payload_len %d", ctx.send_ttl,
                ctx.addr, cp->payload_len);

    if (!bt_mesh_app_key_exists(vnd_app_key_idx)) {
        (void) bt_mesh_app_key_add(vnd_app_key_idx, net.net_idx,
                                   vnd_app_key);
        vnd_models[0].keys[0] = vnd_app_key_idx;
    }

    net_buf_simple_add_mem(msg, cp->payload, cp->payload_len);

    err = bt_mesh_model_send(&vnd_models[0], &ctx, msg, NULL, NULL);
    if (err) {
        SYS_LOG_ERR("Failed to send (err %d)", err);
        status = BTP_STATUS_FAILED;
    }

    os_mbuf_free_chain(msg);

    return status;
}

static uint8_t
health_generate_faults(const void *cmd, uint16_t cmd_len,
                       void *rsp, uint16_t *rsp_len)
{
    struct btp_mesh_health_generate_faults_rp *rp = rsp;
    uint8_t some_faults[] = {0x01, 0x02, 0x03, 0xff, 0x06};
    uint8_t cur_faults_count, reg_faults_count;

    cur_faults_count = min(sizeof(cur_faults), sizeof(some_faults));
    memcpy(cur_faults, some_faults, cur_faults_count);
    memcpy(rp->current_faults, cur_faults, cur_faults_count);
    rp->cur_faults_count = cur_faults_count;

    reg_faults_count = min(sizeof(reg_faults), sizeof(some_faults));
    memcpy(reg_faults, some_faults, reg_faults_count);
    memcpy(rp->registered_faults + cur_faults_count, reg_faults, reg_faults_count);
    rp->reg_faults_count = reg_faults_count;

    bt_mesh_fault_update(&elements[0]);

    *rsp_len = sizeof(*rp) + cur_faults_count + reg_faults_count;

    return BTP_STATUS_SUCCESS;
}

static uint8_t
health_clear_faults(const void *cmd, uint16_t cmd_len,
                    void *rsp, uint16_t *rsp_len)
{
    SYS_LOG_DBG("");

    memset(cur_faults, 0, sizeof(cur_faults));
    memset(reg_faults, 0, sizeof(reg_faults));

    bt_mesh_fault_update(&elements[0]);

    return BTP_STATUS_SUCCESS;
}

static uint8_t
model_send(const void *cmd, uint16_t cmd_len,
           void *rsp, uint16_t *rsp_len)
{
    const struct btp_mesh_model_send_cmd *cp = cmd;
    struct os_mbuf *msg = NET_BUF_SIMPLE(UINT8_MAX);
    struct bt_mesh_model *model = NULL;
    int err, i;
    uint16_t src;
    int status = BTP_STATUS_SUCCESS;

    if (cmd_len < sizeof(*cp) &&
        cmd_len != (sizeof(*cp) + cp->payload_len)) {
        return BTP_STATUS_FAILED;
    }

    struct bt_mesh_msg_ctx ctx = {
        .net_idx = net.net_idx,
        .app_idx = BT_MESH_KEY_DEV,
        .addr = sys_le16_to_cpu(cp->dst),
        .send_ttl = BT_MESH_TTL_DEFAULT,
    };

    src = sys_le16_to_cpu(cp->src);

    /* Lookup source address */
    for (i = 0; i < ARRAY_SIZE(model_bound); i++) {
        if (bt_mesh_model_elem(model_bound[i].model)->addr == src) {
            model = model_bound[i].model;
            ctx.app_idx = model_bound[i].appkey_idx;

            break;
        }
    }

    if (!model) {
        SYS_LOG_ERR("Model not found");
        status = BTP_STATUS_FAILED;

        goto rsp;
    }

    SYS_LOG_DBG("src 0x%04x dst 0x%04x model %p payload_len %d", src,
                ctx.addr, model, cp->payload_len);

    net_buf_simple_add_mem(msg, cp->payload, cp->payload_len);

    err = bt_mesh_model_send(model, &ctx, msg, NULL, NULL);
    if (err) {
        SYS_LOG_ERR("Failed to send (err %d)", err);
        status = BTP_STATUS_FAILED;
    }

rsp:
    os_mbuf_free_chain(msg);
    return status;
}

#if MYNEWT_VAL(BLE_MESH_TESTING)

static uint8_t
lpn_subscribe(const void *cmd, uint16_t cmd_len,
              void *rsp, uint16_t *rsp_len)
{
    const struct btp_mesh_lpn_subscribe_cmd *cp = cmd;
    uint16_t address = sys_le16_to_cpu(cp->address);
    int err;

    SYS_LOG_DBG("address 0x%04x", address);

    err = bt_test_mesh_lpn_group_add(address);
    if (err) {
        SYS_LOG_ERR("Failed to subscribe (err %d)", err);
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
lpn_unsubscribe(const void *cmd, uint16_t cmd_len,
                void *rsp, uint16_t *rsp_len)
{
    const struct btp_mesh_lpn_unsubscribe_cmd *cp = cmd;
    uint16_t address = sys_le16_to_cpu(cp->address);
    int err;

    SYS_LOG_DBG("address 0x%04x", address);

    err = bt_test_mesh_lpn_group_remove(&address, 1);
    if (err) {
        SYS_LOG_ERR("Failed to unsubscribe (err %d)", err);
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
rpl_clear(const void *cmd, uint16_t cmd_len,
          void *rsp, uint16_t *rsp_len)
{
    int err;

    SYS_LOG_DBG("");

    err = bt_test_mesh_rpl_clear();
    if (err) {
        SYS_LOG_ERR("Failed to clear RPL (err %d)", err);
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

#endif /* MYNEWT_VAL(BLE_MESH_TESTING) */

static uint8_t
proxy_identity_enable(const void *cmd, uint16_t cmd_len,
                      void *rsp, uint16_t *rsp_len)
{
    int err;

    SYS_LOG_DBG("");

    err = bt_mesh_proxy_identity_enable();
    if (err) {
        SYS_LOG_ERR("Failed to enable proxy identity (err %d)", err);
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static const struct btp_handler handlers[] = {
    {
        .opcode = BTP_MESH_READ_SUPPORTED_COMMANDS,
        .index = BTP_INDEX_NONE,
        .expect_len = 0,
        .func = supported_commands,
    },
    {
        .opcode = BTP_MESH_CONFIG_PROVISIONING,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = config_prov,
    },
    {
        .opcode = BTP_MESH_PROVISION_NODE,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = provision_node,
    },
    {
        .opcode = BTP_MESH_INIT,
        .expect_len = 0,
        .func = init,
    },
    {
        .opcode = BTP_MESH_RESET,
        .expect_len = 0,
        .func = reset,
    },
    {
        .opcode = BTP_MESH_INPUT_NUMBER,
        .expect_len = sizeof(struct btp_mesh_input_number_cmd),
        .func = input_number,
    },
    {
        .opcode = BTP_MESH_INPUT_STRING,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = input_string,
    },
    {
        .opcode = BTP_MESH_IVU_TEST_MODE,
        .expect_len = sizeof(struct btp_mesh_ivu_test_mode_cmd),
        .func = ivu_test_mode,
    },
    {
        .opcode = BTP_MESH_IVU_TOGGLE_STATE,
        .expect_len = 0,
        .func = ivu_toggle_state,
    },
    {
        .opcode = BTP_MESH_LPN,
        .expect_len = sizeof(struct btp_mesh_lpn_set_cmd),
        .func = lpn,
    },
    {
        .opcode = BTP_MESH_LPN_POLL,
        .expect_len = 0,
        .func = lpn_poll,
    },
    {
        .opcode = BTP_MESH_NET_SEND,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = net_send,
    },
    {
        .opcode = BTP_MESH_HEALTH_GENERATE_FAULTS,
        .expect_len = 0,
        .func = health_generate_faults,
    },
    {
        .opcode = BTP_MESH_HEALTH_CLEAR_FAULTS,
        .expect_len = 0,
        .func = health_clear_faults,
    },
    {
        .opcode = BTP_MESH_MODEL_SEND,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = model_send,
    },
    {
        .opcode = BTP_MESH_LPN_SUBSCRIBE,
        .expect_len = sizeof(struct btp_mesh_lpn_subscribe_cmd),
        .func = lpn_subscribe,
    },
    {
        .opcode = BTP_MESH_LPN_UNSUBSCRIBE,
        .expect_len = sizeof(struct btp_mesh_lpn_unsubscribe_cmd),
        .func = lpn_unsubscribe,
    },
    {
        .opcode = BTP_MESH_RPL_CLEAR,
        .expect_len = 0,
        .func = rpl_clear,
    },
    {
        .opcode = BTP_MESH_PROXY_IDENTITY,
        .expect_len = 0,
        .func = proxy_identity_enable,
    },
};

void
net_recv_ev(uint8_t ttl,
            uint8_t ctl,
            uint16_t src,
            uint16_t dst,
            const void *payload,
            size_t payload_len)
{
    struct os_mbuf *buf = NET_BUF_SIMPLE(UINT8_MAX);
    struct btp_mesh_net_recv_ev *ev;

    SYS_LOG_DBG("ttl 0x%02x ctl 0x%02x src 0x%04x dst 0x%04x "
                "payload_len %d", ttl, ctl, src, dst, payload_len);

    if (payload_len > net_buf_simple_tailroom(buf)) {
        SYS_LOG_ERR("Payload size exceeds buffer size");

        goto done;
    }

    ev = net_buf_simple_add(buf, sizeof(*ev));
    ev->ttl = ttl;
    ev->ctl = ctl;
    ev->src = htole16(src);
    ev->dst = htole16(dst);
    ev->payload_len = payload_len;
    net_buf_simple_add_mem(buf, payload, payload_len);

    tester_send_buf(BTP_SERVICE_ID_MESH, BTP_MESH_EV_NET_RECV, CONTROLLER_INDEX,
                    buf);
done:
    os_mbuf_free_chain(buf);
}

static void
model_bound_cb(uint16_t addr, struct bt_mesh_model *model,
               uint16_t key_idx)
{
    int i;

    SYS_LOG_DBG("remote addr 0x%04x key_idx 0x%04x model %p",
                addr, key_idx, model);

    for (i = 0; i < ARRAY_SIZE(model_bound); i++) {
        if (!model_bound[i].model) {
            model_bound[i].model = model;
            model_bound[i].addr = addr;
            model_bound[i].appkey_idx = key_idx;

            return;
        }
    }

    SYS_LOG_ERR("model_bound is full");
}

static void
model_unbound_cb(uint16_t addr, struct bt_mesh_model *model,
                 uint16_t key_idx)
{
    int i;

    SYS_LOG_DBG("remote addr 0x%04x key_idx 0x%04x model %p",
                addr, key_idx, model);

    for (i = 0; i < ARRAY_SIZE(model_bound); i++) {
        if (model_bound[i].model == model) {
            model_bound[i].model = NULL;
            model_bound[i].addr = 0x0000;
            model_bound[i].appkey_idx = BT_MESH_KEY_UNUSED;

            return;
        }
    }

    SYS_LOG_INF("model not found");
}

static void
invalid_bearer_cb(uint8_t opcode)
{
    struct btp_mesh_invalid_bearer_ev ev = {
        .opcode = opcode,
    };

    SYS_LOG_DBG("opcode 0x%02x", opcode);

    tester_event(BTP_SERVICE_ID_MESH, BTP_MESH_EV_INVALID_BEARER,
                 (uint8_t *) &ev, sizeof(ev));
}

static void
incomp_timer_exp_cb(void)
{
    tester_event(BTP_SERVICE_ID_MESH, BTP_MESH_EV_INCOMP_TIMER_EXP,
                 NULL, 0);
}

static struct bt_test_cb bt_test_cb = {
    .mesh_net_recv = net_recv_ev,
    .mesh_model_bound = model_bound_cb,
    .mesh_model_unbound = model_unbound_cb,
    .mesh_prov_invalid_bearer = invalid_bearer_cb,
    .mesh_trans_incomp_timer_exp = incomp_timer_exp_cb,
};

static void
lpn_established(uint16_t friend_addr)
{

    struct bt_mesh_lpn *lpn = &bt_mesh.lpn;
    struct btp_mesh_lpn_established_ev
        ev = {lpn->sub->net_idx, friend_addr, lpn->queue_size,
              lpn->recv_win};

    SYS_LOG_DBG("Friendship (as LPN) established with "
                "Friend 0x%04x Queue Size %d Receive Window %d",
                friend_addr, lpn->queue_size, lpn->recv_win);

    tester_event(BTP_SERVICE_ID_MESH, BTP_MESH_EV_LPN_ESTABLISHED,
                 (uint8_t *) &ev, sizeof(ev));
}

static void
lpn_terminated(uint16_t friend_addr)
{
    struct bt_mesh_lpn *lpn = &bt_mesh.lpn;
    struct btp_mesh_lpn_terminated_ev ev = {lpn->sub->net_idx, friend_addr};

    SYS_LOG_DBG("Friendship (as LPN) lost with Friend "
                "0x%04x", friend_addr);

    tester_event(BTP_SERVICE_ID_MESH, BTP_MESH_EV_LPN_TERMINATED,
                 (uint8_t *) &ev, sizeof(ev));
}

void
lpn_cb(uint16_t friend_addr, bool established)
{
    if (established) {
        lpn_established(friend_addr);
    } else {
        lpn_terminated(friend_addr);
    }
}

uint8_t
tester_init_mesh(void)
{
    health_pub_init();

    if (IS_ENABLED(CONFIG_BT_TESTING)) {
        bt_mesh_lpn_set_cb(lpn_cb);
        bt_test_cb_register(&bt_test_cb);
    }

    tester_register_command_handlers(BTP_SERVICE_ID_MESH, handlers,
                                     ARRAY_SIZE(handlers));

    return BTP_STATUS_SUCCESS;
}

uint8_t
tester_unregister_mesh(void)
{
    return BTP_STATUS_SUCCESS;
}

#endif /* MYNEWT_VAL(BLE_MESH) */
