/*  Bluetooth Mesh */

/*
 * Copyright (c) 2017 Intel Corporation
 * Copyright (c) 2021 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_BLUETOOTH_MESH_PROXY_MSG_H_
#define ZEPHYR_SUBSYS_BLUETOOTH_MESH_PROXY_MSG_H_

#define PDU_TYPE(data)     (data[0] & BIT_MASK(6))
#define CFG_FILTER_SET     0x00
#define CFG_FILTER_ADD     0x01
#define CFG_FILTER_REMOVE  0x02
#define CFG_FILTER_STATUS  0x03

#define BT_MESH_PROXY_NET_PDU   0x00
#define BT_MESH_PROXY_BEACON    0x01
#define BT_MESH_PROXY_CONFIG    0x02
#define BT_MESH_PROXY_PROV      0x03

#define PDU_HDR(sar, type) (sar << 6 | (type & BIT_MASK(6)))

typedef int (*proxy_send_cb_t)(uint16_t conn_handle,
	const void *data, uint16_t len,
	void (*end)(uint16_t, void *), void *user_data);

typedef void (*proxy_recv_cb_t)(uint16_t conn_handle,
	struct bt_mesh_net_rx *rx, struct os_mbuf *buf);

struct bt_mesh_proxy_role {
	uint16_t conn_handle;
	uint8_t msg_type;

	struct {
		proxy_send_cb_t send;
		proxy_recv_cb_t recv;
	} cb;

	struct k_work_delayable sar_timer;
	struct os_mbuf *buf;
};

ssize_t bt_mesh_proxy_msg_recv(struct bt_mesh_proxy_role *role,
	const void *buf, uint16_t len);
int bt_mesh_proxy_msg_send(struct bt_mesh_proxy_role *role, uint8_t type,
	struct os_mbuf *msg, void (*end)(uint16_t, void *), void *user_data);
void bt_mesh_proxy_msg_init(struct bt_mesh_proxy_role *role);

#endif /* ZEPHYR_SUBSYS_BLUETOOTH_MESH_PROXY_MSG_H_ */
