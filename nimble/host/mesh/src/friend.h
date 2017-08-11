/*  Bluetooth Mesh */

/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __FRIEND_H__
#define __FRIEND_H__

#include "mesh/mesh.h"

static inline bool
bt_mesh_friend_dst_is_lpn(u16_t dst)
{
#if (MYNEWT_VAL(BLE_MESH_FRIEND))
    return (dst == bt_mesh.frnd.lpn);
#else
    return false;
#endif
}

bool
bt_mesh_friend_enqueue(struct os_mbuf *buf, u16_t dst);

int
bt_mesh_friend_poll(struct bt_mesh_net_rx *rx, struct os_mbuf *buf);
int
bt_mesh_friend_req(struct bt_mesh_net_rx *rx, struct os_mbuf *buf);

int
bt_mesh_friend_init(void);

#endif
