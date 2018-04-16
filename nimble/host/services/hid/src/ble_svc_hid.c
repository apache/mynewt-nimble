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

#include "services/hid/ble_svc_hid.h"

#include "sysinit/sysinit.h"
#include "syscfg/syscfg.h"
#include "host/ble_hs.h"


/* TODO: Multiple HID instances */
uint16_t ble_svc_hid_val_handle;

/* HID information */
static ble_svc_hid_hid_information_t ble_svc_hid_hid_info = {
    .hidver = USB_HID_VERSION_1_11;
    .countrycode = 0x00;
    .flags = BLE_SVC_HID_HID_INFO_FLAG_REMOTE_WAKE|BLE_SVC_HID_HID_INFO_FLAG_NORMALLY_CONNECTABLE
}

/* Access function */
static int
ble_svc_hid_access(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg);

/* Service and characteristics definition */
/* TODO: Confirm details of characteristic `Report` */
static const struct ble_gatt_svc_def ble_svc_hid_defs[] = {
    {
        /*** Service: Tx Power Service. */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_HID_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            /*** Characteristic: Protocol Mode. */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_HID_CHR_UUID16_PROTOCOL_MODE),
            .access_cb = ble_svc_hid_access,
            .val_handle = &ble_svc_hid_val_handle,
            .flags = BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_WRITE_NO_RSP,
        }, {
            /*** Characteristic: Report. */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_HID_CHR_UUID16_REPORT),
            .access_cb = ble_svc_hid_access,
            .val_handle = &ble_svc_hid_val_handle,
            .flags = BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_NOTIFY,
        }, {
            /*** Characteristic: Report Map. */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_HID_CHR_UUID16_REPORT_MAP),
            .access_cb = ble_svc_hid_access,
            .val_handle = &ble_svc_hid_val_handle,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            /*** Characteristic: Boot Keyboard Input Report. */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_HID_CHR_UUID16_BOOT_KEYBOARD_INPUT_REPORT),
            .access_cb = ble_svc_hid_access,
            .val_handle = &ble_svc_hid_val_handle,
            .flags = BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_NOTIFY,
        }, {
            /*** Characteristic: Boot Keyboard Output Report. */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_HID_CHR_UUID16_BOOT_KEYBOARD_OUTPUT_REPORT),
            .access_cb = ble_svc_hid_access,
            .val_handle = &ble_svc_hid_val_handle,
            .flags = BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_NO_RSP,
        }, {
            /*** Characteristic: Boot Mouse Input Report. */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_HID_CHR_UUID16_BOOT_MOUSE_INPUT_REPORT),
            .access_cb = ble_svc_hid_access,
            .val_handle = &ble_svc_hid_val_handle,
            .flags = BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_NOTIFY,
        }, {
            /*** Characteristic: Hid Information. */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_HID_CHR_UUID16_HID_INFORMATION),
            .access_cb = ble_svc_hid_access,
            .val_handle = &ble_svc_hid_val_handle,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            /*** Characteristic: Hid Control Point. */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_HID_CHR_UUID16_HID_CONTROL_POINT),
            .access_cb = ble_svc_hid_access,
            .val_handle = &ble_svc_hid_val_handle,
            .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
        }, {
            0, /* No more characteristics in this service. */
        } },
    },

    {
        0, /* No more services. */
    },
};

/*
 * Service API
 */

/*
 *  Set HID Information
 */
int
ble_svc_hid_set_hid_info(uint16_t ver,uint8_t cc, uint8_t flags) {
    int rc = 0;

    ble_svc_hid_hid_info.hidver=ver;
    ble_svc_hid_hid_info.countrycode= cc;
    ble_svc_hid_hid_info.flags = flags

    return rc;
}

/**
 * HID access function
 */
static int
ble_svc_hid_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt,
                          void *arg)
{
    uint16_t uuid16;
    int rc=0;

    uuid16 = ble_uuid_u16(ctxt->chr->uuid);
    assert(uuid16 != 0);

    switch (uuid16) {
    case BLE_SVC_HID_CHR_UUID16_PROTOCOL_MODE:
        if (ctxt->op == BLE_GATT_CHR_F_READ) {
        } else if (ctxt->op == BLE_GATT_CHR_F_WRITE_NO_RSP) {
        }
        return rc;

    case BLE_SVC_HID_CHR_UUID16_REPORT:
        if (ctxt->op == BLE_GATT_CHR_F_READ) {
        } else if (ctxt->op == BLE_GATT_CHR_F_NOTIFY) {
        }
        return rc;

    case BLE_SVC_HID_CHR_UUID16_REPORT_MAP:
        assert(ctxt->op == BLE_GATT_CHR_F_READ);
        return rc;

    case BLE_SVC_HID_CHR_UUID16_BOOT_KEYBOARD_INPUT_REPORT:
        if (ctxt->op == BLE_GATT_CHR_F_READ) {
        } else if (ctxt->op == BLE_GATT_CHR_F_NOTIFY) {
        }
        return rc;

    case BLE_SVC_HID_CHR_UUID16_BOOT_KEYBOARD_OUTPUT_REPORT:
        if (ctxt->op == BLE_GATT_CHR_F_READ) {
        } else if (ctxt->op == BLE_GATT_CHR_F_WRITE) {
        } else if (ctxt->op == BLE_GATT_CHR_F_WRITE_NO_RSP) {
        }
        return rc;

    case BLE_SVC_HID_CHR_UUID16_BOOT_MOUSE_INPUT_REPORT:
        if (ctxt->op == BLE_GATT_CHR_F_READ) {
        } else if (ctxt->op == BLE_GATT_CHR_F_NOTIFY) {
        }
        return rc;

    case BLE_SVC_HID_CHR_UUID16_HID_INFORMATION:
        assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);
        rc = os_mbuf_append(ctxt->om, &ble_svc_hid_hid_info,
                            sizeof(ble_svc_hid_hid_info));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    case BLE_SVC_HID_CHR_UUID16_HID_CONTROL_POINT:
        assert(ctxt->op == BLE_GATT_CHR_F_WRITE_NO_RSP);
        return rc;

    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/**
 * Initialize the HID service
 */
void
ble_svc_hid_init(void)
{
    int rc;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    rc = ble_gatts_count_cfg(ble_svc_hid_defs);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gatts_add_svcs(ble_svc_hid_defs);
    SYSINIT_PANIC_ASSERT(rc == 0);
}
