/*  Bluetooth Mesh */

/*
 * Copyright (c) 2017 Intel Corporation
 * Copyright (c) 2021 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define BT_DBG_ENABLED CONFIG_BT_MESH_DEBUG_PROXY
#define LOG_MODULE_NAME bt_mesh_gatt

#include "../../host/src/ble_hs_priv.h"
#include "services/gatt/ble_svc_gatt.h"

#include "mesh_priv.h"
#include "adv.h"
#include "net.h"
#include "rpl.h"
#include "transport.h"
#include "prov.h"
#include "beacon.h"
#include "foundation.h"
#include "access.h"
#include "proxy.h"
#include "proxy_msg.h"

#define BT_UUID_16_ENCODE(w16)  \
	(((w16) >>  0) & 0xFF), \
	(((w16) >>  8) & 0xFF)
/* Mesh Profile 1.0 Section 6.6:
 * "The timeout for the SAR transfer is 20 seconds. When the timeout
 *  expires, the Proxy Server shall disconnect."
 */
#define PROXY_SAR_TIMEOUT  K_SECONDS(20)

#define CLIENT_BUF_SIZE 65


/** @def BT_UUID_MESH_PROV
 *  @brief Mesh Provisioning Service
 */
ble_uuid16_t BT_UUID_MESH_PROV                 = BLE_UUID16_INIT(0x1827);
#define BT_UUID_MESH_PROV_VAL             0x1827
/** @def BT_UUID_MESH_PROXY
 *  @brief Mesh Proxy Service
 */
ble_uuid16_t BT_UUID_MESH_PROXY                = BLE_UUID16_INIT(0x1828);
#define BT_UUID_MESH_PROXY_VAL            0x1828
/** @def BT_UUID_GATT_CCC
 *  @brief GATT Client Characteristic Configuration
 */
ble_uuid16_t BT_UUID_GATT_CCC                  = BLE_UUID16_INIT(0x2902);
#define BT_UUID_GATT_CCC_VAL              0x2902
/** @def BT_UUID_MESH_PROV_DATA_IN
 *  @brief Mesh Provisioning Data In
 */
ble_uuid16_t BT_UUID_MESH_PROV_DATA_IN         = BLE_UUID16_INIT(0x2adb);
#define BT_UUID_MESH_PROV_DATA_IN_VAL     0x2adb
/** @def BT_UUID_MESH_PROV_DATA_OUT
 *  @brief Mesh Provisioning Data Out
 */
ble_uuid16_t BT_UUID_MESH_PROV_DATA_OUT        = BLE_UUID16_INIT(0x2adc);
#define BT_UUID_MESH_PROV_DATA_OUT_VAL    0x2adc
/** @def BT_UUID_MESH_PROXY_DATA_IN
 *  @brief Mesh Proxy Data In
 */
ble_uuid16_t BT_UUID_MESH_PROXY_DATA_IN        = BLE_UUID16_INIT(0x2add);
#define BT_UUID_MESH_PROXY_DATA_IN_VAL    0x2add
/** @def BT_UUID_MESH_PROXY_DATA_OUT
 *  @brief Mesh Proxy Data Out
 */
ble_uuid16_t BT_UUID_MESH_PROXY_DATA_OUT       = BLE_UUID16_INIT(0x2ade);
#define BT_UUID_MESH_PROXY_DATA_OUT_VAL   0x2ade

#if CONFIG_BT_MESH_DEBUG_USE_ID_ADDR
#define ADV_OPT_USE_IDENTITY BT_LE_ADV_OPT_USE_IDENTITY
#else
#define ADV_OPT_USE_IDENTITY 0
#endif

#if CONFIG_BT_MESH_PROXY_USE_DEVICE_NAME
#define ADV_OPT_USE_NAME BT_LE_ADV_OPT_USE_NAME
#else
#define ADV_OPT_USE_NAME 0
#endif

#define ADV_OPT_PROV                                                           \
.conn_mode = (BLE_GAP_CONN_MODE_UND),                                  \
.disc_mode = (BLE_GAP_DISC_MODE_GEN),

#define ADV_OPT_PROXY                                                          \
.conn_mode = (BLE_GAP_CONN_MODE_UND),                                  \
.disc_mode = (BLE_GAP_DISC_MODE_GEN),

#define ADV_SLOW_INT                                                           \
.itvl_min = BT_GAP_ADV_SLOW_INT_MIN,                             \
.itvl_max = BT_GAP_ADV_SLOW_INT_MAX,

#define ADV_FAST_INT                                                           \
.itvl_min = BT_GAP_ADV_FAST_INT_MIN_2,                             \
.itvl_max = BT_GAP_ADV_FAST_INT_MAX_2,

static struct {
	uint16_t proxy_h;
	uint16_t proxy_data_out_h;
	uint16_t prov_h;
	uint16_t prov_data_in_h;
	uint16_t prov_data_out_h;
} svc_handles;

#if MYNEWT_VAL(BLE_MESH_GATT_PROXY)
static void proxy_send_beacons(struct ble_npl_event *work);
static void proxy_filter_recv(uint16_t conn_handle,
	struct bt_mesh_net_rx *rx, struct os_mbuf *buf);
#endif /* CONFIG_BT_MESH_GATT_PROXY */

#if MYNEWT_VAL(BLE_MESH_PB_GATT)
static bool prov_fast_adv;
#endif /* CONFIG_BT_MESH_PB_GATT */

static int proxy_send(uint16_t conn_handle,
	const void *data, uint16_t len,
	void (*end)(uint16_t, void *), void *user_data);

#if (MYNEWT_VAL(BLE_MESH_PB_GATT))
	static bool prov_fast_adv;
