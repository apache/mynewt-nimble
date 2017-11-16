/*  Bluetooth Mesh */

/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ADV_H__
#define __ADV_H__

/* Maximum advertising data payload for a single data type */
#include "mesh/mesh.h"

#define BT_MESH_ADV_DATA_SIZE 31
#define BT_MESH_ADV_USER_DATA_SIZE (sizeof(struct os_mbuf_pkthdr) + \
                                    sizeof(struct bt_mesh_adv)) + \
                                    sizeof(struct os_mbuf)

#define BT_MESH_ADV(om) ((struct bt_mesh_adv *) OS_MBUF_USRHDR(om))

enum bt_mesh_adv_type
{
    BT_MESH_ADV_PROV, BT_MESH_ADV_DATA, BT_MESH_ADV_BEACON,
};

typedef void
(*bt_mesh_adv_func_t)(struct os_mbuf *adv_data, int err);

struct bt_mesh_adv
{
    bt_mesh_adv_func_t sent;
    u8_t type :2, busy :1;
    u8_t count :3, adv_int :5;
    union
    {
        /* Generic User Data */
        u8_t user_data[2];

        /* Address, used e.g. for Friend Queue messages */
        u16_t addr;

        /* For transport layer segment sending */
        struct
        {
            u8_t tx_id;
            u8_t attempts :6, new_key :1, friend_cred :1;
        } seg;
    };

    int ref_cnt;
    struct os_event ev;
};

/* xmit_count: Number of retransmissions, i.e. 0 == 1 transmission */
struct os_mbuf * bt_mesh_adv_create(enum bt_mesh_adv_type type, u8_t xmit_count,
                                    u8_t xmit_int, s32_t timeout);

struct os_mbuf *bt_mesh_adv_create_from_pool(struct os_mbuf_pool *pool,
					     enum bt_mesh_adv_type type,
					     u8_t xmit_count, u8_t xmit_int,
					     s32_t timeout);

void bt_mesh_adv_send(struct os_mbuf *buf, bt_mesh_adv_func_t sent);

void bt_mesh_adv_update(void);

void bt_mesh_adv_init(void);

int bt_mesh_scan_enable(void);

int bt_mesh_scan_disable(void);

int ble_adv_gap_mesh_cb(struct ble_gap_event *event, void *arg);
#endif
