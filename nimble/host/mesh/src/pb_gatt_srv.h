/*
 * Copyright (c) 2017 Intel Corporation
 * Copyright (c) 2021 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __PB_GATT_SRV_H__
#define __PB_GATT_SRV_H__

int bt_mesh_pb_gatt_send(uint16_t conn_handle, struct os_mbuf *buf,
			 void (*end)(uint16_t, void *), void *user_data);

int bt_mesh_pb_gatt_enable(void);
int bt_mesh_pb_gatt_disable(void);

int prov_ccc_write(uint16_t conn_handle, uint8_t type);
void gatt_disconnected_pb_gatt(uint16_t conn_handle, uint8_t err);
void gatt_connected_pb_gatt(uint16_t conn_handle, uint8_t err);
void resolve_svc_handles(void);

int bt_mesh_pb_gatt_adv_start(void);

#endif /* __PB_GATT_SRV_H__ */