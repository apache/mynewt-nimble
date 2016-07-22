/**
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
#include <string.h>
#include "host/ble_hs.h"
#include "profiles/ble_svc_lls.h"

/* Callback function */
ble_svc_lls_event_fn *cb_fn; 
/* Alert level */
uint8_t ble_svc_lls_alert_level;

/* Access function */
static int
ble_svc_lls_access(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def ble_svc_lls_defs[] = {
    {
        /*** Service: Link Loss Service (LLS). */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(BLE_SVC_LLS_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            /*** Characteristic: Alert Level. */
            .uuid128 = BLE_UUID16(BLE_SVC_LLS_CHR_UUID16_ALERT_LEVEL),
            .access_cb = ble_svc_lls_access,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
        }, {
            0, /* No more characteristics in this service. */
        } },
    },

    {
        0, /* No more services. */
    },
};

/**
 * Simple read/write access callback for the alert level
 * characteristic.
 */
static int
ble_svc_lls_access(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    assert(ctxt->chr == &ble_svc_lls_defs[0].characteristics[0]);
    switch(ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ctxt->att->read.data = &ble_svc_lls_alert_level;
        ctxt->att->read.len = sizeof ble_svc_lls_alert_level;
        break;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (ctxt->att->write.len != sizeof ble_svc_lls_alert_level) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        memcpy(&ble_svc_lls_alert_level, ctxt->att->write.data, 
            sizeof ble_svc_lls_alert_level);
        break;

    default:
        assert(0);
        break;
    }

    return 0;
}

/**
 * This function is the crux of the link loss service. The application
 * developer must call this function inside the gap event callback
 * function when a BLE_GAP_EVENT_DISCONNECT event is received. Here,
 * we then check if the disconnect reason is due to a timout, and if
 * so, we call the ble_svc_lls_event_fn callback with the current 
 * alert level. The actual alert implementation is left up to the 
 * developer.
 *
 * @param event                 The BLE GAP event from the GAP event
 *                                  callback.
 * @param arg                   The BLE GAP event arg from the GAP
 *                                  event callback.
 */
void
ble_svc_lls_on_gap_event(struct ble_gap_event *event, void *arg) {
    if(event->disconnect.reason == 
                BLE_HS_HCI_ERR(BLE_ERR_CONN_SPVN_TMO)) {
            cb_fn(ble_svc_lls_alert_level);
    } 
}

/**
 * Get the current alert level.
 */
uint8_t
ble_svc_lls_alert_level_get(void)
{
    return ble_svc_lls_alert_level;
}

/**
 * Set the current alert level.
 */
int
ble_svc_lls_alert_level_set(uint8_t alert_level)
{
    if (alert_level > BLE_SVC_LLS_ALERT_LEVEL_HIGH_ALERT) {
        return BLE_HS_EINVAL;
    }
    
    memcpy(&ble_svc_lls_alert_level, &alert_level, 
            sizeof alert_level);

    return 0;
}

/**
 * Registers the LLS with the GATT server.
 */
int
ble_svc_lls_register(void)
{
    int rc;

    rc = ble_gatts_register_svcs(ble_svc_lls_defs, NULL, NULL);
    return rc;
}

/**
 * Initialize the LLS. The developer must specify the event function
 * callback for the LLS to function properly.
 */
int
ble_svc_lls_init(struct ble_hs_cfg *cfg, uint8_t initial_alert_level,
                 ble_svc_lls_event_fn *cb)
{
    if (*cb) {
        return BLE_HS_EINVAL;
    }
    ble_svc_lls_alert_level = initial_alert_level;
    cb_fn = cb;

    int rc;
    rc = ble_gatts_count_cfg(ble_svc_lls_defs, cfg);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
