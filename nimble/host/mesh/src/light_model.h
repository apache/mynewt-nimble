/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BT_MESH_LIGHT_MODEL_H
#define __BT_MESH_LIGHT_MODEL_H

#include "syscfg/syscfg.h"
#include "mesh/mesh.h"

#if MYNEWT_VAL(BLE_MESH_SHELL_MODELS)
int light_model_gen_onoff_get(struct bt_mesh_model *model, u8_t *state);
int light_model_gen_onoff_set(struct bt_mesh_model *model, u8_t state);
int light_model_gen_level_get(struct bt_mesh_model *model, s16_t *level);
int light_model_gen_level_set(struct bt_mesh_model *model, s16_t level);
int light_model_init(void);
#else
static inline int light_model_gen_onoff_get(struct bt_mesh_model *model, u8_t *state) { return 0; }
static inline int light_model_gen_onoff_set(struct bt_mesh_model *model, u8_t state) { return 0; }
static inline int light_model_gen_level_get(struct bt_mesh_model *model, s16_t *level) { return 0; }
static inline int light_model_gen_level_set(struct bt_mesh_model *model, s16_t level) { return 0; }
static inline int light_model_init(void) { return 0; }
#endif

#endif