#endif

static struct bt_mesh_proxy_client {
	struct bt_mesh_proxy_role cli;
	uint16_t filter[MYNEWT_VAL(BLE_MESH_PROXY_FILTER_SIZE)];
	enum __packed {
		NONE,
		ACCEPT,
		REJECT,
		PROV,
	} filter_type;
#if MYNEWT_VAL(BLE_MESH_GATT_PROXY)
	struct ble_npl_callout send_beacons;
#endif /* CONFIG_BT_MESH_GATT_PROXY */
} clients[CONFIG_BT_MAX_CONN] = {
	[0 ... (CONFIG_BT_MAX_CONN - 1)] = {
		.cli.cb = {
			.send = proxy_send,
#if MYNEWT_VAL(BLE_MESH_GATT_PROXY)
			.recv = proxy_filter_recv,
#endif /* CONFIG_BT_MESH_GATT_PROXY */
		},
	},
};

//static uint8_t __noinit client_buf_data[CLIENT_BUF_SIZE * CONFIG_BT_MAX_CONN];

/* Track which service is enabled */
static enum {
	MESH_GATT_NONE,
	MESH_GATT_PROV,
	MESH_GATT_PROXY,
} gatt_svc = MESH_GATT_NONE;

static int conn_count;

static struct bt_mesh_proxy_client *find_client(uint16_t conn_handle)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(clients); i++) {
		if (clients[i].cli.conn_handle == conn_handle) {
			return &clients[i];
		}
	}

	return NULL;
}

#define ATTR_IS_PROV(attr) (attr->user_data != NULL)

static ssize_t gatt_recv(uint16_t conn_handle, uint16_t attr_handle,
			 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
	struct bt_mesh_proxy_client *client = find_client(conn_handle);
	const uint8_t *data = ctxt->om->om_data;
	uint16_t len = ctxt->om->om_len;

	client = find_client(conn_handle);

	if (!client) {
		return -ENOTCONN;
	}

	if (len < 1) {
		BT_WARN("Too small Proxy PDU");
		return -EINVAL;
	}

	if ((attr_handle == svc_handles.prov_data_in_h) !=
	    (PDU_TYPE(data) == BT_MESH_PROXY_PROV)) {
		BT_WARN("Proxy PDU type doesn't match GATT service");
		return -EINVAL;
	}

	return bt_mesh_proxy_msg_recv(&client->cli, data, len);
}


#if defined(CONFIG_BT_MESH_GATT_PROXY)
/* Next subnet in queue to be advertised */
static struct bt_mesh_subnet *beacon_sub;

static int filter_set(struct bt_mesh_proxy_client *client,
		      struct os_mbuf *buf)
{
	uint8_t type;

	if (buf->om_len < 1) {
		BT_WARN("Too short Filter Set message");
		return -EINVAL;
	}

	type = net_buf_simple_pull_u8(buf);
	BT_DBG("type 0x%02x", type);

	switch (type) {
		case 0x00:
			(void)memset(client->filter, 0, sizeof(client->filter));
			client->filter_type = ACCEPT;
			break;
		case 0x01:
			(void)memset(client->filter, 0, sizeof(client->filter));
			client->filter_type = REJECT;
			break;
		default:
			BT_WARN("Prohibited Filter Type 0x%02x", type);
			return -EINVAL;
	}

	return 0;
}

static void filter_add(struct bt_mesh_proxy_client *client, uint16_t addr)
{
	int i;

	BT_DBG("addr 0x%04x", addr);

	if (addr == BT_MESH_ADDR_UNASSIGNED) {
		return;
	}

	for (i = 0; i < ARRAY_SIZE(client->filter); i++) {
		if (client->filter[i] == addr) {
			return;
		}
	}

	for (i = 0; i < ARRAY_SIZE(client->filter); i++) {
		if (client->filter[i] == BT_MESH_ADDR_UNASSIGNED) {
			client->filter[i] = addr;
			return;
		}
	}
}

static void filter_remove(struct bt_mesh_proxy_client *client, uint16_t addr)
{
	int i;

	BT_DBG("addr 0x%04x", addr);

	if (addr == BT_MESH_ADDR_UNASSIGNED) {
		return;
	}

	for (i = 0; i < ARRAY_SIZE(client->filter); i++) {
		if (client->filter[i] == addr) {
			client->filter[i] = BT_MESH_ADDR_UNASSIGNED;
			return;
		}
	}
}

static void send_filter_status(struct bt_mesh_proxy_client *client,
			       struct bt_mesh_net_rx *rx,
			       struct os_mbuf *buf)
{
	struct bt_mesh_net_tx tx = {
		.sub = rx->sub,
		.ctx = &rx->ctx,
		.src = bt_mesh_primary_addr(),
	};
	uint16_t filter_size;
	int i, err;

	/* Configuration messages always have dst unassigned */
	tx.ctx->addr = BT_MESH_ADDR_UNASSIGNED;

	net_buf_simple_init(buf, 10);

	net_buf_simple_add_u8(buf, CFG_FILTER_STATUS);

	if (client->filter_type == ACCEPT) {
		net_buf_simple_add_u8(buf, 0x00);
	} else {
		net_buf_simple_add_u8(buf, 0x01);
	}

	for (filter_size = 0U, i = 0; i < ARRAY_SIZE(client->filter); i++) {
		if (client->filter[i] != BT_MESH_ADDR_UNASSIGNED) {
			filter_size++;
		}
	}

	net_buf_simple_add_be16(buf, filter_size);

	BT_DBG("%u bytes: %s", buf->om_len, bt_hex(buf->om_data, buf->om_len));

