/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ble_gatt_priv.h"

int
ble_gatt_disc_all_services(uint16_t conn_handle, ble_gatt_disc_service_fn *cb,
                           void *cb_arg)
{
    return ble_gattc_disc_all_services(conn_handle, cb, cb_arg);
}

int
ble_gatt_disc_service_by_uuid(uint16_t conn_handle, void *service_uuid128,
                              ble_gatt_disc_service_fn *cb, void *cb_arg)
{
    return ble_gattc_disc_service_by_uuid(conn_handle, service_uuid128, cb,
                                          cb_arg);
}

int
ble_gatt_disc_all_chars(uint16_t conn_handle, uint16_t start_handle,
                        uint16_t end_handle, ble_gatt_chr_fn *cb,
                        void *cb_arg)
{
    return ble_gattc_disc_all_chars(conn_handle, start_handle, end_handle, cb,
                                    cb_arg);
}

int
ble_gatt_read(uint16_t conn_handle, uint16_t attr_handle,
              ble_gatt_attr_fn *cb, void *cb_arg)
{
    return ble_gattc_read(conn_handle, attr_handle, cb, cb_arg);
}

int
ble_gatt_write_no_rsp(uint16_t conn_handle, uint16_t attr_handle,
                      void *value, uint16_t value_len,
                      ble_gatt_attr_fn *cb, void *cb_arg)
{
    return ble_gattc_write_no_rsp(conn_handle, attr_handle, value, value_len,
                                  cb, cb_arg);
}

int
ble_gatt_write(uint16_t conn_handle, uint16_t attr_handle, void *value,
               uint16_t value_len, ble_gatt_attr_fn *cb, void *cb_arg)
{
    return ble_gattc_write(conn_handle, attr_handle, value, value_len, cb,
                           cb_arg);
}

int
ble_gatt_exchange_mtu(uint16_t conn_handle)
{
    return ble_gattc_exchange_mtu(conn_handle);
}

void
ble_gatt_connection_broken(uint16_t conn_handle)
{
    ble_gattc_connection_broken(conn_handle);
    /* XXX: Notify GATT server. */
}

int
ble_gatt_register_services(const struct ble_gatt_svc_def *svcs,
                           ble_gatt_register_fn *register_cb, void *cb_arg)
{
    return ble_gatts_register_services(svcs, register_cb, cb_arg);
}

int
ble_gatt_init(void)
{
    int rc;

    rc = ble_gattc_init();
    if (rc != 0) {
        return rc;
    }

    /* XXX: Init server. */

    return 0;
}
