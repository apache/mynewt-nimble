/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __BT_MESH_STORE_H
#define __BT_MESH_STORE_H

#include "mesh/glue.h"

struct bt_mesh_store_net {
    u32_t iv_index;
    u32_t seq:24,
          iv_update:1;

    bool provisioned;
    uint16_t dev_primary_addr;

    u8_t dev_key[16];
};

struct bt_mesh_store_sub {
    u16_t net_idx;
    bool kr_flag;
    u8_t kr_phase;

    struct bt_mesh_store_subnet_keys {
        u8_t net[16];       /* NetKey */
    } keys[2];
};

struct bt_mesh_store_app_key {
    u16_t net_idx;
    u16_t app_idx;
    bool  updated;
    struct bt_mesh_store_app_keys {
        u8_t id;
        u8_t val[16];
    } keys[2];
};

struct bt_mesh_net;
struct bt_mesh_subnet;
struct bt_mesh_app_key;

const struct bt_mesh_store_net *bt_mesh_store_net_get(void);
void bt_mesh_store_net_set(struct bt_mesh_net *net);

const struct bt_mesh_store_sub *bt_mesh_store_sub_get_next(const struct bt_mesh_store_sub *p);
void bt_mesh_store_sub_set(struct bt_mesh_subnet *sub);
void bt_mesh_store_sub_set_all(struct bt_mesh_subnet *sub, size_t count);
u8_t bt_mesh_store_net_flags(const struct bt_mesh_store_sub *sub);

void bt_mesh_store_app_key_del(struct bt_mesh_app_key *key);
void bt_mesh_store_app_key_set(struct bt_mesh_app_key *key);
void bt_mesh_store_app_key_set_all(struct bt_mesh_app_key *key, size_t count);
const struct bt_mesh_store_app_key *bt_mesh_store_app_key_get_next(const struct bt_mesh_store_app_key *p);
void bt_mesh_store_app_key_restore(struct bt_mesh_app_key *key, size_t num);

#endif /* __BT_MESH_STORE_H */