	err = bt_mesh_net_encode(&tx, buf, true);
	if (err) {
		BT_ERR("Encoding Proxy cfg message failed (err %d)", err);
		return;
	}

	err = bt_mesh_proxy_msg_send(&client->cli, BT_MESH_PROXY_CONFIG,
				     buf, NULL, NULL);
	if (err) {
		BT_ERR("Failed to send proxy cfg message (err %d)", err);
	}
}

static void proxy_filter_recv(uint16_t conn_handle,
			      struct bt_mesh_net_rx *rx, struct os_mbuf *buf)
{
	struct bt_mesh_proxy_client *client;
	uint8_t opcode;

	client = find_client(conn_handle);
	if (!client) {
		return;
	}

	opcode = net_buf_simple_pull_u8(buf);
	switch (opcode) {
	case CFG_FILTER_SET:
		filter_set(client, buf);
		send_filter_status(client, rx, buf);
		break;
		case CFG_FILTER_ADD:
			while (buf->om_len >= 2) {
				uint16_t addr;

				addr = net_buf_simple_pull_be16(buf);
				filter_add(client, addr);
			}
			send_filter_status(client, rx, buf);
			break;
		case CFG_FILTER_REMOVE:
			while (buf->om_len >= 2) {
				uint16_t addr;

				addr = net_buf_simple_pull_be16(buf);
				filter_remove(client, addr);
			}
			send_filter_status(client, rx, buf);
			break;
		default:
			BT_WARN("Unhandled configuration OpCode 0x%02x", opcode);
			break;
	}
}

static int beacon_send(struct bt_mesh_proxy_client *client, struct bt_mesh_subnet *sub)
{
	struct os_mbuf *buf = NET_BUF_SIMPLE(23);
	int rc;

	net_buf_simple_init(buf, 1);
	bt_mesh_beacon_create(sub, buf);

	rc = bt_mesh_proxy_msg_send(&client->cli, BT_MESH_PROXY_BEACON, buf,
				    NULL, NULL);
	os_mbuf_free_chain(buf);
	return rc;
}

static int send_beacon_cb(struct bt_mesh_subnet *sub, void *cb_data)
{
	struct bt_mesh_proxy_client *client = cb_data;

	return beacon_send(client, sub);
}

static void proxy_send_beacons(struct ble_npl_event *work)
{
	struct bt_mesh_proxy_client *client;

	client = ble_npl_event_get_arg(work);

	(void)bt_mesh_subnet_find(send_beacon_cb, client);
}

void bt_mesh_proxy_beacon_send(struct bt_mesh_subnet *sub)
{
	int i;

	if (!sub) {
		/* NULL means we send on all subnets */
		bt_mesh_subnet_foreach(bt_mesh_proxy_beacon_send);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(clients); i++) {
		if (clients[i].cli.conn_handle) {
			beacon_send(&clients[i], sub);
		}
	}
}

static void node_id_start(struct bt_mesh_subnet *sub)
{
	sub->node_id = BT_MESH_NODE_IDENTITY_RUNNING;
	sub->node_id_start = k_uptime_get_32();
}

void bt_mesh_proxy_identity_start(struct bt_mesh_subnet *sub)
{
	node_id_start(sub);

	/* Prioritize the recently enabled subnet */
	beacon_sub = sub;
}

void bt_mesh_proxy_identity_stop(struct bt_mesh_subnet *sub)
	{
	sub->node_id = BT_MESH_NODE_IDENTITY_STOPPED;
	sub->node_id_start = 0U;
}

int bt_mesh_proxy_identity_enable(void)
{
	BT_DBG("");

	if (!bt_mesh_is_provisioned()) {
		return -EAGAIN;
	}

	if (bt_mesh_subnet_foreach(node_id_start)) {
		bt_mesh_adv_update();
	}

	return 0;
}

#define ID_TYPE_NET  0x00
#define ID_TYPE_NODE 0x01

#define NODE_ID_LEN  19
#define NET_ID_LEN   11

#define NODE_ID_TIMEOUT (CONFIG_BT_MESH_NODE_ID_TIMEOUT * MSEC_PER_SEC)

static uint8_t proxy_svc_data[NODE_ID_LEN] = {
	BT_UUID_16_ENCODE(BT_UUID_MESH_PROXY_VAL),
};

static const struct bt_data node_id_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_MESH_PROXY_VAL)),
		      BT_DATA(BT_DATA_SVC_DATA16, proxy_svc_data, NODE_ID_LEN),
};

static const struct bt_data net_id_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_MESH_PROXY_VAL)),
		      BT_DATA(BT_DATA_SVC_DATA16, proxy_svc_data, NET_ID_LEN),
};

static int node_id_adv(struct bt_mesh_subnet *sub, int32_t duration)
{
	struct ble_gap_adv_params fast_adv_param = {
		ADV_OPT_PROXY
		ADV_FAST_INT
	};
#if ADV_OPT_USE_NAME
	const char *name = CONFIG_BT_DEVICE_NAME;
	size_t name_len = strlen(name);
	struct bt_data sd = {
		.type = BT_DATA_NAME_COMPLETE,
		.data_len = name_len,
		.data = (void *)name
	};
#else
	struct bt_data *sd = NULL;
#endif
	uint8_t tmp[16];
	int err;

	BT_DBG("");

	proxy_svc_data[2] = ID_TYPE_NODE;

	err = bt_rand(proxy_svc_data + 11, 8);
	if (err) {
		return err;
	}

	(void)memset(tmp, 0, 6);
	memcpy(tmp + 6, proxy_svc_data + 11, 8);
	sys_put_be16(bt_mesh_primary_addr(), tmp + 14);

	err = bt_encrypt_be(sub->keys[SUBNET_KEY_TX_IDX(sub)].identity, tmp,
			    tmp);
	if (err) {
		return err;
	}

	memcpy(proxy_svc_data + 3, tmp + 8, 8);

	err = bt_le_adv_start(&fast_adv_param, duration, node_id_ad,
			      ARRAY_SIZE(node_id_ad), sd, 0);
	if (err) {
		BT_WARN("Failed to advertise using Node ID (err %d)", err);
		return err;
	}

	return 0;
}

