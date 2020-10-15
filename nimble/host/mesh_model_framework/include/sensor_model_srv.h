/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef __SENSOR_MODEL_SRV_H__
#define __SENSOR_MODEL_SRV_H__

#include "mesh/mesh.h"
#include "model.h"

extern const struct bt_mesh_model_op sensor_srv_op[];

#define BT_MESH_MODEL_SENSOR_SRV(srv, pub)       \
    BT_MESH_MODEL(BT_MESH_MODEL_ID_SENSOR_SRV, sensor_srv_op, pub, srv)

#define BT_MESH_SENSOR(_data, _id, _ptol, _ntol, _sfunc, _mperiod, _interval) \
{                                       \
    .data = (uint8_t *) &_data,         \
    .data_length = sizeof(_data),       \
    .descriptor = {                     \
        .property_id = _id,             \
        .postive_tolerance = _ptol,     \
        .negative_tolerance = _ntol,    \
        .sampling_function = _sfunc,    \
        .measurement_period = _mperiod, \
        .update_interval = _interval,   \
    },                                  \
}

struct bt_mesh_sensor_descriptor {
    /*
     * The Sensor Descriptor state represents the attributes describing the
     * sensor data. This state does not change throughout the lifetime of an
     * element.
     */
    uint64_t property_id        : 16,
             postive_tolerance  : 12,
             negative_tolerance : 12,
             sampling_function  : 8,
             measurement_period : 8,
             update_interval    : 8;
};

struct bt_mesh_sensor {
    /*
     * The Sensor Data state is a sequence of one or more pairs of Sensor
     * Property ID and Raw Value fields, with each Raw Value field size and
     * represent ation defined by the characteristics referenced by the Sensor
     * Property ID.
     */
    const size_t data_length;
    uint8_t * const data;

    struct bt_mesh_sensor_descriptor descriptor;
};

struct bt_mesh_model_sensor_srv {
    /*
     * Multiple instances of Sensor states may be present within the same model,
     * provided that each instance has a unique value of the Sensor Property ID
     * to allow the instances to be differentiated.
     */
    const size_t sensor_count;
    struct bt_mesh_sensor * const sensors;

    struct bt_mesh_model_pub pub;
};

#endif /* __SENSOR_MODEL_SRV_H__ */
