/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __MODEL_SRV_H__
#define __MODEL_SRV_H__

struct bt_mesh_gen_onoff_srv_cb {
    int (*get)(struct bt_mesh_model *model, u8_t *state);
    int (*set)(struct bt_mesh_model *model, u8_t state);
};

extern const struct bt_mesh_model_op gen_onoff_srv_op[];

#define BT_MESH_MODEL_GEN_ONOFF_SRV(srv, pub)		\
	BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_ONOFF_SRV,	\
		      gen_onoff_srv_op, pub, srv)

struct bt_mesh_gen_level_srv_cb {
    int (*get)(struct bt_mesh_model *model, s16_t *level);
    int (*set)(struct bt_mesh_model *model, s16_t level);
};

extern const struct bt_mesh_model_op gen_level_srv_op[];

#define BT_MESH_MODEL_GEN_LEVEL_SRV(srv, pub)		\
	BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_LEVEL_SRV,	\
		      gen_level_srv_op, pub, srv)

int pwm_init(void);

#endif
