/**
 * @file testing.h
 * @brief Internal API for Bluetooth testing.
 */

/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __BT_TESTING_H
#define __BT_TESTING_H

#include "slist.h"
#include "glue.h"
#include "access.h"

/**
 * @brief Bluetooth testing
 * @defgroup bt_test_cb Bluetooth testing callbacks
 * @ingroup bluetooth
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Bluetooth Testing callbacks structure.
 *
 *  Callback structure to be used for Bluetooth testing purposes.
 *  Allows access to Bluetooth stack internals, not exposed by public API.
 */
struct bt_test_cb {
	void (*mesh_net_recv)(u8_t ttl, u8_t ctl, u16_t src, u16_t dst,
			      const void *payload, size_t payload_len);
	void (*mesh_model_bound)(u16_t addr, struct bt_mesh_model *model,
				 u16_t key_idx);
	void (*mesh_model_unbound)(u16_t addr, struct bt_mesh_model *model,
				   u16_t key_idx);
	void (*mesh_prov_invalid_bearer)(u8_t opcode);
	void (*mesh_trans_incomp_timer_exp)(void);

	sys_snode_t node;
};

/** Register callbacks for Bluetooth testing purposes
 *
 *  @param cb bt_test_cb callback structure
 */
void bt_test_cb_register(struct bt_test_cb *cb);

/** Unregister callbacks for Bluetooth testing purposes
 *
 *  @param cb bt_test_cb callback structure
 */
void bt_test_cb_unregister(struct bt_test_cb *cb);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* __BT_TESTING_H */