static int net_id_adv(struct bt_mesh_subnet *sub, int32_t duration)
{
	struct ble_gap_adv_params slow_adv_param = {
		ADV_OPT_PROXY
		ADV_SLOW_INT
	};
#if ADV_OPT_USE_NAME
	const char *name = CONFIG_BT_DEVICE_NAME;
	size_t name_len = strlen(name);
	struct bt_data sd = {
		.type = BT_DATA_NAME_COMPLETE,
		.data_len = name_len,
		.data = (void *)name
	};
#else
	struct bt_data *sd = NULL;
#endif
	int err;

	BT_DBG("");

	proxy_svc_data[2] = ID_TYPE_NET;

	BT_DBG("Advertising with NetId %s",
	       bt_hex(sub->keys[SUBNET_KEY_TX_IDX(sub)].net_id, 8));

	memcpy(proxy_svc_data + 3, sub->keys[SUBNET_KEY_TX_IDX(sub)].net_id, 8);

	err = bt_le_adv_start(&slow_adv_param, duration, net_id_ad,
			      ARRAY_SIZE(net_id_ad), sd, 0);
	if (err) {
		BT_WARN("Failed to advertise using Network ID (err %d)", err);
		return err;
	}

	return 0;
}

static bool advertise_subnet(struct bt_mesh_subnet *sub)
{
	if (sub->net_idx == BT_MESH_KEY_UNUSED) {
		return false;
	}

	return (sub->node_id == BT_MESH_NODE_IDENTITY_RUNNING ||
	bt_mesh_gatt_proxy_get() == BT_MESH_GATT_PROXY_ENABLED);
}

static struct bt_mesh_subnet *next_sub(void)
{
	struct bt_mesh_subnet *sub = NULL;

	if (!beacon_sub) {
		beacon_sub = bt_mesh_subnet_next(NULL);
		if (!beacon_sub) {
			/* No valid subnets */
			return NULL;
		}
	}

	sub = beacon_sub;
	do {
		if (advertise_subnet(sub)) {
			beacon_sub = sub;
			return sub;
		}

		sub = bt_mesh_subnet_next(sub);
	} while (sub != beacon_sub);

	/* No subnets to advertise on */
	return NULL;
}

static int sub_count_cb(struct bt_mesh_subnet *sub, void *cb_data)
{
	int *count = cb_data;

	if (advertise_subnet(sub)) {
		(*count)++;
	}

	return 0;
}

static int sub_count(void)
{
	int count = 0;

	(void)bt_mesh_subnet_find(sub_count_cb, &count);

	return count;
}

static int gatt_proxy_advertise(struct bt_mesh_subnet *sub)
{
	int32_t remaining = K_FOREVER;
	int subnet_count;
	int err = -EBUSY;

	BT_DBG("");

	if (conn_count == CONFIG_BT_MAX_CONN) {
		BT_DBG("Connectable advertising deferred (max connections %d)", conn_count);
		return -ENOMEM;
	}

	sub = beacon_sub ? beacon_sub : bt_mesh_subnet_next(beacon_sub);
	if (!sub) {
		BT_WARN("No subnets to advertise on");
		return -ENOENT;
	}

	subnet_count = sub_count();
	BT_DBG("sub_count %u", subnet_count);
	if (subnet_count > 1) {
		int32_t max_timeout;

		/* We use NODE_ID_TIMEOUT as a starting point since it may
		 * be less than 60 seconds. Divide this period into at least
		 * 6 slices, but make sure that a slice is at least one
		 * second long (to avoid excessive rotation).
		 */
		max_timeout = NODE_ID_TIMEOUT / max(subnet_count, 6);
		max_timeout = max(max_timeout, K_SECONDS(1));

		if (remaining > max_timeout || remaining < 0) {
			remaining = max_timeout;
		}
	}

	if (sub->node_id == BT_MESH_NODE_IDENTITY_RUNNING) {
		uint32_t active = k_uptime_get_32() - sub->node_id_start;

		if (active < NODE_ID_TIMEOUT) {
			remaining = NODE_ID_TIMEOUT - active;
			BT_DBG("Node ID active for %u ms, %d ms remaining",
			       active, remaining);
			err = node_id_adv(sub, remaining);
		} else {
			bt_mesh_proxy_identity_stop(sub);
			BT_DBG("Node ID stopped");
		}
	}

	if (sub->node_id == BT_MESH_NODE_IDENTITY_STOPPED) {
		err = net_id_adv(sub, remaining);
	}

	BT_DBG("Advertising %d ms for net_idx 0x%04x",
	       (int) remaining, sub->net_idx);

	beacon_sub = bt_mesh_subnet_next(beacon_sub);

	return err;
}

static void subnet_evt(struct bt_mesh_subnet *sub, enum bt_mesh_key_evt evt)
{
	if (evt == BT_MESH_KEY_DELETED) {
		if (sub == beacon_sub) {
			beacon_sub = NULL;
		}
	} else {
		bt_mesh_proxy_beacon_send(sub);
	}
}

