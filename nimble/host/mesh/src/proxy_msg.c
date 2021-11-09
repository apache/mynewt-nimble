/*  Bluetooth Mesh */

/*
 * Copyright (c) 2017 Intel Corporation
 * Copyright (c) 2021 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "syscfg/syscfg.h"
#define MESH_LOG_MODULE BLE_MESH_PROXY_LOG

#if MYNEWT_VAL(BLE_MESH_PROXY)

#include "mesh/mesh.h"
#include "host/ble_att.h"
#include "services/gatt/ble_svc_gatt.h"
#include "../../host/src/ble_hs_priv.h"

#include "mesh_priv.h"
#include "adv.h"
#include "net.h"
#include "rpl.h"
#include "prov.h"
#include "beacon.h"
#include "foundation.h"
#include "access.h"
#include "proxy.h"
#include "proxy_msg.h"

#define PDU_SAR(data)      (data[0] >> 6)

#define BT_UUID_16_ENCODE(w16)  \
	(((w16) >>  0) & 0xFF), \
	(((w16) >>  8) & 0xFF)
/* Mesh Profile 1.0 Section 6.6:
 * "The timeout for the SAR transfer is 20 seconds. When the timeout
 *  expires, the Proxy Server shall disconnect."
 */
#define PROXY_SAR_TIMEOUT  K_SECONDS(20)

#define SAR_COMPLETE       0x00
#define SAR_FIRST          0x01
#define SAR_CONT           0x02
#define SAR_LAST           0x03

#define PDU_HDR(sar, type) (sar << 6 | (type & BIT_MASK(6)))

#define PB_GATT_BUF_LEN_MAX	66
#define PROXY_BUF_LEN_MAX	30

#if defined(CONFIG_BT_MESH_PB_GATT)
#define PROXY_MSG_FIRST_BUF_LEN PB_GATT_BUF_LEN_MAX
#else
#define PROXY_MSG_FIRST_BUF_LEN PROXY_BUF_LEN_MAX
#endif

static uint8_t bufs[PROXY_MSG_FIRST_BUF_LEN +
		    ((CONFIG_BT_MAX_CONN - 1) * PROXY_BUF_LEN_MAX)];

#if (MYNEWT_VAL(BLE_MESH_GATT_PROXY))
static void proxy_cfg(struct bt_mesh_proxy_role *role)
{
	struct os_mbuf *buf = NET_BUF_SIMPLE(BT_MESH_NET_MAX_PDU_LEN);
	struct bt_mesh_net_rx rx;
	int err;

	err = bt_mesh_net_decode(role->buf, BT_MESH_NET_IF_PROXY_CFG,
				 &rx, buf);
	if (err) {
		BT_ERR("Failed to decode Proxy Configuration (err %d)", err);
		goto done;
	}

	rx.local_match = 1U;

	if (bt_mesh_rpl_check(&rx, NULL)) {
		BT_WARN("Replay: src 0x%04x dst 0x%04x seq 0x%06x",
			rx.ctx.addr, rx.ctx.recv_dst, rx.seq);
		goto done;
	}

	/* Remove network headers */
	net_buf_simple_pull_mem(buf, BT_MESH_NET_HDR_LEN);

	BT_DBG("%u bytes: %s", buf->om_len, bt_hex(buf->om_data, buf->om_len));

	if (buf->om_len < 1) {
		BT_WARN("Too short proxy configuration PDU");
		goto done;
	}

	role->cb.recv(role->conn_handle, &rx, buf);
done:
	os_mbuf_free_chain(buf);
}
#endif /* GATT_PROXY */

static void proxy_complete_pdu(struct bt_mesh_proxy_role *role)
{
	switch (role->msg_type) {
#if defined(CONFIG_BT_MESH_PROXY)
	case BT_MESH_PROXY_NET_PDU:
		BT_INFO("Mesh Network PDU");
		bt_mesh_net_recv(role->buf, 0, BT_MESH_NET_IF_PROXY);
		break;
	case BT_MESH_PROXY_BEACON:
		BT_INFO("Mesh Beacon PDU");
		bt_mesh_beacon_recv(role->buf);
		break;
	case BT_MESH_PROXY_CONFIG:
		BT_INFO("Mesh Configuration PDU");
		proxy_cfg(role);
		break;
#endif
#if (MYNEWT_VAL(BLE_MESH_PB_GATT))
	case BT_MESH_PROXY_PROV:
		BT_INFO("Mesh Provisioning PDU");
		bt_mesh_pb_gatt_recv(role->conn_handle, role->buf);
		break;
#endif
	default:
		BT_WARN("Unhandled Message Type 0x%02x", role->msg_type);
		break;
	}

	net_buf_simple_init(role->buf, 0);
}

