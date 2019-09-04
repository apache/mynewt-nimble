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

#include "mesh/mesh.h"
#include "model.h"
#include "bt_mesh_helper.h"

//=============================================================================
// Globale definitions and variables
//=============================================================================

#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG_MODEL))

//=============================================================================
// Sensor Server
//=============================================================================

static void add_marshalled_sensor_data(struct os_mbuf *msg,
        struct bt_mesh_sensor *state)
{
    uint8_t *mpid = NULL;
    uint8_t *raw_value = NULL;

    /*
     * check payload length and property id length for
     * accurate marshalling
     */
    if (state->data_length <= 16 && state->descriptor.property_id < 2048)
    {
        /*
         * | Format0 | Length  |      Property ID        |
         * | 0       | 1 2 3 4 | 5 6 7 | 0 1 2 3 4 5 6 7 |
         * |<--------- octet0 -------->|<--- octet1 ---->|
         */

        uint16_t temp_mpid = 0;

        mpid = net_buf_simple_add(msg, 2);

        /* payload length */
        temp_mpid = temp_mpid | state->data_length;

        /* property id */
        temp_mpid = temp_mpid << 11;
        temp_mpid = temp_mpid | state->descriptor.property_id;

        BT_DBG("mpid = 0x%x", temp_mpid);

        memcpy(mpid, &temp_mpid, 2);
    }
    else
    {
        /*
         * | Format1 | Length        |            Property ID            |
         * | 0       | 1 2 3 4 5 6 7 | 0 1 2 3 4 5 6 7 | 0 1 2 3 4 5 6 7 |
         * |<-------- octet0 ------->|<--- octet1 ---->|<--- octet2 ---->|
         */

        uint32_t temp_mpid = 0x1;

        mpid = net_buf_simple_add(msg, 3);

        /* payload length */
        temp_mpid = temp_mpid << 7;
        temp_mpid = temp_mpid | state->data_length;

        /* property id */
        temp_mpid = temp_mpid << 16;
        temp_mpid = temp_mpid | state->descriptor.property_id;

        /* since we have 32 bit variable */
        temp_mpid = temp_mpid << 8;

        BT_DBG("mpid = 0x%x", temp_mpid);

        memcpy(mpid, &temp_mpid, 3);
    }

    ble_hs_log_flat_buf(raw_value, state->data_length);

    raw_value = net_buf_simple_add(msg, state->data_length);
    memcpy(raw_value, state->data, state->data_length);
}

static void sensor_status(struct bt_mesh_model *model,
           struct bt_mesh_msg_ctx *ctx,
           uint16_t property_id)
{
    struct bt_mesh_model_sensor_srv *srv = model->user_data;

    if (srv)
    {
        struct os_mbuf *msg = NET_BUF_SIMPLE(4);
        int err, i;

        bt_mesh_model_msg_init(msg, OP_SENSOR_STATUS);

        /* send status for the particular property id */
        if (property_id)
        {
            uint32_t is_property_id_matched = false;

            /* find property id */
            for (i = 0; i < srv->sensor_count; i++)
            {
                struct bt_mesh_sensor state = srv->sensors[i];

                if (property_id == state.descriptor.property_id)
                {
                    is_property_id_matched = true;
                    add_marshalled_sensor_data(msg, &state);
                    break;
                }
            }

            if (!is_property_id_matched)
            {
                uint8_t *mpid = NULL;

                /*
                 * check payload length and property id length for
                 * accurate marshalling
                 */
                if (property_id < 2048)
                {
                    uint16_t temp_mpid = 0;

                    mpid = net_buf_simple_add(msg, 2);

                    temp_mpid = temp_mpid | property_id;

                    BT_DBG("mpid = 0x%x", temp_mpid);

                    memcpy(mpid, &temp_mpid, 2);
                }
                else
                {
                    uint32_t temp_mpid = 0x1;

                    mpid = net_buf_simple_add(msg, 3);

                    /* payload length */
                    temp_mpid = temp_mpid << 7;

                    /* property id */
                    temp_mpid = temp_mpid << 16;
                    temp_mpid = temp_mpid | property_id;

                    /* since we have 32 bit variable */
                    temp_mpid = temp_mpid << 8;

                    BT_DBG("mpid = 0x%x", temp_mpid);

                    memcpy(mpid, &temp_mpid, 3);
                }
            }
        }
        else
        {
            for (i = 0; i < srv->sensor_count; i++)
            {
                add_marshalled_sensor_data(msg, &srv->sensors[i]);
            }
        }

        ble_hs_log_flat_buf(msg->om_databuf, msg->om_len);

        err = bt_mesh_model_send(model, ctx, msg, NULL, NULL);
        if (err)
        {
            BT_ERR("Send status failed error=%d", err);
        }

        os_mbuf_free_chain(msg);
    }
}

static void sensor_get(struct bt_mesh_model *model,
           struct bt_mesh_msg_ctx *ctx,
           struct os_mbuf *buf)
{
    uint16_t property_id = 0;

    if (buf)
    {
        property_id = net_buf_simple_pull_le16(buf);
    }

    sensor_status(model, ctx, property_id);
}

static void sensor_descriptor_status(struct bt_mesh_model *model,
           struct bt_mesh_msg_ctx *ctx,
           uint16_t property_id)
{
    struct bt_mesh_model_sensor_srv *srv = model->user_data;

    if (srv)
    {
        int err, i;
        struct os_mbuf *msg = NET_BUF_SIMPLE(2);
        bt_mesh_model_msg_init(msg, OP_SENSOR_DESCRIPTOR_STATUS);
        struct bt_mesh_sensor *state = NULL;
        uint8_t *descriptor = NULL;
        size_t des_size = sizeof(struct bt_mesh_sensor_descriptor);

        /* send status for the particular property id */
        if (property_id)
        {
            /* find property id */
            for (i = 0; i < srv->sensor_count; i++)
            {
                if (property_id == srv->sensors[i].descriptor.property_id)
                {
                    state = &srv->sensors[i];
                    break;
                }
            }

            if (state)
            {
                descriptor = net_buf_simple_add(msg, des_size);
                memcpy(descriptor, &state->descriptor, des_size);
            }
            else
            {
                descriptor = net_buf_simple_add(msg, 2);
                memcpy(descriptor, &property_id, 2);
            }
        }
        else
        {
            for (i = 0; i < srv->sensor_count; i++)
            {
                state = &srv->sensors[i];

                descriptor = net_buf_simple_add(msg, des_size);
                memcpy(descriptor, &state->descriptor, des_size);
            }
        }

        err = bt_mesh_model_send(model, ctx, msg, NULL, NULL);
        if (err)
        {
            BT_ERR("Send status failed error=%d", err);
        }

        os_mbuf_free_chain(msg);
    }
}

static void sensor_descriptor_get(struct bt_mesh_model *model,
           struct bt_mesh_msg_ctx *ctx,
           struct os_mbuf *buf)
{
    uint16_t property_id = 0;

    if (buf)
    {
        property_id = net_buf_simple_pull_le16(buf);
    }

    sensor_descriptor_status(model, ctx, property_id);
}

const struct bt_mesh_model_op sensor_srv_op[] = {
   { OP_SENSOR_GET,             0, sensor_get },
   { OP_SENSOR_DESCRIPTOR_GET,  0, sensor_descriptor_get },
   BT_MESH_MODEL_OP_END,
};