static void proxy_ccc_write(uint16_t conn_handle)
{
	struct bt_mesh_proxy_client *client;

	BT_DBG("conn_handle %d", conn_handle);

	client = find_client(conn_handle);
	__ASSERT(client, "No client for connection");

	if (client->filter_type == NONE) {
		client->filter_type = ACCEPT;
		k_work_add_arg(&client->send_beacons, client);
		k_work_submit(&client->send_beacons);
	}
}

int bt_mesh_proxy_gatt_enable(void)
{
	uint16_t handle;
	int rc;
	int i;

	BT_DBG("");

	if (gatt_svc == MESH_GATT_PROXY) {
		return -EALREADY;
	}

	if (gatt_svc != MESH_GATT_NONE) {
		return -EBUSY;
	}

	rc = ble_gatts_find_svc(BLE_UUID16_DECLARE(BT_UUID_MESH_PROXY_VAL), &handle);
	assert(rc == 0);
	ble_gatts_svc_set_visibility(handle, 1);
	/* FIXME: figure out end handle */
	ble_svc_gatt_changed(svc_handles.proxy_h, 0xffff);

	gatt_svc = MESH_GATT_PROXY;

	for (i = 0; i < ARRAY_SIZE(clients); i++) {
		if (clients[i].cli.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
			clients[i].filter_type = ACCEPT;
		}
	}
	return 0;
}

void bt_mesh_proxy_gatt_disconnect(void)
{
	int rc;
	int i;

	BT_DBG("");

	for (i = 0; i < ARRAY_SIZE(clients); i++) {
		struct bt_mesh_proxy_client *client = &clients[i];

		if ((client->cli.conn_handle != BLE_HS_CONN_HANDLE_NONE) &&
		    (client->filter_type == ACCEPT ||
		    client->filter_type == REJECT)) {
			client->filter_type = NONE;
			rc = ble_gap_terminate(client->cli.conn_handle,
			                       BLE_ERR_REM_USER_CONN_TERM);
			assert(rc == 0);
		}
	}
}

int bt_mesh_proxy_gatt_disable(void)
{
	uint16_t handle;
	int rc;
	BT_DBG("");

	if (gatt_svc == MESH_GATT_NONE) {
		return -EALREADY;
	}

	if (gatt_svc != MESH_GATT_PROXY) {
		return -EBUSY;
	}

	bt_mesh_proxy_gatt_disconnect();

	rc = ble_gatts_find_svc(BLE_UUID16_DECLARE(BT_UUID_MESH_PROXY_VAL), &handle);
	assert(rc == 0);
	ble_gatts_svc_set_visibility(handle, 0);
	/* FIXME: figure out end handle */
	ble_svc_gatt_changed(svc_handles.proxy_h, 0xffff);
	gatt_svc = MESH_GATT_NONE;

	return 0;
}

void bt_mesh_proxy_addr_add(struct os_mbuf *buf, uint16_t addr)
{
	struct bt_mesh_proxy_client *client =
		CONTAINER_OF(buf, struct bt_mesh_proxy_client, cli.buf);

	BT_DBG("filter_type %u addr 0x%04x", client->filter_type, addr);

	if (client->filter_type == ACCEPT) {
		filter_add(client, addr);
	} else if (client->filter_type == REJECT) {
		filter_remove(client, addr);
	}
}

static bool client_filter_match(struct bt_mesh_proxy_client *client,
				uint16_t addr)
{
	int i;

	BT_DBG("filter_type %u addr 0x%04x", client->filter_type, addr);

	if (client->filter_type == REJECT) {
		for (i = 0; i < ARRAY_SIZE(client->filter); i++) {
			if (client->filter[i] == addr) {
				return false;
			}
		}

		return true;
	}

	if (addr == BT_MESH_ADDR_ALL_NODES) {
		return true;
	}

	if (client->filter_type == ACCEPT) {
		for (i = 0; i < ARRAY_SIZE(client->filter); i++) {
			if (client->filter[i] == addr) {
				return true;
			}
		}
	}

	return false;
}

static void buf_send_end(uint16_t conn_handle, void *user_data)
{
	struct os_mbuf *buf = user_data;

	net_buf_unref(buf);
}

bool bt_mesh_proxy_relay(struct os_mbuf *buf, uint16_t dst)
{
	const struct bt_mesh_send_cb *cb = BT_MESH_ADV(buf)->cb;
	void *cb_data = BT_MESH_ADV(buf)->cb_data;
	bool relayed = false;
	int i, err;

	BT_DBG("%u bytes to dst 0x%04x", buf->om_len, dst);

	for (i = 0; i < ARRAY_SIZE(clients); i++) {
		struct bt_mesh_proxy_client *client = &clients[i];
		struct os_mbuf *msg;

		if (client->cli.conn_handle == BLE_HS_CONN_HANDLE_NONE) {
			continue;
		}

		if (!client_filter_match(client, dst)) {
			continue;
		}

		if (client->filter_type == PROV) {
			BT_ERR("Invalid PDU type for Proxy Client");
			return -EINVAL;
		}

		/* Proxy PDU sending modifies the original buffer,
		 * so we need to make a copy.
		 */
		msg = NET_BUF_SIMPLE(32);
		net_buf_simple_init(msg, 1);
		net_buf_simple_add_mem(msg, buf->om_data, buf->om_len);

		err = bt_mesh_proxy_msg_send(&client->cli, BT_MESH_PROXY_NET_PDU,
					     msg, buf_send_end, net_buf_ref(buf));

		adv_send_start(0, err, cb, cb_data);
		if (err) {
			BT_ERR("Failed to send proxy message (err %d)", err);

			/* If segment_and_send() fails the buf_send_end() callback will
			 * not be called, so we need to clear the user data (net_buf,
			 * which is just opaque data to segment_and send) reference given
			 * to segment_and_send() here.
			 */
			net_buf_unref(buf);
			continue;
		}
		os_mbuf_free_chain(msg);
		relayed = true;
	}

	return relayed;
}


