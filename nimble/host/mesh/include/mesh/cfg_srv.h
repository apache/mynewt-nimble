/** @file
 *  @brief Bluetooth Mesh Configuration Server Model APIs.
 */

/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BT_MESH_CFG_SRV_H
#define __BT_MESH_CFG_SRV_H

/**
 * @brief Bluetooth Mesh
 * @defgroup bt_mesh_cfg_srv Bluetooth Mesh Configuration Server Model
 * @ingroup bt_mesh
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Mesh Configuration Server Model Context */
struct bt_mesh_cfg_srv {
	struct bt_mesh_model *model;

	uint8_t net_transmit;         /* Network Transmit state */
	uint8_t relay;                /* Relay Mode state */
	uint8_t relay_retransmit;     /* Relay Retransmit state */
	uint8_t beacon;               /* Secure Network Beacon state */
	uint8_t gatt_proxy;           /* GATT Proxy state */
	uint8_t frnd;                 /* Friend state */
	uint8_t default_ttl;          /* Default TTL */

	/* Heartbeat Publication */
	struct bt_mesh_hb_pub {
		struct k_delayed_work timer;

		uint16_t dst;
		uint16_t count;
		uint8_t  period;
		uint8_t  ttl;
		uint16_t feat;
		uint16_t net_idx;
	} hb_pub;

	/* Heartbeat Subscription */
	struct bt_mesh_hb_sub {
		int64_t  expiry;

		uint16_t src;
		uint16_t dst;
		uint16_t count;
		uint8_t  min_hops;
		uint8_t  max_hops;

		/* Optional subscription tracking function */
		void (*func)(uint8_t hops, uint16_t feat);
	} hb_sub;
};

extern const struct bt_mesh_model_op bt_mesh_cfg_srv_op[];
extern const struct bt_mesh_model_cb bt_mesh_cfg_srv_cb;

#define BT_MESH_MODEL_CFG_SRV(srv_data)                                        \
	BT_MESH_MODEL_CB(BT_MESH_MODEL_ID_CFG_SRV, bt_mesh_cfg_srv_op, NULL,   \
			 srv_data, &bt_mesh_cfg_srv_cb)

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* __BT_MESH_CFG_SRV_H */