ssize_t bt_mesh_proxy_msg_recv(struct bt_mesh_proxy_role *role,
			       const void *buf, uint16_t len)
{
	const uint8_t *data = buf;

	switch (PDU_SAR(data)) {
	case SAR_COMPLETE:
		if (role->buf->om_len) {
			BT_WARN("Complete PDU while a pending incomplete one");
			return -EINVAL;
		}

		role->msg_type = PDU_TYPE(data);
		net_buf_simple_add_mem(role->buf, data + 1, len - 1);
		proxy_complete_pdu(role);
		break;

	case SAR_FIRST:
		if (role->buf->om_len) {
			BT_WARN("First PDU while a pending incomplete one");
			return -EINVAL;
		}

		k_work_reschedule(&role->sar_timer, PROXY_SAR_TIMEOUT);
		role->msg_type = PDU_TYPE(data);
		net_buf_simple_add_mem(role->buf, data + 1, len - 1);
		break;

	case SAR_CONT:
		if (!role->buf->om_len) {
			BT_WARN("Continuation with no prior data");
			return -EINVAL;
		}

		if (role->msg_type != PDU_TYPE(data)) {
			BT_WARN("Unexpected message type in continuation");
			return -EINVAL;
		}

		k_work_reschedule(&role->sar_timer, PROXY_SAR_TIMEOUT);
		net_buf_simple_add_mem(role->buf, data + 1, len - 1);
		break;

	case SAR_LAST:
		if (!role->buf->om_len) {
			BT_WARN("Last SAR PDU with no prior data");
			return -EINVAL;
		}

		if (role->msg_type != PDU_TYPE(data)) {
			BT_WARN("Unexpected message type in last SAR PDU");
			return -EINVAL;
		}

		/* If this fails, the work handler exits early, as there's no
		 * active SAR buffer.
		 */
		(void)k_work_cancel_delayable(&role->sar_timer);
		net_buf_simple_add_mem(role->buf, data + 1, len - 1);
		proxy_complete_pdu(role);
		break;
	}

	return len;
}

int bt_mesh_proxy_msg_send(struct bt_mesh_proxy_role *role, uint8_t type,
			   struct os_mbuf *msg,
			   	void (*end)(uint16_t, void *), void *user_data)
{
	int err;
	uint16_t mtu;
	uint16_t conn_handle = role->conn_handle;

	BT_DBG("conn_handle %d type 0x%02x len %u: %s", conn_handle, type, msg->om_len,
	       bt_hex(msg->om_data, msg->om_len));

	/* ATT_MTU - OpCode (1 byte) - Handle (2 bytes) */
	mtu = ble_att_mtu(conn_handle) - 3;
	if (mtu > msg->om_len) {
		net_buf_simple_push_u8(msg, PDU_HDR(SAR_COMPLETE, type));
		return role->cb.send(conn_handle, msg->om_data, msg->om_len, end, user_data);
	}

	net_buf_simple_push_u8(msg, PDU_HDR(SAR_FIRST, type));
	err = role->cb.send(conn_handle, msg->om_data, mtu, NULL, NULL);
	if (err) {
		return err;
	}
	net_buf_simple_pull_mem(msg, mtu);

	while (msg->om_len) {
		if (msg->om_len + 1 < mtu) {
			net_buf_simple_push_u8(msg, PDU_HDR(SAR_LAST, type));
			err = role->cb.send(conn_handle, msg->om_data, msg->om_len, end, user_data);
			if (err) {
				return err;
			}
			break;
		}

		net_buf_simple_push_u8(msg, PDU_HDR(SAR_CONT, type));
		err = role->cb.send(conn_handle, msg->om_data, mtu, NULL, NULL);
		if (err) {
			return err;
		}
		net_buf_simple_pull_mem(msg, mtu);
	}

	return 0;
}

void bt_mesh_proxy_msg_init(struct bt_mesh_proxy_role *role)
{
	uint8_t i, len;
	uint8_t *buf;

	/* Check if buf has been allocated, in this way, we no longer need
	 * to repeat the operation.
	 */
	if (role->buf->om_data) {
		net_buf_simple_reset(role->buf);
		return;
	}

	i = role->conn_handle;
	if (!i) {
		len = PROXY_MSG_FIRST_BUF_LEN;
		buf = bufs;
	} else {
		len = PROXY_BUF_LEN_MAX;
		buf = &bufs[PROXY_MSG_FIRST_BUF_LEN + (PROXY_BUF_LEN_MAX * (i - 1))];
	}

	net_buf_simple_init_with_data(role->buf, buf, len);

	net_buf_simple_reset(role->buf);
}

#endif /* MYNEWT_VAL(BLE_MESH_PROXY) */