static void proxy_sar_timeout(struct ble_npl_event *work)
{
	struct bt_mesh_proxy_role *role;
	int rc;
	role = ble_npl_event_get_arg(work);


	BT_WARN("Proxy SAR timeout");

	if (role->conn_handle) {
		rc = ble_gap_terminate(role->conn_handle,
				       BLE_ERR_REM_USER_CONN_TERM);
		assert(rc == 0);
	}
}
#endif /* CONFIG_BT_MESH_GATT_PROXY */

static void gatt_connected(uint16_t conn_handle)
{
	struct bt_mesh_proxy_client *client;

	BT_DBG("conn %d", conn_handle);

	conn_count++;

	/* Try to re-enable advertising in case it's possible */
	if (conn_count < CONFIG_BT_MAX_CONN) {
		bt_mesh_adv_update();
	}

	client = find_client(BLE_HS_CONN_HANDLE_NONE);
	if (!client) {
		BT_ERR("No free Proxy Client objects");
		return;
	}

	client->cli.conn_handle = conn_handle;
	client->filter_type = NONE;
	(void)memset(client->filter, 0, sizeof(client->filter));
	net_buf_simple_init(client->cli.buf, 0);
}

static void gatt_disconnected(uint16_t conn_handle, uint8_t reason)
{
	BT_DBG("conn handle %u reason 0x%02x", conn_handle, reason);
	struct bt_mesh_proxy_client *client;

	conn_count--;

	client = find_client(conn_handle);
	if (!client) {
		BT_WARN("No Gatt Client found");
		return;
	}

	if (IS_ENABLED(CONFIG_BT_MESH_PB_GATT) && client->filter_type == PROV) {
		bt_mesh_pb_gatt_close(conn_handle);
		client->filter_type = NONE;

		if (bt_mesh_is_provisioned() &&
		    IS_ENABLED(CONFIG_BT_MESH_GATT_PROXY) &&
		    bt_mesh_gatt_proxy_get() != BT_MESH_GATT_PROXY_NOT_SUPPORTED) {
			(void)bt_mesh_proxy_gatt_enable();
		}
	}

	/* If this fails, the work handler exits early, as
	 * there's no active connection.
	 */
	(void)k_work_cancel_delayable(&client->cli.sar_timer);
	client->cli.conn_handle = BLE_HS_CONN_HANDLE_NONE;

	bt_mesh_adv_update();
}

struct os_mbuf *bt_mesh_proxy_get_buf(void)
{
	struct os_mbuf *buf = clients[0].cli.buf;

	if (buf != NULL) {
		net_buf_simple_init(buf, 0);
	}

	return buf;
}

#if defined(CONFIG_BT_MESH_PB_GATT)
static void prov_ccc_write(uint16_t conn_handle)
{
	struct bt_mesh_proxy_client *client;

	BT_DBG("conn_handle %d", conn_handle);

	/* If a connection exists there must be a client */
	client = find_client(conn_handle);
	__ASSERT(client, "No client for connection");

	if (client->filter_type == NONE) {
		client->filter_type = PROV;
		bt_mesh_pb_gatt_open(conn_handle);
	}
}

static int
dummy_access_cb(uint16_t conn_handle, uint16_t attr_handle,
		struct ble_gatt_access_ctxt *ctxt, void *arg)
{
	/*
	 * We should never never enter this callback - it's attached to notify-only
	 * characteristic which are notified directly from mbuf. And we can't pass
	 * NULL as access_cb because gatts will assert on init...
	 */
	BLE_HS_DBG_ASSERT(0);
	return 0;
}

static const struct ble_gatt_svc_def svc_defs [] = {
	{
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = BLE_UUID16_DECLARE(BT_UUID_MESH_PROXY_VAL),
		.characteristics = (struct ble_gatt_chr_def[]) { {
				.uuid = BLE_UUID16_DECLARE(BT_UUID_MESH_PROXY_DATA_IN_VAL),
				.access_cb = gatt_recv,
				.flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
			}, {
				.uuid = BLE_UUID16_DECLARE(BT_UUID_MESH_PROXY_DATA_OUT_VAL),
				.access_cb = dummy_access_cb,
				.flags = BLE_GATT_CHR_F_NOTIFY,
			}, {
			0, /* No more characteristics in this service. */
		} },
	}, {
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = BLE_UUID16_DECLARE(BT_UUID_MESH_PROV_VAL),
		.characteristics = (struct ble_gatt_chr_def[]) { {
				.uuid = BLE_UUID16_DECLARE(BT_UUID_MESH_PROV_DATA_IN_VAL),
				.access_cb = gatt_recv,
				.flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
			}, {
				.uuid = BLE_UUID16_DECLARE(BT_UUID_MESH_PROV_DATA_OUT_VAL),
				.access_cb = dummy_access_cb,
				.flags = BLE_GATT_CHR_F_NOTIFY,
			}, {
				0, /* No more characteristics in this service. */
		} },
	}, {
		0, /* No more services. */
	},
};

int bt_mesh_proxy_svcs_register(void)
{
	int rc;

	rc = ble_gatts_count_cfg(svc_defs);
	assert(rc == 0);

	rc = ble_gatts_add_svcs(svc_defs);
	assert(rc == 0);

	return 0;
}

