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

#include <assert.h>

#include "sysinit/sysinit.h"
#include "host/ble_hs.h"
#include "services/gatt/ble_svc_gatt.h"


#define CLIENT_SUPPORTED_FEATURES_MAX_OCTETS        (1)
typedef enum {
    ROBUST_CACHING = 0,
    MAX_SUPPORTED_FEATURES
} client_supported_features;

static struct ble_gap_event_listener ble_svc_gatt_gap_listener;

static uint16_t ble_svc_gatt_changed_val_handle;
static uint16_t ble_svc_gatt_start_handle;
static uint16_t ble_svc_gatt_end_handle;

static uint8_t ble_svc_gatt_client_supported_features_val
                        [CLIENT_SUPPORTED_FEATURES_MAX_OCTETS];

static int
ble_svc_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                    struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def ble_svc_gatt_defs[] = {
    {
        /*** Service: GATT */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_GATT_SVC_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16),
            .access_cb = ble_svc_gatt_access,
            .val_handle = &ble_svc_gatt_changed_val_handle,
            .flags = BLE_GATT_CHR_F_INDICATE,
        }, {
            .uuid =
            BLE_UUID16_DECLARE(BLE_SVC_GATT_CHR_CLIENT_SUPPORTED_FEATURES_UUID16),
            .access_cb = ble_svc_gatt_access,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
        }, {
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_GATT_CHR_DATABASE_HASH_UUID16),
            .access_cb = ble_svc_gatt_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            0, /* No more characteristics in this service. */
        } },
    },

    {
        0, /* No more services. */
    },
};

static int
ble_svc_gatt_service_changed_read_access(struct ble_gatt_access_ctxt *ctxt)
{
    uint8_t *u8p;

    u8p = os_mbuf_extend(ctxt->om, 4);
    if (u8p == NULL) {
        return BLE_HS_ENOMEM;
    }

    put_le16(u8p + 0, ble_svc_gatt_start_handle);
    put_le16(u8p + 2, ble_svc_gatt_end_handle);
    return 0;
}

static int
ble_svc_gatt_client_supported_features_read_access(
                struct ble_gatt_access_ctxt *ctxt)
{
    int rc;

    rc = os_mbuf_append(ctxt->om, ble_svc_gatt_client_supported_features_val,
            sizeof(ble_svc_gatt_client_supported_features_val));

    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int
ble_svc_gatt_client_supported_features_write_access(
                        struct ble_gatt_access_ctxt *ctxt)
{
    uint16_t om_len;
    int rc;

    om_len = OS_MBUF_PKTLEN(ctxt->om);
    if (om_len > CLIENT_SUPPORTED_FEATURES_MAX_OCTETS) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    rc = ble_hs_mbuf_to_flat(ctxt->om,
                    ble_svc_gatt_client_supported_features_val, om_len, NULL);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

static int
ble_svc_gatt_database_hash_read_access(struct ble_gatt_access_ctxt *ctxt)
{
    int rc;
    uint8_t* ble_svc_gatt_database_hash_val;
    ble_svc_gatt_database_hash_val = ble_att_svr_get_database_hash();
    rc = os_mbuf_append(ctxt->om, ble_svc_gatt_database_hash_val,
            BLE_ATT_SVR_HASH_KEY_SIZE_IN_BYTES);

    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int
ble_svc_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid16;
    int rc;

    uuid16 = ble_uuid_u16(ctxt->chr->uuid);
    assert(uuid16 != 0);

    switch (uuid16) {
        case BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16:
            assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);
            rc =  ble_svc_gatt_service_changed_read_access(ctxt);
            return rc;
            break;
        case BLE_SVC_GATT_CHR_CLIENT_SUPPORTED_FEATURES_UUID16:
            if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
                rc = ble_svc_gatt_client_supported_features_read_access(ctxt);
            } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
                rc = ble_svc_gatt_client_supported_features_write_access(ctxt);
            } else {
                assert(0);
            }
            return rc;
            break;
        case BLE_SVC_GATT_CHR_DATABASE_HASH_UUID16:
            assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);
            ble_hs_conn_check_set_awareness_if_bonding(conn_handle, 1);
            rc =  ble_svc_gatt_database_hash_read_access(ctxt);
            return rc;
            break;
        default:
            assert(0);
            return BLE_ATT_ERR_UNLIKELY;
    }
}

/**
 * Indicates a change in attribute assignment to all subscribed peers.
 * Unconnected bonded peers receive an indication when they next connect.
 *
 * @param start_handle          The start of the affected handle range.
 * @param end_handle            The end of the affected handle range.
 */
void
ble_svc_gatt_changed(uint16_t start_handle, uint16_t end_handle)
{
    ble_svc_gatt_start_handle = start_handle;
    ble_svc_gatt_end_handle = end_handle;
    ble_gatts_chr_updated(ble_svc_gatt_changed_val_handle);
}


bool
ble_svc_gatt_is_client_robust_caching_supported(void)
{
   bool rc = ((ble_svc_gatt_client_supported_features_val[0] & (1 << ROBUST_CACHING)) == 1);
   return rc;
}

int
ble_svc_gatt_update_database_hash(void)
{
    int rc ;
    rc = ble_att_svr_calculate_database_hash();
    return rc;
}

static int
ble_svc_gatt_ind_ack_rx(struct ble_gap_event *event, void *arg)
{
    /* Only process connection termination events. */
    if (event->type == BLE_GAP_EVENT_NOTIFY_TX &&
            event->notify_tx.status == BLE_HS_EDONE &&
            event->notify_tx.attr_handle == ble_svc_gatt_changed_val_handle) {

        ble_hs_conn_check_set_awareness_if_bonding(
                event->notify_tx.conn_handle , 1);
    }

    return 0;
}

void
ble_svc_gatt_init(void)
{
    int rc;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    rc = ble_gatts_count_cfg(ble_svc_gatt_defs);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gatts_add_svcs(ble_svc_gatt_defs);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gap_event_listener_register(&ble_svc_gatt_gap_listener,
            ble_svc_gatt_ind_ack_rx, NULL);
    SYSINIT_PANIC_ASSERT(rc == 0);
}