int bt_mesh_proxy_prov_enable(void)
{
	uint16_t handle;
	int rc;
	int i;

	BT_DBG("");

	if (gatt_svc == MESH_GATT_PROV) {
		return -EALREADY;
	}

	if (gatt_svc != MESH_GATT_NONE) {
		return -EBUSY;
	}

	rc = ble_gatts_find_svc(BLE_UUID16_DECLARE(BT_UUID_MESH_PROV_VAL), &handle);
	assert(rc == 0);
	ble_gatts_svc_set_visibility(handle, 1);
	/* FIXME: figure out end handle */
	ble_svc_gatt_changed(svc_handles.prov_h, 0xffff);

	gatt_svc = MESH_GATT_PROV;
	prov_fast_adv = true;

	for (i = 0; i < ARRAY_SIZE(clients); i++) {
		if (clients[i].cli.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
			clients[i].filter_type = PROV;
		}
	}


	return 0;
}

int bt_mesh_proxy_prov_disable(void)
{
	uint16_t handle;
	int rc;

	BT_DBG("");

	if (gatt_svc == MESH_GATT_NONE) {
		return -EALREADY;
	}

	if (gatt_svc != MESH_GATT_PROV) {
		return -EBUSY;
	}

	rc = ble_gatts_find_svc(BLE_UUID16_DECLARE(BT_UUID_MESH_PROV_VAL), &handle);
	assert(rc == 0);
	ble_gatts_svc_set_visibility(handle, 0);
	/* FIXME: figure out end handle */
	ble_svc_gatt_changed(svc_handles.prov_h, 0xffff);

	gatt_svc = MESH_GATT_NONE;

	bt_mesh_adv_update();

	return 0;
}

static uint8_t prov_svc_data[20] = {
	BT_UUID_16_ENCODE(BT_UUID_MESH_PROV_VAL),
};

static const struct bt_data prov_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_MESH_PROV_VAL)),
		      BT_DATA(BT_DATA_SVC_DATA16, prov_svc_data, sizeof(prov_svc_data)),
};

int bt_mesh_pb_gatt_send(uint16_t conn_handle, struct os_mbuf *buf,
			 void (*end)(uint16_t, void *), void *user_data)
{
	struct bt_mesh_proxy_client *client = find_client(conn_handle);

	if (!client) {
		BT_ERR("No Proxy Client found");
		return -ENOTCONN;
	}

	if (client->filter_type != PROV) {
		BT_ERR("Invalid PDU type for Proxy Client");
		return -EINVAL;
	}

	return bt_mesh_proxy_msg_send(&client->cli, BT_MESH_PROXY_PROV, buf, end, user_data);
}

static size_t gatt_prov_adv_create(struct bt_data prov_sd[2])
{
	const struct bt_mesh_prov *prov = bt_mesh_prov_get();
	const char *name = CONFIG_BT_DEVICE_NAME;
	size_t name_len = strlen(name);
	size_t prov_sd_len = 0;
	size_t sd_space = 31;

	memcpy(prov_svc_data + 2, prov->uuid, 16);
	sys_put_be16(prov->oob_info, prov_svc_data + 18);

	if (prov->uri) {
		size_t uri_len = strlen(prov->uri);

		if (uri_len > 29) {
			/* There's no way to shorten an URI */
			BT_WARN("Too long URI to fit advertising packet");
		} else {
			prov_sd[0].type = BT_DATA_URI;
			prov_sd[0].data_len = uri_len;
			prov_sd[0].data = (const uint8_t *)prov->uri;
			sd_space -= 2 + uri_len;
			prov_sd_len++;
		}
	}

	if (sd_space > 2 && name_len > 0) {
		sd_space -= 2;

		if (sd_space < name_len) {
			prov_sd[prov_sd_len].type = BT_DATA_NAME_SHORTENED;
			prov_sd[prov_sd_len].data_len = sd_space;
		} else {
			prov_sd[prov_sd_len].type = BT_DATA_NAME_COMPLETE;
			prov_sd[prov_sd_len].data_len = name_len;
		}

		prov_sd[prov_sd_len].data = (void *)name;
		prov_sd_len++;
	}

	return prov_sd_len;
}
#endif /* CONFIG_BT_MESH_PB_GATT */

static int proxy_send(uint16_t conn_handle,
		      const void *data, uint16_t len,
		      void (*end)(uint16_t, void *), void *user_data)
{
	struct os_mbuf *om;
	int err = 0;

	BT_DBG("%u bytes: %s", len, bt_hex(data, len));

#if (MYNEWT_VAL(BLE_MESH_GATT_PROXY))
	if (gatt_svc == MESH_GATT_PROXY) {
		om = ble_hs_mbuf_from_flat(data, len);
		assert(om);
		err = ble_gattc_notify_custom(conn_handle, svc_handles.proxy_data_out_h, om);
		/* We do not pass cb into ble_gattc_notify_custom - execute it at the end */
	}
#endif

#if (MYNEWT_VAL(BLE_MESH_PB_GATT))
		if (gatt_svc == MESH_GATT_PROV) {
		om = ble_hs_mbuf_from_flat(data, len);
		assert(om);
		err = ble_gattc_notify_custom(conn_handle, svc_handles.prov_data_out_h, om);
	}
#endif

	end(conn_handle, user_data);
	return err;
}

int bt_mesh_proxy_adv_start(void)
{
	BT_DBG("");

	if (gatt_svc == MESH_GATT_NONE) {
		return -ENOENT;
	}

#if (MYNEWT_VAL(BLE_MESH_PB_GATT))
	if (!bt_mesh_is_provisioned()) {
		const struct ble_gap_adv_params *param;
		struct ble_gap_adv_params fast_adv_param = {
			ADV_OPT_PROV
			ADV_FAST_INT
		};
		struct ble_gap_adv_params slow_adv_param = {
			ADV_OPT_PROV
			ADV_SLOW_INT
		};
		struct bt_data prov_sd[1];
		size_t prov_sd_len;
		int err;

		prov_sd_len = gatt_prov_adv_create(prov_sd);
		if (!prov_fast_adv) {
			param = &slow_adv_param;
			return bt_mesh_adv_start(param,
						 K_FOREVER, prov_ad,
						 ARRAY_SIZE(prov_ad), prov_sd,
						 prov_sd_len);
		} else {
			param = &fast_adv_param;
			err = bt_mesh_adv_start(param,
						(60 * MSEC_PER_SEC), prov_ad,
						ARRAY_SIZE(prov_ad), prov_sd,
						prov_sd_len);
			if (!err) {
				prov_fast_adv = false;
			}

			return err;
		}
	}
#endif /* PB_GATT */

#if (MYNEWT_VAL(BLE_MESH_GATT_PROXY))
	if (bt_mesh_is_provisioned()) {
		return gatt_proxy_advertise(next_sub());
	}
#endif /* GATT_PROXY */

	return -ENOTSUP;
}


static void ble_mesh_handle_connect(struct ble_gap_event *event, void *arg)
{
#if MYNEWT_VAL(BLE_EXT_ADV)
	/* When EXT ADV is enabled then mesh proxy is connected
	 * when proxy advertising instance is completed.
	 * Therefore no need to handle BLE_GAP_EVENT_CONNECT
	 */
	if (event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
		/* Reason 0 means advertising has been completed because
		 * connection has been established
		 */
		if (event->adv_complete.reason != 0) {
			return;
		}

		if (event->adv_complete.instance != BT_MESH_ADV_GATT_INST) {
			return;
		}

		gatt_connected(event->adv_complete.conn_handle);
	}
#else
	if (event->type == BLE_GAP_EVENT_CONNECT) {
		gatt_connected(event->connect.conn_handle);
	}
#endif
}

int ble_mesh_proxy_gap_event(struct ble_gap_event *event, void *arg)
{
	if ((event->type == BLE_GAP_EVENT_CONNECT) ||
	(event->type == BLE_GAP_EVENT_ADV_COMPLETE)) {
		ble_mesh_handle_connect(event, arg);
	} else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
		gatt_disconnected(event->disconnect.conn.conn_handle,
				   event->disconnect.reason);
	} else if (event->type == BLE_GAP_EVENT_SUBSCRIBE) {
		if (event->subscribe.attr_handle == svc_handles.proxy_data_out_h) {
#if (MYNEWT_VAL(BLE_MESH_GATT_PROXY))
			proxy_ccc_write(event->subscribe.conn_handle);
#endif
		} else if (event->subscribe.attr_handle ==
		svc_handles.prov_data_out_h) {
#if (MYNEWT_VAL(BLE_MESH_PB_GATT))
			prov_ccc_write(event->subscribe.conn_handle);
#endif
		}
	}

	return 0;
}

static void resolve_svc_handles(void)
{
	int rc;

	/* Either all handles are already resolved, or none of them */
	if (svc_handles.prov_data_out_h) {
		return;
	}

	/*
	 * We assert if attribute is not found since at this stage all attributes
	 * shall be already registered and thus shall be found.
	 */

	rc = ble_gatts_find_svc(BLE_UUID16_DECLARE(BT_UUID_MESH_PROXY_VAL),
				&svc_handles.proxy_h);
	assert(rc == 0);

	rc = ble_gatts_find_chr(BLE_UUID16_DECLARE(BT_UUID_MESH_PROXY_VAL),
				BLE_UUID16_DECLARE(BT_UUID_MESH_PROXY_DATA_OUT_VAL),
				NULL, &svc_handles.proxy_data_out_h);
	assert(rc == 0);

	rc = ble_gatts_find_svc(BLE_UUID16_DECLARE(BT_UUID_MESH_PROV_VAL),
				&svc_handles.prov_h);
	assert(rc == 0);

	rc = ble_gatts_find_chr(BLE_UUID16_DECLARE(BT_UUID_MESH_PROV_VAL),
				BLE_UUID16_DECLARE(BT_UUID_MESH_PROV_DATA_IN_VAL),
				NULL, &svc_handles.prov_data_in_h);
	assert(rc == 0);

	rc = ble_gatts_find_chr(BLE_UUID16_DECLARE(BT_UUID_MESH_PROV_VAL),
				BLE_UUID16_DECLARE(BT_UUID_MESH_PROV_DATA_OUT_VAL),
				NULL, &svc_handles.prov_data_out_h);
	assert(rc == 0);
}


int bt_mesh_proxy_init(void)
{
		int i;

	#if (MYNEWT_VAL(BLE_MESH_GATT_PROXY))
			if (!bt_mesh_subnet_cb_list[4]) {
			bt_mesh_subnet_cb_list[4] = subnet_evt;
		}
	#endif

		for (i = 0; i < MYNEWT_VAL(BLE_MAX_CONNECTIONS); ++i) {
	#if (MYNEWT_VAL(BLE_MESH_GATT_PROXY))
			k_work_init(&clients[i].send_beacons, proxy_send_beacons);
	#endif
			clients[i].cli.buf = NET_BUF_SIMPLE(CLIENT_BUF_SIZE);
			clients[i].cli.conn_handle = BLE_HS_CONN_HANDLE_NONE;

			k_work_init_delayable(&clients[i].cli.sar_timer, proxy_sar_timeout);
			k_work_add_arg_delayable(&clients[i].cli.sar_timer, &clients[i]);
		}

		resolve_svc_handles();

		ble_gatts_svc_set_visibility(svc_handles.proxy_h, 0);
		ble_gatts_svc_set_visibility(svc_handles.prov_h, 0);

	return 0;
}